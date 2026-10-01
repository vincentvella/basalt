// Tests for the fling model.
//
// Core's, so compiled into every platform's suite -- although only one platform
// uses it. macOS decelerates a scroll itself and the AppKit host only reports
// what the system sends; GTK emits one velocity and stops, so the coasting is
// this project's. See core/ScrollMomentum.h.
//
// Testable at all because the model is a pure function of a velocity and a
// clock: no widget, no frame clock, and no waiting a second for a fling to
// finish.

#include "TestHarness.h"

#include "ScrollMomentum.h"

#include <sstream>

namespace {

using basalt::ScrollMomentum;

// A frame at sixty a second, which is what a tick callback hands over.
constexpr double kFrame = 1.0 / 60.0;

// How far a fling travels in total, and how many frames it takes. Bounded, so
// a model that never stops fails the assertion rather than hanging the suite.
struct Flight {
  double distanceY{0};
  int frames{0};
};

Flight flyY(ScrollMomentum &momentum) {
  Flight flight;
  double dx = 0;
  double dy = 0;
  while (momentum.advance(kFrame, dx, dy) && flight.frames < 6000) {
    flight.distanceY += dy;
    flight.frames++;
  }
  flight.distanceY += dy;
  flight.frames++;
  return flight;
}

} // namespace

TEST(momentum_a_fling_travels_and_stops) {
  ScrollMomentum momentum;
  EXPECT(momentum.start(0, 1200, ScrollMomentum::kNormalDeceleration));
  EXPECT(momentum.isRunning());

  const Flight flight = flyY(momentum);
  EXPECT(!momentum.isRunning());
  // The numbers are the model's, not a target: 1200px/s at 0.998 per
  // millisecond comes out at about 595 pixels over about 111 frames, which is
  // roughly what the same flick does on a phone. Asserted as a range, so a
  // change in the constants is caught and a change in the integration step is
  // not.
  EXPECT(flight.distanceY > 450);
  EXPECT(flight.distanceY < 750);
  EXPECT(flight.frames > 60);
  EXPECT(flight.frames < 200);
}

TEST(momentum_a_faster_deceleration_travels_less) {
  ScrollMomentum normal;
  normal.start(0, 1200, ScrollMomentum::kNormalDeceleration);
  const Flight slow = flyY(normal);

  ScrollMomentum fast;
  fast.start(0, 1200, ScrollMomentum::kFastDeceleration);
  const Flight quick = flyY(fast);

  // What React Native's `decelerationRate` prop is for. 'fast' is the smaller
  // number and the shorter fling, which reads backwards until you remember it
  // is the fraction of the velocity that survives each millisecond.
  EXPECT(quick.distanceY < slow.distanceY);
  EXPECT(quick.frames < slow.frames);
}

TEST(momentum_a_fling_keeps_its_direction_on_both_axes) {
  ScrollMomentum momentum;
  momentum.start(-800, 1600, ScrollMomentum::kNormalDeceleration);
  double dx = 0;
  double dy = 0;
  momentum.advance(kFrame, dx, dy);
  EXPECT(dx < 0);
  EXPECT(dy > 0);
  // And in proportion: the model decays a velocity, it does not reshape one.
  EXPECT_NEAR(dy / dx, -2.0, 0.001);
}

TEST(momentum_a_slow_flick_is_not_a_fling) {
  ScrollMomentum momentum;
  // Below the stopping speed, so it would report a momentum scroll that began
  // and ended with nothing in between -- worse than not reporting one.
  EXPECT(!momentum.start(0, 5, ScrollMomentum::kNormalDeceleration));
  EXPECT(!momentum.isRunning());
  double dx = 0;
  double dy = 0;
  EXPECT(!momentum.advance(kFrame, dx, dy));
  EXPECT_NEAR(dx, 0.0, 0.0001);
  EXPECT_NEAR(dy, 0.0, 0.0001);
}

TEST(momentum_stopping_ends_it_at_once) {
  ScrollMomentum momentum;
  momentum.start(0, 2000, ScrollMomentum::kNormalDeceleration);
  momentum.stop();
  EXPECT(!momentum.isRunning());
  double dx = 0;
  double dy = 0;
  EXPECT(!momentum.advance(kFrame, dx, dy));
  EXPECT_NEAR(dy, 0.0, 0.0001);
}

TEST(momentum_a_stalled_frame_clock_does_not_teleport_the_list) {
  ScrollMomentum momentum;
  momentum.start(0, 1000, ScrollMomentum::kNormalDeceleration);
  double dx = 0;
  double dy = 0;
  // A whole second between frames -- a window dragged between monitors, or a
  // machine that swapped. Without a clamp this is a thousand pixels in one
  // step, which reads as the list jumping rather than coasting.
  momentum.advance(1.0, dx, dy);
  EXPECT(dy <= 100.0);
}

TEST(momentum_a_deceleration_rate_of_one_would_never_stop) {
  ScrollMomentum momentum;
  // `decelerationRate` is a float prop an app can set to anything. A rate of 1
  // keeps the whole velocity every millisecond, which is a list that scrolls
  // for ever; the model clamps rather than trusting it.
  momentum.start(0, 1000, 1.0);
  const Flight flight = flyY(momentum);
  EXPECT(flight.frames < 6000);
  EXPECT(!momentum.isRunning());
}
