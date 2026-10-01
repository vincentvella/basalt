// `RNGestureHandlerModule`, the TurboModule react-native-gesture-handler asks
// for by name.
//
// Hand-written rather than generated. Every other module in this project
// derives from one of React Native's codegenned specs, because React Native
// ships the specs; a third-party library's spec is generated during *its* build
// against *an app's* node_modules, and this project's hosts are generic
// binaries that are not built per app. So the eight methods are declared here
// against `TurboModule` directly, from the same TypeScript interface codegen
// would have read.
//
// It is registered unconditionally, like every other core module. An app
// without gesture-handler never looks the name up, and the registry behind it
// is empty and costs a branch.
//
// The module is a thin thing: it converts arguments, hops to the UI thread and
// calls core/Gestures.h. What it adds is the one piece the engine cannot have,
// which is a way to reach JavaScript -- RNGH listens on DeviceEventEmitter, and
// only a TurboModule can emit one.

#pragma once

#include <ReactCommon/TurboModule.h>

#include <folly/dynamic.h>

#include <memory>
#include <string>

namespace basalt {

class DesktopGestureHandlerModule : public facebook::react::TurboModule {
 public:
  static constexpr const char *kModuleName = "RNGestureHandlerModule";

  explicit DesktopGestureHandlerModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker);
  ~DesktopGestureHandlerModule() override;

 private:
  // The method table. Each is a plain function because `MethodMetadata::invoker`
  // is a function pointer, not a std::function -- a TurboModule's methods are
  // meant to be static dispatch.
  static facebook::jsi::Value createGestureHandler(facebook::jsi::Runtime &runtime,
                                                   facebook::react::TurboModule &module,
                                                   const facebook::jsi::Value *args,
                                                   size_t count);
  static facebook::jsi::Value attachGestureHandler(facebook::jsi::Runtime &runtime,
                                                   facebook::react::TurboModule &module,
                                                   const facebook::jsi::Value *args,
                                                   size_t count);
  static facebook::jsi::Value updateGestureHandler(facebook::jsi::Runtime &runtime,
                                                   facebook::react::TurboModule &module,
                                                   const facebook::jsi::Value *args,
                                                   size_t count);
  static facebook::jsi::Value dropGestureHandler(facebook::jsi::Runtime &runtime,
                                                 facebook::react::TurboModule &module,
                                                 const facebook::jsi::Value *args,
                                                 size_t count);
  static facebook::jsi::Value install(facebook::jsi::Runtime &runtime,
                                      facebook::react::TurboModule &module,
                                      const facebook::jsi::Value *args,
                                      size_t count);
  static facebook::jsi::Value noop(facebook::jsi::Runtime &runtime,
                                   facebook::react::TurboModule &module,
                                   const facebook::jsi::Value *args,
                                   size_t count);

  void emitGestureEvent(const std::string &eventName, folly::dynamic payload);
};

} // namespace basalt
