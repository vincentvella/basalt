// A corner radius grown by a shadow's spread. See core/ShadowShape.h.
//
// The curve is the part worth a test: an addition would pass the first two
// assertions below and fail every other one, and a shadow with the wrong corner
// is the kind of difference nobody looks at closely enough to report.

#include "TestHarness.h"

#include "ShadowShape.h"

#include <sstream>

using basalt::spreadRadius;

TEST(shadow_shape_a_square_corner_stays_square_without_a_spread) {
  EXPECT_NEAR(spreadRadius(0.0, 0.0), 0.0, 0.0001);
  EXPECT_NEAR(spreadRadius(12.0, 0.0), 12.0, 0.0001);
}

TEST(shadow_shape_a_radius_wider_than_the_spread_just_grows) {
  // Past the knee the curve is the addition: the ratio only bends a corner
  // tighter than the spread.
  EXPECT_NEAR(spreadRadius(20.0, 5.0), 25.0, 0.0001);
  EXPECT_NEAR(spreadRadius(10.0, 10.0), 20.0, 0.0001);
}

TEST(shadow_shape_a_tight_corner_is_rounded_more_gently_than_the_spread) {
  // A square corner with a wide spread stays square: the ratio is zero, so the
  // adjustment is multiplied by 1 + (-1)^3, which is nothing. Adding the spread
  // would answer 10 here and turn a card's corner into a circle.
  EXPECT_NEAR(spreadRadius(0.0, 10.0), 0.0, 0.0001);

  // And in between, less than the addition but more than nothing: a quarter of
  // the way up the ratio the cubic is still most of the way down.
  const double grown = spreadRadius(2.0, 8.0);
  EXPECT(grown > 2.0);
  EXPECT(grown < 10.0);
}

TEST(shadow_shape_a_negative_spread_shrinks_and_stops_at_zero) {
  // What an inset shadow's hole is built from.
  EXPECT_NEAR(spreadRadius(20.0, -5.0), 15.0, 0.0001);
  // Never negative: a radius that would go through zero is zero, and a negative
  // one would turn a geometry inside out.
  EXPECT_NEAR(spreadRadius(4.0, -20.0), 0.0, 0.0001);
  EXPECT(spreadRadius(0.0, -10.0) >= 0.0);
}
