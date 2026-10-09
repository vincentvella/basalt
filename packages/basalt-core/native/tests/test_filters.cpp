// The arithmetic behind `filter`, against the Filter Effects spec's own numbers.
//
// Seven of the nine functions are affine maps of colour, so the test for each is
// a colour in and a colour out rather than a matrix compared against itself: the
// spec says what grayscale(1) does to red, and that is checkable without
// agreeing on how a matrix is stored. See core/Filters.h.

#include "TestHarness.h"

#include "Filters.h"

#include <cmath>
#include <sstream>

using basalt::ColorMatrix;
using basalt::ResolvedFilters;
using basalt::resolveFilters;
using facebook::react::FilterFunction;
using facebook::react::FilterType;

namespace {

FilterFunction filter(FilterType type, double amount) {
  FilterFunction function;
  function.type = type;
  function.parameters = static_cast<facebook::react::Float>(amount);
  return function;
}

// The colour a list of filters makes of one colour, straight (not
// premultiplied), which is the space both hosts apply the matrix in.
struct Colour {
  float red;
  float green;
  float blue;
  float alpha;
};

Colour through(const std::vector<FilterFunction> &filters, Colour in) {
  const ResolvedFilters resolved = resolveFilters(filters);
  const float input[4] = {in.red, in.green, in.blue, in.alpha};
  float output[4] = {0, 0, 0, 0};
  resolved.matrix.apply(input, output);
  return Colour{output[0], output[1], output[2], output[3]};
}

bool near(float a, float b, float slack = 0.002F) { return std::fabs(a - b) <= slack; }

constexpr Colour kRed{1, 0, 0, 1};
constexpr Colour kWhite{1, 1, 1, 1};
constexpr Colour kBlack{0, 0, 0, 1};

} // namespace

// grayscale(1) replaces a colour with its luminance, and the weights are Rec.
// 709's: red is 0.2126 of white. A matrix built from the wrong weights -- the
// older 0.299/0.587/0.114 is the usual mistake -- gives 0.299 here.
TEST(filters_grayscale_uses_the_luminance_weights_the_spec_names) {
  const Colour grey = through({filter(FilterType::Grayscale, 1.0)}, kRed);
  EXPECT(near(grey.red, 0.2126F));
  EXPECT(near(grey.green, 0.2126F));
  EXPECT(near(grey.blue, 0.2126F));
  // Alpha is untouched: a filter that greys a colour does not make it
  // transparent.
  EXPECT(near(grey.alpha, 1.0F));

  // Half way is half way between the colour and its luminance.
  const Colour half = through({filter(FilterType::Grayscale, 0.5)}, kRed);
  EXPECT(near(half.red, (1.0F + 0.2126F) / 2.0F));
}

// saturate(0) is grayscale(1) from the other end, which is how the spec defines
// both, and a good check that one is not written twice with different numbers.
TEST(filters_saturate_at_zero_is_grayscale) {
  const Colour saturated = through({filter(FilterType::Saturate, 0.0)}, kRed);
  const Colour grey = through({filter(FilterType::Grayscale, 1.0)}, kRed);
  EXPECT(near(saturated.red, grey.red));
  EXPECT(near(saturated.green, grey.green));
  EXPECT(near(saturated.blue, grey.blue));

  // And over-saturating is allowed: the prop takes any non-negative number.
  const Colour over = through({filter(FilterType::Saturate, 2.0)}, Colour{0.5F, 0.5F, 0.6F, 1});
  EXPECT(over.blue > 0.6F);
}

TEST(filters_invert_turns_black_white_and_back) {
  const Colour inverted = through({filter(FilterType::Invert, 1.0)}, kBlack);
  EXPECT(near(inverted.red, 1.0F));
  EXPECT(near(inverted.green, 1.0F));
  EXPECT(near(inverted.blue, 1.0F));

  // Half an inversion is mid grey, whatever it started as: that is the point of
  // the interpolation rather than a plain one-minus.
  const Colour half = through({filter(FilterType::Invert, 0.5)}, kBlack);
  EXPECT(near(half.red, 0.5F));
  const Colour halfWhite = through({filter(FilterType::Invert, 0.5)}, kWhite);
  EXPECT(near(halfWhite.red, 0.5F));
}

