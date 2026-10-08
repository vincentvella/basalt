// Tests for the Pango text layer.
//
// These assert relationships rather than pixel counts. Font metrics differ
// between machines and fontconfig setups, so "this string is 214.5 points wide"
// is not a fact worth encoding; "constraining the width makes it wrap, and
// wrapping makes it taller" is.

#include "TestHarness.h"

#include "PangoTextLayout.h"

#include <react/renderer/attributedstring/AttributedString.h>
#include <react/renderer/attributedstring/ParagraphAttributes.h>

#include <sstream>

using facebook::react::AttributedString;
using facebook::react::EllipsizeMode;
using facebook::react::FontWeight;
using facebook::react::ParagraphAttributes;
using facebook::react::TextAttributes;

namespace {

AttributedString makeText(const std::string &text, float fontSize = 16.0F) {
  TextAttributes attributes;
  attributes.fontSize = fontSize;

  AttributedString::Fragment fragment;
  fragment.string = text;
  fragment.textAttributes = attributes;

  AttributedString result;
  result.appendFragment(std::move(fragment));
  return result;
}

struct Measured {
  float width;
  float height;
};

Measured measure(const AttributedString &text, const ParagraphAttributes &attributes, float maxWidth) {
  PangoLayout *layout = basalt::buildTextLayout(text, attributes, maxWidth);
  Measured measured{};
  basalt::textLayoutSize(layout, &measured.width, &measured.height);
  g_object_unref(layout);
  return measured;
}

// A sentence with an inline view in the middle of it, the shape React Native
// gives `<Text>before <View/> after</Text>`: one fragment per run of text and one
// carrying U+FFFC, whose size the paragraph's own layout has already measured and
// written into the fragment.
AttributedString makeTextWithAttachment(float width, float height, float fontSize = 16.0F) {
  TextAttributes attributes;
  attributes.fontSize = fontSize;

  AttributedString result;

  AttributedString::Fragment before;
  before.string = "before ";
  before.textAttributes = attributes;
  result.appendFragment(std::move(before));

  AttributedString::Fragment view;
  view.string = AttributedString::Fragment::AttachmentCharacter();
  view.textAttributes = attributes;
  view.parentShadowView.layoutMetrics.frame.size = {.width = width, .height = height};
  result.appendFragment(std::move(view));

  AttributedString::Fragment after;
  after.string = " after";
  after.textAttributes = attributes;
  result.appendFragment(std::move(after));

  return result;
}

std::vector<facebook::react::Rect> attachments(const AttributedString &text, float maxWidth) {
  PangoLayout *layout = basalt::buildTextLayout(text, ParagraphAttributes{}, maxWidth);
  const auto frames = basalt::attachmentFrames(layout, text);
  g_object_unref(layout);
  return frames;
}

const char *kLongText =
    "Yoga asked Pango how wide this paragraph wants to be, and Pango answered in "
    "1024ths of a pixel, which is a unit nobody enjoys debugging.";

} // namespace

TEST(text_has_a_size) {
  const auto measured = measure(makeText("Hello"), ParagraphAttributes{}, -1.0F);
  EXPECT(measured.width > 0.0F);
  EXPECT(measured.height > 0.0F);
}

TEST(larger_font_measures_larger) {
  const auto small = measure(makeText("Hello", 12.0F), ParagraphAttributes{}, -1.0F);
  const auto large = measure(makeText("Hello", 36.0F), ParagraphAttributes{}, -1.0F);

  EXPECT(large.width > small.width);
  EXPECT(large.height > small.height);
}

TEST(constraining_the_width_wraps_and_grows_taller) {
  const auto unconstrained = measure(makeText(kLongText), ParagraphAttributes{}, -1.0F);
  const auto wrapped = measure(makeText(kLongText), ParagraphAttributes{}, 200.0F);

  // Wrapping is the whole point: narrower, and more than one line tall.
  EXPECT(wrapped.width <= 200.5F);
  EXPECT(wrapped.width < unconstrained.width);
  EXPECT(wrapped.height > unconstrained.height);
}

TEST(number_of_lines_truncates) {
  ParagraphAttributes oneLine;
  oneLine.maximumNumberOfLines = 1;
  oneLine.ellipsizeMode = EllipsizeMode::Tail;

  const auto unlimited = measure(makeText(kLongText), ParagraphAttributes{}, 200.0F);
  const auto limited = measure(makeText(kLongText), oneLine, 200.0F);

  EXPECT(limited.height < unlimited.height);
}

TEST(default_ellipsize_mode_does_not_collapse_a_paragraph) {
  // React Native defaults ellipsizeMode to Tail, and Pango ellipsizes to a
  // *single line* when no height is set. Translating that default faithfully
  // once turned every wrapping paragraph into one line, so this is a
  // regression test for the bug rather than a test of Pango.
  ParagraphAttributes defaults;
  defaults.ellipsizeMode = EllipsizeMode::Tail;
  EXPECT_EQ(defaults.maximumNumberOfLines, 0);

  const auto measured = measure(makeText(kLongText), defaults, 200.0F);
  const auto oneLineTall = measure(makeText("Hello"), ParagraphAttributes{}, -1.0F);

  EXPECT(measured.height > oneLineTall.height * 1.5F);
}

