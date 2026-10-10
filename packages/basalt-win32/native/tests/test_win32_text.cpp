// DirectWrite measurement, and where the glyphs land.
//
// The union of what tests/test_text.cpp asks of Pango and
// tests/test_appkit_text.mm asks of Core Text. Two of them are regression tests
// on the other platforms rather than general questions, and they are here
// because the bug each describes is a bug any text engine can have:
// `font_size_is_absolute_not_points`, and that a default ellipsize mode does
// not collapse a wrapping paragraph.
//
// No React Native and no window: RnWin32TextLayout knows nothing about either.

#include "TestHarness.h"

#include "RnWin32TextLayout.h"
#include "RnWin32View.h"
#include "Win32Snapshot.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using basalt::win32::RnTextAlign;
using basalt::win32::RnTextSize;
using basalt::win32::RnTextStyle;
using basalt::win32::RnWin32TextLayout;
using basalt::win32::RnWin32View;

namespace {

std::shared_ptr<RnWin32TextLayout> paragraph(const std::string &text,
                                             float fontSize = 16.0f,
                                             int maxLines = 0) {
  RnTextStyle style;
  style.fontSize = fontSize;
  return RnWin32TextLayout::create(text, style, maxLines);
}

constexpr const char *kLongText =
    "The quick brown fox jumps over the lazy dog, and then does it again "
    "because once was never going to be enough for a wrapping test.";

} // namespace

TEST(text_has_a_size) {
  auto layout = paragraph("Hello");
  EXPECT(layout != nullptr);

  const RnTextSize size = layout->measure(-1.0f);
  EXPECT(size.width > 0.0f);
  EXPECT(size.height > 0.0f);
}

TEST(empty_text_measures_to_zero_width) {
  auto layout = paragraph("");
  EXPECT(layout != nullptr);

  const RnTextSize size = layout->measure(-1.0f);
  EXPECT_NEAR(size.width, 0.0, 0.5);
  // A height, though: an empty line still occupies one. Both other platforms
  // report the same, and a zero-height empty <Text> would collapse a layout
  // that was relying on it to hold a row open.
  EXPECT(size.height > 0.0f);
}

TEST(larger_font_measures_larger) {
  const RnTextSize small = paragraph("Hello", 12.0f)->measure(-1.0f);
  const RnTextSize large = paragraph("Hello", 36.0f)->measure(-1.0f);

  EXPECT(large.width > small.width);
  EXPECT(large.height > small.height);
}

TEST(font_size_is_absolute_not_points) {
  // The bug this guards against, in the shape Pango had it: a size interpreted
  // as points and resolved against 96dpi renders about a third larger than
  // asked. React Native's fontSize is in density-independent pixels and every
  // coordinate here lives in that same space, so a hundred-unit font must
  // produce a line in the neighbourhood of a hundred units -- not a hundred and
  // thirty-three, and certainly not a hundred and seventy-seven.
  //
  // The bounds are wide because a font's natural line spacing is its own
  // business; what they exclude is a multiply by 96/72.
  const RnTextSize size = paragraph("Hg", 100.0f)->measure(-1.0f);
  EXPECT(size.height >= 90.0f);
  EXPECT(size.height < 160.0f);
}

TEST(constraining_the_width_wraps_and_grows_taller) {
  auto layout = paragraph(kLongText);

  const RnTextSize wide = layout->measure(-1.0f);
  const RnTextSize narrow = layout->measure(200.0f);

  EXPECT(narrow.width <= 200.5f);
  EXPECT(narrow.width < wide.width);
  EXPECT(narrow.height > wide.height);
}

TEST(number_of_lines_truncates) {
  auto unlimited = paragraph(kLongText, 16.0f, 0);
  auto limited = paragraph(kLongText, 16.0f, 2);

  const RnTextSize full = unlimited->measure(200.0f);
  const RnTextSize clipped = limited->measure(200.0f);

  EXPECT(clipped.height < full.height);
  EXPECT(clipped.height > 0.0f);
}

