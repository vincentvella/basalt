// A laid-out paragraph, and nothing that knows what React Native is.
//
// Deliberately separate from whatever turns an `AttributedString` into one of
// these, for the same reason `appkit/RnTextLayout.h` is separate from
// `CoreTextLayout.h`: the view layer draws paragraphs and must not gain a React
// Native dependency to do it. `basalt_win32_view` builds and is tested on a
// Windows box with nothing but a compiler and the SDK, and that is worth
// keeping -- it is the whole reason there is a test suite on this platform
// before there is a host.
//
// One object serves measurement and painting. That is not tidiness: if the two
// built layouts differently -- a different default font, a different wrap mode
// -- Yoga would allot a box computed one way and the view would paint text laid
// out another, and the result is clipped or overlapping text that looks like a
// rendering bug rather than a measurement one. The GTK side makes the same
// argument in `docs/DECISIONS.md`.
//
// Unlike Pango, DirectWrite needs no mutex around any of this. Pango's font map
// is not documented as reentrant, so GTK serialises every measurement on one
// lock and hides the cost behind a cache; a DWRITE_FACTORY_TYPE_SHARED factory
// is documented thread-safe, and this builds a fresh IDWriteTextLayout per call
// rather than mutating a held one. Fabric's layout thread and the UI thread can
// therefore measure at the same time, which on GTK they cannot.

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

// Forward-declared rather than included, so a translation unit that only builds
// or measures a paragraph does not pull in <d2d1.h> and <dwrite.h> and
// windows.h behind them. The tests do exactly that.
struct ID2D1RenderTarget;
struct IDWriteTextFormat;
struct IDWriteTextLayout;

