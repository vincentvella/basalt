// What a fragment's font size works out to. See core/FontScaling.h.
//
// Shared because `allowFontScaling` and `maxFontSizeMultiplier` are rules
// rather than drawing: upstream's clamping is copied from
// `RCTEffectiveFontSizeMultiplierFromTextAttributes`, and a host reimplementing
// it would be a host getting it slightly differently.

#include "TestHarness.h"

#include "FontScaling.h"

#include <cmath>

using basalt::effectiveFontSize;
using basalt::effectiveFontSizeMultiplier;
using facebook::react::TextAttributes;

namespace {

// Not `near`: that is a legacy macro from <minwindef.h>, which arrives
// through AttributedString.h on Windows and turns the declaration below into
// `bool (float, float)`. Every other core test that spells it `near` gets away
// with it by not including that header.
bool closeTo(float a, float b) { return std::fabs(a - b) < 0.001F; }

} // namespace

// Nothing set, and nothing scaling: the size an app asked for.
TEST(font_scaling_a_plain_fragment_is_its_own_size) {
  TextAttributes attributes;
  attributes.fontSize = 16.0;
  EXPECT(closeTo(effectiveFontSize(attributes, 13.0F, 1.0F), 16.0F));
  // fontSize unset is NaN, which is the host's default rather than zero.
  TextAttributes unsized;
  EXPECT(closeTo(effectiveFontSize(unsized, 13.0F, 1.0F), 13.0F));
}

// The platform's text scale, which is what GTK and Windows publish and macOS
// does not.
TEST(font_scaling_the_system_scale_applies) {
  TextAttributes attributes;
  attributes.fontSize = 20.0;
  EXPECT(closeTo(effectiveFontSize(attributes, 13.0F, 1.5F), 30.0F));
}

// The prop an app is likelier to set than any of the others.
TEST(font_scaling_allow_font_scaling_false_ignores_the_system) {
  TextAttributes attributes;
  attributes.fontSize = 20.0;
  attributes.allowFontScaling = false;
  EXPECT(closeTo(effectiveFontSize(attributes, 13.0F, 1.5F), 20.0F));
  EXPECT(closeTo(effectiveFontSizeMultiplier(attributes, 2.0F), 1.0F));

  // Unset means scaling, which is React Native's default and not this
  // platform's choice: `allowFontScaling.value_or(true)` is upstream's line.
  TextAttributes unsaid;
  unsaid.fontSize = 20.0;
  EXPECT(closeTo(effectiveFontSize(unsaid, 13.0F, 1.5F), 30.0F));
  TextAttributes allowed;
  allowed.fontSize = 20.0;
  allowed.allowFontScaling = true;
  EXPECT(closeTo(effectiveFontSize(allowed, 13.0F, 1.5F), 30.0F));
}

// A ceiling on growth, which is the other half of the pair.
TEST(font_scaling_max_font_size_multiplier_is_a_ceiling) {
  TextAttributes attributes;
  attributes.fontSize = 10.0;
  attributes.maxFontSizeMultiplier = 1.2;
  EXPECT(closeTo(effectiveFontSize(attributes, 13.0F, 2.0F), 12.0F));
  // Below the ceiling it changes nothing.
  EXPECT(closeTo(effectiveFontSize(attributes, 13.0F, 1.1F), 11.0F));
}

// Upstream's rule, and the reason it is worth a test of its own: 0 and NaN both
// mean "no limit", and a limit under 1 is ignored rather than shrinking text.
TEST(font_scaling_a_limit_under_one_is_no_limit) {
  TextAttributes attributes;
  attributes.fontSize = 10.0;
  attributes.maxFontSizeMultiplier = 0.0;
  EXPECT(closeTo(effectiveFontSize(attributes, 13.0F, 2.0F), 20.0F));

  attributes.maxFontSizeMultiplier = 0.5;
  EXPECT(closeTo(effectiveFontSize(attributes, 13.0F, 2.0F), 20.0F));

  TextAttributes unset;
  unset.fontSize = 10.0;
  EXPECT(std::isnan(unset.maxFontSizeMultiplier));
  EXPECT(closeTo(effectiveFontSize(unset, 13.0F, 2.0F), 20.0F));
}

// The prop and the platform multiply, which is the bug this test exists for.
//
// The first version of this had the prop win when it was set, which read well
// and cancelled the platform's scale on every paragraph in an app:
// `TextAttributes::defaultTextAttributes()` sets `fontSizeMultiplier` to 1
// rather than to NaN, so "set" is not a thing a fragment can fail to be. The
// numbers below are the ones that would have passed either way had they been
// chosen less carefully -- 3 times 1.5 is 4.5, which neither rule produces by
// accident.
TEST(font_scaling_a_multiplier_and_the_system_scale_multiply) {
  TextAttributes attributes;
  attributes.fontSize = 10.0;
  attributes.fontSizeMultiplier = 3.0;
  EXPECT(closeTo(effectiveFontSize(attributes, 13.0F, 1.5F), 45.0F));
  EXPECT(closeTo(effectiveFontSize(attributes, 13.0F, 1.0F), 30.0F));

  // What React Native actually sends, which is the default rather than nothing.
  TextAttributes defaulted;
  defaulted.fontSize = 10.0;
  defaulted.fontSizeMultiplier = 1.0;
  EXPECT(closeTo(effectiveFontSize(defaulted, 13.0F, 1.6F), 16.0F));

  // And `allowFontScaling={false}` turns both off, as upstream's `else` does.
  attributes.allowFontScaling = false;
  EXPECT(closeTo(effectiveFontSize(attributes, 13.0F, 1.5F), 10.0F));
}

// A setting is not worth trusting into negative or zero territory: text that
// scales to nothing is worse than text that does not scale.
TEST(font_scaling_a_scale_that_is_not_positive_is_one) {
  TextAttributes attributes;
  attributes.fontSize = 10.0;
  EXPECT(closeTo(effectiveFontSize(attributes, 13.0F, 0.0F), 10.0F));
  EXPECT(closeTo(effectiveFontSize(attributes, 13.0F, -2.0F), 10.0F));
  EXPECT(closeTo(effectiveFontSize(attributes, 13.0F, std::nanf("")), 10.0F));
}
