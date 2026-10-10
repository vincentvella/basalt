// `Settings`, which is a file on this platform.
//
// What is worth asserting is not that a key round-trips -- a map does that --
// but the four things this store has to get right for the API above it to
// behave the way an app written against iOS expects:
//
//   * a value outlives the process, which is the whole point of `Settings`;
//   * `merge` answers only the keys that *changed*, because that is what a
//     watcher would be told about and what decides whether anything is written;
//   * a null value deletes rather than stores a null, which is what
//     `RCTSettingsManager` does and what `Settings.set({k: null})` means;
//   * every way the file can be wrong leaves an app running, because a settings
//     file somebody hand-edited is not a reason to refuse to start.
//
// The module over it is three lines of jsi per method and is covered by
// e2e/settings.tsx, which runs a host twice and reads back what the first run
// wrote. This file is the half that can be asserted without a runtime.

#include "TestHarness.h"

#include "SettingsStore.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using basalt::SettingsStore;

namespace {

void setEnvironment(const char *name, const std::string &value) {
#if defined(_WIN32)
  _putenv_s(name, value.c_str());
#else
  setenv(name, value.c_str(), 1);
#endif
}

void unsetEnvironment(const char *name) {
#if defined(_WIN32)
  // The documented way to remove one: `_putenv_s` with an empty value.
  _putenv_s(name, "");
#else
  unsetenv(name);
#endif
}

// A directory of its own per test, removed afterwards. A test that leaves files
// behind makes the next run's failure someone else's.
struct Scratch {
  std::filesystem::path directory;

