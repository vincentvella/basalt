// Tests for the Pango text layer.
//
// These assert relationships rather than pixel counts. Font metrics differ
// between machines and fontconfig setups, so "this string is 214.5 points wide"
// is not a fact worth encoding; "constraining the width makes it wrap, and
// wrapping makes it taller" is.

#include "TestHarness.h"

#include "GtkPixels.h"

#include "FontScaling.h"
#include "FontFitting.h"
#include "PangoTextLayout.h"
#include "TextDirection.h"
// For rn_pango_clip_height, the contract between measuring a clipped paragraph
// and painting one. It lives in the widget layer; see the comment there.
#include "RnView.h"

#include <react/renderer/attributedstring/AttributedString.h>
#include <react/renderer/attributedstring/ParagraphAttributes.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
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

// `textAlign: 'justify'`, asserted in pixels rather than in a Pango flag.
//
// Pango expresses it separately from the other alignments, through
// `pango_layout_set_justify`, so "is the flag set" is one bug away from "does
// the text reach the edge" -- and the other host's half of this was recorded as
// missing for months while Core Text had been doing it all along. See
// backlog/platform-macos.md. Measured here the same way as there: the rightmost
// ink on the first line, against the same text drawn flush left.
TEST(text_justified_lines_reach_both_edges) {
  const char *paragraph = "Justified text stretches every line but the last one";

  // Alignment is a *fragment* attribute, which is where React Native resolves it
  // onto: the paragraph settings come from the first fragment. See
  // PangoTextLayout.cpp.
  const auto aligned = [&](facebook::react::TextAlignment alignment) {
    TextAttributes attributes;
    attributes.fontSize = 14.0F;
    attributes.alignment = alignment;

    AttributedString::Fragment fragment;
    fragment.string = paragraph;
    fragment.textAttributes = attributes;

    AttributedString result;
    result.appendFragment(std::move(fragment));
    return result;
  };

  const auto rightmostInk = [&](bool justify) {
    const facebook::react::TextAlignment alignment =
        justify ? facebook::react::TextAlignment::Justified : facebook::react::TextAlignment::Left;
    PangoLayout *layout =
        basalt::buildTextLayout(aligned(alignment), ParagraphAttributes{}, 200.0F);

    RnView *view = rn_view_new(1);
    g_object_ref_sink(view);
    rn_view_set_frame(view, 0.0F, 0.0F, 200.0F, 80.0F);
    const GdkRGBA black{0.0F, 0.0F, 0.0F, 1.0F};
    rn_view_set_text_layout(view, layout, &black);

    const basalt::testing::RnPixels pixels = basalt::testing::renderView(view, 200, 80);
    // The first line, which is the top band of the image: whatever the font
    // metrics are, a 14 point line fits inside the top twenty rows.
    int rightmost = -1;
    for (int x = 0; x < 200; x++) {
      for (int y = 0; y < 20; y++) {
        if (pixels.at(x, y).alpha > 40) {
          rightmost = x;
          break;
        }
      }
    }

    g_object_unref(layout);
    g_object_unref(view);
    return rightmost;
  };

  const int flushLeft = rightmostInk(false);
  const int justified = rightmostInk(true);

  // Ink at all first: a paragraph that drew nothing would pass the comparison.
  EXPECT(flushLeft > 0);
  EXPECT(justified > flushLeft);
  // Stretched to the edge of the 200 point box.
  EXPECT(justified >= 190);
}

// `textTransform`, which was in the ignored list and is what an uppercase button
// label is made of.
//
// Applied before measurement, not at paint time, which is the part worth
// asserting: "shout" and "SHOUT" are different widths, so a host that
// transformed only on the way to the screen would lay the paragraph out at the
// wrong size and wrap in the wrong place.
//
// The non-ASCII cases are deliberate. `std::toupper` over bytes looks right in
// English and leaves "é" alone, and "straße" has no single-character uppercase at
// all -- the correct answer is longer than the input, which is also what makes
// the attachment-offset arithmetic worth having a case for.
namespace {

std::string transformedText(const std::string &text, facebook::react::TextTransform transform) {
  TextAttributes attributes;
  attributes.fontSize = 16.0F;
  attributes.textTransform = transform;

  AttributedString::Fragment fragment;
  fragment.string = text;
  fragment.textAttributes = attributes;

  AttributedString result;
  result.appendFragment(std::move(fragment));

  PangoLayout *layout = basalt::buildTextLayout(result, ParagraphAttributes{}, -1.0F);
  const char *laid = pango_layout_get_text(layout);
  std::string answer(laid != nullptr ? laid : "");
  g_object_unref(layout);
  return answer;
}

} // namespace

TEST(text_transform_uppercases_and_lowercases) {
  using facebook::react::TextTransform;
  EXPECT_EQ(transformedText("shout", TextTransform::Uppercase), std::string("SHOUT"));
  EXPECT_EQ(transformedText("WHISPER", TextTransform::Lowercase), std::string("whisper"));
  // Untouched without the prop, and with it set to none.
  EXPECT_EQ(transformedText("As Written", TextTransform::None), std::string("As Written"));
}

