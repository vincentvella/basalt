// Quitting, gracefully, at a moment the harness chooses.
//
// `BASALT_QUIT_AFTER_MS` is the other way out, and it is decided before the
// process starts. That is fine for a scenario whose length is known and wrong
// for one whose length is not: the Fast Refresh scenario waits for an edit to
// reach the window, which takes a second on a warm laptop and up to two and a
// half minutes on a cold CI machine, so no single budget is both quick and
// safe. It used a signal instead -- and signals are not portable. Windows has
// no SIGTERM, `TerminateProcess` cannot be handled, and every host writes its
// widget tree on the way out, so a killed host on Windows failed the assertion
// that reads that tree whether or not the thing under test had worked.
//
// So: `BASALT_TEST_QUIT_FILE=<path>`, and the host quits as soon as <path>
// exists, through exactly the shutdown `BASALT_QUIT_AFTER_MS` uses. The harness
// creates the file when it has what it came for. Nothing is read from the file
// and it may be empty; its existence is the whole message, which is why there
// is no race worth worrying about between writing it and seeing it.
//
// Not refusable, for the reason the timed quit is not: this is the harness
// ending the run rather than a person closing a window, and an app that
// intercepts its own close would otherwise refuse the harness and hang. The
// scenario for *that* feature is exactly such an app.
//
// Polled rather than watched. A file watcher is three different APIs across
// these hosts and each has its own failure mode on network and virtual
// filesystems; a quarter-second poll on a timer each host already runs costs
// nothing measurable and cannot fail to be delivered.

#pragma once

#include <optional>
#include <string>

namespace basalt {

// One spelling, in one place. Three hosts reading the variable by name is three
// chances to typo it, and a typo here does not fail: the host simply never
// quits, and the scenario reports a timeout somewhere unrelated.
inline constexpr const char *kTestQuitFileVar = "BASALT_TEST_QUIT_FILE";

// How often to look. Small enough that the wait is not noticeable next to the
// process teardown that follows it, large enough to be free.
inline constexpr unsigned kTestQuitFilePollMs = 250;

// The line all three hosts log when it fires, so that three transcripts read
// the same way. `BASALT_QUIT_AFTER_MS elapsed; quitting` had to be retrofitted
// onto Windows after a scenario asserted on a string only two hosts printed;
// this starts out shared instead.
inline constexpr const char *kTestQuitFileMessage = "BASALT_TEST_QUIT_FILE appeared; quitting";

// The path the instrument was pointed at, or nothing when it is off. Read once:
// the environment does not change under a running process, and each host calls
// this on a timer.
std::optional<std::string> testQuitFilePath();

// Whether that file exists now. False when the instrument is off, so a host can
// call it without checking first.
bool testQuitFileAppeared();

} // namespace basalt