TEST(a_line_limit_that_fits_is_not_truncated) {
  // Five lines allowed and the paragraph needs fewer, so the limit must change
  // nothing at all -- not the height, and not the text.
  auto unlimited = paragraph("Short", 16.0f, 0);
  auto limited = paragraph("Short", 16.0f, 5);

  EXPECT_NEAR(limited->measure(-1.0f).height, unlimited->measure(-1.0f).height, 0.01);
  EXPECT_NEAR(limited->measure(-1.0f).width, unlimited->measure(-1.0f).width, 0.01);
}

TEST(default_ellipsize_mode_does_not_collapse_a_paragraph) {
  // Pango's trap, stated as a test: React Native's ellipsizeMode defaults to
  // tail, and translating that faithfully by switching ellipsization on without
  // also setting a height collapsed every wrapping paragraph to one line. A
  // paragraph with no line limit must wrap to as many lines as it needs.
  auto layout = paragraph(kLongText);

  const RnTextSize oneLine = paragraph("Hello")->measure(-1.0f);
  const RnTextSize wrapped = layout->measure(200.0f);

  EXPECT(wrapped.height > oneLine.height * 2.0f);
}

TEST(bold_is_wider_than_regular) {
  RnTextStyle regular;
  regular.fontSize = 24.0f;
  RnTextStyle bold = regular;
  bold.bold = true;

  const RnTextSize plain =
      RnWin32TextLayout::create("Handgloves", regular, 0)->measure(-1.0f);
  const RnTextSize heavy = RnWin32TextLayout::create("Handgloves", bold, 0)->measure(-1.0f);

  EXPECT(heavy.width > plain.width);
}

TEST(alignment_does_not_change_the_measured_size) {
  RnTextStyle left;
  left.fontSize = 16.0f;
  RnTextStyle centred = left;
  centred.align = RnTextAlign::Center;

  const RnTextSize a = RnWin32TextLayout::create(kLongText, left, 0)->measure(300.0f);
  const RnTextSize b = RnWin32TextLayout::create(kLongText, centred, 0)->measure(300.0f);

  // Alignment moves glyphs inside the box. It does not change how big the box
  // has to be, and a platform that reported otherwise would make Yoga lay out
  // a centred paragraph differently from a left-aligned one.
  EXPECT_NEAR(a.width, b.width, 0.01);
  EXPECT_NEAR(a.height, b.height, 0.01);
}

TEST(text_is_reported_in_the_tree) {
  auto view = std::make_unique<RnWin32View>(7);
  view->setFrame(0, 0, 200, 40);
  view->setTextLayout(paragraph("Hi \"there\"\nyou"));

  // Escaped the way the GTK side escapes it, so a quote or a newline keeps the
  // dump to one line per view.
  EXPECT_EQ(view->describeTree(),
            std::string("view tag=7 frame=(0,0 200x40) text=\"Hi \\\"there\\\"\\nyou\"\n"));
}

TEST(text_draws_where_the_alignment_says) {
  // The assertion the other two hosts cannot make: not that alignment was set,
  // but that the glyphs moved. A left-aligned line puts ink near the left edge
  // and none near the right; a right-aligned one does the opposite.
  const auto inkColumns = [](RnTextAlign align) {
    RnTextStyle style;
    style.fontSize = 24.0f;
    style.align = align;

    auto root = std::make_unique<RnWin32View>(1);
    root->setFrame(0, 0, 300, 40);
    root->setTextLayout(RnWin32TextLayout::create("Ill", style, 0));

    const basalt::win32::RnPixels pixels = basalt::win32::renderToPixels(*root);
    int left = 0;
    int right = 0;
    for (unsigned y = 0; y < pixels.height(); y++) {
      for (unsigned x = 0; x < 60; x++) {
        if (pixels.at(x, y).alpha > 32) {
          left++;
        }
        if (pixels.at(pixels.width() - 1 - x, y).alpha > 32) {
          right++;
        }
      }
    }
    return std::pair<int, int>{left, right};
  };

  const auto [leftInkWhenLeft, rightInkWhenLeft] = inkColumns(RnTextAlign::Left);
  EXPECT(leftInkWhenLeft > 0);
  EXPECT_EQ(rightInkWhenLeft, 0);

  const auto [leftInkWhenRight, rightInkWhenRight] = inkColumns(RnTextAlign::Right);
  EXPECT_EQ(leftInkWhenRight, 0);
  EXPECT(rightInkWhenRight > 0);
}

