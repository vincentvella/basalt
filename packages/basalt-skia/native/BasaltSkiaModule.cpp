#include "BasaltSkiaModule.h"

#include "BasaltSkPlatformContext.h"

#include "RNSkManager.h"

#include <glog/logging.h>

#include <exception>
#include <memory>
#include <utility>

namespace basalt {
namespace {

// The manager outlives the call that made it and owns everything Skia
// installed into the runtime, so it is held here rather than on the module: a
// TurboModule is recreated whenever the runtime is, and Skia's bindings are
// torn down and rebuilt with it.
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

BasaltSkiaModule::BasaltSkiaModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
    : TurboModule(kModuleName, std::move(jsInvoker)) {
  // Blocking and synchronous on the JavaScript thread, because the package's
  // JavaScript calls it and then immediately expects `global.SkiaApi` to be
  // there. Arity zero and a boolean return, matching what NativeSkiaModule's
  // spec declares.
  methodMap_["install"] = MethodMetadata{0, install};
}

BasaltSkiaModule::~BasaltSkiaModule() { skManager().reset(); }

RNSkia::RNSkManager *BasaltSkiaModule::manager() { return skManager().get(); }

facebook::jsi::Value BasaltSkiaModule::install(facebook::jsi::Runtime &runtime,
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
  // static member of this class may reach it on a BasaltSkiaModule and not on
  // a TurboModule. The reference is always ours -- the registry only calls
  // this through the method table filled in above.
  auto invoker = static_cast<BasaltSkiaModule &>(module).jsInvoker_;
  if (invoker == nullptr) {
    LOG(ERROR) << "RNSkiaModule: no call invoker, so Skia cannot be installed";
    return facebook::jsi::Value(false);
  }

  try {
    skManager() = std::make_unique<RNSkia::RNSkManager>(
        &runtime, invoker, std::make_shared<BasaltSkPlatformContext>(invoker));
  } catch (const std::exception &error) {
    // Reported as false rather than thrown. The package's JavaScript checks
    // the return and says something useful about it; an exception here crosses
    // the JSI boundary during module initialisation, where the message tends
    // to arrive without a stack and without naming Skia.
    LOG(ERROR) << "RNSkiaModule: install failed: " << error.what();
    return facebook::jsi::Value(false);
  }

  LOG(INFO) << "RNSkiaModule: Skia installed, over Graphite";
  return facebook::jsi::Value(true);
}

std::shared_ptr<facebook::react::TurboModule> makeSkiaTurboModule(
    const std::string &name,
    const std::shared_ptr<facebook::react::CallInvoker> &jsInvoker) {
  if (name != BasaltSkiaModule::kModuleName) {
    return nullptr;
  }
  return std::make_shared<BasaltSkiaModule>(jsInvoker);
}

} // namespace basalt
