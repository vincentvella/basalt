// The window this process owns, for the parts of the host that need it and are
// not the window module.
//
// `NSApp.keyWindow` and `NSApp.mainWindow` are both nil whenever the app is not
// the active one, and two different things were asking only those: a bounds read
// answered 0x0, and a menu role sent through the responder chain went nowhere.
// Both are what an app looks like when it was launched while something else had
// focus, which is most of the time under a test harness and some of the time on
// a desktop.
//
// So the rule lives in one place. It is GTK's rule, from
// GtkWindowControl.cpp: prefer the active window, fall back to the first visible
// one that is not a panel.

#pragma once

#import <AppKit/AppKit.h>

#include <react/renderer/core/ReactPrimitives.h>

#include <functional>

namespace basalt {

// The app's own window, or nil when it has none yet. Main thread only.
NSWindow *appWindow();

// How to find a window by its surface id.
//
// The registry is `main_appkit.mm`'s -- it owns every window it opens -- and
// this file is compiled into the mounting library, which the host links rather
// than the other way round. So the host installs the lookup at startup and
// everything here asks through it.
//
// Not installed is the normal case for a test binary and for both mount
// harnesses: none of them opens a second window, and none should grow a
// registry to keep compiling. `appWindowFor` falls back to `appWindow()`
// there, which is what every caller did before windows had ids.
using AppWindowLookup = std::function<NSWindow *(facebook::react::SurfaceId)>;
void setAppWindowLookup(AppWindowLookup lookup);

// The window with this surface id, or the app's own for `kAppWindow` and for
// an id the lookup does not know. Main thread only.
NSWindow *appWindowFor(facebook::react::SurfaceId window);

} // namespace basalt
