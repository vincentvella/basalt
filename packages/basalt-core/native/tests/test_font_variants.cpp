// `fontVariant`, as flags and as OpenType tags. See core/FontVariants.h.

#include "TestHarness.h"

#include "FontVariants.h"

#include <sstream>
#include <string>

using basalt::fontFeatureSettings;
using basalt::fontVariants;
using basalt::openTypeTag;
using facebook::react::FontVariant;
using facebook::react::TextAttributes;

namespace {

TextAttributes with(int mask) {
  TextAttributes attributes;
  attributes.fontVariant = static_cast<FontVariant>(mask);
  return attributes;
}

} // namespace

TEST(font_variants_nothing_asked_is_no_features) {
  TextAttributes attributes;
  EXPECT(fontVariants(attributes).empty());
  EXPECT(fontFeatureSettings(attributes).empty());

  // And `Default` is the absence of a variant rather than one of them, which
  // matters because React Native spells "no variants" as that rather than as an
  // unset optional when a stylesheet says `fontVariant: []`.
  EXPECT(fontVariants(with(static_cast<int>(FontVariant::Default))).empty());
  EXPECT(fontFeatureSettings(with(static_cast<int>(FontVariant::Default))).empty());
}

TEST(font_variants_one_flag_is_one_tag) {
  EXPECT_EQ(fontFeatureSettings(with(static_cast<int>(FontVariant::SmallCaps))),
            std::string("smcp=1"));
  EXPECT_EQ(fontFeatureSettings(with(static_cast<int>(FontVariant::TabularNums))),
            std::string("tnum=1"));
  EXPECT_EQ(fontFeatureSettings(with(static_cast<int>(FontVariant::OldstyleNums))),
            std::string("onum=1"));
}

// A bitmask, which is the whole reason this is not a single value: a stylesheet
// asks for a list and React Native ORs it into one int.
TEST(font_variants_several_flags_come_back_in_a_fixed_order) {
  const int mask = static_cast<int>(FontVariant::TabularNums)
      | static_cast<int>(FontVariant::SmallCaps)
      | static_cast<int>(FontVariant::StylisticTwo);
  const auto variants = fontVariants(with(mask));
  EXPECT_EQ(variants.size(), std::size_t{3});
  // Lowest bit first, so both hosts apply the same features in the same order.
  EXPECT(variants[0] == FontVariant::SmallCaps);
  EXPECT(variants[1] == FontVariant::TabularNums);
  EXPECT(variants[2] == FontVariant::StylisticTwo);
  EXPECT_EQ(fontFeatureSettings(with(mask)), std::string("smcp=1,tnum=1,ss02=1"));
}

// Twenty alternates, and the tag is where an off-by-one would hide: `ss01` is
// StylisticOne and `ss20` is the last.
TEST(font_variants_a_stylistic_alternate_is_ss_and_its_number) {
  EXPECT_EQ(std::string(openTypeTag(FontVariant::StylisticOne)), std::string("ss01"));
  EXPECT_EQ(std::string(openTypeTag(FontVariant::StylisticNine)), std::string("ss09"));
  EXPECT_EQ(std::string(openTypeTag(FontVariant::StylisticTen)), std::string("ss10"));
  EXPECT_EQ(std::string(openTypeTag(FontVariant::StylisticTwenty)), std::string("ss20"));
  EXPECT(openTypeTag(FontVariant::Default) == nullptr);
}

// Every flag React Native declares has a tag, which is the check that catches
// an upstream addition rather than a typo.
TEST(font_variants_every_flag_has_a_tag) {
  int mask = 0;
  for (int bit = 1; bit <= 25; bit++) {
    mask |= 1 << bit;
  }
  const auto variants = fontVariants(with(mask));
  EXPECT_EQ(variants.size(), std::size_t{25});
  for (const auto variant : variants) {
    EXPECT(openTypeTag(variant) != nullptr);
  }
}
