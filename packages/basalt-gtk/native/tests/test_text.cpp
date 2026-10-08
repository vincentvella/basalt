// Tests for the Pango text layer.
//
// These assert relationships rather than pixel counts. Font metrics differ
// between machines and fontconfig setups, so "this string is 214.5 points wide"
// is not a fact worth encoding; "constraining the width makes it wrap, and
// wrapping makes it taller" is.

#include "TestHarness.h"

#include "PangoTextLayout.h"
// For rn_pango_clip_height, the contract between measuring a clipped paragraph
// and painting one. It lives in the widget layer; see the comment there.
#include "RnView.h"

#include <react/renderer/attributedstring/AttributedString.h>
#include <react/renderer/attributedstring/ParagraphAttributes.h>

#include <sstream>
#include <thread>

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

// `numberOfLines={lines}` with one of the four `ellipsizeMode` values.
ParagraphAttributes limitedTo(int lines, EllipsizeMode mode) {
  ParagraphAttributes attributes;
  attributes.maximumNumberOfLines = lines;
  attributes.ellipsizeMode = mode;
  return attributes;
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

// `numberOfLines` with `ellipsizeMode: 'clip'`, which asks for a cut and no
// ellipsis. Pango acts on a line limit only while it is ellipsizing, so this is
// the one mode whose truncation the platform has to carry out itself: the
// measurement reports the height of the lines that stay, and the widget clips
// its painting to the same height. See backlog/text.md.

TEST(clip_truncates_the_way_the_ellipsizing_modes_do) {
  const auto unlimited = measure(makeText(kLongText), ParagraphAttributes{}, 200.0F);
  const auto clipped = measure(makeText(kLongText), limitedTo(2, EllipsizeMode::Clip), 200.0F);
  const auto ellipsized = measure(makeText(kLongText), limitedTo(2, EllipsizeMode::Tail), 200.0F);

  // The bug: clip was the only mode that did not truncate at all, so the
  // paragraph measured its full height and nothing was ever hidden.
  EXPECT(clipped.height < unlimited.height);

  // Clip hides exactly the lines tail replaces with an ellipsis, so the two
  // occupy the same box. Which of the two it is becomes a question about the
  // glyphs on the last line rather than about the paragraph's size. The
  // tolerance is a fraction of a line rather than a fraction of a point,
  // because the ellipsis can come from a fallback face whose metrics are a
  // little taller than the one the text is set in.
  EXPECT_NEAR(clipped.height, ellipsized.height, 2.0);
}

TEST(clip_keeps_exactly_the_number_of_lines_it_was_given) {
  const auto one = measure(makeText(kLongText), limitedTo(1, EllipsizeMode::Clip), 200.0F);
  const auto two = measure(makeText(kLongText), limitedTo(2, EllipsizeMode::Clip), 200.0F);
  const auto three = measure(makeText(kLongText), limitedTo(3, EllipsizeMode::Clip), 200.0F);

  // An off-by-one would be invisible to the test above: keeping one line too
  // few or too many still measures shorter than the whole paragraph. The
  // multiples hold because every line here is set in one size, which is also
  // why the cut is read off the lines themselves rather than computed this way.
  EXPECT(two.height > one.height);
  EXPECT(three.height > two.height);
  EXPECT_NEAR(two.height, one.height * 2.0F, 1.0);
  EXPECT_NEAR(three.height, one.height * 3.0F, 1.5);
}

TEST(a_clip_limit_no_line_reaches_changes_nothing) {
  const auto unlimited = measure(makeText(kLongText), ParagraphAttributes{}, 200.0F);
  const auto limited = measure(makeText(kLongText), limitedTo(50, EllipsizeMode::Clip), 200.0F);

  // A limit the text never reaches has to leave the paragraph exactly as it
  // was. The widget asks the same question before it clips, so a mistake here
  // would also cut a paragraph that had nothing to hide.
  EXPECT_NEAR(limited.height, unlimited.height, 0.51);
  EXPECT_NEAR(limited.width, unlimited.width, 0.51);
}

TEST(a_clipped_paragraph_tells_the_widget_where_to_cut) {
  const auto whole = measure(makeText(kLongText), ParagraphAttributes{}, 200.0F);

  PangoLayout *layout =
      basalt::buildTextLayout(makeText(kLongText), limitedTo(2, EllipsizeMode::Clip), 200.0F);
  float clipHeight = 0.0F;
  const gboolean clips = rn_pango_clip_height(layout, &clipHeight);
  g_object_unref(layout);

  // This is the number the widget builds its clip node from, which is what
  // makes the painted paragraph the one that was measured rather than a second
  // guess at it.
  EXPECT(clips);
  EXPECT(clipHeight > 0.0F);
  EXPECT(clipHeight < whole.height);
}

TEST(only_clip_asks_the_widget_for_a_clip) {
  const auto needsClip = [](EllipsizeMode mode) {
    PangoLayout *layout = basalt::buildTextLayout(makeText(kLongText), limitedTo(2, mode), 200.0F);
    float clipHeight = 0.0F;
    const gboolean clips = rn_pango_clip_height(layout, &clipHeight);
    g_object_unref(layout);
    return clips;
  };

  // Pango drops the surplus lines itself as soon as it is ellipsizing, so for
  // head, middle and tail there is nothing left over to hide, and a clip
  // applied anyway would shave the bottom off a line Pango meant to keep.
  EXPECT(!needsClip(EllipsizeMode::Head));
  EXPECT(!needsClip(EllipsizeMode::Middle));
  EXPECT(!needsClip(EllipsizeMode::Tail));
  EXPECT(needsClip(EllipsizeMode::Clip));
}

TEST(a_paragraph_with_no_line_limit_asks_for_no_clip) {
  // ParagraphAttributes defaults ellipsizeMode to Clip, that being the first
  // name in React Native's enum, so a paragraph that never mentioned
  // numberOfLines arrives here as a clipped one with no limit. If the widget
  // clipped that it would cut every ordinary wrapping paragraph on the screen.
  PangoLayout *layout = basalt::buildTextLayout(makeText(kLongText), ParagraphAttributes{}, 200.0F);
  float clipHeight = 0.0F;
  const gboolean clips = rn_pango_clip_height(layout, &clipHeight);
  g_object_unref(layout);

  EXPECT(!clips);
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

// The per-thread Pango context, which is what replaced a process-wide mutex.
// Pango hands each thread its own default font map precisely so that no lock is
// needed; a single shared context opted out of that and then had to lock to put
// it back. These assert the arrangement rather than the absence of a race, which
// no test here could show: see backlog/text.md.

TEST(each_thread_lays_out_against_its_own_context) {
  PangoLayout *here = basalt::buildTextLayout(makeText("Hello"), ParagraphAttributes{}, -1.0F);
  PangoContext *hereContext = pango_layout_get_context(here);

  PangoContext *thereContext = nullptr;
  std::thread worker([&thereContext] {
    PangoLayout *there = basalt::buildTextLayout(makeText("Hello"), ParagraphAttributes{}, -1.0F);
    // Read, not kept: the layout owns its context and this only needs identity.
    thereContext = pango_layout_get_context(there);
    g_object_unref(there);
  });
  worker.join();

  EXPECT(hereContext != nullptr);
  EXPECT(thereContext != nullptr);
  // The whole point. Share one context between the two and this is equality.
  EXPECT(hereContext != thereContext);

  g_object_unref(here);
}

TEST(a_thread_keeps_one_context_rather_than_building_one_per_layout) {
  PangoLayout *first = basalt::buildTextLayout(makeText("one"), ParagraphAttributes{}, -1.0F);
  PangoLayout *second = basalt::buildTextLayout(makeText("two"), ParagraphAttributes{}, -1.0F);

  // A context per call would mean a font map per call, which is the expensive
  // way to be correct and would throw away every glyph cache between paragraphs.
  EXPECT(pango_layout_get_context(first) == pango_layout_get_context(second));

  g_object_unref(first);
  g_object_unref(second);
}

TEST(measuring_from_two_threads_at_once_does_not_deadlock) {
  // With the mutex gone there is nothing left to deadlock on, which is worth one
  // test because the mutex used to be taken on both of these paths. It proves
  // only that: a race would not show up here.
  std::thread worker([] {
    for (int i = 0; i < 40; i++) {
      const auto measured = measure(makeText(kLongText), ParagraphAttributes{}, 180.0F);
      EXPECT(measured.height > 0.0F);
    }
  });
  for (int i = 0; i < 40; i++) {
    const auto measured = measure(makeText(kLongText), ParagraphAttributes{}, 220.0F);
    EXPECT(measured.height > 0.0F);
  }
  worker.join();
}
