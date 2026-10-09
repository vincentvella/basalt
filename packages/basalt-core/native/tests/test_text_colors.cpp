// A fragment's colours, and the `opacity` that multiplies them. See
// core/TextColors.h.
//
// Shared because the three rules are React Native's rather than a toolkit's:
// the default is opaque black and not the platform's label colour, opacity
// multiplies an alpha rather than replacing it, and a background nobody set is
// nothing rather than transparent black.

#include "TestHarness.h"

#include "TextColors.h"

#include <cmath>

using basalt::textBackgroundColor;
using basalt::textForegroundColor;
using basalt::textOpacity;
using facebook::react::ColorComponents;
using facebook::react::TextAttributes;

namespace {

// Not `near`: see test_font_scaling.cpp for the Windows macro that takes.
bool closeTo(float a, float b) { return std::fabs(a - b) < 0.01F; }

TextAttributes coloured(float red, float alpha) {
  TextAttributes attributes;
  attributes.foregroundColor =
      facebook::react::colorFromComponents(ColorComponents{red, 0.0F, 0.0F, alpha});
  return attributes;
}

} // namespace

// React Native's default, which is not the platform's.
TEST(text_colors_the_default_foreground_is_opaque_black) {
  TextAttributes attributes;
  const auto color = textForegroundColor(attributes);
  EXPECT(closeTo(color.red, 0.0F));
  EXPECT(closeTo(color.green, 0.0F));
  EXPECT(closeTo(color.blue, 0.0F));
  EXPECT(closeTo(color.alpha, 1.0F));
}

TEST(text_colors_opacity_multiplies_the_alpha_rather_than_replacing_it) {
  TextAttributes attributes = coloured(1.0F, 0.5F);
  attributes.opacity = 0.5;
  // Half of a half, within a byte: a colour is packed to eight bits a channel
  // on the way in.
  EXPECT(closeTo(textForegroundColor(attributes).alpha, 0.25F));

  // And it applies to the default black too, which an app setting opacity and
  // no colour depends on.
  TextAttributes bare;
  bare.opacity = 0.25;
  EXPECT(closeTo(textForegroundColor(bare).alpha, 0.25F));
}

TEST(text_colors_an_unset_opacity_changes_nothing) {
  TextAttributes attributes = coloured(1.0F, 0.75F);
  EXPECT(std::isnan(attributes.opacity));
  EXPECT(closeTo(textForegroundColor(attributes).alpha, 0.75F));
  EXPECT(closeTo(textOpacity(attributes), 1.0F));
}

// A stylesheet can ask for either, and neither is a thing an alpha can be.
TEST(text_colors_an_opacity_outside_zero_to_one_is_clamped) {
  TextAttributes high = coloured(1.0F, 1.0F);
  high.opacity = 4.0;
  EXPECT(closeTo(textForegroundColor(high).alpha, 1.0F));

  TextAttributes low = coloured(1.0F, 1.0F);
  low.opacity = -2.0;
  EXPECT(closeTo(textForegroundColor(low).alpha, 0.0F));
}

// Nothing, rather than transparent black: a host that drew it would paint a box
// behind text that asked for none.
TEST(text_colors_a_background_nobody_set_is_nothing) {
  TextAttributes attributes;
  EXPECT(!textBackgroundColor(attributes).has_value());

  attributes.opacity = 0.5;
  EXPECT(!textBackgroundColor(attributes).has_value());

  attributes.backgroundColor =
      facebook::react::colorFromComponents(ColorComponents{0.0F, 0.0F, 1.0F, 1.0F});
  const auto background = textBackgroundColor(attributes);
  EXPECT(background.has_value());
  EXPECT(background.has_value() && closeTo(background->blue, 1.0F));
  EXPECT(background.has_value() && closeTo(background->alpha, 0.5F));
}
