// The arithmetic behind a CSS gradient: where its line runs and where its stops
// land.
//
// Shared by both hosts, so it is asserted once here rather than twice over a
// toolkit. What makes it worth asserting at all is that every wrong answer still
// draws a gradient: a line along the diagonal instead of the spec's
// perpendicular construction, stops spread evenly instead of pulled forward, a
// transition hint ignored. See core/Gradients.h.

#include "TestHarness.h"

#include "Gradients.h"

#include <cmath>
#include <sstream>

using basalt::GradientLine;
using basalt::GradientStop;
using basalt::gradientAngleForKeyword;
using basalt::linearGradientLineForAngle;
using basalt::resolveGradientStops;
using facebook::react::ColorStop;
using facebook::react::GradientKeyword;
using facebook::react::UnitType;
using facebook::react::ValueUnit;

namespace {

ColorStop stop(unsigned colour, float position, UnitType unit) {
  ColorStop result;
  result.color = facebook::react::SharedColor(colour);
  result.position = ValueUnit{position, unit};
  return result;
}

ColorStop unpositioned(unsigned colour) {
  return stop(colour, 0.0F, UnitType::Undefined);
}

// A hint: a position with no colour of its own.
ColorStop hint(float percent) {
  ColorStop result;
  result.position = ValueUnit{percent, UnitType::Percent};
  return result;
}

constexpr unsigned kRed = 0xFFFF0000;
constexpr unsigned kBlue = 0xFF0000FF;

bool near(float a, float b, float slack = 0.01F) { return std::fabs(a - b) <= slack; }

} // namespace

// The four right angles, which are the ones an app actually writes and the ones
// the general construction divides by zero on.
TEST(gradient_the_right_angles_run_along_the_box) {
  // 0deg is "to top": from the bottom edge to the top one.
  const GradientLine up = linearGradientLineForAngle(0, 100, 50);
  EXPECT(near(up.startY, 50) && near(up.endY, 0));
  EXPECT(near(up.startX, 0) && near(up.endX, 0));

  // 90deg is to the right, 180 down, 270 left.
  const GradientLine right = linearGradientLineForAngle(90, 100, 50);
  EXPECT(near(right.startX, 0) && near(right.endX, 100));
  const GradientLine down = linearGradientLineForAngle(180, 100, 50);
  EXPECT(near(down.startY, 0) && near(down.endY, 50));
  const GradientLine left = linearGradientLineForAngle(270, 100, 50);
  EXPECT(near(left.startX, 100) && near(left.endX, 0));
}

// 45 degrees on a square: the line runs corner to corner, which is the one case
// where the diagonal happens to be the right answer.
TEST(gradient_forty_five_degrees_on_a_square_is_the_diagonal) {
  const GradientLine line = linearGradientLineForAngle(45, 100, 100);
  EXPECT(near(line.startX, 0) && near(line.startY, 100));
  EXPECT(near(line.endX, 100) && near(line.endY, 0));
}

// And on a box that is not square it is not the diagonal, which is the case that
// tells the spec's construction from the obvious one.
//
// Two properties, both of them the spec's own words rather than a number read
// off this implementation. The line is W*|sin| + H*|cos| long, which for a wide
// box at 45 degrees is *shorter* than the diagonal -- 212 against 224 -- so a
// gradient drawn corner to corner would be stretched. And the perpendicular
// through the end point passes through the corner the gradient heads for, which
// is the construction itself: the end point here is outside the box, at
// (175,-25), and that is right.
TEST(gradient_forty_five_degrees_on_a_wide_box_is_not_the_diagonal) {
  const GradientLine line = linearGradientLineForAngle(45, 200, 100);
  const float diagonal = std::sqrt(200.0F * 200.0F + 100.0F * 100.0F);
  const float expected =
      (200.0F + 100.0F) * std::sin(static_cast<float>(basalt::kPi) / 4.0F);
  EXPECT(near(line.length(), expected, 0.1F));
  EXPECT(line.length() < diagonal);

  // Symmetric about the centre, whatever the angle.
  EXPECT(near((line.startX + line.endX) / 2, 100));
  EXPECT(near((line.startY + line.endY) / 2, 50));

  // (corner - end) is perpendicular to the line's own direction. The corner is
  // the top right one, y counting down, which is where 45 degrees points.
  const float dx = line.endX - line.startX;
  const float dy = line.endY - line.startY;
  EXPECT(near(dx * (200.0F - line.endX) + dy * (0.0F - line.endY), 0, 1.0F));
}

