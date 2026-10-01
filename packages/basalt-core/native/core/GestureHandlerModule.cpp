#include "GestureHandlerModule.h"

#include "Gestures.h"
#include "PlatformServices.h"

#include <jsi/JSIDynamic.h>

namespace basalt {

using facebook::jsi::Runtime;
using facebook::jsi::Value;
using facebook::react::TurboModule;

namespace {

double numberArg(const Value *args, size_t count, size_t index) {
  return index < count && args[index].isNumber() ? args[index].asNumber() : 0;
}

folly::dynamic objectArg(Runtime &runtime, const Value *args, size_t count, size_t index) {
  if (index >= count || !args[index].isObject()) {
    return folly::dynamic::object();
  }
  return facebook::jsi::dynamicFromValue(runtime, args[index]);
}

} // namespace

DesktopGestureHandlerModule::DesktopGestureHandlerModule(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
    : TurboModule(kModuleName, std::move(jsInvoker)) {
  methodMap_["createGestureHandler"] = MethodMetadata{3, createGestureHandler};
  methodMap_["attachGestureHandler"] = MethodMetadata{3, attachGestureHandler};
  methodMap_["updateGestureHandler"] = MethodMetadata{2, updateGestureHandler};
  methodMap_["dropGestureHandler"] = MethodMetadata{1, dropGestureHandler};
  methodMap_["install"] = MethodMetadata{0, install};
  // Nothing here batches, so there is nothing to flush; and there is no
  // responder to hand over, because this platform's gesture recognisers and
  // React Native's touches are fed from the same place rather than competing
  // for a responder. See core/Gestures.h.
  methodMap_["flushOperations"] = MethodMetadata{0, noop};
  methodMap_["handleSetJSResponder"] = MethodMetadata{2, noop};
  methodMap_["handleClearJSResponder"] = MethodMetadata{0, noop};

  // The registry outlives any one instance of this module -- it is UI-thread
  // state and the module is recreated whenever the runtime is -- so the
  // emitter is installed here and taken away in the destructor rather than
  // left pointing at a module that has gone.
  gestures().setEmitter([this](const std::string &eventName, folly::dynamic payload) {
    emitGestureEvent(eventName, std::move(payload));
  });
}

DesktopGestureHandlerModule::~DesktopGestureHandlerModule() {
  gestures().setEmitter(nullptr);
}

void DesktopGestureHandlerModule::emitGestureEvent(const std::string &eventName,
                                                   folly::dynamic payload) {
  emitDeviceEvent(eventName,
                  [payload = std::move(payload)](Runtime &runtime, std::vector<Value> &args) {
                    args.emplace_back(facebook::jsi::valueFromDynamic(runtime, payload));
                  });
}

Value DesktopGestureHandlerModule::createGestureHandler(Runtime &runtime,
                                                        TurboModule & /*module*/,
                                                        const Value *args,
                                                        size_t count) {
  if (count < 2 || !args[0].isString()) {
    return Value::undefined();
  }
  std::string name = args[0].asString(runtime).utf8(runtime);
  const int tag = static_cast<int>(numberArg(args, count, 1));
  folly::dynamic config = objectArg(runtime, args, count, 2);

  postToUiThread([name = std::move(name), tag, config = std::move(config)]() mutable {
    gestures().create(name, tag, std::move(config));
  });
  return Value::undefined();
}

Value DesktopGestureHandlerModule::attachGestureHandler(Runtime & /*runtime*/,
                                                        TurboModule & /*module*/,
                                                        const Value *args,
                                                        size_t count) {
  const int handlerTag = static_cast<int>(numberArg(args, count, 0));
  const int viewTag = static_cast<int>(numberArg(args, count, 1));
  // The third argument is RNGH's ActionType. 1 is REANIMATED_WORKLET, which
  // means the callbacks are worklets and the events are meant for a worklet
  // runtime rather than for DeviceEventEmitter. Attaching it anyway is right:
  // the handler still runs, and its events are still emitted; what a worklet
  // gesture loses is being handled off the JS thread.
  postToUiThread([handlerTag, viewTag]() { gestures().attach(handlerTag, viewTag); });
  return Value::undefined();
}

Value DesktopGestureHandlerModule::updateGestureHandler(Runtime &runtime,
                                                        TurboModule & /*module*/,
                                                        const Value *args,
                                                        size_t count) {
  const int handlerTag = static_cast<int>(numberArg(args, count, 0));
  folly::dynamic config = objectArg(runtime, args, count, 1);
  postToUiThread([handlerTag, config = std::move(config)]() mutable {
    gestures().update(handlerTag, std::move(config));
  });
  return Value::undefined();
}

Value DesktopGestureHandlerModule::dropGestureHandler(Runtime & /*runtime*/,
                                                      TurboModule & /*module*/,
                                                      const Value *args,
                                                      size_t count) {
  const int handlerTag = static_cast<int>(numberArg(args, count, 0));
  postToUiThread([handlerTag]() { gestures().drop(handlerTag); });
  return Value::undefined();
}

// True means "the JSI bindings are in place". RNGH calls this once, on the
// first render of a GestureHandlerRootView under Fabric, and uses the answer
// only to avoid calling it again.
Value DesktopGestureHandlerModule::install(Runtime & /*runtime*/,
                                           TurboModule & /*module*/,
                                           const Value * /*args*/,
                                           size_t /*count*/) {
  return Value(true);
}

Value DesktopGestureHandlerModule::noop(Runtime & /*runtime*/,
                                        TurboModule & /*module*/,
                                        const Value * /*args*/,
                                        size_t /*count*/) {
  return Value::undefined();
}

} // namespace basalt
