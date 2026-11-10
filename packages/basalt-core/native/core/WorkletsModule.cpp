#include "WorkletsModule.h"

#include "Gestures.h"
#include "PlatformServices.h"

#include <glog/logging.h>

#ifdef BASALT_HAS_WORKLETS
#include <worklets/NativeModules/WorkletsModuleProxy.h>
#include <worklets/Tools/JSScheduler.h>
#include <worklets/Tools/PlatformLogger.h>
#include <worklets/Tools/RNRuntimeStatus.h>
#include <worklets/Tools/UIScheduler.h>
#include <worklets/WorkletRuntime/RuntimeBindings.h>

#include <thread>
#endif

namespace basalt {

bool hasWorklets() {
#ifdef BASALT_HAS_WORKLETS
  return true;
#else
  return false;
#endif
}

#ifdef BASALT_HAS_WORKLETS

namespace {

using facebook::jsi::Runtime;
using facebook::jsi::Value;
using facebook::react::TurboModule;

// The thread the bundle runs on, learned at install time -- which happens from
// JavaScript, so whichever thread that is, is the one. Worklets asks about it
// to decide whether a job can run now or has to be scheduled.
std::thread::id &javaScriptThread() {
  static std::thread::id id;
  return id;
}

// The UI thread's scheduler. `postToUiThread` is the platform's, and the base
// class does the queueing; this only has to get `triggerUI` called over there.
class DesktopUIScheduler final : public worklets::UIScheduler,
                                 public std::enable_shared_from_this<DesktopUIScheduler> {
 public:
  // The one thing the base class cannot answer for itself, and the reason it
  // is pure virtual: worklets 0.13 moved `isOnUIThread()` to a non-virtual
  // wrapper over this. Before that, a platform inherited an implementation and
  // a platform that never overrode it compiled anyway -- which is how this
  // class came to be `final` and abstract at the same time.
  bool queryIsOnUIThread() const override {
    return isUiThread();
  }

  void scheduleOnUI(std::function<void()> job) override {
    // Already on the UI thread: run it now rather than a frame later, which is
    // what makes a gesture handler's worklet feel immediate.
    if (std::this_thread::get_id() != javaScriptThread()) {
      job();
      return;
    }

    UIScheduler::scheduleOnUI(std::move(job));
    if (!scheduledOnUI_) {
      postToUiThread([weakThis = weak_from_this()]() {
        // The scheduler can be gone by the time this runs -- the runtime it
        // belongs to is torn down on reload.
        if (auto self = weakThis.lock()) {
          self->triggerUI();
        }
      });
    }
  }
};

// Held for reanimated, which needs the same proxy rather than a second one, and
// so that `start()` can reach what `installTurboModule()` made. Weak, because
// the module owns it and the module goes away with the runtime.
std::weak_ptr<worklets::WorkletsModuleProxy> &installedProxy() {
  static std::weak_ptr<worklets::WorkletsModuleProxy> proxy;
  return proxy;
}

std::shared_ptr<worklets::WorkletsModuleProxy> &heldProxy() {
  static std::shared_ptr<worklets::WorkletsModuleProxy> proxy;
  return proxy;
}

std::shared_ptr<worklets::RNRuntimeStatus> &runtimeStatus() {
  static std::shared_ptr<worklets::RNRuntimeStatus> status;
  return status;
}

} // namespace

DesktopWorkletsModule::DesktopWorkletsModule(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
    : TurboModule(kModuleName, std::move(jsInvoker)) {
  methodMap_["installTurboModule"] = MethodMetadata{1, installTurboModule};
  methodMap_["start"] = MethodMetadata{0, start};
  methodMap_["toggleSlowAnimationsOnUIRuntime"] = MethodMetadata{0, toggleSlowAnimations};
}

DesktopWorkletsModule::~DesktopWorkletsModule() {
  if (runtimeStatus() != nullptr) {
    // Tells the worklet runtime that the runtime it was talking to is gone, so
    // that a job still in flight stops rather than reaching into it.
    runtimeStatus()->setDead();
  }
  heldProxy().reset();
  installedProxy().reset();
}

Value DesktopWorkletsModule::installTurboModule(Runtime &runtime,
                                                TurboModule &module,
                                                const Value * /*args*/,
                                                size_t /*count*/) {
  javaScriptThread() = std::this_thread::get_id();

  auto &self = static_cast<DesktopWorkletsModule &>(module);
  auto uiScheduler = std::make_shared<DesktopUIScheduler>();
  runtimeStatus() = std::make_shared<worklets::RNRuntimeStatus>();

  auto runtimeBindings = std::make_shared<worklets::RuntimeBindings>(worklets::RuntimeBindings{
      // A timer rather than the display's refresh. The display link on each
      // platform belongs to React Native's own AnimationChoreographer, which
      // pauses whenever React Native has no animation running -- exactly when a
      // Reanimated one might be. Sixteen milliseconds is the honest
      // approximation until there is a frame source both can share; see
      .requestAnimationFrame =
          [](std::function<void(const double)> callback) {
            postDelayed(16, [callback = std::move(callback)]() {
              callback(monotonicMilliseconds());
            });
          },
      // Only used in bundle mode, which this is not.
      .nativeLoggingHook = {},
  });

  // The runtime, the call invoker and "am I on the JavaScript queue" used to be
  // three of the proxy's own arguments. Worklets 0.13 groups them into a
  // JSScheduler, which is the same three values and nothing else, so this is a
  // rename rather than a new dependency.
  auto jsScheduler = std::make_shared<worklets::JSScheduler>(
      runtime,
      self.jsInvoker_,
      []() -> bool { return std::this_thread::get_id() == javaScriptThread(); });

  // BundleModeConfig is gone from the constructor too. It only ever said
  // "bundle mode off", which is still what this is: worklets decides it for
  // itself now, and `isDevBundle()` is where it reports the answer.
  auto proxy = std::make_shared<worklets::WorkletsModuleProxy>(
      jsScheduler, uiScheduler, runtimeBindings, runtimeStatus());

  // And then attach it to the runtime the bundle is running on, which is the
  // step that installs `globalThis.__workletsModuleProxy`.
  //
  // Worklets 0.12 did this from the constructor, which is why it took a
  // Runtime. 0.13 split it out, and the lifecycle comment in
  // WorkletsModuleProxy.h spells out the order a platform owes: construct,
  // attach on `installTurboModule`, then `start` when JavaScript asks. Without
  // the attach everything compiles, the module installs, and the first worklet
  // call dies with "Cannot read property 'loadUnpackersWithCode' of
  // undefined" -- the JavaScript reaching for a global nothing had installed.
  //
  // The config says bundle mode off, which is what this platform is; the
  // comment there calls it "a disabled Bundle Mode config".
  proxy->attachToRNRuntime(
      runtime,
      worklets::BundleModeConfig{.enabled = false, .script = nullptr, .sourceURL = ""});

  heldProxy() = proxy;
  installedProxy() = proxy;
  return Value(true);
}

Value DesktopWorkletsModule::start(Runtime & /*runtime*/,
                                   TurboModule & /*module*/,
                                   const Value * /*args*/,
                                   size_t /*count*/) {
  if (auto proxy = installedProxy().lock()) {
    proxy->start();
    return Value(true);
  }
  return Value(false);
}

Value DesktopWorkletsModule::toggleSlowAnimations(Runtime & /*runtime*/,
                                                  TurboModule & /*module*/,
                                                  const Value * /*args*/,
                                                  size_t /*count*/) {
  // A developer-menu affordance; iOS throws here for the same reason.
  LOG(WARNING) << "worklets: slow animations are not supported on this platform";
  return Value(false);
}

#endif

} // namespace basalt

#ifdef BASALT_HAS_WORKLETS
// The logging seam worklets' C++ declares and leaves to each platform: Android
// writes to logcat, Apple to os_log, and this to the same place every other log
// line in this project goes.
namespace worklets {

void PlatformLogger::log(const char *str) {
  LOG(INFO) << "[worklets] " << str;
}

void PlatformLogger::log(const std::string &str) {
  LOG(INFO) << "[worklets] " << str;
}

void PlatformLogger::log(const double d) {
  LOG(INFO) << "[worklets] " << d;
}

void PlatformLogger::log(const int i) {
  LOG(INFO) << "[worklets] " << i;
}

void PlatformLogger::log(const bool b) {
  LOG(INFO) << "[worklets] " << (b ? "true" : "false");
}

} // namespace worklets
#endif
