#!/usr/bin/env python3
"""Checks on the end-to-end harness itself, for the failures that point at green.

A bug in a test harness is not like a bug in the code under test. If it makes a
wait return early or a count match something it should not, every scenario using
it passes while checking less than its name says, and nothing in the output
looks wrong. That is the class of bug here.

Both checks below are regressions:

- `wait_for_log` counted from the start of the file, and the Fast Refresh
  scenario used it to wait for Metro to serve the running app a bundle. But
  `Metro.prewarm` is itself a request and Metro logs two BUNDLE lines for it, so
  the count was already satisfied before the host was started. The wait returned
  in about a hundredth of a second, on every platform, for as long as it had
  existed. What hid it is that the edit assertion after it carried the scenario
  on the machines where Metro's file watching works.

- `stop_host` replaced a bare `terminate()`. GTK and AppKit handle SIGTERM and
  exit 0; Windows has no SIGTERM, so `terminate()` is `TerminateProcess` and the
  exit code is always 1. Asserting on it reported our own kill as the app
  crashing, the first time that scenario ran on Windows.

Usage:  scripts/test_harness.py
"""

import importlib.util
import pathlib
import re
import subprocess
import sys
import tempfile

HARNESS = pathlib.Path(__file__).with_name("integration_test.py")


def load():
    spec = importlib.util.spec_from_file_location("integration_test", HARNESS)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> int:
    harness = load()
    failures = []

    def check(label, got, want):
        if got != want:
            failures.append(f"{label}: got {got!r}, wanted {want!r}")

    with tempfile.TemporaryDirectory() as directory:
        log = pathlib.Path(directory) / "metro.log"

        # What prewarm leaves behind: Metro logs a progress line and a final one.
        log.write_text(" BUNDLE  e2e/index.js 0% (0/1)\n BUNDLE  e2e/index.js\n")
        served = log.stat().st_size

        # The bug, kept as a check so the distinction cannot quietly go away: read
        # from the start and the needle is already there.
        check("counting from the start still sees prewarm",
              harness.wait_for_log(log, "BUNDLE", 1, timeout=1), True)

        # The fix. Nothing has happened since `served`, so this must not match.
        check("an offset ignores what prewarm wrote",
              harness.wait_for_log(log, "BUNDLE", 1, timeout=1, start=served), False)

        # And it must still see what happens after it.
        log.write_text(log.read_text() + " BUNDLE  e2e/index.js\n")
        check("an offset sees the host's own request",
              harness.wait_for_log(log, "BUNDLE", 1, timeout=1, start=served), True)

        # Counts past the offset, not in total: two more lines, asked for two.
        log.write_text(log.read_text() + " BUNDLE  e2e/index.js\n")
        check("counts are relative to the offset",
              harness.wait_for_log(log, "BUNDLE", 2, timeout=1, start=served), True)
        check("and do not borrow from before it",
              harness.wait_for_log(log, "BUNDLE", 3, timeout=1, start=served), False)

        # A log that does not exist yet is a wait, not a crash: the host writes
        # it, and the harness starts watching before the host has opened it.
        missing = pathlib.Path(directory) / "not-yet.log"
        check("a missing log times out rather than raising",
              harness.wait_for_log(missing, "anything", 1, timeout=1), False)

        # An offset past the end of the file reads nothing rather than raising.
        check("an offset past the end reads nothing",
              harness.wait_for_log(log, "BUNDLE", 1, timeout=1,
                                   start=log.stat().st_size + 4096), False)

        # Undecodable bytes: Metro's progress lines carry control characters, and
        # a host can die mid-write. The wait must survive its own input.
        raw = pathlib.Path(directory) / "raw.log"
        raw.write_bytes(b"\xff\xfe BUNDLE  e2e/index.js\n")
        check("invalid utf-8 does not stop the search",
              harness.wait_for_log(raw, "BUNDLE", 1, timeout=1), True)

    # stop_host against a real process that ignores nothing: it must end it, and
    # must say whether the exit code it leaves behind is the app's or the kill's.
    sleeper = subprocess.Popen(
        [sys.executable, "-c", "import time; time.sleep(120)"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    meaningful = harness.stop_host(sleeper)
    check("stop_host ends the process", sleeper.poll() is not None, True)
    check("stop_host trusts the exit code everywhere but Windows",
          meaningful, harness.PLATFORM != "windows")
    # The claim the Windows branch rests on, asserted rather than assumed: a
    # terminate() there is a kill with its own exit code, and on POSIX it is a
    # signal the process can act on. If a future Python changed either, this is
    # where it would be noticed rather than in a scenario.
    if harness.PLATFORM == "windows":
        check("a killed process on Windows exits nonzero", sleeper.returncode != 0, True)
    else:
        check("a signalled process on POSIX reports the signal",
              sleeper.returncode < 0, True)

    # The instrument list in core/TestSettle.h against the instruments the hosts
    # actually read. BASALT_QUIT_WHEN_SETTLED decides a scenario is finished once
    # the scripted input has been delivered, and it works out *whether any was
    # asked for* from that list -- so an input instrument missing from it settles
    # its scenario before the input lands. That fails rather than passes, because
    # everything opted in asserts on the mounted tree, but it fails a long way
    # from the instrument that was added, which is why this is checked here and
    # not left to a comment.
    #
    # Done in Python because the question is about three C++ source files, which
    # a C++ unit test cannot read portably. test_settle.cpp checks the list's
    # contents; this checks it is still the right list.
    repo = pathlib.Path(__file__).resolve().parent.parent
    header = (repo / "packages/basalt-core/native/core/TestSettle.h").read_text()
    listed = set(re.findall(r'"(BASALT_TEST_[A-Z_]+)"', header))

    hosts = [
        "packages/basalt-gtk/native/gtk/main_gtk.cpp",
        "packages/basalt-appkit/native/appkit/main_appkit.mm",
        "packages/basalt-win32/native/win32/main_win32.cpp",
    ]
    read_by_hosts = set()
    for host in hosts:
        read_by_hosts |= set(re.findall(r'"(BASALT_TEST_[A-Z_]+)"', (repo / host).read_text()))
    # And core, because an instrument's name does not have to live in a host to
    # be scheduled by one. BASALT_TEST_KEY is spelt once in core/TestKeys.h and
    # the hosts call scriptedKeyPresses(), which is the arrangement this file
    # keeps asking for everywhere else -- and which this check did not allow for
    # until it flagged it.
    for source in sorted((repo / "packages/basalt-core/native/core").glob("*.h")):
        if source.name == "TestSettle.h":
            continue  # the list itself
        read_by_hosts |= set(re.findall(r'"(BASALT_TEST_[A-Z_]+)"', source.read_text()))

    # Not scripted input, and deliberately not in the list. Each is here with a
    # reason rather than silently skipped.
    NOT_INPUT = {
        # Ends the run; it does not deliver anything into the app.
        "BASALT_TEST_QUIT_FILE",
        # A modifier on BASALT_TEST_TYPE's timing, not an instrument of its own.
        "BASALT_TEST_TYPE_AFTER_MS",
        # A setting rather than an event: it stands in for a desktop's text
        # scale, which two of the three platforms do not publish, and is read
        # once by core/FontScaling.h. Nothing is scheduled for it, so nothing
        # waits on it.
        "BASALT_TEST_FONT_SCALE",
    }

    unlisted = read_by_hosts - listed - NOT_INPUT
    check("every input instrument the hosts read is in scriptedInputVars",
          sorted(unlisted), [])

    # And nothing in the list that no host reads, which would mean a rename left
    # a dead entry behind and the real name unguarded.
    stale = listed - read_by_hosts
    check("no instrument in scriptedInputVars is unknown to every host",
          sorted(stale), [])

    # How long the host lives when the clicks come from outside it. "scroll away
    # and back" is two clicks inside a six second timer, which left 0.2 seconds
    # between the second click and the host exiting: it failed on CI's Linux
    # shard three runs running as soon as a 0.3 second settle was added, and was
    # a flake waiting for a slower runner before that.
    budget = harness.real_input_budget
    # The clicks themselves: four seconds for the window, then 0.4 + 1.0 each.
    for clicks in (1, 2, 3):
        spent = 4000 + clicks * 1400
        check(f"{clicks} clicks fit, with room to spare",
              budget(clicks, False, 6000) - spent >= 1500, True)
    check("typing is paid for too",
          budget(1, True, 6000) > budget(1, False, 6000), True)
    # And a scenario that asked for longer keeps it.
    check("a longer run is never shortened", budget(2, False, 30000), 30000)

    # Which window the Linux clicks are aimed at, which is a regression and is
    # here for the reason the other two are. The first version took the first
    # window whose `_NET_WM_PID` equalled the host's, and a window that reports
    # no pid -- the normal case under Xvfb, where nothing sets it -- was skipped
    # in favour of one further down the list. "scroll away and back" failed on
    # CI's Linux shard twice: the clicks were measured against a window that was
    # not the toplevel they were aimed at.
    pick = harness.pick_window_of
    owners = {"a": None, "b": 42, "c": 7}
    # The case the strict rule got wrong, and the only one that tells the two
    # rules apart: a window reporting no pid comes first and one reporting *our*
    # pid comes later. Taking the later one is what aimed the clicks at the
    # wrong window. Checked first because a weaker case passes either way.
    check("a window that reports no pid is ours, even when a later one matches",
          pick(["a", "c"], 7, owners.get), "a")
    check("a window that reports no pid is still ours",
          pick(["a", "b"], 7, owners.get), "a")
    check("a window owned by another process is skipped",
          pick(["b", "c"], 7, owners.get), "c")
    check("ours is taken in search order when it reports a pid",
          pick(["c", "b"], 7, owners.get), "c")
    # Every candidate disqualified is still a click rather than a scenario that
    # cannot run: a wrong click reports better than a Failure nobody can read.
    check("all of them somebody else's falls back to the first",
          pick(["b"], 7, owners.get), "b")

    if failures:
        print("harness checks failed:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    print("all harness checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
