// Which HWND a surface id means.
//
// The same seam AppKitAppWindow.h and GtkAppWindow.h describe, written for
// this toolkit and for the same reason: the registry of open windows is
// `main_win32.cpp`'s, and everything that resolves one is compiled into the
// mounting library, which the host links rather than the other way round. So
// the host installs a lookup at startup and this is what the window module
// asks.
//
// Not installed is the normal case for a test binary: it opens no window, and
// should not grow a registry to keep compiling. `win32AppWindowFor` falls back
// to the active window, which is what every caller here did before windows had
// ids.

#pragma once

#include <react/renderer/core/ReactPrimitives.h>

#include <functional>

#include <windows.h>

namespace basalt {

// The app's own window, by the rule this host has always used: the active
// window, falling back to the foreground one. UI thread only.
HWND win32AppWindow();

using Win32AppWindowLookup = std::function<HWND(facebook::react::SurfaceId)>;
void setWin32AppWindowLookup(Win32AppWindowLookup lookup);

// The window with this surface id, or the app's own for `kAppWindow` and for
// an id the lookup does not know. UI thread only.
HWND win32AppWindowFor(facebook::react::SurfaceId window);

} // namespace basalt
