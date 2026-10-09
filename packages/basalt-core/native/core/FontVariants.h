// `fontVariant`, as OpenType feature tags.
//
// React Native's `FontVariant` is a bitmask of twenty-five flags: small caps,
// four number styles, and twenty stylistic alternates. Nothing read it, so
// `fontVariant: ['small-caps']` did nothing on any desktop.
//
// ## Why the tags are here and the rest is not
//
// A feature tag is the portable half. OpenType names each of these with four
// characters -- `smcp`, `onum`, `lnum`, `tnum`, `pnum`, `ss01` to `ss20` -- and
// two of the three toolkits take exactly that: Pango through
// `pango_attr_font_features_new`, DirectWrite through `DWRITE_FONT_FEATURE_TAG`.
//
// Core Text does not. It takes Apple's older AAT selectors, a type and a
// selector number per feature, which is what upstream's iOS half uses in
// `RCTFontFeatures` and what the AppKit host copies. So the flags are walked
// here, in one order, and each host turns a flag into its own vocabulary. The
// alternative -- tags in core and a tag-to-AAT table in AppKit -- would be a
// translation of a translation.

#pragma once

#include <react/renderer/attributedstring/TextAttributes.h>

#include <string>
#include <vector>

namespace basalt {

// The flags an app asked for, lowest bit first, so both hosts apply features in
// the same order. `Default` is not in the list: it is the absence of a variant
// rather than one of them.
inline std::vector<facebook::react::FontVariant> fontVariants(
    const facebook::react::TextAttributes &textAttributes) {
  using facebook::react::FontVariant;

  std::vector<FontVariant> variants;
  if (!textAttributes.fontVariant.has_value()) {
    return variants;
  }
  const int mask = static_cast<int>(*textAttributes.fontVariant);

  static const FontVariant kAll[] = {
      FontVariant::SmallCaps,          FontVariant::OldstyleNums,
      FontVariant::LiningNums,         FontVariant::TabularNums,
      FontVariant::ProportionalNums,   FontVariant::StylisticOne,
      FontVariant::StylisticTwo,       FontVariant::StylisticThree,
      FontVariant::StylisticFour,      FontVariant::StylisticFive,
      FontVariant::StylisticSix,       FontVariant::StylisticSeven,
      FontVariant::StylisticEight,     FontVariant::StylisticNine,
      FontVariant::StylisticTen,       FontVariant::StylisticEleven,
      FontVariant::StylisticTwelve,    FontVariant::StylisticThirteen,
      FontVariant::StylisticFourteen,  FontVariant::StylisticFifteen,
      FontVariant::StylisticSixteen,   FontVariant::StylisticSeventeen,
      FontVariant::StylisticEighteen,  FontVariant::StylisticNineteen,
      FontVariant::StylisticTwenty,
  };
  for (const FontVariant variant : kAll) {
    if ((mask & static_cast<int>(variant)) != 0) {
      variants.push_back(variant);
    }
  }
  return variants;
}

// The OpenType tag for one flag, or nullptr for one that has none.
//
// The four number styles and small caps are the standard tags; a stylistic
// alternate is `ssNN`, which is why they are a run rather than a table.
inline const char *openTypeTag(facebook::react::FontVariant variant) {
  using facebook::react::FontVariant;
  switch (variant) {
    case FontVariant::SmallCaps:
      return "smcp";
    case FontVariant::OldstyleNums:
      return "onum";
    case FontVariant::LiningNums:
      return "lnum";
    case FontVariant::TabularNums:
      return "tnum";
    case FontVariant::ProportionalNums:
      return "pnum";
    case FontVariant::StylisticOne:
      return "ss01";
    case FontVariant::StylisticTwo:
      return "ss02";
    case FontVariant::StylisticThree:
      return "ss03";
    case FontVariant::StylisticFour:
      return "ss04";
    case FontVariant::StylisticFive:
      return "ss05";
    case FontVariant::StylisticSix:
      return "ss06";
    case FontVariant::StylisticSeven:
      return "ss07";
    case FontVariant::StylisticEight:
      return "ss08";
    case FontVariant::StylisticNine:
      return "ss09";
    case FontVariant::StylisticTen:
      return "ss10";
    case FontVariant::StylisticEleven:
      return "ss11";
    case FontVariant::StylisticTwelve:
      return "ss12";
    case FontVariant::StylisticThirteen:
      return "ss13";
    case FontVariant::StylisticFourteen:
      return "ss14";
    case FontVariant::StylisticFifteen:
      return "ss15";
    case FontVariant::StylisticSixteen:
      return "ss16";
    case FontVariant::StylisticSeventeen:
      return "ss17";
    case FontVariant::StylisticEighteen:
      return "ss18";
    case FontVariant::StylisticNineteen:
      return "ss19";
    case FontVariant::StylisticTwenty:
      return "ss20";
    case FontVariant::Default:
      return nullptr;
  }
  return nullptr;
}

// Every flag an app asked for, as the comma-separated list Pango and
// DirectWrite take: `smcp=1,tnum=1`. Empty when nothing asked.
inline std::string fontFeatureSettings(
    const facebook::react::TextAttributes &textAttributes) {
  std::string settings;
  for (const auto variant : fontVariants(textAttributes)) {
    const char *const tag = openTypeTag(variant);
    if (tag == nullptr) {
      continue;
    }
    if (!settings.empty()) {
      settings += ",";
    }
    settings += tag;
    settings += "=1";
  }
  return settings;
}

} // namespace basalt
