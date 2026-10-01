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
#include <vector>
#include <string>

// Forward-declared rather than included, so a translation unit that only builds
// or measures a paragraph does not pull in <d2d1.h> and <dwrite.h> and
// windows.h behind them. The tests do exactly that.
struct ID2D1RenderTarget;
struct IDWriteTextFormat;
struct IDWriteTextLayout;

namespace basalt::win32 {

enum class RnTextAlign {
  Left,
  Center,
  Right,
  Justified,
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
};

struct RnTextSize {
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

  // The size this paragraph needs at `maxWidth`. Pass a negative width for
  // unconstrained. In React Native's density-independent pixels, like every
  // other coordinate here: DirectWrite measures in DIPs at 96 dpi and nothing
  // multiplies by a scale factor, which is what keeps a `fontSize` of 16 the
  // same sixteen units it is on the other two desktops.
  RnTextSize measure(float maxWidth) const;

  // Draws at the target's current origin, into a box `width` by `height`.
  void draw(ID2D1RenderTarget *target, float width, float height) const;

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
  };

  std::string utf8_;
  std::wstring utf16_;
  RnTextStyle style_;
  std::vector<ResolvedRun> runs_;
  int maximumNumberOfLines_ = 0;
  IDWriteTextFormat *format_ = nullptr;
};

} // namespace basalt::win32
