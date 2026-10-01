// BasaltWindow: the window's title bar, reached from JavaScript.
//
// basalt-core's src/TitleBar.js is the API over this -- useTitleBar,
// <TitleBar>, useTitleBarMetrics -- and Win32TitleBar is what it drives. A
// module with no React Native spec, written the way the gesture handler one
// is, because it is this platform's own and there is no spec for it to follow.
//
// Every method is called on the JavaScript thread and posts its request to the
// UI thread, where the window lives. getTitleBarMetrics answers from a copy the
// title bar keeps under a lock, and "basaltTitleBarMetricsChanged" is emitted
// whenever that copy changes.

#pragma once

#include <ReactCommon/TurboModule.h>

#include <memory>

namespace basalt {

class Win32WindowModule : public facebook::react::TurboModule {
 public:
  static constexpr const char *kModuleName = "BasaltWindow";

  explicit Win32WindowModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker);
  ~Win32WindowModule() override;

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
  // The caption's four actions. GtkWindowModule and AppKitWindowModule
  // register the same four under the same names; this host registered none of
  // them, so useWindow().minimize() and .close() were silent no-ops here while
  // working on the other two desktops -- and useCloseRequest could refuse a
  // close and then not be able to honour agreeing to it.
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