TEST(font_size_is_absolute_not_points) {
  // set_size would resolve against the context's resolution, making a fontSize
  // of 100 render at about 133px on a 96dpi context. React Native's fontSize is
  // in logical pixels, so a single line of 100pt text should be roughly 100
  // tall, not a third taller again.
  const auto measured = measure(makeText("Hg", 100.0F), ParagraphAttributes{}, -1.0F);

  EXPECT(measured.height > 90.0F);
  EXPECT(measured.height < 130.0F);
}

TEST(bold_is_wider_than_regular) {
  TextAttributes regular;
  regular.fontSize = 24.0F;

  TextAttributes bold;
  bold.fontSize = 24.0F;
  bold.fontWeight = FontWeight::Bold;

  const auto build = [](const TextAttributes &attributes) {
    AttributedString::Fragment fragment;
    fragment.string = "Weight";
    fragment.textAttributes = attributes;
    AttributedString text;
    text.appendFragment(std::move(fragment));
    return text;
  };

  const auto regularSize = measure(build(regular), ParagraphAttributes{}, -1.0F);
  const auto boldSize = measure(build(bold), ParagraphAttributes{}, -1.0F);

  // Not guaranteed by typography in general, but true of every sans-serif
  // family a desktop ships as its default. If this fails the font description
  // is probably not being applied at all.
  EXPECT(boldSize.width >= regularSize.width);
}

TEST(empty_text_measures_to_zero_width) {
  const auto measured = measure(makeText(""), ParagraphAttributes{}, -1.0F);
  EXPECT_NEAR(measured.width, 0.0, 0.001);
}

// Inline views: `<Text>before <View/> after</Text>`. React Native measures the
// view itself and writes the size into the fragment before asking for the
// paragraph, so what is asserted here is that the paragraph honours it and says
// where the view goes. See backlog/text.md.

TEST(an_inline_view_reserves_its_own_width) {
  const auto narrow = measure(makeTextWithAttachment(10.0F, 20.0F), ParagraphAttributes{}, -1.0F);
  const auto wide = measure(makeTextWithAttachment(120.0F, 20.0F), ParagraphAttributes{}, -1.0F);

  // The only difference is the box, so the wider box has to make a wider line.
  EXPECT(wide.width > narrow.width);
  EXPECT(wide.width - narrow.width > 100.0F);
}

TEST(a_tall_inline_view_makes_the_line_taller) {
  const auto shortOne = measure(makeTextWithAttachment(20.0F, 8.0F), ParagraphAttributes{}, -1.0F);
  const auto tall = measure(makeTextWithAttachment(20.0F, 90.0F), ParagraphAttributes{}, -1.0F);

  EXPECT(tall.height > shortOne.height);
}

TEST(an_inline_view_is_reported_at_the_size_it_was_given) {
  const auto frames = attachments(makeTextWithAttachment(40.0F, 20.0F), -1.0F);

  EXPECT(frames.size() == 1);
  EXPECT(frames[0].size.width == 40.0F);
  EXPECT(frames[0].size.height == 20.0F);
}

TEST(an_inline_view_is_positioned_after_the_text_before_it) {
  const auto frames = attachments(makeTextWithAttachment(40.0F, 20.0F), -1.0F);

  EXPECT(frames.size() == 1);
  // "before " precedes it, so it cannot sit at the left edge.
  EXPECT(frames[0].origin.x > 0.0F);
}

TEST(a_second_line_puts_an_inline_view_lower) {
  // Narrow enough that "before " and the 40pt box cannot share a line with
  // " after", which pushes the box onto a line of its own or the second line.
  const auto wide = attachments(makeTextWithAttachment(40.0F, 20.0F), -1.0F);
  const auto wrapped = attachments(makeTextWithAttachment(40.0F, 20.0F), 60.0F);

  EXPECT(wide.size() == 1);
  EXPECT(wrapped.size() == 1);
  EXPECT(wrapped[0].origin.y > wide[0].origin.y);
}

TEST(an_unmeasured_inline_view_is_reported_as_zero_rather_than_guessed) {
  // Nothing measured it, so there is no box to honour and no position to invent.
  const auto frames = attachments(makeTextWithAttachment(0.0F, 0.0F), -1.0F);

  EXPECT(frames.size() == 1);
  EXPECT(frames[0].size.width == 0.0F);
  EXPECT(frames[0].size.height == 0.0F);
}

TEST(a_paragraph_with_no_inline_views_reports_none) {
  const auto frames = attachments(makeText("just text"), -1.0F);
  EXPECT(frames.empty());
}
