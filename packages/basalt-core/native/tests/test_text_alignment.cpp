// Which edge a paragraph's text sits against, and which way it runs.
//
// The two halves of one question that three hosts were each answering locally
// and differently; `core/TextAlignments.h` holds the measurements that prompted
// this. What is asserted here is the table itself, plus Unicode's rule P2 over
// UTF-8 -- the direction a paragraph runs when the app said `natural` or said
// nothing, which is the common case and the one every relative alignment needs.
//
// The GTK suite holds `textStartsRightToLeft` against `pango_find_base_dir`
// over the same scripts, so the rule is checked against a real implementation
// of it rather than only against this file's expectations.

#include "TestHarness.h"

#include "TextAlignments.h"
#include "TextDirection.h"

#include <sstream>
#include <string>

using basalt::PhysicalTextAlignment;
using basalt::physicalTextAlignment;
using basalt::textStartsRightToLeft;
using facebook::react::TextAlignment;

namespace {

// UTF-8 as a source file can carry it without depending on the compiler's idea
// of an execution charset.
const std::string kHebrew = "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d";          // Hebrew
const std::string kArabic = "\xd8\xa7\xd9\x84\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85"; // Arabic
const std::string kThaana = "\xde\x8b\xde\xa8";                          // Thaana
const std::string kNko = "\xdf\x92\xdf\x8a";                             // NKo

const char *name(PhysicalTextAlignment alignment) {
  switch (alignment) {
    case PhysicalTextAlignment::Left:
      return "left";
    case PhysicalTextAlignment::Center:
      return "center";
    case PhysicalTextAlignment::Right:
      return "right";
    case PhysicalTextAlignment::Justified:
      return "justified";
  }
  return "?";
}

std::string edge(std::optional<TextAlignment> alignment, bool rightToLeft) {
  return name(physicalTextAlignment(alignment, rightToLeft));
}

} // namespace

TEST(alignment_the_physical_two_are_the_same_in_both_directions) {
  // `left` means the left of the box, in any paragraph. This is the row AppKit
  // had wrong: it folded `left` into `natural`, so a right-to-left paragraph
  // with `textAlign: 'left'` drew flush right.
  EXPECT_EQ(edge(TextAlignment::Left, false), std::string("left"));
  EXPECT_EQ(edge(TextAlignment::Left, true), std::string("left"));
  EXPECT_EQ(edge(TextAlignment::Right, false), std::string("right"));
  EXPECT_EQ(edge(TextAlignment::Right, true), std::string("right"));
  EXPECT_EQ(edge(TextAlignment::Center, false), std::string("center"));
  EXPECT_EQ(edge(TextAlignment::Center, true), std::string("center"));
}

TEST(alignment_natural_follows_the_direction) {
  // React Native's default, and the answer for a paragraph that said nothing.
  EXPECT_EQ(edge(TextAlignment::Natural, false), std::string("left"));
  EXPECT_EQ(edge(TextAlignment::Natural, true), std::string("right"));
  EXPECT_EQ(edge(std::nullopt, false), std::string("left"));
  EXPECT_EQ(edge(std::nullopt, true), std::string("right"));
}

#if BASALT_RN_MINOR >= 87
TEST(alignment_start_and_end_are_the_edges_the_line_runs_between) {
  // The entry this file closes: `end` is where a line finishes, which in a
  // right-to-left paragraph is the left edge. All three hosts drew it flush
  // right, two of them by folding it into `right`.
  EXPECT_EQ(edge(TextAlignment::Start, false), std::string("left"));
  EXPECT_EQ(edge(TextAlignment::Start, true), std::string("right"));
  EXPECT_EQ(edge(TextAlignment::End, false), std::string("right"));
  EXPECT_EQ(edge(TextAlignment::End, true), std::string("left"));
}
#endif

TEST(alignment_justified_is_not_an_edge_and_stays_itself) {
  // It reaches the engine, which stretches the lines; the flush factor below is
  // for the last line, which goes against the start edge.
  EXPECT_EQ(edge(TextAlignment::Justified, false), std::string("justified"));
  EXPECT_EQ(edge(TextAlignment::Justified, true), std::string("justified"));
  EXPECT_NEAR(basalt::textFlushFactor(PhysicalTextAlignment::Justified, false), 0.0, 0.001);
  EXPECT_NEAR(basalt::textFlushFactor(PhysicalTextAlignment::Justified, true), 1.0, 0.001);
}

TEST(alignment_flush_factors_are_the_three_core_text_takes) {
  EXPECT_NEAR(basalt::textFlushFactor(PhysicalTextAlignment::Left, false), 0.0, 0.001);
  EXPECT_NEAR(basalt::textFlushFactor(PhysicalTextAlignment::Center, false), 0.5, 0.001);
  EXPECT_NEAR(basalt::textFlushFactor(PhysicalTextAlignment::Right, false), 1.0, 0.001);
  // The direction does not enter into it: the edge has already been resolved.
  EXPECT_NEAR(basalt::textFlushFactor(PhysicalTextAlignment::Left, true), 0.0, 0.001);
  EXPECT_NEAR(basalt::textFlushFactor(PhysicalTextAlignment::Right, true), 1.0, 0.001);
}

