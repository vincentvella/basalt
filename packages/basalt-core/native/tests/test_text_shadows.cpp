// The text shadow a paragraph draws. See core/TextShadows.h.
//
// The resolution is shared because the rule is a decision rather than a
// reading: the props are per fragment and neither text engine can draw a
// different shadow per run, so the first fragment that asks for one wins. A
// host deciding that for itself would be a host deciding it differently.

#include "TestHarness.h"

#include "TextShadows.h"

#include <sstream>
#include <utility>
#include <vector>

using basalt::TextShadow;
using basalt::textShadow;
using facebook::react::AttributedString;
using facebook::react::ColorComponents;
using Fragment = facebook::react::AttributedString::Fragment;
using facebook::react::TextAttributes;

namespace {

Fragment fragmentWith(const TextAttributes &attributes, const std::string &text = "x") {
  Fragment fragment;
  fragment.string = text;
  fragment.textAttributes = attributes;
  return fragment;
}

TextAttributes shadowed(float dx, float dy, float radius, float alpha) {
  TextAttributes attributes;
  attributes.textShadowOffset = facebook::react::Size{dx, dy};
  attributes.textShadowRadius = radius;
  attributes.textShadowColor =
      facebook::react::colorFromComponents(ColorComponents{1.0F, 0.0F, 0.0F, alpha});
  return attributes;
}

AttributedString stringOf(std::vector<Fragment> fragments) {
  AttributedString string;
  for (auto &fragment : fragments) {
    // appendFragment takes an rvalue, which is how React Native's own callers
    // build one.
    string.appendFragment(std::move(fragment));
  }
  return string;
}

// Not `near`: that is a legacy macro from <minwindef.h>, which arrives
// through AttributedString.h on Windows and turns the declaration below into
// `bool (float, float)`. Every other core test that spells it `near` gets away
// with it by not including that header.
bool closeTo(float a, float b) { return std::fabs(a - b) < 0.001F; }

} // namespace

TEST(text_shadow_carries_its_offset_sigma_and_colour) {
  const auto shadow = textShadow(stringOf({fragmentWith(shadowed(2.0F, 3.0F, 4.0F, 0.5F))}));
  EXPECT(shadow.has_value());
  if (!shadow.has_value()) {
    return;
  }
  EXPECT(closeTo(shadow->dx, 2.0F));
  EXPECT(closeTo(shadow->dy, 3.0F));
  // The standard deviation as React Native gave it: iOS puts this straight into
  // NSShadow's blur radius, so it is not CSS's radius and must not be halved or
  // doubled here. Each host converts for its own call.
  EXPECT(closeTo(shadow->standardDeviation, 4.0F));
  EXPECT(closeTo(shadow->red, 1.0F));
  // Within a byte: a colour is packed to eight bits a channel on the way in, so
  // half alpha comes back as 128/255 rather than as 0.5 exactly.
  EXPECT(std::fabs(shadow->alpha - 0.5F) < 0.01F);
}

// The first fragment that asks for one, which is the rule and the limit.
TEST(text_shadow_is_the_first_fragments) {
  TextAttributes plain;
  const auto shadow = textShadow(stringOf({
      fragmentWith(plain, "no shadow here"),
      fragmentWith(shadowed(1.0F, 1.0F, 1.0F, 1.0F), "this one"),
      fragmentWith(shadowed(9.0F, 9.0F, 9.0F, 1.0F), "not this one"),
  }));
  EXPECT(shadow.has_value());
  EXPECT(shadow.has_value() && closeTo(shadow->dx, 1.0F));
}

// No colour is no shadow, which is React Native's default for every `<Text>`:
// the props are three and the colour is the one that turns it on.
TEST(text_shadow_needs_a_colour) {
  TextAttributes noColour;
  noColour.textShadowOffset = facebook::react::Size{2.0F, 2.0F};
  noColour.textShadowRadius = 3.0F;
  EXPECT(!textShadow(stringOf({fragmentWith(noColour)})).has_value());

  // And a transparent colour is a shadow an app asked to be invisible.
  EXPECT(!textShadow(stringOf({fragmentWith(shadowed(2.0F, 2.0F, 3.0F, 0.0F))})).has_value());
}

// A shadow with no offset and no blur is behind the glyphs and cannot be seen,
// so it is skipped rather than drawn: both hosts pay for a shadow pass.
TEST(text_shadow_with_no_offset_and_no_blur_is_nothing) {
  EXPECT(!textShadow(stringOf({fragmentWith(shadowed(0.0F, 0.0F, 0.0F, 1.0F))})).has_value());
  // A blur on its own is visible, though, and so is an offset on its own.
  EXPECT(textShadow(stringOf({fragmentWith(shadowed(0.0F, 0.0F, 2.0F, 1.0F))})).has_value());
  EXPECT(textShadow(stringOf({fragmentWith(shadowed(1.0F, 0.0F, 0.0F, 1.0F))})).has_value());
}

// `textShadowRadius` arrives as NaN when the style does not set it, which is how
// React Native spells "not set" for a Float -- and NaN in a blur is a crash or a
// blank paragraph depending on the renderer.
TEST(text_shadow_an_unset_radius_is_zero_rather_than_nan) {
  TextAttributes attributes;
  attributes.textShadowOffset = facebook::react::Size{2.0F, 0.0F};
  attributes.textShadowColor =
      facebook::react::colorFromComponents(ColorComponents{0.0F, 0.0F, 0.0F, 1.0F});
  // textShadowRadius is left at its default, which is quiet NaN.
  const auto shadow = textShadow(stringOf({fragmentWith(attributes)}));
  EXPECT(shadow.has_value());
  EXPECT(shadow.has_value() && closeTo(shadow->standardDeviation, 0.0F));

  // And a negative radius is clamped, which a stylesheet can ask for and a
  // gaussian cannot take.
  TextAttributes negative = shadowed(1.0F, 1.0F, -8.0F, 1.0F);
  const auto clamped = textShadow(stringOf({fragmentWith(negative)}));
  EXPECT(clamped.has_value() && closeTo(clamped->standardDeviation, 0.0F));
}

TEST(text_shadow_an_empty_string_has_none) {
  EXPECT(!textShadow(AttributedString{}).has_value());
}