TEST(each_run_draws_in_its_own_colour) {
  // `Hello <Text style={{color:'red'}}>world</Text>` is two runs of one
  // paragraph, and DirectWrite carries colour as a *drawing effect* rather than
  // as a range attribute -- so a layout built the obvious way renders the whole
  // paragraph in the first run's colour. Both other desktops get this from
  // their attributed string for free, which is why the gap here lasted as long
  // as it did.
  //
  // Ink rather than an exact pixel: where a glyph lands depends on the font,
  // and what is being asked is only which colours appear at all.
  RnTextStyle red;
  red.fontSize = 32.0f;
  red.color[0] = 1.0f;
  red.color[1] = 0.0f;
  red.color[2] = 0.0f;

  RnTextStyle blue = red;
  blue.color[0] = 0.0f;
  blue.color[2] = 1.0f;

  auto root = std::make_unique<RnWin32View>(1);
  root->setFrame(0, 0, 400, 60);
  root->setTextLayout(RnWin32TextLayout::createFromRuns(
      {basalt::win32::RnTextRun{"IIII", red}, basalt::win32::RnTextRun{"IIII", blue}}, 0));

  const basalt::win32::RnPixels pixels = basalt::win32::renderToPixels(*root);
  EXPECT(!pixels.empty());

  int reddish = 0;
  int bluish = 0;
  for (unsigned y = 0; y < pixels.height(); y++) {
    for (unsigned x = 0; x < pixels.width(); x++) {
      const auto pixel = pixels.at(x, y);
      if (pixel.alpha < 128) {
        continue;
      }
      if (pixel.red > pixel.blue + 64) {
        reddish++;
      }
      if (pixel.blue > pixel.red + 64) {
        bluish++;
      }
    }
  }

  EXPECT(reddish > 0);
  EXPECT(bluish > 0);
}

// ---------------------------------------------------------------------------
// textDecorationLine, which DirectWrite draws itself.
//
// Against pixels rather than against the style the layout was given: an
// underline that was asked for and not drawn is exactly the bug the other two
// hosts had, and a test on the struct would pass through it. What is measured
// is the longest horizontal run of ink, which for a line is about the width of
// the text and for glyphs is a few pixels.
// ---------------------------------------------------------------------------
namespace {

// The widest horizontal run of ink in the view, and which row it was on.
struct LongestRun {
  int length{0};
  int row{-1};
};

LongestRun longestInkRun(const RnTextStyle &style, const char *text) {
  auto root = std::make_unique<RnWin32View>(1);
  root->setFrame(0, 0, 300, 60);
  root->setTextLayout(RnWin32TextLayout::create(text, style, 0));

  const basalt::win32::RnPixels pixels = basalt::win32::renderToPixels(*root);
  LongestRun longest;
  for (unsigned y = 0; y < pixels.height(); y++) {
    int run = 0;
    for (unsigned x = 0; x < pixels.width(); x++) {
      if (pixels.at(x, y).alpha > 32) {
        run++;
        if (run > longest.length) {
          longest.length = run;
          longest.row = static_cast<int>(y);
        }
      } else {
        run = 0;
      }
    }
  }
  return longest;
}

} // namespace

TEST(text_an_underline_draws_a_line) {
  RnTextStyle plain;
  plain.fontSize = 24.0f;

  RnTextStyle underlined = plain;
  underlined.underline = true;

  // "iiiii" has no wide glyph in it, so any long horizontal run is the line.
  const LongestRun without = longestInkRun(plain, "iiiii");
  const LongestRun with = longestInkRun(underlined, "iiiii");

  EXPECT(without.length > 0);
  EXPECT(with.length > without.length * 2);
}

