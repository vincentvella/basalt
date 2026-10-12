// `RNSkiaModule`, the TurboModule React Native Skia asks for by name.
//
// The package's own Apple module is 90 lines of Objective-C++: one
// `RCT_EXPORT_BLOCKING_SYNCHRONOUS_METHOD(install)` that builds an
// `RNSkia::RNSkManager` from an `RCTBridge`-era view registry and a call
// invoker. `RNSkManager` itself is portable C++ over JSI, so what the port
// amounts to is a runtime, a call invoker and a platform context -- all three
// of which this platform has without any of React Native's iOS half.
//
// `install` is where Skia's whole JavaScript API arrives. Constructing
// RNSkManager installs `SkiaApi` and the rest onto the global object, so
// everything the package's JavaScript reaches for exists from that moment.
// Until it is called, `getEnforcing('RNSkiaModule')` throws and an app that
// imports Skia does not start at all -- not fails to render, does not start.
// The package's `NativeSetup.js` also insists the return be exactly `true`,
// not merely truthy.
//
// ## Why this one is portable and AppKitSkiaModule is not
//
// They are the same module for two different Skias. `AppKitSkiaModule` is the
// 2.x one: Ganesh over Metal, so its platform context is Objective-C++ and it
// lives in the AppKit package. 3.x draws through Graphite over Dawn, and
// Dawn's Metal is behind `wgpu::Surface` -- which leaves nothing
// platform-specific in the module *or* the context, so both live here and
// compile the same way on any host whose Skia has a Dawn.
//
// The two never exist together. `BASALT_HAS_SKIA` is defined only when
// cmake/Skia.cmake built the Ganesh half, and that file refuses a 3.x install
// by version, so the AppKit module is compiled out in exactly the builds this
// one is compiled in. Both answer to `RNSkiaModule` and only one is ever
// registered.

#pragma once

#include <ReactCommon/TurboModule.h>

#include <memory>
#include <string>

namespace RNSkia {
class RNSkManager;
} // namespace RNSkia

namespace basalt {

class BasaltSkiaModule : public facebook::react::TurboModule {
 public:
  static constexpr const char *kModuleName = "RNSkiaModule";

  explicit BasaltSkiaModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker);
  ~BasaltSkiaModule() override;

  // The manager, or null before `install` has run. A `<Canvas>` needs it to
  // register itself by nativeId, and the lifetime is the runtime's rather than
  // any one view's -- which is why it is reachable here and not owned by a
  // view.
  //
  // Only valid on the thread that installed it, which is the JavaScript one.
  static RNSkia::RNSkManager *manager();

 private:
  // A plain function, because `MethodMetadata::invoker` is a function pointer
  // rather than a std::function: a TurboModule's methods are static dispatch.
  static facebook::jsi::Value install(facebook::jsi::Runtime &runtime,
                                      facebook::react::TurboModule &module,
                                      const facebook::jsi::Value *args,
                                      size_t count);
};

// The factory the generated provider calls, one package at a time, with the
// name JavaScript asked for. Null for anything but this module's name.
std::shared_ptr<facebook::react::TurboModule> makeSkiaTurboModule(
    const std::string &name,
    const std::shared_ptr<facebook::react::CallInvoker> &jsInvoker);

} // namespace basalt
