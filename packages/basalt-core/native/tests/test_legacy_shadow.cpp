// The iOS shadow props as a CSS box shadow. See core/LegacyShadow.h.
//
// Four props that every pre-`boxShadow` React Native component sets, and which
// no host here read. The conversion is shared because the alternative is each
// host doubling a blur radius on its own, which is exactly the kind of arithmetic
// that comes out half as big on one platform and nobody notices for a year.

#include "TestHarness.h"

#include "LegacyShadow.h"

#include <sstream>

using facebook::react::BaseViewProps;
using facebook::react::BoxShadow;
using facebook::react::ColorComponents;

namespace {

// Props as React Native hands them over, with nothing set but the shadow: the
// defaults for `shadowOffset` and `shadowRadius` are React Native's own, which
// is what makes "set only a colour and an opacity" a case worth testing.
BaseViewProps shadowProps(float red, float green, float blue, float alpha, double opacity) {
  BaseViewProps props;
  props.shadowColor = facebook::react::colorFromComponents(ColorComponents{red, green, blue, alpha});
  props.shadowOpacity = opacity;
  return props;
}

ColorComponents componentsOf(const BoxShadow &shadow) {
  return facebook::react::colorComponentsFromColor(shadow.color);
}

} // namespace

TEST(legacy_shadow_defaults_are_the_shadow_ios_would_draw) {
  const BaseViewProps props = shadowProps(0.0F, 0.0F, 0.0F, 1.0F, 0.5);
  const std::optional<BoxShadow> shadow = basalt::legacyShadow(props);
  EXPECT(shadow.has_value());
  if (!shadow.has_value()) {
    return;
  }

  // React Native's own defaults: offset (0, -3), radius 3.
  EXPECT_NEAR(shadow->offsetX, 0.0, 0.001);
  EXPECT_NEAR(shadow->offsetY, -3.0, 0.001);
  // Doubled, a CALayer radius being a standard deviation where CSS's is twice
  // one. A conversion that forgot this draws a shadow half as soft, which is
  // the sort of wrong that looks fine on its own and wrong beside the other
  // host.
  EXPECT_NEAR(shadow->blurRadius, 6.0, 0.001);
  // No spread, and not inset: the legacy props cannot ask for either.
  EXPECT_NEAR(shadow->spreadDistance, 0.0, 0.001);
  EXPECT(!shadow->inset);
}

// `shadowOpacity` multiplies the colour's alpha, which is what
// `CALayer.shadowOpacity` does on top of `shadowColor`. Both halves matter: an
// opacity ignored gives a shadow twice as dark, and a colour alpha ignored gives
// one that cannot be made translucent by its colour.
TEST(legacy_shadow_opacity_multiplies_the_colours_alpha) {
  const std::optional<BoxShadow> half =
      basalt::legacyShadow(shadowProps(0.0F, 0.0F, 0.0F, 1.0F, 0.5));
  EXPECT(half.has_value());
  EXPECT_NEAR(componentsOf(*half).alpha, 0.5, 0.01);

  const std::optional<BoxShadow> quarter =
      basalt::legacyShadow(shadowProps(0.0F, 0.0F, 0.0F, 0.5F, 0.5));
  EXPECT(quarter.has_value());
  EXPECT_NEAR(componentsOf(*quarter).alpha, 0.25, 0.01);

  // And the colour itself is the colour, which an alpha multiply can trample if
  // it is applied to the wrong component.
  const std::optional<BoxShadow> red =
      basalt::legacyShadow(shadowProps(1.0F, 0.0F, 0.0F, 1.0F, 1.0));
  EXPECT(red.has_value());
  EXPECT_NEAR(componentsOf(*red).red, 1.0, 0.01);
  EXPECT_NEAR(componentsOf(*red).green, 0.0, 0.01);
  EXPECT_NEAR(componentsOf(*red).alpha, 1.0, 0.01);
}

// An opacity over one is a value React Native will pass straight through, and
// a colour cannot hold an alpha above one.
TEST(legacy_shadow_a_large_opacity_is_clamped) {
  const std::optional<BoxShadow> shadow =
      basalt::legacyShadow(shadowProps(0.0F, 0.0F, 0.0F, 1.0F, 4.0));
  EXPECT(shadow.has_value());
  EXPECT_NEAR(componentsOf(*shadow).alpha, 1.0, 0.01);
}

// The gates, which are what keeps every view in every app from gaining a shadow:
// `shadowOpacity` defaults to zero and `shadowColor` to nothing.
TEST(legacy_shadow_nothing_is_asked_for_by_default) {
  const BaseViewProps plain;
  EXPECT(!basalt::legacyShadow(plain).has_value());
  EXPECT(basalt::allShadows(plain).empty());

  // A colour with no opacity, which is the default for every styled view that
  // sets `shadowColor` and forgets the rest.
  EXPECT(!basalt::legacyShadow(shadowProps(0.0F, 0.0F, 0.0F, 1.0F, 0.0)).has_value());
  // An opacity with no colour.
  BaseViewProps colourless;
  colourless.shadowOpacity = 0.5;
  EXPECT(!basalt::legacyShadow(colourless).has_value());
  // And a transparent colour, which is a colour React Native did parse.
  EXPECT(!basalt::legacyShadow(shadowProps(0.0F, 0.0F, 0.0F, 0.0F, 0.5)).has_value());
}

// Both mechanisms at once, which iOS allows: the layer's own shadow and the
// extra layers `boxShadow` gets. The legacy one goes last, which is the back of
// the list, so adding a `boxShadow` to an old component cannot be hidden behind
// the shadow it already had.
TEST(legacy_shadow_goes_behind_the_css_shadows) {
  BaseViewProps props = shadowProps(0.0F, 0.0F, 0.0F, 1.0F, 0.5);
  BoxShadow css;
  css.offsetX = 1;
  css.offsetY = 2;
  css.blurRadius = 4;
  css.color = facebook::react::colorFromComponents(ColorComponents{1.0F, 0.0F, 0.0F, 1.0F});
  props.boxShadow = {css};

  const std::vector<BoxShadow> all = basalt::allShadows(props);
  EXPECT_EQ((int)all.size(), 2);
  if (all.size() != 2) {
    return;
  }
  // The CSS one first, as the app wrote it.
  EXPECT_NEAR(all[0].offsetX, 1.0, 0.001);
  EXPECT_NEAR(componentsOf(all[0]).red, 1.0, 0.01);
  // The legacy one behind it, converted.
  EXPECT_NEAR(all[1].offsetY, -3.0, 0.001);
  EXPECT_NEAR(all[1].blurRadius, 6.0, 0.001);
}

// And a `boxShadow` on its own is untouched, which is the case that was already
// working and must stay that way: nothing is appended, nothing is reordered.
TEST(legacy_shadow_leaves_a_css_only_view_alone) {
  BaseViewProps props;
  BoxShadow css;
  css.offsetY = 4;
  css.blurRadius = 8;
  css.inset = true;
  css.color = facebook::react::colorFromComponents(ColorComponents{0.0F, 0.0F, 0.0F, 0.25F});
  props.boxShadow = {css};

  const std::vector<BoxShadow> all = basalt::allShadows(props);
  EXPECT_EQ((int)all.size(), 1);
  if (all.empty()) {
    return;
  }
  EXPECT(all[0].inset);
  EXPECT_NEAR(all[0].blurRadius, 8.0, 0.001);
}
