// `NativeModules.BasaltSubprocess`, the JavaScript boundary for Subprocess.h.
//
// A TurboModule rather than an Expo module, unlike this project's notifications
// package: that one exists to answer `requireNativeModule('ExpoNotifications')`
// for an app already using expo-notifications, and there is no Expo package for
// running a process to match. So the shape is this project's, and a TurboModule
// is what a React Native app can reach without Expo installed at all.
//
// Reached from a host because core/PackageModules.h's generated
// `packageTurboModule` offers it by name; the factory at the bottom of the .cpp
// is what native/CMakeLists.txt registers.

#pragma once

#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/TurboModule.h>

#include <memory>
#include <string>

namespace basalt {

// Held by shared_ptr -- which is how a TurboModule is always held -- so that the
// listeners can take a weak reference to it rather than a bare `this`. See
// `attach` and the destructor.
class SubprocessModule : public facebook::react::TurboModule,
                         public std::enable_shared_from_this<SubprocessModule> {
 public:
  static constexpr auto kModuleName = "BasaltSubprocess";

  // The events, on DeviceEventEmitter. Named here so that the TypeScript and
  // this file cannot drift apart without one of them failing to compile.
  static constexpr auto kOutputEvent = "basaltSubprocessOutput";
  static constexpr auto kExitEvent = "basaltSubprocessExit";

  explicit SubprocessModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker);
  ~SubprocessModule() override;

  // Subscribes this module to the subprocess seam. Separate from the
  // constructor because it needs `weak_from_this`, which is not usable until a
  // shared_ptr owns the object; `makeSubprocessTurboModule` does both.
  void attach();

 private:
  static facebook::jsi::Value spawn(
      facebook::jsi::Runtime &runtime,
      facebook::react::TurboModule &module,
      const facebook::jsi::Value *args,
      size_t count);
  static facebook::jsi::Value kill(
      facebook::jsi::Runtime &runtime,
      facebook::react::TurboModule &module,
      const facebook::jsi::Value *args,
      size_t count);
  static facebook::jsi::Value isRunning(
      facebook::jsi::Runtime &runtime,
      facebook::react::TurboModule &module,
      const facebook::jsi::Value *args,
      size_t count);
};

// What core/PackageModules.h's generated chain calls. Answers nullptr for every
// name but this module's.
std::shared_ptr<facebook::react::TurboModule> makeSubprocessTurboModule(
    const std::string &name,
    const std::shared_ptr<facebook::react::CallInvoker> &jsInvoker);

} // namespace basalt