  Scratch() {
    directory = std::filesystem::temp_directory_path() /
        ("basalt-settings-" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
    std::filesystem::create_directories(directory);
  }

  ~Scratch() {
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
  }

  Scratch(const Scratch &) = delete;
  Scratch &operator=(const Scratch &) = delete;

  std::filesystem::path file() const {
    return directory / "settings.json";
  }

  void write(const std::string &contents) const {
    std::ofstream stream(file());
    stream << contents;
  }

  std::string read() const {
    std::ifstream stream(file());
    std::stringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
  }
};

std::string stringAt(const SettingsStore &store, const char *key) {
  const folly::dynamic &values = store.values();
  const auto found = values.find(key);
  return found == values.items().end() || !found->second.isString()
      ? std::string{}
      : found->second.asString();
}

} // namespace

TEST(settings_a_first_run_has_no_settings_and_is_not_an_error) {
  const Scratch scratch;
  const SettingsStore store(scratch.file());
  EXPECT(store.values().isObject());
  EXPECT_EQ((long)store.values().size(), 0L);
  // Nothing is written by reading, so an app that only ever reads settings
  // leaves no file behind.
  EXPECT(!std::filesystem::exists(scratch.file()));
}

TEST(settings_outlive_the_process) {
  // The claim the whole module exists for. Two stores over one file is what two
  // runs of a host are, minus the process.
  const Scratch scratch;
  {
    SettingsStore store(scratch.file());
    store.merge(folly::dynamic::object("tab", "inbox")("seen", 3));
  }
  const SettingsStore reopened(scratch.file());
  EXPECT_EQ(stringAt(reopened, "tab"), std::string("inbox"));
  EXPECT_EQ((long)reopened.values()["seen"].asInt(), 3L);
}

TEST(settings_merge_answers_only_the_keys_that_changed) {
  const Scratch scratch;
  SettingsStore store(scratch.file());

  const std::vector<std::string> first = store.merge(folly::dynamic::object("tab", "inbox"));
  EXPECT_EQ((long)first.size(), 1L);
  EXPECT_EQ(first[0], std::string("tab"));

  // The same value again. An app re-rendering and setting what it already set
  // is the normal case, and it is neither a change to report nor a reason to
  // write the file.
  const std::vector<std::string> again = store.merge(folly::dynamic::object("tab", "inbox"));
  EXPECT(again.empty());

  const std::vector<std::string> changed = store.merge(folly::dynamic::object("tab", "archive"));
  EXPECT_EQ((long)changed.size(), 1L);
  EXPECT_EQ(stringAt(store, "tab"), std::string("archive"));
}

TEST(settings_merge_is_one_key_deep_like_object_assign) {
  // `Settings.set` is `Object.assign` in JavaScript and this has to agree with
  // it, or the copy in memory and the copy on disk disagree about what a nested
  // object holds.
  const Scratch scratch;
  SettingsStore store(scratch.file());
  store.merge(folly::dynamic::object("window", folly::dynamic::object("x", 10)("y", 20)));
  store.merge(folly::dynamic::object("window", folly::dynamic::object("x", 30)));
  EXPECT_EQ((long)store.values()["window"]["x"].asInt(), 30L);
  EXPECT(store.values()["window"].find("y") == store.values()["window"].items().end());
}

TEST(settings_a_null_value_deletes_the_key) {
  // iOS's behaviour, and not an accident of it: `RCTSettingsManager` calls
  // `removeObjectForKey:` when the value does not convert to a property list,
  // so `Settings.set({tab: null})` forgets the key there and has to here.
  const Scratch scratch;
  SettingsStore store(scratch.file());
  store.merge(folly::dynamic::object("tab", "inbox"));

  const std::vector<std::string> changed = store.merge(folly::dynamic::object("tab", nullptr));
  EXPECT_EQ((long)changed.size(), 1L);
  EXPECT_EQ((long)store.values().size(), 0L);

  // And deleting what is not there changes nothing, so it is not reported.
  EXPECT(store.merge(folly::dynamic::object("tab", nullptr)).empty());
}

TEST(settings_deleting_answers_what_was_actually_there) {
  const Scratch scratch;
  SettingsStore store(scratch.file());
  store.merge(folly::dynamic::object("tab", "inbox")("seen", 3));

  const std::vector<std::string> removed = store.erase({"tab", "never-set"});
  EXPECT_EQ((long)removed.size(), 1L);
  EXPECT_EQ(removed[0], std::string("tab"));
  EXPECT_EQ((long)store.values().size(), 1L);

  const SettingsStore reopened(scratch.file());
  EXPECT_EQ((long)reopened.values().size(), 1L);
}

TEST(settings_a_file_that_is_not_json_starts_empty_rather_than_failing) {
  const Scratch scratch;
  scratch.write("this is not json at all");
  const SettingsStore store(scratch.file());
  EXPECT_EQ((long)store.values().size(), 0L);
}

TEST(settings_a_file_of_the_wrong_shape_starts_empty) {
  // Valid JSON, wrong thing. Keyed access over an array would throw on every
  // call afterwards, so the shape is checked once, at the read.
  const Scratch scratch;
  scratch.write("[1, 2, 3]");
  const SettingsStore store(scratch.file());
  EXPECT(store.values().isObject());
  EXPECT_EQ((long)store.values().size(), 0L);
}

TEST(settings_a_bad_file_is_replaced_by_the_next_write) {
  const Scratch scratch;
  scratch.write("{ truncated");
  SettingsStore store(scratch.file());
  store.merge(folly::dynamic::object("tab", "inbox"));
  EXPECT_EQ(stringAt(SettingsStore(scratch.file()), "tab"), std::string("inbox"));
}

TEST(settings_the_file_is_replaced_whole_and_leaves_nothing_beside_it) {
  // The write goes to a sibling and is renamed over, so that a process that
  // dies mid-write leaves the previous file rather than half of a new one. The
  // sibling must not survive a successful write: a stale `settings.json.new`
  // next to the real file would be a puzzle for whoever looked.
  const Scratch scratch;
  SettingsStore store(scratch.file());
  store.merge(folly::dynamic::object("tab", "inbox"));
  EXPECT(!std::filesystem::exists(scratch.file().string() + ".new"));
  EXPECT(scratch.read().find("inbox") != std::string::npos);
}

TEST(settings_a_directory_that_does_not_exist_yet_is_created) {
  // The first run of a packaged app: nothing under the config directory has its
  // identifier yet.
  const Scratch scratch;
  const std::filesystem::path nested = scratch.directory / "dev.example.demo" / "settings.json";
  SettingsStore store(nested);
  store.merge(folly::dynamic::object("tab", "inbox"));
  EXPECT(std::filesystem::exists(nested));
}

TEST(settings_with_nowhere_to_put_them_stay_in_memory) {
  // A container with no HOME and no APPDATA. The alternative would be a module
  // that throws at startup, which is the failure this whole entry was about.
  SettingsStore store({});
  const std::vector<std::string> changed = store.merge(folly::dynamic::object("tab", "inbox"));
  EXPECT_EQ((long)changed.size(), 1L);
  EXPECT_EQ(stringAt(store, "tab"), std::string("inbox"));
}

TEST(settings_the_path_can_be_pointed_somewhere_by_the_environment) {
  const Scratch scratch;
  setEnvironment(basalt::kSettingsFileVar, scratch.file().string());
  EXPECT_EQ(basalt::settingsFilePath().string(), scratch.file().string());
  unsetEnvironment(basalt::kSettingsFileVar);
}

TEST(settings_live_under_this_desktop_own_config_directory_by_default) {
  // Not the contents of the path -- that is the operating system's answer and
  // differs per machine -- but its shape: somewhere absolute, in a directory of
  // the application's own, called settings.json. An app with no identity file
  // lands under `basalt`, which is what a host built by hand is.
  unsetEnvironment(basalt::kSettingsFileVar);
  const std::filesystem::path path = basalt::settingsFilePath();
  // Empty would mean neither HOME nor APPDATA was set, which is not a machine
  // any suite runs on; if this ever fires, the environment is the finding.
  EXPECT(!path.empty());
  EXPECT(path.is_absolute());
  EXPECT_EQ(path.filename().string(), std::string("settings.json"));
  EXPECT(!path.parent_path().filename().empty());
}
