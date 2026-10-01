// Opening a second window.
//
// Fabric has supported more than one surface all along -- `startSurface` takes
// a surface id, the mounting managers keep a root per id, and the error
// inspector has been a second surface since it was written. What has been
// missing is a second *window* to put one in: every host made exactly one, at
// startup, and everything in it reached for `the` window and `the` root.
//
// So this is the seam for the part that is genuinely per-toolkit -- making a
// window and wiring a surface into it -- and `core/WindowsModule.h` is the
// JavaScript boundary above it. The implementation lives in each host's
// `main_*.cpp` rather than beside the other platform services, because that is
// where a window is made and where the ReactHost that starts a surface lives.
//
// ## A window is a surface
//
// One window, one surface, one React root. That is Fabric's grain rather than a
// simplification: a surface is what has a size, a layout context and a root
// shadow node, and two windows sharing one would be two windows sharing a
// layout. It is also why the id a window is identified by *is* its surface id
// -- there is no second numbering to keep in step.
//
// The cost is that a second window is a second React tree: it does not share
// context, state or a render pass with the first. `<Window>` in JavaScript
// papers over enough of that to be useful and says where it cannot; see
// src/Window.js.

#pragma once

#include <folly/dynamic.h>

#include <functional>
#include <react/renderer/core/ReactPrimitives.h>

#include <string>
#include <vector>

namespace basalt {

struct NewWindowOptions {
  // The component to run, as registered with `AppRegistry.registerComponent`.
  // A window with no component is a window with nothing in it, which is not
  // something to open by accident: an empty one is refused.
  std::string component;
  std::string title;
  double width{900.0};
  double height{700.0};
  // `initialProps`, which is how the second React root is told which window it
  // is. See src/Window.js.
  folly::dynamic props{folly::dynamic::object()};
};

// Opens a window and starts `component` in it. Returns the new surface id,
// which is also the window's, or 0 if it could not.
//
// Called on the UI thread.
facebook::react::SurfaceId openHostWindow(const NewWindowOptions &options);

// Closes a window opened by `openHostWindow` and stops its surface. Ignores the
// main window: an app closing the window it is running in should say so through
// `close()` on the window itself, which is a different thing from destroying a
// surface out from under a React tree.
//
// Called on the UI thread.
void closeHostWindow(facebook::react::SurfaceId surfaceId);

// Ends the application: `[NSApp terminate:]`, `g_application_quit`,
// `PostQuitMessage`. What `quit()` reaches, and what a quit handler calls
// when its answer is "go ahead" -- an app that intercepted the quit refused
// the one the system asked for, so somebody has to ask again.
//
// Called on the UI thread.
void quitHost();

// Every window this host has open, main window first. For the tree dump, and
// for an app asking what it has.
std::vector<facebook::react::SurfaceId> hostWindows();

// A window was closed by the person rather than by the app -- its own close
// button, or the window manager.
//
// This is not a nicety. Without it the host is left holding a record whose
// window is gone, which is a dangling pointer rather than a stale flag, and the
// `<Window>` that opened it goes on believing it is open: the app's state says
// one thing and the screen says another, and the next render tries to close a
// window that has already closed itself.
//
// So the host calls `hostWindowClosed` on the way out, and the module turns it
// into a device event that `<Window>` unmounts on.
void setHostWindowClosedListener(std::function<void(facebook::react::SurfaceId)> listener);

// The host's half: called on the UI thread, before the record is dropped.
void hostWindowClosed(facebook::react::SurfaceId surfaceId);

// ## Refusing to close
//
// The above is a window that *has* closed. This is a window somebody is trying
// to close, which is a different question with a different answer: an app with
// unsaved work wants to ask before it goes, and by the time `hostWindowClosed`
// runs there is nothing left to ask about.
//
// Electron spells this `event.preventDefault()` in a `close` handler, and can,
// because its handler runs in the main process and the answer is available the
// moment the event is raised. Here the answer lives in JavaScript, on another
// thread, and the window manager wants a synchronous yes or no -- so the
// decision has to be made *before* the attempt rather than during it.
//
// Hence a flag rather than a callback. An app that registers a close handler
// marks its window intercepted; the host then refuses every close the window
// manager asks for and reports the attempt instead, and the app closes the
// window itself when it is ready. A window with no handler is not marked and
// closes exactly as it did before -- which matters, because that is every
// window, and a close path that waits on JavaScript to say "go ahead" would be
// a window that does not close when the runtime is busy.
void setHostWindowCloseIntercepted(facebook::react::SurfaceId surfaceId, bool intercepted);

// Whether this window refuses the window manager. Called on the UI thread from
// inside a close handler, which is why it may not hop to ask anybody.
bool hostWindowCloseIntercepted(facebook::react::SurfaceId surfaceId);

// Somebody tried to close a window that is intercepted, and it did not close.
// The module turns this into a device event; the app decides what to do and
// calls `close()` if that is "go ahead".
void setHostWindowCloseRequestListener(std::function<void(facebook::react::SurfaceId)> listener);

// The host's half: called on the UI thread, instead of closing.
void hostWindowCloseRequested(facebook::react::SurfaceId surfaceId);

// ## Refusing to quit
//
// The same question about the application rather than about one window, and
// not answerable by the above: an app can guard every window it owns and
// still lose the person's work to Cmd-Q. macOS routes quitting through
// `applicationShouldTerminate:` and asks no window whether it minds; the
// Windows and GNOME session-end signals do the same.
//
// A flag rather than a callback, for exactly the reason the close case is:
// the system wants a synchronous yes or no and the answer lives in
// JavaScript on another thread. An app that registers a quit handler marks
// the application intercepted; the host then refuses the quit and reports
// the attempt, and the app quits itself when it is ready.
//
// One flag rather than one per window, because quitting is one event no
// matter how many windows are open -- and because the last window closing is
// already a different thing that already works.
void setHostQuitIntercepted(bool intercepted);

// Whether the application refuses to quit. Called on the UI thread from
// inside a terminate handler, which is why it may not hop to ask anybody.
bool hostQuitIntercepted();

// Somebody tried to quit an intercepted application, and it did not quit.
void setHostQuitRequestListener(std::function<void()> listener);

// The host's half: called on the UI thread, instead of quitting.
void hostQuitRequested();

} // namespace basalt
