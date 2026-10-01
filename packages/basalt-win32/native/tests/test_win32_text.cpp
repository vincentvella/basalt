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

#include <memory>
#include <sstream>
#include <string>

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
