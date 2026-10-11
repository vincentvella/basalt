// What `Dimensions` and `PixelRatio` are told, which until 2026-10-10 was a
// hardcoded 1280x720 with a zero scale and an uninitialised font scale. See
// core/DeviceMetrics.h.

#include "TestHarness.h"

#include "DeviceMetrics.h"

#include <sstream>
#include <vector>

using basalt::deviceMetricsFrom;
using basalt::DisplayInfo;
using basalt::WindowBounds;

namespace {

DisplayInfo display(double x, double width, double scale, bool primary) {
  DisplayInfo info;
  info.x = x;
  info.y = 0.0;
  info.width = width;
  info.height = 900.0;
  info.workX = x;
  info.workWidth = width;
  info.workHeight = 900.0;
  info.scaleFactor = scale;
  info.primary = primary;
  return info;
}

WindowBounds window(double x, double width, double height) {
  WindowBounds bounds;
  bounds.x = x;
  bounds.y = 0.0;
  bounds.width = width;
  bounds.height = height;
  return bounds;
}

} // namespace

TEST(device_metrics_report_the_window_and_the_screen_it_is_on) {
  const std::vector<DisplayInfo> displays{display(0.0, 1440.0, 2.0, true)};
  const auto metrics = deviceMetricsFrom(window(100.0, 900.0, 732.0), 900.0, 700.0, displays, 1.0);

  EXPECT_EQ((long)metrics.window.width, 900L);
  // The surface, not the frame: the window is 732 tall and 32 of that is a
  // title bar the app cannot lay anything out in.
  EXPECT_EQ((long)metrics.window.height, 700L);
  // The screen is the display, not the window: the distinction React Native
  // draws, and one that matters on a desktop where a window is a fraction of a
  // screen.
  EXPECT_EQ((long)metrics.screen.width, 1440L);
  EXPECT_EQ((long)metrics.screen.height, 900L);
  EXPECT_EQ((long)metrics.window.scale, 2L);
  EXPECT_EQ((long)metrics.screen.scale, 2L);
}

// Which display a window is on is its centre's, so a window straddling two
// reports the one most of it is on.
TEST(device_metrics_follow_the_window_to_another_display) {
  const std::vector<DisplayInfo> displays{display(0.0, 1440.0, 2.0, true),
                                          display(1440.0, 1920.0, 1.0, false)};

  const auto onFirst = deviceMetricsFrom(window(1000.0, 800.0, 600.0), 0.0, 0.0, displays, 1.0);
  EXPECT_EQ((long)onFirst.window.scale, 2L);
  EXPECT_EQ((long)onFirst.screen.width, 1440L);

  const auto onSecond = deviceMetricsFrom(window(2000.0, 800.0, 600.0), 0.0, 0.0, displays, 1.0);
  EXPECT_EQ((long)onSecond.window.scale, 1L);
  EXPECT_EQ((long)onSecond.screen.width, 1920L);
}

// `PixelRatio.get()` is this number and `roundToNearestPixel` divides by it, so
// a zero is a NaN in an app's layout arithmetic -- which is what upstream's
// hardcoded constants produced.
TEST(device_metrics_never_report_a_zero_scale) {
  const std::vector<DisplayInfo> displays{display(0.0, 1440.0, 0.0, true)};
  const auto metrics = deviceMetricsFrom(window(0.0, 900.0, 700.0), 0.0, 0.0, displays, 0.0);

  EXPECT_EQ((long)metrics.window.scale, 1L);
  EXPECT_EQ((long)metrics.window.fontScale, 1L);
  EXPECT_EQ((long)metrics.screen.scale, 1L);
  EXPECT_EQ((long)metrics.screen.fontScale, 1L);
}

// The text scale rides along, because `Dimensions` is where React Native puts
// it and `PixelRatio.getFontScale()` is what reads it.
TEST(device_metrics_carry_the_text_scale) {
  const std::vector<DisplayInfo> displays{display(0.0, 1440.0, 1.0, true)};
  const auto metrics = deviceMetricsFrom(window(0.0, 900.0, 700.0), 0.0, 0.0, displays, 1.5);

  EXPECT_EQ((long)(metrics.window.fontScale * 10), 15L);
  EXPECT_EQ((long)(metrics.screen.fontScale * 10), 15L);
}

// Before the host has reported a window at all, which is where an app's first
// render is. Zero would be divided by.
TEST(device_metrics_borrow_the_screen_before_the_first_frame) {
  const std::vector<DisplayInfo> displays{display(0.0, 1440.0, 2.0, true)};
  const auto metrics = deviceMetricsFrom(WindowBounds{}, 0.0, 0.0, displays, 1.0);

  EXPECT_EQ((long)metrics.window.width, 1440L);
  EXPECT_EQ((long)metrics.window.height, 900L);
}

// And with no displays either, which is a host that has reported nothing: the
// numbers are zero rather than invented, but the two scales are still one.
TEST(device_metrics_with_nothing_reported_are_zero_but_divisible) {
  const auto metrics = deviceMetricsFrom(WindowBounds{}, 0.0, 0.0, {}, 1.0);

  EXPECT_EQ((long)metrics.window.width, 0L);
  EXPECT_EQ((long)metrics.window.scale, 1L);
  EXPECT_EQ((long)metrics.window.fontScale, 1L);
}

// A window whose surface has not started yet reports the window itself, which
// is the closest thing there is to an answer before the first layout.
TEST(device_metrics_fall_back_to_the_window_before_a_surface_starts) {
  const std::vector<DisplayInfo> displays{display(0.0, 1440.0, 1.0, true)};
  const auto metrics = deviceMetricsFrom(window(0.0, 820.0, 640.0), 0.0, 0.0, displays, 1.0);

  EXPECT_EQ((long)metrics.window.width, 820L);
  EXPECT_EQ((long)metrics.window.height, 640L);
}