TEST(filters_brightness_scales_and_contrast_pivots_on_grey) {
  const Colour brighter = through({filter(FilterType::Brightness, 2.0)}, Colour{0.25F, 0.25F, 0.25F, 1});
  EXPECT(near(brighter.red, 0.5F));

  // contrast(0) collapses everything onto mid grey; contrast(1) changes nothing.
  const Colour flat = through({filter(FilterType::Contrast, 0.0)}, kRed);
  EXPECT(near(flat.red, 0.5F));
  EXPECT(near(flat.blue, 0.5F));
  const Colour same = through({filter(FilterType::Contrast, 1.0)}, kRed);
  EXPECT(near(same.red, 1.0F));
  EXPECT(near(same.blue, 0.0F));
  // And doubling it pushes away from grey in both directions.
  const Colour harder = through({filter(FilterType::Contrast, 2.0)}, Colour{0.75F, 0.25F, 0.5F, 1});
  EXPECT(near(harder.red, 1.0F));
  EXPECT(near(harder.green, 0.0F));
  EXPECT(near(harder.blue, 0.5F));
}

TEST(filters_sepia_matches_the_matrix_the_spec_prints) {
  const Colour sepia = through({filter(FilterType::Sepia, 1.0)}, kRed);
  EXPECT(near(sepia.red, 0.393F));
  EXPECT(near(sepia.green, 0.349F));
  EXPECT(near(sepia.blue, 0.272F));
}

// A full turn is the identity, which is the cheapest check that the rotation is
// a rotation; and a quarter turn moves a hue without moving its luminance, which
// is the property the spec's matrix is built around.
TEST(filters_hue_rotate_comes_back_round) {
  const Colour full = through({filter(FilterType::HueRotate, 360.0)}, kRed);
  EXPECT(near(full.red, 1.0F, 0.01F));
  EXPECT(near(full.green, 0.0F, 0.01F));
  EXPECT(near(full.blue, 0.0F, 0.01F));

  const Colour quarter = through({filter(FilterType::HueRotate, 120.0)}, kRed);
  EXPECT(!near(quarter.red, 1.0F, 0.1F));
  const float before = 0.2126F;
  const float after = 0.2126F * quarter.red + 0.7152F * quarter.green + 0.0722F * quarter.blue;
  EXPECT(near(after, before, 0.02F));
}

// Two filters are one matrix, applied in the order they were written. Brightness
// then grayscale is not the same as grayscale then brightness for a saturated
// colour... except that both are linear, which is why this asserts the composed
// answer rather than an order-dependence that does not exist.
TEST(filters_a_list_of_colour_filters_is_one_matrix) {
  const ResolvedFilters resolved =
      resolveFilters({filter(FilterType::Grayscale, 1.0), filter(FilterType::Brightness, 2.0)});
  EXPECT(resolved.hasMatrix);
  EXPECT(near(resolved.blurRadius, 0.0F));

  const Colour both = through(
      {filter(FilterType::Grayscale, 1.0), filter(FilterType::Brightness, 2.0)}, kRed);
  EXPECT(near(both.red, 0.2126F * 2.0F));
  EXPECT(near(both.green, 0.2126F * 2.0F));
}

// Opacity is alpha, not colour, and several of them multiply.
TEST(filters_opacity_multiplies) {
  EXPECT(near(resolveFilters({filter(FilterType::Opacity, 0.5)}).opacity, 0.5F));
  EXPECT(near(
      resolveFilters({filter(FilterType::Opacity, 0.5), filter(FilterType::Opacity, 0.5)}).opacity,
      0.25F));
  EXPECT(near(resolveFilters({filter(FilterType::Grayscale, 1.0)}).opacity, 1.0F));
}

// Two blurs are one blur of the combined variance, which is what convolving two
// gaussians gives: 3 and 4 make 5, not 7.
TEST(filters_two_blurs_compose_in_quadrature) {
  EXPECT(near(resolveFilters({filter(FilterType::Blur, 8.0)}).blurRadius, 8.0F));
  const float composed =
      resolveFilters({filter(FilterType::Blur, 6.0), filter(FilterType::Blur, 8.0)}).blurRadius;
  EXPECT(near(composed, 10.0F, 0.01F));
}

