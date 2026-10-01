// When a host has nothing left to wait for.
//
// The decision, not the quitting: three hosts shut down three ways and each owns
// that. What is shared -- and so what can be got wrong once and be wrong
// everywhere -- is when they are *allowed* to.
//
// The interesting case is the one that was wrong first. The hosts report their
// scripted-input schedule whether or not any was asked for, and that total is
// never zero because a tap needs a window to land on, so a scenario that only
// mounts was waiting out an empty schedule: 2.96 seconds against a 3 second
// budget, which is no saving at all on exactly the scenarios this exists for.

#include "TestHarness.h"

#include "TestSettle.h"

#include <cstdlib>
#include <sstream>
#include <string>

namespace {

void setVariable(const char *name, const char *value) {
#ifdef _WIN32
  _putenv_s(name, value);
#else
  setenv(name, value, 1);
#endif
}

} // namespace

// First, because both `quitWhenSettledMs` and `anyScriptedInputRequested` cache
// on first read -- the environment does not change under a running process, and
// hosts ask on a timer. The ordering below is deliberate, not incidental.
//
// Zero settle so nothing here has to sleep: the question is whether the
// conditions are met, and the delay after them is arithmetic.
TEST(settle_reads_the_variable) {
  setVariable(basalt::kQuitWhenSettledVar, "0");
  EXPECT(basalt::quitWhenSettledMs().has_value());
  EXPECT_EQ((int)*basalt::quitWhenSettledMs(), 0);
}

TEST(settle_waits_for_the_first_mount) {
  // Nothing has mounted yet in this process -- no host here -- so however long
  // has passed, there is nothing to have settled.
  EXPECT(!basalt::hasSettled());

  basalt::noteMountApplied();
  EXPECT(basalt::hasSettled());
}

TEST(settle_keeps_the_first_mount_not_the_last) {
  const long long first = basalt::firstMountAtMsRef().load();
  EXPECT(first != 0);

  // A reload mounts again. Taking the later one would push the settle out on
  // every commit, so an app that re-renders steadily would never settle at all.
  basalt::noteMountApplied();
  EXPECT_EQ((long long)basalt::firstMountAtMsRef().load(), first);
}

TEST(settle_ignores_a_schedule_nobody_asked_for) {
  // No input instrument is set in this process, so the schedule the hosts report
  // is the bare 1500ms base and means nothing. Reporting an hour must not push
  // the settle out by an hour.
  basalt::noteScriptedInputEndsInMs(3600000);
  EXPECT(!basalt::anyScriptedInputRequested());
  EXPECT(basalt::hasSettled());
}

TEST(settle_lists_the_instruments_that_delay_it) {
  // The list is what decides whether a reported schedule is real. An input
  // instrument missing from it settles its scenario before the input lands --
  // which fails rather than passes, because everything that opts in asserts on
  // the mounted tree, but it fails a long way from the change that caused it.
  std::size_t count = 0;
  const char *const *vars = basalt::scriptedInputVars(count);
  EXPECT(count >= 11);

  // Every instrument a host lays out on its scripted timer. Not
  // BASALT_TEST_MENU or BASALT_TEST_DIALOG: those answer something when it
  // appears rather than being scheduled, so they do not move the end of the
  // schedule. scripts/test_harness.py holds the list against the hosts.
  for (const char *needed : {"BASALT_TEST_TAP",
                             "BASALT_TEST_SECONDARY_TAP",
                             "BASALT_TEST_CLICK",
                             "BASALT_TEST_HOVER",
                             "BASALT_TEST_SCROLL",
                             "BASALT_TEST_FOCUS",
                             "BASALT_TEST_TYPE",
                             "BASALT_TEST_DRAG",
                             "BASALT_TEST_DROP",
                             "BASALT_TEST_QUIT",
                             "BASALT_TEST_CLOSE_WINDOW"}) {
    bool found = false;
    for (std::size_t i = 0; i < count; i++) {
      if (std::string(vars[i]) == needed) {
        found = true;
        break;
      }
    }
    EXPECT_EQ(std::string(found ? needed : "missing from scriptedInputVars"), std::string(needed));
  }
}

TEST(settle_message_is_shared) {
  // For the reason core/TestQuitFile.h gives: BASALT_QUIT_AFTER_MS's line had to
  // be retrofitted onto Windows after a scenario asserted on a string only two
  // hosts printed.
  EXPECT_EQ(std::string(basalt::kQuitWhenSettledVar), std::string("BASALT_QUIT_WHEN_SETTLED"));
  EXPECT_EQ(std::string(basalt::kQuitWhenSettledMessage),
            std::string("BASALT_QUIT_WHEN_SETTLED: settled; quitting"));
}
