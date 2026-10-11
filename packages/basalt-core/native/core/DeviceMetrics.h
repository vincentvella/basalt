// What `Dimensions` and `PixelRatio` answer with.
//
// React Native's `Dimensions.get('window')` is one of the most-used APIs it
// has, and on this platform it reported a fiction: ReactCxxPlatform's
// `DeviceInfoModule::getConstants` returns a hardcoded 1280x720 with a
// `// TODO: Wire this to come from the actual app size` beside it and no seam
// to wire. `scale` came back 0 and `fontScale` was never initialised at all --
// the generated struct leaves it so -- which made `PixelRatio.get()` zero and
// `PixelRatio.roundToNearestPixel(x)` NaN, silently, in every app.
//
// Everything needed is already here: `lastKnownWindowBounds()` is the window
// the host reports on every change, `lastKnownDisplays()` carries each
// display's scale factor, and `systemFontScale()` is the text scale the font
// machinery already multiplies by. This is the arithmetic that turns the three
// into React Native's two payloads, written once so three hosts cannot
// disagree about which display a window is on.

#pragma once

#include "WindowControl.h"

#include <vector>

namespace basalt {

// One of React Native's `DisplayMetrics`: a size in logical pixels, how many
// physical pixels each of those is, and the text scale.
struct DisplayMetrics {
  double width{0.0};
  double height{0.0};
  // Never zero. A caller dividing by it -- which is exactly what
  // `PixelRatio.roundToNearestPixel` does -- should not have to check.
  double scale{1.0};
  double fontScale{1.0};
};

struct DeviceMetrics {
  DisplayMetrics window;
  DisplayMetrics screen;

  bool operator==(const DeviceMetrics &other) const {
    return window.width == other.window.width && window.height == other.window.height &&
        window.scale == other.window.scale && window.fontScale == other.window.fontScale &&
        screen.width == other.screen.width && screen.height == other.screen.height &&
        screen.scale == other.screen.scale && screen.fontScale == other.screen.fontScale;
  }
  bool operator!=(const DeviceMetrics &other) const { return !(*this == other); }
};

// The window and the screen it is on, from what the host has reported.
//
// `window` is the window's own size; `screen` is the display it sits on, which
// is the distinction React Native draws and which matters on a desktop in a way
// it never does on a phone: a window is a fraction of a screen, and an app
// deciding how much it can show wants the first while one deciding where to put
// itself wants the second.
//
// Which display a window is on is decided by its centre, so a window straddling
// two reports the one most of it is on. A window the host has not reported yet
// -- everything is zero -- borrows the primary display's size rather than
// reporting a zero an app would divide by; that is the first render, before the
// first layout, and a zero there is worse than an estimate.
// `surfaceWidth` and `surfaceHeight` are the area React lays out in, which is
// what `window` reports when it is known: a desktop window's frame includes a
// title bar the app cannot put anything in, and the two differ by exactly that
// on AppKit. Zero means the host has not started a surface yet, and then the
// window's own size is used instead.
DeviceMetrics deviceMetricsFrom(const WindowBounds &window,
                                double surfaceWidth,
                                double surfaceHeight,
                                const std::vector<DisplayInfo> &displays,
                                double fontScale);

// The same, from what the host last reported and what the font machinery is
// scaling by. Safe from any thread, which is the point: a TurboModule method
// runs on the JavaScript thread and the toolkit may only be asked on the UI
// one.
DeviceMetrics currentDeviceMetrics();

} // namespace basalt