TEST(text_a_strikethrough_draws_a_line_above_an_underline) {
  RnTextStyle struck;
  struck.fontSize = 24.0f;
  struck.strikethrough = true;

  RnTextStyle underlined;
  underlined.fontSize = 24.0f;
  underlined.underline = true;

  const LongestRun strikeRun = longestInkRun(struck, "iiiii");
  const LongestRun underRun = longestInkRun(underlined, "iiiii");

  // Both draw a line, and they are not the same line: a strikethrough crosses
  // the glyphs and an underline sits below them, which is the one thing that
  // says the two properties were not confused for each other.
  EXPECT(strikeRun.length > 0);
  EXPECT(underRun.length > 0);
  EXPECT(strikeRun.row >= 0);
  EXPECT(underRun.row > strikeRun.row);
}

TEST(text_both_decorations_at_once_draw_two_lines) {
  RnTextStyle both;
  both.fontSize = 24.0f;
  both.underline = true;
  both.strikethrough = true;

  auto root = std::make_unique<RnWin32View>(1);
  root->setFrame(0, 0, 300, 60);
  root->setTextLayout(RnWin32TextLayout::create("iiiii", both, 0));

  const basalt::win32::RnPixels pixels = basalt::win32::renderToPixels(*root);
  // Rows whose ink run is long enough to be a line rather than a glyph.
  int lineRows = 0;
  for (unsigned y = 0; y < pixels.height(); y++) {
    int run = 0;
    int longest = 0;
    for (unsigned x = 0; x < pixels.width(); x++) {
      if (pixels.at(x, y).alpha > 32) {
        longest = ++run > longest ? run : longest;
      } else {
        run = 0;
      }
    }
    if (longest > 20) {
      lineRows++;
    }
  }
  // At least one row for each line. Not an exact count: how thick a line is
  // comes from the font's metrics.
  EXPECT(lineRows >= 2);
}

// ---------------------------------------------------------------------------
// writingDirection, and what a natural alignment means underneath it.
//
// Setting the reading direction gets the glyphs into the right order inside a
// line; which edge the line itself starts from is the second half, and the one
// both other hosts got wrong first. DirectWrite's alignments are relative --
// LEADING is the right edge in a right-to-left paragraph -- so a natural
// alignment follows the direction with no arithmetic, and the two physical
// alignments have to swap.
//
// Against ink columns, the same instrument `text_draws_where_the_alignment
// _says` uses: a line against one edge puts ink there and none at the other.
// ---------------------------------------------------------------------------
namespace {

std::pair<int, int> inkAtTheEdges(RnTextAlign align, bool rightToLeft) {
  RnTextStyle style;
  style.fontSize = 24.0f;
  style.align = align;
  style.rightToLeft = rightToLeft;

  auto root = std::make_unique<RnWin32View>(1);
  root->setFrame(0, 0, 300, 40);
  root->setTextLayout(RnWin32TextLayout::create("Ill", style, 0));

  const basalt::win32::RnPixels pixels = basalt::win32::renderToPixels(*root);
  int left = 0;
  int right = 0;
  for (unsigned y = 0; y < pixels.height(); y++) {
    for (unsigned x = 0; x < 60; x++) {
      if (pixels.at(x, y).alpha > 32) {
        left++;
      }
      if (pixels.at(pixels.width() - 1 - x, y).alpha > 32) {
        right++;
      }
    }
  }
  return {left, right};
}

} // namespace

TEST(text_a_natural_alignment_follows_the_writing_direction) {
  const auto [leftInLtr, rightInLtr] = inkAtTheEdges(RnTextAlign::Natural, false);
  EXPECT(leftInLtr > 0);
  EXPECT_EQ(rightInLtr, 0);

  // The whole point of the entry this closes: the same paragraph, right to
  // left, starts from the other edge.
  const auto [leftInRtl, rightInRtl] = inkAtTheEdges(RnTextAlign::Natural, true);
  EXPECT_EQ(leftInRtl, 0);
  EXPECT(rightInRtl > 0);
}

TEST(text_a_physical_alignment_stays_physical_in_a_right_to_left_paragraph) {
  // `textAlign: 'left'` is a physical edge in React Native and on the other two
  // hosts, so it has to survive the direction change. DirectWrite's LEADING
  // would have moved it, which is what the swap in `toDWriteAlignment` is for.
  const auto [left, right] = inkAtTheEdges(RnTextAlign::Left, true);
  EXPECT(left > 0);
  EXPECT_EQ(right, 0);

  const auto [farLeft, farRight] = inkAtTheEdges(RnTextAlign::Right, true);
  EXPECT_EQ(farLeft, 0);
  EXPECT(farRight > 0);
}