TEST(text_transform_knows_unicode_rather_than_bytes) {
  using facebook::react::TextTransform;
  EXPECT_EQ(transformedText("café", TextTransform::Uppercase), std::string("CAFÉ"));
  // One character in, two out: a byte-wise transform leaves this as "STRAßE".
  EXPECT_EQ(transformedText("straße", TextTransform::Uppercase), std::string("STRASSE"));
}

// Capitalize follows React Native's rule, surprising parts included: each word
// is capitalised *and lowered*, so "iOS" becomes "Ios", and a word starting with
// a digit is only lowered.
TEST(text_transform_capitalize_follows_react_natives_rule) {
  using facebook::react::TextTransform;
  EXPECT_EQ(transformedText("hello wide world", TextTransform::Capitalize),
            std::string("Hello Wide World"));
  EXPECT_EQ(transformedText("iOS and Android", TextTransform::Capitalize),
            std::string("Ios And Android"));
  EXPECT_EQ(transformedText("3rd place", TextTransform::Capitalize), std::string("3rd Place"));
}

// And it changes the measurement, which is the half a paint-time transform would
// get wrong.
TEST(text_transform_changes_what_the_paragraph_measures) {
  using facebook::react::TextTransform;
  const auto widthOf = [](facebook::react::TextTransform transform) {
    TextAttributes attributes;
    attributes.fontSize = 16.0F;
    attributes.textTransform = transform;
    AttributedString::Fragment fragment;
    fragment.string = "shout";
    fragment.textAttributes = attributes;
    AttributedString text;
    text.appendFragment(std::move(fragment));
    return measure(text, ParagraphAttributes{}, -1.0F).width;
  };

  EXPECT(widthOf(TextTransform::Uppercase) > widthOf(TextTransform::None));
}

// `textShadowColor`, `textShadowOffset` and `textShadowRadius`, in pixels.
//
// The resolution -- which fragment's shadow wins, and what an unset radius means
// -- is core's and is tested there. What only a picture can say is that the
// shadow is drawn, where it was offset to, and that it is a shadow of the
// *glyphs* rather than of the view: something dark appearing behind a paragraph
// would pass a test that only asked whether the picture changed.
//
// Nothing here assumes where the ink is. The font is whatever this machine has,
// so the plain rendering is measured first and every probe is relative to that
// box -- which is also what makes the test readable when it fails.
TEST(text_a_shadow_is_drawn_behind_the_glyphs_where_it_was_offset) {
  AttributedString::Fragment fragment;
  fragment.string = "H";
  facebook::react::TextAttributes attributes;
  attributes.fontSize = 48.0F;
  attributes.foregroundColor = facebook::react::colorFromComponents(
      facebook::react::ColorComponents{1.0F, 1.0F, 1.0F, 1.0F});
  fragment.textAttributes = attributes;
  AttributedString string;
  string.appendFragment(std::move(fragment));

  PangoLayout *layout = basalt::buildTextLayout(string, ParagraphAttributes{}, 200.0F);
  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);
  rn_view_set_frame(view, 0.0F, 0.0F, 200.0F, 100.0F);
  const GdkRGBA white{1.0F, 1.0F, 1.0F, 1.0F};
  rn_view_set_text_layout(view, layout, &white);

  // The glyph's own box, from the picture.
  const basalt::testing::RnPixels plain = basalt::testing::renderView(view, 200, 100);
  int left = 200;
  int right = -1;
  int top = 100;
  int bottom = -1;
  for (int x = 0; x < 200; x++) {
    for (int y = 0; y < 100; y++) {
      if (plain.at(x, y).alpha > 40) {
        left = std::min(left, x);
        right = std::max(right, x);
        top = std::min(top, y);
        bottom = std::max(bottom, y);
      }
    }
  }
  EXPECT(right > left);
  EXPECT(bottom > top);
  const int middle = (top + bottom) / 2;

  // A hard red shadow twenty points to the right: no blur, so the shadow is the
  // glyph's own silhouette in red, and the part of it past the glyph's right
  // edge is where nothing was drawn before.
  const GdkRGBA red{1.0F, 0.0F, 0.0F, 1.0F};
  rn_view_set_text_shadow(view, 20.0F, 0.0F, 0.0F, &red);
  const basalt::testing::RnPixels shadowed = basalt::testing::renderView(view, 200, 100);

  const auto redBetween = [&](const basalt::testing::RnPixels &pixels, int fromX, int toX) {
    for (int x = fromX; x <= toX; x++) {
      for (int y = top; y <= bottom; y++) {
        const basalt::testing::RnPixel pixel = pixels.at(x, y);
        if (pixel.red > 150 && pixel.green < 100 && pixel.blue < 100) {
          return true;
        }
      }
    }
    return false;
  };

  // Past the glyph, inside the twenty points the shadow moved: red.
  EXPECT(redBetween(shadowed, right + 1, right + 19));
  // The glyph is still on top of its own shadow rather than behind it: the
  // leftmost ink is white, not red.
  const basalt::testing::RnPixel onGlyph = shadowed.at(left + 1, middle);
  EXPECT(onGlyph.red > 150);
  EXPECT(onGlyph.green > 150);
  EXPECT(onGlyph.blue > 150);

  // It follows the glyphs rather than the box: above the glyph's own top there
  // is nothing, because the H's silhouette does not reach there. A view-shaped
  // shadow would have filled it.
  int redAboveTheGlyph = 0;
  for (int x = right + 1; x <= right + 19; x++) {
    const basalt::testing::RnPixel pixel = shadowed.at(x, std::max(top - 4, 0));
    if (pixel.red > 150 && pixel.green < 100) {
      redAboveTheGlyph++;
    }
  }
  EXPECT_EQ(redAboveTheGlyph, 0);

  // Taken away again, and the red goes with it: a shadow left behind is the
  // failure the filter shadows actually had.
  rn_view_set_text_shadow(view, 0.0F, 0.0F, 0.0F, nullptr);
  const basalt::testing::RnPixels cleared = basalt::testing::renderView(view, 200, 100);
  EXPECT(!redBetween(cleared, right + 1, right + 19));

  g_object_unref(layout);
  g_object_unref(view);
}

