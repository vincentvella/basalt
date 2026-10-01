// The instrument that lets the harness end a run at a moment of its choosing.
//
// What is testable here is the decision -- is the variable set, and is the file
// there yet -- rather than the quitting, which is three different shutdowns and
// belongs to each host. This runs on all three, which is the point: the
// instrument exists because the *portable* way to end a host was a signal, and
// Windows has no SIGTERM.

#include "TestHarness.h"

#include "TestQuitFile.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

// A path that does not exist yet, in the place the platform keeps such things.
std::string scratchPath() {
  std::error_code ignored;
  const std::filesystem::path path =
      std::filesystem::temp_directory_path(ignored) / "basalt-test-quit-file-probe";
  return path.string();
}

void setVariable(const std::string &value) {
#ifdef _WIN32
  _putenv_s(basalt::kTestQuitFileVar, value.c_str());
#else
  setenv(basalt::kTestQuitFileVar, value.c_str(), 1);
#endif
}

} // namespace

// Set before anything reads it, because the path is cached on first read -- the
// environment does not change under a running process, and each host asks for
// this on a timer. One test therefore establishes it for the file, which is why
// the ordering below is deliberate rather than incidental.
TEST(quit_file_reads_the_variable) {
  const std::string path = scratchPath();
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
  setVariable(path);

  EXPECT(basalt::testQuitFilePath().has_value());
  EXPECT_EQ(*basalt::testQuitFilePath(), path);
}

TEST(quit_file_is_not_there_until_it_is) {
  const std::string path = *basalt::testQuitFilePath();
  std::error_code ignored;
  std::filesystem::remove(path, ignored);

  // The whole contract: false until the file exists, and true once it does.
  // A host polls this, so the transition is the instrument.
  EXPECT(!basalt::testQuitFileAppeared());

  { std::ofstream created(path); }
  EXPECT(basalt::testQuitFileAppeared());

  // And not cached, unlike the path: a stale true would quit the next run at
  // startup, before it had done anything, and the failure would look like a
  // crash rather than like a leftover file.
  std::filesystem::remove(path, ignored);
  EXPECT(!basalt::testQuitFileAppeared());
}

TEST(quit_file_is_content_free) {
  const std::string path = *basalt::testQuitFilePath();
  std::error_code ignored;

  // Existence is the message. An empty file is the normal case, because the
  // harness has nothing to say beyond "now" -- and if content mattered there
  // would be a race between creating the file and writing it.
  { std::ofstream created(path); }
  EXPECT(basalt::testQuitFileAppeared());

  { std::ofstream created(path); created << "ignored"; }
  EXPECT(basalt::testQuitFileAppeared());

  // A directory at that path counts too. Nothing creates one, and reporting it
  // as "not yet" would hang the run instead of ending it.
  std::filesystem::remove(path, ignored);
  std::filesystem::create_directory(path, ignored);
  EXPECT(basalt::testQuitFileAppeared());
  std::filesystem::remove(path, ignored);
}

TEST(quit_file_poll_and_message_are_shared) {
  // The three hosts read these rather than spelling them out, so that a person
  // reading three transcripts side by side sees the same shutdown in each.
  // BASALT_QUIT_AFTER_MS had to have its line retrofitted onto Windows after a
  // scenario asserted on a string only two hosts printed.
  EXPECT_EQ(std::string(basalt::kTestQuitFileVar), std::string("BASALT_TEST_QUIT_FILE"));
  EXPECT_EQ(std::string(basalt::kTestQuitFileMessage),
            std::string("BASALT_TEST_QUIT_FILE appeared; quitting"));
  // Responsive enough not to show next to process teardown, and not a busy loop.
  EXPECT(basalt::kTestQuitFilePollMs > 0 && basalt::kTestQuitFilePollMs <= 500);
}
