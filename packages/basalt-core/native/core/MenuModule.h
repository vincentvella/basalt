// `BasaltMenu`: the application menu, from JavaScript.
//
// The application menu: `<Menu>` describes one, this installs it, and an item
// the app owns comes back as a device event carrying its id -- which is the
// same arrangement the title bar and the window use, for the same reason: a
// menu item is chosen at a time nothing is waiting for it.
//
// And context menus, which are the other kind and answer differently. See
// `showContextMenu` below.
//
// Roles never come back. They are handled where they are: see
// core/MenuModel.h, and appkit/AppKitMenuBar.mm for the part that makes Cmd-C
// reach a text field.

#pragma once

#include <ReactCommon/TurboModule.h>

#include <memory>
#include <mutex>

namespace basalt {

class DesktopMenuModule : public facebook::react::TurboModule {
 public:
  static constexpr const char *kModuleName = "BasaltMenu";

  explicit DesktopMenuModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker);
  ~DesktopMenuModule() override;

 private:
  // `setApplicationMenu(items)`, where items are the shape core/MenuModel.h
  // describes. An empty list restores the platform's default.
  static facebook::jsi::Value setMenu(facebook::jsi::Runtime &runtime,
                                      facebook::react::TurboModule &module,
                                      const facebook::jsi::Value *args,
                                      size_t count);
  // `isSupported()`: whether this platform puts a menu bar anywhere a person
  // can see it. False on GNOME, and not a promise to change; see
  // core/MenuModel.h.
  static facebook::jsi::Value isSupported(facebook::jsi::Runtime &runtime,
                                          facebook::react::TurboModule &module,
                                          const facebook::jsi::Value *args,
                                          size_t count);
  // `showContextMenu(items, x, y)` -> a promise of the index chosen, or null
  // when it was dismissed.
  //
  // A promise rather than the device event the application menu uses, and the
  // difference is worth saying. A menu bar is installed once and its items are
  // chosen at times nothing is waiting for; a context menu is opened by an app
  // that is, right then, asking a question -- so the answer belongs to the call
  // that asked it rather than to a listener somewhere else.
  static facebook::jsi::Value showContextMenu(facebook::jsi::Runtime &runtime,
                                              facebook::react::TurboModule &module,
                                              const facebook::jsi::Value *args,
                                              size_t count);
  static facebook::jsi::Value noop(facebook::jsi::Runtime &runtime,
                                   facebook::react::TurboModule &module,
                                   const facebook::jsi::Value *args,
                                   size_t count);

 public:
  // What an installed menu's handler holds instead of the module. Hermes
  // 260318099 (React Native 0.88) frees host objects on a finalizer thread of
  // its own, so this module can be destroyed on any thread while its handler is
  // called on the UI thread. The pointer is cleared under the lock; the menu
  // itself is only ever touched on the UI thread.
  struct Owner {
    std::mutex mutex;
    DesktopMenuModule *module = nullptr;
  };

 private:
  std::shared_ptr<Owner> owner_;
};

// The device event an item's id arrives on. Named here so the two sides cannot
// drift.
inline constexpr const char *kMenuChosenEvent = "basaltMenuItemChosen";

} // namespace basalt
