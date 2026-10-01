// The window's geometry: how big it is, where it is, and whether it fills the
// screen.
//
// React Native has no API for any of this, for the same reason it has no file
// dialog: on a phone there is one window, it is the screen, and nothing an app
// can say would change it. On a desktop it is the first thing an app wants
// after it has drawn something.
//
// A seam rather than three modules. The title bar next door is per-host because
// what it does differs per host -- GTK installs a header bar to have something
// to style, Windows draws its own caption buttons -- but a window's size is the
// same idea everywhere and only the call differs. So the JavaScript boundary is
// written once, in core/WindowMethods.h, and this is the four lines under it
// each platform answers.
//
// One window, which is the honest limit. Every function here means "the
// window", because the host makes exactly one; multiple windows are their own
// piece of work and are in docs/BACKLOG.md. When they arrive these grow a
// window argument and the module grows a handle, and nothing above changes
// shape.

#pragma once

#include <functional>
#include <vector>

namespace basalt {

struct WindowBounds {
  // In logical pixels, in the desktop's coordinates -- which is what an app can
  // pass straight back to `setWindowPosition`. Not physical pixels: an app
  // reasoning about its own layout is reasoning in the same units React Native
  // lays out in.
  double x{0.0};
  double y{0.0};
  double width{0.0};
  double height{0.0};

  // Distinct states, not one enum: a window can be maximised *and* full screen
  // on every one of these desktops, and an app that restores a remembered
  // layout needs to know which it was.
  bool fullScreen{false};
  bool maximized{false};
};

// Where the window is now.
//
// **Called on the UI thread only.** Every one of these touches a GtkWindow, an
// NSWindow or an HWND, and none of those may be touched from the JavaScript
// thread -- which is where a TurboModule method arrives. `core/WindowMethods.h`
// is what marshals, so a platform implementation may assume it is already in
// the right place. Getting this wrong is quiet rather than loud: the AppKit
// resize simply did not happen, with no warning anywhere.
WindowBounds windowBounds();

void setWindowSize(double width, double height);
void setWindowPosition(double x, double y);

// Centred on the display the window is already on, which is not always the
// primary one.
void centerWindow();

void setWindowFullScreen(bool fullScreen);

// Called by the host whenever the window is resized, moved, or changes state.
//
// The module installs one of these and turns it into a device event, so that
// `useWindow()` can hand an app live bounds rather than a snapshot it has to
// remember to refresh. Replacing a listener drops the previous one; passing
// nothing removes it.
void setWindowBoundsListener(std::function<void(const WindowBounds &)> listener);

// The host's half of that: called from wherever it already learns the window
// moved or resized, on the UI thread. Also updates the cache below, so it is
// worth calling once when the window is first shown even if nothing is
// listening yet.
void notifyWindowBoundsChanged();

// How big the window is allowed to be, in logical pixels.
//
// Zero means "no limit" in either direction, which is the default and is also
// how an app clears one. Not part of `WindowBounds`: bounds are what the window
// *is* and these are what it may become, and an app restoring a remembered
// layout wants the first without being told the second.
struct WindowSizeLimits {
  double minWidth{0.0};
  double minHeight{0.0};
  double maxWidth{0.0};
  double maxHeight{0.0};
};

// Set the limits and apply them. Portable: the storing is identical everywhere
// and only `applyWindowSizeLimits` differs.
void setWindowMinimumSize(double width, double height);
void setWindowMaximumSize(double width, double height);

// What is in force, readable from any thread. Windows answers WM_GETMINMAXINFO
// from this, which is a message rather than a call and so cannot be given them.
WindowSizeLimits windowSizeLimits();

// Clamp a requested size to the limits this platform says it enforces.
//
// Portable, and applied before every `setWindowSize`, because the toolkits do
// not agree on whether a programmatic resize is subject to the same limits a
// dragged corner is. AppKit's `setFrame:` clamps down to the maximum and not up
// to the minimum, which is a resize that lands outside a limit an app set and
// the window manager would have refused -- found by the test for exactly that.
//
// Filtered by `windowCapabilities()` rather than applied unconditionally, so
// that what an app is told and what it gets stay the same thing: a desktop that
// reports `maximumSize: false` does not quietly enforce one here.
void constrainToWindowSizeLimits(double &width, double &height);

// Tell the window manager about the limits above. Called on the UI thread, with
// the new values already stored.
void applyWindowSizeLimits();

// Whether the person may resize the window at all.
void setWindowResizable(bool resizable);

// Whether it floats above other applications' windows. Not "above everything":
// no desktop lets an ordinary application outrank a screen lock or a system
// alert, and none of these ask it to.
void setWindowAlwaysOnTop(bool alwaysOnTop);

// Which of the above this desktop actually does.
//
// Answered rather than documented, because two of these are not gaps and will
// not be filled. GTK4 removed `gtk_window_set_geometry_hints` and
// `gtk_window_set_keep_above`, and Wayland has no protocol for either: a client
// there does not know where it is, does not place itself, and does not outrank
// its neighbours. An app that asks can put its "always on top" switch away on
// the platform that has no such thing, which is better than a switch that lies.
struct WindowCapabilities {
  bool position{false};
  bool minimumSize{false};
  bool maximumSize{false};
  bool resizable{false};
  bool alwaysOnTop{false};
};

WindowCapabilities windowCapabilities();

// The last bounds the host reported, readable from any thread.
//
// `windowBounds()` cannot be: it asks the toolkit, and a TurboModule method
// runs on the JavaScript thread. So `getBounds()` answers from here, which is
// exact rather than approximate -- the host reports every change -- and needs
// no hop, which matters because it is read during render.
//
// All zeroes before the host has reported anything, which is what an app
// reading bounds during its first render sees.
WindowBounds lastKnownWindowBounds();

// --- The displays the desktop has -------------------------------------------
//
// React Native's `Dimensions` reports the window, which is the right answer to
// a different question: an app placing a window, or deciding how much it can
// show, is asking about the screen it is on and the ones beside it.
//
// The lookups already existed -- `NSScreen`, `GdkMonitor` and
// `MonitorFromWindow` are how `center()` and full screen work -- and were
// simply not passed on.

struct DisplayInfo {
  // In the same logical pixels and the same desktop coordinates WindowBounds
  // uses, so a bound and a display can be compared without converting either.
  // Top-left origin on every host, which costs AppKit a flip: its own
  // coordinates grow upwards from the primary screen.
  double x{0.0};
  double y{0.0};
  double width{0.0};
  double height{0.0};

