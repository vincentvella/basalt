#include "ExpoModules.h"

#include "JsRuntimeAccess.h"
#include "JsiPromise.h"
#include "PackageModules.h"
#include "PlatformServices.h"

#ifdef BASALT_HAS_EXPO
#include <EventEmitter.h>
#include <NativeModule.h>
#endif

#include <folly/json.h>
#include <glog/logging.h>
#include <jsi/JSIDynamic.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>

namespace basalt {

namespace {

// Parsed once, at load, so a malformed config is reported when it is read
// rather than when a module happens to ask for it.
std::optional<folly::dynamic> &appConfig() {
  static std::optional<folly::dynamic> config;
  return config;
}

} // namespace

void loadExpoAppConfigBeside(const std::string &bundlePath) {
  std::error_code ignored;
  const std::filesystem::path path =
      std::filesystem::absolute(bundlePath, ignored).parent_path() / "app.config.json";

  std::ifstream file(path);
  if (!file) {
    return;
  }

  std::ostringstream contents;
  contents << file.rdbuf();

  try {
    folly::dynamic parsed = folly::parseJson(contents.str());
    if (!parsed.isObject()) {
      LOG(WARNING) << "expo app config at " << path.string() << " is not an object; ignoring it";
      return;
    }
    appConfig() = std::move(parsed);
  } catch (const std::exception &error) {
    LOG(WARNING) << "could not read the expo app config at " << path.string() << ": "
                 << error.what();
  }
}

std::string expoAppName() {
  const auto &config = appConfig();
  if (!config.has_value()) {
    return {};
  }
  const folly::dynamic *name = config->get_ptr("name");
  return name != nullptr && name->isString() ? name->getString() : std::string{};
}

#ifdef BASALT_HAS_EXPO

namespace {

using facebook::jsi::Array;
using facebook::jsi::Function;
using facebook::jsi::Object;
using facebook::jsi::PropNameID;
using facebook::jsi::Runtime;
using facebook::jsi::String;
using facebook::jsi::Value;

// A module is one of expo's own `NativeModule` instances rather than a plain
// object: that is where `addListener`, `removeListener` and `emit` come from,
// and expo-linking calls `addListener` whether or not anything ever emits.
void addFunction(Runtime &runtime,
                 Object &module,
                 const char *name,
                 unsigned argumentCount,
                 facebook::jsi::HostFunctionType function) {
  module.setProperty(runtime,
                     name,
                     Function::createFromHostFunction(runtime,
                                                      PropNameID::forAscii(runtime, name),
                                                      argumentCount,
                                                      std::move(function)));
}

// ExpoClipboard.
//
// The same clipboard React Native's own `Clipboard` module uses; see
// core/PlatformServices.h. Text only: images and URLs are their own pasteboard
// types on each platform, and the methods for them are deliberately absent so
// that expo-clipboard's own check reports "not available on this platform"
// naming the method, rather than a stub returning null.
Object makeClipboardModule(Runtime &runtime) {
  Object module = expoModule(runtime);

  addFunction(runtime,
              module,
              "getStringAsync",
              1,
              [](Runtime &rt, const Value &, const Value *, size_t) -> Value {
                return resolved(rt, String::createFromUtf8(rt, clipboardText()));
              });

  addFunction(runtime,
              module,
              "setStringAsync",
              2,
              [](Runtime &rt, const Value &, const Value *args, size_t count) -> Value {
                if (count < 1 || !args[0].isString()) {
                  return rejected(rt, "setStringAsync expects a string");
                }
                setClipboardText(args[0].asString(rt).utf8(rt));
                // The boolean is "did it happen", which it did.
                return resolved(rt, Value(true));
              });

  addFunction(runtime,
              module,
              "hasStringAsync",
              0,
              [](Runtime &rt, const Value &, const Value *, size_t) -> Value {
                return resolved(rt, Value(!clipboardText().empty()));
              });

  // iOS-only UI, and expo-clipboard reads it unconditionally on every platform.
  module.setProperty(runtime, "isPasteButtonAvailable", Value(false));

  return module;
}

// ExpoLinking.
//
// Only the two functions expo-linking calls on the native module; `openURL` and
// `canOpenURL` go through React Native's own `Linking`, which this platform
// already implements.
//
// Neither desktop delivers a URL to a running application yet -- macOS needs an
// Apple Event handler and a registered scheme, Linux a desktop entry and a
// single-instance activation -- so `getLinkingURL` is honestly null rather than
// absent: expo-linking calls it without checking, and `useURL()` returning null
// is exactly what "no deep link brought this app up" means.
Object makeLinkingModule(Runtime &runtime) {
  Object module = expoModule(runtime);

  addFunction(runtime,
              module,
              "getLinkingURL",
              0,
              [](Runtime &, const Value &, const Value *, size_t) -> Value {
                return Value::null();
              });

  addFunction(runtime,
              module,
              "clearInitialURL",
              0,
              [](Runtime &, const Value &, const Value *, size_t) -> Value {
                return Value::undefined();
              });

  return module;
}

// ExpoImage's module half: the static functions on `Image`, as against its view.
//
// The cache functions answer honestly rather than pretending. There is no disk
// cache here -- images are fetched through the same loader React Native's
// <Image> uses, which caches in memory for the life of the process -- so
// `clearDiskCacheAsync` reports false, which is expo-image's own way of saying
// "nothing was cleared". `prefetch` is not implemented rather than lying about
// having warmed a cache that does not exist.
//
// `ViewPrototypes` is what `requireNativeViewManager` reads to hang native view
// methods off a ref (`startAnimating`, `lockResourceAsync`). None of them apply
// to a still image drawn by this platform, so the object is there and empty --
// which is the difference between "this view has no extra methods" and the
// TypeError that reading a property of undefined would raise.
Object makeImageModule(Runtime &runtime) {
  Object module = expoModule(runtime);

  addFunction(runtime,
              module,
              "clearMemoryCache",
              0,
              [](Runtime &rt, const Value &, const Value *, size_t) -> Value {
                return resolved(rt, Value(false));
              });

  addFunction(runtime,
              module,
              "clearDiskCache",
              0,
              [](Runtime &rt, const Value &, const Value *, size_t) -> Value {
                return resolved(rt, Value(false));
              });

  module.setProperty(runtime, "ViewPrototypes", Object(runtime));

  return module;
}

// ExponentConstants, which is expo-constants' native half.
//
// Two things live here. The manifest is the app's own Expo config, loaded from
// beside the bundle; expo-constants exposes it as `Constants.expoConfig`, and
// treats any object without a `metadata` key as an embedded manifest, which is
// what this is. Everything else is a native constant, and each one here is
// either true or absent -- expo-constants spreads this object into `Constants`,
// so a field invented here is a field an app will read and believe.
//
// `executionEnvironment: 'bare'` is the accurate answer: bare is "a native app
// that happens to use Expo modules", as against running inside Expo Go or a
// dev client, which a desktop host is not.
Object makeConstantsModule(Runtime &runtime) {
  Object module = expoModule(runtime);

  if (appConfig().has_value()) {
    module.setProperty(
        runtime, "manifest", facebook::jsi::valueFromDynamic(runtime, *appConfig()));
  } else {
    // Null rather than absent: expo-constants checks `ExponentConstants.manifest`
    // and warns clearly when it is missing, which is the message a developer
    // needs. Leaving the property off produces the same warning.
    module.setProperty(runtime, "manifest", Value::null());
  }

  module.setProperty(runtime, "executionEnvironment", String::createFromUtf8(runtime, "bare"));
  module.setProperty(runtime, "appOwnership", Value::null());
  module.setProperty(runtime, "expoVersion", Value::null());
  // A desktop is not a simulator, and nothing here reports a status bar.
  module.setProperty(runtime, "isDevice", Value(true));
  module.setProperty(runtime, "statusBarHeight", Value(0));
  // Android and iOS fill this with their own device details. There is no shape
  // defined for a desktop, and inventing one would be inventing a field.
  module.setProperty(runtime, "platform", Object(runtime));

  addFunction(runtime,
              module,
              "getWebViewUserAgentAsync",
              0,
              [](Runtime &rt, const Value &, const Value *, size_t) -> Value {
                return resolved(rt, Value::null());
              });

  return module;
}

// `expo.getViewConfig(moduleName, viewName)`, which is how an Expo view becomes
// a React Native component.
//
// `requireNativeViewManager` calls this, wraps the answer with React Native's
// `createViewConfig` -- which merges in the base view config, so style, layout
// and touch props come free -- and registers the result as a component called
// `ViewManagerAdapter_<moduleName>`. What is listed here is therefore exactly
// what reaches C++: a prop that is not in `validAttributes` is dropped in
// JavaScript, silently, before anything can diff it.
//
// Native platforms generate this from the module's own definition. Here it is a
// table, because here there is no module definition to generate it from -- the
// views are written directly against Fabric.
Value viewConfigFor(Runtime &runtime, const std::string &moduleName) {
  if (moduleName != "ExpoImage") {
    return Value::null();
  }

  Object attributes(runtime);
  // What core/ExpoImageComponent.cpp parses. expo-image sends a good deal more
  // -- placeholder, transition, blurhash, cachePolicy, SF Symbol props -- and
  // leaving them out here is what stops them travelling to a platform that
  // would ignore them anyway.
  for (const char *name : {"source", "contentFit", "tintColor"}) {
    attributes.setProperty(runtime, name, Value(true));
  }

  Object events(runtime);
  // "topLoad" rather than "load": EventEmitter::normalizeEventType prefixes
  // what C++ dispatches, and this is the name after that.
  for (const auto &[eventName, handler] :
       {std::pair{"topLoadStart", "onLoadStart"},
        std::pair{"topLoad", "onLoad"},
        std::pair{"topError", "onError"}}) {
    Object registration(runtime);
    registration.setProperty(runtime, "registrationName", String::createFromUtf8(runtime, handler));
    events.setProperty(runtime, eventName, std::move(registration));
  }

  Object config(runtime);
  config.setProperty(runtime, "validAttributes", std::move(attributes));
  config.setProperty(runtime, "directEventTypes", std::move(events));
  return Value(runtime, config);
}

} // namespace

Object expoModule(Runtime &runtime) {
  return expo::NativeModule::createInstance(runtime);
}

void addExpoFunction(Runtime &runtime,
                     Object &module,
                     const char *name,
                     unsigned argumentCount,
                     facebook::jsi::HostFunctionType function) {
  addFunction(runtime, module, name, argumentCount, std::move(function));
}

bool emitExpoEvent(const std::string &moduleName,
                   const std::string &eventName,
                   std::function<void(Runtime &, std::vector<Value> &)> arguments) {
  return runOnJsRuntime([moduleName, eventName, arguments = std::move(arguments)](
                            Runtime &runtime) {
    // Found by name each time; see the header for why nothing is held.
    const Value expo = runtime.global().getProperty(runtime, "expo");
    if (!expo.isObject()) {
      return;
    }
    const Value modules = expo.asObject(runtime).getProperty(runtime, "modules");
    if (!modules.isObject()) {
      return;
    }
    const Value module =
        modules.asObject(runtime).getProperty(runtime, moduleName.c_str());
    if (!module.isObject()) {
      // Installed later, or never. Not an error: see the header.
      return;
    }
    try {
      std::vector<Value> args;
      arguments(runtime, args);
      // A named lvalue: expo's emitEvent takes `jsi::Object &` in some versions
      // and `const jsi::Object &` in others, and only an lvalue binds to both.
      Object emitter = module.asObject(runtime);
      expo::EventEmitter::emitEvent(runtime, emitter, eventName, args);
    } catch (const facebook::jsi::JSIException &error) {
      // A listener that throws is Expo's business -- its emitEvent swallows
      // those deliberately. This is the payload itself failing to build, or the
      // module turning out not to be an emitter, and the thread that asked for
      // the emit is long gone and cannot be told.
      //
      // Caught at all because this runs as a scheduler task: an exception let
      // out here unwinds through the runtime's own loop rather than through
      // anything that could report it. JSIException rather than JSError, which
      // is only one of its two halves.
      LOG(ERROR) << "basalt: emitting " << moduleName << "." << eventName
                 << " failed: " << error.what();
    }
  });
}

void installExpoModules(Runtime &runtime, Object &modules) {
  modules.setProperty(runtime, "ExpoClipboard", makeClipboardModule(runtime));
  modules.setProperty(runtime, "ExpoImage", makeImageModule(runtime));
  modules.setProperty(runtime, "ExpoLinking", makeLinkingModule(runtime));
  // Replaces the empty stub installExpoRuntime puts in for Expo's own start-up.
  modules.setProperty(runtime, "ExponentConstants", makeConstantsModule(runtime));


  // And whatever capability packages the build discovered. Last, so that a
  // package replacing a built-in is something you can see happening here rather
  // than a name collision resolved by ordering nobody wrote down.
  installPackageExpoModules(runtime, modules);
}

void installExpoViewConfigs(Runtime &runtime, Object &expo) {
  expo.setProperty(
      runtime,
      "getViewConfig",
      Function::createFromHostFunction(
          runtime,
          PropNameID::forAscii(runtime, "getViewConfig"),
          2,
          [](Runtime &rt, const Value &, const Value *args, size_t count) -> Value {
            if (count < 1 || !args[0].isString()) {
              return Value::null();
            }
            return viewConfigFor(rt, args[0].asString(rt).utf8(rt));
          }));
}

#else

void installExpoModules(facebook::jsi::Runtime & /*runtime*/,
                        facebook::jsi::Object & /*modules*/) {}

void installExpoViewConfigs(facebook::jsi::Runtime & /*runtime*/,
                            facebook::jsi::Object & /*expo*/) {}

// Unlike expoModule and addExpoFunction above, this one has a no-op: a package's
// Expo module is compiled only when there is an Expo, but the native half that
// *reports* -- a subprocess reader, a device watcher -- is portable code with no
// reason to know whether Expo exists. It emits, and without an Expo nothing is
// listening, which is already the ordinary case.
bool emitExpoEvent(
    const std::string & /*moduleName*/,
    const std::string & /*eventName*/,
    std::function<void(facebook::jsi::Runtime &, std::vector<facebook::jsi::Value> &)>
    /*arguments*/) {
  return false;
}

#endif

} // namespace basalt
