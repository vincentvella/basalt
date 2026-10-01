// An animated scroll, which is arithmetic and so can be tested without a
// display -- unlike almost everything else about a scroll view on these hosts.

#include "TestHarness.h"

#include "ScrollAnimation.h"

#include <cmath>
#include <sstream>

using basalt::ScrollAnimation;

namespace {

TEST(scroll_animation_a_target_equal_to_the_start_is_not_an_animation) {
  // So a caller can apply the offset and be done rather than running a curve
  // that goes nowhere -- and, more to the point, never reporting that it
  // finished.
  ScrollAnimation animation;
  EXPECT(!animation.start(10, 20, 10, 20));
  EXPECT(!animation.isRunning());
}

TEST(scroll_animation_arrives_exactly_at_its_target) {
  // Evaluated at the end rather than approached. An app that scrolls to a
  // computed offset and reads it back should get its own number, not one a
  // curve came close to.
  ScrollAnimation animation;
  EXPECT(animation.start(0, 0, 100, 250));

  double x = 0;
  double y = 0;
  while (animation.advance(1.0 / 60.0, x, y)) {
  }
  EXPECT_EQ(x, 100.0);
  EXPECT_EQ(y, 250.0);
  EXPECT(!animation.isRunning());
}

TEST(scroll_animation_moves_monotonically_towards_the_target) {
  ScrollAnimation animation;
  animation.start(0, 0, 0, 300);

  double x = 0;
  double y = 0;
  double previous = -1;
  int steps = 0;
  while (animation.advance(1.0 / 60.0, x, y)) {
    EXPECT(y >= previous);
    previous = y;
    steps++;
  }
  // Something between "instant" and "a second". At 60Hz a 0.3s curve is about
  // eighteen frames, and asserting the shape rather than the number keeps this
  // from failing if the duration is tuned.
  EXPECT(steps > 5);
  EXPECT(steps < 60);
}

TEST(scroll_animation_starts_and_ends_slowly) {
  // The property the curve was chosen for: zero derivative at both ends, so two
  // scrolls in a row have no visible seam. Asserted as "the first step is
  // smaller than a middle step", which is what that means in practice.
  ScrollAnimation animation;
  animation.start(0, 0, 0, 1000);

  double x = 0;
  double y = 0;
  const double step = ScrollAnimation::kDurationSeconds / 10.0;

  animation.advance(step, x, y);
  const double first = y;

  double previous = y;
  for (int i = 0; i < 4; i++) {
    animation.advance(step, x, y);
  }
  const double middle = y - previous;

  EXPECT(first < middle);
}

TEST(scroll_animation_a_stopped_animation_reports_nothing_further) {
  ScrollAnimation animation;
  animation.start(0, 0, 0, 500);
  double x = 0;
  double y = 0;
  animation.advance(0.05, x, y);
  animation.stop();
  EXPECT(!animation.isRunning());
  EXPECT(!animation.advance(0.05, x, y));
}

TEST(scroll_animation_a_long_frame_does_not_overshoot) {
  // A stalled frame -- a garbage collection, a slow mount -- hands this more
  // time than the whole animation. It has to land on the target rather than
  // past it, because an offset beyond the content is a scroll view showing
  // nothing.
  ScrollAnimation animation;
  animation.start(0, 0, 0, 400);
  double x = 0;
  double y = 0;
  EXPECT(!animation.advance(5.0, x, y));
  EXPECT_EQ(y, 400.0);
}

TEST(scroll_animation_runs_backwards_as_well) {
  ScrollAnimation animation;
  animation.start(0, 900, 0, 0);
  double x = 0;
  double y = 900;
  double previous = 901;
  while (animation.advance(1.0 / 60.0, x, y)) {
    EXPECT(y <= previous);
    previous = y;
  }
  EXPECT_EQ(y, 0.0);
}

} // namespace