// The desktop's text scale, which GTK publishes as a font resolution.
//
// `gtk-xft-dpi` is what GNOME's "Large Text" moves, and it is in 1024ths of a
// dot per inch, so the arithmetic is worth asserting rather than reading: 96 is
// unscaled and a 1.25 scaling factor arrives as 120.
TEST(gtk_text_scale_comes_from_gtk_xft_dpi) {
  GtkSettings *settings = gtk_settings_get_default();
  EXPECT(settings != nullptr);
  if (settings == nullptr) {
    return;
  }
  int original = -1;
  g_object_get(settings, "gtk-xft-dpi", &original, nullptr);

  g_object_set(settings, "gtk-xft-dpi", 96 * 1024, nullptr);
  EXPECT(std::fabs(basalt::gtkTextScale(settings) - 1.0F) < 0.001F);

  g_object_set(settings, "gtk-xft-dpi", 120 * 1024, nullptr);
  EXPECT(std::fabs(basalt::gtkTextScale(settings) - 1.25F) < 0.001F);

  // What a display that said nothing reads as, which is not a scale of zero.
  g_object_set(settings, "gtk-xft-dpi", -1, nullptr);
  EXPECT(std::fabs(basalt::gtkTextScale(settings) - 1.0F) < 0.001F);

  EXPECT(std::fabs(basalt::gtkTextScale(nullptr) - 1.0F) < 0.001F);

  g_object_set(settings, "gtk-xft-dpi", original, nullptr);
}

// And what it does to a paragraph. Through `measure`, which is the function
// Yoga asks, so a scale that did not reach the layout would be a scale that
// changed what was drawn without changing what it was given room for.
TEST(gtk_a_text_scale_grows_what_a_paragraph_measures) {
  const auto widthAt = [](float scale, std::optional<bool> allowFontScaling) {
    basalt::setSystemFontScale(scale);
    TextAttributes attributes;
    attributes.fontSize = 16.0F;
    attributes.allowFontScaling = allowFontScaling;
    AttributedString::Fragment fragment;
    fragment.string = "scaled";
    fragment.textAttributes = attributes;
    AttributedString text;
    text.appendFragment(std::move(fragment));
    const float width = measure(text, ParagraphAttributes{}, -1.0F).width;
    basalt::setSystemFontScale(1.0F);
    return width;
  };

  const float plain = widthAt(1.0F, std::nullopt);
  EXPECT(widthAt(1.5F, std::nullopt) > plain);

  // The prop an app sets on a label whose box cannot grow.
  EXPECT(std::fabs(widthAt(1.5F, false) - plain) < 0.5F);
  EXPECT(widthAt(1.5F, true) > plain);
}

// `textDecorationColor`, in pixels rather than in attributes.
//
// Asserting the PangoAttrList would only prove this file and PangoTextLayout.cpp
// agree. What a picture proves is that Pango draws the underline in the colour
// it was given, which is the claim the support page makes.
//
// The text is white on nothing and the underline is red, so "a red pixel
// anywhere" is the whole assertion and it cannot be the glyphs: a white glyph
// has every channel high.
TEST(gtk_a_decoration_colour_reaches_the_underline) {
  const auto renderWith = [](bool coloured) {
    AttributedString::Fragment fragment;
    fragment.string = "Hxy";
    TextAttributes attributes;
    attributes.fontSize = 40.0F;
    attributes.foregroundColor = facebook::react::colorFromComponents(
        facebook::react::ColorComponents{1.0F, 1.0F, 1.0F, 1.0F});
    attributes.textDecorationLineType = facebook::react::TextDecorationLineType::Underline;
    if (coloured) {
      attributes.textDecorationColor = facebook::react::colorFromComponents(
          facebook::react::ColorComponents{1.0F, 0.0F, 0.0F, 1.0F});
    }
    fragment.textAttributes = attributes;
    AttributedString string;
    string.appendFragment(std::move(fragment));

    PangoLayout *layout = basalt::buildTextLayout(string, ParagraphAttributes{}, 200.0F);
    RnView *view = rn_view_new(1);
    g_object_ref_sink(view);
    rn_view_set_frame(view, 0.0F, 0.0F, 200.0F, 100.0F);
    const GdkRGBA white{1.0F, 1.0F, 1.0F, 1.0F};
    rn_view_set_text_layout(view, layout, &white);
    const basalt::testing::RnPixels pixels = basalt::testing::renderView(view, 200, 100);
    g_object_unref(view);
    g_object_unref(layout);

    int red = 0;
    for (int x = 0; x < 200; x++) {
      for (int y = 0; y < 100; y++) {
        const basalt::testing::RnPixel pixel = pixels.at(x, y);
        if (pixel.alpha > 40 && pixel.red > 150 && pixel.green < 110 && pixel.blue < 110) {
          red++;
        }
      }
    }
    return red;
  };

  // The negative control, and the reason this is two renders: an underline in
  // the text's own colour must put no red on the surface at all, or the count
  // below would be measuring something else.
  EXPECT_EQ(renderWith(false), 0);
  EXPECT(renderWith(true) > 0);
}