// A drop shadow is the one function that does not commute with the others, so it
// is kept as a shadow rather than folded in -- and the rest of the list still
// applies, which is what stops one function taking a whole style with it.
TEST(filters_a_drop_shadow_is_kept_and_the_rest_still_applies) {
  FilterFunction shadow;
  shadow.type = FilterType::DropShadow;
  shadow.parameters = facebook::react::DropShadowParams{};

  const ResolvedFilters resolved =
      resolveFilters({shadow, filter(FilterType::Grayscale, 1.0)});
  EXPECT(resolved.hasDropShadow);
  EXPECT_EQ((int)resolved.dropShadows.size(), 1);
  EXPECT(resolved.hasMatrix);
  EXPECT(!resolved.empty());
}

// The numbers come through as React Native parsed them: an offset, a standard
// deviation -- not a CSS blur radius, which is what the field is named after --
// and a colour. Each host converts from the sigma to whatever its own shadow
// takes, so getting this wrong is a shadow twice as soft on one desktop.
TEST(filters_a_drop_shadow_carries_its_offset_sigma_and_colour) {
  FilterFunction function;
  function.type = FilterType::DropShadow;
  facebook::react::DropShadowParams params;
  params.offsetX = 4;
  params.offsetY = -6;
  params.standardDeviation = 3;
  params.color = facebook::react::colorFromComponents(
      facebook::react::ColorComponents{1.0F, 0.0F, 0.0F, 0.5F});
  function.parameters = params;

  const ResolvedFilters resolved = resolveFilters({function});
  EXPECT_EQ((int)resolved.dropShadows.size(), 1);
  if (resolved.dropShadows.empty()) {
    return;
  }
  const basalt::FilterShadow &shadow = resolved.dropShadows[0];
  EXPECT(near(shadow.dx, 4.0F, 0.001F));
  EXPECT(near(shadow.dy, -6.0F, 0.001F));
  EXPECT(near(shadow.standardDeviation, 3.0F, 0.001F));
  EXPECT(near(shadow.red, 1.0F, 0.01F));
  EXPECT(near(shadow.green, 0.0F, 0.01F));
  EXPECT(near(shadow.alpha, 0.5F, 0.01F));
}

// No colour is legal CSS -- it means `currentColor` -- and nothing here has one,
// so it is opaque black. The alternative is dropping a declaration that parsed,
// which is worse than a shadow in the wrong colour.
TEST(filters_a_drop_shadow_with_no_colour_is_black) {
  FilterFunction function;
  function.type = FilterType::DropShadow;
  function.parameters = facebook::react::DropShadowParams{};

  const ResolvedFilters resolved = resolveFilters({function});
  EXPECT_EQ((int)resolved.dropShadows.size(), 1);
  if (resolved.dropShadows.empty()) {
    return;
  }
  EXPECT(near(resolved.dropShadows[0].alpha, 1.0F, 0.01F));
  EXPECT(near(resolved.dropShadows[0].red, 0.0F, 0.01F));
}

// Several of them, in the order they were written: CSS applies a filter list
// left to right, so the first drop shadow is the one nearest the content, and a
// host that reversed them would put the wrong one on top.
TEST(filters_drop_shadows_keep_their_order) {
  FilterFunction first;
  first.type = FilterType::DropShadow;
  facebook::react::DropShadowParams nearParams;
  nearParams.offsetX = 1;
  first.parameters = nearParams;

  FilterFunction second;
  second.type = FilterType::DropShadow;
  facebook::react::DropShadowParams farParams;
  farParams.offsetX = 9;
  second.parameters = farParams;

  const ResolvedFilters resolved = resolveFilters({first, second});
  EXPECT_EQ((int)resolved.dropShadows.size(), 2);
  if (resolved.dropShadows.size() != 2) {
    return;
  }
  EXPECT(near(resolved.dropShadows[0].dx, 1.0F, 0.001F));
  EXPECT(near(resolved.dropShadows[1].dx, 9.0F, 0.001F));
}

TEST(filters_an_empty_list_is_nothing_to_do) {
  const ResolvedFilters resolved = resolveFilters({});
  EXPECT(resolved.empty());
  EXPECT(resolved.matrix.isIdentity());
  EXPECT(!resolved.hasDropShadow);

  // And a filter that does nothing is still nothing to do, which keeps a host
  // from pushing an identity matrix node for `brightness(1)`.
  EXPECT(resolveFilters({filter(FilterType::Brightness, 1.0)}).matrix.isIdentity());
}
