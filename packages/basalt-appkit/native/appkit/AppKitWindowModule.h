// `BasaltWindow` on macOS: the module src/TitleBar.js talks to.
//
// The same four methods Win32WindowModule has, because the JavaScript is
// shared and does not know which desktop it is on. What differs is underneath,
// in AppKitTitleBar.h.

#pragma once

#include <ReactCommon/TurboModule.h>

#include <memory>

namespace basalt {

class AppKitWindowModule : public facebook::react::TurboModule {
 public:
  static constexpr const char *kModuleName = "BasaltWindow";

  explicit AppKitWindowModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker);
  ~AppKitWindowModule() override;

 private:
  static facebook::jsi::Value setTitle(facebook::jsi::Runtime &runtime,
                                       facebook::react::TurboModule &module,
                                       const facebook::jsi::Value *args,
                                       size_t count);
  static facebook::jsi::Value setTitleBarColors(facebook::jsi::Runtime &runtime,
                                                facebook::react::TurboModule &module,
                                                const facebook::jsi::Value *args,
                                                size_t count);
  static facebook::jsi::Value setTitleBarStyle(facebook::jsi::Runtime &runtime,
                                               facebook::react::TurboModule &module,
                                               const facebook::jsi::Value *args,
                                               size_t count);
  static facebook::jsi::Value getTitleBarMetrics(facebook::jsi::Runtime &runtime,
                                                 facebook::react::TurboModule &module,
                                                 const facebook::jsi::Value *args,
                                                 size_t count);
  // The caption buttons' functions, for an app drawing its own header.
  static facebook::jsi::Value minimize(facebook::jsi::Runtime &runtime,
                                       facebook::react::TurboModule &module,
                                       const facebook::jsi::Value *args,
                                       size_t count);
  static facebook::jsi::Value toggleMaximize(facebook::jsi::Runtime &runtime,
                                             facebook::react::TurboModule &module,
                                             const facebook::jsi::Value *args,
                                             size_t count);
  static facebook::jsi::Value closeWindow(facebook::jsi::Runtime &runtime,
                                          facebook::react::TurboModule &module,
                                          const facebook::jsi::Value *args,
                                          size_t count);
  static facebook::jsi::Value startWindowDrag(facebook::jsi::Runtime &runtime,
                                              facebook::react::TurboModule &module,
                                              const facebook::jsi::Value *args,
                                              size_t count);
  static facebook::jsi::Value noop(facebook::jsi::Runtime &runtime,
                                   facebook::react::TurboModule &module,
                                   const facebook::jsi::Value *args,
                                   size_t count);
};

} // namespace basalt