// `textDecorationStyle`, which Pango takes as an enum with no patterns in it.
// Double is a value it has; dotted and dashed are not, and fall back to a
// single line. Asserted through the attribute list, there being no honest
// picture of "a line that should have been dotted".
TEST(gtk_a_decoration_style_maps_to_what_pango_has) {
  const auto underlineOf = [](std::optional<facebook::react::TextDecorationStyle> style) {
    AttributedString::Fragment fragment;
    fragment.string = "Hxy";
    TextAttributes attributes;
    attributes.fontSize = 20.0F;
    attributes.textDecorationLineType = facebook::react::TextDecorationLineType::Underline;
    attributes.textDecorationStyle = style;
    fragment.textAttributes = attributes;
    AttributedString string;
    string.appendFragment(std::move(fragment));

    PangoLayout *layout = basalt::buildTextLayout(string, ParagraphAttributes{}, 200.0F);
    PangoAttrList *list = pango_layout_get_attributes(layout);
    PangoUnderline found = PANGO_UNDERLINE_NONE;
    if (list != nullptr) {
      PangoAttrIterator *iterator = pango_attr_list_get_iterator(list);
      do {
        const PangoAttribute *attribute =
            pango_attr_iterator_get(iterator, PANGO_ATTR_UNDERLINE);
        if (attribute != nullptr) {
          found = static_cast<PangoUnderline>(
              reinterpret_cast<const PangoAttrInt *>(attribute)->value);
        }
      } while (pango_attr_iterator_next(iterator));
      pango_attr_iterator_destroy(iterator);
    }
    g_object_unref(layout);
    return found;
  };

  using facebook::react::TextDecorationStyle;
  EXPECT(underlineOf(std::nullopt) == PANGO_UNDERLINE_SINGLE);
  EXPECT(underlineOf(TextDecorationStyle::Solid) == PANGO_UNDERLINE_SINGLE);
  EXPECT(underlineOf(TextDecorationStyle::Double) == PANGO_UNDERLINE_DOUBLE);
  // The wavy one a spell checker draws, which is what `wavy` means.
  EXPECT(underlineOf(TextDecorationStyle::Wavy) == PANGO_UNDERLINE_ERROR);
  // Pango has no pattern for either, so both are a single line and the gap is
  // recorded rather than approximated with something it does have.
  EXPECT(underlineOf(TextDecorationStyle::Dotted) == PANGO_UNDERLINE_SINGLE);
  EXPECT(underlineOf(TextDecorationStyle::Dashed) == PANGO_UNDERLINE_SINGLE);
}

// `fontVariant`, as the feature string Pango takes.
//
// Asserted as an attribute rather than in pixels, and that is a limit worth
// naming: whether `smcp=1` *changes* anything depends on the font having a
// small-caps table, and the font here is whatever the machine has. What this
// platform is responsible for is asking, which is what this checks.
TEST(gtk_a_font_variant_becomes_a_pango_feature_string) {
  const auto featuresOf = [](std::optional<facebook::react::FontVariant> variant) {
    AttributedString::Fragment fragment;
    fragment.string = "Figures 123";
    TextAttributes attributes;
    attributes.fontSize = 20.0F;
    attributes.fontVariant = variant;
    fragment.textAttributes = attributes;
    AttributedString text;
    text.appendFragment(std::move(fragment));

    PangoLayout *layout = basalt::buildTextLayout(text, ParagraphAttributes{}, -1.0F);
    PangoAttrList *list = pango_layout_get_attributes(layout);
    std::string found;
    if (list != nullptr) {
      PangoAttrIterator *iterator = pango_attr_list_get_iterator(list);
      do {
        const PangoAttribute *attribute =
            pango_attr_iterator_get(iterator, PANGO_ATTR_FONT_FEATURES);
        if (attribute != nullptr) {
          found = reinterpret_cast<const PangoAttrString *>(attribute)->value;
        }
      } while (pango_attr_iterator_next(iterator));
      pango_attr_iterator_destroy(iterator);
    }
    g_object_unref(layout);
    return found;
  };

  using facebook::react::FontVariant;
  EXPECT_EQ(featuresOf(std::nullopt), std::string());
  EXPECT_EQ(featuresOf(FontVariant::SmallCaps), std::string("smcp=1"));
  // A bitmask, in core's order rather than the order a stylesheet listed them.
  EXPECT_EQ(featuresOf(static_cast<FontVariant>(
                static_cast<int>(FontVariant::TabularNums)
                | static_cast<int>(FontVariant::SmallCaps))),
            std::string("smcp=1,tnum=1"));
}

