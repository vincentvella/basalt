// `adjustsFontSizeToFit`: the search that finds how far to shrink a paragraph.
//
// The search is core's and the measuring is each host's, so this hands it a
// measurement that is arithmetic -- a paragraph whose size is proportional to
// its font size, which is what a single line of text actually is. That makes
// the ratio predictable and the assertions about the *rules* rather than about
// a font.

#include "TestHarness.h"

#include "FontFitting.h"

#include <limits>
#include <sstream>

using basalt::FontFit;
using basalt::FontFitBox;
using basalt::fontFitToBox;

namespace {

// A line of text `width` by `height` at its natural size, measured at a ratio.
//
// Linear in the ratio, which a single unwrapped line is: twice the font size is
// twice the width and twice the height.
auto line(double width, double height) {
  return [width, height](double ratio) {
    return FontFitBox{.width = width * ratio, .height = height * ratio};
  };
}

} // namespace

TEST(fit_text_that_already_fits_is_not_touched) {
  // Upstream's first move, and the reason the prop never grows anything: a
  // paragraph that fits is done, whatever room is left over.
  const FontFit fit = fontFitToBox(FontFit{}, line(50, 10), FontFitBox{.width = 200, .height = 100});
  EXPECT(!fit.scales());
  EXPECT_NEAR(fit.ratio, 1.0, 0.0001);
}

TEST(fit_text_twice_too_wide_is_halved) {
  // 200 wide in a 100 box: the ratio that fits is a half, and the search should
  // land within a hair of it rather than at some ratio that merely fits.
  const FontFit fit = fontFitToBox(FontFit{}, line(200, 10), FontFitBox{.width = 100, .height = 50});
  EXPECT(fit.scales());
  EXPECT(fit.ratio > 0.45);
  EXPECT(fit.ratio <= 0.5);
}

TEST(fit_the_height_can_be_what_binds) {
  // A paragraph narrow enough and too tall: the search has to shrink for the
  // dimension that does not fit, not only for the width.
  const FontFit fit = fontFitToBox(FontFit{}, line(10, 200), FontFitBox{.width = 100, .height = 50});
  EXPECT(fit.scales());
  EXPECT(fit.ratio <= 0.25);
}

TEST(fit_a_box_with_no_size_is_not_fitted_to) {
  // An infinite dimension is Yoga saying "as much as you need", and a
  // paragraph measured unconstrained always fits.
  const double infinity = std::numeric_limits<double>::infinity();
  EXPECT(!fontFitToBox(FontFit{}, line(200, 10), FontFitBox{.width = infinity, .height = 50})
              .scales());
  EXPECT(!fontFitToBox(FontFit{}, line(200, 10), FontFitBox{.width = 100, .height = infinity})
              .scales());
  // And a box of nothing is not a box: a paragraph in a zero-width column would
  // otherwise shrink to the floor.
  EXPECT(!fontFitToBox(FontFit{}, line(200, 10), FontFitBox{.width = 0, .height = 0}).scales());
}

TEST(fit_the_minimum_size_is_a_floor_and_not_a_refusal) {
  // The clamp belongs to the fitting rather than to the text: scaling by the
  // ratio and then clamping is what keeps a paragraph of mixed sizes in
  // proportion until the floor bites.
  FontFit limits;
  limits.minimum = 10.0;
  const FontFit fit =
      fontFitToBox(limits, line(400, 10), FontFitBox{.width = 100, .height = 50});
  EXPECT(fit.scales());
  // A 16pt fragment would want 4pt and gets the floor.
  EXPECT_NEAR(fit.apply(16.0), 10.0, 0.0001);
  // And a fragment already below the floor is raised to it, which is what a
  // minimum means: the search is about the paragraph, not about one fragment.
  EXPECT_NEAR(fit.apply(2.0), 10.0, 0.0001);
}

TEST(fit_the_maximum_size_bounds_the_scaled_result) {
  FontFit limits;
  limits.maximum = 20.0;
  FontFit scaled = limits;
  scaled.ratio = 0.5;
  EXPECT_NEAR(scaled.apply(80.0), 20.0, 0.0001);
  EXPECT_NEAR(scaled.apply(30.0), 15.0, 0.0001);
}

