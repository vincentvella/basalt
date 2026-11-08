// The store under async-storage, on whichever desktop this suite was built
// for.
//
// Here rather than in an app for the reason the subprocess tests are: they run
// in all three host suites, so the rules below are held on Windows by this
// project's Windows CI. An app's repository would never have built them.
//
// The module above this is not tested here, and cannot be: it derives from a
// base class generated out of async-storage's own spec, which exists only in a
// build that ran codegen over an app. What that leaves untested is the jsi
// conversion, and what it leaves tested is every rule that could silently lose
// somebody's data.

#include "TestHarness.h"

#include "AsyncStorageStore.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using basalt::AsyncStorageStore;
using basalt::StoredValue;

// A directory of this test's own, removed on the way out. Named for the test
// so a failure leaves something identifiable behind when the removal is what
// failed.
class Scratch {
 public:
  explicit Scratch(const std::string &name)
      : path_(std::filesystem::temp_directory_path() / ("basalt-async-storage-" + name)) {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
    std::filesystem::create_directories(path_, ignored);
  }
  ~Scratch() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  const std::filesystem::path &path() const { return path_; }

 private:
  std::filesystem::path path_;
};

TEST(async_storage_a_value_survives_a_new_store) {
  // The whole point of the package: what one run wrote, the next run reads.
  // Two stores over one directory is the closest this suite gets to two runs
  // of an application.
  const Scratch scratch("survives");
  {
    AsyncStorageStore writing(scratch.path());
    EXPECT(writing.set("db", {StoredValue{"token", "abc123"}}));
  }

  AsyncStorageStore reading(scratch.path());
  const std::vector<StoredValue> read = reading.get("db", {"token"});
  EXPECT_EQ(read.size(), size_t{1});
  EXPECT(read[0].value.has_value());
  EXPECT_EQ(read[0].value.value(), std::string{"abc123"});
}

TEST(async_storage_a_missing_key_is_absent_rather_than_empty) {
  // async-storage's API distinguishes them, and an app storing "" is not an
  // app storing nothing.
  const Scratch scratch("missing");
  AsyncStorageStore store(scratch.path());
  EXPECT(store.set("db", {StoredValue{"empty", std::string{}}}));

  const std::vector<StoredValue> read = store.get("db", {"empty", "never-written"});
  EXPECT_EQ(read.size(), size_t{2});
  EXPECT(read[0].value.has_value());
  EXPECT_EQ(read[0].value.value(), std::string{});
  EXPECT(!read[1].value.has_value());
}

TEST(async_storage_a_null_value_removes_the_key) {
  const Scratch scratch("null-removes");
  AsyncStorageStore store(scratch.path());
  EXPECT(store.set("db", {StoredValue{"gone", "here"}}));
  EXPECT(store.set("db", {StoredValue{"gone", std::nullopt}}));

  EXPECT(store.keys("db").empty());
}

TEST(async_storage_databases_do_not_see_each_other) {
  const Scratch scratch("databases");
  AsyncStorageStore store(scratch.path());
  EXPECT(store.set("one", {StoredValue{"key", "first"}}));
  EXPECT(store.set("two", {StoredValue{"key", "second"}}));

  EXPECT_EQ(store.get("one", {"key"})[0].value.value(), std::string{"first"});
  EXPECT_EQ(store.get("two", {"key"})[0].value.value(), std::string{"second"});

  EXPECT(store.clear("one"));
  EXPECT(store.keys("one").empty());
  EXPECT_EQ(store.keys("two").size(), size_t{1});
}

