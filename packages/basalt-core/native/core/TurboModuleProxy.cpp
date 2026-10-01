#include "TurboModuleProxy.h"

#include <glog/logging.h>

#include <string>

namespace basalt {

using facebook::jsi::Function;
using facebook::jsi::Object;
using facebook::jsi::PropNameID;
using facebook::jsi::Runtime;
using facebook::jsi::Value;

void installTurboModuleProxy(Runtime &runtime) {
  if (runtime.global().hasProperty(runtime, "__turboModuleProxy")) {
    // A non-bridgeless runtime installed a real one. Leave it.
    return;
  }

  runtime.global().setProperty(
      runtime,
      "__turboModuleProxy",
      Function::createFromHostFunction(
          runtime,
          PropNameID::forAscii(runtime, "__turboModuleProxy"),
          1,
          [](Runtime &rt, const Value & /*thisVal*/, const Value *args, size_t count) -> Value {
            if (count < 1 || !args[0].isString()) {
              return Value::undefined();
            }
            const std::string name = args[0].getString(rt).utf8(rt);
            // `nativeModuleProxy` rather than a captured provider: it is what
            // NativeModules reads, so this answers for the same set of modules
            // by construction rather than by being kept in step.
            const Value proxy = rt.global().getProperty(rt, "nativeModuleProxy");
            if (!proxy.isObject()) {
              return Value::undefined();
            }
            // Undefined for an unknown name, not an exception: a caller that
            // asks for a module this platform does not have is the ordinary
            // case, and `TurboModuleRegistry.get` is allowed to return null.
            try {
              return proxy.getObject(rt).getProperty(rt, PropNameID::forUtf8(rt, name));
            } catch (const facebook::jsi::JSError &) {
              return Value::undefined();
            }
          }));
}

} // namespace basalt
