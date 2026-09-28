#include "ExpoRuntime.h"

#include "ExpoModules.h"
#include "FontRegistry.h"

#include <filesystem>
#include <random>
#include <string>
#include <string_view>

#ifdef BASALT_HAS_EXPO
#include <EventEmitter.h>
#include <JSI/JSIUtils.h>
#include <NativeModule.h>
#include <SharedObject.h>
#include <SharedRef.h>
#endif

namespace basalt {

bool hasExpoRuntime() {
#ifdef BASALT_HAS_EXPO
  return true;
#else
  return false;
#endif
}

#ifdef BASALT_HAS_EXPO

namespace {

// A version 4 UUID: 122 random bits, with the version and variant fields set as
// RFC 4122 requires. `std::random_device` rather than a seeded generator --
// this names things that may outlive the process and must not repeat between
// two runs started in the same millisecond.
std::string randomUuidV4() {
  std::random_device entropy;
  std::uniform_int_distribution<unsigned> nibble(0, 15);

  static constexpr char kHex[] = "0123456789abcdef";
  std::string uuid = "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx";
  for (char &c : uuid) {
    if (c == 'x') {
      c = kHex[nibble(entropy)];
    } else if (c == 'y') {
      // The variant: one of 8, 9, a or b.
      c = kHex[(nibble(entropy) & 0x3U) | 0x8U];
    }
  }
  return uuid;
}

using facebook::jsi::Function;
using facebook::jsi::Object;
using facebook::jsi::PropNameID;
using facebook::jsi::Runtime;
using facebook::jsi::String;
using facebook::jsi::Value;

// An already-settled promise, which is all this module needs: registering a
// font is synchronous, and the JavaScript signature is async only because it is
// asynchronous everywhere else.
Value resolvedPromise(Runtime &rt) {
  return rt.global()
      .getPropertyAsObject(rt, "Promise")
      .getPropertyAsFunction(rt, "resolve")
      .call(rt);
}

Value rejectedPromise(Runtime &rt, const std::string &message) {
  Function error = rt.global().getPropertyAsFunction(rt, "Error");
  Value reason = error.callAsConstructor(rt, String::createFromUtf8(rt, message));
  return rt.global()
      .getPropertyAsObject(rt, "Promise")
      .getPropertyAsFunction(rt, "reject")
      .call(rt, reason);
}

// expo-font hands over whatever `expo-asset` resolved: a string, or an object
// with a localUri or uri. Both shapes appear depending on how the asset was
// bundled, so both are read rather than one being assumed.
std::string localPathFrom(Runtime &rt, const Value &resource) {
  std::string uri;
  if (resource.isString()) {
    uri = resource.asString(rt).utf8(rt);
  } else if (resource.isObject()) {
    Object object = resource.asObject(rt);
    for (const char *key : {"localUri", "uri"}) {
      Value value = object.getProperty(rt, key);
      if (value.isString()) {
        uri = value.asString(rt).utf8(rt);
        break;
      }
    }
  }

  if (uri.rfind("file://", 0) == 0) {
    uri.erase(0, std::string_view{"file://"}.size());
  }
  return uri;
}

// ExpoAsset, which is how expo-asset turns an asset's URL into a local file.
//
// For a `file://` URL there is nothing to download: React Native resolved it
// beside the bundle and it is already on disk, so the work is confirming that
// and handing the same URL back. The existence check is the point -- without it
// a missing asset succeeds here and fails much later, somewhere that has no
// idea which file was meant.
//
// An http URL is what a dev server produces, and fetching one is not
// implemented. It rejects saying so rather than pretending.
Object makeAssetModule(Runtime &runtime) {
  Object module(runtime);

  module.setProperty(
      runtime,
      "downloadAsync",
      Function::createFromHostFunction(
          runtime,
          PropNameID::forAscii(runtime, "downloadAsync"),
          3,
          [](Runtime &rt, const Value &, const Value *args, size_t count) -> Value {
            if (count < 1 || !args[0].isString()) {
              return rejectedPromise(rt, "downloadAsync expects a url");
            }
            const std::string url = args[0].asString(rt).utf8(rt);

            if (url.rfind("file://", 0) != 0) {
              return rejectedPromise(
                  rt,
                  "react-native-basalt cannot fetch assets over the network yet; "
                  "got " + url);
            }

            const std::string path = url.substr(std::string_view{"file://"}.size());
            std::error_code ignored;
            if (!std::filesystem::is_regular_file(path, ignored)) {
              return rejectedPromise(
                  rt,
                  "no asset at " + path +
                      ". Assets are not copied beside the bundle yet.");
            }

            return rt.global()
                .getPropertyAsObject(rt, "Promise")
                .getPropertyAsFunction(rt, "resolve")
                .call(rt, String::createFromUtf8(rt, url));
          }));

  return module;
}

// ExpoFontLoader, as far as a non-web platform is concerned. expo-font's own
// type declaration marks everything but these two as web-only.
Object makeFontLoader(Runtime &runtime) {
  Object loader(runtime);

  loader.setProperty(
      runtime,
      "loadAsync",
      Function::createFromHostFunction(
          runtime,
          PropNameID::forAscii(runtime, "loadAsync"),
          2,
          [](Runtime &rt, const Value &, const Value *args, size_t count) -> Value {
            if (count < 2 || !args[0].isString()) {
              return rejectedPromise(rt, "loadAsync expects a family name and a font");
            }
            const std::string name = args[0].asString(rt).utf8(rt);
            const std::string path = localPathFrom(rt, args[1]);
            if (path.empty()) {
              return rejectedPromise(
                  rt, "could not find a local file for font '" + name + "'");
            }
            if (!registerFont(name, path)) {
              return rejectedPromise(rt, "could not load font '" + name + "' from " + path);
            }
            return resolvedPromise(rt);
          }));

  loader.setProperty(
      runtime,
      "isLoaded",
      Function::createFromHostFunction(
          runtime,
          PropNameID::forAscii(runtime, "isLoaded"),
          1,
          [](Runtime &rt, const Value &, const Value *args, size_t count) -> Value {
            if (count < 1 || !args[0].isString()) {
              return Value(false);
            }
            return Value(isFontRegistered(args[0].asString(rt).utf8(rt)));
          }));

  loader.setProperty(
      runtime,
      "getLoadedFonts",
      Function::createFromHostFunction(
          runtime,
          PropNameID::forAscii(runtime, "getLoadedFonts"),
          0,
          [](Runtime &rt, const Value &, const Value *, size_t) -> Value {
            Object array = rt.global()
                               .getPropertyAsFunction(rt, "Array")
                               .callAsConstructor(rt)
                               .asObject(rt);
            Function push = array.getPropertyAsFunction(rt, "push");

            const std::string joined = registeredFontNamesJoined('\n');
            std::size_t start = 0;
            while (start < joined.size()) {
              const std::size_t end = joined.find('\n', start);
              const std::string one =
                  joined.substr(start, end == std::string::npos ? std::string::npos : end - start);
              push.callWithThis(rt, array, String::createFromUtf8(rt, one));
              if (end == std::string::npos) {
                break;
              }
              start = end + 1;
            }
            return Value(rt, array);
          }));

  return loader;
}

// `expo/fetch`, which this platform does not have, routed back to the one it
// does.
//
// Expo replaces `globalThis.fetch` with its own WinterCG implementation, built
// on a native module called `ExpoFetchModule` -- a pair of SharedObject classes
// with a streaming body between them. There is no port of it here, so the
// replacement is a lazy getter that throws `Cannot find native module
// 'ExpoFetchModule'` the first time anything in the app calls `fetch`. That is
// most apps, immediately, and it is not a failure an app can catch: it happens
// inside the global it was calling.
//
// Expo's own way out is `EXPO_PUBLIC_USE_RN_FETCH`, which leaves React Native's
// fetch in place -- and React Native's fetch works here, over the same curl
// client core/HttpClient.cpp gives the rest of the networking stack. So the
// default is set here rather than asked of every app, because "install basalt
// and the desktop works" stops being true if each app has to discover this
// variable from a crash inside `fetch`.
//
// **Before the bundle, so that everything downstream agrees.** Metro's prelude
// opens with `process = globalThis.process || {}` and React Native's
// setUpGlobals with `global.process = global.process || {}`, both of which keep
// what is already there. Expo's CLI then defines the app's own `EXPO_PUBLIC_`
// variables over the top in development, which is the right precedence: a
// developer who sets this explicitly beats the default. Plain assignment rather
// than defineProperty for that reason -- a non-configurable property here would
// make their `Object.defineProperties` throw.
//
// What is lost is what `expo/fetch` adds: a streamed response body. React
// Native's fetch buffers, and an app importing `expo/fetch` directly still gets
// the error naming the module, which is the honest answer until someone ports
// it.
void useReactNativeFetch(facebook::jsi::Runtime &runtime) {
  namespace jsi = facebook::jsi;
  static constexpr const char *kVariable = "EXPO_PUBLIC_USE_RN_FETCH";

  jsi::Object global = runtime.global();
  const jsi::Value processValue = global.getProperty(runtime, "process");
  jsi::Object process =
      processValue.isObject() ? processValue.asObject(runtime) : jsi::Object(runtime);

  const jsi::Value envValue = process.getProperty(runtime, "env");
  jsi::Object env = envValue.isObject() ? envValue.asObject(runtime) : jsi::Object(runtime);

  // Only as a default. Nothing sets this before the bundle today, but something
  // that did would have meant it.
  if (env.getProperty(runtime, kVariable).isUndefined()) {
    env.setProperty(runtime, kVariable, jsi::String::createFromAscii(runtime, "1"));
  }

  process.setProperty(runtime, "env", std::move(env));
  global.setProperty(runtime, "process", std::move(process));
}

} // namespace

void installExpoRuntime(facebook::jsi::Runtime &runtime) {
  namespace jsi = facebook::jsi;

  // `expo` has to exist before the classes go in: each of them reaches for
  // `global.expo` through expo::common::getCoreObject and hangs itself off it.
  // Installing them first throws on a property that is not there yet.
  runtime.global().setProperty(runtime, "expo", jsi::Object(runtime));

  expo::EventEmitter::installClass(runtime);

  // The releaser is how Expo hands a shared object's native half back to be
  // freed. On Android it reaches through JNI to the owning context. Nothing
  // here owns any native Expo object -- no Expo module is ported yet -- so
  // there is nothing to free, and the honest implementation of "free nothing"
  // is to do nothing.
  expo::SharedObject::installBaseClass(runtime, [](expo::SharedObject::ObjectId) {});
  expo::SharedRef::installBaseClass(runtime);
  expo::NativeModule::installClass(runtime);

  // The module registry.
  //
  // ExpoAsset is here rather than in ExpoModules.cpp because it is older than
  // the module host and because it is half a stub: it confirms a `file://`
  // asset exists and rejects an http one, which is enough for Expo's own
  // start-up and no more. ExponentConstants used to sit beside it as an empty
  // object for the same reason, and is a real module now.
  jsi::Object modules(runtime);
  modules.setProperty(runtime, "ExpoAsset", makeAssetModule(runtime));

  // The first Expo module here that is not a stub. It loads a real font file
  // into fontconfig, which is where Pango looks, so `fontFamily` works
  // afterwards for text this platform renders. See src/LinuxFonts.cpp.
  modules.setProperty(runtime, "ExpoFontLoader", makeFontLoader(runtime));

  // This platform's own Expo modules. See core/ExpoModules.h; they are separate
  // from the stubs above because they are not stubs.
  installExpoModules(runtime, modules);

  // expo-modules-core builds its NativeModulesProxy by iterating this, falling
  // back to the legacy proxy when it is absent, so its presence matters even
  // when it is empty.
  jsi::Object proxy(runtime);
  proxy.setProperty(runtime, "exportedMethods", jsi::Object(runtime));
  proxy.setProperty(runtime, "viewManagersMetadata", jsi::Object(runtime));
  modules.setProperty(runtime, "NativeModulesProxy", std::move(proxy));

  jsi::Object core = runtime.global().getPropertyAsObject(runtime, "expo");
  core.setProperty(runtime, "modules", std::move(modules));
  installExpoViewConfigs(runtime, core);

  // Before the bundle evaluates, which is the only moment early enough; see the
  // function.
  useReactNativeFetch(runtime);

  // `expo.uuidv4`, which expo-modules-core's own `uuid.v4()` calls and throws
  // without: "Native UUID version 4 generator implementation wasn't found in
  // `expo-modules-core`". It is not a notifications concern, though that is
  // where it was found -- `scheduleNotificationAsync` names a notification with
  // one when an app does not, and so does every other Expo package that
  // generates an identifier.
  //
  // `uuidv5` is deliberately absent: it is a SHA-1 of a namespace and a name,
  // which is a hash implementation rather than a random number, and expo
  // reports a missing one by name the way it reports a missing method.
  core.setProperty(
      runtime,
      "uuidv4",
      jsi::Function::createFromHostFunction(
          runtime,
          jsi::PropNameID::forAscii(runtime, "uuidv4"),
          0,
          [](jsi::Runtime &rt, const jsi::Value &, const jsi::Value *, size_t) -> jsi::Value {
            return jsi::String::createFromUtf8(rt, randomUuidV4());
          }));
}

#else

void installExpoRuntime(facebook::jsi::Runtime & /*runtime*/) {}

#endif

} // namespace basalt
