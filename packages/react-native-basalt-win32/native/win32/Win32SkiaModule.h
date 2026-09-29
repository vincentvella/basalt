// `RNSkiaModule`, the TurboModule @shopify/react-native-skia asks for by name.
//
// The AppKit half of this is appkit/AppKitSkiaModule.h and this is the same
// shape, for the same reason: the package's own module is 61 lines of
// Objective-C that builds a manager from an `RCTBridge`, and what it actually
// needs -- a runtime, a call invoker and a platform context -- basalt already
// has. So the module is ours and the manager is the package's.
//
// `install` is where Skia's whole JavaScript API arrives. Constructing
// RNSkManager installs `SkiaApi` onto the global object, so everything the
// package's JavaScript reaches for exists from that moment. Until it is called,
// `getEnforcing('RNSkiaModule')` throws and an app that imports Skia does not
// start at all -- which is exactly where kino stopped on this platform.
//
// In the win32 package rather than in core, for the reason Skia.cmake gives
// about the Apple one: a platform context is a host's business.

#pragma once

#include <ReactCommon/TurboModule.h>

#include "RNSkManager.h"

#include <memory>

namespace basalt {

class Win32SkiaModule : public facebook::react::TurboModule {
 public:
  static constexpr const char *kModuleName = "RNSkiaModule";

  explicit Win32SkiaModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker);
  ~Win32SkiaModule() override;

  // The manager, or null before `install` has run. A `<Canvas>` registers
  // itself with it by nativeId -- see win32/Win32SkiaPeer.h -- and the lifetime
  // is the runtime's rather than any one view's, which is why it is reachable
  // here and not owned by a view.
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