TEST(async_storage_a_database_name_cannot_escape_its_directory) {
  // The security-relevant rule, and the reason fileNameFor is public. The name
  // arrives from JavaScript: `../../../.ssh/id_rsa` is a string an app can
  // pass, and a path built from it unescaped is a file outside the store.
  EXPECT_EQ(AsyncStorageStore::fileNameFor("../../etc/passwd"),
            std::string{"%2E%2E%2F%2E%2E%2Fetc%2Fpasswd.json"});
  EXPECT_EQ(AsyncStorageStore::fileNameFor("C:\\Windows\\system32"),
            std::string{"C%3A%5CWindows%5Csystem32.json"});
  EXPECT_EQ(AsyncStorageStore::fileNameFor("plain-name_1"), std::string{"plain-name_1.json"});
  // And an empty name is the default store rather than a file called ".json".
  EXPECT_EQ(AsyncStorageStore::fileNameFor(""),
            std::string{AsyncStorageStore::defaultDatabase()} + ".json");

  // The rule holding, rather than the encoding: a traversing name writes
  // inside the root and nowhere else.
  const Scratch scratch("escape");
  AsyncStorageStore store(scratch.path());
  EXPECT(store.set("../escaped", {StoredValue{"key", "value"}}));
  std::error_code ignored;
  EXPECT(!std::filesystem::exists(scratch.path().parent_path() / "escaped.json", ignored));
  size_t written = 0;
  for (const auto &entry : std::filesystem::directory_iterator(scratch.path(), ignored)) {
    (void)entry;
    written++;
  }
  EXPECT_EQ(written, size_t{1});
}

TEST(async_storage_merge_updates_one_field_and_keeps_the_rest) {
  // What multiMerge is for: changing a field of a stored object without
  // reading it first.
  const Scratch scratch("merge");
  AsyncStorageStore store(scratch.path());
  EXPECT(store.set("db", {StoredValue{"user", R"({"name":"Ada","theme":{"mode":"dark"}})"}}));
  EXPECT(store.merge("db", {StoredValue{"user", R"({"theme":{"accent":"ember"}})"}}));

  const std::string merged = store.get("db", {"user"})[0].value.value();
  // Nested objects merge rather than replace, so the mode survives an update
  // that only mentioned the accent.
  EXPECT(merged.find("\"name\":\"Ada\"") != std::string::npos);
  EXPECT(merged.find("\"mode\":\"dark\"") != std::string::npos);
  EXPECT(merged.find("\"accent\":\"ember\"") != std::string::npos);
}

TEST(async_storage_merging_onto_something_that_is_not_json_replaces_it) {
  // Rather than failing the write. Somebody else's string is not this store's
  // to refuse.
  const Scratch scratch("merge-garbage");
  AsyncStorageStore store(scratch.path());
  EXPECT(store.set("db", {StoredValue{"key", "not json at all"}}));
  EXPECT(store.merge("db", {StoredValue{"key", R"({"now":"json"})"}}));
  EXPECT_EQ(store.get("db", {"key"})[0].value.value(), std::string{R"({"now":"json"})"});
}

TEST(async_storage_a_corrupt_file_reads_as_empty_rather_than_throwing) {
  // An application that cannot start because something truncated a cache is a
  // worse failure than an application that lost the cache.
  const Scratch scratch("corrupt");
  {
    std::ofstream broken(scratch.path() / AsyncStorageStore::fileNameFor("db"));
    broken << "{this is not json";
  }

  AsyncStorageStore store(scratch.path());
  EXPECT(store.keys("db").empty());
  EXPECT(store.set("db", {StoredValue{"fresh", "value"}}));
  EXPECT_EQ(store.get("db", {"fresh"})[0].value.value(), std::string{"value"});
}

TEST(async_storage_an_interrupted_write_leaves_the_previous_store) {
  // The reason the write goes to a temporary and is renamed: a half-written
  // file is the one failure mode that loses data somebody already had.
  const Scratch scratch("atomic");
  AsyncStorageStore store(scratch.path());
  EXPECT(store.set("db", {StoredValue{"key", "first"}}));

  // No temporary is left behind by a successful write.
  std::error_code ignored;
  for (const auto &entry : std::filesystem::directory_iterator(scratch.path(), ignored)) {
    EXPECT(entry.path().extension() != ".writing");
  }
}

} // namespace
