// Quitting when there is nothing left to wait for, rather than on a guess.
//
// `BASALT_QUIT_AFTER_MS` is a budget chosen before the process starts, and it
// has to be long enough for the slowest machine that will ever run it. So every
// scenario waits out the worst case on every machine: `Image.getSize` asks for
// three seconds and takes 3.7 of them, of which about 0.7 is real work.
//
// Two problems, and the smaller one is the time. The larger is that every such
// budget is a latent flake -- "should be long enough" is a guess that a loaded
// CI machine eventually falsifies, and when it does the scenario fails for a
// reason that has nothing to do with what it tests. One of those is already in
// docs/backlog/testing.md.
//
// `BASALT_QUIT_WHEN_SETTLED=<ms>` replaces the guess with a fact. The host quits
// <ms> after it has settled, where settled means:
//
//   - the first mount has been applied -- React produced a tree and the host
//     put it on screen. Recorded in MountingWalk::applyMutations, which is the
//     one place all three hosts mount through, so this is one code site rather
//     than three.
//   - any scripted input has been delivered. Each host schedules its own and
//     knows the total, and says so through noteScriptedInputEndsInMs.
//
// `BASALT_QUIT_AFTER_MS` stays as the backstop, so nothing gets *slower* and a
// host that never settles still ends. The two are complementary: one is the
// ceiling, this is the actual duration.
//
// The <ms> is a settle rather than a deadline, and it is small -- a few hundred
// milliseconds for React's next commit and the frame that draws it. It is still
// a guess, but it is a guess about one round trip instead of about a whole
// scenario, and quitting too early fails loudly: every scenario that opts in
// asserts on the dumped tree, so a tree that is not there yet is a failure and
// not a false pass. That direction is the whole reason this is safe to do.

#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <optional>
#include <string>

namespace basalt {

inline constexpr const char *kQuitWhenSettledVar = "BASALT_QUIT_WHEN_SETTLED";

// One line, shared, for the reason core/TestQuitFile.h gives: three hosts
// spelling it themselves is three chances to differ, and a scenario that asserts
// on a string only two of them print fails on the third for no reason.
inline constexpr const char *kQuitWhenSettledMessage = "BASALT_QUIT_WHEN_SETTLED: settled; quitting";

inline long long testNowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// Zero means "not yet". A steady clock could in principle read zero at process
// start, which would make the first mount look like it had never happened, so
// the stored value is never allowed to be.
inline std::atomic<long long> &firstMountAtMsRef() {
  static std::atomic<long long> value{0};
  return value;
}

// When the scripted input finishes, as an absolute time on the same clock. Zero
// when there is none, which is the common case.
inline std::atomic<long long> &scriptedInputEndsAtMsRef() {
  static std::atomic<long long> value{0};
  return value;
}

// Called from MountingWalk::applyMutations, on every transaction; only the first
// is kept. Cheap enough to sit on that path unconditionally -- one relaxed load
// and, once, one compare-exchange.
inline void noteMountApplied() {
  if (firstMountAtMsRef().load(std::memory_order_relaxed) != 0) {
    return;
  }
  long long expected = 0;
  const long long now = testNowMs();
  firstMountAtMsRef().compare_exchange_strong(expected, now == 0 ? 1 : now);
}

// Called by a host once, after it has scheduled whatever BASALT_TEST_TAP and its
// siblings asked for. `delayMs` is the total schedule, which is what each host
// already computes to lay the timers out.
inline void noteScriptedInputEndsInMs(long long delayMs) {
  scriptedInputEndsAtMsRef().store(testNowMs() + (delayMs > 0 ? delayMs : 0));
}

// Whether anything was asked to be delivered *into* the app. The hosts lay their
// scripted input out starting 1500ms in -- a tap needs a window to land on --
// and they report that total whether or not any of it was asked for, so without
// this a scenario that only mounts would wait out a schedule that is empty. It
// measured 2.96s against a 3s budget, which is to say no saving at all on
// exactly the scenarios this should help most.
//
// **A new instrument that a host puts on its scripted timer belongs in this
// list.** Left out, its scenario settles before the input lands -- which fails
// rather than passes, because everything that opts in asserts on the mounted
// tree, but it fails for a reason a long way from the change.
//
// Scheduled, not merely test-related. `BASALT_TEST_MENU` and
// `BASALT_TEST_DIALOG` are deliberately absent: they *answer* something when it
// appears, from core/TestDialog.cpp, and are never laid out on a host's timer --
// so they say nothing about when a schedule ends. Every scenario that uses them
// also drives the thing that shows the menu or the dialog, and that instrument
// is in this list. Including them was the first version, and the check below
// caught it: a list of "instruments to do with input" is not the same as a list
// of "instruments that delay the schedule", and only the second is what
// scriptedInputEndsAtMs needs.
//
// scripts/test_harness.py holds this against what the three host mains actually
// read, in both directions -- an instrument they schedule and this omits, and an
// entry here that no host knows about, which is what a rename leaves behind.
// native/tests/test_settle.cpp checks the contents.
inline const char *const *scriptedInputVars(std::size_t &count) {
  static const char *const vars[] = {
      "BASALT_TEST_TAP",
      "BASALT_TEST_SECONDARY_TAP",
      "BASALT_TEST_CLICK",
      "BASALT_TEST_HOVER",
      "BASALT_TEST_SCROLL",
      "BASALT_TEST_FOCUS",
      "BASALT_TEST_KEY",
      "BASALT_TEST_TYPE",
      "BASALT_TEST_DRAG",
      "BASALT_TEST_DROP",
      "BASALT_TEST_QUIT",
      "BASALT_TEST_CLOSE_WINDOW",
  };
  count = sizeof(vars) / sizeof(vars[0]);
  return vars;
}

inline bool anyScriptedInputRequested() {
  static const bool any = []() {
    std::size_t count = 0;
    const char *const *vars = scriptedInputVars(count);
    for (std::size_t i = 0; i < count; i++) {
      const char *value = std::getenv(vars[i]);
      if (value != nullptr && *value != '\0') {
        return true;
      }
    }
    return false;
  }();
  return any;
}

inline std::optional<unsigned> quitWhenSettledMs() {
  static const std::optional<unsigned> ms = []() -> std::optional<unsigned> {
    const char *value = std::getenv(kQuitWhenSettledVar);
    if (value == nullptr || *value == '\0') {
      return std::nullopt;
    }
    char *end = nullptr;
    const unsigned long parsed = std::strtoul(value, &end, 10);
    if (end == value) {
      return std::nullopt;
    }
    return static_cast<unsigned>(parsed);
  }();
  return ms;
}

// Whether everything worth waiting for has happened, and the settle has passed.
inline bool hasSettled() {
  const std::optional<unsigned> settle = quitWhenSettledMs();
  if (!settle.has_value()) {
    return false;
  }
  const long long mounted = firstMountAtMsRef().load();
  if (mounted == 0) {
    return false;
  }
  // The later of the two, because a scenario that taps needs the tap delivered
  // and a scenario that does not still needs its tree. Taking the maximum makes
  // the two cases one expression rather than a branch that can be got wrong.
  //
  // Only when input was actually asked for. The hosts report their schedule
  // total unconditionally, and that total is never zero -- it starts at 1500ms
  // so a tap has a window to land on -- so an empty schedule would otherwise be
  // waited out by every scenario that just mounts.
  const long long scripted = anyScriptedInputRequested() ? scriptedInputEndsAtMsRef().load() : 0;
  const long long ready = mounted > scripted ? mounted : scripted;
  return testNowMs() - ready >= static_cast<long long>(*settle);
}

} // namespace basalt