// `TextAttributes::opacity`, in pixels, because an alpha attribute Pango
// ignored would look exactly like one it honoured.
TEST(gtk_a_fragment_opacity_lightens_the_glyphs) {
  const auto darkestInk = [](double opacity) {
    AttributedString::Fragment fragment;
    fragment.string = "H";
    TextAttributes attributes;
    attributes.fontSize = 48.0F;
    attributes.foregroundColor = facebook::react::colorFromComponents(
        facebook::react::ColorComponents{0.0F, 0.0F, 0.0F, 1.0F});
    attributes.opacity = opacity;
    fragment.textAttributes = attributes;
    AttributedString text;
    text.appendFragment(std::move(fragment));

    PangoLayout *layout = basalt::buildTextLayout(text, ParagraphAttributes{}, -1.0F);
    RnView *view = rn_view_new(1);
    g_object_ref_sink(view);
    rn_view_set_frame(view, 0.0F, 0.0F, 100.0F, 80.0F);
    // An opaque white backing, so "how dark did the glyph get" is a question
    // about the text and not about what is behind it.
    const GdkRGBA white{1.0F, 1.0F, 1.0F, 1.0F};
    rn_view_set_background_color(view, TRUE, &white);
    const GdkRGBA black{0.0F, 0.0F, 0.0F, 1.0F};
    rn_view_set_text_layout(view, layout, &black);

    const basalt::testing::RnPixels pixels = basalt::testing::renderView(view, 100, 80);
    int darkest = 255;
    for (int x = 0; x < 100; x++) {
      for (int y = 0; y < 80; y++) {
        darkest = std::min<int>(darkest, pixels.at(x, y).red);
      }
    }
    g_object_unref(view);
    g_object_unref(layout);
    return darkest;
  };

  // Opaque black ink reaches black; half-opaque cannot, and a quarter is
  // lighter still. Thresholds rather than exact values: antialiasing decides
  // the last few levels and the font decides how much ink there is.
  const int opaque = darkestInk(1.0);
  const int half = darkestInk(0.5);
  const int quarter = darkestInk(0.25);
  EXPECT(opaque < 40);
  EXPECT(half > 90 && half < 160);
  EXPECT(quarter > half);
}

// `baseWritingDirection`, which decides which edge a paragraph starts from.
//
// Asserted two ways, because each catches what the other cannot: Pango's own
// answer for the line it laid out, and where the ink actually landed. A layout
// built on a context with the wrong base direction would still report the
// direction it was asked for if only the first were checked.
TEST(gtk_a_base_writing_direction_resolves_and_moves_the_ink) {
  using facebook::react::WritingDirection;

  const auto laidOut = [](std::optional<WritingDirection> direction) {
    AttributedString::Fragment fragment;
    fragment.string = "abc";
    TextAttributes attributes;
    attributes.fontSize = 24.0F;
    attributes.foregroundColor = facebook::react::colorFromComponents(
        facebook::react::ColorComponents{0.0F, 0.0F, 0.0F, 1.0F});
    attributes.baseWritingDirection = direction;
    fragment.textAttributes = attributes;
    AttributedString text;
    text.appendFragment(std::move(fragment));
    // A width, so there is somewhere for a right-aligned line to go.
    return basalt::buildTextLayout(text, ParagraphAttributes{}, 200.0F);
  };

  // Pango's resolved direction for the first line, which is the engine's own
  // answer rather than ours.
  const auto resolvedOf = [](PangoLayout *layout) {
    PangoLayoutLine *line = pango_layout_get_line_readonly(layout, 0);
    return line != nullptr ? line->resolved_dir : PANGO_DIRECTION_NEUTRAL;
  };

  PangoLayout *natural = laidOut(std::nullopt);
  PangoLayout *ltr = laidOut(WritingDirection::LeftToRight);
  PangoLayout *rtl = laidOut(WritingDirection::RightToLeft);

  // "abc" is strongly left-to-right, so the natural answer is LTR and the
  // explicit LTR cannot be told from it. The RTL one is the interesting case:
  // the app overrode the algorithm.
  EXPECT(resolvedOf(natural) == PANGO_DIRECTION_LTR);
  EXPECT(resolvedOf(ltr) == PANGO_DIRECTION_LTR);
  EXPECT(resolvedOf(rtl) == PANGO_DIRECTION_RTL);

  // And where the ink is. Pango reads alignment relative to the base
  // direction, so the same unaligned paragraph starts at the left edge in LTR
  // and the right edge in RTL.
  const auto inkCentre = [](PangoLayout *layout) {
    RnView *view = rn_view_new(1);
    g_object_ref_sink(view);
    rn_view_set_frame(view, 0.0F, 0.0F, 200.0F, 60.0F);
    const GdkRGBA black{0.0F, 0.0F, 0.0F, 1.0F};
    rn_view_set_text_layout(view, layout, &black);
    const basalt::testing::RnPixels pixels = basalt::testing::renderView(view, 200, 60);
    long total = 0;
    long weight = 0;
    for (int x = 0; x < 200; x++) {
      for (int y = 0; y < 60; y++) {
        if (pixels.at(x, y).alpha > 40) {
          total += x;
          weight++;
        }
      }
    }
    g_object_unref(view);
    return weight > 0 ? static_cast<double>(total) / static_cast<double>(weight) : -1.0;
  };

  const double ltrCentre = inkCentre(ltr);
  const double rtlCentre = inkCentre(rtl);
  EXPECT(ltrCentre >= 0.0);
  EXPECT(rtlCentre >= 0.0);
  // Left half against right half, with the whole width between them.
  EXPECT(ltrCentre < 100.0);
  EXPECT(rtlCentre > 100.0);

  g_object_unref(natural);
  g_object_unref(ltr);
  g_object_unref(rtl);
}