  // What is left after the desktop's own furniture -- a menu bar, a taskbar, a
  // dock, a panel. Where a window should go; `bounds` is where the glass is.
  // Equal to bounds on a desktop that reserves nothing.
  double workX{0.0};
  double workY{0.0};
  double workWidth{0.0};
  double workHeight{0.0};

  // Physical pixels per logical pixel: 2 on a Retina display, 1 on an
  // ordinary one, and fractional under Wayland or a Windows display set to
  // 150%. Never zero -- a host that cannot answer reports 1, because a caller
  // dividing by this should not have to check.
  double scaleFactor{1.0};

  // Exactly one display is primary, and it is the one the desktop's
  // coordinates are anchored to.
  bool primary{false};
};

// Every display, primary first.
//
// **Called on the UI thread only**, like everything above it: each host asks
// its toolkit.
std::vector<DisplayInfo> displays();

// The last list the host reported, readable from any thread.
//
// The same arrangement `lastKnownWindowBounds` has, and for the same reason:
// `displays()` asks the toolkit and a TurboModule method runs on the
// JavaScript thread. Exact rather than approximate, because the host reports
// every change -- and empty before the host has reported anything, which is
// what an app asking during its first render sees.
std::vector<DisplayInfo> lastKnownDisplays();

// Where the pointer is, in the same coordinates the bounds above use.
//
// A display question rather than a window one: it may be over a different
// display than the app's window, or over no window at all. All zeroes on a
// host that cannot answer -- Wayland does not tell a client where the pointer
// is unless it is over one of its own surfaces.
struct PointerPosition {
  double x{0.0};
  double y{0.0};
  // False where the desktop will not say, so that an app can tell "at the
  // origin" from "not answered".
  bool known{false};
};

PointerPosition pointerPosition();

// A monitor plugged in, unplugged, or rearranged.
//
// No argument: an app that cares re-reads the list, which is the only way to
// be right about several changes arriving together. Same shape as the window
// bounds listener above, and set by the module for the same reason.
void setDisplaysListener(std::function<void()> listener);

// The host's half: called on the UI thread when the arrangement changes.
void notifyDisplaysChanged();

} // namespace basalt
