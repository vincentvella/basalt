#include "DeviceMetrics.h"

#include "FontScaling.h"

namespace basalt {

namespace {

// Which display a point is on, or nothing when no display contains it -- a
// window dragged to a monitor that has just been unplugged, or a host that has
// reported no displays at all.
const DisplayInfo *displayContaining(const std::vector<DisplayInfo> &displays,
                                     double x,
                                     double y) {
  for (const DisplayInfo &display : displays) {
    if (x >= display.x && x < display.x + display.width && y >= display.y &&
        y < display.y + display.height) {
      return &display;
    }
  }
  return nullptr;
}

const DisplayInfo *primaryDisplay(const std::vector<DisplayInfo> &displays) {
  for (const DisplayInfo &display : displays) {
    if (display.primary) {
      return &display;
    }
  }
  return displays.empty() ? nullptr : &displays.front();
}

} // namespace

DeviceMetrics deviceMetricsFrom(const WindowBounds &window,
                                double surfaceWidth,
                                double surfaceHeight,
                                const std::vector<DisplayInfo> &displays,
                                double fontScale) {
  const double scale = fontScale > 0.0 ? fontScale : 1.0;

  // The display the window's centre is on, falling back to the primary one: a
  // window that has not been placed yet is at the origin, which is the primary
  // display's corner on every host.
  const DisplayInfo *on = displayContaining(displays,
                                            window.x + window.width / 2.0,
                                            window.y + window.height / 2.0);
  if (on == nullptr) {
    on = primaryDisplay(displays);
  }

  DeviceMetrics metrics;
  metrics.window.scale = on != nullptr && on->scaleFactor > 0.0 ? on->scaleFactor : 1.0;
  metrics.window.fontScale = scale;
  // The surface when the host has started one, which is the area an app can
  // lay out in; the window's frame otherwise.
  metrics.window.width = surfaceWidth > 0.0 ? surfaceWidth : window.width;
  metrics.window.height = surfaceHeight > 0.0 ? surfaceHeight : window.height;

  metrics.screen = metrics.window;
  if (on != nullptr) {
    metrics.screen.width = on->width;
    metrics.screen.height = on->height;
  }

  // Nothing reported yet. The screen is a better answer than zero for an app
  // laying out during its first render; see the header.
  if (metrics.window.width <= 0.0 || metrics.window.height <= 0.0) {
    metrics.window.width = metrics.screen.width;
    metrics.window.height = metrics.screen.height;
  }

  return metrics;
}

DeviceMetrics currentDeviceMetrics() {
  double surfaceWidth = 0.0;
  double surfaceHeight = 0.0;
  lastKnownSurfaceSize(&surfaceWidth, &surfaceHeight);
  // The app's own window: `Dimensions` is singular and means the window the
  // app is running in, which is what core/WindowControl.h says of the surface
  // size beside it.
  return deviceMetricsFrom(lastKnownWindowBounds(kAppWindow),
                           surfaceWidth,
                           surfaceHeight,
                           lastKnownDisplays(),
                           static_cast<double>(systemFontScale()));
}

} // namespace basalt
