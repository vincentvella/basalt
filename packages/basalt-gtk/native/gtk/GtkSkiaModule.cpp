#include "GtkSkiaModule.h"

#include "GtkSkiaContext.h"

#include "RNSkManager.h"

#include <glog/logging.h>

#include <memory>
#include <utility>

namespace basalt {
namespace {

// The manager outlives the call that made it and owns everything Skia installed
// into the runtime, so it is held here rather than on the module: a TurboModule
// is recreated whenever the runtime is, and Skia's bindings are torn down and
// rebuilt with it.
//
// Reset rather than leaked on destruction, which matters for a reload: the
// bindings hold a jsi::Runtime reference, and keeping them past the runtime
// they were installed into is how a reload turns into a crash in somebody
// else's frame.
std::unique_ptr<RNSkia::RNSkManager> &skManager() {
  static std::unique_ptr<RNSkia::RNSkManager> manager;
  return manager;
}

} // namespace

GtkSkiaModule::GtkSkiaModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
    : TurboModule(kModuleName, std::move(jsInvoker)) {
  // Blocking and synchronous on the JavaScript thread, because the package's
  // JavaScript calls it and then immediately expects `global.SkiaApi` to be
  // there. Zero arity, boolean return, matching what NativeSkiaModule declares.
  methodMap_["install"] = MethodMetadata{0, install};
}

GtkSkiaModule::~GtkSkiaModule() { skManager().reset(); }

RNSkia::RNSkManager *GtkSkiaModule::manager() { return skManager().get(); }

facebook::jsi::Value GtkSkiaModule::install(facebook::jsi::Runtime &runtime,
                                            facebook::react::TurboModule &module,
                                            const facebook::jsi::Value * /*args*/,
                                            size_t /*count*/) {
  if (skManager() != nullptr) {
    // Already installed. The package calls this from module scope and React
    // Native may evaluate that more than once; a second manager over the same
    // runtime would install a second set of bindings over the first.
    return facebook::jsi::Value(true);
  }

  // Through the derived type: `jsInvoker_` is protected on TurboModule, so a
  // static member of this class may reach it on a GtkSkiaModule and not on a
  // TurboModule. The reference is always ours -- the registry only calls this
  // through the method table filled in above.
  auto invoker = static_cast<GtkSkiaModule &>(module).jsInvoker_;
  if (invoker == nullptr) {
    LOG(ERROR) << "RNSkiaModule: no call invoker, so Skia cannot be installed";
    return facebook::jsi::Value(false);
  }

  try {
    skManager() = std::make_unique<RNSkia::RNSkManager>(
        &runtime, invoker, makeSkiaPlatformContext(invoker));
  } catch (const std::exception &error) {
    // Reported as false rather than thrown, as on the other two hosts: the
    // package's JavaScript checks the return and says something useful about
    // it, whereas an exception crossing the JSI boundary during module
    // initialisation arrives without a stack and without naming Skia.
    LOG(ERROR) << "RNSkiaModule: install failed: " << error.what();
    return facebook::jsi::Value(false);
  }

  LOG(INFO) << "RNSkiaModule: Skia installed";
  return facebook::jsi::Value(true);
}

} // namespace basalt