// --- adjustsFontSizeToFit ----------------------------------------------------
//
// The search is core's and tested there; what these ask is the half that is
// Pango's -- that the ratio reaches the font sizes, that the fitted paragraph
// actually fits, and that a paragraph which never asked is left alone.

TEST(text_adjusts_font_size_to_fit_shrinks_until_it_fits) {
  ParagraphAttributes plain;
  const AttributedString label = makeText("Delete all of the messages", 32.0F);

  // Unconstrained, this is far wider than the box below.
  const Measured natural = measure(label, plain, -1.0F);
  EXPECT(natural.width > 200.0F);

  ParagraphAttributes fitting;
  fitting.adjustsFontSizeToFit = true;
  const basalt::FontFit fit = basalt::textFitScale(label, fitting, 200.0F, 40.0F);
  EXPECT(fit.scales());
  EXPECT(fit.ratio < 1.0);

  // And the fitted paragraph fits, which is the claim: measured through the
  // same builder the host paints with.
  PangoLayout *layout = basalt::buildTextLayout(label, fitting, 200.0F, fit);
  float width = 0;
  float height = 0;
  basalt::textLayoutSize(layout, &width, &height);
  g_object_unref(layout);
  EXPECT(width <= 200.0F);
  EXPECT(height <= 40.0F);
}

TEST(text_a_paragraph_that_did_not_ask_is_not_shrunk) {
  // The control, and the behaviour of every paragraph in an app that never set
  // the prop: the search does not run at all, which matters because it builds a
  // layout per probe.
  ParagraphAttributes plain;
  const AttributedString label = makeText("Delete all of the messages", 32.0F);
  EXPECT(!basalt::textFitScale(label, plain, 200.0F, 40.0F).scales());

  const Measured measured = measure(label, plain, 200.0F);
  // Wrapped and taller than the box, rather than shrunk into it.
  EXPECT(measured.height > 40.0F);
}

TEST(text_a_paragraph_that_already_fits_keeps_its_size) {
  ParagraphAttributes fitting;
  fitting.adjustsFontSizeToFit = true;
  const AttributedString label = makeText("Hi", 16.0F);
  EXPECT(!basalt::textFitScale(label, fitting, 300.0F, 100.0F).scales());
}

TEST(text_the_minimum_font_size_stops_the_shrinking) {
  // A floor the text cannot fit above: the ratio still comes back, and the size
  // it resolves to is the floor rather than something unreadable.
  ParagraphAttributes fitting;
  fitting.adjustsFontSizeToFit = true;
  fitting.minimumFontSize = 24.0F;
  const AttributedString label = makeText("Delete all of the messages", 32.0F);

  const basalt::FontFit fit = basalt::textFitScale(label, fitting, 60.0F, 30.0F);
  EXPECT_NEAR(fit.apply(32.0), 24.0, 0.0001);

  // Which is also what gets drawn: the layout is built with the same fit.
  PangoLayout *layout = basalt::buildTextLayout(label, fitting, 60.0F, fit);
  float width = 0;
  float height = 0;
  basalt::textLayoutSize(layout, &width, &height);
  g_object_unref(layout);
  // It does not fit -- it cannot -- and that is the floor doing its job rather
  // than the search failing.
  EXPECT(height > 30.0F);
}

TEST(text_a_line_limit_does_not_hide_the_overflow_from_the_search) {
  // `numberOfLines` with `adjustsFontSizeToFit` is the pairing a button label
  // is written with, and the reason the search measures the *untruncated*
  // paragraph: a one-line limit makes a layout that fits any box by throwing
  // text away, so a search that asked the truncated layout would shrink
  // nothing and the label would be ellipsised instead.
  ParagraphAttributes oneLine;
  oneLine.adjustsFontSizeToFit = true;
  oneLine.maximumNumberOfLines = 1;
  const AttributedString label = makeText("Delete all of the messages", 32.0F);

  const basalt::FontFit fit = basalt::textFitScale(label, oneLine, 200.0F, 40.0F);
  EXPECT(fit.scales());
  EXPECT(fit.ratio < 1.0);
}


// --- Which edge the text sits against -----------------------------------------
//
// `textAlign` is five values and two of them are relative: `start` and `end`
// name the edges a line runs between, so in a right-to-left paragraph `end` is
// the left one. All three hosts had this wrong, each in its own way, and the
// table they now agree on is in core/TextAlignments.h along with what each of
// them used to answer.
//
// Asserted against the ink, through `pango_layout_get_extents`, because this is
// a question about where the glyphs are rather than about what was asked for. A
// 300pt box and a short string, so the two edges are far apart and a wrong
// answer cannot be a rounding difference.

