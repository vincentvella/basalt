#include "AsyncStorageModule.h"

#include "AppPaths.h"
#include "JsiPromise.h"

#include <filesystem>
#include <utility>

namespace basalt {

namespace {

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

std::shared_ptr<facebook::react::TurboModule> makeAsyncStorageTurboModule(
    const std::string &name,
    const std::shared_ptr<facebook::react::CallInvoker> &jsInvoker) {
  if (name != AsyncStorageModule::kModuleName) {
    return nullptr;
  }
  return std::make_shared<AsyncStorageModule>(jsInvoker);
}

} // namespace basalt
