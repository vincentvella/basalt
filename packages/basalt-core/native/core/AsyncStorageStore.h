// The store under `@react-native-async-storage/async-storage`.
//
// Deliberately knows nothing about JavaScript: a key-value store over files,
// so that every rule in it -- what a database name is allowed to be, what
// merging two values means, what happens to a half-written file -- is testable
// without a runtime. AsyncStorageModule is the thin part on top that speaks
// jsi.
//
// One JSON object per database, in one file. async-storage's own
// implementations are SQLite on Android and a directory of files on iOS, and
// both are answers to a phone's constraints: a few megabytes of settings on a
// desktop is a file, and a file is what can be read, backed up and deleted by
// the person whose computer it is.
//
// Held in memory once read, and written whole on every change. That is the
// honest shape for what this holds -- a navigation state, a token, a theme --
// and it is wrong for a large store, which is a thing to notice rather than to
// pre-solve: the alternative is a database, and the time to bring one in is
// when somebody's store is big enough to measure.

#pragma once

#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace basalt {

// A key and what it holds, where absent is `std::nullopt` rather than an empty
// string: async-storage's own API distinguishes them and an app storing "" is
// not an app storing nothing.
struct StoredValue {
  std::string key;
  std::optional<std::string> value;
};

class AsyncStorageStore {
 public:
  // `root` is where the database files go, usually
  // `applicationDataPath()/async-storage`. Nothing is read or created until
  // the first call that needs it, so constructing one on a machine with
  // nowhere to write is not itself a failure.
  explicit AsyncStorageStore(std::filesystem::path root);

  // Reads, in the order asked, with a missing key answering nullopt.
  std::vector<StoredValue> get(const std::string &database,
                               const std::vector<std::string> &keys);

  // Writes. A value of nullopt removes the key, which is what async-storage's
  // `setValues` means by a null value.
  bool set(const std::string &database, const std::vector<StoredValue> &values);

  bool remove(const std::string &database, const std::vector<std::string> &keys);

  std::vector<std::string> keys(const std::string &database);

  bool clear(const std::string &database);

  // `multiMerge`: both sides are JSON objects and the new one wins key by key,
  // recursively. A key that is not an object on both sides is replaced rather
  // than merged, which is what the Android implementation does. A value that
  // is not JSON at all is replaced too, because the alternative is to fail a
  // write over the shape of something somebody else stored.
  bool merge(const std::string &database, const std::vector<StoredValue> &values);

  // Where a database's file is. Public because the rule it encodes is the
  // security-relevant one and is tested directly: a database name arrives
  // from JavaScript, and `../../../.ssh/id_rsa` must not name a file outside
  // the root.
  static std::string fileNameFor(const std::string &database);

  // What the legacy calls use, which name no database: async-storage's own
  // default, so a store written by the old API is read by the new one.
  static const char *defaultDatabase();

 private:
  std::map<std::string, std::string> &loaded(const std::string &database);
  bool flush(const std::string &database);

  std::filesystem::path root_;
  std::mutex mutex_;
  std::map<std::string, std::map<std::string, std::string>> databases_;
};

} // namespace basalt