TEST(text_end_is_relative_and_follows_the_direction) {
  // `start` and `end` are the relative pair, and this host keeps them relative:
  // `end` is the right edge in a left-to-right paragraph and the left edge in a
  // right-to-left one. The other two fold `end` into physical right.
  const auto [leftInLtr, rightInLtr] = inkAtTheEdges(RnTextAlign::End, false);
  EXPECT_EQ(leftInLtr, 0);
  EXPECT(rightInLtr > 0);

  const auto [leftInRtl, rightInRtl] = inkAtTheEdges(RnTextAlign::End, true);
  EXPECT(leftInRtl > 0);
  EXPECT_EQ(rightInRtl, 0);
}

// fontVariant, as far as a test without a known font can go.
//
// Whether a face has `smcp` is the face's business: DirectWrite asks for the
// feature and a font without it renders unchanged, so there is no pixel to
// assert. What this pins is that asking does not break the layout, which is the
// failure mode of a wrong `DWRITE_FONT_FEATURE_TAG`: a rejected typography
// object takes the whole paragraph with it.
TEST(text_font_features_do_not_break_the_layout) {
  RnTextStyle plain;
  plain.fontSize = 20.0f;

  RnTextStyle featured = plain;
  featured.fontFeatures = {"smcp", "tnum", "ss07"};

  const RnTextSize without = paragraph("Handgloves 0123", 20.0f)->measure(-1.0f);
  auto layout = RnWin32TextLayout::create("Handgloves 0123", featured, 0);
  EXPECT(layout != nullptr);
  const RnTextSize with = layout->measure(-1.0f);

  EXPECT(with.width > 0.0f);
  EXPECT(with.height > 0.0f);
  // Within a few points of the same string unfeatured: small caps may or may
  // not exist in Segoe UI, and either way the paragraph is still that text at
  // that size rather than nothing.
  EXPECT(with.height > without.height * 0.5f);
}

// ---------------------------------------------------------------------------
// The text shadow: `textShadowColor`, `textShadowOffset` and
// `textShadowRadius`.
//
// One shadow per paragraph, as on the other two hosts and for the same reason:
// no engine here can draw a different one per run. The blur is an effect, so
// the first thing asserted is that an effect can be had at all -- a render
// target made by a Direct2D 1.1 factory answers `ID2D1DeviceContext`, and if
// that ever stops being true on a runner, a failure here says so rather than a
// shadow quietly going hard.
// ---------------------------------------------------------------------------

TEST(text_a_shadow_is_drawn_under_the_glyphs) {
  RnTextStyle style;
  style.fontSize = 28.0f;
  // White text, so the shadow is the only dark ink in the picture.
  style.color[0] = 1.0f;
  style.color[1] = 1.0f;
  style.color[2] = 1.0f;

  auto root = std::make_unique<RnWin32View>(1);
  root->setFrame(0, 0, 200, 60);
  auto layout = RnWin32TextLayout::create("Shadow", style, 0);
  EXPECT(layout != nullptr);
  const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  layout->setShadow(6.0f, 6.0f, 2.0f, black);
  root->setTextLayout(std::move(layout));

  const basalt::win32::RnPixels pixels = basalt::win32::renderToPixels(*root);
  EXPECT(!pixels.empty());

  int dark = 0;
  int light = 0;
  for (unsigned y = 0; y < pixels.height(); y++) {
    for (unsigned x = 0; x < pixels.width(); x++) {
      const auto pixel = pixels.at(x, y);
      if (pixel.alpha < 64) {
        continue;
      }
      if (pixel.red < 80 && pixel.green < 80 && pixel.blue < 80) {
        dark++;
      }
      if (pixel.red > 200 && pixel.green > 200 && pixel.blue > 200) {
        light++;
      }
    }
  }

  // Both: the glyphs and something dark under them, which is the shadow. A
  // shadow drawn over the text instead would leave no light ink at all.
  EXPECT(light > 0);
  EXPECT(dark > 0);
}

