#include "ReanimatedModule.h"

#include "Gestures.h"
#include "PlatformServices.h"
#include "UIManagerAccess.h"

#include <glog/logging.h>

#ifdef BASALT_HAS_REANIMATED
#include <reanimated/NativeModules/ReanimatedModuleProxy.h>
#include <reanimated/RuntimeDecorators/RNRuntimeDecorator.h>
#include <reanimated/Tools/PlatformDepMethodsHolder.h>
#include <worklets/Compat/StableApi.h>
#endif

namespace basalt {

bool hasReanimated() {
#ifdef BASALT_HAS_REANIMATED
  return true;
#else
  return false;
#endif
}

#ifdef BASALT_HAS_REANIMATED

namespace {

using facebook::jsi::Runtime;
using facebook::jsi::Value;
using facebook::react::TurboModule;

std::shared_ptr<reanimated::ReanimatedModuleProxy> &heldProxy() {
  static std::shared_ptr<reanimated::ReanimatedModuleProxy> proxy;
  return proxy;
}

// Reanimated's operations are flushed once a frame, forever, from the moment it
// is installed. The frame is a timer for the same reason worklets' is: the
// display link on each platform belongs to React Native's own choreographer,
// which pauses whenever React Native has no animation of its own -- which is
// exactly when a Reanimated one might be running.
constexpr double kFrameMilliseconds = 16;

void scheduleFlush() {
  postDelayed(kFrameMilliseconds, []() {
    if (auto proxy = heldProxy()) {
      proxy->performOperations();
      scheduleFlush();
    }
  });
}

reanimated::PlatformDepMethodsHolder makePlatformDepMethodsHolder() {
  reanimated::PlatformDepMethodsHolder holder{};

  holder.requestRender = [](std::function<void(const double)> callback) {
    postDelayed(kFrameMilliseconds, [callback = std::move(callback)]() {
      callback(monotonicMilliseconds());
    });
  };

#ifdef __APPLE__
  // iOS takes a snapshot of a view before a shared-element transition. Nothing
  // here does shared-element transitions, and the field exists on Apple builds
  // whether or not it is used.
  holder.forceScreenSnapshotFunction = [](facebook::react::Tag) {};
#endif

  // The direct-to-view path, which skips the shadow tree for props that can be
  // applied to a mounted view immediately. Not wired: this platform's mounting
  // managers only accept mutations from a Fabric transaction, which is what
  // makes their thread rule assertable. Reanimated's other path -- committing
  // to the shadow tree -- is the one used here, and it is the one that works on
  // every platform.
  holder.synchronouslyUpdateUIPropsFunction = [](const int, const folly::dynamic &) {};

  holder.getAnimationTimestamp = []() { return monotonicMilliseconds(); };

  // useAnimatedSensor. A desktop has no accelerometer, gyroscope or
  // magnetometer; -1 is Reanimated's own "could not register".
  holder.registerSensor = [](int, int, int, std::function<void(double[], int)>) { return -1; };
  holder.unregisterSensor = [](int) {};

  // A worklet telling a gesture handler what to do, which is how
  // GestureStateManager.activate() and friends work. The registry is
  // core/Gestures.h's, so this is the two halves of phase 37 and phase 38
  // meeting.
  holder.setGestureStateFunction = [](int handlerTag, int newState) {
    postToUiThread([handlerTag, newState]() { gestures().setStateFromWorklet(handlerTag, newState); });
  };

  // A desktop has no soft keyboard to move out of the way of.
  holder.subscribeForKeyboardEvents = [](std::function<void(int, int)>, bool, bool) { return 0; };
  holder.unsubscribeFromKeyboardEvents = [](int) {};

  holder.maybeFlushUIUpdatesQueueFunction = []() {};

  // CSS transitions driven by the platform rather than by Reanimated's own
  // clock -- an optimisation Apple has and this does not. Declining every
  // property means Reanimated animates them itself.
  holder.cssCanRouteProperty = [](const std::string &,
                                  const reanimated::css::EasingConfig &) { return false; };
  holder.cssRemoveTransition = [](facebook::react::Tag, const std::string &) {};

  // Hover and press pseudo-selectors, which need the view layer to report
  // pointer state per view. Not wired yet.
  holder.attachPseudoSelector = [](facebook::react::Tag,
                                   reanimated::PseudoSelector,
                                   std::function<void(bool)>) {};
  holder.detachPseudoSelector = [](facebook::react::Tag, reanimated::PseudoSelector) {};

  return holder;
}

} // namespace

DesktopReanimatedModule::DesktopReanimatedModule(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
    : TurboModule(kModuleName, std::move(jsInvoker)) {
  methodMap_["installTurboModule"] = MethodMetadata{0, installTurboModule};
}

DesktopReanimatedModule::~DesktopReanimatedModule() {
  heldProxy().reset();
}

Value DesktopReanimatedModule::installTurboModule(Runtime &runtime,
                                                  TurboModule &module,
                                                  const Value * /*args*/,
                                                  size_t /*count*/) {
  auto &self = static_cast<DesktopReanimatedModule &>(module);

  // Worklets put both of these on the JavaScript global when its own module
  // installed. Reading them back is Reanimated's own arrangement for finding
  // the runtime it shares with worklets rather than making a second one.
  auto global = runtime.global();
  auto uiWorkletRuntime = worklets::getWorkletRuntimeFromHolder(
      runtime, global.getPropertyAsObject(runtime, "__UI_WORKLET_RUNTIME_HOLDER"));
  auto uiScheduler = worklets::getUISchedulerFromHolder(
      runtime, global.getPropertyAsObject(runtime, "__UI_SCHEDULER_HOLDER"));

  if (uiWorkletRuntime == nullptr || uiScheduler == nullptr) {
    LOG(ERROR) << "reanimated: the worklets runtime is not installed; is "
                  "react-native-worklets compiled into this host?";
    return Value(false);
  }

  const auto platformDepMethodsHolder = makePlatformDepMethodsHolder();
  auto proxy = std::make_shared<reanimated::ReanimatedModuleProxy>(uiWorkletRuntime,
                                                                   uiScheduler,
                                                                   runtime,
                                                                   self.jsInvoker_,
                                                                   platformDepMethodsHolder,
                                                                   /*isReducedMotion=*/false);
  proxy->init(platformDepMethodsHolder);

  auto &uiRuntime = worklets::getJSIRuntimeFromWorkletRuntime(uiWorkletRuntime);
  reanimated::RNRuntimeDecorator::decorate(runtime, uiRuntime, proxy);

  // The shadow-tree half. Without a UIManager Reanimated still runs -- shared
  // values update and callbacks fire -- and nothing it animates reaches the
  // screen, which is the confusing failure, so it is worth a loud line.
  if (auto uiManager = sharedUIManager()) {
    proxy->initializeFabric(uiManager);
  } else {
    LOG(ERROR) << "reanimated: no UIManager, so animated styles will not be applied";
  }

  // Every event Fabric dispatches, offered to Reanimated before the components
  // see it. That is how a scroll handler or a gesture callback written as a
  // worklet runs at all: without it they are declared, attached and never
  // called.
  //
  // Only events raised on the UI thread, which is what iOS does and for the
  // same reason -- a worklet runtime may only be touched from the thread that
  // owns it, and an event from the JavaScript thread (a layout, a state update)
  // arrives on the wrong one.
  std::weak_ptr<reanimated::ReanimatedModuleProxy> weakProxy = proxy;
  const bool listening = installEventListener(std::make_shared<facebook::react::EventListener>(
      [weakProxy](const facebook::react::RawEvent &rawEvent) {
        if (!isUiThread()) {
          return false;
        }
        if (auto proxy = weakProxy.lock()) {
          return proxy->handleRawEvent(rawEvent, monotonicMilliseconds());
        }
        return false;
      }));
  if (!listening) {
    LOG(WARNING) << "reanimated: no event listener, so scroll and gesture "
                    "worklets will not run";
  }

  heldProxy() = proxy;
  scheduleFlush();
  return Value(true);
}

#endif

} // namespace basalt
