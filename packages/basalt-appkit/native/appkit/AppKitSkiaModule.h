// `RNSkiaModule`, the TurboModule @shopify/react-native-skia asks for by name.
//
// The package's own is 61 lines: one blocking synchronous `install` that builds
// a `SkiaManager` from an `RCTBridge` and a call invoker, and the manager wraps
// `RNSkia::RNSkManager`, which is portable C++ over JSI. basalt has a runtime and
// a call invoker, so all that is left is the platform context -- see
// AppKitSkiaContext.h -- and this.
//
// `install` is where Skia's whole JavaScript API arrives. Constructing
// RNSkManager installs `SkiaApi` and the rest onto the global object, so
// everything the package's JavaScript reaches for exists from that moment. Until
// it is called, `getEnforcing('RNSkiaModule')` throws and an app that imports
// Skia does not start at all.
//
// In the AppKit package rather than in core, for the reason Skia.cmake gives:
// the Metal and AppKit halves are a host's business, and `basalt_core_probe`
// said so by refusing to link NSScreen.

#pragma once

#include <ReactCommon/TurboModule.h>

#include "RNSkManager.h"

#include <memory>

namespace basalt {

class AppKitSkiaModule : public facebook::react::TurboModule {
public:
  static constexpr const char *kModuleName = "RNSkiaModule";

  explicit AppKitSkiaModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker);
  ~AppKitSkiaModule() override;

  // The manager, or null before `install` has run. A `<Canvas>` needs it to
  // register itself by nativeId, and the lifetime is the runtime's rather than
  // any one view's -- which is why it is reachable here and not owned by a view.
  //
  // Only valid on the thread that installed it. Every caller so far is the UI
  // thread, which is also where mounting happens.
  static RNSkia::RNSkManager *manager();

private:
  // A plain function, because `MethodMetadata::invoker` is a function pointer
  // rather than a std::function -- a TurboModule's methods are static dispatch.
  static facebook::jsi::Value install(facebook::jsi::Runtime &runtime,
                                      facebook::react::TurboModule &module,
                                      const facebook::jsi::Value *args,
                                      size_t count);
};

} // namespace basalt
