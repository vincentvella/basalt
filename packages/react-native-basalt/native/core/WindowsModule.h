// `BasaltWindows`: opening and closing windows, from JavaScript.
//
// Three methods over core/WindowHost.h. What is worth saying is what a window
// *is* here, because it decides the shape of everything above: one window is
// one surface is one React root. See that header.
//
// Separate from `BasaltWindow`, which is the singular one next door: that
// module is about the window an app is already in -- its size, its title, its
// caption buttons -- and this one is about there being more than one of them.
// Merging them would mean one module whose methods mean different things
// depending on which half you are in.

#pragma once

#include <ReactCommon/TurboModule.h>

#include <memory>

namespace basalt {

class DesktopWindowsModule : public facebook::react::TurboModule {
 public:
  static constexpr const char *kModuleName = "BasaltWindows";

  explicit DesktopWindowsModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker);
  ~DesktopWindowsModule() override;

 private:
  // `open({component, title, width, height, props})` -> a promise of the new
  // window's id, or of null when the platform could not open one.
  //
  // A promise rather than a number, because the window is made on the UI
  // thread: this is called from the JavaScript one, and a synchronous answer
  // would mean blocking it on a window manager.
  static facebook::jsi::Value open(facebook::jsi::Runtime &runtime,
                                   facebook::react::TurboModule &module,
                                   const facebook::jsi::Value *args,
                                   size_t count);
  static facebook::jsi::Value close(facebook::jsi::Runtime &runtime,
                                    facebook::react::TurboModule &module,
                                    const facebook::jsi::Value *args,
                                    size_t count);
  // Every window that is open, main one first. For an app asking what it has,
  // and for a test asserting that a window really closed.
  static facebook::jsi::Value getWindows(facebook::jsi::Runtime &runtime,
                                         facebook::react::TurboModule &module,
                                         const facebook::jsi::Value *args,
                                         size_t count);
  // `interceptClose(surfaceId, true)` -- this window refuses the window manager
  // and reports the attempt instead. See core/WindowHost.h for why it is a flag
  // set in advance rather than an answer given when asked.
  static facebook::jsi::Value interceptClose(facebook::jsi::Runtime &runtime,
                                             facebook::react::TurboModule &module,
                                             const facebook::jsi::Value *args,
                                             size_t count);
  static facebook::jsi::Value setHandledKeys(facebook::jsi::Runtime &runtime,
                                             facebook::react::TurboModule &module,
                                             const facebook::jsi::Value *args,
                                             size_t count);
  static facebook::jsi::Value clearHandledKeys(facebook::jsi::Runtime &runtime,
                                               facebook::react::TurboModule &module,
                                               const facebook::jsi::Value *args,
                                               size_t count);

  // `getDisplays()` -- every display, primary first. Synchronous, off the
  // cache the host keeps up to date; see core/WindowControl.h.
  static facebook::jsi::Value getDisplays(facebook::jsi::Runtime &runtime,
                                          facebook::react::TurboModule &module,
                                          const facebook::jsi::Value *args,
                                          size_t count);

  // `getPointerPosition()` -- a promise, unlike the above: there is nothing
  // to cache, because the answer changes whenever the pointer moves and
  // nothing reports that.
  static facebook::jsi::Value getPointerPosition(facebook::jsi::Runtime &runtime,
                                                 facebook::react::TurboModule &module,
                                                 const facebook::jsi::Value *args,
                                                 size_t count);

  // `quit()` -- end the application. What a quit handler calls when it is
  // ready, since intercepting refused the quit the system asked for.
  static facebook::jsi::Value quit(facebook::jsi::Runtime &runtime,
                                   facebook::react::TurboModule &module,
                                   const facebook::jsi::Value *args,
                                   size_t count);

  // `interceptQuit(true)` -- the same for the application. One argument
  // rather than two, because quitting is not about a particular window.
  static facebook::jsi::Value interceptQuit(facebook::jsi::Runtime &runtime,
                                            facebook::react::TurboModule &module,
                                            const facebook::jsi::Value *args,
                                            size_t count);
  static facebook::jsi::Value noop(facebook::jsi::Runtime &runtime,
                                   facebook::react::TurboModule &module,
                                   const facebook::jsi::Value *args,
                                   size_t count);
};

// The device event a window's closing arrives on. Named here so the two sides
// cannot drift.
inline constexpr const char *kWindowClosedEvent = "basaltWindowClosed";

// And the one a window's *attempted* closing arrives on: somebody tried, the
// window is still open, and the app decides. Only ever sent for a window that
// asked to intercept.
inline constexpr const char *kWindowCloseRequestedEvent = "basaltWindowCloseRequested";

// The application, not a window: somebody tried to quit and the app asked to
// be asked first. Carries no argument, because there is nothing to
// distinguish -- see core/WindowHost.h.
inline constexpr const char *kQuitRequestedEvent = "basaltQuitRequested";

// A monitor plugged in, unplugged, or rearranged. No argument: an app that
// cares re-reads the list, which is the only way to be right when several
// changes arrive at once.
inline constexpr const char *kDisplaysChangedEvent = "basaltDisplaysChanged";

// Something dragged over, off, or onto a view that marked itself a drop
// target. Carries the tag, the phase, the position and -- on a drop only --
// what was dropped. See core/DragAndDrop.h.
inline constexpr const char *kDropEvent = "basaltDrop";

// A key combination a view declared through `<KeyHandler>`. Carries the tag and
// the combination, and arrives only for combinations some view claimed -- the
// host has already decided that, synchronously, because it had to consume the key
// or pass it on before this could be sent. See core/KeyEvents.h.
inline constexpr const char *kKeyEvent = "basaltKey";

} // namespace basalt