namespace {

// Hebrew, which is right-to-left with no explicit direction needed: its letters
// are strong R, so Unicode's rule P2 makes the paragraph right-to-left and both
// Pango and core/TextDirection.h say so.
const char *const kHebrew = "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d \xd7\xa2\xd7\x95\xd7\x9c\xd7\x9d";

// Where the text sits in a 300pt box: the left edge of the logical extents,
// which is 0 for flush left and about 222 for flush right.
float alignedAt(const char *text,
                std::optional<facebook::react::WritingDirection> direction,
                std::optional<facebook::react::TextAlignment> alignment) {
  TextAttributes attributes;
  attributes.fontSize = 16.0F;
  attributes.alignment = alignment;
  attributes.baseWritingDirection = direction;

  AttributedString::Fragment fragment;
  fragment.string = text;
  fragment.textAttributes = attributes;
  AttributedString string;
  string.appendFragment(std::move(fragment));

  PangoLayout *layout = basalt::buildTextLayout(string, ParagraphAttributes{}, 300.0F);
  PangoRectangle logical{};
  pango_layout_get_extents(layout, nullptr, &logical);
  const float x = static_cast<float>(logical.x) / PANGO_SCALE;
  g_object_unref(layout);
  return x;
}

// Flush left and flush right, as the two answers this can give. The text is
// about 78pt wide in a 300pt box, so the right edge is past 200 and the left is
// at 0; nothing in between is either.
bool isLeft(float x) {
  return x < 1.0F;
}

bool isRight(float x) {
  return x > 200.0F;
}

} // namespace

TEST(text_align_left_and_right_are_physical_in_both_directions) {
  using A = facebook::react::TextAlignment;
  using D = facebook::react::WritingDirection;

  // Latin text: nothing surprising, and the control for everything below.
  EXPECT(isLeft(alignedAt("hello world", std::nullopt, A::Left)));
  EXPECT(isRight(alignedAt("hello world", std::nullopt, A::Right)));

  // Hebrew with no explicit direction, which is the case Pango resolves itself.
  // This is the row that was wrong here: with `auto_dir` on, Pango reads
  // ALIGN_LEFT and ALIGN_RIGHT as *start* and *end*, so asking for the right
  // edge put the text on the left.
  EXPECT(isLeft(alignedAt(kHebrew, std::nullopt, A::Left)));
  EXPECT(isRight(alignedAt(kHebrew, std::nullopt, A::Right)));

  // And with the direction said out loud, where Pango's alignments are physical
  // and no inversion is wanted.
  EXPECT(isLeft(alignedAt(kHebrew, D::RightToLeft, A::Left)));
  EXPECT(isRight(alignedAt(kHebrew, D::RightToLeft, A::Right)));
}

TEST(text_align_natural_follows_the_paragraph_direction) {
  using A = facebook::react::TextAlignment;
  using D = facebook::react::WritingDirection;

  EXPECT(isLeft(alignedAt("hello world", std::nullopt, A::Natural)));
  EXPECT(isLeft(alignedAt("hello world", std::nullopt, std::nullopt)));
  // Resolved from the text, which is what `natural` means.
  EXPECT(isRight(alignedAt(kHebrew, std::nullopt, A::Natural)));
  EXPECT(isRight(alignedAt(kHebrew, std::nullopt, std::nullopt)));
  // And from the prop when there is one, even over Latin text.
  EXPECT(isRight(alignedAt("hello world", D::RightToLeft, A::Natural)));
  EXPECT(isRight(alignedAt("hello world", D::RightToLeft, std::nullopt)));
}

#if BASALT_RN_MINOR >= 87
TEST(text_align_start_and_end_are_the_edges_the_line_runs_between) {
  using A = facebook::react::TextAlignment;
  using D = facebook::react::WritingDirection;

  EXPECT(isLeft(alignedAt("hello world", std::nullopt, A::Start)));
  EXPECT(isRight(alignedAt("hello world", std::nullopt, A::End)));

  // The entry this closes: `end` in a right-to-left paragraph is the *left*
  // edge, and this host drew it flush right whether the direction came from the
  // text or from the prop.
  EXPECT(isRight(alignedAt(kHebrew, std::nullopt, A::Start)));
  EXPECT(isLeft(alignedAt(kHebrew, std::nullopt, A::End)));
  EXPECT(isRight(alignedAt(kHebrew, D::RightToLeft, A::Start)));
  EXPECT(isLeft(alignedAt(kHebrew, D::RightToLeft, A::End)));

  // Latin text in a paragraph the app said runs right to left, which is the
  // case that cannot be got right by looking at the text alone.
  EXPECT(isRight(alignedAt("hello world", D::RightToLeft, A::Start)));
  EXPECT(isLeft(alignedAt("hello world", D::RightToLeft, A::End)));
}
#endif

