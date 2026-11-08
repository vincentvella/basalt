// `@react-native-async-storage/async-storage`, on the desktop.
//
// **This package ships no JavaScript.** The application imports the module it
// already imports on a phone, and what is missing on a desktop is only the
// native half, which is this. That is the whole shape of porting a third-party
// native module here, and it is why this exists at all: nothing had been
// through that path, so what it cost was a guess. See docs/PORTING.md.
//
// The interface is not written here either. async-storage ships a TypeScript
// spec and a `codegenConfig`, React Native's codegen turns that into
// `NativeAsyncStorageCxxSpec<T>` -- a CRTP base whose kModuleName is already
// `RNAsyncStorage` -- and this derives from it. The build runs that codegen
// over the app's own dependencies; see `appCodegen` in cli/desktop.ts.
//
// So the spec cannot drift from the module: if async-storage adds a method or
// changes a signature, the generated base changes with it and this stops
// compiling, which is what a spec is for.

#pragma once

#include <react/renderer/components/AsyncStorageSpec/AsyncStorageSpecJSI.h>

#include "AsyncStorageStore.h"

#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace basalt {

class AsyncStorageModule : public facebook::react::NativeAsyncStorageCxxSpec<AsyncStorageModule> {
 public:
  explicit AsyncStorageModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker);

  // Every method answers a settled promise. The store is files and the calls
  // are immediate, so there is nothing to wait for and no thread to invent;
  // core/JsiPromise.h says the same about every other asynchronous API here.
  facebook::jsi::Value getValues(facebook::jsi::Runtime &runtime,
                                 std::string database,
                                 std::vector<std::string> keys);
  facebook::jsi::Value setValues(facebook::jsi::Runtime &runtime,
                                 std::string database,
                                 std::vector<StoredValue> values);
  facebook::jsi::Value removeValues(facebook::jsi::Runtime &runtime,
                                    std::string database,
                                    std::vector<std::string> keys);
  facebook::jsi::Value getKeys(facebook::jsi::Runtime &runtime, std::string database);
  facebook::jsi::Value clearStorage(facebook::jsi::Runtime &runtime, std::string database);

  // The calls async-storage kept for stores written before it grew databases.
  // They name none, so they are the default one, which is async-storage's own
  // `RKStorage`: a store written by the old API is read by the new one.
  facebook::jsi::Value legacy_multiGet(facebook::jsi::Runtime &runtime,
                                       std::vector<std::string> keys);
  // A pair arrives as a two-element array rather than as a std::tuple, and
  // that is React Native's bridging rather than a choice: Bridging<std::tuple>
  // is BridgingStatic, which defines toJs and no fromJs, so a tuple can be
  // answered and cannot be received. Going out, below, it is a tuple.
  facebook::jsi::Value legacy_multiSet(
      facebook::jsi::Runtime &runtime,
      std::vector<std::vector<std::string>> pairs);
  facebook::jsi::Value legacy_multiRemove(facebook::jsi::Runtime &runtime,
                                          std::vector<std::string> keys);
  facebook::jsi::Value legacy_multiMerge(
      facebook::jsi::Runtime &runtime,
      std::vector<std::vector<std::string>> pairs);
  facebook::jsi::Value legacy_getAllKeys(facebook::jsi::Runtime &runtime);
  facebook::jsi::Value legacy_clear(facebook::jsi::Runtime &runtime);

 private:
  // Answers nullptr once, and a rejected promise from every call, when there
  // is nowhere to write: see applicationDataPath(). An application on a
  // read-only home directory gets an error it can show rather than a crash.
  AsyncStorageStore &store();

  std::unique_ptr<AsyncStorageStore> store_;
};

// What core/PackageModules.h's generated chain calls. Answers nullptr for
// every name but this module's.
std::shared_ptr<facebook::react::TurboModule> makeAsyncStorageTurboModule(
    const std::string &name,
    const std::shared_ptr<facebook::react::CallInvoker> &jsInvoker);

} // namespace basalt

// How `std::vector<StoredValue>` crosses the boundary, in both directions.
//
// Codegen writes these for a spec that names its types; async-storage's spec
// uses inline object literals -- `{key: string, value: string | null}[]` --
// so there is no generated struct and this is the hand-written half. It is
// also the only hand-written interface code in the package.
namespace facebook::react {

template <>
struct Bridging<basalt::StoredValue> {
  static basalt::StoredValue fromJs(jsi::Runtime &runtime,
                                    const jsi::Object &object,
                                    const std::shared_ptr<CallInvoker> & /*jsInvoker*/) {
    const jsi::Value value = object.getProperty(runtime, "value");
    return basalt::StoredValue{
        object.getProperty(runtime, "key").asString(runtime).utf8(runtime),
        value.isString() ? std::optional<std::string>{value.asString(runtime).utf8(runtime)}
                         : std::nullopt,
    };
  }

  static jsi::Object toJs(jsi::Runtime &runtime,
                          const basalt::StoredValue &stored,
                          const std::shared_ptr<CallInvoker> & /*jsInvoker*/) {
    jsi::Object object(runtime);
    object.setProperty(runtime, "key", jsi::String::createFromUtf8(runtime, stored.key));
    object.setProperty(runtime,
                       "value",
                       stored.value.has_value()
                           ? jsi::Value(jsi::String::createFromUtf8(runtime, *stored.value))
                           : jsi::Value::null());
    return object;
  }
};

} // namespace facebook::react
