#include "AsyncStorageModule.h"

#include "AppPaths.h"
#include "JsiPromise.h"

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace basalt {

namespace {

namespace jsi = facebook::jsi;

using facebook::jsi::Runtime;
using facebook::jsi::Value;

// Where the stores live. Under the application's own data directory, in a
// directory of their own, so that what this package wrote is identifiable and
// deletable without knowing which file belongs to which database.
std::filesystem::path storeRoot() {
  const std::string data = applicationDataPath();
  if (data.empty()) {
    return {};
  }
  return std::filesystem::path(data) / "async-storage";
}

Value nowhereToWrite(Runtime &runtime) {
  return rejected(runtime,
                  "async storage has nowhere to write: no application data directory "
                  "could be created");
}

} // namespace

AsyncStorageModule::AsyncStorageModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
    : NativeAsyncStorageCxxSpec<AsyncStorageModule>(std::move(jsInvoker)) {}

AsyncStorageStore &AsyncStorageModule::store() {
  if (store_ == nullptr) {
    store_ = std::make_unique<AsyncStorageStore>(storeRoot());
  }
  return *store_;
}

Value AsyncStorageModule::getValues(Runtime &runtime,
                                    std::string database,
                                    std::vector<std::string> keys) {
  if (storeRoot().empty()) {
    return nowhereToWrite(runtime);
  }
  const std::vector<StoredValue> values = store().get(database, keys);
  return resolved(runtime, facebook::react::bridging::toJs(runtime, values, jsInvoker_));
}

Value AsyncStorageModule::setValues(Runtime &runtime,
                                    std::string database,
                                    std::vector<StoredValue> values) {
  if (storeRoot().empty()) {
    return nowhereToWrite(runtime);
  }
  if (!store().set(database, values)) {
    return rejected(runtime, "async storage could not write " + database);
  }
  // The spec answers with what was written, which is what async-storage's own
  // implementations do: the caller gets back the entries it passed.
  return resolved(runtime, facebook::react::bridging::toJs(runtime, values, jsInvoker_));
}

Value AsyncStorageModule::removeValues(Runtime &runtime,
                                       std::string database,
                                       std::vector<std::string> keys) {
  if (storeRoot().empty()) {
    return nowhereToWrite(runtime);
  }
  if (!store().remove(database, keys)) {
    return rejected(runtime, "async storage could not write " + database);
  }
  return resolved(runtime);
}

Value AsyncStorageModule::getKeys(Runtime &runtime, std::string database) {
  if (storeRoot().empty()) {
    return nowhereToWrite(runtime);
  }
  const std::vector<std::string> keys = store().keys(database);
  return resolved(runtime, facebook::react::bridging::toJs(runtime, keys, jsInvoker_));
}

Value AsyncStorageModule::clearStorage(Runtime &runtime, std::string database) {
  if (storeRoot().empty()) {
    return nowhereToWrite(runtime);
  }
  if (!store().clear(database)) {
    return rejected(runtime, "async storage could not write " + database);
  }
  return resolved(runtime);
}

Value AsyncStorageModule::legacy_multiGet(Runtime &runtime, std::vector<std::string> keys) {
  if (storeRoot().empty()) {
    return nowhereToWrite(runtime);
  }
  const std::vector<StoredValue> values =
      store().get(AsyncStorageStore::defaultDatabase(), keys);
  // `[key, value][]`, where a missing value is null: the shape the old API
  // answered with, and what async-storage's JavaScript still unpacks.
  std::vector<std::tuple<std::string, std::optional<std::string>>> pairs;
  pairs.reserve(values.size());
  for (const StoredValue &value : values) {
    pairs.emplace_back(value.key, value.value);
  }
  return resolved(runtime, facebook::react::bridging::toJs(runtime, pairs, jsInvoker_));
}

Value AsyncStorageModule::legacy_multiSet(
    Runtime &runtime,
    std::vector<std::vector<std::string>> pairs) {
  if (storeRoot().empty()) {
    return nowhereToWrite(runtime);
  }
  std::vector<StoredValue> values;
  values.reserve(pairs.size());
  for (const std::vector<std::string> &pair : pairs) {
    // `[key, value]`. Anything else is not a pair, and skipping it beats
    // indexing past the end of an array JavaScript handed us.
    if (pair.size() == 2) {
      values.push_back(StoredValue{pair[0], pair[1]});
    }
  }
  if (!store().set(AsyncStorageStore::defaultDatabase(), values)) {
    return rejected(runtime, "async storage could not write its default store");
  }
  return resolved(runtime);
}

Value AsyncStorageModule::legacy_multiRemove(Runtime &runtime, std::vector<std::string> keys) {
  if (storeRoot().empty()) {
    return nowhereToWrite(runtime);
  }
  if (!store().remove(AsyncStorageStore::defaultDatabase(), keys)) {
    return rejected(runtime, "async storage could not write its default store");
  }
  return resolved(runtime);
}

Value AsyncStorageModule::legacy_multiMerge(
    Runtime &runtime,
    std::vector<std::vector<std::string>> pairs) {
  if (storeRoot().empty()) {
    return nowhereToWrite(runtime);
  }
  std::vector<StoredValue> values;
  values.reserve(pairs.size());
  for (const std::vector<std::string> &pair : pairs) {
    if (pair.size() == 2) {
      values.push_back(StoredValue{pair[0], pair[1]});
    }
  }
  if (!store().merge(AsyncStorageStore::defaultDatabase(), values)) {
    return rejected(runtime, "async storage could not write its default store");
  }
  return resolved(runtime);
}

Value AsyncStorageModule::legacy_getAllKeys(Runtime &runtime) {
  if (storeRoot().empty()) {
    return nowhereToWrite(runtime);
  }
  const std::vector<std::string> keys = store().keys(AsyncStorageStore::defaultDatabase());
  return resolved(runtime, facebook::react::bridging::toJs(runtime, keys, jsInvoker_));
}

Value AsyncStorageModule::legacy_clear(Runtime &runtime) {
  if (storeRoot().empty()) {
    return nowhereToWrite(runtime);
  }
  if (!store().clear(AsyncStorageStore::defaultDatabase())) {
    return rejected(runtime, "async storage could not write its default store");
  }
  return resolved(runtime);
}

// --- The legacy module, which is what Windows asks for --------------------
//
// One store behind both: an app that wrote through the promise module on a
// Mac and reads through this one on Windows is reading the same file, which
// is the only behaviour that would not be a bug.

namespace {

// The store these share. A function rather than a member, because the two
// modules are separate objects and the file underneath is not.
AsyncStorageStore *legacyStore() {
  static AsyncStorageStore *store = [] {
    const std::string data = applicationDataPath();
    return data.empty() ? nullptr
                        : new AsyncStorageStore(std::filesystem::path(data) / "async-storage");
  }();
  return store;
}

// `callback(error)`, where a success passes nothing at all. React Native's
// old bridge called these with an array of error-like objects or with
// undefined, and async-storage's JavaScript checks only whether the first
// argument is set.
jsi::Value settle(Runtime &runtime, const jsi::Value *args, size_t count, bool ok,
                  const char *message) {
  if (count < 1 || !args[count - 1].isObject() ||
      !args[count - 1].asObject(runtime).isFunction(runtime)) {
    return jsi::Value::undefined();
  }
  const jsi::Function callback = args[count - 1].asObject(runtime).asFunction(runtime);
  if (ok) {
    callback.call(runtime, jsi::Value::undefined());
    return jsi::Value::undefined();
  }
  jsi::Object error(runtime);
  error.setProperty(runtime, "message", jsi::String::createFromUtf8(runtime, message));
  jsi::Array errors(runtime, 1);
  errors.setValueAtIndex(runtime, 0, std::move(error));
  callback.call(runtime, std::move(errors));
  return jsi::Value::undefined();
}

// The keys of a `[key, value][]` argument.
std::vector<std::string> keysOf(Runtime &runtime, const jsi::Value &value) {
  std::vector<std::string> keys;
  if (!value.isObject() || !value.asObject(runtime).isArray(runtime)) {
    return keys;
  }
  const jsi::Array array = value.asObject(runtime).asArray(runtime);
  const size_t length = array.length(runtime);
  keys.reserve(length);
  for (size_t index = 0; index < length; index++) {
    const jsi::Value item = array.getValueAtIndex(runtime, index);
    if (item.isString()) {
      keys.push_back(item.asString(runtime).utf8(runtime));
    }
  }
  return keys;
}

// The pairs of a `[key, value][]` argument, skipping anything that is not one.
std::vector<StoredValue> pairsOf(Runtime &runtime, const jsi::Value &value) {
  std::vector<StoredValue> pairs;
  if (!value.isObject() || !value.asObject(runtime).isArray(runtime)) {
    return pairs;
  }
  const jsi::Array array = value.asObject(runtime).asArray(runtime);
  const size_t length = array.length(runtime);
  pairs.reserve(length);
  for (size_t index = 0; index < length; index++) {
    const jsi::Value item = array.getValueAtIndex(runtime, index);
    if (!item.isObject() || !item.asObject(runtime).isArray(runtime)) {
      continue;
    }
    const jsi::Array pair = item.asObject(runtime).asArray(runtime);
    if (pair.length(runtime) < 2) {
      continue;
    }
    const jsi::Value key = pair.getValueAtIndex(runtime, 0);
    const jsi::Value held = pair.getValueAtIndex(runtime, 1);
    if (!key.isString()) {
      continue;
    }
    pairs.push_back(StoredValue{
        key.asString(runtime).utf8(runtime),
        held.isString() ? std::optional<std::string>{held.asString(runtime).utf8(runtime)}
                        : std::nullopt,
    });
  }
  return pairs;
}

constexpr const char *kNowhere = "async storage has nowhere to write";

} // namespace

LegacyAsyncStorageModule::LegacyAsyncStorageModule(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
    : TurboModule(kModuleName, std::move(jsInvoker)) {
  methodMap_["multiGet"] = MethodMetadata{2, multiGet};
  methodMap_["multiSet"] = MethodMetadata{2, multiSet};
  methodMap_["multiRemove"] = MethodMetadata{2, multiRemove};
  methodMap_["multiMerge"] = MethodMetadata{2, multiMerge};
  methodMap_["getAllKeys"] = MethodMetadata{1, getAllKeys};
  methodMap_["clear"] = MethodMetadata{1, clear};
}

jsi::Value LegacyAsyncStorageModule::multiGet(Runtime &runtime,
                                              facebook::react::TurboModule & /*module*/,
                                              const jsi::Value *args,
                                              size_t count) {
  AsyncStorageStore *store = legacyStore();
  if (store == nullptr || count < 2) {
    return settle(runtime, args, count, false, kNowhere);
  }
  const std::vector<StoredValue> values =
      store->get(AsyncStorageStore::defaultDatabase(), keysOf(runtime, args[0]));

  // `[key, value][]`, with a missing value as null, which is what the old
  // bridge answered and what async-storage's JavaScript unpacks.
  jsi::Array answered(runtime, values.size());
  for (size_t index = 0; index < values.size(); index++) {
    jsi::Array pair(runtime, 2);
    pair.setValueAtIndex(runtime, 0,
                         jsi::String::createFromUtf8(runtime, values[index].key));
    pair.setValueAtIndex(
        runtime, 1,
        values[index].value.has_value()
            ? jsi::Value(jsi::String::createFromUtf8(runtime, *values[index].value))
            : jsi::Value::null());
    answered.setValueAtIndex(runtime, index, std::move(pair));
  }

  const jsi::Function callback = args[count - 1].asObject(runtime).asFunction(runtime);
  callback.call(runtime, jsi::Value::undefined(), std::move(answered));
  return jsi::Value::undefined();
}

jsi::Value LegacyAsyncStorageModule::multiSet(Runtime &runtime,
                                              facebook::react::TurboModule & /*module*/,
                                              const jsi::Value *args,
                                              size_t count) {
  AsyncStorageStore *store = legacyStore();
  if (store == nullptr || count < 2) {
    return settle(runtime, args, count, false, kNowhere);
  }
  const bool ok =
      store->set(AsyncStorageStore::defaultDatabase(), pairsOf(runtime, args[0]));
  return settle(runtime, args, count, ok, "async storage could not write");
}

jsi::Value LegacyAsyncStorageModule::multiRemove(Runtime &runtime,
                                                 facebook::react::TurboModule & /*module*/,
                                                 const jsi::Value *args,
                                                 size_t count) {
  AsyncStorageStore *store = legacyStore();
  if (store == nullptr || count < 2) {
    return settle(runtime, args, count, false, kNowhere);
  }
  const bool ok =
      store->remove(AsyncStorageStore::defaultDatabase(), keysOf(runtime, args[0]));
  return settle(runtime, args, count, ok, "async storage could not write");
}

jsi::Value LegacyAsyncStorageModule::multiMerge(Runtime &runtime,
                                                facebook::react::TurboModule & /*module*/,
                                                const jsi::Value *args,
                                                size_t count) {
  AsyncStorageStore *store = legacyStore();
  if (store == nullptr || count < 2) {
    return settle(runtime, args, count, false, kNowhere);
  }
  const bool ok =
      store->merge(AsyncStorageStore::defaultDatabase(), pairsOf(runtime, args[0]));
  return settle(runtime, args, count, ok, "async storage could not write");
}

jsi::Value LegacyAsyncStorageModule::getAllKeys(Runtime &runtime,
                                                facebook::react::TurboModule & /*module*/,
                                                const jsi::Value *args,
                                                size_t count) {
  AsyncStorageStore *store = legacyStore();
  if (store == nullptr || count < 1) {
    return settle(runtime, args, count, false, kNowhere);
  }
  const std::vector<std::string> keys = store->keys(AsyncStorageStore::defaultDatabase());
  jsi::Array answered(runtime, keys.size());
  for (size_t index = 0; index < keys.size(); index++) {
    answered.setValueAtIndex(runtime, index,
                             jsi::String::createFromUtf8(runtime, keys[index]));
  }
  const jsi::Function callback = args[count - 1].asObject(runtime).asFunction(runtime);
  callback.call(runtime, jsi::Value::undefined(), std::move(answered));
  return jsi::Value::undefined();
}

jsi::Value LegacyAsyncStorageModule::clear(Runtime &runtime,
                                           facebook::react::TurboModule & /*module*/,
                                           const jsi::Value *args,
                                           size_t count) {
  AsyncStorageStore *store = legacyStore();
  if (store == nullptr || count < 1) {
    return settle(runtime, args, count, false, kNowhere);
  }
  const bool ok = store->clear(AsyncStorageStore::defaultDatabase());
  return settle(runtime, args, count, ok, "async storage could not write");
}

std::shared_ptr<facebook::react::TurboModule> makeAsyncStorageTurboModule(
    const std::string &name,
    const std::shared_ptr<facebook::react::CallInvoker> &jsInvoker) {
  if (name == AsyncStorageModule::kModuleName) {
    return std::make_shared<AsyncStorageModule>(jsInvoker);
  }
  // What `createAsyncStorage.windows.ts` asks for, on every desktop rather
  // than only on Windows: the module is the app's to ask for, and answering
  // it everywhere costs nothing and makes one less thing platform-specific.
  if (name == LegacyAsyncStorageModule::kModuleName) {
    return std::make_shared<LegacyAsyncStorageModule>(jsInvoker);
  }
  return nullptr;
}

} // namespace basalt