TEST(text_direction_agrees_with_pangos_own_answer) {
  // core/TextDirection.h implements Unicode's rule P2 because two of the three
  // text engines will not answer it. Pango will -- `pango_find_base_dir` is
  // that rule -- so this is where the shared one is held against a real
  // implementation of it, which is what makes "the rule below is P2" a
  // measurement rather than a claim.
  const char *const samples[] = {
      "hello",
      "Grüße",
      "123",
      "   ",
      "",
      "(1) hello",
      kHebrew,
      "\xd8\xa7\xd9\x84\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85",       // Arabic
      "\xde\x8b\xde\xa8",                                   // Thaana
      "\xdf\x92\xdf\x8a",                                   // NKo
      "\xd9\xa1\xd9\xa2\xd9\xa3",                             // Arabic-Indic digits
      "\xd9\xa1\xd9\xa2\xd9\xa3 \xd8\xa7\xd9\x84\xd8\xb3",            // digits then Arabic
      "hello \xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d",                 // Latin then Hebrew
      "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d hello",                 // Hebrew then Latin
      "\xe2\x80\x8f 123",                                    // a right-to-left mark
      "\xf0\x9f\x98\x80 \xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d",            // an emoji then Hebrew
  };

  for (const char *sample : samples) {
    // `pango_find_base_dir`, deliberately, and it is deprecated as of Pango
    // 1.56 -- which is why the deprecation is suppressed here rather than the
    // call being replaced. It is rule P2 and nothing else in Pango is: a
    // layout's own `pango_layout_get_direction` answers for the *character* at
    // an index, so over "١٢٣ السلام" it says left-to-right for the digit at
    // byte 0 while the paragraph it is in runs right to left. That is a correct
    // answer to a different question, and it is the question this file asked
    // first.
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    const PangoDirection pango =
        pango_find_base_dir(sample, static_cast<int>(strlen(sample)));
    G_GNUC_END_IGNORE_DEPRECATIONS
    const bool pangoSaysRtl = pango == PANGO_DIRECTION_RTL;
    const bool oursSaysRtl = basalt::textStartsRightToLeft(sample);
    if (pangoSaysRtl != oursSaysRtl) {
      std::ostringstream message;
      message << "pango_find_base_dir says " << (pangoSaysRtl ? "rtl" : "ltr")
              << " and textStartsRightToLeft says " << (oursSaysRtl ? "rtl" : "ltr") << " for \""
              << sample << "\"";
      ::basalt::testing::recordFailure(std::string(__FILE__) + ":" + std::to_string(__LINE__),
                                       message.str());
    }
  }
}

// --- Where the paragraph sits in its box --------------------------------------
//
// `textAlignVertical`, which `verticalAlign` becomes in React Native's own
// JavaScript. Asserted against the pixels, because the only observable
// difference is where the ink is: the box, the string and the measured size are
// all the same whichever end of it the text sits at.

namespace {

// The topmost row with ink in it, for a short paragraph in a 100pt-tall box.
int inkRowFor(float flush) {
  PangoLayout *layout = basalt::buildTextLayout(makeText("Up or down"), ParagraphAttributes{}, 200.0F);

  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);
  rn_view_set_frame(view, 0.0F, 0.0F, 200.0F, 100.0F);
  const GdkRGBA black{0.0F, 0.0F, 0.0F, 1.0F};
  rn_view_set_text_layout(view, layout, &black);
  rn_view_set_text_vertical_flush(view, flush);

  const basalt::testing::RnPixels pixels = basalt::testing::renderView(view, 200, 100);
  int topmost = -1;
  for (int y = 0; y < 100 && topmost < 0; y++) {
    for (int x = 0; x < 200; x++) {
      if (pixels.at(x, y).alpha > 40) {
        topmost = y;
        break;
      }
    }
  }
  g_object_unref(view);
  return topmost;
}

} // namespace

TEST(text_vertical_align_moves_the_paragraph_down_its_box) {
  const int top = inkRowFor(0.0F);
  const int middle = inkRowFor(0.5F);
  const int bottom = inkRowFor(1.0F);

  // Ink at all, first: a paragraph that drew nothing would pass every
  // comparison below.
  EXPECT(top >= 0);
  EXPECT(top < 20);
  // A 16pt line in a 100pt box leaves about 80 points of slack, so the three
  // positions are tens of pixels apart and no font difference can confuse them.
  EXPECT(middle > top + 20);
  EXPECT(bottom > middle + 20);
  EXPECT(bottom > 60);
}

TEST(text_vertical_align_a_paragraph_as_tall_as_its_box_does_not_move) {
  // The clamp, through the widget: with no slack there is nowhere to go, and a
  // negative offset would push the first line out through the top.
  PangoLayout *layout =
      basalt::buildTextLayout(makeText(kLongText), ParagraphAttributes{}, 200.0F);
  int height = 0;
  pango_layout_get_pixel_size(layout, nullptr, &height);

  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);
  // A box exactly as tall as the text needs.
  rn_view_set_frame(view, 0.0F, 0.0F, 200.0F, static_cast<float>(height));
  const GdkRGBA black{0.0F, 0.0F, 0.0F, 1.0F};
  rn_view_set_text_layout(view, layout, &black);
  rn_view_set_text_vertical_flush(view, 1.0F);

  const basalt::testing::RnPixels pixels = basalt::testing::renderView(view, 200, height);
  int topmost = -1;
  for (int y = 0; y < height && topmost < 0; y++) {
    for (int x = 0; x < 200; x++) {
      if (pixels.at(x, y).alpha > 40) {
        topmost = y;
        break;
      }
    }
  }
  g_object_unref(view);
  EXPECT(topmost >= 0);
  EXPECT(topmost < 20);
}