TEST(text_a_shadow_follows_its_offset) {
  const auto inkBounds = [](float dx, float dy) {
    RnTextStyle style;
    style.fontSize = 28.0f;
    style.color[0] = 1.0f;
    style.color[1] = 1.0f;
    style.color[2] = 1.0f;

    auto root = std::make_unique<RnWin32View>(1);
    root->setFrame(0, 0, 200, 60);
    auto layout = RnWin32TextLayout::create("Shadow", style, 0);
    const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    layout->setShadow(dx, dy, 0.0f, black);
    root->setTextLayout(std::move(layout));

    const basalt::win32::RnPixels pixels = basalt::win32::renderToPixels(*root);
    int right = 0;
    int bottom = 0;
    for (unsigned y = 0; y < pixels.height(); y++) {
      for (unsigned x = 0; x < pixels.width(); x++) {
        const auto pixel = pixels.at(x, y);
        if (pixel.alpha > 64 && pixel.red < 80 && pixel.green < 80 && pixel.blue < 80) {
          right = (std::max)(right, static_cast<int>(x));
          bottom = (std::max)(bottom, static_cast<int>(y));
        }
      }
    }
    return std::pair<int, int>{right, bottom};
  };

  const auto [rightNear, bottomNear] = inkBounds(2.0f, 2.0f);
  const auto [rightFar, bottomFar] = inkBounds(12.0f, 12.0f);

  // A bigger offset puts the shadow further right and further down. Both axes,
  // because one offset read into both is the mistake this catches.
  EXPECT(rightFar > rightNear + 5);
  EXPECT(bottomFar > bottomNear + 5);
}

// The blur, which is the half that needs an effect.
TEST(text_a_shadow_radius_spreads_it) {
  const auto darkInk = [](float standardDeviation) {
    RnTextStyle style;
    style.fontSize = 28.0f;
    style.color[0] = 1.0f;
    style.color[1] = 1.0f;
    style.color[2] = 1.0f;

    auto root = std::make_unique<RnWin32View>(1);
    root->setFrame(0, 0, 200, 60);
    auto layout = RnWin32TextLayout::create("Shadow", style, 0);
    const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    layout->setShadow(4.0f, 4.0f, standardDeviation, black);
    root->setTextLayout(std::move(layout));

    const basalt::win32::RnPixels pixels = basalt::win32::renderToPixels(*root);
    int ink = 0;
    for (unsigned y = 0; y < pixels.height(); y++) {
      for (unsigned x = 0; x < pixels.width(); x++) {
        const auto pixel = pixels.at(x, y);
        // Anything that is not white and not empty: a blur spreads the shadow
        // into partly covered pixels, which is exactly what is being counted.
        if (pixel.alpha > 24 && pixel.red < 200) {
          ink++;
        }
      }
    }
    return ink;
  };

  const int hard = darkInk(0.0f);
  const int blurred = darkInk(4.0f);

  EXPECT(hard > 0);
  // A blurred shadow covers more pixels than a hard one, which is what says
  // the effect ran. Without a device context the fallback draws the hard
  // shadow for both and these are equal, which is the case this test exists to
  // notice.
  EXPECT(blurred > hard);
}

