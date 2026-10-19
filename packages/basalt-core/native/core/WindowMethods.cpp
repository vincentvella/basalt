#include "WindowMethods.h"

#include "PlatformServices.h"
#include "WindowControl.h"

#include <mutex>

namespace basalt {

namespace {

using facebook::jsi::Object;
using facebook::jsi::Runtime;
using facebook::jsi::Value;
using facebook::react::TurboModule;

bool twoNumbers(const Value *args, size_t count) {
  return count >= 2 && args[0].isNumber() && args[1].isNumber();
}

// Which window the call is about, from the argument after the ones it needs.
//
// Trailing and optional, which is what lets a host that has not been taught
// this ignore it and a bundle that does not send it keep working; see the
// change's design.md. A missing, undefined or non-numeric id is `kAppWindow`,
// so `windowControl` -- callable from outside any component, and so from
// inside no window -- lands on the window the app started in.
facebook::react::SurfaceId windowAt(const Value *args, size_t count, size_t index) {
  if (count <= index || !args[index].isNumber()) {
    return kAppWindow;
  }
  return static_cast<facebook::react::SurfaceId>(args[index].asNumber());
}

// Everything that changes a window happens on the UI thread.
//
// A TurboModule method runs on the JavaScript thread, and none of the three
// toolkits may be touched from it. This was found the loud way and reported the
// quiet way: `setSize` from JavaScript did nothing at all on macOS, with no
// warning and no crash.
void onUiThread(std::function<void()> work) {
  postToUiThread(std::move(work));
}

} // namespace

Value windowSetSize(Runtime & /*runtime*/,
                    TurboModule & /*module*/,
                    const Value *args,
                    size_t count) {
  if (twoNumbers(args, count)) {
    const double first = args[0].asNumber();
    const double second = args[1].asNumber();
    const facebook::react::SurfaceId window = windowAt(args, count, 2);
    onUiThread([first, second, window] {
      // Clamped here rather than left to the toolkit, which is the only way the
      // three agree. See core/WindowControl.h.
      //
      // The limits are the app's window's, which is what the change that made
      // geometry per-window deliberately did not change: they are one set of
      // numbers and Windows answers WM_GETMINMAXINFO out of them.
      double width = first;
      double height = second;
      constrainToWindowSizeLimits(width, height);
      setWindowSize(window, width, height);
    });
  }
  return Value::undefined();
}

Value windowSetPosition(Runtime & /*runtime*/,
                        TurboModule & /*module*/,
                        const Value *args,
                        size_t count) {
  if (twoNumbers(args, count)) {
    const double first = args[0].asNumber();
    const double second = args[1].asNumber();
    const facebook::react::SurfaceId window = windowAt(args, count, 2);
    onUiThread([first, second, window] { setWindowPosition(window, first, second); });
  }
  return Value::undefined();
}

Value windowCenter(Runtime & /*runtime*/,
                   TurboModule & /*module*/,
                   const Value *args,
                   size_t count) {
  const facebook::react::SurfaceId window = windowAt(args, count, 0);
  onUiThread([window] { centerWindow(window); });
  return Value::undefined();
}

Value windowSetFullScreen(Runtime & /*runtime*/,
                          TurboModule & /*module*/,
                          const Value *args,
                          size_t count) {
  // Anything but a boolean is ignored rather than coerced: `setFullScreen()`
  // with no argument reads as "make it full screen" to a JavaScript truthiness
  // check and as a mistake to everyone else.
  if (count >= 1 && args[0].isBool()) {
    const bool fullScreen = args[0].asBool();
    const facebook::react::SurfaceId window = windowAt(args, count, 1);
    onUiThread([fullScreen, window] { setWindowFullScreen(window, fullScreen); });
  }
  return Value::undefined();
}

Value windowSetMinimumSize(Runtime & /*runtime*/,
                           TurboModule & /*module*/,
                           const Value *args,
                           size_t count) {
  if (twoNumbers(args, count)) {
    const double width = args[0].asNumber();
    const double height = args[1].asNumber();
    // The store is portable and thread-safe; what needs the UI thread is the
    // applying, which is why the hop wraps the whole call rather than half of
    // it. See core/WindowControl.h.
    onUiThread([width, height] { setWindowMinimumSize(width, height); });
  }
  return Value::undefined();
}

Value windowSetMaximumSize(Runtime & /*runtime*/,
                           TurboModule & /*module*/,
                           const Value *args,
                           size_t count) {
  if (twoNumbers(args, count)) {
    const double width = args[0].asNumber();
    const double height = args[1].asNumber();
    onUiThread([width, height] { setWindowMaximumSize(width, height); });
  }
  return Value::undefined();
}

Value windowSetResizable(Runtime & /*runtime*/,
                         TurboModule & /*module*/,
                         const Value *args,
                         size_t count) {
  if (count >= 1 && args[0].isBool()) {
    const bool resizable = args[0].asBool();
    onUiThread([resizable] { setWindowResizable(resizable); });
  }
  return Value::undefined();
}

Value windowSetAlwaysOnTop(Runtime & /*runtime*/,
                           TurboModule & /*module*/,
                           const Value *args,
                           size_t count) {
  if (count >= 1 && args[0].isBool()) {
    const bool onTop = args[0].asBool();
    onUiThread([onTop] { setWindowAlwaysOnTop(onTop); });
  }
  return Value::undefined();
}

Value windowGetCapabilities(Runtime &runtime,
                            TurboModule & /*module*/,
                            const Value * /*args*/,
                            size_t /*count*/) {
  const WindowCapabilities capabilities = windowCapabilities();
  Object result(runtime);
  result.setProperty(runtime, "position", capabilities.position);
  result.setProperty(runtime, "minimumSize", capabilities.minimumSize);
  result.setProperty(runtime, "maximumSize", capabilities.maximumSize);
  result.setProperty(runtime, "resizable", capabilities.resizable);
  result.setProperty(runtime, "alwaysOnTop", capabilities.alwaysOnTop);
  return Value(runtime, result);
}

Value windowBoundsPayload(Runtime &runtime,
                          facebook::react::SurfaceId window,
                          const WindowBounds &bounds) {
  Object object(runtime);
  object.setProperty(runtime, "x", bounds.x);
  object.setProperty(runtime, "y", bounds.y);
  object.setProperty(runtime, "width", bounds.width);
  object.setProperty(runtime, "height", bounds.height);
  object.setProperty(runtime, "fullScreen", bounds.fullScreen);
  object.setProperty(runtime, "maximized", bounds.maximized);
  // Which window moved, so a hook in another one can ignore it.
  object.setProperty(runtime, "window", static_cast<double>(window));
  return Value(runtime, object);
}

Value windowGetBounds(Runtime &runtime,
                      TurboModule & /*module*/,
                      const Value *args,
                      size_t count) {
  // From the cache rather than from the toolkit: this runs on the JavaScript
  // thread, and it is read during render, so it can neither ask AppKit nor wait
  // for a hop. The host reports every change, so the cache is exact.
  const WindowBounds bounds = lastKnownWindowBounds(windowAt(args, count, 0));
  Object object(runtime);
  object.setProperty(runtime, "x", bounds.x);
  object.setProperty(runtime, "y", bounds.y);
  object.setProperty(runtime, "width", bounds.width);
  object.setProperty(runtime, "height", bounds.height);
  object.setProperty(runtime, "fullScreen", bounds.fullScreen);
  object.setProperty(runtime, "maximized", bounds.maximized);
  return Value(runtime, object);
}

} // namespace basalt
