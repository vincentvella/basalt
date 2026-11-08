#include "AsyncStorageStore.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>
#include <system_error>
#include <utility>

namespace basalt {

namespace {

using nlohmann::json;

// Everything the new value says wins, and an object on both sides is merged
// rather than replaced. `multiMerge`'s whole purpose is updating one field of
// a stored object without reading it first.
void mergeInto(json &into, const json &from) {
  if (!into.is_object() || !from.is_object()) {
    into = from;
    return;
  }
  for (const auto &[key, value] : from.items()) {
    mergeInto(into[key], value);
  }
}

} // namespace

const char *AsyncStorageStore::defaultDatabase() {
  // async-storage's own default, so that a store written through the legacy
  // calls and one written through the current ones are the same store.
  return "RKStorage";
}

// A file name a database name cannot escape.
//
// The name arrives from JavaScript and is used to build a path, which is the
// shape of every directory traversal there has ever been: `..`, a slash, a
// backslash on Windows, a NUL, a drive letter. Rather than enumerate those,
// this keeps a small safe set and escapes everything else, so the mapping is
// total, reversible enough to debug, and has no case to miss.
std::string AsyncStorageStore::fileNameFor(const std::string &database) {
  const std::string name = database.empty() ? defaultDatabase() : database;
  std::string encoded;
  encoded.reserve(name.size() + 8);
  for (const unsigned char character : name) {
    const bool safe = (character >= 'a' && character <= 'z') ||
                      (character >= 'A' && character <= 'Z') ||
                      (character >= '0' && character <= '9') || character == '-' ||
                      character == '_';
    if (safe) {
      encoded.push_back(static_cast<char>(character));
    } else {
      // Percent-encoded, upper case, two digits: `..` becomes `%2E%2E` and
      // names no parent of anything.
      char buffer[4];
      std::snprintf(buffer, sizeof(buffer), "%%%02X", character);
      encoded.append(buffer);
    }
  }
  return encoded + ".json";
}

AsyncStorageStore::AsyncStorageStore(std::filesystem::path root) : root_(std::move(root)) {}

std::map<std::string, std::string> &AsyncStorageStore::loaded(const std::string &database) {
  const auto existing = databases_.find(database);
  if (existing != databases_.end()) {
    return existing->second;
  }

  std::map<std::string, std::string> values;
  const std::filesystem::path file = root_ / fileNameFor(database);
  std::ifstream stream(file);
  if (stream) {
    // A file that is not an object, or not JSON at all, reads as an empty
    // store rather than throwing. The alternative is an application that
    // cannot start because something corrupted a cache, and the first write
    // replaces it.
    const json parsed = json::parse(stream, nullptr, false);
    if (parsed.is_object()) {
      for (const auto &[key, value] : parsed.items()) {
        if (value.is_string()) {
          values.emplace(key, value.get<std::string>());
        }
      }
    }
  }
  return databases_.emplace(database, std::move(values)).first->second;
}

bool AsyncStorageStore::flush(const std::string &database) {
  std::error_code code;
  std::filesystem::create_directories(root_, code);

  json object = json::object();
  for (const auto &[key, value] : databases_[database]) {
    object[key] = value;
  }

  // Written beside and renamed, so an interrupted write leaves the previous
  // store rather than half of this one. Rename is atomic on every filesystem
  // this runs on, and `rename` over an existing file is allowed on all three
  // desktops.
  const std::filesystem::path file = root_ / fileNameFor(database);
  const std::filesystem::path temporary = file.string() + ".writing";
  {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream) {
      return false;
    }
    stream << object.dump();
    if (!stream.good()) {
      return false;
    }
  }
  std::filesystem::rename(temporary, file, code);
  if (code) {
    std::filesystem::remove(temporary, code);
    return false;
  }
  return true;
}

std::vector<StoredValue> AsyncStorageStore::get(const std::string &database,
                                                const std::vector<std::string> &keys) {
  const std::lock_guard<std::mutex> lock(mutex_);
  const std::map<std::string, std::string> &values = loaded(database);
  std::vector<StoredValue> answered;
  answered.reserve(keys.size());
  for (const std::string &key : keys) {
    const auto found = values.find(key);
    answered.push_back(StoredValue{
        key,
        found == values.end() ? std::nullopt : std::optional<std::string>{found->second}});
  }
  return answered;
}

bool AsyncStorageStore::set(const std::string &database,
                            const std::vector<StoredValue> &values) {
  const std::lock_guard<std::mutex> lock(mutex_);
  std::map<std::string, std::string> &stored = loaded(database);
  for (const StoredValue &value : values) {
    if (value.value.has_value()) {
      stored[value.key] = *value.value;
    } else {
      stored.erase(value.key);
    }
  }
  return flush(database);
}

bool AsyncStorageStore::remove(const std::string &database,
                               const std::vector<std::string> &keys) {
  const std::lock_guard<std::mutex> lock(mutex_);
  std::map<std::string, std::string> &stored = loaded(database);
  for (const std::string &key : keys) {
    stored.erase(key);
  }
  return flush(database);
}

std::vector<std::string> AsyncStorageStore::keys(const std::string &database) {
  const std::lock_guard<std::mutex> lock(mutex_);
  const std::map<std::string, std::string> &stored = loaded(database);
  std::vector<std::string> answered;
  answered.reserve(stored.size());
  for (const auto &[key, value] : stored) {
    answered.push_back(key);
  }
  return answered;
}

bool AsyncStorageStore::clear(const std::string &database) {
  const std::lock_guard<std::mutex> lock(mutex_);
  loaded(database).clear();
  return flush(database);
}

bool AsyncStorageStore::merge(const std::string &database,
                              const std::vector<StoredValue> &values) {
  const std::lock_guard<std::mutex> lock(mutex_);
  std::map<std::string, std::string> &stored = loaded(database);
  for (const StoredValue &value : values) {
    if (!value.value.has_value()) {
      stored.erase(value.key);
      continue;
    }
    const json incoming = json::parse(*value.value, nullptr, false);
    const auto existing = stored.find(value.key);
    if (existing == stored.end() || incoming.is_discarded()) {
      stored[value.key] = *value.value;
      continue;
    }
    json current = json::parse(existing->second, nullptr, false);
    if (current.is_discarded()) {
      stored[value.key] = *value.value;
      continue;
    }
    mergeInto(current, incoming);
    stored[value.key] = current.dump();
  }
  return flush(database);
}

} // namespace basalt