namespace basalt::win32 {

enum class RnTextAlign {
  // Physical edges, which is what React Native's `left` and `right` mean: they
  // stay where they are in a right-to-left paragraph, as they do on the other
  // two hosts.
  Left,
  Center,
  Right,
  Justified,
  // The reading direction's own edges, which is what `natural`, `start` and
  // `end` ask for. DirectWrite expresses these directly, as LEADING and
  // TRAILING, so a natural alignment follows the direction without this file
  // having to know which way that is.
  Natural,
  End,
};

// `textDecorationStyle`, as React Native's five.
//
// DirectWrite has none of them: `SetUnderline` takes a boolean, so the style is
// drawn rather than asked for -- which is what the custom text renderer in
// RnWin32TextLayout.cpp is for, and why this host ends up with all five where
// Pango has three and Core Text four.
enum class RnTextDecorationStyle {
  Solid,
  Double,
  Dotted,
  Dashed,
  Wavy,
};

// The attributes this platform honours. React Native has many more; each one
// added here is a line in `DirectWriteLayout`, which is the file that will
// translate an AttributedString once there is a mounting manager to deliver
// one.
struct RnTextStyle {
  std::string fontFamily = "Segoe UI";
  float fontSize = 14.0f;
  bool bold = false;
  bool italic = false;
  // 0 means the font's own line spacing, which is what React Native means by an
  // unset lineHeight.
  float lineHeight = 0.0f;
  RnTextAlign align = RnTextAlign::Left;
  float color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  // `textDecorationLine`. `SetUnderline` and `SetStrikethrough` take a range
  // and a boolean and nothing else, so these two say *whether* there is a line
  // and the renderer in RnWin32TextLayout.cpp decides what it looks like: it is
  // handed the position and thickness DirectWrite computed from the font and
  // draws the line itself.
  bool underline = false;
  bool strikethrough = false;
  // `textDecorationColor` and `textDecorationStyle`. Unset colour means the
  // text's own, which is React Native's default and what the other two hosts
  // do; the style is drawn by the renderer because DirectWrite has no form of
  // it. See core/TextDecorations.h, which resolves both for all three hosts.
  bool hasDecorationColour = false;
  float decorationColour[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  RnTextDecorationStyle decorationStyle = RnTextDecorationStyle::Solid;
  // `backgroundColor` on a `<Text>` fragment: the box behind the glyphs, which
  // DirectWrite also does not draw -- it draws glyphs and nothing else. Pango
  // has a background attribute and Core Text fills the rectangle itself.
  bool hasBackgroundColour = false;
  float backgroundColour[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  // `writingDirection`, which decides the order of the glyphs in a line and
  // which edge the line starts from. False is left-to-right, and is also what
  // `natural` resolves to for Latin text.
  bool rightToLeft = false;
  // `letterSpacing`, in the same points everything else here is in. Zero is
  // React Native's unset, which is also "no extra space", so the two need no
  // distinction -- unlike `lineHeight`, where zero means "the font's own".
  //
  // Added *after* each character, which is what CSS's letter-spacing and iOS's
  // kerning both do: DirectWrite calls that trailing spacing.
  float letterSpacing = 0.0f;
  // `fontVariant`, as OpenType tags: "smcp", "tnum", "ss07". Resolved by
  // `core/FontVariants.h`, which the GTK host uses in the same form; DirectWrite
  // takes them as a `DWRITE_FONT_FEATURE_TAG` each, on an `IDWriteTypography`.
  //
  // Whether a font actually has a feature is the font's business: DirectWrite
  // asks for it and a face without it renders unchanged, which is why nothing
  // here asserts on the pixels.
  std::vector<std::string> fontFeatures;
};

struct RnTextSize {
  float width = 0.0f;
  float height = 0.0f;
};

// An inline `<View>` inside a `<Text>`, as the text layout sees it: a box that
// occupies space and draws nothing.
//
// React Native has already laid the view out and the mounting manager will
// paint it, so all the paragraph has to do is reserve the room and say where it
// landed. The box's *bottom* sits on the text baseline, which is where CSS puts
// an inline box with no explicit vertical alignment and what the other two
// hosts' shape attributes and run delegates do.
struct RnInlineBox {
  float width = 0.0f;
  float height = 0.0f;
};

// One styled span of a paragraph.
//
// React Native's `<Text>` is not one string with one style: a
// ParagraphShadowNode folds its whole subtree into an AttributedString of
// fragments, each with its own font, size and weight. A layout built from only
// the first fragment's style renders `Hello <b>world</b>` entirely unbold,
// which measures wrong as well as looking wrong.
struct RnTextRun {
  std::string text;
  RnTextStyle style;
  // Set when this run is an attachment rather than text. Its `text` is then
  // React Native's own placeholder character, which is what the inline object
  // is attached to and is never drawn.
  std::optional<RnInlineBox> inlineBox;
};

// Where an inline box landed, in the paragraph's own coordinates.
struct RnAttachmentBox {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
};

class RnWin32TextLayout {
 public:
  // `maximumNumberOfLines` of 0 means no limit, matching
  // ParagraphAttributes::maximumNumberOfLines. Returns null if DirectWrite
  // could not produce a text format, which in practice means the process has no
  // DirectWrite at all.
  static std::shared_ptr<RnWin32TextLayout>
  create(std::string utf8Text, const RnTextStyle &style, int maximumNumberOfLines);

  // The same thing for a paragraph of differently styled spans. The first run's
  // style sets the paragraph defaults -- alignment and line height, which
  // DirectWrite has no per-range form of -- and each run's font family, size,
  // weight, slant and colour are applied over its own character range.
  //
  // Colour is the odd one, and `draw` is where it happens: DirectWrite carries
  // it as a drawing effect rather than as a range attribute, and a drawing
  // effect is only meaningful to whoever renders the layout.
  static std::shared_ptr<RnWin32TextLayout>
  createFromRuns(const std::vector<RnTextRun> &runs, int maximumNumberOfLines);

  ~RnWin32TextLayout();

  RnWin32TextLayout(const RnWin32TextLayout &) = delete;
  RnWin32TextLayout &operator=(const RnWin32TextLayout &) = delete;

  // The original UTF-8, for `describeTree`. Kept alongside the UTF-16 copy
  // DirectWrite needs so that the cross-host dump does not depend on a
  // conversion round-tripping.
  const std::string &text() const { return utf8_; }

  const RnTextStyle &style() const { return style_; }
  int maximumNumberOfLines() const { return maximumNumberOfLines_; }

  // `textShadowColor`, `textShadowOffset` and `textShadowRadius`: one shadow
  // for the whole paragraph, resolved by `core/TextShadows.h`.
  //
  // One rather than one per fragment, which is the same decision both other
  // hosts made for the same reason: neither engine can draw a different shadow
  // per run, so the first fragment that asks for one decides it. `sd` is the
  // gaussian's standard deviation, which is what Direct2D's shadow effect takes
  // too, so nothing converts here.
  void setShadow(float dx, float dy, float standardDeviation, const float colour[4]);
  bool hasShadow() const { return shadowColour_[3] > 0.0f; }
  float shadowDx() const { return shadowDx_; }
  float shadowDy() const { return shadowDy_; }
  float shadowStandardDeviation() const { return shadowStandardDeviation_; }
  const float *shadowColour() const { return shadowColour_; }

  // The size this paragraph needs at `maxWidth`. Pass a negative width for
  // unconstrained. In React Native's density-independent pixels, like every
  // other coordinate here: DirectWrite measures in DIPs at 96 dpi and nothing
  // multiplies by a scale factor, which is what keeps a `fontSize` of 16 the
  // same sixteen units it is on the other two desktops.
  RnTextSize measure(float maxWidth) const;

  // Draws at the target's current origin, into a box `width` by `height`.
  void draw(ID2D1RenderTarget *target, float width, float height) const;

  // --- Selecting that text ----------------------------------------------------
  //
  // `<Text selectable>`, which `userSelect` also arrives in. The two questions
  // that need DirectWrite -- where a point lands in the text, and what a range
  // covers -- plus the substring a clipboard takes. What a press and a drag
  // *mean* is core/TextSelection.h, shared with the other two hosts.
  //
  // Offsets are UTF-16 code units, which is what DirectWrite counts and what
  // this object already holds alongside the UTF-8 it was given. The AppKit host
  // counts the same unit and the GTK one counts bytes; core/TextSelection.h
  // says why the three never have to agree.

  // The range to draw as selected. A length of zero selects nothing, which is
  // how a selection is cleared.
  void setSelection(UINT32 start, UINT32 length);
  UINT32 selectionStart() const { return selectionStart_; }
  UINT32 selectionLength() const { return selectionLength_; }

  // The offset nearest a point in the paragraph's own coordinates, inside a box
  // of (width, height) -- the same box `draw` is given, because the answer
  // depends on all of it: the width breaks the lines and the height is what
  // `textAlignVertical` moved the paragraph inside.
  UINT32 indexAtPoint(float x, float y, float width, float height) const;

  // The selected text, in UTF-8, or empty when nothing is selected.
  std::string selectedText() const;

  // How far down its own box the paragraph sits: 0 for the top, 0.5 for the
  // middle, 1 for the bottom, which is what `textAlignVertical` asks for and
  // what `verticalAlign` becomes in React Native's own JavaScript. The offset
  // it produces is core/TextVerticalAlign.h's, applied in `draw` -- the box is
  // only known then, a view resized without its props changing having new slack
  // and the same text.
  //
  // Not `SetParagraphAlignment`, which is DirectWrite's own answer to this
  // question: `applyLineLimit` sets the layout's maximum height to the lines
  // that fit, so FAR would align against the trimmed box rather than the view's
  // and a `numberOfLines` paragraph would not move at all. The draw origin is
  // the same arithmetic the other two hosts do, over the box this is given.
  void setVerticalFlush(float flush) { verticalFlush_ = flush; }

  // Where each inline box landed, in the order the attachment runs were given,
  // laid out at `maxWidth` -- the same width `measure` would be asked, so the
  // positions belong to the paragraph Yoga was told about.
  //
  // Empty when the paragraph has no attachments, which is almost all of them.
  // The width and height are the ones React Native measured rather than
  // anything DirectWrite echoes back, so a rounding difference in the reserved
  // box cannot move the view a fraction of a point from its own layout.
  std::vector<RnAttachmentBox> attachmentBoxes(float maxWidth) const;

 private:
  RnWin32TextLayout() = default;

  // The one place a layout is configured. Both `measure` and `draw` go through
  // it, which is what makes the size Yoga is told the size the text is painted
  // at.
  //
  // Returns a reference the caller owns and must release; the two callers do it
  // with a ComPtr. A fresh layout per call rather than one held and mutated,
  // because `SetMaxWidth` on a shared layout is exactly the race that a
  // thread-safe factory would otherwise have saved us from.
  IDWriteTextLayout *buildLayout(float maxWidth, float maxHeight) const;

  // Applies `maximumNumberOfLines` to a built layout, and reports the height it
  // is now limited to, or a negative number for "no limit applied". Separate
  // because DirectWrite has no line-count property: the limit has to be turned
  // into a height by adding up the line metrics, which means the layout must
  // already exist.
  float applyLineLimit(IDWriteTextLayout *layout) const;

  // A styled span, resolved to UTF-16 character positions, which is what
  // DirectWrite's ranges are counted in. Empty for a single-style paragraph,
  // where the format alone says everything.
  struct ResolvedRun {
    unsigned start = 0;
    unsigned length = 0;
    RnTextStyle style;
    std::optional<RnInlineBox> inlineBox;
  };

  // Drawing a shadow needs an effect, which needs a device context: the view
  // layer hands over an ID2D1RenderTarget, and this asks it for one. See
  // `draw`.
  float verticalFlush_ = 0.0f;
  UINT32 selectionStart_ = 0;
  UINT32 selectionLength_ = 0;

  // The highlight, under the glyphs. Its own method for the reason the shadow's
  // is: it draws before the runs and needs the layout the caller just built.
  void drawSelection(ID2D1RenderTarget *target,
                     IDWriteTextLayout *layout,
                     float verticalOffset) const;

  void drawShadow(ID2D1RenderTarget *target,
                  IDWriteTextLayout *layout,
                  float width,
                  float height,
                  float verticalOffset) const;

  std::string utf8_;
  std::wstring utf16_;
  RnTextStyle style_;
  std::vector<ResolvedRun> runs_;
  int maximumNumberOfLines_ = 0;
  float shadowDx_ = 0.0f;
  float shadowDy_ = 0.0f;
  float shadowStandardDeviation_ = 0.0f;
  float shadowColour_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  IDWriteTextFormat *format_ = nullptr;
};

} // namespace basalt::win32
