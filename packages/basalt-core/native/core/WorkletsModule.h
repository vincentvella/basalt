// react-native-worklets, which is what Reanimated 4 is built on.
//
// The package ships its portable half as C++ in `Common/cpp` -- a second
// JavaScript runtime for the UI thread, the serialisation that gets a function
// into it, the schedulers between the two -- depending on jsi, Hermes, a call
// invoker and nothing else. So it compiles here unmodified, at the app's own
// version, the same arrangement expo-modules-core has. See cmake/Worklets.cmake.
//
// What the package does not ship for a desktop is the host half, and this is
// it: the TurboModule its JavaScript looks up, a UI scheduler, a frame source,
// and the platform logger its C++ declares and leaves to each platform.
//
// The whole of the port is those four things, which is worth saying plainly:
// the hard part of worklets -- a separate Hermes runtime, capturing a closure
// across it, keeping shared values coherent between two runtimes -- is written
// once, portably, by the people who wrote it. A platform contributes a thread
// to run on and a heartbeat.

#pragma once

#include <ReactCommon/TurboModule.h>

#include <memory>

namespace basalt {

// Whether this build has react-native-worklets compiled into it, for the log
// line that tells a developer which of the two situations they are in.
bool hasWorklets();

#ifdef BASALT_HAS_WORKLETS

class DesktopWorkletsModule : public facebook::react::TurboModule {
 public:
  static constexpr const char *kModuleName = "WorkletsModule";

  explicit DesktopWorkletsModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker);
  ~DesktopWorkletsModule() override;

 private:
  static facebook::jsi::Value installTurboModule(facebook::jsi::Runtime &runtime,
                                                 facebook::react::TurboModule &module,
                                                 const facebook::jsi::Value *args,
                                                 size_t count);
  static facebook::jsi::Value start(facebook::jsi::Runtime &runtime,
                                    facebook::react::TurboModule &module,
                                    const facebook::jsi::Value *args,
                                    size_t count);
  static facebook::jsi::Value toggleSlowAnimations(facebook::jsi::Runtime &runtime,
                                                   facebook::react::TurboModule &module,
                                                   const facebook::jsi::Value *args,
                                                   size_t count);
};

#endif

} // namespace basalt
