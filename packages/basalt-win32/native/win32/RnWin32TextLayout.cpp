#include "RnWin32TextLayout.h"

#include "Win32Offscreen.h"
#include "Win32Strings.h"

#include <windows.h>

#include <d2d1.h>
#include <d2d1_1.h>
#include <d2d1effects.h>
#include <dwrite.h>
// IDWriteTextLayout1, which is where character spacing lives: DirectWrite 1.1.
#include <dwrite_1.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace basalt::win32 {
namespace {

// Whether a run needs a brush of its own. Exact rather than tolerant: these
// numbers come from the same ColorComponents on both sides of the comparison,
// so anything but equality would be inventing a tolerance to hide a bug.
bool sameColour(const float a[4], const float b[4]) {
  return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

// One shared DirectWrite factory for the process.
//
// DWRITE_FACTORY_TYPE_SHARED rather than ISOLATED: the shared factory caches
// font data across everything in the process and is documented thread-safe,
// which is what lets measurement happen on Fabric's layout thread and painting
// on the UI thread with no lock between them. The GTK side cannot do this --
// see the header.
IDWriteFactory *dwriteFactory() {
  static IDWriteFactory *factory = [] {
    IDWriteFactory *created = nullptr;
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                        __uuidof(IDWriteFactory),
                        reinterpret_cast<IUnknown **>(&created));
    return created;
  }();
  return factory;
}

// DirectWrite's alignments are relative to the reading direction: LEADING is
// the right edge in a right-to-left paragraph. So the two physical alignments
// swap there, and the two relative ones do not have to be told the direction at
// all.
DWRITE_TEXT_ALIGNMENT toDWriteAlignment(RnTextAlign align, bool rightToLeft) {
  switch (align) {
    case RnTextAlign::Center:
      return DWRITE_TEXT_ALIGNMENT_CENTER;
    case RnTextAlign::Justified:
      return DWRITE_TEXT_ALIGNMENT_JUSTIFIED;
    case RnTextAlign::Right:
      return rightToLeft ? DWRITE_TEXT_ALIGNMENT_LEADING : DWRITE_TEXT_ALIGNMENT_TRAILING;
    case RnTextAlign::End:
      return DWRITE_TEXT_ALIGNMENT_TRAILING;
    case RnTextAlign::Natural:
      return DWRITE_TEXT_ALIGNMENT_LEADING;
    case RnTextAlign::Left:
      break;
  }
  return rightToLeft ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING;
}

// `fontVariant`, as DirectWrite takes it: an IDWriteTypography holding one
// feature per tag, set on a range.
//
// A tag is four characters packed into a DWORD, which is what
// `DWRITE_MAKE_OPENTYPE_TAG` does; core/FontVariants.h already produced the
// characters, so there is no table here. A feature's parameter is 1, meaning
// "on" -- the alternates that take a number are not among the ones React
// Native's `fontVariant` can name.
//
// The typography object is per range and released straight after: a layout
// takes its own reference, and the ranges of a paragraph rarely share a set of
// features.
// `letterSpacing`, which is `IDWriteTextLayout1`'s rather than
// `IDWriteTextLayout`'s: character spacing arrived with DirectWrite 1.1, so the
// layout is asked for the newer interface and the prop is simply not applied if
// a system answers no -- which no supported Windows does, and which is cheaper
// than a second code path.
//
// Trailing rather than leading, because that is where CSS puts
// `letter-spacing` and where iOS's kerning goes: the space follows each
// character, so a run of three characters is two gaps wide plus one at the end,
// and the other two hosts measure the same.
//
// `minimumAdvanceWidth` is zero, which is the documented "no minimum": a
// negative letter spacing is allowed to tighten a glyph's advance, which is
// what an app asking for -1 means, and clamping it to the glyph's own width
// would silently ignore the prop for small negatives.
void applyLetterSpacing(IDWriteTextLayout *layout,
                        const RnTextStyle &style,
                        DWRITE_TEXT_RANGE range) {
  if (style.letterSpacing == 0.0f) {
    return;
  }
  ComPtr<IDWriteTextLayout1> spacing;
  if (FAILED(layout->QueryInterface(IID_PPV_ARGS(spacing.GetAddressOf()))) || !spacing) {
    return;
  }
  spacing->SetCharacterSpacing(0.0f, style.letterSpacing, 0.0f, range);
}

void applyTypography(IDWriteTextLayout *layout,
                     const RnTextStyle &style,
                     DWRITE_TEXT_RANGE range) {
  if (style.fontFeatures.empty()) {
    return;
  }
  IDWriteFactory *factory = dwriteFactory();
  if (factory == nullptr) {
    return;
  }

  IDWriteTypography *typography = nullptr;
  if (FAILED(factory->CreateTypography(&typography)) || typography == nullptr) {
    return;
  }
  for (const std::string &tag : style.fontFeatures) {
    if (tag.size() != 4) {
      continue;
    }
    const DWRITE_FONT_FEATURE feature{
        static_cast<DWRITE_FONT_FEATURE_TAG>(
            DWRITE_MAKE_OPENTYPE_TAG(tag[0], tag[1], tag[2], tag[3])),
        1u};
    typography->AddFontFeature(feature);
  }
  layout->SetTypography(typography, range);
  typography->Release();
}

// What "unconstrained" means to DirectWrite. It has no notion of an infinite
// width and will happily produce NaN metrics from one, so this is a width no
// paragraph reaches rather than FLT_MAX.
constexpr float kUnconstrained = 1.0e6f;


// An inline `<View>` as DirectWrite wants it: an object that reports a size and
// draws nothing.
//
// `IDWriteTextLayout::SetInlineObject` is the only way to make room inside a
// paragraph, and it takes an interface rather than a rectangle -- so this is a
// COM object, which is three methods of bookkeeping and three of substance.
// The equivalents on the other two hosts are a Pango shape attribute and a Core
// Text run delegate, both of which are also "answer these metrics and draw
// nothing"; this one is just more typing.
//
// `Draw` does nothing on purpose. The view is a real view in the tree and the
// mounting manager paints it at the frame this layout reports, which is what
// keeps its background, its border and its own children working. An inline
// object that drew something would draw it twice.
class RnInlineObject final : public IDWriteInlineObject {
 public:
  RnInlineObject(float width, float height) : width_(width), height_(height) {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override {
    if (object == nullptr) {
      return E_POINTER;
    }
    if (riid == __uuidof(IDWriteInlineObject) || riid == __uuidof(IUnknown)) {
      *object = static_cast<IDWriteInlineObject *>(this);
      AddRef();
      return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
  }

  ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }

  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG remaining = --references_;
    if (remaining == 0) {
      delete this;
    }
    return remaining;
  }

