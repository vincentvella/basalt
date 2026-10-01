// What the app calls itself, read from beside its bundle.
//
// A generic host binary cannot know which application it is, and everything the
// operating system keys on identity needs it: a notification's name and icon, a
// taskbar group, a GTK application id. `cli/packageApp.js` writes the answer and
// this is the reading half.
//
// What is worth asserting is the degrading, not the happy path. This file is
// written by a build step and read at startup, so every way it can be wrong is
// a way an app fails to start -- and an app that refuses to run because
// somebody hand-edited a JSON file would be a worse platform than one that runs
// without an icon.

#include "TestHarness.h"

#include "AppIdentity.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using basalt::AppIdentity;
using basalt::readAppIdentity;

namespace {

// A directory with `app.identity.json` in it, and the bundle path a host would
// have been given. Removed by the destructor: a test that leaves files behind
// makes the next run's failure someone else's.
struct Beside {
  std::filesystem::path directory;

  explicit Beside(const std::string &contents) {
    directory = std::filesystem::temp_directory_path() /
        ("basalt-identity-" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
    std::filesystem::create_directories(directory);
    if (!contents.empty()) {
      std::ofstream file(directory / "app.identity.json");
      file << contents;
    }
  }

  ~Beside() {
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
  }

  Beside(const Beside &) = delete;
  Beside &operator=(const Beside &) = delete;

  std::string bundlePath() const {
    return (directory / "main.jsbundle.js").string();
  }
};

} // namespace

TEST(identity_is_read_from_beside_the_bundle) {
  const Beside beside(R"({
    "name": "The Demo",
    "identifier": "dev.example.demo",
    "schemes": ["demo", "demo2"],
    "version": "1.2.3"
  })");

  const AppIdentity identity = readAppIdentity(beside.bundlePath());
  EXPECT(!identity.empty());
  EXPECT_EQ(identity.name, std::string("The Demo"));
  EXPECT_EQ(identity.identifier, std::string("dev.example.demo"));
  EXPECT_EQ(identity.version, std::string("1.2.3"));
  EXPECT_EQ((long)identity.schemes.size(), 2L);
  EXPECT_EQ(identity.schemes[0], std::string("demo"));
}

TEST(identity_absent_is_not_an_error) {
  // An app built by hand, with no CLI anywhere near it. Every caller treats an
  // empty identity as "this platform's identity features are off", which is the
  // answer all three hosts gave before any of this existed.
  const Beside beside("");
  EXPECT(readAppIdentity(beside.bundlePath()).empty());
}

TEST(identity_malformed_is_the_same_as_absent) {
  const Beside beside("this is not json at all");
  EXPECT(readAppIdentity(beside.bundlePath()).empty());
}

TEST(identity_of_the_wrong_shape_is_the_same_as_absent) {
  // Valid JSON, wrong thing. Refusing to start over this would be worse than
  // starting without an icon.
  const Beside beside("[1, 2, 3]");
  EXPECT(readAppIdentity(beside.bundlePath()).empty());
}

TEST(identity_missing_fields_are_left_empty_rather_than_guessed) {
  // The identifier is the one field that decides anything -- `empty()` is
  // defined by it, because a name with no identifier cannot be registered with
  // any of the three desktops.
  const Beside beside(R"({"name": "Nameless"})");
  const AppIdentity identity = readAppIdentity(beside.bundlePath());
  EXPECT(identity.empty());
  EXPECT_EQ(identity.name, std::string("Nameless"));
  EXPECT(identity.schemes.empty());
}

TEST(identity_a_scheme_list_with_rubbish_in_it_keeps_the_strings) {
  const Beside beside(R"({"identifier": "dev.example.demo", "schemes": ["demo", 7, null]})");
  const AppIdentity identity = readAppIdentity(beside.bundlePath());
  EXPECT(!identity.empty());
  EXPECT_EQ((long)identity.schemes.size(), 1L);
  EXPECT_EQ(identity.schemes[0], std::string("demo"));
}