TEST(direction_a_right_to_left_script_makes_a_right_to_left_paragraph) {
  EXPECT(textStartsRightToLeft(kHebrew));
  EXPECT(textStartsRightToLeft(kArabic));
  EXPECT(textStartsRightToLeft(kThaana));
  EXPECT(textStartsRightToLeft(kNko));
}

TEST(direction_latin_and_every_other_left_to_right_script_does_not) {
  EXPECT(!textStartsRightToLeft("hello"));
  // Latin with diacritics, Japanese, Greek and Cyrillic, as UTF-8 bytes: this
  // file is compiled by MSVC too, which reads a source as the machine's code
  // page unless it is told otherwise, and would re-encode a literal written in
  // the script itself. The escapes say exactly what reaches the function.
  //
  // Split before the `e`: a hex escape in C++ is greedy, so "\x9fe" is one
  // escape and out of range rather than an eszett followed by a letter.
  EXPECT(!textStartsRightToLeft("Gr\xc3\xbc\xc3\x9f" "e"));             // Gruesse
  EXPECT(!textStartsRightToLeft("\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"));     // Japanese
  EXPECT(!textStartsRightToLeft("\xce\x95\xce\xbb\xce\xbb\xce\xb7"));         // Greek
  EXPECT(!textStartsRightToLeft("\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2"));         // Cyrillic
}

TEST(direction_the_first_strong_character_decides_and_the_rest_do_not) {
  // Rule P2: the *first* strong character, not the majority and not the last.
  EXPECT(!textStartsRightToLeft("hello " + kHebrew));
  EXPECT(textStartsRightToLeft(kHebrew + " hello"));
}

TEST(direction_punctuation_digits_and_spaces_are_skipped) {
  // What sits in front of a word in real text. A quotation mark deciding the
  // direction of the sentence after it is the bug this skipping prevents.
  EXPECT(textStartsRightToLeft("  \"" + kHebrew + "\""));
  EXPECT(textStartsRightToLeft("(1) " + kHebrew));
  EXPECT(!textStartsRightToLeft("(1) hello"));
  EXPECT(!textStartsRightToLeft("123"));
  EXPECT(!textStartsRightToLeft(""));
  EXPECT(!textStartsRightToLeft("   "));
  // An emoji is not a letter either.
  EXPECT(textStartsRightToLeft("\xf0\x9f\x98\x80 " + kHebrew));
}

TEST(direction_arabic_indic_digits_are_not_strong) {
  // U+0660..0669 are class AN, inside the Arabic block and not strong, so a
  // paragraph that starts with them is not right-to-left by P2. Reading the
  // block as a whole is the obvious mistake here.
  const std::string digits = "\xd9\xa1\xd9\xa2\xd9\xa3"; // Arabic-Indic 123
  EXPECT(!textStartsRightToLeft(digits));
  EXPECT(!textStartsRightToLeft(digits + " hello"));
  EXPECT(textStartsRightToLeft(digits + " " + kArabic));
  // The extended Arabic-Indic digits, U+06F0..06F9, which are class EN.
  const std::string extended = "\xdb\xb1\xdb\xb2"; // extended Arabic-Indic 12
  EXPECT(!textStartsRightToLeft(extended + " hello"));
  EXPECT(textStartsRightToLeft(extended + " " + kArabic));
}

TEST(direction_a_right_to_left_mark_is_strong_on_its_own) {
  // U+200F is class R and is how a string with no letters at all can still ask
  // for a right-to-left paragraph.
  EXPECT(textStartsRightToLeft("\xe2\x80\x8f 123"));
}

TEST(direction_broken_utf8_is_skipped_rather_than_refused) {
  // This decides an alignment. A string with a bad byte in it still has to lay
  // out, and refusing would mean a paragraph that does not draw.
  EXPECT(!textStartsRightToLeft("\xff\xfe"));
  EXPECT(textStartsRightToLeft("\xff" + kHebrew));
  // A truncated sequence at the end, which is what a cut-off string looks like.
  EXPECT(!textStartsRightToLeft("\xd7"));
}

TEST(direction_the_prop_wins_when_the_app_set_one) {
  using facebook::react::WritingDirection;
  EXPECT(!basalt::paragraphIsRightToLeft(WritingDirection::LeftToRight, kHebrew));
  EXPECT(basalt::paragraphIsRightToLeft(WritingDirection::RightToLeft, "hello"));
  // `natural` and nothing are the same question, and both ask the text.
  EXPECT(basalt::paragraphIsRightToLeft(WritingDirection::Natural, kHebrew));
  EXPECT(basalt::paragraphIsRightToLeft(std::nullopt, kHebrew));
  EXPECT(!basalt::paragraphIsRightToLeft(std::nullopt, "hello"));
}