TEST(fit_an_unscaled_paragraph_is_not_clamped) {
  // The bounds are the fitting's, not the text's: a 200pt heading in an app
  // that named a maximum of 96 is still 200pt while it fits.
  FontFit limits;
  limits.maximum = 96.0;
  EXPECT_NEAR(limits.apply(200.0), 200.0, 0.0001);
}

TEST(fit_a_contradictory_pair_of_bounds_lets_the_minimum_win) {
  // An app that asked for a minimum above its maximum has said something
  // impossible; readable text is the better answer than invisible text.
  facebook::react::ParagraphAttributes attributes;
  attributes.minimumFontSize = 30.0F;
  attributes.maximumFontSize = 10.0F;
  const FontFit fit = basalt::fontFitLimits(attributes);
  EXPECT_NEAR(fit.minimum, 30.0, 0.0001);
  EXPECT_NEAR(fit.maximum, 30.0, 0.0001);
}

TEST(fit_unset_bounds_are_the_ones_ios_uses) {
  // NaN is React Native's "nothing here", and the defaults are
  // RCTTextLayoutManager's own rather than numbers invented here.
  const FontFit fit = basalt::fontFitLimits(facebook::react::ParagraphAttributes{});
  EXPECT_NEAR(fit.minimum, basalt::kFontFitMinimumSize, 0.0001);
  EXPECT_NEAR(fit.maximum, basalt::kFontFitMaximumSize, 0.0001);
}

TEST(fit_a_paragraph_that_cannot_fit_at_all_still_answers) {
  // Text a hundred times too big, with the floor in the way: the search closes
  // on the smallest ratio that fitted rather than running for ever, and the
  // clamp is what keeps the result legible.
  FontFit limits;
  limits.minimum = 8.0;
  const FontFit fit =
      fontFitToBox(limits, line(10000, 10), FontFitBox{.width = 100, .height = 50});
  EXPECT(fit.scales());
  EXPECT(fit.ratio > 0.0);
  EXPECT_NEAR(fit.apply(16.0), 8.0, 0.0001);
}

TEST(fit_a_minimum_scale_is_a_floor_on_the_ratio) {
  // `minimumFontScale` is the same floor as `minimumFontSize` said as a ratio,
  // which is Android's spelling of the prop. iOS ignores it; there is no reason
  // to here, the search being a search over exactly this number.
  FontFit limits;
  limits.minimumRatio = 0.75;
  const FontFit fit =
      fontFitToBox(limits, line(400, 10), FontFitBox{.width = 100, .height = 50});
  EXPECT(fit.scales());
  EXPECT(fit.ratio >= 0.75);
  // And the text does not fit at that ratio, which is the floor doing its job
  // rather than the search failing: 400 * 0.75 is still past 100.
  EXPECT_NEAR(fit.apply(16.0), 12.0, 0.0001);
}

TEST(fit_a_minimum_scale_of_one_refuses_to_shrink_at_all) {
  // A legal thing to ask for, and worth not reading as "unset": the paragraph
  // overflows rather than being made smaller.
  FontFit limits;
  limits.minimumRatio = 1.0;
  const FontFit fit =
      fontFitToBox(limits, line(400, 10), FontFitBox{.width = 100, .height = 50});
  EXPECT_NEAR(fit.ratio, 1.0, 0.0001);
}

TEST(fit_an_unset_minimum_scale_is_not_a_floor_of_zero) {
  facebook::react::ParagraphAttributes attributes;
  EXPECT_NEAR(basalt::fontFitLimits(attributes).minimumRatio, 0.0, 0.0001);
  // And a value outside React Native's own documented range is clamped into it
  // rather than trusted: 0 would be a floor of nothing and 2 would be a floor
  // above the ceiling.
  attributes.minimumFontScale = 0.0F;
  EXPECT_NEAR(basalt::fontFitLimits(attributes).minimumRatio, 0.01, 0.0001);
  attributes.minimumFontScale = 2.0F;
  EXPECT_NEAR(basalt::fontFitLimits(attributes).minimumRatio, 1.0, 0.0001);
}
