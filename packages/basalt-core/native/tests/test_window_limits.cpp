// How big a window may be: the clamp, which is portable.
//
// Worth a unit test rather than only an end-to-end one, because the end-to-end
// half cannot run everywhere. Enforcing a limit needs a window manager to
// enforce it, and CI runs the Linux host under Xvfb, which has none -- so the
// scenario there asserts what the platform *says* it does and stops. This is
// the arithmetic underneath, and it runs on any machine.
//
// The clamp exists at all because the toolkits disagree. AppKit's `setFrame:`
// clamps a programmatic resize down to the maximum and not up to the minimum,
// so a window asked to be 300x200 under a minimum of 500x400 went to 300x200 --
// smaller than anything a person dragging its corner could have made it.

#include "TestHarness.h"

#include "WindowControl.h"

#include <sstream>

namespace {

// Back to no limits at all, which is the state every other test assumes.
struct NoLimits {
  ~NoLimits() {
    basalt::setWindowMinimumSize(0, 0);
    basalt::setWindowMaximumSize(0, 0);
  }
};

TEST(limits_nothing_is_clamped_by_default) {
  const NoLimits reset;
  double width = 123.0;
  double height = 45.0;
  basalt::constrainToWindowSizeLimits(width, height);
  EXPECT_EQ(width, 123.0);
  EXPECT_EQ(height, 45.0);
}

TEST(limits_a_minimum_raises_a_smaller_request) {
  const NoLimits reset;
  basalt::setWindowMinimumSize(500, 400);
  double width = 300.0;
  double height = 200.0;
  basalt::constrainToWindowSizeLimits(width, height);

  // Only where the platform says it does this. A desktop that reports
  // `minimumSize: false` must not quietly enforce one here: the whole value of
  // answering the question is that the answer can be trusted.
  if (basalt::windowCapabilities().minimumSize) {
    EXPECT_EQ(width, 500.0);
    EXPECT_EQ(height, 400.0);
  } else {
    EXPECT_EQ(width, 300.0);
    EXPECT_EQ(height, 200.0);
  }
}

TEST(limits_a_maximum_lowers_a_larger_request) {
  const NoLimits reset;
  basalt::setWindowMaximumSize(800, 600);
  double width = 1400.0;
  double height = 1100.0;
  basalt::constrainToWindowSizeLimits(width, height);

  if (basalt::windowCapabilities().maximumSize) {
    EXPECT_EQ(width, 800.0);
    EXPECT_EQ(height, 600.0);
  } else {
    // GTK4 removed `gtk_window_set_geometry_hints` and Wayland has no protocol
    // for a maximum, so Linux reports false and is left alone. Enforcing one
    // for a programmatic resize and not for a dragged corner would be a limit
    // that half exists.
    EXPECT_EQ(width, 1400.0);
    EXPECT_EQ(height, 1100.0);
  }
}

TEST(limits_a_request_already_inside_them_is_untouched) {
  const NoLimits reset;
  basalt::setWindowMinimumSize(400, 300);
  basalt::setWindowMaximumSize(1000, 800);
  double width = 700.0;
  double height = 500.0;
  basalt::constrainToWindowSizeLimits(width, height);
  EXPECT_EQ(width, 700.0);
  EXPECT_EQ(height, 500.0);
}

TEST(limits_zero_clears_one_rather_than_pinning_a_window_to_nothing) {
  const NoLimits reset;
  basalt::setWindowMinimumSize(500, 400);
  basalt::setWindowMinimumSize(0, 0);
  double width = 100.0;
  double height = 80.0;
  basalt::constrainToWindowSizeLimits(width, height);
  EXPECT_EQ(width, 100.0);
  EXPECT_EQ(height, 80.0);
}

TEST(limits_a_negative_is_the_same_as_clearing) {
  // An app clearing a constraint is more likely to pass 0 than -1, and neither
  // should be a window that cannot be narrower than minus one pixel.
  const NoLimits reset;
  basalt::setWindowMinimumSize(-1, -1);
  const basalt::WindowSizeLimits limits = basalt::windowSizeLimits();
  EXPECT_EQ(limits.minWidth, 0.0);
  EXPECT_EQ(limits.minHeight, 0.0);
}

TEST(limits_one_dimension_can_be_limited_without_the_other) {
  const NoLimits reset;
  basalt::setWindowMinimumSize(500, 0);
  double width = 300.0;
  double height = 10.0;
  basalt::constrainToWindowSizeLimits(width, height);
  if (basalt::windowCapabilities().minimumSize) {
    EXPECT_EQ(width, 500.0);
  }
  // Never raised: a limit of zero is no limit, not a limit of zero.
  EXPECT_EQ(height, 10.0);
}

} // namespace
