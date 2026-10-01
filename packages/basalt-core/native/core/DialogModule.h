// `BasaltDialog`: the native file dialogs, which React Native has no API for.
//
// React Native was built for phones, and a phone has no file dialog -- so there
// is nothing here to be compatible with and the shape is this project's own.
// It follows Electron's semantics, because that is the vocabulary a desktop
// developer already has, with React Native's naming: `openFile`, `saveFile` and
// `openFolder`, each answering a promise with `{canceled, paths}`.
//
// `canceled` rather than an empty list, and `paths` rather than `path` even for
// a save: telling "the person pressed Cancel" from "the person chose nothing"
// is the distinction every one of these APIs gets wrong at least once, and a
// single shape for all three means an app that switches between them does not
// switch between result types.
//
// A plain TurboModule rather than a codegen spec, like `BasaltWindow` beside
// it: codegen specs exist for React Native's own modules, and this is not one.
//
// The dialog itself is `showFileDialog` in PlatformServices.h. Everything here
// is the JavaScript boundary: reading the options, and settling the promise on
// the JavaScript thread when the person is done -- which may be a long time.

#pragma once

#include <ReactCommon/TurboModule.h>

#include <memory>

namespace basalt {

class DesktopDialogModule : public facebook::react::TurboModule {
 public:
  static constexpr const char *kModuleName = "BasaltDialog";

  explicit DesktopDialogModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker);

 private:
  static facebook::jsi::Value openFile(facebook::jsi::Runtime &runtime,
                                       facebook::react::TurboModule &module,
                                       const facebook::jsi::Value *args,
                                       size_t count);
  static facebook::jsi::Value saveFile(facebook::jsi::Runtime &runtime,
                                       facebook::react::TurboModule &module,
                                       const facebook::jsi::Value *args,
                                       size_t count);
  static facebook::jsi::Value openFolder(facebook::jsi::Runtime &runtime,
                                         facebook::react::TurboModule &module,
                                         const facebook::jsi::Value *args,
                                         size_t count);
};

} // namespace basalt