// A corner keyword is not 45 degrees either, for the same reason.
TEST(gradient_a_corner_keyword_follows_the_box_shape) {
  EXPECT(near(gradientAngleForKeyword(GradientKeyword::ToTopRight, 100, 100), 45));
  // Wide box: the angle to the top right is shallower than 45.
  EXPECT(gradientAngleForKeyword(GradientKeyword::ToTopRight, 200, 100) < 45);
  // Tall box: steeper.
  EXPECT(gradientAngleForKeyword(GradientKeyword::ToTopRight, 100, 200) > 45);
  // To bottom right is past 90, to top left past 270: the quadrants the
  // construction picks its corner from.
  EXPECT(gradientAngleForKeyword(GradientKeyword::ToBottomRight, 100, 100) > 90);
  EXPECT(gradientAngleForKeyword(GradientKeyword::ToTopLeft, 100, 100) > 270);
}

TEST(gradient_two_stops_with_no_positions_are_the_ends) {
  const auto stops = resolveGradientStops({unpositioned(kRed), unpositioned(kBlue)}, 100);
  EXPECT_EQ((long)stops.size(), 2L);
  EXPECT(near(stops[0].offset, 0));
  EXPECT(near(stops[1].offset, 1));
  EXPECT(near(stops[0].red, 1) && near(stops[0].blue, 0));
  EXPECT(near(stops[1].blue, 1) && near(stops[1].red, 0));
}

// A run with no positions is spread evenly between the ones that have them,
// which is step three of the fixup.
TEST(gradient_a_run_of_unpositioned_stops_is_spread_evenly) {
  const auto stops = resolveGradientStops(
      {unpositioned(kRed), unpositioned(kBlue), unpositioned(kRed), unpositioned(kBlue)}, 100);
  EXPECT_EQ((long)stops.size(), 4L);
  EXPECT(near(stops[0].offset, 0));
  EXPECT(near(stops[1].offset, 1.0F / 3.0F));
  EXPECT(near(stops[2].offset, 2.0F / 3.0F));
  EXPECT(near(stops[3].offset, 1));
}

// A position in points is a distance along the gradient line, so the same
// stylesheet means different offsets on a short line and a long one.
TEST(gradient_a_position_in_points_is_measured_along_the_line) {
  const auto shortLine =
      resolveGradientStops({unpositioned(kRed), stop(kBlue, 25, UnitType::Point)}, 100);
  EXPECT(near(shortLine[1].offset, 0.25F));
  const auto longLine =
      resolveGradientStops({unpositioned(kRed), stop(kBlue, 25, UnitType::Point)}, 200);
  EXPECT(near(longLine[1].offset, 0.125F));
}

// A position that goes backwards is pulled forward to the one before it, which
// makes `red 60%, blue 20%` a hard edge at 60% rather than a reversed ramp.
TEST(gradient_a_backwards_position_becomes_a_hard_edge) {
  const auto stops = resolveGradientStops(
      {stop(kRed, 60, UnitType::Percent), stop(kBlue, 20, UnitType::Percent)}, 100);
  EXPECT_EQ((long)stops.size(), 2L);
  EXPECT(near(stops[0].offset, 0.6F));
  EXPECT(near(stops[1].offset, 0.6F));
}

// A transition hint becomes a run of stops along the spec's curve, and the one
// at the hint's own position is the midpoint colour -- which is the whole point
// of a hint, and what tells this apart from ignoring it.
TEST(gradient_a_transition_hint_moves_the_midpoint_colour) {
  const auto stops = resolveGradientStops({unpositioned(kRed), hint(20), unpositioned(kBlue)}, 100);
  // Nine stops in place of the hint, so eleven.
  EXPECT_EQ((long)stops.size(), 11L);

  // The colour halfway between red and blue should now be at 20%, not at 50%.
  // Find the stop nearest to half red, half blue.
  size_t midpoint = 0;
  float best = 10.0F;
  for (size_t i = 0; i < stops.size(); i++) {
    const float distance = std::fabs(stops[i].red - 0.5F) + std::fabs(stops[i].blue - 0.5F);
    if (distance < best) {
      best = distance;
      midpoint = i;
    }
  }
  EXPECT(stops[midpoint].offset < 0.3F);
  EXPECT(stops[midpoint].offset > 0.1F);

  // Offsets never go backwards, whatever the hint does.
  for (size_t i = 1; i < stops.size(); i++) {
    EXPECT(stops[i].offset >= stops[i - 1].offset);
  }
}

// A hint in the middle is a plain linear ramp, so it is dropped rather than
// expanded into nine stops that say the same thing.
TEST(gradient_a_hint_in_the_middle_is_dropped) {
  const auto stops = resolveGradientStops({unpositioned(kRed), hint(50), unpositioned(kBlue)}, 100);
  EXPECT_EQ((long)stops.size(), 2L);
}

TEST(gradient_no_stops_is_no_gradient) {
  EXPECT_EQ((long)resolveGradientStops({}, 100).size(), 0L);
}