  HRESULT STDMETHODCALLTYPE Draw(void *,
                                 IDWriteTextRenderer *,
                                 FLOAT,
                                 FLOAT,
                                 BOOL,
                                 BOOL,
                                 IUnknown *) override {
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetMetrics(DWRITE_INLINE_OBJECT_METRICS *metrics) override {
    if (metrics == nullptr) {
      return E_POINTER;
    }
    metrics->width = width_;
    metrics->height = height_;
    // The box's bottom on the text baseline, which is what a baseline equal to
    // the height means and where CSS puts an inline box that asked for nothing
    // else. Both other hosts place it the same way.
    metrics->baseline = height_;
    metrics->supportsSideways = FALSE;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetOverhangMetrics(DWRITE_OVERHANG_METRICS *overhangs) override {
    if (overhangs == nullptr) {
      return E_POINTER;
    }
    // Nothing spills outside the box: the view is clipped to its own frame by
    // everything above this.
    *overhangs = DWRITE_OVERHANG_METRICS{0.0f, 0.0f, 0.0f, 0.0f};
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetBreakConditions(DWRITE_BREAK_CONDITION *before,
                                               DWRITE_BREAK_CONDITION *after) override {
    // Neutral, which is what a replaced inline element is in CSS: a line may
    // break beside it if the text around it allows, and the object itself
    // neither demands nor forbids one.
    if (before != nullptr) {
      *before = DWRITE_BREAK_CONDITION_NEUTRAL;
    }
    if (after != nullptr) {
      *after = DWRITE_BREAK_CONDITION_NEUTRAL;
    }
    return S_OK;
  }

 private:
  std::atomic<ULONG> references_{1};
  float width_ = 0.0f;
  float height_ = 0.0f;
};

// --- The custom text renderer -----------------------------------------------
//
// Direct2D's own `DrawTextLayout` draws glyphs and, as its one special case, a
// drawing effect that happens to be an `ID2D1Brush` recolours its range. That
// is enough for per-run colour and nothing else, which is why three props sat
// recorded rather than done: a `<Text>`'s own `backgroundColor`, which
// DirectWrite never draws because it draws glyphs, and `textDecorationColor`
// and `textDecorationStyle`, because `SetUnderline` takes a boolean.
//
// `IDWriteTextLayout::Draw` with a renderer of our own answers all three. It is
// more code than a property would be, and it buys something neither other host
// can do: Pango's underline is an enum with no patterns and Core Text has no
// wavy, where a renderer can draw whatever a path can.
//
// What is *not* here: the text shadow still goes through `DrawTextLayout` into
// an offscreen, because only that bitmap's alpha is read and Direct2D's own
// renderer fills it faster than this one would. The consequence is that a
// shadow is cast by the glyphs and by a *solid* line of whatever decoration was
// asked for, never by the background box -- Direct2D draws no background
// either. A wavy underline over a shadow therefore has a straight shadow, which
// is a thing to notice rather than a thing to fix: both other hosts shadow the
// glyphs only, so this is already the closest of the three.

// What one run is painted with, carried as a drawing effect.
//
// A struct rather than a brush, which is the whole reason for the renderer: a
// brush can say what colour the glyphs are and nothing about the box behind
// them or the line under them.
struct RnRunPaint {
  float colour[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  bool hasBackground = false;
  float background[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  bool hasDecorationColour = false;
  float decorationColour[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  RnTextDecorationStyle decorationStyle = RnTextDecorationStyle::Solid;
};

// The paint as a drawing effect: an `IUnknown` the layout holds per range and
// hands back to each callback.
class RnWin32TextEffect final : public IUnknown {
 public:
  explicit RnWin32TextEffect(const RnRunPaint &paint) : paint_(paint) {}

  const RnRunPaint &paint() const { return paint_; }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override {
    if (object == nullptr) {
      return E_POINTER;
    }
    if (riid == __uuidof(IUnknown)) {
      *object = static_cast<IUnknown *>(this);
      AddRef();
      return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
  }

  ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }

  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG remaining = --references_;
    if (remaining == 0) {
      delete this;
    }
    return remaining;
  }

 private:
  std::atomic<ULONG> references_{1};
  RnRunPaint paint_;
};

// The stroke a dotted or dashed decoration is drawn with, or nothing for the
// styles that are not stroked.
//
// Direct2D's own DOT and DASH patterns, which are counted in multiples of the
// stroke's width -- so the dots of a 40pt underline are as far apart, in
// proportion, as the dots of a 12pt one, which is what a browser does and what
// a hand-written pattern would have had to reproduce. The round caps are not
// decoration: DOT is a zero-length dash, and a zero-length dash with a flat cap
// draws nothing at all.
ComPtr<ID2D1StrokeStyle> decorationDashes(ID2D1Factory *factory, RnTextDecorationStyle style) {
  ComPtr<ID2D1StrokeStyle> stroke;
  if (factory == nullptr
      || (style != RnTextDecorationStyle::Dotted && style != RnTextDecorationStyle::Dashed)) {
    return stroke;
  }
  const bool dots = style == RnTextDecorationStyle::Dotted;
  const D2D1_CAP_STYLE cap = dots ? D2D1_CAP_STYLE_ROUND : D2D1_CAP_STYLE_FLAT;
  const D2D1_STROKE_STYLE_PROPERTIES properties =
      D2D1::StrokeStyleProperties(cap,
                                  cap,
                                  cap,
                                  D2D1_LINE_JOIN_MITER,
                                  10.0f,
                                  dots ? D2D1_DASH_STYLE_DOT : D2D1_DASH_STYLE_DASH,
                                  0.0f);
  if (FAILED(factory->CreateStrokeStyle(properties, nullptr, 0, stroke.GetAddressOf()))) {
    stroke.Reset();
  }
  return stroke;
}

// The renderer itself. Lives for one `Draw` call, on the stack, which is why its
// reference counting is a formality: DirectWrite does not keep it.
class RnWin32TextRenderer final : public IDWriteTextRenderer {
 public:
  RnWin32TextRenderer(ID2D1RenderTarget *target, const RnRunPaint &fallback)
      : target_(target), fallback_(fallback) {
    target_->GetFactory(factory_.GetAddressOf());
  }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override {
    if (object == nullptr) {
      return E_POINTER;
    }
    if (riid == __uuidof(IDWriteTextRenderer) || riid == __uuidof(IDWritePixelSnapping)
        || riid == __uuidof(IUnknown)) {
      *object = static_cast<IDWriteTextRenderer *>(this);
      AddRef();
      return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
  }

  ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }

  ULONG STDMETHODCALLTYPE Release() override {
    // Deliberately does not delete: this is a stack object for the length of
    // one Draw, and DirectWrite balances its own references within the call.
    return --references_;
  }

  // --- IDWritePixelSnapping -------------------------------------------------

  HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void *, BOOL *disabled) override {
    if (disabled == nullptr) {
      return E_POINTER;
    }
    // Snapping on, which is what DrawTextLayout does: a baseline on a whole
    // pixel is the difference between crisp text and soft text.
    *disabled = FALSE;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetCurrentTransform(void *, DWRITE_MATRIX *transform) override {
    if (transform == nullptr) {
      return E_POINTER;
    }
    // The target's own, so text inside a rotated view snaps against the right
    // grid. The two types carry the same six numbers in the same order.
    D2D1_MATRIX_3X2_F current{};
    target_->GetTransform(&current);
    transform->m11 = current.m11;
    transform->m12 = current.m12;
    transform->m21 = current.m21;
    transform->m22 = current.m22;
    transform->dx = current.dx;
    transform->dy = current.dy;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void *, FLOAT *pixelsPerDip) override {
    if (pixelsPerDip == nullptr) {
      return E_POINTER;
    }
    // One: this host measures and draws in DIPs and lets the render target
    // carry the display scale, which is the division of labour
    // docs/DECISIONS.md records for Pango's absolute sizes.
    *pixelsPerDip = 1.0f;
    return S_OK;
  }

  // --- IDWriteTextRenderer --------------------------------------------------

  HRESULT STDMETHODCALLTYPE DrawGlyphRun(void *,
                                         FLOAT baselineOriginX,
                                         FLOAT baselineOriginY,
                                         DWRITE_MEASURING_MODE measuringMode,
                                         const DWRITE_GLYPH_RUN *glyphRun,
                                         const DWRITE_GLYPH_RUN_DESCRIPTION *,
                                         IUnknown *effect) override {
    if (glyphRun == nullptr) {
      return S_OK;
    }
    const RnRunPaint &paint = paintFor(effect);

    // The box behind the glyphs, which is the whole of `backgroundColor` on a
    // `<Text>`: DirectWrite draws no background, so the rectangle is filled
    // here and the run goes on top of it.
    //
    // The em box rather than the ink: ascent and descent come from the face's
    // own metrics, so two runs of the same size have backgrounds of the same
    // height whatever letters are in them -- which is what Pango's background
    // attribute and Core Text's filled rectangle both give.
    if (paint.hasBackground) {
      if (const ComPtr<ID2D1SolidColorBrush> brush = brushFor(paint.background)) {
        DWRITE_FONT_METRICS metrics{};
        if (glyphRun->fontFace != nullptr) {
          glyphRun->fontFace->GetMetrics(&metrics);
        }
        const float scale = metrics.designUnitsPerEm > 0
            ? glyphRun->fontEmSize / static_cast<float>(metrics.designUnitsPerEm)
            : 0.0f;
        const float ascent = static_cast<float>(metrics.ascent) * scale;
        const float descent = static_cast<float>(metrics.descent) * scale;
        float width = 0.0f;
        if (glyphRun->glyphAdvances != nullptr) {
          for (UINT32 index = 0; index < glyphRun->glyphCount; index++) {
            width += glyphRun->glyphAdvances[index];
          }
        }
        // A right-to-left run advances leftwards from its origin, which is what
        // an odd bidi level means.
        const float left =
            (glyphRun->bidiLevel & 1u) != 0u ? baselineOriginX - width : baselineOriginX;
        target_->FillRectangle(
            D2D1::RectF(left, baselineOriginY - ascent, left + width, baselineOriginY + descent),
            brush.Get());
      }
    }

    if (const ComPtr<ID2D1SolidColorBrush> brush = brushFor(paint.colour)) {
      target_->DrawGlyphRun(
          D2D1::Point2F(baselineOriginX, baselineOriginY), glyphRun, brush.Get(), measuringMode);
    }
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE DrawUnderline(void *,
                                          FLOAT baselineOriginX,
                                          FLOAT baselineOriginY,
                                          const DWRITE_UNDERLINE *underline,
                                          IUnknown *effect) override {
    if (underline == nullptr) {
      return S_OK;
    }
    drawDecoration(baselineOriginX,
                   baselineOriginY,
                   underline->width,
                   underline->thickness,
                   underline->offset,
                   underline->readingDirection,
                   paintFor(effect));
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE DrawStrikethrough(void *,
                                              FLOAT baselineOriginX,
                                              FLOAT baselineOriginY,
                                              const DWRITE_STRIKETHROUGH *strikethrough,
                                              IUnknown *effect) override {
    if (strikethrough == nullptr) {
      return S_OK;
    }
    drawDecoration(baselineOriginX,
                   baselineOriginY,
                   strikethrough->width,
                   strikethrough->thickness,
                   strikethrough->offset,
                   strikethrough->readingDirection,
                   paintFor(effect));
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE DrawInlineObject(void *context,
                                             FLOAT originX,
                                             FLOAT originY,
                                             IDWriteInlineObject *inlineObject,
                                             BOOL isSideways,
                                             BOOL isRightToLeft,
                                             IUnknown *effect) override {
    if (inlineObject == nullptr) {
      return S_OK;
    }
    // Asked rather than skipped, although this host's own inline objects draw
    // nothing: the trimming ellipsis DirectWrite makes for `numberOfLines` is
    // an inline object too, and that one does draw -- dropping this would lose
    // the ellipsis.
    return inlineObject->Draw(context, this, originX, originY, isSideways, isRightToLeft, effect);
  }

 private:
  // The paint a callback was handed, or the paragraph's own.
  //
  // A downcast rather than a QueryInterface, which is sound because of an
  // invariant one file wide: `RnWin32TextLayout::draw` is the only thing that
  // ever sets a drawing effect on a layout this renderer is given, and the only
  // thing it sets is an `RnWin32TextEffect`. DirectWrite hands back the same
  // pointer it was given.
  const RnRunPaint &paintFor(IUnknown *effect) const {
    if (effect != nullptr) {
      return static_cast<RnWin32TextEffect *>(effect)->paint();
    }
    return fallback_;
  }

  ComPtr<ID2D1SolidColorBrush> brushFor(const float colour[4]) const {
    ComPtr<ID2D1SolidColorBrush> brush;
    if (colour[3] <= 0.0f) {
      // Nothing to draw, and a transparent brush would still cost a draw call.
      return brush;
    }
    target_->CreateSolidColorBrush(
        D2D1::ColorF(colour[0], colour[1], colour[2], colour[3]), brush.GetAddressOf());
    return brush;
  }

  // One line, in whichever of the five styles was asked for.
  //
  // `offset` is DirectWrite's: positive is below the baseline for an underline
  // and negative above it for a strikethrough, so the same arithmetic serves
  // both and neither needs to know which it is.
  void drawDecoration(float baselineOriginX,
                      float baselineOriginY,
                      float width,
                      float thickness,
                      float offset,
                      DWRITE_READING_DIRECTION direction,
                      const RnRunPaint &paint) {
    const float *const colour = paint.hasDecorationColour ? paint.decorationColour : paint.colour;
    const ComPtr<ID2D1SolidColorBrush> brush = brushFor(colour);
    if (!brush || width <= 0.0f) {
      return;
    }
    const float line = thickness > 0.0f ? thickness : 1.0f;
    const float left = direction == DWRITE_READING_DIRECTION_RIGHT_TO_LEFT
        ? baselineOriginX - width
        : baselineOriginX;
    const float top = baselineOriginY + offset;

    switch (paint.decorationStyle) {
      case RnTextDecorationStyle::Solid:
        target_->FillRectangle(D2D1::RectF(left, top, left + width, top + line), brush.Get());
        return;

      case RnTextDecorationStyle::Double:
        // Two lines a line apart, which is what CSS's `double` is and what
        // Pango's DOUBLE draws. The second below the first, so the pair hangs
        // off the same offset a single line would have used.
        target_->FillRectangle(D2D1::RectF(left, top, left + width, top + line), brush.Get());
        target_->FillRectangle(
            D2D1::RectF(left, top + 2.0f * line, left + width, top + 3.0f * line), brush.Get());
        return;

      case RnTextDecorationStyle::Dotted:
      case RnTextDecorationStyle::Dashed: {
        // Stroked rather than filled, for the reason a dashed border is:
        // Direct2D's dash pattern is a property of a stroke. Down the middle of
        // the line, because a stroke straddles its path.
        const ComPtr<ID2D1StrokeStyle> dashes =
            decorationDashes(factory_.Get(), paint.decorationStyle);
        const float middle = top + line / 2.0f;
        target_->DrawLine(D2D1::Point2F(left, middle),
                          D2D1::Point2F(left + width, middle),
                          brush.Get(),
                          line,
                          dashes.Get());
        return;
      }

      case RnTextDecorationStyle::Wavy:
        drawWave(left, top, width, line, brush.Get());
        return;
    }
  }

  // A squiggle, which is the one style no other host here can draw: Pango has
  // PANGO_UNDERLINE_ERROR and Core Text has nothing, so a renderer that takes a
  // path is what makes `wavy` possible at all.
  //
  // Quadratics alternating above and below the line, a wavelength of six times
  // the thickness, which is about what a browser draws. Stroked at the line's
  // own thickness, so a wavy underline is as heavy as a solid one.
  //
  // The control point is *twice* the amplitude away from the line, because a
  // quadratic only reaches halfway to it: the first version put the control
  // point at one thickness and drew a wave of half that, which CI measured as
  // an edge that moves by two pixels where the test asked for three. That is
  // the whole reason this number is 2.0 and not 1.0.
  void drawWave(float left, float top, float width, float thickness, ID2D1Brush *brush) {
    if (!factory_) {
      return;
    }
    ComPtr<ID2D1PathGeometry> path;
    if (FAILED(factory_->CreatePathGeometry(path.GetAddressOf())) || !path) {
      return;
    }
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(path->Open(sink.GetAddressOf())) || !sink) {
      return;
    }

    const float wave = thickness * 6.0f;
    const float middle = top + thickness / 2.0f;
    sink->BeginFigure(D2D1::Point2F(left, middle), D2D1_FIGURE_BEGIN_HOLLOW);
    bool up = true;
    for (float x = left; x < left + width; x += wave) {
      const float next = std::min(x + wave, left + width);
      const float peak = middle + 2.0f * (up ? -thickness : thickness);
      sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(
          D2D1::Point2F((x + next) / 2.0f, peak), D2D1::Point2F(next, middle)));
      up = !up;
    }
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    if (FAILED(sink->Close())) {
      return;
    }
    target_->DrawGeometry(path.Get(), brush, thickness);
  }

  ID2D1RenderTarget *target_ = nullptr;
  ComPtr<ID2D1Factory> factory_;
  RnRunPaint fallback_;
  std::atomic<ULONG> references_{1};
};

// The paint a style asks for.
RnRunPaint paintOf(const RnTextStyle &style) {
  RnRunPaint paint;
  for (int component = 0; component < 4; component++) {
    paint.colour[component] = style.color[component];
    paint.background[component] = style.backgroundColour[component];
    paint.decorationColour[component] = style.decorationColour[component];
  }
  paint.hasBackground = style.hasBackgroundColour;
  paint.hasDecorationColour = style.hasDecorationColour;
  paint.decorationStyle = style.decorationStyle;
  return paint;
}

// Where the baseline of the line a text position falls on is, measured from the
// top of the whole paragraph.
//
// From the line metrics alone rather than from the hit test, which is the one
// decision in here worth stating: `HitTestTextPosition` also answers a Y, and
// what that Y is relative to is not a thing to be wrong about silently -- the
// first version of this added a line's baseline to it and put a short box below
// the paragraph, which the baseline test caught. Line metrics are unambiguous:
// each line has a height and a baseline measured from its own top, so the sum of
// the heights before a line plus that line's baseline is where the baseline is.
float baselineForPosition(const std::vector<DWRITE_LINE_METRICS> &lines, unsigned position) {
  unsigned at = 0;
  float top = 0.0f;
  for (size_t index = 0; index < lines.size(); index++) {
    const unsigned end = at + lines[index].length;
    if (position < end || index + 1 == lines.size()) {
      return top + lines[index].baseline;
    }
    at = end;
    top += lines[index].height;
  }
  return 0.0f;
}

} // namespace

std::shared_ptr<RnWin32TextLayout>
RnWin32TextLayout::create(std::string utf8Text, const RnTextStyle &style, int maximumNumberOfLines) {
  IDWriteFactory *factory = dwriteFactory();
  if (factory == nullptr) {
    return nullptr;
  }

  const std::wstring family = widen(style.fontFamily);

  IDWriteTextFormat *format = nullptr;
  const HRESULT hr = factory->CreateTextFormat(
      family.empty() ? L"Segoe UI" : family.c_str(),
      nullptr, // the system font collection
      style.bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
      style.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
      DWRITE_FONT_STRETCH_NORMAL,
      // In DIPs at 96 dpi, which is React Native's density-independent pixel
      // exactly. Nothing here multiplies by a scale factor, and nothing should:
      // the render target carries the display scale, the same division of
      // labour `docs/DECISIONS.md` records for Pango's absolute sizes.
      style.fontSize,
      L"",
      &format);
  if (FAILED(hr) || format == nullptr) {
    return nullptr;
  }

  format->SetTextAlignment(toDWriteAlignment(style.align, style.rightToLeft));
  // The paragraph's direction, which is a format property rather than a range
  // one: DirectWrite has no per-run reading direction, and neither has React
  // Native -- `writingDirection` is a paragraph's. The first run's style is
  // what the format is built from, which is where the alignment and the line
  // height already come from.
  format->SetReadingDirection(style.rightToLeft ? DWRITE_READING_DIRECTION_RIGHT_TO_LEFT
                                                : DWRITE_READING_DIRECTION_LEFT_TO_RIGHT);
  format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
  format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
  if (style.lineHeight > 0.0f) {
    // Baseline at 80% of the line box is DirectWrite's own recommendation for
    // uniform spacing, and it is what an unset baseline would have produced.
    format->SetLineSpacing(
        DWRITE_LINE_SPACING_METHOD_UNIFORM, style.lineHeight, style.lineHeight * 0.8f);
  }

  auto layout = std::shared_ptr<RnWin32TextLayout>(new RnWin32TextLayout());
  layout->utf8_ = std::move(utf8Text);
  layout->utf16_ = widen(layout->utf8_);
  layout->style_ = style;
  layout->maximumNumberOfLines_ = maximumNumberOfLines > 0 ? maximumNumberOfLines : 0;
  layout->format_ = format;
  return layout;
}

std::shared_ptr<RnWin32TextLayout>
RnWin32TextLayout::createFromRuns(const std::vector<RnTextRun> &runs, int maximumNumberOfLines) {
  if (runs.empty()) {
    return create(std::string{}, RnTextStyle{}, maximumNumberOfLines);
  }

  // The first run's style is the paragraph's: alignment and line spacing belong
  // to the whole thing rather than to a span, and React Native applies them
  // that way too.
  std::string joined;
  for (const auto &run : runs) {
    joined += run.text;
  }

  auto layout = create(joined, runs.front().style, maximumNumberOfLines);
  if (layout == nullptr) {
    return nullptr;
  }

  // One style needs no ranges, and skipping them keeps the common case -- a
  // plain <Text> -- free of per-range work. An attachment is the exception: its
  // box is attached to a range, so the runs have to be resolved even when there
  // is only one of them.
  const bool anyAttachment = std::any_of(runs.begin(), runs.end(), [](const RnTextRun &run) {
    return run.inlineBox.has_value();
  });
  if (runs.size() == 1 && !anyAttachment) {
    return layout;
  }

  // Ranges are counted in UTF-16 code units, not bytes and not code points, so
  // each run's extent has to be measured after conversion. Getting this wrong
  // shifts every style after the first emoji.
  unsigned start = 0;
  for (const auto &run : runs) {
    const unsigned length = static_cast<unsigned>(widen(run.text).size());
    layout->runs_.push_back(ResolvedRun{start, length, run.style, run.inlineBox});
    start += length;
  }
  return layout;
}

RnWin32TextLayout::~RnWin32TextLayout() {
  if (format_ != nullptr) {
    format_->Release();
    format_ = nullptr;
  }
}

IDWriteTextLayout *RnWin32TextLayout::buildLayout(float maxWidth, float maxHeight) const {
  IDWriteFactory *factory = dwriteFactory();
  if (factory == nullptr || format_ == nullptr) {
    return nullptr;
  }

  const float width = maxWidth < 0.0f ? kUnconstrained : maxWidth;
  const float height = maxHeight < 0.0f ? kUnconstrained : maxHeight;

  IDWriteTextLayout *layout = nullptr;
  const HRESULT hr = factory->CreateTextLayout(
      utf16_.c_str(), static_cast<UINT32>(utf16_.size()), format_, width, height, &layout);
  if (FAILED(hr)) {
    return nullptr;
  }

  // Per-span styling, applied here rather than at creation so that measurement
  // and painting see exactly the same runs -- which is the whole reason this
  // function exists.
  for (const auto &run : runs_) {
    const DWRITE_TEXT_RANGE range{run.start, run.length};
    const std::wstring family = widen(run.style.fontFamily);
    if (!family.empty()) {
      layout->SetFontFamilyName(family.c_str(), range);
    }
    layout->SetFontSize(run.style.fontSize, range);
    layout->SetFontWeight(
        run.style.bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL, range);
    layout->SetFontStyle(
        run.style.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL, range);
    if (run.style.underline) {
      layout->SetUnderline(TRUE, range);
    }
    if (run.style.strikethrough) {
      layout->SetStrikethrough(TRUE, range);
    }
    applyTypography(layout, run.style, range);
    applyLetterSpacing(layout, run.style, range);
  }

  // The inline boxes, which have to come after the per-run styling: setting a
  // font size over a range that carries an inline object would otherwise be the
  // last word on how tall the line is.
  //
  // One object per attachment rather than one shared between them, because each
  // carries its own size. The layout takes a reference and this drops its own,
  // so the object lives exactly as long as the layout does.
  for (const auto &run : runs_) {
    if (!run.inlineBox.has_value()) {
      continue;
    }
    ComPtr<IDWriteInlineObject> object;
    object.Attach(new RnInlineObject(run.inlineBox->width, run.inlineBox->height));
    layout->SetInlineObject(object.Get(), DWRITE_TEXT_RANGE{run.start, run.length});
  }

  // The single-style paragraph has no runs at all: its font, size and weight
  // live on the text format. A format has no underline or strikethrough
  // property, though -- they exist only on a layout and only per range -- so
  // the whole string is the range here.
  if (runs_.empty()) {
    const DWRITE_TEXT_RANGE whole{0, static_cast<UINT32>(utf16_.size())};
    if (style_.underline) {
      layout->SetUnderline(TRUE, whole);
    }
    if (style_.strikethrough) {
      layout->SetStrikethrough(TRUE, whole);
    }
    applyTypography(layout, style_, whole);
    applyLetterSpacing(layout, style_, whole);
  }

  return layout;
}

float RnWin32TextLayout::applyLineLimit(IDWriteTextLayout *layout) const {
  if (layout == nullptr || maximumNumberOfLines_ <= 0) {
    return -1.0f;
  }

  UINT32 lineCount = 0;
  // The documented two-call form: the first is expected to fail with
  // E_NOT_SUFFICIENT_BUFFER and to fill in the count.
  layout->GetLineMetrics(nullptr, 0, &lineCount);
  if (lineCount == 0) {
    return -1.0f;
  }

  std::vector<DWRITE_LINE_METRICS> lines(lineCount);
  if (FAILED(layout->GetLineMetrics(lines.data(), lineCount, &lineCount))) {
    return -1.0f;
  }

  const UINT32 limit = static_cast<UINT32>(maximumNumberOfLines_);
  if (lineCount <= limit) {
    // A limit the paragraph already fits inside is not a truncation, and must
    // not become one. This is the shape of the trap `docs/DECISIONS.md` records
    // Pango springing: there, setting the *default* ellipsize mode with no
    // height collapsed every wrapping paragraph to a single line. DirectWrite
    // will not trim without a height, so declining to set one here is what
    // makes the same default safe.
    return -1.0f;
  }

  float limitHeight = 0.0f;
  for (UINT32 i = 0; i < limit; i++) {
    limitHeight += lines[i].height;
  }

  layout->SetMaxHeight(limitHeight);

  // An ellipsis on the last line that fits, which is React Native's default
  // `ellipsizeMode: 'tail'`.
  ComPtr<IDWriteInlineObject> ellipsis;
  if (SUCCEEDED(dwriteFactory()->CreateEllipsisTrimmingSign(layout, ellipsis.GetAddressOf()))) {
    DWRITE_TRIMMING trimming{};
    trimming.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
    layout->SetTrimming(&trimming, ellipsis.Get());
  }
  return limitHeight;
}

RnTextSize RnWin32TextLayout::measure(float maxWidth) const {
  ComPtr<IDWriteTextLayout> layout;
  layout.Attach(buildLayout(maxWidth, -1.0f));
  if (!layout) {
    return {};
  }

  const float limitHeight = applyLineLimit(layout.Get());

  DWRITE_TEXT_METRICS metrics{};
  if (FAILED(layout->GetMetrics(&metrics))) {
    return {};
  }

  // `width` rather than `widthIncludingTrailingWhitespace`, so a trailing space
  // does not widen the box -- which is what every other platform reports and
  // what makes empty text measure to zero.
  RnTextSize size{metrics.width, metrics.height};
  if (limitHeight >= 0.0f) {
    size.height = std::min(size.height, limitHeight);
  }
  return size;
}

// Where each inline box landed.
//
// Read back rather than computed, which is the whole reason the box goes through
// DirectWrite at all: the paragraph decides where a line breaks, how the line is
// aligned and how a right-to-left run is ordered, and an attachment moves with
// all three.
//
// `HitTestTextPosition` answers the leading edge of a character, which is the
// horizontal half. The vertical half is the line metrics: the inline object's
// own baseline is its height, so the box hangs above the baseline of its line,
// and that baseline is the heights of the lines before it plus its own baseline
// offset.
std::vector<RnAttachmentBox> RnWin32TextLayout::attachmentBoxes(float maxWidth) const {
  std::vector<RnAttachmentBox> boxes;
  const bool any = std::any_of(runs_.begin(), runs_.end(), [](const ResolvedRun &run) {
    return run.inlineBox.has_value();
  });
  if (!any) {
    return boxes;
  }

  ComPtr<IDWriteTextLayout> layout;
  layout.Attach(buildLayout(maxWidth, -1.0f));
  if (!layout) {
    // No DirectWrite at all. The sizes are still what React Native asked for,
    // which keeps the count right and puts every box at the origin -- the state
    // this host was in before any of this, and better than reporting nothing.
    for (const auto &run : runs_) {
      if (run.inlineBox.has_value()) {
        boxes.push_back(RnAttachmentBox{0.0f, 0.0f, run.inlineBox->width, run.inlineBox->height});
      }
    }
    return boxes;
  }
  applyLineLimit(layout.Get());

  UINT32 lineCount = 0;
  layout->GetLineMetrics(nullptr, 0, &lineCount);
  std::vector<DWRITE_LINE_METRICS> lines(lineCount);
  if (lineCount > 0 && FAILED(layout->GetLineMetrics(lines.data(), lineCount, &lineCount))) {
    lines.clear();
  }

  for (const auto &run : runs_) {
    if (!run.inlineBox.has_value()) {
      continue;
    }
    RnAttachmentBox box;
    box.width = run.inlineBox->width;
    box.height = run.inlineBox->height;

    // The hit test for the horizontal position, which it is the only answer to:
    // where along the line the box landed depends on the text before it, the
    // alignment and the reading direction. The vertical comes from the line
    // metrics; see `baselineForPosition`.
    float pointX = 0.0f;
    float pointY = 0.0f;
    DWRITE_HIT_TEST_METRICS metrics{};
    if (SUCCEEDED(layout->HitTestTextPosition(run.start, FALSE, &pointX, &pointY, &metrics))) {
      box.x = pointX;
    }
    box.y = baselineForPosition(lines, run.start) - run.inlineBox->height;
    boxes.push_back(box);
  }
  return boxes;
}

void RnWin32TextLayout::setShadow(float dx,
                                  float dy,
                                  float standardDeviation,
                                  const float colour[4]) {
  shadowDx_ = dx;
  shadowDy_ = dy;
  shadowStandardDeviation_ = standardDeviation > 0.0f ? standardDeviation : 0.0f;
  for (int component = 0; component < 4; component++) {
    shadowColour_[component] = colour != nullptr ? colour[component] : 0.0f;
  }
}

// The shadow, under the text.
//
// A blurred shadow is an effect, and an effect needs an `ID2D1DeviceContext`
// where the view layer hands over an `ID2D1RenderTarget`. A render target made
// by a Direct2D 1.1 factory answers that interface, and both of this host's
// targets are -- the window's and the suite's WIC bitmap -- so this asks rather
// than threading a second type through the view layer.
//
// The shape is the one `CLSID_D2D1Shadow` wants: the text is drawn into a
// compatible bitmap, the effect blurs that bitmap's *alpha* and colours it, and
// the result is drawn at the offset. Which is why the text in the bitmap is
// drawn in the shadow's own colour only as a formality; the effect's colour
// property is what decides it.
//
// Without a device context there is no blur to be had, and the fallback is the
// honest one: the same text drawn once at the offset in the shadow colour,
// which is a hard shadow. backlog/platform-windows.md records that.
void RnWin32TextLayout::drawShadow(ID2D1RenderTarget *target,
                                   IDWriteTextLayout *layout,
                                   float width,
                                   float height) const {
  const D2D1_COLOR_F colour = D2D1::ColorF(
      shadowColour_[0], shadowColour_[1], shadowColour_[2], shadowColour_[3]);

  ComPtr<ID2D1DeviceContext> context;
  const bool haveContext = SUCCEEDED(target->QueryInterface(IID_PPV_ARGS(&context)));

  if (!haveContext || shadowStandardDeviation_ <= 0.0f) {
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(target->CreateSolidColorBrush(colour, brush.GetAddressOf()))) {
      return;
    }
    target->DrawTextLayout(D2D1::Point2F(shadowDx_, shadowDy_),
                           layout,
                           brush.Get(),
                           D2D1_DRAW_TEXT_OPTIONS_NONE);
    return;
  }

  // The bitmap is the size of the box the text is laid out in. A shadow spreads
  // beyond the glyphs, so the blur is given room by drawing the result at an
  // offset rather than by growing the bitmap: the effect's own output is larger
  // than its input and Direct2D composites all of it.
  // Asked for with a format that keeps alpha rather than inheriting the
  // window's, which has none: the shadow is a blur of this bitmap's alpha, so
  // an opaque one would be a filled box. See Win32Offscreen.h.
  const ComPtr<ID2D1BitmapRenderTarget> offscreen = createOffscreen(target, width, height);
  if (!offscreen) {
    return;
  }

  ComPtr<ID2D1SolidColorBrush> opaque;
  if (FAILED(offscreen->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black, 1.0f),
                                              opaque.GetAddressOf()))) {
    return;
  }
  offscreen->BeginDraw();
  offscreen->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
  offscreen->DrawTextLayout(
      D2D1::Point2F(0.0f, 0.0f), layout, opaque.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
  if (FAILED(offscreen->EndDraw())) {
    return;
  }

  ComPtr<ID2D1Bitmap> glyphs;
  if (FAILED(offscreen->GetBitmap(glyphs.GetAddressOf())) || !glyphs) {
    return;
  }

  ComPtr<ID2D1Effect> shadow;
  if (FAILED(context->CreateEffect(CLSID_D2D1Shadow, shadow.GetAddressOf())) || !shadow) {
    return;
  }
  shadow->SetInput(0, glyphs.Get());
  shadow->SetValue(D2D1_SHADOW_PROP_BLUR_STANDARD_DEVIATION, shadowStandardDeviation_);
  const D2D1_VECTOR_4F tint{colour.r, colour.g, colour.b, colour.a};
  shadow->SetValue(D2D1_SHADOW_PROP_COLOR, tint);

  context->DrawImage(shadow.Get(), D2D1::Point2F(shadowDx_, shadowDy_));
}

void RnWin32TextLayout::draw(ID2D1RenderTarget *target, float width, float height) const {
  if (target == nullptr) {
    return;
  }

  ComPtr<IDWriteTextLayout> layout;
  layout.Attach(buildLayout(width, height));
  if (!layout) {
    return;
  }
  applyLineLimit(layout.Get());

  // The shadow first, so the glyphs land on top of it, and before any drawing
  // effect is attached: it goes through Direct2D's own `DrawTextLayout`, which
  // reads a drawing effect only when it is an `ID2D1Brush`, and the effects
  // below are not.
  if (hasShadow()) {
    drawShadow(target, layout.Get(), width, height);
  }

  // What each run is painted with, attached to the layout as a drawing effect
  // and handed back per glyph run, underline and strikethrough.
  //
  // Only the runs that differ from the paragraph's own style get one; the rest
  // fall through to the renderer's fallback, which is one fewer allocation for
  // the common paragraph where every fragment looks the same. The effects have
  // to outlive `Draw`, which is why they are held in a vector rather than
  // created and dropped inside the loop.
  const RnRunPaint fallback = paintOf(style_);
  std::vector<ComPtr<IUnknown>> effects;
  for (const ResolvedRun &run : runs_) {
    if (run.length == 0) {
      continue;
    }
    if (sameColour(run.style.color, style_.color) && !run.style.hasBackgroundColour
        && !run.style.hasDecorationColour
        && run.style.decorationStyle == style_.decorationStyle) {
      continue;
    }
    ComPtr<IUnknown> effect;
    effect.Attach(new RnWin32TextEffect(paintOf(run.style)));
    layout->SetDrawingEffect(effect.Get(), DWRITE_TEXT_RANGE{run.start, run.length});
    effects.push_back(std::move(effect));
  }

  RnWin32TextRenderer renderer(target, fallback);
  layout->Draw(nullptr, &renderer, 0.0f, 0.0f);
}

} // namespace basalt::win32
