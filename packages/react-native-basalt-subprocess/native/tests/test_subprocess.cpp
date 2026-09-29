// Running a command, on whichever desktop this suite was built for.
//
// The point of these being here rather than in an app: they run in all three
// host suites, so the Windows implementation is exercised by this project's
// Windows CI. Nothing in an app's repository would ever have built that file.
//
// They spawn real commands, which means they are the same tests everywhere only
// because each asserts on a shell's *behaviour* rather than on a shell. `echo`
// and `exit` exist in both sh and cmd.exe; anything that did not would be two
// tests pretending to be one.

#include "TestHarness.h"

#include "Subprocess.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

std::mutex gLock;
std::vector<std::string> gOutput;
std::atomic<int> gExited{-1};
std::atomic<int> gExitedPid{0};

void listen() {
  basalt::setSubprocessListeners(
      [](int pid, const std::string &data, bool isStandardError) {
        std::lock_guard<std::mutex> guard(gLock);
        gOutput.push_back(std::string(isStandardError ? "err:" : "out:") + data);
        (void)pid;
      },
      [](int pid, int code) {
        gExitedPid.store(pid);
        gExited.store(code);
      });
}

void reset() {
  std::lock_guard<std::mutex> guard(gLock);
  gOutput.clear();
  gExited.store(-1);
  gExitedPid.store(0);
}

// Up to five seconds, which is a long time for `echo` and short enough that a
// hung test fails rather than hangs the suite.
bool waitForExit() {
  for (int i = 0; i < 250 && gExited.load() < 0; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return gExited.load() >= 0;
}

bool sawOutput(const std::string &needle) {
  std::lock_guard<std::mutex> guard(gLock);
  for (const std::string &line : gOutput) {
    if (line.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

bool sawOn(const std::string &stream, const std::string &needle) {
  std::lock_guard<std::mutex> guard(gLock);
  for (const std::string &line : gOutput) {
    if (line.rfind(stream + ":", 0) == 0 && line.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

} // namespace

TEST(a_command_runs_and_its_output_arrives) {
  listen();
  reset();

  std::string error;
  basalt::SpawnRequest request;
  request.command = "echo hello";
  const int pid = basalt::spawnSubprocess(request, error);

  EXPECT(pid > 0);
  EXPECT(error.empty());
  EXPECT(waitForExit());
  EXPECT_EQ(gExited.load(), 0);
  EXPECT_EQ(gExitedPid.load(), pid);
  EXPECT(sawOn("out", "hello"));
}

TEST(a_commands_exit_code_is_the_one_it_chose) {
  listen();
  reset();

  std::string error;
  basalt::SpawnRequest request;
  request.command = "exit 3";
  const int pid = basalt::spawnSubprocess(request, error);

  EXPECT(pid > 0);
  EXPECT(waitForExit());
  EXPECT_EQ(gExited.load(), 3);
}

TEST(standard_error_arrives_marked_as_standard_error) {
  listen();
  reset();

  std::string error;
  basalt::SpawnRequest request;
  // Redirection is spelled the same in both shells, which is why this is one
  // test rather than two.
  request.command = "echo trouble 1>&2";
  EXPECT(basalt::spawnSubprocess(request, error) > 0);
  EXPECT(waitForExit());

  EXPECT(sawOn("err", "trouble"));
  EXPECT(!sawOn("out", "trouble"));
}

TEST(every_byte_written_arrives_before_the_exit_that_follows_it) {
  // The ordering the one-thread-per-child shape exists for. A reader that let
  // the exit overtake the output would put "exited" above the reason for it.
  listen();
  reset();

  std::string error;
  basalt::SpawnRequest request;
  request.command = "echo first";
  EXPECT(basalt::spawnSubprocess(request, error) > 0);
  EXPECT(waitForExit());

  // The exit was reported after the listener had already been given the output:
  // gOutput is filled before gExited is set, so seeing the exit means the output
  // is already there.
  EXPECT(sawOutput("first"));
}

TEST(a_variable_reaches_the_command_without_being_in_it) {
  // The reason SpawnRequest has an `env` at all: `FOO=bar cmd` is POSIX shell
  // syntax cmd.exe does not have, so a command carrying its own variables only
  // runs on two of the three desktops.
  listen();
  reset();

  std::string error;
  basalt::SpawnRequest request;
#ifdef _WIN32
  request.command = "echo %BASALT_TEST_VARIABLE%";
#else
  request.command = "echo $BASALT_TEST_VARIABLE";
#endif
  request.env.emplace_back("BASALT_TEST_VARIABLE", "arrived");
  EXPECT(basalt::spawnSubprocess(request, error) > 0);
  EXPECT(waitForExit());

  EXPECT(sawOutput("arrived"));
}

TEST(a_command_that_both_begins_and_ends_with_a_quote_reaches_the_program) {
  // The shape that broke the first real command this ran: an absolute program
  // path in quotes, and a final argument in quotes too. cmd.exe removes the
  // first and last quote of the line it is handed, so without the outer pair
  // this host adds, those two disappear and every quote after them pairs with
  // the wrong partner -- the command dies in the shell, before the program.
  listen();
  reset();

  std::string error;
  basalt::SpawnRequest request;
#ifdef _WIN32
  request.command = "\"cmd.exe\" /d /s /c echo \"quoted tail\"";
#else
  request.command = "\"/bin/echo\" \"quoted tail\"";
#endif
  EXPECT(basalt::spawnSubprocess(request, error) > 0);
  EXPECT(waitForExit());

  EXPECT(sawOutput("quoted tail"));
  EXPECT_EQ(gExited.load(), 0);
}

TEST(a_command_runs_where_it_was_told_to) {
  listen();
  reset();

  std::string error;
  basalt::SpawnRequest request;
#ifdef _WIN32
  request.command = "cd";
  request.cwd = "C:\\";
#else
  request.command = "pwd";
  request.cwd = "/tmp";
#endif
  EXPECT(basalt::spawnSubprocess(request, error) > 0);
  EXPECT(waitForExit());

#ifdef _WIN32
  EXPECT(sawOutput("C:\\"));
#else
  // macOS answers /private/tmp for /tmp, which is the same directory.
  EXPECT(sawOutput("tmp"));
#endif
}

TEST(a_command_that_cannot_be_run_reports_through_the_exit) {
  // Not through spawn: the shell started, which is what spawn answers for. What
  // failed is the command, and a shell reports that by exiting non-zero.
  listen();
  reset();

  std::string error;
  basalt::SpawnRequest request;
  request.command = "this-command-does-not-exist-anywhere";
  EXPECT(basalt::spawnSubprocess(request, error) > 0);
  EXPECT(waitForExit());
  EXPECT(gExited.load() != 0);
}

TEST(a_long_running_command_can_be_stopped) {
  listen();
  reset();

  std::string error;
  basalt::SpawnRequest request;
#ifdef _WIN32
  // ping to localhost is the usual way to sleep in cmd.exe without timeout,
  // which refuses to run when input is redirected.
  request.command = "ping -n 30 127.0.0.1 > nul";
#else
  request.command = "sleep 30";
#endif
  const int pid = basalt::spawnSubprocess(request, error);

  EXPECT(pid > 0);
  EXPECT(basalt::isSubprocessRunning(pid));
  EXPECT(basalt::killSubprocess(pid));
  EXPECT(waitForExit());
  EXPECT(!basalt::isSubprocessRunning(pid));
}

TEST(a_process_this_one_did_not_start_is_not_ours_to_answer_about) {
  listen();
  EXPECT(!basalt::isSubprocessRunning(999999));
  EXPECT(!basalt::killSubprocess(999999));
}