TEST(text_no_shadow_colour_is_no_shadow) {
  RnTextStyle style;
  style.fontSize = 28.0f;

  auto layout = RnWin32TextLayout::create("Shadow", style, 0);
  EXPECT(!layout->hasShadow());

  // A transparent colour is a shadow an app asked to be invisible, which
  // core/TextShadows.h refuses before it gets here; this is the view layer
  // agreeing.
  const float invisible[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  layout->setShadow(4.0f, 4.0f, 2.0f, invisible);
  EXPECT(!layout->hasShadow());

  const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  layout->setShadow(4.0f, 4.0f, 2.0f, black);
  EXPECT(layout->hasShadow());
}

TEST(text_a_shadow_is_reported_in_the_tree) {
  RnTextStyle style;
  style.fontSize = 16.0f;

  auto root = std::make_unique<RnWin32View>(1);
  root->setFrame(0, 0, 200, 60);
  auto layout = RnWin32TextLayout::create("Shadow", style, 0);
  // The same numbers e2e/text.tsx asks for, so the line this prints is the line
  // the end-to-end scenario reads on the other two hosts.
  const float blue[4] = {77.0f / 255.0f, 140.0f / 255.0f, 242.0f / 255.0f, 1.0f};
  layout->setShadow(2.0f, 3.0f, 4.0f, blue);
  root->setTextLayout(std::move(layout));

  EXPECT(root->describeTree().find("text-shadow=(2,3,4,#4d8cf2ff)") != std::string::npos);
}

// ---------------------------------------------------------------------------
// An inline `<View>` inside a `<Text>`: the box the paragraph reserves for it
// and where that box lands.
//
// The same two questions tests/test_text.cpp asks of Pango's shape attribute
// and tests/test_appkit_text.mm asks of Core Text's run delegate, which is the
// useful part of having done those first: the reservation has to be the size
// React Native measured, and the position has to come from the engine, because
// the engine is what knows where the line broke and how it was aligned.
// ---------------------------------------------------------------------------
namespace {

using basalt::win32::RnAttachmentBox;
using basalt::win32::RnInlineBox;
using basalt::win32::RnTextRun;

// React Native's own placeholder for an attachment: U+FFFC, the object
// replacement character, which is what `AttributedString::Fragment` carries and
// what the inline box is attached to.
constexpr const char *kPlaceholder = "\xEF\xBF\xBC";

RnTextRun textRun(const std::string &text, float fontSize = 16.0f) {
  RnTextStyle style;
  style.fontSize = fontSize;
  return RnTextRun{text, style, std::nullopt};
}

RnTextRun attachmentRun(float width, float height, float fontSize = 16.0f) {
  RnTextRun run = textRun(kPlaceholder, fontSize);
  run.inlineBox = RnInlineBox{width, height};
  return run;
}

} // namespace

TEST(text_an_attachment_reserves_the_box_react_native_measured) {
  const auto plain = RnWin32TextLayout::createFromRuns({textRun("AB")}, 0);
  const auto withBox =
      RnWin32TextLayout::createFromRuns({textRun("A"), attachmentRun(48.0f, 24.0f), textRun("B")},
                                        0);
  EXPECT(plain != nullptr && withBox != nullptr);
  if (plain == nullptr || withBox == nullptr) {
    return;
  }

  const RnTextSize text = plain->measure(-1.0f);
  const RnTextSize boxed = withBox->measure(-1.0f);

  // Exactly the box wider, which is two assertions in one: the room is
  // reserved, and the placeholder character itself draws no glyph of its own.
  // Without the inline object U+FFFC measures as whatever the font has for it,
  // which is neither zero nor forty-eight.
  EXPECT_NEAR(boxed.width, text.width + 48.0, 1.0);
  // And taller, the box being taller than sixteen point text.
  EXPECT(boxed.height > text.height);
  EXPECT(boxed.height >= 24.0f);
}

TEST(text_an_attachment_is_reported_where_the_engine_put_it) {
  const auto layout =
      RnWin32TextLayout::createFromRuns({textRun("A"), attachmentRun(48.0f, 24.0f)}, 0);
  EXPECT(layout != nullptr);
  if (layout == nullptr) {
    return;
  }

  const std::vector<RnAttachmentBox> boxes = layout->attachmentBoxes(-1.0f);
  EXPECT_EQ(boxes.size(), 1u);
  if (boxes.empty()) {
    return;
  }

  // The size is React Native's, unchanged.
  EXPECT_NEAR(boxes[0].width, 48.0, 0.01);
  EXPECT_NEAR(boxes[0].height, 24.0, 0.01);

  // And the position is after the "A", which is the thing no arithmetic here
  // could have worked out: it is where DirectWrite laid the line out.
  const auto letter = RnWin32TextLayout::createFromRuns({textRun("A")}, 0);
  const RnTextSize measured = letter->measure(-1.0f);
  EXPECT_NEAR(boxes[0].x, measured.width, 1.5);
}

// The box's bottom sits on the text baseline, which is what the inline object's
// baseline says and what CSS does with an inline box. For a box taller than the
// text that puts its top at the top of the line; for a short one it hangs below.
TEST(text_an_attachment_sits_on_the_baseline) {
  const auto tall =
      RnWin32TextLayout::createFromRuns({textRun("A"), attachmentRun(10.0f, 40.0f)}, 0);
  const auto shortBox =
      RnWin32TextLayout::createFromRuns({textRun("A"), attachmentRun(10.0f, 4.0f)}, 0);
  EXPECT(tall != nullptr && shortBox != nullptr);
  if (tall == nullptr || shortBox == nullptr) {
    return;
  }

  // The tall box is what decides the line's height, so its bottom is the
  // baseline and its top is the top of the paragraph.
  const std::vector<RnAttachmentBox> tallBoxes = tall->attachmentBoxes(-1.0f);
  EXPECT_EQ(tallBoxes.size(), 1u);
  if (!tallBoxes.empty()) {
    EXPECT_NEAR(tallBoxes[0].y, 0.0, 1.0);
  }

  // A four point box on a line of sixteen point text hangs near the bottom: its
  // top is the baseline less four, which is most of the way down the line. A
  // host that put every attachment at the top of its line answers zero here.
  const std::vector<RnAttachmentBox> shortBoxes = shortBox->attachmentBoxes(-1.0f);
  EXPECT_EQ(shortBoxes.size(), 1u);
  if (!shortBoxes.empty()) {
    EXPECT(shortBoxes[0].y > 4.0f);
    EXPECT(shortBoxes[0].y < shortBox->measure(-1.0f).height);
  }
}

TEST(text_every_attachment_is_reported_in_order) {
  const auto layout = RnWin32TextLayout::createFromRuns({textRun("A"),
                                                         attachmentRun(10.0f, 10.0f),
                                                         textRun("B"),
                                                         attachmentRun(30.0f, 10.0f)},
                                                        0);
  EXPECT(layout != nullptr);
  if (layout == nullptr) {
    return;
  }

  const std::vector<RnAttachmentBox> boxes = layout->attachmentBoxes(-1.0f);
  EXPECT_EQ(boxes.size(), 2u);
  if (boxes.size() < 2) {
    return;
  }
  // In the order they were given, which is the order the layout manager pairs
  // them with fragments: the sizes say which is which.
  EXPECT_NEAR(boxes[0].width, 10.0, 0.01);
  EXPECT_NEAR(boxes[1].width, 30.0, 0.01);
  // And the second is to the right of the first, with a "B" between them.
  EXPECT(boxes[1].x > boxes[0].x + 10.0f);
}

// A wrap moves an attachment to the next line, which is the case that makes
// reading the position back worth it rather than adding up widths.
TEST(text_an_attachment_follows_a_wrap_onto_the_next_line) {
  const auto layout =
      RnWin32TextLayout::createFromRuns({textRun("wrap me around"), attachmentRun(40.0f, 10.0f)},
                                        0);
  EXPECT(layout != nullptr);
  if (layout == nullptr) {
    return;
  }

  // Narrow enough that the box cannot share the first line.
  const std::vector<RnAttachmentBox> boxes = layout->attachmentBoxes(60.0f);
  EXPECT_EQ(boxes.size(), 1u);
  if (boxes.empty()) {
    return;
  }
  // Below the first line, and near the left edge rather than after the text.
  EXPECT(boxes[0].y > 10.0f);
  EXPECT(boxes[0].x < 20.0f);
}

TEST(text_a_paragraph_with_no_attachment_reports_no_boxes) {
  const auto layout = RnWin32TextLayout::createFromRuns({textRun("A"), textRun("B")}, 0);
  EXPECT(layout != nullptr);
  if (layout != nullptr) {
    EXPECT(layout->attachmentBoxes(-1.0f).empty());
  }
  // And neither does a single-style paragraph, which skips the run list
  // entirely.
  const auto plain = paragraph("Hello");
  EXPECT(plain != nullptr);
  if (plain != nullptr) {
    EXPECT(plain->attachmentBoxes(-1.0f).empty());
  }
}
