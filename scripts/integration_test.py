#!/usr/bin/env python3
"""End-to-end tests for the host.

The unit suite (build/basalt_gtk_tests) covers everything that can be reached without a
JavaScript runtime. What it cannot cover is the path this project actually
exists to provide: JavaScript, React, Fabric, the mounting manager, and GTK
widgets, all in one process. Until there was a way to read the resulting widget
tree, the only way to check that path was to look at a screenshot.

BASALT_DUMP_TREE makes it assertable. Each scenario below runs the real host
against the real bundle, taps something, and asserts on the tree it wrote on
the way out.

Taps arrive one of two ways:

  real       xdotool moves the pointer and clicks, so the event goes through
             the X server and GDK exactly as a person's click would. This is
             the only mode that exercises event delivery itself.
  injected   BASALT_TEST_TAP enters at the touch dispatcher, skipping the
             window system. The fallback where a real event cannot be
             synthesised: macOS needs accessibility permission an automated run
             does not have, and Windows needs SendInput, which moves the real
             cursor and so cannot run beside anything else on the machine.

The default picks real input when a display and xdotool are both present.

One scenario is different: the Fast Refresh one starts its own Metro, runs the
host in dev mode against it, and edits the demo while it is on screen. It needs
a React Native checkout -- scripts/metro.js looks for one beside the repo, and
RN_DIR overrides that -- and it restores the file it edits once the host has
exited.

Usage:  scripts/integration_test.py [--bundle build/main.jsbundle.js]
                                    [--input auto|real|injected]
                                    [--platform auto|linux|macos|windows]
                                    [--build-dir build]

Runs whichever host is built. The scenarios are the same on all three, because
they are about React and Fabric rather than about a toolkit; what differs is
which binary is launched and how a tap is delivered.

Needs a display, like any GTK program. On a headless Linux box:

    Xvfb :99 -screen 0 1400x1000x24 &
    DISPLAY=:99 scripts/integration_test.py

On Windows and macOS nothing has to be arranged -- the host makes its own
window -- and input is injected, because neither can synthesise a real click
without taking over the machine's cursor.
"""

import argparse
import bisect
import os
import re
import signal
import shutil
import subprocess
import sys
import socket
import tempfile
import shutil
import time
import urllib.error
import urllib.request
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

# Filled in by javascript_built_at() on first use.
_JAVASCRIPT_BUILT_AT: float | None = None
# Which host, and therefore which binary and which bundle. Rebound by
# --platform and --build-dir: testing against a second React Native version
# means a second build tree, and the suite has to run the host from it.
#
# One suite for three platforms rather than three, because the scenarios are
# about React and Fabric rather than about a toolkit -- the tree a tap produces
# is the same tree on all three, which is the claim scripts/compare_hosts.sh
# makes and this one relies on.
HOSTS = {
    "linux": "basalt_gtk",
    "macos": "basalt_appkit",
    "windows": "basalt_win32.exe",
}
PLATFORM = "linux"
HOST = REPO / "build" / "basalt_gtk"
MODULE = "BasaltDemo"

# Where the demo's buttons are is asked of the running app rather than worked
# out from e2e/index.js. See `tap_point` below for why.

# e2e/hover.js, which the hover scenario runs instead of the demo: a card at
# y=24..244 holding two 140pt boxes, at x=44..184 and x=204..344. The third
# point is inside the card and outside both boxes, which is the one that
# distinguishes enter from over.
HOVER_LEFT_BOX = (100, 130)
HOVER_RIGHT_BOX = (270, 130)
HOVER_CARD_ONLY = (700, 130)

# The string the Fast Refresh scenario swaps in the demo's heading, and puts
# back. Chosen to be unmistakable in a widget tree and unique in the file.
BEFORE = "React Native on the desktop"
AFTER = "Fast Refresh reached the window"


# What the host last printed. Kept here rather than threaded through every
# caller of run_host, which only ever wants the tree.
LAST_HOST_OUTPUT = ""


def _remember_output(text: str) -> None:
    global LAST_HOST_OUTPUT
    LAST_HOST_OUTPUT = text or ""


class Failure(Exception):
    pass


class Skipped(Exception):
    """A scenario that cannot run here, and says why rather than passing."""


# The demo's <TextInput>, from e2e/index.js: frame (24,183 320x44). Clicked
# rather than tapped, so this is a point inside it in surface coordinates.
FIELD_POINT = (120, 205)

# styles.fieldFocused sets borderColor to PALETTE[0], and describeTree prints
# per-edge colours -- so focus is visible in the tree without the demo needing
# to render anything new for the test's benefit.
FOCUSED_BORDER = "borderc=(#4285f4ff,#4285f4ff,#4285f4ff,#4285f4ff)"

INPUT_MODE = "injected"


def screen_is_locked() -> bool:
    """Whether the login session's screen is locked, on macOS.

    A locked session has no *realised* windows: the window server has nothing on
    screen, so a process's accessibility window list is empty and `CADisplayLink`
    does not fire. Two scenarios depend on one or the other, and both failed with
    messages that pointed somewhere else entirely -- "could not find the host
    window after 20 seconds" and "no scroll offsets were reported at all" -- for
    no better reason than a developer's Mac locking itself during a long run.

    `CGSSessionScreenIsLocked` in the console session's dictionary is the answer,
    and `ioreg` prints it without a framework binding. False on every other
    platform, and false when the question cannot be answered: a skip that fires
    when it should not would hide a real failure.
    """
    if PLATFORM != "macos":
        return False
    try:
        session = subprocess.run(
            ["ioreg", "-n", "Root", "-d1"], capture_output=True, text=True, timeout=20
        )
    except (OSError, subprocess.SubprocessError):
        return False
    for line in session.stdout.splitlines():
        if "CGSSessionScreenIsLocked" in line:
            return '"CGSSessionScreenIsLocked"=Yes' in line.replace(" ", "")
    return False


def real_input_available() -> bool:
    return bool(os.environ.get("DISPLAY")) and shutil.which("xdotool") is not None


def crash_or_tail(stderr: str, budget: int = 2600) -> str:
    """The crash, if there was one, and otherwise the end of the output.

    The last 2000 characters was the whole story until a host died and the
    report said this:

        host exited -11
        ntingWalkINS_21AppKitMountingManagerE...destroyEi + 320

    Frames 0 to 4 and the handler's own marker line -- the signal, the faulting
    address and which thread it was -- had been cut off the front, which is the
    half that says *what* was dereferenced. core/CrashHandler.cpp prints that
    marker precisely so a CI failure answers the question, and a tail-shaped
    budget threw it away.

    So: from the marker when there is one, and from the end when there is not.
    """
    marker = stderr.rfind("*** basalt: ")
    if marker == -1:
        return stderr[-budget:]
    # A little of what came before it, which is often the log line that says
    # what the host was doing, and then as much of the stack as the budget
    # allows -- from the top, the frames nearest the fault being the ones worth
    # keeping.
    # Each clipped, because a glog line carrying a whole shadow tree would
    # otherwise push the stack back out of the budget it was just rescued from.
    preamble = [line[-200:] for line in stderr[:marker].rstrip().splitlines()[-3:]]
    head = ("\n".join(preamble) + "\n") if preamble else ""
    return head + stderr[marker:marker + budget]


def check_output(stderr: str, returncode: int, allow_js_errors: bool = False) -> None:
    if returncode != 0:
        raise Failure(f"host exited {returncode}\n{crash_or_tail(stderr)}")
    if allow_js_errors:
        # For the one scenario whose whole point is an error: e2e/logbox.js calls
        # console.error deliberately, and LogBox is what is being tested.
        return
    for line in stderr.splitlines():
        # A JS error does not fail the process, so it has to be looked for.
        if "onJsError" in line or "Invariant Violation" in line:
            raise Failure(f"javascript error: {line}")


def window_pid(window: str) -> int | None:
    """What `_NET_WM_PID` says owns a window, or None if it does not say."""
    owner = subprocess.run(
        ["xdotool", "getwindowpid", window], capture_output=True, text=True
    )
    if owner.returncode != 0 or not owner.stdout.strip().isdigit():
        return None
    return int(owner.stdout.strip())


def pick_window_of(windows, pid, owner_of=window_pid):
    """The first window that is not positively somebody else's.

    `xdotool search --name basalt-core` matches every host on the display, and a
    leftover one from an earlier scenario answers to the same name, so taking
    the first match can mean clicking a window that is not under test.

    The rule is deliberately weak, and the first version was not: it took the
    first window whose `_NET_WM_PID` *equalled* this host's, which broke "scroll
    away and back" on CI's Linux shard twice. A window that reports no pid at
    all has to count as ours, in the order the search returned it, because that
    is the normal case where nothing sets `_NET_WM_PID` -- under Xvfb with no
    window manager, among other places -- and skipping it goes looking for a
    window further down the list that is not the toplevel the clicks were aimed
    at.

    So: skip a window only when something else owns it, and fall back to the
    first match when every candidate is disqualified, because a wrong click
    reports better than a scenario that cannot run.
    """
    for window in windows:
        owner = owner_of(window)
        if owner is None or owner == pid:
            return window
    return windows[0]


def click_with_xdotool(points: list[tuple[int, int]], pid: int) -> None:
    """Clicks through the X server, so GDK delivers the event itself."""
    windows = subprocess.run(
        ["xdotool", "search", "--name", "basalt-core"],
        capture_output=True,
        text=True,
    ).stdout.split()
    if not windows:
        raise Failure("could not find the host window with xdotool")
    window = [pick_window_of(windows, pid)]

    geometry = subprocess.run(
        ["xdotool", "getwindowgeometry", "--shell", window[0]],
        capture_output=True,
        text=True,
    ).stdout
    origin = dict(
        line.split("=", 1) for line in geometry.splitlines() if "=" in line
    )
    x0, y0 = int(origin.get("X", 0)), int(origin.get("Y", 0))

    # No raise here, unlike the macOS path. That was added for symmetry and it
    # cost three red CI runs: nothing has ever reported a window over the GTK
    # host's under Xvfb, and the 0.3 seconds it slept to let the raise land came
    # out of the budget below, which had 0.2 to spare.

    for x, y in points:
        subprocess.run(["xdotool", "mousemove", str(x0 + x), str(y0 + y)], check=True)
        time.sleep(0.4)
        subprocess.run(["xdotool", "click", "1"], check=True)
        time.sleep(1.0)


def type_with_xdotool(text: str) -> None:
    """Types through the X server, so GDK and the input method see the keys."""
    subprocess.run(["xdotool", "type", "--delay", "80", text], check=True)
    time.sleep(1.5)


# --- A real click, which is the only kind that can focus a field -------------


def click_field_with_cgevent(surface_height: int, pid: int) -> None:
    """Clicks the demo's <TextInput> with a real mouse event, on macOS.

    Not `BASALT_TEST_TAP`, and not System Events' `click at`. The first enters
    at the touch dispatcher, below the window system, so it moves React
    Native's responder and never reaches the peer that takes focus. The second
    performs an accessibility *press*, which a text field does nothing with --
    and it answers with the name of the element it found, which makes it look
    like it worked. Both were tried, both reported the feature broken, and the
    feature was fine.

    The field is located through accessibility rather than by arithmetic on the
    window's origin: the title bar's height is the host's business and nothing
    here should have to know it. That also means this clicks the control the
    system believes is there, which is worth something on its own.
    """
    # Derived rather than looked up. The field is exposed to accessibility as
    # an AXGroup rather than an AXTextField -- see docs/BACKLOG.md -- so there
    # is no role to search for, and hunting the only group in the window would
    # break the first time the demo grows another.
    #
    # So: ask the window where it is and how big it is, and take the title
    # bar's height as the difference between that and the surface, which the
    # caller read out of a tree dump. Nothing here has to know what AppKit's
    # title bar measures.
    # By pid, not by name. `every process whose name contains "basalt"` also
    # matches basalt_appkit_tests and any host left over from an earlier
    # scenario, and `item 1` of that list is whichever the window server
    # answered with. Both hosts open a 900x700 window at the same default
    # position, so clicking the wrong one lands on a *different* process's
    # field and the scenario reports this feature broken. The caller launched
    # the host and knows which process it is.
    # The window is raised before it is measured, and that is not politeness.
    # CGEventPost delivers to whatever window is topmost at the screen point,
    # which need not be the one the point was computed from: an editor or a
    # Finder window over that corner of the screen swallows the click and the
    # scenario reports the feature broken. Measured, not guessed -- with
    # another application's window covering the field the scenario failed 3 of
    # 3 runs, and 0 of 5 without it.
    script = f"""
    tell application "System Events"
      set procs to (every process whose unix id is {pid})
      if (count of procs) = 0 then error "no host process for pid {pid}"
      set proc to item 1 of procs
      set frontmost of proc to true
      set w to first window of proc
      perform action "AXRaise" of w
      set {{wx, wy}} to position of w
      set {{ww, wh}} to size of w
      return (wx as text) & "," & (wy as text) & "," & (ww as text) & "," & (wh as text)
    end tell
    """
    # Polled rather than asked once. The caller has already waited five seconds
    # for the window, which is enough on a developer's machine and was enough on
    # CI until it was not: a loaded runner produced "Can't get window 1 of
    # application" from a run whose only change was elsewhere, and the same job
    # had passed on the three commits before it. A fixed sleep in front of a
    # single attempt is a race with the window server, and the fix for a race is
    # not a longer sleep.
    deadline = time.monotonic() + 20
    found = None
    # The first failure as well as the last, because they are different failures
    # and the last is the least informative: by then the host has usually exited
    # on its own timer, so every attempt ends in "no host process" whatever went
    # wrong at the start. The first attempt said "Can't get window 1 ... Invalid
    # index", which is a window that was never realised and a different problem.
    first = None
    while time.monotonic() < deadline:
        found = subprocess.run(
            ["osascript", "-e", script], capture_output=True, text=True
        )
        if found.returncode == 0 and found.stdout.count(",") == 3:
            break
        if first is None:
            first = found.stderr.strip()
        time.sleep(0.5)
    if found is None or found.returncode != 0 or found.stdout.count(",") != 3:
        raise Failure(
            "could not find the host window after 20 seconds.\n"
            f"  first attempt: {first or 'never asked'}\n"
            f"  last attempt:  {found.stderr.strip() if found is not None else 'never asked'}"
        )
    # The raise has to reach the window server before the click is posted, or
    # the click arrives while the old window is still in front.
    time.sleep(0.5)
    wx, wy, _ww, wh = (int(part) for part in found.stdout.strip().split(","))

    chrome = wh - surface_height
    x = wx + FIELD_POINT[0]
    y = wy + chrome + FIELD_POINT[1]

    # CoreGraphics through ctypes, so nothing has to be compiled to run the
    # suite. A post that goes nowhere is what a missing accessibility
    # permission looks like, which the scenario's failure names.
    import ctypes

    core = ctypes.cdll.LoadLibrary(
        "/System/Library/Frameworks/ApplicationServices.framework/ApplicationServices"
    )

    class CGPoint(ctypes.Structure):
        _fields_ = [("x", ctypes.c_double), ("y", ctypes.c_double)]

    core.CGEventCreateMouseEvent.restype = ctypes.c_void_p
    core.CGEventCreateMouseEvent.argtypes = [
        ctypes.c_void_p, ctypes.c_uint32, CGPoint, ctypes.c_uint32
    ]
    core.CGEventPost.argtypes = [ctypes.c_uint32, ctypes.c_void_p]

    point = CGPoint(x, y)
    for event_type in (5, 1, 2):  # mouseMoved, leftMouseDown, leftMouseUp
        event = core.CGEventCreateMouseEvent(None, event_type, point, 0)
        core.CGEventPost(0, event)  # kCGHIDEventTap
        time.sleep(0.15)


def click_field_for_real(surface_height: int, pid: int) -> None:
    """The platform's way of producing a click a window system believes in."""
    if PLATFORM == "macos":
        click_field_with_cgevent(surface_height, pid)
        return
    if PLATFORM == "linux":
        # The demo's field, from e2e/index.js. xdotool goes through the X server,
        # so GDK delivers the press itself.
        click_with_xdotool([(FIELD_POINT[0], FIELD_POINT[1])], pid)
        return
    raise Skipped(f"no real click on {PLATFORM}")


def real_input_budget(points: int, typing: bool, run_ms: int) -> int:
    """How long the host has to live for clicks driven from outside it.

    A real click is `xdotool` moving the pointer and pressing, so the host knows
    nothing about it and its quit timer is whatever the scenario asked for. The
    clicks cost four seconds of waiting for the window and about 1.4 each, which
    for "scroll away and back" left **0.2 seconds** between the second click and
    the host exiting. It failed on CI's Linux shard three runs running the moment
    something slept in there -- a 0.3 second settle after an `xdotool
    windowraise` -- and would have failed on its own on a slower runner.

    So the timer is computed from the work rather than assumed to cover it, with
    two seconds over for the dump and the exit. Never shortened: a scenario that
    asked for longer wants longer, for reasons of its own.

    Injected mode is not this: there the host schedules the taps itself and
    core/TestSettle.h extends its own schedule to cover them.
    """
    needed = 4000 + points * 1500 + (2500 if typing else 0) + 2000
    return max(run_ms, needed)


def run_host(bundle: Path, taps: str = "", run_ms: int = 4000, typing: str = "") -> str:
    """Runs the host once and returns the widget tree it dumped."""
    points = [
        (int(part.split(",")[0]), int(part.split(",")[1]))
        for part in taps.split(";")
        if part
    ]

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env = dict(os.environ)
        env["BASALT_DUMP_TREE"] = str(dump)
        env["BASALT_QUIT_AFTER_MS"] = str(run_ms)
        env.pop("BASALT_TEST_TAP", None)
        env.pop("BASALT_TEST_TYPE", None)

        if points and INPUT_MODE == "injected":
            env["BASALT_TEST_TAP"] = taps
        if typing and INPUT_MODE == "injected":
            env["BASALT_TEST_TYPE"] = typing

        command = [str(HOST), str(bundle), MODULE]

        if (points or typing) and INPUT_MODE == "real":
            run_ms = real_input_budget(len(points), bool(typing), run_ms)
            env["BASALT_QUIT_AFTER_MS"] = str(run_ms)

        timeout = run_ms / 1000 + 60

        if (points or typing) and INPUT_MODE == "real":
            process = subprocess.Popen(
                command, cwd=REPO, env=env, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, text=True,
            )
            # The window has to exist before it can be clicked.
            time.sleep(4)
            try:
                click_with_xdotool(points, process.pid)
                if typing:
                    type_with_xdotool(typing)
            finally:
                # The same courtesy `run_host_process` extends, this path having
                # to open the process itself: xdotool needs it running to click
                # it. Without this, the one input mode CI's Linux uses would be
                # the one that reports a hang as four words.
                try:
                    _, stderr = process.communicate(timeout=timeout)
                except subprocess.TimeoutExpired:
                    _, stderr = _ask_where_it_was(process)
                    raise subprocess.TimeoutExpired(
                        command, timeout, stderr=stderr) from None
            _remember_output(stderr)
            check_output(stderr, process.returncode)
        else:
            result = run_host_process(
                command, cwd=REPO, env=env, capture_output=True, text=True, timeout=timeout,
            )
            _remember_output(result.stderr)
            check_output(result.stderr, result.returncode)

        if not dump.exists():
            raise Failure("host wrote no widget tree")
        return dump.read_text()


def expect_logged(needle: str, why: str) -> None:
    """Asserts on what the host printed, rather than on what it rendered.

    Some facts are not in the widget tree and should not be. `Platform.OS` is
    one: the demo used to render it, which made the tree differ between the two
    desktops for a reason that was not a bug, so scripts/compare_all.sh could
    never compare the richest app there is. Logging it keeps the check and lets
    the trees match.
    """
    if needle not in LAST_HOST_OUTPUT:
        raise Failure(f"{why}: expected {needle!r} in the host's output")


def expect_not_logged(needle: str, why: str) -> None:
    """The other half of `expect_logged`, for a line that means something broke.

    Asserting an *absence* is weak on its own, since a line can go missing because
    nothing logged it. It earns its place next to an `expect_logged` for the
    matching success, which is what makes the pair say the thing is working
    rather than merely quiet.
    """
    if needle in LAST_HOST_OUTPUT:
        line = next(
            (one.strip() for one in LAST_HOST_OUTPUT.splitlines() if needle in one),
            needle,
        )
        raise Failure(f"{why}\n        {line}")


def expect_contains(tree: str, needle: str, why: str) -> None:
    if needle not in tree:
        raise Failure(f"{why}: expected to find {needle!r} in the widget tree")


# core/ScrollIndicator.h's constants. Named here rather than repeated as
# numbers, and deliberately not read from the C++ -- a test that took them from
# the thing it is testing could not notice either one changing.
SCROLLBAR_INSET = 2.0
SCROLLBAR_MINIMUM = 24.0


def scroll_offset(tree: str) -> float:
    """The vertical scroll offset of the only scrolling view in the tree."""
    matches = re.findall(r"scroll=\(([-0-9.]+),([-0-9.]+)\)", tree)
    if not matches:
        return 0.0
    return float(matches[0][1])


def scrollbar(tree: str, axis: str) -> tuple:
    """The overlay scrollbar's (offset, length) along `axis`, or None.

    Absent from the dump when the content fits, which is the same thing as the
    scrollbar not being drawn -- see core/ScrollIndicator.h.
    """
    match = re.search(rf"scrollbar-{axis}=\(([-0-9.]+),([-0-9.]+)\)", tree)
    if match is None:
        return None
    return (float(match.group(1)), float(match.group(2)))


def scroller_size(tree: str) -> tuple:
    """The width and height of the view that reported itself as a list."""
    for line in tree.splitlines():
        if "role=list" not in line:
            continue
        match = re.search(r"frame=\([-0-9.]+,[-0-9.]+ ([0-9.]+)x([0-9.]+)\)", line)
        if match:
            return (float(match.group(1)), float(match.group(2)))
    raise Failure("no view reported itself as a list; is the ScrollView mounted?")


# --------------------------------------------------------------------------
# Where to tap
#
# These coordinates used to be constants worked out from e2e/index.js -- "the
# button row sits at the bottom of a 900x700 window, inside 24pt of padding,
# three buttons across 852pt with 12pt gaps, so each is 272 wide". Correct
# arithmetic, and it says nothing about where the button actually went: a
# restyle moves the thing and the taps keep landing on empty background, where
# the scenario fails with "scrollToEnd left the offset at 0" and sends whoever
# reads it looking at the scroll code.
#
# So the app is asked instead. One extra run per bundle, with no taps, whose
# tree is then searched for the label -- which is also how a person would find
# the button, and survives anything but renaming it.
#
# It measures per host, which matters more than it sounds: the three shapers
# disagree about how wide "scroll to end" is, so the centre of that label is a
# few points apart on each desktop. The old constants were one set of numbers
# for all three.

_MEASURED: dict = {}


def measured_tree(bundle: Path) -> str:
    """The tree this bundle mounts when nothing is tapped, measured once."""
    key = (str(bundle), PLATFORM, MODULE)
    if key not in _MEASURED:
        # Long enough to mount and lay out, and no longer: nothing is being
        # driven, so there is nothing to wait for after the first frame.
        _MEASURED[key] = run_host(bundle, run_ms=2500)
    return _MEASURED[key]


def _views(tree: str):
    """Every view in the dump, with its frame in surface-root points.

    Frames in the dump are relative to the parent, and depth is two spaces of
    indentation, so the absolute position is the sum down the path. Transforms
    and scroll offsets are not applied -- a tap target inside a rotated or
    scrolled view would need them, and nothing here taps one.
    """
    origins = {-1: (0.0, 0.0)}
    for line in tree.splitlines():
        body = line.lstrip(" ")
        if not body.startswith("view "):
            continue
        match = re.search(
            r"frame=\(([-0-9.]+),([-0-9.]+) ([0-9.]+)x([0-9.]+)\)", body)
        if match is None:
            continue
        depth = (len(line) - len(body)) // 2
        x, y, width, height = (float(group) for group in match.groups())
        parent_x, parent_y = origins.get(depth - 1, (0.0, 0.0))
        origin = (parent_x + x, parent_y + y)
        origins[depth] = origin
        yield body, origin, (width, height)


def _centre(bundle: Path, matches, what: str) -> tuple:
    for body, (x, y), (width, height) in _views(measured_tree(bundle)):
        if matches(body):
            return (round(x + width / 2), round(y + height / 2))
    raise Failure(f"no {what} in the widget tree; has the demo been restyled?")


def tap_point(bundle: Path, label: str) -> tuple:
    """The centre of the <Text> reading `label`, in surface-root points.

    The label rather than the button around it, deliberately: a tap that lands
    on the glyphs and still works proves a touch on a child bubbled to the
    Pressable that handles it.
    """
    return _centre(bundle, lambda body: f'text="{label}"' in body, f"label {label!r}")


def taps_for(bundle: Path, *labels: str) -> str:
    """`BASALT_TEST_TAP`'s semicolon-separated form, for these labels in order."""
    points = [tap_point(bundle, label) for label in labels]
    return ";".join(f"{x},{y}" for x, y in points)


def offset_label(tree: str) -> float:
    """The number the demo renders next to 'contentOffset.y'."""
    for line in tree.splitlines():
        if 'text="contentOffset.y"' in line:
            continue
        # Not anchored to the end of the line: a line may carry a role or
        # other attributes after its text.
        match = re.search(r'text="(\d+)"', line)
        if match:
            return float(match.group(1))
    raise Failure("no numeric offset label in the widget tree")


# --------------------------------------------------------------------------
# Scenarios
# --------------------------------------------------------------------------


def test_initial_render(bundle: Path) -> None:
    tree = run_host(bundle)

    expect_contains(tree, "React Native on the desktop", "React rendered no text")

    # The platform package, end to end: an app built for `linux` has to see
    # Platform.OS === 'linux'. Getting this wrong is quiet -- React Native's
    # Platform shim resolves to itself and yields undefined rather than
    # complaining -- so the demo logs it and this asserts on the log.
    #
    # The log rather than the tree, since phase 28: rendering it made the demo's
    # tree differ between the two desktops for a reason that was not a bug, and
    # that tree is the thing scripts/compare_all.sh compares.
    expect_logged(
        f"Platform.OS is {PLATFORM}",
        f"the app did not see Platform.OS === {PLATFORM!r}; was it bundled for {PLATFORM}?",
    )
    # The mount-report guard, in the one direction nothing else can see. Every
    # host reports a finished transaction to the UIManager for the benefit of a
    # mount hook, and core/UIManagerAccess.cpp refuses to do it for a surface
    # the UIManager has already lost, which is a workaround for an upstream
    # use-after-free. Nothing in this suite asserts that a mount hook ever ran,
    # so a guard that refused *every* surface would stop Reanimated and leave the
    # whole suite green. These two lines are the cheapest thing that notices:
    # the report has to happen at least once, and in a run that tears nothing
    # down it must never be refused.
    expect_logged(
        "mount reported to the UIManager",
        "no transaction was reported to the UIManager, so a mount hook would "
        "never run and an animation driven by one would never resume",
    )
    expect_not_logged(
        "mount report refused",
        "a mount report was refused in a run that stops no surface, so the "
        "guard in core/UIManagerAccess.cpp is asking the wrong question",
    )

    expect_contains(tree, "texture=160x100", "the image never loaded or decoded")
    expect_contains(tree, 'text="row 0"', "the list did not render")
    expect_contains(tree, 'text="row 23"', "the list is short of rows")

    # Accessibility: what a screen reader would be told. A <Text> should call
    # itself a label and an <Image> an image without the app saying so, and an
    # explicit accessibilityRole should win.
    expect_contains(tree, "role=text", "no <Text> reported itself as text")
    expect_contains(tree, "role=image", "no <Image> reported itself as an image")
    expect_contains(tree, "role=button", "the buttons did not take their accessibilityRole")
    expect_contains(tree, "role=list", "the ScrollView did not take its accessibilityRole")

    # The ScrollView must clip, or its content paints over its siblings. It is
    # found by its accessibilityRole rather than its colour: more than one view
    # in the demo shares a background, and matching on that picked the wrong
    # one as soon as the demo grew.
    scroller = [line for line in tree.splitlines() if "role=list" in line]
    if not scroller:
        raise Failure("no view reported itself as a list; is the ScrollView mounted?")
    if "clip" not in scroller[0]:
        raise Failure("the ScrollView is not clipping")

    if scroll_offset(tree) != 0.0:
        raise Failure("a freshly mounted ScrollView should be at the top")

    # The overlay scrollbar. It is pure paint -- no widget, no child, nothing
    # with a tag -- so the dump is the only place a test can see it at all.
    bar = scrollbar(tree, "v")
    if bar is None:
        raise Failure("the ScrollView drew no scrollbar, though its content overflows")
    bar_offset, bar_length = bar
    _, height = scroller_size(tree)
    if bar_offset != SCROLLBAR_INSET:
        raise Failure(f"a scrollbar at the top belongs at the inset, not at {bar_offset}")
    if not SCROLLBAR_MINIMUM <= bar_length < height:
        raise Failure(
            f"the thumb is {bar_length} long in a {height} track, which is not a fraction of it"
        )


def test_scroll_to_end(bundle: Path) -> None:
    # The tap lands on the button's *label*, so a pass also means a touch on a
    # child bubbled to the Pressable that handles it. Under real input it also
    # means the X server and GDK delivered the event.
    tree = run_host(bundle, taps=taps_for(bundle, "scroll to end"), run_ms=5000)

    offset = scroll_offset(tree)
    if offset <= 0:
        raise Failure(f"scrollToEnd left the offset at {offset}")

    # onScroll has to have reached JavaScript, or the label would still say 0.
    label = offset_label(tree)
    if label <= 0:
        raise Failure(f"onScroll never reached React; the label reads {label}")
    if abs(label - offset) > 2.0:
        raise Failure(f"the label ({label}) disagrees with the widget tree ({offset})")

    # And the scrollbar went with it, all the way to the far end of its track.
    bar = scrollbar(tree, "v")
    if bar is None:
        raise Failure("scrollToEnd left the ScrollView with no scrollbar at all")
    bar_offset, bar_length = bar
    if bar_offset <= SCROLLBAR_INSET:
        raise Failure(f"the thumb stayed at {bar_offset} while the content scrolled to the end")
    _, height = scroller_size(tree)
    if abs((bar_offset + bar_length) - (height - SCROLLBAR_INSET)) > 1.0:
        raise Failure(
            f"the thumb ends at {bar_offset + bar_length} rather than at the end of "
            f"the {height}-point track"
        )


def test_scroll_round_trip(bundle: Path) -> None:
    tree = run_host(
        bundle, taps=taps_for(bundle, "scroll to end", "scroll to top"), run_ms=6000)

    if scroll_offset(tree) != 0.0:
        raise Failure("scrollTo({y: 0}) did not return to the top")
    if offset_label(tree) != 0.0:
        raise Failure("the offset label did not follow the scroll back to zero")


# --------------------------------------------------------------------------
# Fast Refresh
# --------------------------------------------------------------------------

# Not 8081: a developer running Metro for real should not have to stop it, and
# a test that silently talks to someone else's dev server proves nothing.
METRO_PORT = 8099


def is_port_taken(port: int, host: str = "localhost") -> bool:
    try:
        with socket.create_connection((host, port), timeout=1.5):
            return True
    except OSError:
        return False


class Metro:
    """Metro on its own port, for the Fast Refresh scenario."""

    def __init__(self, log: Path) -> None:
        self.process = None
        self.log = log

    def __enter__(self) -> "Metro":
        # Refuse to share the port. A packager already listening here is not
        # ours, and talking to it means testing against whatever React Native
        # *it* was started with. That happened: a Metro left over from a run
        # against another version served its bundle to this one for an hour,
        # and the symptom was a version mismatch nobody had introduced. Note
        # that neither `pkill -f "cli.js start"` nor `pkill -f "metro serve"`
        # matches these -- scripts/metro.js serves in-process -- which is why
        # they accumulate unnoticed.
        if is_port_taken(METRO_PORT):
            raise Failure(
                f"something is already listening on port {METRO_PORT}.\n"
                "This scenario needs its own packager, and reusing a stranger's "
                "would test whatever React Native that one was started with.\n"
                'Stop it with: pkill -f "scripts/metro.js"'
            )

        # Kept rather than discarded: when this scenario fails it is almost
        # always Metro doing something, and a CI run that only says "no update
        # arrived" costs another four minutes to learn anything from.
        self.sink = self.log.open("w")
        self.process = subprocess.Popen(
            ["node", str(REPO / "scripts" / "metro.js"), "--port", str(METRO_PORT)],
            cwd=REPO,
            stdout=self.sink,
            stderr=subprocess.STDOUT,
        )
        # Readiness is the port accepting a connection, not any particular
        # endpoint: Metro's /status is not served on this version. Polling beats
        # a fixed sleep -- a cold Metro on a slow machine takes a while.
        deadline = time.time() + 90
        while time.time() < deadline:
            if self.process.poll() is not None:
                raise Failure("metro exited before it started serving")
            try:
                with socket.create_connection(("localhost", METRO_PORT), timeout=2):
                    return self
            except OSError:
                time.sleep(1)
        raise Failure("metro did not start listening")

    def serves_edit(self) -> str:
        """Whether a fresh bundle from Metro contains the edit.

        This is what separates the two ways this scenario can fail. If Metro
        serves the new text, its file watching is fine and the Fast Refresh
        client never subscribed. If it serves the old text, Metro never saw the
        file change.
        """
        try:
            return "yes" if AFTER in self.fetch_bundle() else "no"
        except Exception as error:  # diagnostics must not raise
            return f"could not tell ({error})"

    def fetch_bundle(self) -> str:
        # The platform of the run, not a fixed one. Both callers care:
        # `prewarm` is warming the bundle *this host* is about to ask for, and
        # `serves_edit` is diagnosing the bundle it actually ran. Hardcoding
        # `linux` made a macOS run prewarm the wrong bundle -- leaving the real
        # one cold, which is the race prewarm exists to lose -- and then report
        # on a bundle nothing had loaded.
        url = (
            f"http://localhost:{METRO_PORT}/index.bundle"
            f"?platform={PLATFORM}&dev=true&minify=false"
        )
        with urllib.request.urlopen(url, timeout=300) as response:
            if response.status != 200:
                raise Failure(f"metro answered {response.status} for the bundle")
            return response.read().decode("utf-8", "replace")

    def prewarm(self) -> None:
        """Builds the bundle before the host asks for it.

        The host tries Metro and falls back to the on-disk bundle, which is a
        production one. A cold Metro takes longer to answer than that fallback
        is willing to wait, so without this the app quietly runs the *release*
        bundle and no edit will ever reach it -- which is what CI saw, reported
        as "Metro never pushed an update".

        Warming alone is all this is for. It used to do a second job by
        accident: the graph it built was the one HMRClient subscribed to, so on
        Linux Fast Refresh worked because of this call rather than because the
        host was right.
        """
        try:
            self.fetch_bundle()
        except urllib.error.URLError as error:
            raise Failure(f"metro could not build the bundle: {error}") from error

    def __exit__(self, *_) -> None:
        if self.process is not None:
            self.process.terminate()
            try:
                self.process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                self.process.kill()
        self.sink.close()


def tail(log: Path, lines: int = 25) -> str:
    if not log.exists():
        return "(no log)"
    return "\n".join(log.read_text().splitlines()[-lines:])


def git_bash() -> Path | None:
    """Git for Windows' bash, or None. See bundle_app for why it is not `bash`."""
    roots = [
        os.environ.get("ProgramFiles", r"C:\Program Files"),
        os.environ.get("ProgramW6432", r"C:\Program Files"),
        os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
    ]
    for root in roots:
        candidate = Path(root) / "Git" / "bin" / "bash.exe"
        if candidate.exists():
            return candidate
    return None


def app_source(entry: str) -> Path:
    """Where `entry` lives, whatever extension it is written in.

    The apps are TypeScript; `metro.config.js` and `babel.config.js` are not,
    because Metro and Babel load those as plain CommonJS before anything could
    compile them. Asked rather than assumed so that a stray `.js` app keeps
    working and so that this is the only place the answer lives.
    """
    for extension in ("tsx", "ts", "js"):
        candidate = REPO / "e2e" / f"{entry}.{extension}"
        if candidate.exists():
            return candidate
    raise Failure(f"no e2e/{entry}.(tsx|ts|js)")


def javascript_built_at() -> float:
    """When this repository's own JavaScript was last built.

    Walked once and remembered: thirty-one apps ask, and the answer cannot
    change while the suite runs -- `scripts/test_all.sh` builds the packages
    before any scenario starts.
    """
    global _JAVASCRIPT_BUILT_AT
    if _JAVASCRIPT_BUILT_AT is None:
        newest = 0.0
        for built in (REPO / "packages" / "basalt-core" / "dist").rglob("*.js"):
            newest = max(newest, built.stat().st_mtime)
        _JAVASCRIPT_BUILT_AT = newest
    return _JAVASCRIPT_BUILT_AT


def bundle_app(build: Path, entry: str, dev: bool = False) -> Path:
    """Bundles e2e/<entry>.js for this platform, unless it is already there.

    Most scenarios run the demo, which CI bundles as a build step. The two that
    need an app of their own -- hover and pointerEvents, both about input that
    the demo has nothing listening for -- build it here rather than adding two
    more steps to every CI job that will not use them.
    """
    bundled = build / f"{entry}.{PLATFORM}.jsbundle.js"
    source = app_source(entry)
    # Older than the file it was built from means a scenario would silently test
    # the last version of the app rather than this one -- which is exactly what
    # happened the first time this helper was used twice.
    #
    # And "the file it was built from" is not only the app. A bundle also
    # carries this repository's JavaScript: the view-config override decides
    # which props React sends at all, so a change there is invisible to an app
    # whose source has not moved. That cost an hour: `accessibilityViewIsModal`
    # was added to the override, the scenario kept failing, and the bundle it
    # was testing had been built before the override changed.
    newest = max(source.stat().st_mtime, javascript_built_at())
    if bundled.exists() and bundled.stat().st_mtime >= newest:
        return bundled
    arguments = [
        "--dev" if dev else "--prod",
        "--platform", PLATFORM,
        "--entry", source.name,
        "--out", f"{entry}.{PLATFORM}.jsbundle",
        "--build-dir", build.name,
    ]
    # Windows cannot exec a shell script, and answers an attempt with
    # "%1 is not a valid Win32 application" -- which says nothing about shells.
    # Git for Windows brings bash, and CI's own bundle step already runs this
    # same script through it.
    #
    # Named by path, not as `bash`. On a Windows runner that name resolves to
    # C:\Windows\System32\bash.exe, which is the WSL launcher: it exits 1
    # saying "Windows Subsystem for Linux has no installed distributions", on
    # *stdout*, which is how this failed for two runs with an empty error
    # message.
    #
    # A relative script path, and only here: bash on Windows reads a backslash
    # as an escape, so an absolute `D:\a\...\bundle.sh` is not the path it
    # looks like. Everything here runs with cwd=REPO anyway.
    if os.name == "nt":
        shell = git_bash()
        if shell is None:
            raise Skipped("bundling this app needs Git for Windows' bash")
        command = [str(shell), "scripts/bundle.sh", *arguments]
    else:
        command = [str(REPO / "scripts" / "bundle.sh"), *arguments]

    built = subprocess.run(command, cwd=REPO, capture_output=True, text=True, timeout=600)
    if built.returncode != 0 or not bundled.exists():
        # Both streams and the exit code: bundle.sh puts Metro's own output on
        # stdout, and a failure that reports neither is a failure nobody can act
        # on -- which is exactly how this first failed on a runner.
        raise Failure(
            f"could not bundle e2e/{entry}.js (exit {built.returncode}, "
            f"{'wrote' if bundled.exists() else 'no'} bundle):\n"
            f"--- stdout ---\n{tail_text(built.stdout, 30)}\n"
            f"--- stderr ---\n{tail_text(built.stderr, 30)}"
        )
    return bundled


def tail_text(text: str, lines: int = 25) -> str:
    return "\n".join(text.splitlines()[-lines:])


# How long a host gets to print a backtrace once it has been asked for one.
# Generous: it writes with `backtrace_symbols_fd` from inside a signal handler,
# resolving symbols against a binary that is not small.
BACKTRACE_GRACE_S = 10.0

# The marker core/CrashHandler.cpp writes before the frames.
CRASH_MARKER = "*** basalt: "

# What `_ask_where_it_was` writes before gdb's output, and how much of it to
# show. Ten threads of a Fabric host is a few hundred lines; the cap is there so
# one hang cannot bury the rest of a job's log.
THREADS_MARKER = "--- every thread, from gdb ---"
MAX_THREAD_LINES = 400


def run_host_process(args: list, *, cwd: Path = REPO, env: dict = None,
                     capture_output: bool = True, text: bool = True,
                     timeout: float = None) -> subprocess.CompletedProcess:
    """`subprocess.run` for a host binary, that asks a hung one where it was.

    Not `run_host` above, which starts a host and gives back its widget tree.
    This is the layer under that: it is what every scenario that launches the
    binary itself now goes through.

    A host that stops answering was the least informative failure in this suite.
    `capture_output` holds the pipes and the harness kills the process, so the
    report was four words. The hang in `docs/backlog/testing.md` was diagnosed
    only by which log lines were *missing*, because that was all there was.

    So ask before killing. `SIGABRT` runs the handler in core/CrashHandler.cpp,
    which writes a marker and up to 64 frames straight to `STDERR_FILENO` and
    then re-raises, so the frames arrive on the pipe already being read and a
    hang reads the way a crash does. The timeout still raises `TimeoutExpired`,
    now carrying that output, so every caller that already catches one keeps
    working and gets the frames without knowing about any of this.

    POSIX only. Windows has no `SIGABRT` to send from another process, and its
    half of the crash handler is an unhandled-exception filter rather than a
    signal handler, so there this stays a kill. The hang it was built for is on
    GTK.

    **The frames are of whichever thread took the signal, which is not promised
    to be the stuck one.** `kill` delivers to any thread that has it unblocked,
    and for a main loop that stopped dispatching that is usually the main
    thread. A backtrace that looks unrelated to the hang is a reason to suspect
    this before concluding anything from it.
    """
    assert capture_output and text, "a host is always captured, and always text"
    with subprocess.Popen(args, cwd=cwd, env=env, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE) as host:
        try:
            out, err = host.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            out, err = _ask_where_it_was(host)
            # `from None`: the chained original would say only that a timeout
            # caused a timeout, and this one carries output the other lacked.
            raise subprocess.TimeoutExpired(
                args, timeout, output=out, stderr=err) from None
        return subprocess.CompletedProcess(args, host.returncode, out, err)


def all_thread_stacks(pid: int) -> str:
    """Every thread's stack, which the signal handler cannot give.

    `SIGABRT` is delivered to one thread, so the handler reports one stack. That
    is enough for a host blocked in its own main loop and useless for a deadlock,
    where the interesting thread is the one the stalled thread waits for:
    docs/backlog/testing.md has a teardown hang whose main thread sits in
    `future::wait()` for a JavaScript thread nobody has seen the stack of.

    Best effort, and it says why when it cannot. Attaching needs ptrace, which a
    runner may restrict to a direct parent (`kernel.yama.ptrace_scope`), and gdb
    is not installed everywhere. Linux only: lldb on a Mac wants codesigning this
    does not have, and the hang this is for is on GTK.
    """
    if sys.platform != "linux":
        return ""
    if not shutil.which("gdb"):
        return "(no gdb here, so no other thread's stack)"
    try:
        asked = subprocess.run(
            ["gdb", "-p", str(pid), "-batch", "-nx",
             "-ex", "set pagination off",
             "-ex", "thread apply all bt"],
            capture_output=True, text=True, timeout=120,
        )
    except (OSError, subprocess.TimeoutExpired):
        return "(gdb did not answer)"
    if not asked.stdout.strip():
        # Printed rather than swallowed: "ptrace: Operation not permitted" is the
        # usual reason and is fixed in the workflow, not here.
        return f"(gdb could not attach: {tail_text(asked.stderr, 2).strip() or 'no output'})"
    return asked.stdout


def _ask_where_it_was(host: subprocess.Popen) -> tuple:
    """Every thread if gdb can give it, then `SIGABRT`, then a kill either way."""
    # Before the signal, because SIGABRT ends the process and a deadlock needs
    # the thread that is *not* stalled.
    threads = all_thread_stacks(host.pid)

    def answered(out: str, err: str) -> tuple:
        if not threads:
            return out, err
        return out, f"{err}\n--- every thread, from gdb ---\n{threads}"

    if os.name != "nt":
        try:
            host.send_signal(signal.SIGABRT)
        except (ProcessLookupError, OSError):
            pass  # Gone between the timeout and here, which is its own answer.
        else:
            try:
                return answered(*host.communicate(timeout=BACKTRACE_GRACE_S))
            except subprocess.TimeoutExpired:
                pass  # Stuck where a signal handler cannot run either.
    host.kill()
    try:
        return answered(*host.communicate(timeout=BACKTRACE_GRACE_S))
    except subprocess.TimeoutExpired:
        return "", threads


def hang_text(stream, lines: int = 64) -> str:
    """What to show of a hung host: its frames if it managed any, else the tail.

    `tail_text` is wrong for a backtrace. Frames print innermost first, so the
    last lines of the stream are the ones nearest `main`, and the innermost
    frames -- where it is actually stuck -- are exactly what a tail discards.

    The default is the handler's own 64 frames rather than a round number. A
    first reading of real frames was cut at 24 and stopped just above
    `g_closure_invoke`, which left the one question that mattered unanswered:
    whether the stack reached `main`, and so whether the stalled thread was the
    loop or JavaScript.
    """
    if isinstance(stream, bytes):
        stream = stream.decode("utf-8", "replace")

    # gdb's dump is appended after the host's own output and gets its own budget.
    # Sharing one would be the worst of both: a signal handler's 64 frames and
    # ten threads of gdb do not fit in the same cut, and the second is the half
    # that exists for deadlocks.
    threads = ""
    split = stream.find(THREADS_MARKER)
    if split >= 0:
        threads = "\n".join(stream[split:].splitlines()[:MAX_THREAD_LINES])
        stream = stream[:split]

    marker = stream.rfind(CRASH_MARKER)
    if marker < 0:
        shown = tail_text(stream, lines)
    else:
        shown = symbolised("\n".join(stream[marker:].splitlines()[:lines + 1]))
    return f"{shown}\n{threads}" if threads else shown


# `basalt_gtk(+0x87685c)[0x5611...]`: the offset is what addr2line wants, and
# the absolute address is useless once the process is gone.
FRAME = re.compile(r"^(?P<path>\S+)\(\+(?P<offset>0x[0-9a-fA-F]+)\)\[0x[0-9a-fA-F]+\]$")


def symbolised(frames: str) -> str:
    """Puts names on our own frames, where the platform can.

    `backtrace_symbols_fd` allocates nothing, which is why the handler uses it,
    and the price is that a static function in our own binary prints as a bare
    offset while every library frame comes out named. That asymmetry is exactly
    backwards for reading a hang: the library frames say GTK was in a clipboard
    call, and the unnamed ones are the code that asked it to be.

    Best effort. No addr2line, or an addr2line that cannot read the binary, and
    the frames come back as they were rather than the function failing.
    """
    if not shutil.which("addr2line"):
        return frames

    lines = frames.splitlines()
    wanted = {}
    for index, line in enumerate(lines):
        found = FRAME.match(line.strip())
        if found and Path(found.group("path")).name.startswith("basalt_"):
            wanted.setdefault(found.group("path"), []).append(
                (index, found.group("offset")))

    for binary, entries in wanted.items():
        if not Path(binary).exists():
            continue
        try:
            named = subprocess.run(
                ["addr2line", "-C", "-f", "-p", "-e", binary,
                 *(offset for _, offset in entries)],
                capture_output=True, text=True, timeout=60,
            )
        except (OSError, subprocess.TimeoutExpired):
            continue
        answers = named.stdout.splitlines()
        if len(answers) != len(entries):
            continue
        unresolved = []
        for (index, offset), answer in zip(entries, answers):
            if "??" in answer:
                unresolved.append((index, offset))
                continue
            lines[index] = f"{offset}  {answer.strip()}"
        # addr2line reads DWARF, so it answers for our own sources and says `??`
        # for everything linked in without `-g`. That is most of the interesting
        # frames: the first real hang bottomed out in React Native's code, where
        # every frame came back unnamed while basalt's own resolved. Those
        # functions are still in the symbol table, which is what nm reads.
        if unresolved:
            for index, name in nearest_symbols(binary, unresolved):
                lines[index] = name
    return "\n".join(lines)


def nearest_symbols(binary: str, wanted: list) -> list:
    """Names frames from the symbol table: the nearest symbol at or below each.

    Nearest-preceding rather than exact, because a return address points into
    the middle of a function. The offset is kept alongside so a reader can tell
    a confident hit from a frame that landed a long way past its symbol.
    """
    if not shutil.which("nm"):
        return []
    try:
        listed = subprocess.run(
            ["nm", "-C", "--defined-only", "--numeric-sort", binary],
            capture_output=True, text=True, timeout=120,
        )
    except (OSError, subprocess.TimeoutExpired):
        return []

    table = []
    for line in listed.stdout.splitlines():
        parts = line.split(" ", 2)
        if len(parts) == 3 and parts[1].upper() in ("T", "W"):
            try:
                table.append((int(parts[0], 16), parts[2]))
            except ValueError:
                continue
    if not table:
        return []
    table.sort()

    named = []
    for index, offset in wanted:
        address = int(offset, 16)
        at = bisect.bisect_right(table, (address, chr(0x10FFFF))) - 1
        if at < 0:
            continue
        start, name = table[at]
        named.append((index, f"{offset}  {name} + {address - start}"))
    return named


def is_subsequence(wanted: list, got: list) -> bool:
    """Whether `wanted` appears in `got` in order, with anything in between.

    Not equality: a run on a real display can pick up a motion event of its own
    -- a window mapping under the cursor is one -- and what is being asserted is
    the order these arrive in, not that nothing else ever happens.
    """
    iterator = iter(got)
    return all(item in iterator for item in wanted)


def wait_for_log(log: Path, needle: str, count: int, timeout: float,
                 start: int = 0) -> bool:
    """Waits until `needle` has appeared in `log` at least `count` times.

    `start` is a byte offset to begin reading at, for the case where the log
    already contains what is being waited for and what matters is a *new*
    occurrence. Counting from the beginning silently succeeds there, and a wait
    that cannot fail is worse than no wait: it reports the thing it was watching
    for as having happened.
    """
    deadline = time.time() + timeout
    while time.time() < deadline:
        if log.exists():
            with log.open("rb") as handle:
                handle.seek(start)
                text = handle.read().decode("utf-8", "replace")
            if text.count(needle) >= count:
                return True
        time.sleep(0.5)
    return False


# Scenarios that may quit as soon as the host has settled, rather than waiting
# out a budget chosen for the slowest machine that will ever run them. See
# core/TestSettle.h: settled means the first mount was applied and any scripted
# input was delivered, plus a short settle for React's next commit.
#
# An opt-in list rather than a flag on every scenario, and rather than opting
# *out*: what this cannot know about is work that happens after the last input
# with nothing scheduling it -- a promise resolving, an animation running, a
# dialog being answered. Those need their own budget, and there is no way to tell
# them apart by reading the code. So a scenario joins this list once it has been
# run and seen to pass, and the ones absent from it are not all unsafe -- most
# are simply unexamined.
#
# Getting it wrong fails rather than passes: every scenario here asserts on the
# dumped tree or on a log line, so quitting early produces a missing assertion
# and not a quiet success. That is the only reason an empirical list is
# acceptable here.
# Which scenarios are absent, and why, because that is the part worth knowing.
# Every scenario was opted in at once and the suite run; these seven failed, and
# each failure is the same shape -- work that happens after the last scripted
# input with nothing scheduling it, so "settled" is true before the scenario is
# finished:
#
#   test_scrollbar_can_be_turned_off  scrollTo from JavaScript after mount;
#   test_content_inset               "the list never scrolled", "rested at 0.0"
#   test_animated_scroll              an animation, which is time by definition
#   test_click_focuses_a_field        a real mouse, driven from outside the process
#   test_logbox                       a console error and the toast it raises
#   test_quit_request                 a refusal, then an agreement, which is two
#                                     events separated by time on purpose
#   test_debugging_overlay            a trace update that takes itself down again
#   test_fast_refresh                 an edit arriving from another process
#
# And five more that passed the probe and then flaked in CI, which is the same
# category arriving late: every scenario that answers a **dialog or a menu**.
#
#   test_alert, test_share, test_file_dialogs   BASALT_TEST_DIALOG
#   test_context_menu, test_dev_menu            BASALT_TEST_MENU
#
# Those instruments are *reactive*: they answer something when it appears, rather
# than being laid out on a host's timer, so the settle cannot see them and the
# callback they provoke arrives after it. `Alert.alert` failed on a macOS shard
# with "the alert's callback never reached JavaScript" -- the host had quit.
#
# This is the distinction core/TestSettle.h already draws, and drawing it there
# and not here is the mistake. BASALT_TEST_MENU and BASALT_TEST_DIALOG were taken
# *out* of scriptedInputVars for exactly this reason, and the scenarios that
# depend on them were left opted in. One fact, two places, and it only got
# applied to one of them.
#
# It cost about 70 of the 235 seconds. A flaky suite is worse than a slow one,
# which is the argument that justified this work in the first place.
#
# Those keep their budgets, and should: a settle cannot stand in for a duration
# that is the thing under test. Everything else here was run and seen to pass.
#
# Guessing is safe only because getting it wrong fails loudly -- each of these
# asserts on the dumped tree or a logged line, so quitting early produces a
# missing assertion rather than a quiet success. It is still a list of scenarios
# that have been *run*, not of scenarios that looked fine.
SETTLES_EARLY: set = set()
# Empty, deliberately, and the instrument it opts into is kept. See below.
#
# BASALT_QUIT_WHEN_SETTLED waits for the first mount and for scripted input to be
# *delivered*. It cannot wait for the round trip to JavaScript, and most of these
# scenarios assert on what JavaScript logged -- so the host quits while the
# evidence is still in flight. Three scenarios proved it, in three different
# ways:
#
#   Alert.alert   "the alert's callback never reached JavaScript", on CI
#   hover         a truncated event list: over/enter/leave arrived, the rest did not
#   the dialogs   answered reactively, so nothing schedules the answer at all
#
# The list was built by opting everything in, running the suite, and keeping
# what passed. That is unsound and this is what unsound looks like: passing four
# times does not establish the absence of a race, and I trimmed the list twice
# before accepting that the method rather than the entries was wrong.
#
# **The right shape is per scenario, and the tool for it already exists.** Wait
# for the evidence in the log -- the line the scenario is about to assert on --
# and then write BASALT_TEST_QUIT_FILE. That is what stop_host does for Fast
# Refresh, it is correct rather than probabilistic, and it is faster than a
# settle because it ends at the evidence instead of at a fixed delay after
# input. It is also more work per scenario, which is why this took the blanket
# route first.
#
# So the suite is back to its budgets and 445 seconds, from 298. The 147 seconds
# were not real: a suite that fails one run in two is worth less than a slow one,
# which is the argument that justified the work in the first place.

# How long after settling to go. A guess about one React commit and the frame
# that draws it, rather than about a whole scenario -- which is the difference
# that matters, because this one is bounded by the runtime and the other was
# bounded by the machine.
SETTLE_MS = "600"


def stop_host(process: subprocess.Popen, quit_file: Path = None) -> bool:
    """Ends a host that is still running. Says whether its exit code means anything.

    With a `quit_file`, asks: the path was given to the host as
    `BASALT_TEST_QUIT_FILE`, the host polls for it and shuts down through the
    same path `BASALT_QUIT_AFTER_MS` uses, so the widget tree is dumped and the
    exit code is the app's. This is the portable way and the one to prefer --
    measured at a quarter of a second on both hosts that can be measured here.

    Without one, or if the ask is not answered, a signal. GTK and AppKit install
    a SIGTERM handler and exit 0, so `terminate()` there is a request and the
    code is worth checking. Windows has no SIGTERM: `Popen.terminate()` is
    `TerminateProcess`, a hard kill that sets the exit code to 1 and that no host
    can be written to return 0 from. So on Windows the code belongs to our own
    kill rather than to the app, and asserting on it reports the kill as a crash
    -- which is what it did the first time the Fast Refresh scenario ran there,
    against a host that had started perfectly well. It also skips the tree dump,
    which is why the quit file had to exist before that scenario could run there
    at all.

    The JS-error scan in `check_output` is unaffected either way: it reads what
    the host logged, which a kill does not change.
    """
    if quit_file is not None:
        # Existence is the message; see core/TestQuitFile.h.
        quit_file.write_text("")
        try:
            # Generous: the measured time from writing the file to the process
            # being gone is 0.24s on GTK and on AppKit, and what follows the
            # poll is an ordinary teardown.
            process.wait(timeout=30)
            return True
        except subprocess.TimeoutExpired:
            # Loudly, not quietly. This used to fall through to the kill and
            # return the platform's verdict, which meant a host whose poll never
            # fired produced a passing scenario -- the instrument would have been
            # dead on a platform and nothing would have said so. That is the
            # shape of bug this whole file keeps finding, so it does not get to
            # live in the function that ends every host.
            process.kill()
            process.wait(timeout=15)
            raise Failure(
                f"the host ignored {quit_file.name} for 30 seconds.\n"
                "BASALT_TEST_QUIT_FILE is meant to be polled on every host; see "
                "core/TestQuitFile.h. Either this host does not poll it, or it "
                "was too busy to. Killed instead, so no widget tree was written "
                "and anything downstream of one would have failed next."
            )
    process.terminate()
    process.wait(timeout=60)
    return PLATFORM != "windows"


def test_fast_refresh(bundle: Path):
    """Edits the demo while it runs and checks the change lands in the window.

    This is the scenario that would have caught a wrong claim in the README:
    nothing else here runs the host in dev mode at all, so "development still
    works" was being taken on trust.

    A __DEV__ bundle needs the DevSettings TurboModule, which
    ReactCxxTurboModuleProvider serves only when a DevServerHelper exists. So
    this also pins the one configuration where that is true.

    It waits on the host's own log rather than on sleeps. A fixed budget was
    the first version and it failed in CI, where Metro is cold and a rebuild
    takes longer than a developer's warm one -- which is the same class of
    flake as any other "should be long enough".

    `BASALT_SKIP_FAST_REFRESH` drops the *edit*, not the scenario. It used to
    drop the whole thing, which meant that on every machine that is not
    somebody's laptop nothing checked dev mode at all -- and the half CI cannot
    do is one specific half. Metro on a GitHub runner never notices a file
    change; everything before the edit is the host's own code, works there, and
    is worth guarding. Two things have broken in that half already: the host
    read its Metro entry from an argument nothing passed, and it reported a dev
    script URL naming `linux` from every platform. Both would have been caught
    here without an edit ever being made.
    """
    skip_edit = bool(os.environ.get("BASALT_SKIP_FAST_REFRESH"))
    source = app_source("index")
    original = source.read_text()
    if original.count(BEFORE) != 1:
        raise Failure(f"the demo does not contain exactly one {BEFORE!r} to edit")

    running = f'Running "{MODULE}"'

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        log = Path(directory) / "host.log"
        env = dict(os.environ)
        env["BASALT_DUMP_TREE"] = str(dump)
        # A backstop, not the schedule: the host is asked to quit as soon as the
        # refresh shows up, through the quit file below. Long, because the edit
        # loop's own deadline is 150 seconds on a cold CI machine and a backstop
        # that fires first would end the run mid-scenario.
        env["BASALT_QUIT_AFTER_MS"] = "180000"
        # The portable way to ask, which the host polls for. Windows is why it
        # exists: `terminate()` there is `TerminateProcess`, which reaches
        # neither `WM_CLOSE` nor the tree dump hanging off it, so the assertion
        # at the end of this scenario failed on a host that had worked. See
        # core/TestQuitFile.h.
        quit_file = Path(directory) / "quit"
        env["BASALT_TEST_QUIT_FILE"] = str(quit_file)
        env["BASALT_DEV"] = "1"
        env["BASALT_DEV_PORT"] = str(METRO_PORT)
        env.pop("BASALT_TEST_TAP", None)
        env.pop("BASALT_TEST_TYPE", None)

        metro_log = Path(directory) / "metro.log"

        def file_state() -> str:
            """What is actually on disk, since everything else is inference."""
            try:
                stat = source.stat()
                text = source.read_text()
                return (
                    f"{source}: {stat.st_size} bytes, mtime {stat.st_mtime}, "
                    f"contains the edit: {AFTER in text}"
                )
            except Exception as error:
                return f"could not stat the demo ({error})"

        def diagnose(message: str) -> Failure:
            """Fails with what the two processes were saying, not just a verdict."""
            return Failure(
                f"{message}\n"
                f"--- the file on disk ---\n{file_state()}\n"
                f"--- all of metro ---\n{tail(metro_log, 400)}\n"
                f"--- last of the host ---\n{tail(log)}"
            )

        with Metro(metro_log) as metro, log.open("w") as sink:
            metro.prewarm()
            # Where Metro's log had got to before the host existed. Everything
            # past this point was provoked by the host and nothing else.
            time.sleep(1)
            served = metro_log.stat().st_size
            process = subprocess.Popen(
                [str(HOST), str(bundle), MODULE],
                cwd=REPO, env=env, stdout=subprocess.DEVNULL, stderr=sink, text=True,
            )
            try:
                if not wait_for_log(log, running, 1, timeout=120):
                    raise diagnose("the app never started")

                # Which bundle actually evaluated. Editing before knowing that
                # produces a confusing failure much later, because the app runs
                # perfectly well on the stale one.
                #
                # Asked of Metro rather than of the host: Metro logs a BUNDLE
                # line when it serves one, and the host logs nothing at all when
                # it falls back to the bundle on disk. The previous version
                # watched the host's log for `Failed to load TurboModule:
                # LogBox`, which said the same thing only for as long as LogBox
                # was unimplemented -- once it worked, the line stopped
                # appearing and this scenario failed on every machine, for a
                # reason that had nothing to do with Fast Refresh.
                #
                # From `served` onwards, which is the whole of the check.
                # `prewarm` is itself a request and Metro logs two BUNDLE lines
                # for it, so counting from the start of the log matched before
                # the host had done anything -- this returned in about a
                # hundredth of a second and could not fail. It had been that way
                # since it was written, on every platform; what hid it is that
                # the edit assertion below carried the scenario, and the edit
                # only works where Metro's file watching does.
                if not wait_for_log(metro_log, "BUNDLE", 1, timeout=60,
                                    start=served):
                    raise diagnose(
                        "the app is running the on-disk release bundle, not Metro's"
                    )

                if skip_edit:
                    # Everything above ran: dev mode, the dev server helper, the
                    # websocket, and a bundle Metro is now on record as having
                    # served to this process. The edit is what this machine
                    # cannot do, so stop here rather than fail at it.
                    meaningful = stop_host(process, quit_file)
                    stderr = log.read_text()
                    check_output(stderr, process.returncode if meaningful else 0)
                    if "Failed to load TurboModule: DevSettings" in stderr:
                        raise Failure(
                            "DevSettings was not served, so no __DEV__ bundle can run"
                        )
                    # That the quit file reached the *dump*, not merely that the
                    # process ended. The edit path's last assertion reads this
                    # file, and the whole reason the instrument exists is that a
                    # killed host on Windows never writes it -- so without this
                    # line CI proves the poll fires and says nothing about the
                    # thing the poll was added to make possible. It costs a
                    # stat, and it is the only place the full chain is checked
                    # on a machine rather than by hand.
                    if not dump.exists():
                        raise Failure(
                            "quitting through BASALT_TEST_QUIT_FILE wrote no widget "
                            "tree.\nThe host exited cleanly, so the quit file was "
                            "seen, but the shutdown did not reach the dump -- which "
                            "is the half of this instrument the edit path depends "
                            "on. See core/TestQuitFile.h."
                        )
                    return (
                        "the edit was not made: Metro does not notice file changes "
                        "on this machine, so only the host half of dev mode was "
                        "checked. See docs/TESTING.md"
                    )

                edited = original.replace(BEFORE, AFTER)
                source.write_text(edited)

                # The demo has no refresh boundary, so React Native reloads the
                # whole surface and the app runs a second time. Waiting on
                # either the reload or that second run keeps this from depending
                # on which of the two the demo happens to provoke.
                #
                # Rewritten on each attempt rather than written once, because a
                # watcher that is not up yet does not queue anything: the event
                # is simply never delivered and nothing re-crawls. Metro watches
                # the React Native checkout, some eight thousand directories,
                # and attaching all of that takes markedly longer on a cold CI
                # machine than on a warm laptop. Everything else was ruled out
                # first -- inotify works there, the limits are generous, and
                # neither side has watchman, so both run the same node watcher.
                deadline = time.time() + 150
                applied = False
                while time.time() < deadline and not applied:
                    applied = wait_for_log(log, running, 2, timeout=10)
                    if not applied:
                        # A rewrite is enough: the file map keys on size and
                        # mtime, and mtime moves.
                        source.write_text(edited)
                if not applied:
                    raise diagnose(
                        "Metro never pushed an update after the edit "
                        f"(does a fresh bundle contain it? {metro.serves_edit()})"
                    )

                # Rendering follows the reload; the tree is dumped on the way out.
                time.sleep(3)
                meaningful = stop_host(process, quit_file)
            finally:
                source.write_text(original)
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=15)

        stderr = log.read_text()
        check_output(stderr, process.returncode if meaningful else 0)

        # The exact line, not "DevSettings" anywhere: the module logs its own
        # name at INFO when it works, and plenty of other TurboModules fail to
        # load harmlessly, so a conjunction of the two substrings reports
        # success as failure. It did.
        if "Failed to load TurboModule: DevSettings" in stderr:
            raise Failure("DevSettings was not served, so no __DEV__ bundle can run")
        if not dump.exists():
            raise Failure("host wrote no widget tree")
        if AFTER not in dump.read_text():
            raise Failure(
                "the edit never reached the running app; Fast Refresh did not apply it"
            )


def editable_value(tree: str) -> str:
    """The text inside the only <TextInput> in the tree."""
    match = re.search(r'editable="([^"]*)"', tree)
    if match is None:
        raise Failure("no text field in the widget tree; is the TextInput mounted?")
    return match.group(1)


def test_text_input(bundle: Path) -> None:
    # The field is focused through the third button rather than by tapping it,
    # so this covers the focus command as well as the typing. The value is
    # controlled by React, so what ends up in the widget is only there because
    # onChange reached JavaScript and the new value came back down.
    tree = run_host(
        bundle,
        taps=taps_for(bundle, "focus the field"),
        typing="Ada",
        run_ms=9000,
    )

    if "focused" not in tree:
        raise Failure("the focus command did not move focus to the field")

    value = editable_value(tree)
    if value != "Ada":
        raise Failure(f"the field holds {value!r}, not the text that was typed")

    # The caret moved, and JavaScript was told. Asserted on every host because
    # every instrument moves the caret however it inserts the text.
    #
    # Windows is why this is asserted at all: a plain EDIT has no notification
    # for the caret moving -- EN_SELCHANGE belongs to RichEdit -- so there the
    # selection is read after anything that could have moved it, and this says
    # that reading happens.
    selections = [
        line.split("selection: ", 1)[1].strip()
        for line in LAST_HOST_OUTPUT.splitlines()
        if "selection: " in line
    ]
    if PLATFORM == "linux":
        # GTK's instrument inserts through `gtk_editable_insert_text`, and
        # `notify::cursor-position` does not fire for that -- verified by
        # instrumenting the handler, which is connected to a real GtkText with
        # no warning and simply never runs. So the caret moves and nothing says
        # so, and there is nothing here to assert rather than something broken.
        print(
            "        (GTK's typing instrument inserts text directly, and the "
            "caret notification does not fire for that)"
        )
    elif "3-3" not in selections:
        raise Failure(
            f"after typing three characters the caret should be reported at 3-3; "
            f"the selections reported were {selections}."
        )

    # One key event per key, which is the half a tree dump cannot show: a field
    # reporting the whole string each time would look identical in it. React
    # Native's contract is the typed character, with 'Enter' and 'Backspace' by
    # name.
    #
    # Only where the instrument sends keys. GTK inserts through
    # `gtk_editable_insert_text` and AppKit through the field editor -- both
    # deliberately, because a synthesised key needs a seat this run does not
    # have -- so on those two there is no key to report and nothing to assert.
    # Windows sends real WM_CHARs, which makes it the only host that can check
    # this and the only one that could have got it wrong.
    keys = [
        line.split("key: ", 1)[1].strip()
        for line in LAST_HOST_OUTPUT.splitlines()
        if "key: " in line
    ]
    if PLATFORM != "windows":
        print(
            "        (this host types by inserting text rather than by sending "
            "keys, so onKeyPress has nothing to report)"
        )
    elif not is_subsequence(["A", "d", "a"], keys):
        raise Failure(
            f"typing 'Ada' reported the keys {keys}, which is not one event per "
            "key carrying the character typed."
        )

    # The echo label is rendered from React state, so it only says this if
    # onChangeText fired. Without it the widget could hold the right text
    # purely because GtkText kept it, with JavaScript none the wiser.
    expect_contains(
        tree,
        'text="hello, Ada"',
        "onChangeText never reached React; the field is not controlled",
    )



def test_click_focuses_a_field(bundle: Path) -> None:
    """A real click into a <TextInput> focuses it.

    The one interaction nothing else here covers. "focus a TextInput, type, and
    see it round-trip" looks like it does, and does not: it taps the demo's
    *button*, which calls focus() -- so every path through this suite focuses
    programmatically, and a regression in focus-by-click would be invisible.

    The demo's field takes a different border colour when it is focused, and
    `describeTree` prints per-edge border colours, so the tree is the assertion
    and nothing new has to be rendered to check it.
    """
    if PLATFORM == "windows":
        raise Skipped("SendInput moves the runner's real cursor")
    if PLATFORM == "linux" and not real_input_available():
        raise Skipped("needs a display and xdotool; a click has to be real")
    if screen_is_locked():
        # A locked session realises no windows, so the host's accessibility
        # window list is empty and the point to click cannot be computed. What
        # came out of that was "could not find the host window after 20
        # seconds", which sounds like the host and is the screen.
        raise Skipped("the screen is locked, so the host has no realised window")

    # The surface's height, so the click can be aimed without anything here
    # knowing what a title bar measures. One short run with no input, which is
    # cheaper than guessing and cannot drift.
    root = re.search(r"view tag=\d+ frame=\(0,0 [\d.]+x([\d.]+)\)", run_host(bundle, run_ms=3000))
    if root is None:
        raise Failure("could not read the surface height from a tree dump")
    surface_height = int(float(root.group(1)))

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env = dict(os.environ)
        env["BASALT_DUMP_TREE"] = str(dump)
        env["BASALT_QUIT_AFTER_MS"] = "12000"
        env.pop("BASALT_TEST_TAP", None)
        env.pop("BASALT_TEST_TYPE", None)

        process = subprocess.Popen(
            [str(HOST), str(bundle), MODULE], cwd=REPO, env=env,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )
        # The window has to exist, and be where the system thinks it is.
        time.sleep(5)
        try:
            click_field_for_real(surface_height, process.pid)
            time.sleep(2)
        finally:
            _, stderr = process.communicate(timeout=90)
        _remember_output(stderr)
        check_output(stderr, process.returncode)

        if not dump.exists():
            raise Failure("host wrote no widget tree")
        tree = dump.read_text()

    # PALETTE[0] on every edge, which styles.fieldFocused sets and nothing else
    # in the demo uses as a border.
    if FOCUSED_BORDER not in tree:
        raise Failure(
            "the field did not take focus from a real click; its border is still "
            f"unfocused.\nOn macOS the usual cause is that the run has no "
            f"accessibility permission, so CGEventPost silently posts nothing.\n"
            f"{tree[:1200]}"
        )


def test_hover(bundle: Path) -> None:
    """A cursor that presses nothing still reaches JavaScript.

    Runs e2e/hover.js rather than the demo, because hover is the one part of the
    input path with no equivalent in React Native's touch model and the demo has
    nothing listening for it.

    What this actually proves is a division of labour. A host emits one
    `pointerMove` per motion and nothing else; React Native's own
    PointerEventsProcessor turns that into enter, leave, over and out. So the
    assertion that matters is the third move, from one box to the other: `over`
    and `out` fire at the boxes, and the card -- which contains both -- hears
    neither a leave nor an enter, because the cursor never left it. A host that
    emitted enter and leave itself would get that wrong, and would also see
    every event twice. See core/HoverTracker.h.

    Injected on every platform, unlike the tap scenarios. A real hover means
    moving the machine's actual cursor over the window and leaving it there,
    which no automated run can do without taking the pointer away from whoever
    is using the machine.
    """
    hover_bundle = bundle_app(bundle.parent, "hover")

    points = ";".join(
        f"{x},{y}" for x, y in (HOVER_LEFT_BOX, HOVER_RIGHT_BOX, HOVER_CARD_ONLY)
    )
    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "9000"
    # A negative point is the cursor leaving the surface, which no move can
    # express and which the host reports as a pointerLeave.
    env["BASALT_TEST_HOVER"] = f"{points};-1,-1"
    env.pop("BASALT_TEST_TAP", None)
    env.pop("BASALT_TEST_TYPE", None)

    result = run_host_process(
        [str(HOST), str(hover_bundle), "BasaltHover"],
        cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
    )
    _remember_output(result.stderr)
    check_output(result.stderr, result.returncode)

    # Both streams: GLib sends g_message to stdout and only warnings to stderr,
    # so on the GTK host the app's own logging is not where the other scenarios
    # look for it.
    events = [
        line.split("hover: ", 1)[1].strip()
        for line in (result.stdout + result.stderr).splitlines()
        if "hover: " in line
    ]

    expected = [
        # Into the left box: it is hovered, and so is the card around it.
        "over left", "enter card", "enter left",
        # Across to the right box.
        "out left", "leave left", "over right", "enter right",
        # Into the card, outside both boxes.
        "out right", "leave right",
        # Off the surface entirely.
        "leave card",
    ]
    # A real cursor sitting over the window when it maps has already entered the
    # card before the first scripted move lands, so the `enter card` this
    # expects in the middle of the sequence arrived at the start instead -- and
    # another arrives at the end, when the pointer is back over it. Every event
    # is present and the scripted ones are in order; what differs is an event
    # the script did not ask for.
    #
    # `is_subsequence` was written to tolerate exactly that -- "a run on a real
    # display can pick up a motion event of its own" -- and cannot here,
    # because the spurious event has the same name as a wanted one and the match
    # takes the wrong occurrence.
    #
    # Only where there is a real cursor to do it. CI runs this host under Xvfb,
    # which has no pointer, and asserts the full order; this machine runs the
    # GTK host over the quartz backend, which docs/HANDOFF.md already warns is
    # not the target. Skipped rather than loosened, so that what CI checks stays
    # exact.
    if not is_subsequence(expected, events) and PLATFORM == "linux" and sys.platform == "darwin":
        raise Skipped(
            "a real cursor over the window enters the card before the script "
            "does; this host is GTK over quartz, which is not the target"
        )
    if not is_subsequence(expected, events):
        raise Failure(
            "hover did not reach JavaScript in the expected order.\n"
            f"expected, in order: {expected}\n"
            f"got:                {events}"
        )

    # The claim the order alone does not make: crossing from one box to the
    # other must not leave the card, because the cursor stayed inside it.
    crossing = events[events.index("leave left"):events.index("enter right")]
    if "leave card" in crossing:
        raise Failure(
            "the card was left while the cursor moved between its own children; "
            "enter and leave are being treated as if they bubbled.\n"
            f"got: {events}"
        )


def test_pointer_events(bundle: Path) -> None:
    """What a press can land on, which is not the same as what is drawn.

    Runs e2e/pointerevents.js: four rows, one per value of the prop, each a
    panel with a button inside it and a backdrop behind it. Every one of the
    three reports which view it thinks was pressed, so each tap has exactly one
    right answer and a wrong implementation says which way it is wrong.

    `box-none` and `box-only` are the pair worth the app. They are the two
    everyone gets the wrong way round, and the difference between `box-none`
    and simply falling back to the parent is only visible when something is
    underneath -- which is what the backdrop is for.
    """
    app = bundle_app(bundle.parent, "pointerevents")

    # Panel-sized coordinates from the app's own layout: rows 110 tall with 12
    # between them under 24 of padding, and a panel inset 14 inside each. x=500
    # is over the panel and clear of the button; x=100 is over the button.
    taps = [(500, 80), (500, 200), (500, 320), (100, 320), (500, 440), (100, 440)]
    expected = [
        # auto: the ordinary case.
        "auto: panel",
        # none: the panel and its button are out of hit testing entirely.
        "none: backdrop",
        # box-none: transparent to a press that misses its children...
        "box-none: backdrop",
        # ...and its children are still targets.
        "box-none: button",
        # box-only: the panel takes the press wherever it lands, including
        # over the button.
        "box-only: panel",
        "box-only: panel",
    ]

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "12000"
    env["BASALT_TEST_TAP"] = ";".join(f"{x},{y}" for x, y in taps)
    env.pop("BASALT_TEST_TYPE", None)
    env.pop("BASALT_TEST_HOVER", None)

    result = run_host_process(
        [str(HOST), str(app), "BasaltPointerEvents"],
        cwd=REPO, env=env, capture_output=True, text=True, timeout=150,
    )
    _remember_output(result.stderr)
    check_output(result.stderr, result.returncode)

    # Both streams: GLib sends g_message to stdout and only warnings to stderr.
    pressed = [
        line.split("pressed ", 1)[1].strip()
        for line in (result.stdout + result.stderr).splitlines()
        if "pressed " in line
    ]
    if pressed != expected:
        raise Failure(
            "a press landed on the wrong view.\n"
            f"expected: {expected}\n"
            f"got:      {pressed}"
        )


def test_keyboard_focus(bundle: Path) -> None:
    """An app driven entirely from the keyboard.

    Runs e2e/focus.js: three buttons and a text field, in that order. The field
    is the interesting neighbour -- it takes focus because it is a real GtkText,
    NSTextField or EDIT control, and it has to sit in the same Tab order as the
    buttons around it without either side knowing about the other.

    Tab reaching a `<Pressable>` is not something React Native gives a platform
    for free: its `focusable` prop is parsed only into Android's and tvOS's
    props, and the C++ host's are a bare alias of the base ones. So `accessible`
    is the signal on all three hosts, and this is what says they agree about it.

    Activating a focused button dispatches `topClick` with an empty payload --
    what React Native for Android sends from a focusable view -- which
    Pressability turns into the `onPress` the app already handles. So the last
    assertion is that a keyboard press and a mouse press are the same press.

    Injected on every platform, unlike the tap scenarios. A real Tab needs a
    window the display server considers focused, which an automated run does not
    reliably have on any of the three.
    """
    app = bundle_app(bundle.parent, "focus")

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "14000"
    env["BASALT_TEST_FOCUS"] = "tab;activate;tab;tab;tab;shift-tab"
    env.pop("BASALT_TEST_TAP", None)
    env.pop("BASALT_TEST_TYPE", None)
    env.pop("BASALT_TEST_HOVER", None)

    result = run_host_process(
        [str(HOST), str(app), "BasaltFocus"],
        cwd=REPO, env=env, capture_output=True, text=True, timeout=180,
    )
    _remember_output(result.stderr)
    check_output(result.stderr, result.returncode)

    # Both streams: GLib sends g_message to stdout and only warnings to stderr.
    events = [
        line.split("[js] ", 1)[1].strip()
        for line in (result.stdout + result.stderr).splitlines()
        if "[js] " in line and line.split("[js] ", 1)[1].strip().split(" ")[0]
        in ("focus", "blur", "press")
    ]

    expected = [
        "focus first",
        # Enter on a focused button is the same press a click produces.
        "press first",
        "blur first",
        "focus second",
        "blur second",
        "focus third",
        # The text field is a stop like any other, in tree order.
        "blur third",
        "focus field",
        # And back the way it came.
        "blur field",
        "focus third",
    ]
    if events != expected:
        raise Failure(
            "the keyboard did not move focus where it should have.\n"
            f"expected: {expected}\n"
            f"got:      {events}"
        )


# The message the demo logs as an error, which is what tells its toast from
# the warning one above it.
ERROR_TOAST_TEXT = "an error that should raise a red box"


def _measure_toast(app: Path, label: str) -> tuple:
    """Where a LogBox toast carrying `label` is, in surface-root points.

    One extra run of the host with nothing tapped. The toasts appear a second
    and a half in, so this waits for them rather than dumping immediately.
    """
    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "4000"
    for name in ("BASALT_TEST_TAP", "BASALT_TEST_TYPE", "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS"):
        env.pop(name, None)

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env["BASALT_DUMP_TREE"] = str(dump)
        run_host_process(
            [str(HOST), str(app), "BasaltLogBox"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
        )
        if not dump.exists():
            raise Failure("the host wrote no tree while looking for the error toast")
        tree = dump.read_text()

    for body, (x, y), (width, height) in _views(tree):
        if f'text="{label}"' in body:
            return (round(x + width / 2), round(y + height / 2))
    raise Failure(
        f"no toast carrying {label!r} in the tree; did the demo stop logging it?\n"
        f"{tree[-1200:]}"
    )


def test_logbox(bundle: Path) -> None:
    """A console error opens React Native's own inspector.

    The red box is not something a host draws. It is
    `LogBoxInspectorContainer`, registered by AppRegistry under the name
    "LogBox" exactly as an app registers its own component -- so what a host
    provides is a second surface, started when the LogBox TurboModule asks. The
    toasts, which sit inside the app's own surface, have worked since <View> and
    <Text> did; the box they open needed the surface.

    A development bundle, because that is the only kind that has LogBox at all:
    a production one registers a component that renders nothing.

    The scenario taps the error toast and asserts on the inspector's own tree,
    which each host appends to the dump under `--- LogBox ---`.
    """
    app = bundle_app(bundle.parent, "logbox", dev=True)

    # Where the error toast is, asked of the app rather than assumed.
    #
    # This tapped 400,650 until the release workflow ran the suite on macOS
    # for the first time and it missed: the toast is at y=580..628 there and
    # the tap landed 22 points under it. The two hosts put LogBox's toasts at
    # different heights and one hard-coded point cannot be inside both.
    #
    # The warning toast sits above the error one, so the message text is what
    # distinguishes them -- and a tap on the text bubbles to the toast, the
    # same way the demo's button labels work.
    toast = _measure_toast(app, ERROR_TOAST_TEXT)

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "12000"
    # The first tap is a miss, and is there only to let the error arrive: the
    # app logs it a second and a half in, and taps fire a second apart.
    env["BASALT_TEST_TAP"] = f"5,5;{toast[0]},{toast[1]}"
    env.pop("BASALT_TEST_TYPE", None)
    env.pop("BASALT_TEST_HOVER", None)
    env.pop("BASALT_TEST_FOCUS", None)

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env["BASALT_DUMP_TREE"] = str(dump)
        result = run_host_process(
            [str(HOST), str(app), "BasaltLogBox"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=180,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode, allow_js_errors=True)
        if not dump.exists():
            raise Failure("host wrote no widget tree")
        tree = dump.read_text()

    if "--- LogBox ---" not in tree:
        raise Failure(
            "tapping the error toast did not open the inspector; no second "
            f"surface was started.\n{tree[-1500:]}"
        )
    inspector = tree.split("--- LogBox ---", 1)[1]

    # The inspector's own furniture, which nothing else in the tree has.
    for needle, why in (
        ('text="Console Error"', "the inspector did not name the log's level"),
        ('text="an error that should raise a red box"', "the message is missing"),
        ('text="Call Stack"', "the stack section is missing"),
    ):
        if needle not in inspector:
            raise Failure(f"{why}: expected {needle} in\n{inspector[:1500]}")

    # LogBox's icons are `require()`d images, and Metro's `build` has no
    # --assets-dest -- so without scripts/copy_assets.js they lay out at the
    # right size and draw nothing. `texture=` is what says one arrived.
    if "texture=" not in inspector:
        raise Failure(
            "the inspector's icons did not load; were the bundle's assets copied?\n"
            f"{inspector[:1500]}"
        )


def test_initial_url(bundle: Path) -> None:
    """`Linking.getInitialURL()` answers with what the desktop launched the app
    with.

    A desktop hands a URL over on the command line -- a `.desktop` entry's `%u`,
    a registered scheme on macOS, a shell association on Windows -- so the host
    records whichever argument carried a scheme and the module answers with it.
    Run twice, because both answers matter: an app that was not opened with a
    URL must get null rather than an empty string, which is what React Native's
    own JavaScript checks for.

    What this does not cover is a URL delivered to an application that is
    *already* running. That needs single-instance activation on each desktop and
    is a separate piece of work; see docs/BACKLOG.md.
    """
    app = bundle_app(bundle.parent, "modules")

    def initial_url_for(arguments: list[str]) -> str:
        env = dict(os.environ)
        env["BASALT_QUIT_AFTER_MS"] = "6000"
        env.pop("BASALT_TEST_TAP", None)
        env.pop("BASALT_TEST_TYPE", None)
        env.pop("BASALT_TEST_HOVER", None)
        env.pop("BASALT_TEST_FOCUS", None)
        result = run_host_process(
            [str(HOST), str(app), "BasaltModules", *arguments],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        # Both streams: GLib sends g_message to stdout and only warnings to
        # stderr.
        for line in (result.stdout + result.stderr).splitlines():
            if "initialURL: " in line:
                return line.split("initialURL: ", 1)[1].strip()
        raise Failure("the app never reported an initial URL")

    launched = "basalt://opened-by-the-desktop"
    if initial_url_for([launched]) != launched:
        raise Failure(
            f"launched with {launched}, and getInitialURL did not say so: "
            f"{initial_url_for([launched])!r}"
        )
    if initial_url_for([]) != "null":
        raise Failure(
            "an app launched with no URL must get null rather than a string: "
            f"{initial_url_for([])!r}"
        )


def test_share(bundle: Path) -> None:
    """`Share.share()` reaches the platform and settles both ways.

    Two things were broken and only one of them was a missing module.
    `Share.js` branches on `Platform.OS` being exactly `android` or `ios` and
    rejects with "Unsupported platform" otherwise, so no desktop module was ever
    reached; the replacement is in packages/basalt-core/src/overrides.

    Run twice, because a share sheet has two answers and an app is expected to
    handle both. BASALT_TEST_DIALOG answers the picker without showing one --
    see native/core/TestDialog.h for why that rather than driving a real dialog,
    and for what it skips.

    On the desktops with no share service the picker is built from a clipboard
    and a mail client, so "Copy" is checked by reading the clipboard back:
    what is asserted there is that the picker ran, not merely that a promise
    settled. macOS shows NSSharingServicePicker instead, which the instrument
    answers without running any of that.
    """
    app = bundle_app(bundle.parent, "share")

    def outcome_for(answer: str) -> tuple[str, str]:
        env = dict(os.environ)
        env["BASALT_QUIT_AFTER_MS"] = "8000"
        env["BASALT_TEST_DIALOG"] = answer
        for name in ("BASALT_TEST_TAP", "BASALT_TEST_TYPE", "BASALT_TEST_HOVER",
                     "BASALT_TEST_FOCUS"):
            env.pop(name, None)
        result = run_host_process(
            [str(HOST), str(app), "BasaltShare"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        both = result.stdout + result.stderr
        action = ""
        clipboard = ""
        for line in both.splitlines():
            if "share: action " in line:
                action = line.split("share: action ", 1)[1].strip()
            if "clipboard: " in line:
                clipboard = line.split("clipboard: ", 1)[1].strip()
        if not action:
            raise Failure(f"the share never settled:\n{tail_text(both, 30)}")
        return action, clipboard

    # Button 0 is "Copy" in the fallback picker, and any chosen service on macOS.
    shared, clipboard = outcome_for("0")
    if shared != "sharedAction":
        raise Failure(f"a share that was accepted reported {shared!r}")

    if PLATFORM != "macos":
        # The picker really ran: this is what its "Copy" did.
        if "React Native on the desktop" not in clipboard:
            raise Failure(
                "the picker resolved but nothing reached the clipboard: "
                f"{clipboard!r}"
            )

    dismissed, _ = outcome_for("dismiss")
    if dismissed != "dismissedAction":
        raise Failure(
            "a dismissed share must resolve rather than reject, and with "
            f"dismissedAction; got {dismissed!r}"
        )


def test_alert(bundle: Path) -> None:
    """`Alert.alert()` shows a dialog and reports which button was pressed.

    It did neither, on any of the three desktops, and had not since the module
    was written: `Alert.alert` branches on `Platform.OS` being exactly `ios` or
    `android` with no else, and `RCTAlertManager` resolves to its Android
    sibling, which calls a module this platform does not have. Two breaks in one
    chain, both silent. Found while implementing Share, which fails the same way
    for the same reason.

    e2e/alert.js logs which button its handler ran, so the assertion is on the
    answer travelling back rather than on a dialog being on screen.
    """
    app = bundle_app(bundle.parent, "alert")

    def chose(answer: str) -> str:
        env = dict(os.environ)
        env["BASALT_QUIT_AFTER_MS"] = "6000"
        env["BASALT_TEST_DIALOG"] = answer
        for name in ("BASALT_TEST_TAP", "BASALT_TEST_TYPE", "BASALT_TEST_HOVER",
                     "BASALT_TEST_FOCUS"):
            env.pop(name, None)
        result = run_host_process(
            [str(HOST), str(app), "BasaltAlert"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        both = result.stdout + result.stderr
        for line in both.splitlines():
            if "chose: " in line:
                return line.split("chose: ", 1)[1].strip()
        raise Failure(f"the alert's callback never reached JavaScript:\n{tail_text(both, 30)}")

    # e2e/alert.js offers Cancel then Delete, in that order.
    if chose("0") != "Cancel":
        raise Failure(f"pressing the first button reported {chose('0')!r}")
    if chose("1") != "Delete":
        raise Failure(f"pressing the second button reported {chose('1')!r}")
    # `dismiss` is the last button, which is where the way out lives.
    if chose("dismiss") != "Delete":
        raise Failure("dismiss should answer with the last button")


class NotificationService:
    """A session bus with a stand-in notification daemon on it.

    The one thing about notifications that cannot be asserted without a service
    running is that a notification is actually sent, and neither a developer's
    Mac nor a CI runner has a desktop's own daemon. So the suite starts both: a
    plain `dbus-daemon` -- not `dbus-run-session`, which on macOS insists on
    launchd's socket and fails -- and `basalt_notification_stub` on it.

    A context manager rather than a fixture, so the bus goes away with the test
    even when it fails.
    """

    def __init__(self, build: Path, directory: Path) -> None:
        self.stub_binary = build / "basalt_notification_stub"
        self.directory = directory
        self.address: str | None = None
        self.log = directory / "stub.log"
        self._bus: subprocess.Popen | None = None
        self._stub: subprocess.Popen | None = None

    def available(self) -> bool:
        return self.stub_binary.exists() and shutil.which("dbus-daemon") is not None

    def __enter__(self) -> "NotificationService":
        config = self.directory / "session.conf"
        # A unix socket in a temporary directory, and a policy that allows
        # everything: this bus exists for one test and has one client on it.
        config.write_text(
            "<!DOCTYPE busconfig PUBLIC "
            '"-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN" '
            '"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">\n'
            "<busconfig><type>session</type>"
            "<listen>unix:tmpdir=/tmp</listen>"
            '<policy context="default">'
            '<allow send_destination="*"/><allow own="*"/><allow receive_sender="*"/>'
            "</policy></busconfig>\n"
        )
        started = subprocess.run(
            ["dbus-daemon", "--config-file", str(config), "--print-address", "--fork",
             "--print-pid"],
            capture_output=True, text=True, timeout=30,
        )
        lines = started.stdout.split()
        if started.returncode != 0 or not lines:
            raise Failure(f"could not start a session bus: {started.stderr.strip()}")
        self.address = lines[0]

        environment = dict(os.environ)
        environment["DBUS_SESSION_BUS_ADDRESS"] = self.address
        self._stub = subprocess.Popen(
            [str(self.stub_binary)], env=environment,
            stdout=self.log.open("w"), stderr=subprocess.STDOUT,
        )
        # Owning the name is asynchronous, and the host asks whether anyone owns
        # it before sending anything.
        deadline = time.time() + 10
        while time.time() < deadline:
            if self.log.exists() and "STUB ready" in self.log.read_text():
                return self
            time.sleep(0.1)
        raise Failure("the notification stub never took the bus name")

    def __exit__(self, *_: object) -> None:
        if self._stub is not None:
            self._stub.terminate()
            self._stub.wait(timeout=10)
        # The bus forked, so it is not a child of this process; it is told to go
        # by closing its socket, which happens when the directory is removed.
        subprocess.run(["pkill", "-f", "dbus-daemon --config-file " + str(self.directory)],
                       capture_output=True)

    def saw(self) -> str:
        return self.log.read_text() if self.log.exists() else ""


def packaged_host(build: Path) -> Path:
    """The host as the platform's own idea of an application, where that differs.

    macOS is the one that differs and the one this exists for: an executable
    with no bundle around it has no bundle identifier, and
    `UNUserNotificationCenter` refuses a process without one -- so the same code
    answers `denied` out of a build directory and `granted` inside a `.app`.
    Running the bundled copy is the only way a scenario can see the second.

    Everything else returns the host it was given, because on Linux and Windows
    nothing about where the executable sits decides anything.
    """
    if PLATFORM != "macos":
        return HOST

    # Plainly `subprocess.run`: this is node, packaging the host rather than being
    # it, so there is no crash handler at the other end to ask for a backtrace.
    node = subprocess.run(
        ["node", "-e",
         "const {packageApp} = require(process.argv[1]);"
         "process.stdout.write(packageApp({"
         "platform: 'macos', hostBinary: process.argv[2],"
         "outputDir: process.argv[3], projectRoot: process.argv[4]}).launchPath);",
         str(REPO / "packages/basalt-core/dist/cli/packageApp.js"),
         str(HOST), str(build / "app"), str(REPO / "e2e")],
        cwd=REPO, capture_output=True, text=True, timeout=120,
    )
    if node.returncode != 0:
        raise Failure(f"could not package the host:\n{node.stderr}")
    return Path(node.stdout.strip())


def test_notifications(bundle: Path) -> None:
    """An app using `expo-notifications` gets answers rather than an exception.

    React Native has no notification API to port, so the contract implemented is
    expo-notifications' -- the same argument gesture-handler and expo-clipboard
    were ported on. The package's JavaScript is unchanged; what this platform
    supplies is the native modules under it.

    Skipped unless BASALT_EXPO_APP names an app with expo-notifications
    installed *and* the host was built against that app's expo-modules-core with
    -DBASALT_EXPO_MODULES_CORE. Neither is true of a plain checkout, and CI does
    not build with Expo at all.

    What is asserted is everything that does not need a notification service to
    be running:

      - the package imports, which needs all thirteen native modules to exist,
        because `requireNativeModule` throws on a name it cannot find;
      - `getPermissionsAsync` answers, and carries a reason when it says no;
      - a rejected send carries that same reason rather than an empty failure;
      - a method this platform does not implement is reported by name.

    Where `dbus-daemon` and `basalt_notification_stub` are both there, the suite
    starts a session bus with a stand-in daemon on it and asserts the rest: that
    the permission is granted, that the send reaches the service, and that it
    carries the app's own words. That is the whole contract, and it is the only
    way to exercise it -- a real desktop's daemon is not present on a Mac or on
    a CI runner.
    """
    expo_app = os.environ.get("BASALT_EXPO_APP")
    if not expo_app or not (Path(expo_app) / "node_modules" / "expo-notifications").exists():
        raise Skipped("needs BASALT_EXPO_APP naming an app with expo-notifications")

    app = bundle_app(bundle.parent, "notifications")
    host = packaged_host(bundle.parent)

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "8000"
    for name in ("BASALT_TEST_TAP", "BASALT_TEST_TYPE", "BASALT_TEST_HOVER",
                 "BASALT_TEST_FOCUS", "BASALT_TEST_DIALOG"):
        env.pop(name, None)

    with tempfile.TemporaryDirectory() as directory:
        service = NotificationService(bundle.parent, Path(directory))
        # A session bus with a daemon on it where one can be had, so that the
        # send itself is exercised rather than only the refusal. Without it the
        # host reports why it cannot, which is the other half worth asserting.
        #
        # Linux only, even on a machine that has dbus installed: it is the only
        # host that sends over D-Bus, and a daemon listening beside a macOS run
        # proves nothing except that it was listening.
        if PLATFORM == "linux" and service.available():
            with service:
                env["DBUS_SESSION_BUS_ADDRESS"] = service.address or ""
                result = run_host_process(
                    [str(host), str(app), "BasaltNotifications"],
                    cwd=REPO, env=env, capture_output=True, text=True, timeout=180,
                )
                sent = service.saw()
        else:
            sent = ""
            result = run_host_process(
                [str(host), str(app), "BasaltNotifications"],
                cwd=REPO, env=env, capture_output=True, text=True, timeout=180,
            )

    _remember_output(result.stderr)
    check_output(result.stderr, result.returncode)
    both = result.stdout + result.stderr

    said = {}
    for line in both.splitlines():
        if "notifications: " in line:
            rest = line.split("notifications: ", 1)[1].strip()
            key, _, value = rest.partition(" ")
            said[key] = value

    if "imported" not in said:
        raise Failure(
            "expo-notifications did not import; a native module it asks for is "
            f"missing.\n{tail_text(both, 30)}"
        )
    if said.get("status") not in ("granted", "denied"):
        raise Failure(f"getPermissionsAsync answered {said.get('status')!r}")

    if said["status"] == "denied":
        # The reason is the whole value of answering `denied` rather than
        # throwing: an app, or the person reading the log, can tell why.
        if not said.get("reason"):
            raise Failure("a denied permission must say why")
        if not said.get("rejected", "").endswith(said["reason"]):
            raise Failure(
                "a send that could not happen should be rejected with the same "
                f"reason the permission gave; got {said.get('rejected')!r}"
            )
    else:
        if said.get("scheduled") != "true":
            raise Failure("a granted platform did not schedule a notification")
        if said.get("presented") != "1":
            raise Failure(
                f"one notification was sent and {said.get('presented')} are presented"
            )
        # What the daemon actually received, which is the only thing that says
        # the D-Bus call was made and carried the app's words. Only Linux has
        # one: macOS hands the request to UNUserNotificationCenter, where what
        # happens next depends on a permission a person grants in System
        # Settings and an automated run cannot. What the macOS run does assert
        # is everything above -- that a bundled host answers `granted` where an
        # unbundled one cannot, and that the send was accepted.
        if PLATFORM != "linux" or not sent:
            return
        if "Notify" not in sent:
            raise Failure(f"the notification service was never asked to show one:\n{sent}")
        if "A notification from a desktop" not in sent:
            raise Failure(f"the notification reached the service without its body:\n{sent}")
        if "CloseNotification" not in sent:
            raise Failure(f"dismissing a notification did not reach the service:\n{sent}")

    # Android's channels, which no desktop has. expo's own check reports it by
    # name because the method is left off rather than stubbed -- see
    # native/core/ExpoModules.h.
    if "channels" not in said:
        raise Failure("an unimplemented method neither answered nor reported itself")


def test_macos_key_props(bundle: Path) -> None:
    """An app declares a shortcut the way react-native-macos does, and it fires.

    `keyDownEvents`, `onKeyDown` and `focusable` spread onto a plain <View>, with
    nothing from this package in the app -- which is the point. react-native-macos
    is what a desktop React Native app is most likely already written against,
    and an app that has to be rewritten to run here is an app this platform did
    not replace.

    The other spelling, `<KeyHandler>`, has its own scenario. Both go through one
    implementation; see src/useHandledKeys.ts.

    The callback is asserted to receive `{nativeEvent}`, which is that platform's
    shape: an app reading `event.nativeEvent.key` would see undefined otherwise,
    and undefined is exactly what a dropped prop looks like from the app's side.
    """
    app = bundle_app(bundle.parent, "macoskeys")
    host = packaged_host(bundle.parent)

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "9000"
    # A letter, a space and a combination: the three shapes a binding takes, and
    # the space is the one a naive parser loses.
    env["BASALT_TEST_KEY"] = "j; ;z+meta"
    for name in ("BASALT_TEST_TAP", "BASALT_TEST_TYPE", "BASALT_TEST_HOVER",
                 "BASALT_TEST_FOCUS", "BASALT_TEST_DIALOG"):
        env.pop(name, None)

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env["BASALT_DUMP_TREE"] = str(dump)
        result = run_host_process(
            [str(host), str(app), "BasaltMacosKeys"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=180,
        )
        tree = dump.read_text() if dump.exists() else ""

    _remember_output(result.stderr)
    check_output(result.stderr, result.returncode)
    both = result.stdout + result.stderr

    pressed = [line.split("macoskeys: down ", 1)[1].strip()
               for line in both.splitlines() if "macoskeys: down " in line]
    if not pressed:
        raise Failure(
            "no key reached the app, so `keyDownEvents` on a plain <View> did "
            f"nothing -- which is the failure this scenario exists for.\n{tail_text(both, 30)}"
        )
    if not any(entry.startswith('"j"') for entry in pressed):
        raise Failure(f"a letter binding did not fire: {pressed}")
    if not any(entry.startswith('" "') for entry in pressed):
        raise Failure(f"a space binding did not fire: {pressed}")
    if not any("meta=true" in entry for entry in pressed):
        raise Failure(f"a combination did not fire with its modifier: {pressed}")

    # And that the app's own state moved, not only that a callback ran: the
    # handler reads `event.nativeEvent.key`, so a wrong payload shape shows up
    # here as the word "undefined".
    if 'pressed: j, ,z' not in tree and 'pressed: j,' not in tree:
        raise Failure(f"the app did not record the presses it was given:\n{tail_text(tree, 20)}")


def test_subprocess(bundle: Path) -> None:
    """A capability package's TurboModule runs a command and reports it.

    Two things at once, and both are only testable from here. That a package can
    contribute a *TurboModule* -- the chain core/PackageModules.h generates is
    built by CMake and nothing else in this repository reaches it. And that the
    subprocess capability works through JavaScript rather than only through the
    unit tests beside it, which call the seam directly.

    The command is written for both shells, and the variable is passed rather
    than written into it, which is the thing that makes one command string run on
    all three desktops; see basalt-subprocess/native/Subprocess.h.
    """
    app = bundle_app(bundle.parent, "subprocess")
    host = packaged_host(bundle.parent)

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "9000"
    for name in ("BASALT_TEST_TAP", "BASALT_TEST_TYPE", "BASALT_TEST_HOVER",
                 "BASALT_TEST_FOCUS", "BASALT_TEST_DIALOG"):
        env.pop(name, None)

    result = run_host_process(
        [str(host), str(app), "BasaltSubprocess"],
        cwd=REPO, env=env, capture_output=True, text=True, timeout=180,
    )
    _remember_output(result.stderr)
    check_output(result.stderr, result.returncode)
    both = result.stdout + result.stderr

    said = {}
    for line in both.splitlines():
        if "subprocess: " in line:
            rest = line.split("subprocess: ", 1)[1].strip()
            key, _, value = rest.partition(" ")
            said[key] = value

    if said.get("supported") != "true":
        raise Failure(
            "the subprocess package's TurboModule was not reachable from "
            f"JavaScript, so the generated provider chain did not offer it.\n{tail_text(both, 30)}"
        )
    if said.get("spawned") != "true":
        raise Failure(f"spawn did not answer a process id.\n{tail_text(both, 30)}")
    if said.get("runningBefore") != "true":
        raise Failure("a process that had just started was not reported as running")
    if said.get("exitPid") != "true":
        raise Failure("the exit was reported for a process id that was not the one spawned")
    if said.get("code") != "0":
        raise Failure(f"the command exited {said.get('code')!r} rather than 0")
    # The whole point of `env`: the variable reached the command without being
    # part of the command string, which is what lets one string run everywhere.
    if "hello-from-a-subprocess" not in said.get("output", ""):
        raise Failure(
            f"the command's output did not arrive: {said.get('output')!r}\n{tail_text(both, 30)}"
        )
    if said.get("runningAfter") != "false":
        raise Failure("a process that had exited was still reported as running")


def test_expo_fetch(bundle: Path) -> None:
    """An Expo app calls `fetch` and gets one that works.

    Expo replaces `globalThis.fetch` with its own WinterCG implementation, on a
    native module -- `ExpoFetchModule` -- that this platform has no port of. It
    installs the replacement as a lazy getter, so an app fails at its first call
    rather than at import, inside the global it was calling and where it cannot
    catch it. That is what killed kino: the error naming the module, and then a
    TypeError from the component whose data never arrived.

    So the platform sets expo's own `EXPO_PUBLIC_USE_RN_FETCH` before the bundle
    runs, which leaves React Native's fetch in place -- and React Native's fetch
    works here. See native/core/ExpoRuntime.cpp.

    **What this covers is the arrival**, which is the fragile half: the value is
    written from native before the bundle, and both Metro's prelude and React
    Native's setUpGlobals keep an existing `process.env` only because they say
    `|| {}`. A change to either would drop it silently, and this is what would
    notice.

    It does not cover expo honouring the variable. This app does not import
    `expo`, and importing it does not help -- this repository's own bundler does
    not pull expo's winter runtime into a e2e/ app, so the replacement never runs
    here. That half was checked by running kino against a real Expo bundle: with
    the default it boots clean, and without it the first `fetch` dies naming the
    module.

    Skipped unless BASALT_EXPO_APP names an app, because the behaviour only
    exists when the host was built against an expo-modules-core; without one
    there is no Expo runtime and nothing sets the default.
    """
    expo_app = os.environ.get("BASALT_EXPO_APP")
    if not expo_app or not (Path(expo_app) / "node_modules" / "expo").exists():
        raise Skipped("needs BASALT_EXPO_APP naming an app with expo installed")

    app = bundle_app(bundle.parent, "expofetch")
    host = packaged_host(bundle.parent)

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "8000"
    for name in ("BASALT_TEST_TAP", "BASALT_TEST_TYPE", "BASALT_TEST_HOVER",
                 "BASALT_TEST_FOCUS", "BASALT_TEST_DIALOG"):
        env.pop(name, None)

    result = run_host_process(
        [str(host), str(app), "BasaltExpoFetch"],
        cwd=REPO, env=env, capture_output=True, text=True, timeout=180,
    )
    _remember_output(result.stderr)
    check_output(result.stderr, result.returncode)
    both = result.stdout + result.stderr

    said = {}
    for line in both.splitlines():
        if "fetch: " in line:
            rest = line.split("fetch: ", 1)[1].strip()
            key, _, value = rest.partition(" ")
            said[key] = value

    if said.get("useRnFetch") != "1":
        raise Failure(
            "the platform's default for expo's own fetch switch did not reach the "
            f"bundle: EXPO_PUBLIC_USE_RN_FETCH was {said.get('useRnFetch')!r}. "
            "Metro's prelude and React Native's setUpGlobals both keep an existing "
            f"process.env; something replaced it.\n{tail_text(both, 30)}"
        )
    if "global" not in said:
        raise Failure(
            "reading globalThis.fetch threw, which is the failure this scenario "
            f"exists for.\n{tail_text(both, 30)}"
        )
    if said["global"] != "function":
        raise Failure(f"globalThis.fetch was {said['global']!r} rather than a function")
    # Expo brands what it installs. False means React Native's own is what the
    # app would call. Weak evidence in this bundle, where expo's runtime never
    # ran anyway -- it is here so that the day the harness does bundle it, this
    # is already the assertion that catches the regression.
    if said.get("expo") != "false":
        raise Failure(
            f"fetch is expo's ({said.get('expo')!r}), and ExpoFetchModule is not ported"
        )
    # And that the call reached something. Connection refused arrives as a
    # rejected promise; anything else means fetch is present and inert.
    if "rejected" not in said and "status" not in said:
        raise Failure(
            f"fetch neither answered nor rejected, so nothing ran.\n{tail_text(both, 30)}"
        )


def test_controls(bundle: Path) -> None:
    """The four components that are a control rather than a box.

    Runs e2e/controls.js, which has one of each: three <ActivityIndicator>s, three
    <Switch>es, a <Modal> and a <RefreshControl> inside a <ScrollView>. Three
    short runs rather than one, because each needs a different instrument and
    the three hosts do not agree on what order two instruments run in -- a
    dependency worth not having.

    What each run is really asserting:

      mounted     that all four reached the view layer at all. Until this
                  existed the registry substituted UnimplementedNativeView for
                  every one of them, which renders as nothing and says nothing.
                  The `control=` field is written by core/DesktopControls.h, so
                  three hosts cannot describe the same switch differently.
      switch      that a press becomes `onValueChange` and that the *app* is
                  what moves the switch. A host that let its own GtkSwitch or
                  NSSwitch move would pass a screenshot and fail this.
      modal       that `visible` mounts an overlay over the whole surface, that
                  `onShow` fires, and that Escape reaches `onRequestClose`
                  rather than closing the modal behind the app's back.
      refresh     that a wheel which keeps asking to go up after the list has
                  reached its top fires `onRefresh` -- once. See
                  core/PullToRefresh.h for why a desktop counts the wheel
                  instead of measuring a pull.
    """
    app = bundle_app(bundle.parent, "controls")

    def run(run_ms: int, **instruments: str) -> tuple[str, str]:
        with tempfile.TemporaryDirectory() as directory:
            dump = Path(directory) / "tree.txt"
            env = dict(os.environ)
            env["BASALT_DUMP_TREE"] = str(dump)
            env["BASALT_QUIT_AFTER_MS"] = str(run_ms)
            for name in ("BASALT_TEST_TAP", "BASALT_TEST_TYPE", "BASALT_TEST_HOVER",
                         "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL"):
                env.pop(name, None)
            env.update(instruments)
            result = run_host_process(
                [str(HOST), str(app), "BasaltControls"],
                cwd=REPO, env=env, capture_output=True, text=True,
                timeout=run_ms / 1000 + 60,
            )
            _remember_output(result.stderr)
            check_output(result.stderr, result.returncode)
            # Both streams: GLib sends g_message to stdout and only warnings to
            # stderr.
            return dump.read_text() if dump.exists() else "", result.stdout + result.stderr

    # --- everything mounts, and says what state it is in ---------------------
    #
    # Nothing is pressed in this run: what it is asserting is the state the app
    # asked for, which a run that had already toggled a switch could not.
    tree, _ = run(6000)

    for expected in ("control=spinner:animating",
                     "control=spinner-large:animating",
                     "control=spinner:stopped",
                     "control=switch:off",
                     "control=switch:on",
                     "control=switch:on:disabled",
                     "control=refresh:idle"):
        if expected not in tree:
            raise Failure(
                f"no view in the tree reports {expected}.\n"
                "A component the registry does not know about is substituted by\n"
                "UnimplementedNativeView, which renders as nothing at all.\n"
                f"{tree}"
            )

    # --- a press on a switch is React's to answer ----------------------------
    #
    # The coordinates come from the app's own layout: 24 of padding, a 22-tall
    # label, a 56-tall row, another label, then the switch row -- so the first
    # switch's cell is 90 wide starting at x=24 and its middle is (69, 152).
    tree, logged = run(7000, BASALT_TEST_TAP="69,152")

    if "switch one -> true" not in logged:
        raise Failure(
            "pressing a <Switch> did not reach onValueChange.\n"
            f"{tail_text(logged)}"
        )
    if "switch three ->" in logged:
        raise Failure("a disabled <Switch> reported a change")
    # Two switches were on when the app started; the pressed one makes three.
    # This is the half that says React moved it: a host whose own GtkSwitch or
    # NSSwitch flipped itself would have got here without the prop changing.
    if tree.count("control=switch:on") != 3:
        raise Failure(
            "the switch did not follow its own prop after the press.\n"
            f"{tree}"
        )

    # --- a switch is reachable from the keyboard -----------------------------
    #
    # React Native's `focusable` prop never reaches this platform, so
    # `accessible` is the signal -- and <Switch> does not set it, because on a
    # phone the native control is focusable by being a control. On a desktop a
    # switch Tab skips is broken, so being a control is the signal here too.
    tree, logged = run(9000, BASALT_TEST_FOCUS="tab;activate")

    if "switch one -> true" not in logged:
        raise Failure(
            "Tab did not reach a <Switch>, or Enter did not toggle it.\n"
            f"{tail_text(logged)}"
        )
    # The disabled one is not a stop. Every desktop skips a control that cannot
    # be operated rather than stopping on one that does nothing.
    for line in tree.splitlines():
        if "control=switch:on:disabled" in line and "focusable" in line:
            raise Failure(f"a disabled <Switch> is in the tab order:\n{line}")

    # --- the modal opens, and Escape asks the app to close it ----------------
    _, logged = run(9000, BASALT_TEST_TAP="114,226", BASALT_TEST_FOCUS="escape")

    events = [
        line.split("[js] ", 1)[1].strip()
        for line in logged.splitlines()
        if "[js] " in line and line.split("[js] ", 1)[1].strip().startswith(
            ("opening modal", "modal "))
    ]
    expected = ["opening modal", "modal shown", "modal close requested"]
    if not is_subsequence(expected, events):
        raise Failure(
            "the modal did not open and answer Escape.\n"
            f"expected, in order: {expected}\n"
            f"got:                {events}"
        )

    # --- a wheel past the top of a list is this platform's pull --------------
    #
    # Two notches of 53 pixels each, which is past the 80 in
    # core/DesktopControls.h. Negative is up, the direction contentOffset reads.
    _, logged = run(9000, BASALT_TEST_SCROLL="400,400,-1;400,400,-1;400,400,-1")

    if "refresh requested" not in logged:
        raise Failure(
            "a wheel past the top of the list did not fire onRefresh.\n"
            f"{tail_text(logged)}"
        )
    # Once, not once a notch. React Native's contract is one call per pull, and
    # a missing latch is invisible except as a stream of requests.
    if logged.count("refresh requested") != 1:
        raise Failure(
            "onRefresh fired "
            f"{logged.count('refresh requested')} times for one pull; it must fire once.\n"
            f"{tail_text(logged)}"
        )


def test_dev_menu(bundle: Path) -> None:
    """React Native's developer menu, which on a desktop is a keyboard shortcut.

    Runs the host in dev mode against a real Metro, opens the menu the way
    Ctrl+D does, and picks an item. Two runs, because the two items worth
    asserting on prove different halves:

      Reload                    that the menu reaches JavaScript at all. The
                                reload lives on ReactHost's private
                                `reloadReactInstance` and the only thing wired
                                to it is React Native's own DevSettings module,
                                so the host asks JavaScript to make the call --
                                through a device event that
                                `src/overrides/setUpDeveloperTools.js` listens
                                for. If that override is not in the bundle,
                                nothing happens and nothing says so, which is
                                exactly what this catches.

      Toggle Element Inspector  that the inspector renders. It needs no platform
                                code -- it is React Native's own event and its
                                own React views -- so what this really asserts
                                is that a host mounts enough of React Native for
                                its developer tools to work unmodified.

    Injected at the host's own dev-menu entry point rather than through a real
    Ctrl+D, for the same reason every other keyboard scenario is: a real
    keystroke needs a window the display server considers focused. What it
    skips is the delivery of the keystroke and nothing above it.

    BASALT_TEST_MENU answers the menu, because a popup nobody dismisses stops an
    automated run where it stands -- on macOS `popUpMenuPositioningItem` runs
    its tracking loop on the main thread. See core/TestDialog.h.
    """
    def run(choice: str, run_ms: int) -> tuple[str, str]:
        with tempfile.TemporaryDirectory() as directory:
            dump = Path(directory) / "tree.txt"
            log = Path(directory) / "host.log"
            env = dict(os.environ)
            env["BASALT_DUMP_TREE"] = str(dump)
            env["BASALT_QUIT_AFTER_MS"] = str(run_ms)
            env["BASALT_DEV"] = "1"
            env["BASALT_DEV_PORT"] = str(METRO_PORT)
            env["BASALT_TEST_FOCUS"] = "devmenu"
            env["BASALT_TEST_MENU"] = choice
            for name in ("BASALT_TEST_TAP", "BASALT_TEST_TYPE", "BASALT_TEST_HOVER",
                         "BASALT_TEST_SCROLL"):
                env.pop(name, None)

            with log.open("w") as sink:
                process = subprocess.Popen(
                    [str(HOST), str(bundle), MODULE],
                    cwd=REPO, env=env, stdout=subprocess.DEVNULL, stderr=sink, text=True,
                )
                process.wait(timeout=run_ms / 1000 + 90)
            text = log.read_text()
            _remember_output(text)
            check_output(text, process.returncode)
            return (dump.read_text() if dump.exists() else ""), text

    with Metro(Path(tempfile.gettempdir()) / "basalt-devmenu-metro.log"):
        # Item 0 is Reload. The app running a second time is the whole
        # assertion: the instance was torn down and rebuilt.
        _, logged = run("0", 20000)
        running = logged.count(f'Running "{MODULE}"')
        if running < 2:
            raise Failure(
                "the dev menu's Reload did not reload the app.\n"
                f'"Running \"{MODULE}\"" appears {running} time(s); it should appear twice.\n'
                f"{tail_text(logged)}"
            )

        # Item 1 is Toggle Element Inspector. React Native's own inspector
        # panel, rendered out of ordinary views this host already mounts.
        tree, logged = run("1", 16000)
        if "Tap something to inspect it" not in tree:
            raise Failure(
                "toggling the element inspector rendered nothing.\n"
                "React Native's inspector is ordinary React views, so this is a\n"
                "statement about mounting rather than about developer tools.\n"
                f"{tree}"
            )


def test_context_menu_role(bundle: Path) -> None:
    """That a role *does* something, and not only that it reports an index.

    A role is the Copy that actually copies: the behaviour is the whole reason it
    exists, and an app cannot implement it itself. The menu scenario above asserts
    that a role item comes back with its index and runs its handler, which says
    the plumbing is connected and nothing about whether the platform acted.

    Two roles are driven here, because they prove different halves.

    `paste` is the text one, and the interesting one: a role acts on whatever has
    focus, so there has to be something focused before the tap that opens the
    menu. `autoFocus` does that, as of 2026-10-07 -- until then it silently did
    nothing, which is what blocked this assertion and left the gap recorded in the
    archived change. The app puts a known string on the clipboard, never writes it
    into the field, and the field containing it afterwards is the platform's doing
    and nobody else's.

    `close` is the window one, and needs no focus: choosing it ends the host in
    about two seconds against an eight-second quit timer.

    The control run is what makes either mean anything. The same menu is driven
    with an ordinary item, and must leave the host running *and* leave the field
    alone -- a host that died early, or a field that somehow had the string in it
    all along, would otherwise pass both assertions above.
    """
    app = bundle_app(bundle.parent, "menu")

    def choose(index: str, timeout: float = 120) -> tuple[float, str]:
        env = dict(os.environ)
        env["BASALT_QUIT_AFTER_MS"] = "8000"
        # The button at (134, 70): 24 of padding, a 22-tall label, then a 220x48
        # button. The same point the menu scenario presses.
        env["BASALT_TEST_TAP"] = "134,70"
        env["BASALT_TEST_MENU"] = index
        for name in ("BASALT_TEST_SECONDARY_TAP", "BASALT_TEST_TYPE", "BASALT_TEST_HOVER",
                     "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL", "BASALT_TEST_CLOSE_WINDOW"):
            env.pop(name, None)
        started = time.monotonic()
        result = run_host_process(
            [str(HOST), str(app), "BasaltContextMenu"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=timeout,
        )
        elapsed = time.monotonic() - started
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        return elapsed, result.stdout + result.stderr

    # --- a text role: entry 12 carries `role: 'paste'` ------------------------
    #
    # Skipped where its preconditions are not met, rather than failed, and on the
    # conditions themselves rather than on the platform: GTK passes this on a
    # developer's Mac, which is the quartz backend and not an X server at all,
    # and fails its preconditions under the Xvfb CI runs on, so a platform check
    # would throw away the coverage that works.
    #
    # A missing capability skips; a hang does not. This used to skip on both,
    # and a5b6dea is what that cost: the host hung here, the scenario skipped,
    # and the Linux job reported success with the bug in it. A clipboard that
    # cannot round-trip is a thing this environment does not have. A host that
    # does not exit is the serious bug in docs/backlog/testing.md, and a suite
    # that skips on it is the same fault as the cancelled job that reads as a
    # job that ran, recorded in that same file.
    #
    # This is expected to be red on Linux until the hang is fixed, which is the
    # point of it.
    try:
        _, logged = choose("12", timeout=40)
    except subprocess.TimeoutExpired as expired:
        raise Failure(
            "the host did not exit after a run that reads the clipboard. This is "
            "the hang in docs/backlog/testing.md, which skipped here until "
            "2026-10-07 and so went unseen on a green job.\n"
            + "\n".join(f"        {line}"
                         for line in hang_text(expired.stderr).splitlines())
        )
    if "clipboard reads back: PASTEDBYROLE" not in logged:
        raise Skipped(
            "the clipboard does not round-trip in this environment, so a role has "
            "nothing to paste and nothing here would be about roles. See "
            "docs/backlog/testing.md"
        )
    if "field focused" not in logged:
        # A failure, not a skip, since 2026-10-07. autoFocus takes on all three
        # hosts today -- this scenario passes on Windows and reaches the paste
        # precondition on Linux, which it could not do without focus -- so there
        # is no environment left that legitimately cannot focus a field, and a
        # skip here meant an autoFocus regression on any host turned this green.
        # That is the same fault as a hang reading as a skip, which this file
        # already records further down.
        raise Failure(
            "no field took focus, so autoFocus did not take. The hosts log a "
            "reason when they refuse first responder or cannot find a window; "
            f"this is the whole of what the host said.\n{tail_text(logged)}"
        )

    # Three preconditions, each asserted separately and each with its own message.
    # The first version of this asked only the last question and could not tell
    # three different bugs apart: on CI's Linux it reported "the role did nothing"
    # when the truth was none of the three.
    if "context menu selected: Paste" not in logged:
        raise Failure(f"the paste role item was not chosen.\n{tail_text(logged)}")
    # Any `field text:` line carrying the string, not an exact match on the whole
    # line. Whether a paste lands before or after what is already there depends
    # on where focus left the caret, and the hosts do not agree: AppKit leaves it
    # at the end, so the field reads "select mePASTEDBYROLE", while a freshly
    # focused Windows EDIT has it at 0 and reads "PASTEDBYROLEselect me". The
    # role worked either way, which is what this is asking.
    #
    # It used to say that both hosts here *replace* the contents, which was true
    # and was the bug: replacing means the text was selected, and autoFocus
    # selecting a field's contents arms the next keystroke to wipe them. Fixed on
    # GTK by `gtk_text_grab_focus_without_selecting` and on AppKit by collapsing
    # the field editor's selection afterwards. This comment was the only place
    # the symptom had been written down.
    pasted = [line for line in logged.splitlines()
              if "field text: " in line and "PASTEDBYROLE" in line]
    if not pasted and PLATFORM == "linux":
        # Skipped rather than failed, and this one *is* a platform check, which the
        # skips above deliberately are not. GTK's paste is an asynchronous clipboard
        # read, and under the display CI runs on it does not complete: the preconditions
        # both pass there, the menu answers, and the text never arrives. On a
        # developer's X server the same run pastes, so the code is exercised; it is
        # the environment that cannot finish the read.
        #
        # What this costs, said plainly: on Linux a regression that broke `paste`
        # would come back as a skip rather than a failure. The hard assertion holds
        # on AppKit and on Windows, and `close` covers the mechanism on all three.
        # Closing this properly means finding out why the read does not complete,
        # which is the entry in docs/backlog/testing.md.
        raise Skipped(
            "GTK's clipboard paste does not complete under this display, though the "
            "clipboard round-tripped and the field had focus. See "
            "docs/backlog/testing.md"
        )
    if not pasted:
        raise Failure(
            "the `paste` role did not reach the focused field. The clipboard "
            "round-tripped and the field had focus, both asserted above, and the "
            "menu answered -- so the menu reached JavaScript and the platform did "
            f"nothing with it.\n{tail_text(logged)}"
        )

    # --- a window role: entry 11 carries `role: 'close'` ----------------------
    closed, logged = choose("11")
    if "context menu selected: Close Window" not in logged:
        raise Failure(
            f"a role still runs the item's handler, and this one did not.\n{tail_text(logged)}"
        )
    if closed > 5.0:
        raise Failure(
            f"the `close` role did not close the window: the host ran for "
            f"{closed:.1f}s against a quit timer of 8s, so it was the timer that "
            f"ended it.\n{tail_text(logged)}"
        )

    # --- the control: entry 10 carries no role --------------------------------
    survived, logged = choose("10")
    if survived < 6.0:
        raise Failure(
            f"the control run ended after {survived:.1f}s without a role, so the "
            "timing above says nothing: something else is ending this host "
            f"early.\n{tail_text(logged)}"
        )
    # On a `field text:` line specifically. The app logs the string on every run
    # when it reads the clipboard back, so looking for it anywhere in the output
    # matches always and asserts nothing -- which is what the first version of
    # this did, and it failed both hosts the moment the readback was added.
    leaked = [line for line in logged.splitlines()
              if "field text: " in line and "PASTEDBYROLE" in line]
    if leaked:
        raise Failure(
            "the field gained the clipboard string without any role being "
            "performed, so the paste assertion above proves nothing. The app only "
            f"ever puts that string on the clipboard, never into the field.\n"
            f"{tail_text(logged)}"
        )


def test_file_dialogs(bundle: Path) -> None:
    """The native file dialogs, which React Native has no API for at all.

    Runs e2e/dialogs.js: three buttons, one per kind. A phone has no file dialog,
    so unlike every other scenario here there is no React Native behaviour to be
    compatible with -- what is asserted is this project's own contract, which is
    that all three answer `{canceled, paths}`:

      * `canceled` is a different answer from an empty list. Every API that
        collapses those two is one somebody has to work around.
      * `paths` is a list even for a save and a folder, where it always holds
        one, so an app moving between the three is not also moving between
        result types. The native side clamps it, which is what the second run
        checks: a script naming two paths for a save still gets one.

    BASALT_TEST_FILE_DIALOG answers the dialog, because a file dialog is the
    third thing an automated run cannot get past and the one with the most
    behind it -- what an app does with a path cannot be reached without a path.
    See core/TestDialog.h.
    """
    app = bundle_app(bundle.parent, "dialogs")

    # The app's own layout: 24 of padding, a 22-tall label, then 48-tall buttons
    # 12 apart. Their middles are at y=70, 130 and 190.
    taps = "134,70;134,130;134,190"

    def run(answer: str) -> str:
        env = dict(os.environ)
        env["BASALT_QUIT_AFTER_MS"] = "9000"
        env["BASALT_TEST_TAP"] = taps
        env["BASALT_TEST_FILE_DIALOG"] = answer
        for name in ("BASALT_TEST_TYPE", "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS",
                     "BASALT_TEST_SCROLL", "BASALT_TEST_MENU"):
            env.pop(name, None)
        result = run_host_process(
            [str(HOST), str(app), "BasaltDialogs"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=150,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        # Both streams: GLib sends g_message to stdout and only warnings to
        # stderr.
        return result.stdout + result.stderr

    def answers(text: str) -> dict:
        found = {}
        for line in text.splitlines():
            if "dialog " in line and "canceled=" in line:
                rest = line.split("dialog ", 1)[1].strip()
                kind, _, detail = rest.partition(": ")
                found[kind] = detail
        return found

    # The path separator the host splits on: a Windows path starts "C:\\", so a
    # colon would cut every one of them in two.
    separator = ";" if PLATFORM == "windows" else ":"
    first = "C:\\tmp\\a.png" if PLATFORM == "windows" else "/tmp/a.png"
    second = "C:\\tmp\\b.png" if PLATFORM == "windows" else "/tmp/b.png"

    chosen = answers(run(separator.join([first, second])))
    if len(chosen) != 3:
        raise Failure(
            f"expected an answer from all three dialogs, got {sorted(chosen)}"
        )
    # A multiple open keeps both.
    if chosen["open"] != f"canceled=false paths={first}|{second}":
        raise Failure(f"openFile answered {chosen['open']!r}")
    # A save and a folder are one path however many the script named, which is
    # what the platform dialogs enforce and what an app may assume.
    if chosen["save"] != f"canceled=false paths={first}":
        raise Failure(f"saveFile answered {chosen['save']!r}")
    if chosen["folder"] != f"canceled=false paths={first}":
        raise Failure(f"openFolder answered {chosen['folder']!r}")

    cancelled = answers(run("cancel"))
    for kind, detail in cancelled.items():
        if detail != "canceled=true paths=":
            raise Failure(f"a cancelled {kind} answered {detail!r}")
    if len(cancelled) != 3:
        raise Failure(f"expected all three to report a cancel, got {sorted(cancelled)}")


def has_window_manager() -> bool:
    """Whether anything on this display would honour a full-screen request.

    Every EWMH-compliant window manager sets `_NET_SUPPORTING_WM_CHECK` on the
    root window, and a bare X server -- which is what Xvfb is without one --
    has nobody to set it. Answers false when `xprop` is missing too: it cannot
    be told from "no window manager" and both mean the same thing here.
    """
    try:
        result = subprocess.run(
            ["xprop", "-root", "-notype", "_NET_SUPPORTING_WM_CHECK"],
            capture_output=True, text=True, timeout=10,
        )
    except (OSError, subprocess.SubprocessError):
        return False
    return result.returncode == 0 and "window id" in result.stdout


def test_window(bundle: Path) -> None:
    """The window an app is in, which React Native has no API for.

    Runs e2e/window.js, which logs its own bounds every time they change. Two
    presses, and what each one proves is different:

      setSize      that a request reaches the window manager and comes back as
                   the size the app actually got. The app never reads what it
                   asked for -- `bounds` is what happened, which is the only
                   honest answer when a tiling window manager may refuse.

      setFullScreen  that a state change is reported as well as a size change.
                   It is the case most likely to be missed, because on two of
                   the three hosts going full screen is *not* a resize: GTK
                   changes a window property and Windows changes a style, and a
                   host watching only for resizes would report neither.

    Position is deliberately not asserted. GTK4 removed `gtk_window_move` and
    Wayland has no equivalent, so `setPosition` and `center` do nothing on Linux
    and `bounds.x` is always zero there -- a cross-platform assertion on it
    would be asserting a lie. See native/gtk/GtkWindowControl.cpp.
    """
    app = bundle_app(bundle.parent, "window")

    # The app's own layout: 24 of padding, a 22-tall label, then 48-tall rows 12
    # apart. Row one's middle is y=70 and row two's is y=130; the buttons are
    # 150 wide from x=24, so their middles are x=99 and x=261.
    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "12000"
    env["BASALT_TEST_TAP"] = "99,70;261,130"
    for name in ("BASALT_TEST_TYPE", "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS",
                 "BASALT_TEST_SCROLL", "BASALT_TEST_MENU"):
        env.pop(name, None)

    result = run_host_process(
        [str(HOST), str(app), "BasaltWindow"],
        cwd=REPO, env=env, capture_output=True, text=True, timeout=150,
    )
    _remember_output(result.stderr)
    check_output(result.stderr, result.returncode)

    # Both streams: GLib sends g_message to stdout and only warnings to stderr.
    reported = [
        line.split("window bounds: ", 1)[1].strip()
        for line in (result.stdout + result.stderr).splitlines()
        if "window bounds: " in line
    ]
    if not reported:
        raise Failure(
            "the app never learned its own bounds.\n"
            f"{tail_text(result.stdout + result.stderr)}"
        )

    # A window of some size, before anything was asked of it. The first line is
    # zeroes on purpose -- it is the render before the module exists -- so what
    # matters is that a real one followed.
    if all(line.startswith("0x0 ") for line in reported):
        raise Failure(f"the bounds never became real: {reported}")

    if not any(line.startswith("700x500 ") for line in reported):
        raise Failure(f"setSize(700, 500) was not reported back: {reported}")

    if not any("fullScreen=true" in line for line in reported):
        # A window manager is what makes a window full screen; the application
        # only asks. CI runs the Linux host under Xvfb, which has no window
        # manager at all, so the request is not refused so much as unheard --
        # and asserting it there would be asserting something about the runner.
        #
        # Checked rather than assumed: where there *is* a window manager this
        # must pass, and a silent skip would hide the case this scenario exists
        # for. `_NET_SUPPORTING_WM_CHECK` is the property every EWMH-compliant
        # window manager sets on the root window.
        if PLATFORM == "linux" and not has_window_manager():
            print(
                "        (no window manager, so the full-screen half of this "
                "scenario could not run)"
            )
            return
        raise Failure(
            "setFullScreen(true) was never reported. On two of the three hosts "
            "this is not a resize, so a host watching only for resizes misses "
            f"it entirely.\n{reported}"
        )


def test_context_menu(bundle: Path) -> None:
    """The other kind of menu: the one that pops up where you press.

    Runs the second app in e2e/menu.js. A press opens a four-entry popup --
    a separator and a disabled item among them -- and BASALT_TEST_MENU answers
    it, because a menu cannot be dismissed by an automated run. On macOS that is
    not a convenience: `popUpMenuPositioningItem` runs the menu's own tracking
    loop on the main thread, so a popup nobody closes stops the process where it
    stands.

    Two runs, because a menu has two answers and they must not be the same one:

      chosen      index 2 is Rename, which is past a separator. Indexes count
                  separators so they line up with the list that was passed in,
                  and getting that wrong shows up as an app acting on the item
                  above or below the one a person picked.

      dismissed   null, not an index -- the shape a cancelled file dialog
                  answers with, and for the same reason: an index is a number,
                  and a caller checking `if (index)` would read entry zero as
                  nothing.

    Unlike the application menu this runs on all three, which is the point of it
    existing: `Menu.isSupported` is false on GNOME, and a popup is something
    every desktop has always had.
    """
    app = bundle_app(bundle.parent, "menu")

    def press(variable: str, answer: str) -> str:
        env = dict(os.environ)
        env["BASALT_QUIT_AFTER_MS"] = "8000"
        # The button is at (134, 70): 24 of padding, a 22-tall label, then a
        # 220x48 button.
        env[variable] = "134,70"
        env["BASALT_TEST_MENU"] = answer
        for name in ("BASALT_TEST_TAP", "BASALT_TEST_SECONDARY_TAP", "BASALT_TEST_TYPE",
                     "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL",
                     "BASALT_TEST_CLOSE_WINDOW"):
            if name != variable:
                env.pop(name, None)
        result = run_host_process(
            [str(HOST), str(app), "BasaltContextMenu"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        return result.stdout + result.stderr

    def run(answer: str) -> str:
        return press("BASALT_TEST_TAP", answer)

    def run_secondary(answer: str) -> str:
        return press("BASALT_TEST_SECONDARY_TAP", answer)

    logged = run("2")
    if "context menu: opening" not in logged:
        raise Failure(f"the press never reached the app.\n{tail_text(logged)}")
    if "context menu answered: 2" not in logged:
        raise Failure(
            "choosing entry 2 did not come back as 2. Indexes count separators, "
            "so an off-by-one here is an app acting on the item next to the one "
            f"a person picked.\n{tail_text(logged)}"
        )
    # The onSelect half: `show()` calls the chosen item's handler before it
    # resolves, which is what most callers use instead of the index. Rename is
    # entry 2, so this also says the index was mapped back to the right item.
    if "context menu selected: Rename" not in logged:
        raise Failure(
            f"the chosen item's onSelect never ran.\n{tail_text(logged)}"
        )

    # An item inside a submenu. Index 6 is "Copy link", the first child of the
    # "Share" parent at 5, and reaching it says three things at once: the parent
    # was built as a submenu, the index counts into it, and both halves agree on
    # the pre-order numbering -- JavaScript's `itemAt` walks the tree it passed
    # and the native side numbers the tree it was given.
    logged = run("6")
    if "context menu answered: 6" not in logged:
        raise Failure(
            "choosing entry 6 did not come back as 6. It is the first item of a "
            "submenu, and an index that counts only the top level would have "
            f"refused it as past the end.\n{tail_text(logged)}"
        )
    if "context menu selected: Copy link" not in logged:
        raise Failure(
            "entry 6 is a submenu's first child and its onSelect did not run, so "
            f"the index was mapped to the wrong item.\n{tail_text(logged)}"
        )

    # A parent is not a choice. Choosing index 5 opens the submenu rather than
    # picking anything, so the scripted answer is a dismissal, the same as what
    # a person clicking "Share" would produce.
    logged = run("5")
    if "context menu answered: dismissed" not in logged:
        raise Failure(
            "choosing the submenu's parent answered with an index. A parent "
            f"opens; it is not an item.\n{tail_text(logged)}"
        )

    # The one most likely to be wrong, and the reason the app has an `About`
    # item: it names the `about` role, which only macOS can perform, so Linux and
    # Windows leave it out of the menu. It keeps its index anyway, because the
    # index is into the list JavaScript passed -- so `Last` is 10 on all three.
    # Numbering only what each platform drew would make this 9 on two of them,
    # and every app with a role would silently act on the wrong item there.
    logged = run("10")
    if "context menu answered: 10" not in logged:
        raise Failure(
            "entry 10 did not come back as 10. It sits after an item carrying a "
            "role that two of the three desktops leave out, and an index is into "
            f"the list that was passed, not into what was drawn.\n{tail_text(logged)}"
        )
    if "context menu selected: Last" not in logged:
        raise Failure(
            "entry 10's onSelect did not run, so an item the platform left out "
            f"shifted the ones after it.\n{tail_text(logged)}"
        )

    # A marked item is an ordinary item. A tick or a radio mark is presentational,
    # so a marked entry is drawn, is choosable, is counted, and answers with its
    # own index like anything else. 14 is a checkbox and 18 is the middle member of
    # a radio run, which is also the furthest index the suite names and so the one
    # that would move first if marks were counted differently.
    logged = run("14")
    if "context menu selected: Word wrap" not in logged:
        raise Failure(
            f"a checkbox item did not run its own handler.\n{tail_text(logged)}"
        )
    logged = run("18")
    if "context menu answered: 18" not in logged:
        raise Failure(
            "the middle member of a radio run did not come back as its own index. "
            "A run shares one action on GTK and carries the index as a target "
            f"rather than in the action's name.\n{tail_text(logged)}"
        )
    if "context menu selected: Medium" not in logged:
        raise Failure(
            f"a radio item did not run its own handler.\n{tail_text(logged)}"
        )

    # A role this desktop cannot perform is not in the menu, so naming it is a
    # dismissal. macOS has `about` and answers 9; the other two do not.
    logged = run("9")
    expected = ("context menu answered: 9" if PLATFORM == "macos"
                else "context menu answered: dismissed")
    if expected not in logged:
        raise Failure(
            "the `about` role should be choosable on macOS and absent elsewhere. "
            f"Expected {expected!r}.\n{tail_text(logged)}"
        )

    # A real right-click. The button number reaches `onPointerDown` as W3C's 2,
    # and -- the half that matters -- `onPress` does not fire: the same view is
    # a button and has a context menu, which is what a desktop expects.
    #
    # Every host used to get this wrong in its own way. GTK set its click
    # gesture to button 0 and AppKit forwarded rightMouseDown: like mouseDown:,
    # so on both a right-click *activated* whatever it landed on; Windows
    # handled only WM_LBUTTONDOWN, so a right-click there did nothing at all.
    logged = run_secondary("2")
    if "context menu: opening from a right-click" not in logged:
        raise Failure(
            "a right-click did not reach onPointerDown with button 2.\n"
            f"{tail_text(logged)}"
        )
    if "context menu: opening\n" in logged or "context menu: opening\r" in logged:
        raise Failure(
            "a right-click fired onPress as well. A secondary click is not an "
            "activation on any desktop, and a view that is both a button and a "
            f"context-menu target would do two things at once.\n{tail_text(logged)}"
        )
    if "context menu selected: Rename" not in logged:
        raise Failure(f"the right-click menu chose nothing.\n{tail_text(logged)}")

    logged = run("dismiss")
    if "context menu answered: dismissed" not in logged:
        raise Failure(
            "a dismissed menu did not answer null. An index is a number, and a "
            "caller checking `if (index)` would read entry zero as nothing.\n"
            f"{tail_text(logged)}"
        )


def test_animated_scroll(bundle: Path) -> None:
    """`scrollTo({animated: true})` moves rather than jumps.

    Runs the second app in e2e/scroll.js, which scrolls to 530 with the flag set
    and logs every offset `onScroll` reports.

    Arriving at 530 proves nothing: an instant jump arrives too, which is what
    every host did until now -- the flag was parsed and dropped. What says it
    animated is the offsets *in between*, so that is what this asserts.

    It also asserts arrival exactly, because a curve that approaches its target
    asymptotically would look animated and leave a list one pixel short of where
    the app asked for. See core/ScrollAnimation.h for why that is evaluated
    rather than approached.
    """
    if screen_is_locked():
        # The curve is driven by a CADisplayLink, which does not fire while the
        # session is locked: there is nothing on a screen to be in step with. So
        # the app arrives at 530 with no offsets in between, which reads as the
        # flag being dropped -- the exact failure this scenario is here to catch.
        raise Skipped("the screen is locked, so no display link runs")

    app = bundle_app(bundle.parent, "scroll")

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "4000"
    for name in ("BASALT_TEST_TAP", "BASALT_TEST_SECONDARY_TAP", "BASALT_TEST_TYPE",
                 "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL",
                 "BASALT_TEST_MENU", "BASALT_TEST_CLOSE_WINDOW"):
        env.pop(name, None)

    result = run_host_process(
        [str(HOST), str(app), "BasaltScrollAnimated"],
        cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
    )
    _remember_output(result.stderr)
    check_output(result.stderr, result.returncode)
    logged = result.stdout + result.stderr

    if "scroll: animating to 530" not in logged:
        raise Failure(f"the app never asked for an animated scroll.\n{tail_text(logged)}")

    offsets = [
        float(line.split("scrolled to ", 1)[1].strip())
        for line in logged.splitlines()
        if "scrolled to " in line
    ]
    if not offsets:
        raise Failure(f"no scroll offsets were reported at all.\n{tail_text(logged)}")

    if offsets[-1] != 530:
        raise Failure(
            f"an animated scroll to 530 finished at {offsets[-1]}. The curve is "
            "evaluated at its end rather than approached, so it should arrive at "
            "the number the app asked for."
        )

    between = [y for y in offsets if 0 < y < 530]
    if not between:
        raise Failure(
            "the scroll arrived at 530 without passing through anything, which "
            "is a jump rather than an animation -- the `animated` flag being "
            f"parsed and dropped is exactly how this used to behave.\n{offsets}"
        )


def test_scrollbar_can_be_turned_off(bundle: Path) -> None:
    """`showsVerticalScrollIndicator={false}` takes the bar away and leaves the
    scrolling.

    Runs the third app in e2e/scroll.js, which is the same list as the first one
    with the prop set. The first app's scenarios above already assert that a
    list that overflows grows a scrollbar, so what is left is the absence --
    and that the absence is only the bar: a host that read the prop as
    "scrollEnabled" would pass a test that looked no further.
    """
    app = bundle_app(bundle.parent, "scroll")

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env = dict(os.environ)
        env["BASALT_DUMP_TREE"] = str(dump)
        env["BASALT_QUIT_AFTER_MS"] = "3000"
        for name in ("BASALT_TEST_TAP", "BASALT_TEST_SECONDARY_TAP", "BASALT_TEST_TYPE",
                     "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL",
                     "BASALT_TEST_MENU", "BASALT_TEST_CLOSE_WINDOW"):
            env.pop(name, None)

        result = run_host_process(
            [str(HOST), str(app), "BasaltScrollBare"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        tree = dump.read_text() if dump.exists() else ""

    if not tree:
        raise Failure("the host dumped no tree")
    if "scrollbar-v=" in tree:
        raise Failure(
            "showsVerticalScrollIndicator={false} still drew a vertical scrollbar:\n"
            + "\n".join(line for line in tree.splitlines() if "scrollbar-v=" in line)
        )
    # And it still scrolled, which is the half the prop must not touch.
    if scroll_offset(tree) <= 0:
        raise Failure(
            "the list never scrolled, so the prop turned off more than the bar"
        )


def test_content_inset(bundle: Path) -> None:
    """`contentInset` changes how far a list scrolls, and
    `scrollIndicatorInsets` changes only where its bar is drawn.

    Runs the fourth app in e2e/scroll.js, which sets a 60pt top content inset
    and a 30pt top indicator inset, then scrolls to -60.

    The offset is the whole of the first half: a platform that reads the prop
    and ignores it -- which all three did until now -- clamps -60 to 0, so the
    number is the difference between applied and merely parsed.

    The second half is the indicator, which is why the two insets are
    different numbers. The bar's track starts at the standard inset *plus*
    the indicator inset, and a host that applied `contentInset` to both would
    put it 60 down instead of 30.
    """
    app = bundle_app(bundle.parent, "scroll")

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env = dict(os.environ)
        env["BASALT_DUMP_TREE"] = str(dump)
        env["BASALT_QUIT_AFTER_MS"] = "3000"
        for name in ("BASALT_TEST_TAP", "BASALT_TEST_SECONDARY_TAP", "BASALT_TEST_TYPE",
                     "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL",
                     "BASALT_TEST_MENU", "BASALT_TEST_CLOSE_WINDOW"):
            env.pop(name, None)

        result = run_host_process(
            [str(HOST), str(app), "BasaltScrollInset"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        tree = dump.read_text() if dump.exists() else ""

    if not tree:
        raise Failure("the host dumped no tree")

    offset = scroll_offset(tree)
    if offset > -59.0:
        raise Failure(
            f"scrollTo({{y: -60}}) with a 60pt contentInset rested at {offset}. "
            "Zero means the inset was parsed and not applied, which is what "
            "every host did before this."
        )

    # The bar sits below its own inset, not the content one.
    bar = scrollbar(tree, "v")
    if bar is None:
        raise Failure("the inset list drew no scrollbar")
    bar_offset, _ = bar
    expected = SCROLLBAR_INSET + 30.0
    if abs(bar_offset - expected) > 1.0:
        raise Failure(
            f"the bar starts at {bar_offset}, not {expected}. "
            "60 would mean scrollIndicatorInsets was ignored and contentInset "
            "used for both."
        )


def test_press_location(bundle: Path) -> None:
    """`locationX`/`locationY` are where inside the target the press landed.

    They come from `Touch::offsetPoint`, which carried the *page* point on all
    three hosts until the target's own coordinates were computed -- right only
    for a view sitting at the surface's origin, and wrong by that view's
    position for every other.

    e2e/press.js is the app for it because its button is inset by the page's
    24pt padding, so page and local differ by a number this can name. A tap at
    (100, 60) is 76 into the button and 36 down; a host reporting the page
    point logs 100,60 instead.
    """
    app = bundle_app(bundle.parent, "press")

    with tempfile.TemporaryDirectory() as directory:
        env = dict(os.environ)
        env["BASALT_QUIT_AFTER_MS"] = "3000"
        env["BASALT_TEST_TAP"] = "100,60"
        for name in ("BASALT_TEST_SECONDARY_TAP", "BASALT_TEST_TYPE", "BASALT_TEST_HOVER",
                     "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL", "BASALT_TEST_MENU",
                     "BASALT_TEST_CLOSE_WINDOW"):
            env.pop(name, None)

        result = run_host_process(
            [str(HOST), str(app), "BasaltPress"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        logged = result.stdout + result.stderr

    if "pressed 1" not in logged:
        raise Failure(f"the tap did not press the button.\n{tail_text(logged)}")

    match = re.search(r"press at (-?\d+),(-?\d+)", logged)
    if match is None:
        raise Failure(f"the app logged no press location.\n{tail_text(logged)}")
    x, y = int(match.group(1)), int(match.group(2))

    # The page's padding is 24, so the button's own origin is (24, 24).
    if (x, y) == (100, 60):
        raise Failure(
            "locationX/locationY are the page point (100,60). offsetPoint is "
            "supposed to be relative to the target, which here is (76,36)."
        )
    if abs(x - 76) > 2 or abs(y - 36) > 2:
        raise Failure(f"the press landed at {x},{y} inside the button; expected about 76,36")


def test_layout_styles(bundle: Path) -> None:
    """The layout props arrive, and Yoga's answer is the frame each host applies.

    `padding`, `margin`, `gap`, `flexGrow`, `position: 'absolute'` with insets,
    `aspectRatio`: the props an app writes most, and the one group on the
    support page that no host reads. Fabric hands Yoga the style, Yoga answers
    with a frame, and each host applies the frame -- so there is nothing in a
    host to get wrong and exactly one thing that can go wrong anywhere, which is
    arrival. A name dropped from `ReactNativeStyleAttributes` or from the style
    flattener reaches nobody, silently, which is how `accessibilityViewIsModal`
    and `writingDirection` were each broken for months.

    So this is a scenario rather than a unit test: only an app can write a style,
    and only the tree can say what came of it. Six probes in e2e/views.tsx, each
    isolating one prop against a fixed box and carrying a `testID` so its line
    is findable wherever the tree puts it.

    **Every assertion is a difference rather than a position**, which is what
    view flattening makes necessary and is worth knowing before reading them:
    Fabric hoists a view that groups nothing natively and rebases its children's
    frames onto the nearest ancestor that stayed, so a child's frame in the dump
    is not relative to the parent in the JSX. See docs/ARCHITECTURE.md. A gap of
    nine points is still nine points between two boxes whatever they were
    rebased onto, and the arithmetic is exact because Yoga's is.

    Runs on all three hosts and skips none: there is no platform half to be
    missing. A host whose frames disagreed with these numbers would be applying
    Yoga's answer wrongly, which is the other thing this would catch.
    """
    app = bundle_app(bundle.parent, "views")

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "3000"
    for name in ("BASALT_TEST_TAP", "BASALT_TEST_SECONDARY_TAP", "BASALT_TEST_TYPE",
                 "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL",
                 "BASALT_TEST_MENU", "BASALT_TEST_CLOSE_WINDOW"):
        env.pop(name, None)

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env["BASALT_DUMP_TREE"] = str(dump)
        result = run_host_process(
            [str(HOST), str(app), "BasaltViews"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        if not dump.exists():
            raise Failure("host wrote no widget tree")
        tree = dump.read_text()

    frames = {}
    for line in tree.splitlines():
        name = re.search(r"testid=(\S+)", line)
        frame = re.search(r"frame=\(([-\d.]+),([-\d.]+) ([\d.]+)x([\d.]+)\)", line)
        if name is None or frame is None:
            continue
        frames[name.group(1)] = tuple(float(frame.group(index)) for index in (1, 2, 3, 4))

    probes = ["pad-parent", "pad-child", "margin-first", "margin-second",
              "gap-first", "gap-second", "grow-fixed", "grow-rest",
              "inset-parent", "inset-child", "ratio"]
    missing = [name for name in probes if name not in frames]
    if missing:
        raise Failure(
            f"no view in the tree carries {', '.join(missing)}. A probe with no "
            "line was flattened away before it reached the host, which is what "
            f"its background colour is for.\n{tree}"
        )

    # Each box is 20 by 10, which is the other half of every number below: a
    # width that moved means the prop that set it did not arrive either.
    for name in ["pad-child", "margin-first", "margin-second", "gap-first",
                 "gap-second", "grow-fixed", "inset-child"]:
        if frames[name][2:] != (20.0, 10.0):
            raise Failure(
                f"{name} measures {frames[name][2]}x{frames[name][3]} and the app asks "
                "for 20x10, so `width` or `height` did not arrive"
            )

    def difference(inner, outer, expected, why):
        dx = frames[inner][0] - frames[outer][0]
        dy = frames[inner][1] - frames[outer][1]
        if abs(dx - expected[0]) > 0.01 or abs(dy - expected[1]) > 0.01:
            raise Failure(
                f"{why}: {inner} is ({dx},{dy}) from {outer}, and Yoga says {expected}"
            )

    difference("pad-child", "pad-parent", (12.0, 12.0),
               "padding: 12 did not inset the child on both axes")
    # 20 points of box and then the 7 the margin asked for.
    difference("margin-second", "margin-first", (27.0, 0.0),
               "marginLeft: 7 did not space the second child")
    difference("gap-second", "gap-first", (29.0, 0.0),
               "gap: 9 did not space the children")
    difference("grow-rest", "grow-fixed", (20.0, 0.0),
               "the grown child does not start where the fixed one ends")
    difference("inset-child", "inset-parent", (4.0, 3.0),
               "position: 'absolute' with top: 3 and left: 4 did not place the child")

    # flexGrow takes what is left of a 100 point row after a 20 point box.
    if abs(frames["grow-rest"][2] - 80.0) > 0.01:
        raise Failure(
            f"flexGrow: 1 took {frames['grow-rest'][2]} of a 100 point row rather "
            "than the 80 that was left"
        )

    # aspectRatio decides a width from a height, so the ratio is the assertion
    # and the place in the row is not.
    ratio = frames["ratio"]
    if abs(ratio[2] - ratio[3] * 2.0) > 0.01:
        raise Failure(
            f"aspectRatio: 2 on a 10 point box measured {ratio[2]}x{ratio[3]}"
        )


def test_hit_slop(bundle: Path) -> None:
    """`hitSlop` grows what a press can land on, and only that.

    The prop exists because a small target is hard to aim at: e2e/press.js has a
    24pt square with 16 points of slop on every side, absolutely positioned so
    its box is at (300,300) whatever the rest of the app does.

    Two taps in one run, which is what makes this an assertion rather than an
    anecdote. The first lands 28 points left of the box, outside the slop, and
    must do nothing; the second lands 12 points left of it, inside the slop, and
    must press it. A host that ignored the prop fails the second; one that
    treated any nearby point as a hit fails the first.

    Through BASALT_TEST_TAP, so the press goes through the host's own hit test
    and React Native's responder rather than past them.

    All three hosts now. Each widens one view's own test rather than the walk
    that reached it, which is what keeps a slop reaching outside its parent
    unreachable there: GTK by widening `contains`, AppKit by widening the rect
    at the top of its walk, Win32 by widening the bounds check in `hitTest`.
    """

    app = bundle_app(bundle.parent, "press")

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "4000"
    env["BASALT_TEST_TAP"] = "272,312;288,312"
    for name in ("BASALT_TEST_SECONDARY_TAP", "BASALT_TEST_TYPE", "BASALT_TEST_HOVER",
                 "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL", "BASALT_TEST_MENU",
                 "BASALT_TEST_CLOSE_WINDOW"):
        env.pop(name, None)

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env["BASALT_DUMP_TREE"] = str(dump)
        result = run_host_process(
            [str(HOST), str(app), "BasaltPress"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        logged = result.stdout + result.stderr
        tree = dump.read_text() if dump.exists() else ""

    # The prop arrived, which is worth separating from the behaviour: a host that
    # never read it fails here and says so, rather than failing a tap.
    if "hit-slop=(16,16,16,16)" not in tree:
        raise Failure(
            f"the view does not report its hitSlop, so the prop never arrived.\n{tree}"
        )

    presses = logged.count("slop pressed")
    if presses == 0:
        raise Failure(
            "a tap 12 points outside a 24pt box with 16 points of slop pressed "
            f"nothing, so the slop is not part of the hit test.\n{tail_text(logged)}"
        )
    if presses > 1:
        raise Failure(
            "both taps pressed it, including one 28 points outside a 16 point "
            f"slop: the target is bigger than it was asked to be.\n{tail_text(logged)}"
        )


def test_animated_image(bundle: Path) -> None:
    """An animated GIF is animated rather than painted as a still.

    Every host decoded frame zero and stopped, which is a reasonable thing to do
    until there is an animator and was still what every spinner and every
    reaction GIF looked like. Each host's own suite asserts the frames against
    real pixels, red then blue then red; what only an app can show is that the
    props path recognises the file at all, since being animated is something the
    *loader* notices and the mounting manager has to pass on.

    `animated=1` rather than which frame: the three hosts tick on their own
    clocks, so a cross-host diff of a frame index would be a race. Exactly one
    image in e2e/image.tsx is animated, so a host that claimed it for every
    <Image> would fail here rather than pass twice over.
    """
    app = bundle_app(bundle.parent, "image")

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "3000"
    for name in ("BASALT_TEST_TAP", "BASALT_TEST_SECONDARY_TAP", "BASALT_TEST_TYPE",
                 "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL",
                 "BASALT_TEST_MENU", "BASALT_TEST_CLOSE_WINDOW"):
        env.pop(name, None)

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env["BASALT_DUMP_TREE"] = str(dump)
        result = run_host_process(
            [str(HOST), str(app), "BasaltImage"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        if not dump.exists():
            raise Failure("host wrote no widget tree")
        tree = dump.read_text()

    images = [line for line in tree.splitlines() if "texture=" in line]
    if not images:
        raise Failure(f"no image loaded at all, so nothing can be animated\n{tree[:1500]}")

    animated = [line for line in images if "animated=1" in line]
    if len(animated) != 1:
        raise Failure(
            f"{len(animated)} images report an animation; the app has exactly "
            "one, a two-frame GIF. None at all means the loader did not notice "
            "the frames or the mounting manager dropped them.\n"
            + "\n".join(images)
        )
    # The GIF is four pixels square, which is what says the animated line is the
    # GIF's and not some other image that acquired the flag.
    if "texture=4x4" not in animated[0]:
        raise Failure(
            f"the animated image is not the 4x4 GIF.\n{animated[0]}"
        )


def test_image_get_size(bundle: Path) -> None:
    """`Image.getSize` answers, and a missing file rejects.

    A different seam from the one that paints: the pixels on screen come from
    the mounting manager, and this goes through React Native's own
    `ImageLoader` module. That module takes an `IImageLoader` and
    `ReactCxxTurboModuleProvider` builds it with none and offers no way to
    supply one -- so `getSize` resolved never, on every host, until each
    registered the module itself with the loader it already had.

    Both halves matter. A host that answered every call would pass on the
    first assertion alone while reporting a size for a file that does not
    exist.
    """
    app = bundle_app(bundle.parent, "image")

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "3000"
    for name in ("BASALT_TEST_TAP", "BASALT_TEST_SECONDARY_TAP", "BASALT_TEST_TYPE",
                 "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL",
                 "BASALT_TEST_MENU", "BASALT_TEST_CLOSE_WINDOW"):
        env.pop(name, None)

    result = run_host_process(
        [str(HOST), str(app), "BasaltImage"],
        cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
    )
    _remember_output(result.stderr)
    check_output(result.stderr, result.returncode)
    logged = result.stdout + result.stderr

    if "getSize 160x100" not in logged:
        raise Failure(
            "Image.getSize did not report the asset's size. Nothing at all "
            f"means the module has no loader and the promise never settles.\n{tail_text(logged)}"
        )
    if "getSize missing rejected" not in logged:
        raise Failure(
            "Image.getSize resolved for a file that does not exist.\n"
            + tail_text(logged)
        )


def test_image_tint_and_blur(bundle: Path) -> None:
    """`tintColor` and `blurRadius` reach the view.

    The two <Image> props a frame cannot show: a tinted image, a blurred one and
    a plain one are the same size in the same place, so a tree dump is the only
    thing that can say the prop arrived. Both hosts print them for that reason.

    Which is the whole scenario, and it exists because of how one of them broke.
    Both props are read in the same branch of each mounting manager, and on GTK
    the blur was assigned *inside* the `if` that read the tint: a blurred image
    with no tintColor came out sharp. Every unit test still passed, because each
    one pushes the blur onto the widget by hand and none of them goes through the
    props. This one does.

    Separate from the getSize scenario above, which runs the same app: that one
    is about React Native's ImageLoader module, and sharing a run would make a
    failure in either read as a failure of both.

    Both props on all three hosts as of 2026-10-09, which is why the `blurs`
    flag below is gone: it existed because `blurRadius` was GTK and AppKit only,
    and half a scenario that runs was worth more than a scenario that did not.
    """
    app = bundle_app(bundle.parent, "image")

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "3000"
    for name in ("BASALT_TEST_TAP", "BASALT_TEST_SECONDARY_TAP", "BASALT_TEST_TYPE",
                 "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL",
                 "BASALT_TEST_MENU", "BASALT_TEST_CLOSE_WINDOW"):
        env.pop(name, None)

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env["BASALT_DUMP_TREE"] = str(dump)
        result = run_host_process(
            [str(HOST), str(app), "BasaltImage"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        if not dump.exists():
            raise Failure("host wrote no widget tree")
        tree = dump.read_text()

    images = [line for line in tree.splitlines() if "texture=" in line]
    if not images:
        raise Failure(f"no image loaded at all, so neither prop can be read\n{tree[:1500]}")

    wanted = [("tint=#ff00aa", "the tintColor never reached the view"),
              ("blur=12", "the blurRadius never reached the view")]
    for needle, why in wanted:
        if not any(needle in line for line in images):
            raise Failure(
                f"{why}: no image reports {needle}.\n" + "\n".join(images)
            )

    # And exactly one image has each, so a host that applied a prop to every
    # <Image> it mounted would fail here rather than pass twice over.
    tinted = [line for line in images if "tint=" in line]
    blurred = [line for line in images if "blur=" in line]
    if len(tinted) != 1 or len(blurred) != 1:
        raise Failure(
            f"{len(tinted)} images are tinted and {len(blurred)} are blurred; "
            "the app sets each on exactly one.\n" + "\n".join(images)
        )
    # Not the same one: the app deliberately blurs an image that is not tinted,
    # which is the case the GTK bug got wrong and the Win32 half was written
    # against.
    if tinted[0] == blurred[0]:
        raise Failure(
            "one image carries both props, so a blur that only applies to a "
            "tinted image would pass.\n" + "\n".join(images)
        )


def test_border_style(bundle: Path) -> None:
    """`borderStyle: 'dashed'` and `'dotted'` reach the view.

    All three hosts draw those as one stroked outline rather than four filled
    edges, and the switch is invisible in every other line of a tree dump: same
    widths, same colours, same frame. So each prints the style, and this reads
    it.

    It exists because the prop never arrived. React Native's `borderStyles` is a
    cascade of optionals with a slot per spelling -- `left`, `top`, `start`,
    `horizontal`, `all` -- and a style written once for the whole border lands in
    `all`. Both hosts read the four sides directly and found nothing, so every
    dashed border in a React app drew solid, while the unit tests on both sides
    set the style on the widget by hand and passed. `resolveBorderMetrics` does
    the cascade; reading its answer is the fix.

    Runs e2e/views.js, the app scripts/compare_hosts.sh diffs between desktops,
    which is where a prop that arrives on one and not the other belongs.

    All three hosts now, and all three stroke it: GSK, Core Animation and
    Direct2D each take a dash array, so none of them needed a second mechanism.
    """

    app = bundle_app(bundle.parent, "views")

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "3000"
    for name in ("BASALT_TEST_TAP", "BASALT_TEST_SECONDARY_TAP", "BASALT_TEST_TYPE",
                 "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL",
                 "BASALT_TEST_MENU", "BASALT_TEST_CLOSE_WINDOW"):
        env.pop(name, None)

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env["BASALT_DUMP_TREE"] = str(dump)
        result = run_host_process(
            [str(HOST), str(app), "BasaltViews"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        if not dump.exists():
            raise Failure("host wrote no widget tree")
        tree = dump.read_text()

    bordered = [line for line in tree.splitlines() if "borderw=" in line]
    if len(bordered) != 2:
        raise Failure(
            f"the app draws two bordered views and {len(bordered)} have a border "
            f"width.\n{tree}"
        )
    for style in ("dashed", "dotted"):
        if not any(f"border-style={style}" in line for line in bordered):
            raise Failure(
                f"no view reports border-style={style}, so the prop did not reach "
                "the view layer. The usual cause is reading props->borderStyles "
                "rather than the resolved metrics: a style written once lands in "
                "the cascade's `all` slot and in none of the four sides.\n"
                + "\n".join(bordered)
            )


def test_box_shadow(bundle: Path) -> None:
    """`boxShadow` reaches the view, the list and the inset flag with it.

    A shadow is a prop two of the three hosts cannot be asked about in pixels.
    GTK's is a GSK node, which its unit tests walk; macOS composites it in Core
    Animation, and `renderInContext:` -- which is what this project's own
    snapshots use -- draws no shadow at all, measured on a bare layer. Windows
    draws it into the same bitmap as everything else, so its own suite does ask
    in pixels. The tree dump is what all three can be asked, and this reads it.

    Two shadows on one view, one of them inset, because a host can drop the
    second of a list or lose the inset flag and still draw something plausible:
    e2e/views.tsx writes them as one CSS shorthand, so this also covers React
    Native's own parse of it.
    """
    app = bundle_app(bundle.parent, "views")

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "3000"
    for name in ("BASALT_TEST_TAP", "BASALT_TEST_SECONDARY_TAP", "BASALT_TEST_TYPE",
                 "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL",
                 "BASALT_TEST_MENU", "BASALT_TEST_CLOSE_WINDOW"):
        env.pop(name, None)

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env["BASALT_DUMP_TREE"] = str(dump)
        result = run_host_process(
            [str(HOST), str(app), "BasaltViews"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        if not dump.exists():
            raise Failure("host wrote no widget tree")
        tree = dump.read_text()

    # The view the CSS shadows are on, by its outer shadow: the app also has a
    # view carrying the older iOS shadow props, which arrive as a shadow of
    # their own -- see test_legacy_shadow.
    carrying = [line for line in tree.splitlines() if "shadow=(0,4,8,0,#00000040)" in line]
    if len(carrying) != 1:
        raise Failure(
            f"{len(carrying)} views report this shadow; the app sets two on one.\n{tree}"
        )
    # The numbers in full. An offset read onto the wrong axis, or a blur read as a
    # spread, still draws a shadow -- so the assertion is the whole tuple.
    for needle in ("shadow=(0,4,8,0,#00000040)", "shadow=(inset 0,1,0,0,#ffffffff)"):
        if needle not in carrying[0]:
            raise Failure(
                f"expected {needle} on the shadowed view.\n{carrying[0]}"
            )


def test_linear_gradient(bundle: Path) -> None:
    """`backgroundImage: 'linear-gradient(...)'` reaches the view, resolved.

    The dump carries the gradient's line and its stop count, which is the part
    worth comparing across hosts: both resolve the angle, the box size and CSS's
    colour-stop fixup through the same shared code, so the two trees agreeing is
    what says neither host did its own arithmetic.

    e2e/views.tsx asks for 135 degrees on an 80x40 box, which is the case that
    tells the spec's construction from the obvious one: the line is the
    perpendicular construction, (10,-10) to (70,50) -- longer than the box, ends
    outside it, centred on it -- where the diagonal would be (0,0) to (80,40).

    """
    app = bundle_app(bundle.parent, "views")

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "3000"
    for name in ("BASALT_TEST_TAP", "BASALT_TEST_SECONDARY_TAP", "BASALT_TEST_TYPE",
                 "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL",
                 "BASALT_TEST_MENU", "BASALT_TEST_CLOSE_WINDOW"):
        env.pop(name, None)

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env["BASALT_DUMP_TREE"] = str(dump)
        result = run_host_process(
            [str(HOST), str(app), "BasaltViews"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        if not dump.exists():
            raise Failure("host wrote no widget tree")
        tree = dump.read_text()

    # This one, by its own line: the app has a radial gradient too, which the
    # same dump line reports differently, and a second linear one for the
    # background-size scenario next door.
    carrying = [line for line in tree.splitlines() if "gradient=((10,-10)-(70,50)" in line]
    if len(carrying) != 1:
        others = [line for line in tree.splitlines() if "gradient=" in line]
        raise Failure(
            "no view reports the gradient line the spec's construction gives for "
            "135 degrees on an 80x40 box. The gradients that did arrive are "
            "below.\n" + "\n".join(others)
        )
    if "gradient=((10,-10)-(70,50),2 stops,at=(0,0 80x40)" not in carrying[0]:
        raise Failure(
            "the gradient line is not where the spec puts it. The diagonal, "
            "(0,0)-(80,40), is the usual wrong answer; so is an angle measured "
            "anticlockwise or from the wrong axis.\n" + carrying[0]
        )


def test_accessibility_labelled_by(bundle: Path) -> None:
    """`accessibilityLabelledBy` resolves a `nativeID` to the view that has it.

    A relation rather than a copied string: the field's name lives on the caption
    beside it, so a caption that changes its text does not leave a stale copy
    behind. GTK gets an AT-SPI LABELLED_BY relation and macOS an
    `accessibilityTitleUIElement`.

    The scenario is here because of *when* it resolves. e2e/a11y.tsx deliberately
    puts the field before its caption, which is the ordinary way round, and Fabric
    mounts in tree order -- so the field is mounted while the id it names does not
    exist yet. A resolution done as the props arrived finds nothing and stays
    wrong, and that is what the unit tests on every host fail on when the
    resolution is moved. This asserts the end of it: the relation is on the view
    after the app has rendered.
    """
    app = bundle_app(bundle.parent, "a11y")

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "3000"
    for name in ("BASALT_TEST_TAP", "BASALT_TEST_SECONDARY_TAP", "BASALT_TEST_TYPE",
                 "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL",
                 "BASALT_TEST_MENU", "BASALT_TEST_CLOSE_WINDOW"):
        env.pop(name, None)

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env["BASALT_DUMP_TREE"] = str(dump)
        result = run_host_process(
            [str(HOST), str(app), "BasaltA11y"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        if not dump.exists():
            raise Failure("host wrote no widget tree")
        tree = dump.read_text()

    related = [line for line in tree.splitlines() if "labelled-by=" in line]
    if len(related) != 1:
        raise Failure(
            f"{len(related)} views report a labelled-by relation; the app sets one "
            f"on one.\n{tree}"
        )
    # The tag it names has to be a view that is actually there, and the one
    # carrying the caption: a relation pointing at nothing is the failure this
    # cannot be allowed to pass as.
    named = related[0].split("labelled-by=")[1].split()[0]
    caption = [line for line in tree.splitlines()
               if f"tag={named} " in line and "Save the document" in line]
    if not caption:
        raise Failure(
            f"the relation names tag {named}, which is not the caption. The usual "
            "cause is resolving the nativeID as the props arrive, before the "
            f"caption is mounted.\n{tree}"
        )


def test_test_id(bundle: Path) -> None:
    """`testID` reaches the view, and the platform's identifier for it.

    The prop every app under test sets, and no host read it until 2026-10-09:
    an identifier for whoever is driving the app from outside, which each
    platform has its own name for.

    | Host | Where it goes |
    | --- | --- |
    | GTK | the accessible id, `GtkAccessibleIface::get_accessible_id` |
    | AppKit | `accessibilityIdentifier` |
    | Win32 | UIA's `UIA_AutomationIdPropertyId` |

    This asserts the dump, which carries React Native's spelling on all three so
    that the trees compare. That each *platform* published it is asserted in
    each host's own suite, which is the same division the `role=` line uses --
    and on GTK it needs 4.22, the version where the vfunc arrived, so there the
    unit test is the one with a version guard on it and this is not.
    """
    app = bundle_app(bundle.parent, "a11y")

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "3000"
    for name in ("BASALT_TEST_TAP", "BASALT_TEST_SECONDARY_TAP", "BASALT_TEST_TYPE",
                 "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL",
                 "BASALT_TEST_MENU", "BASALT_TEST_CLOSE_WINDOW"):
        env.pop(name, None)

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env["BASALT_DUMP_TREE"] = str(dump)
        result = run_host_process(
            [str(HOST), str(app), "BasaltA11y"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        if not dump.exists():
            raise Failure("host wrote no widget tree")
        tree = dump.read_text()

    carrying = [line for line in tree.splitlines() if "testid=" in line]
    if len(carrying) != 1:
        raise Failure(
            f"{len(carrying)} views report a testID; e2e/a11y.tsx sets one.\n{tree}"
        )
    if "testid=save-button" not in carrying[0]:
        raise Failure(f"the testID is not the one the app set.\n{carrying[0]}")
    # On the view that asked for it, which is the button rather than the <Text>
    # inside it: `testID` does not inherit, and a host that put it on every
    # descendant would pass the count above only by accident.
    if "role=button" not in carrying[0]:
        raise Failure(
            "the testID landed on a view that is not the button that set it.\n"
            f"{carrying[0]}"
        )


def test_modal_view(bundle: Path) -> None:
    """`accessibilityViewIsModal` reaches the view, and the platform's flag.

    One prop, three names: ARIA's `aria-modal` on GTK, `accessibilityModal` on
    AppKit, and UIA's `IsDialog` on Windows. All three mean the same thing to a
    screen reader, which is to stay inside the element rather than reading the
    views behind it, and none of the three hosts read it until 2026-10-09.

    The dump carries React Native's word, so this runs everywhere; that each
    platform really set its own flag is asserted in each host's own suite.
    """
    app = bundle_app(bundle.parent, "a11y")

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "3000"
    for name in ("BASALT_TEST_TAP", "BASALT_TEST_SECONDARY_TAP", "BASALT_TEST_TYPE",
                 "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL",
                 "BASALT_TEST_MENU", "BASALT_TEST_CLOSE_WINDOW"):
        env.pop(name, None)

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env["BASALT_DUMP_TREE"] = str(dump)
        result = run_host_process(
            [str(HOST), str(app), "BasaltA11y"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        if not dump.exists():
            raise Failure("host wrote no widget tree")
        tree = dump.read_text()

    modal = [line for line in tree.splitlines() if " modal" in line]
    if len(modal) != 1:
        raise Failure(
            f"{len(modal)} views report being modal; e2e/a11y.tsx marks one.\n{tree}"
        )
    # On the view that asked, which is the checkbox: the prop does not inherit,
    # and a host that set it on every descendant would fail the count above.
    if "role=checkbox" not in modal[0]:
        raise Failure(f"the modal flag landed on the wrong view.\n{modal[0]}")


def test_accessibility_order(bundle: Path) -> None:
    """`experimental_accessibilityOrder` reaches the view, resolved to tags.

    A parent lists its children by `nativeID` in the order a screen reader
    should read them, which is the one accessibility prop that needs a *lookup*
    rather than a value: the ids name views that may not have mounted yet.
    `core/LabelRegistry.h` already did that for `accessibilityLabelledBy` and
    now holds both relations against one index.

    Each host expresses it in its own way and the dump prints neither: GTK sets
    `aria-flowto`, AppKit replaces `accessibilityChildren`, and the line here is
    the resolved tags in the order the app asked for. That is the half worth
    asserting end to end, since the ids have to survive the JavaScript side as
    well: `experimental_accessibilityOrder` is declared in the Android view
    config and *not* in `ReactNativeApi.d.ts`, so the app casts and only a real
    run can say whether React sent it.

    Windows reads no reading order yet, so it is skipped by name.
    """
    if PLATFORM == "windows":
        raise Skipped("UIA has no reading-order property; the tree order is the order")

    app = bundle_app(bundle.parent, "a11y")

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "3000"
    for name in ("BASALT_TEST_TAP", "BASALT_TEST_SECONDARY_TAP", "BASALT_TEST_TYPE",
                 "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL",
                 "BASALT_TEST_MENU", "BASALT_TEST_CLOSE_WINDOW"):
        env.pop(name, None)

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env["BASALT_DUMP_TREE"] = str(dump)
        result = run_host_process(
            [str(HOST), str(app), "BasaltA11y"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        if not dump.exists():
            raise Failure("host wrote no widget tree")
        tree = dump.read_text()

    ordered = [line for line in tree.splitlines() if "a11y-order=" in line]
    if len(ordered) != 1:
        raise Failure(
            f"{len(ordered)} views report a reading order; e2e/a11y.tsx sets one.\n{tree}"
        )
    tags = re.search(r"a11y-order=([\d,]+)", ordered[0])
    if tags is None:
        raise Failure(f"could not read the order off the line.\n{ordered[0]}")
    resolved = [int(tag) for tag in tags.group(1).split(",")]
    if len(resolved) != 2:
        raise Failure(
            f"the order resolved to {len(resolved)} views; the app named two.\n{ordered[0]}"
        )
    # The app asked for the *second* child before the first, so the tags come
    # back descending. An order that silently followed the mount order would
    # come back ascending, which is the failure this is here to catch.
    if resolved[0] < resolved[1]:
        raise Failure(
            "the reading order is the mount order; the app asked for the "
            f"second child first.\n{ordered[0]}"
        )


def test_accessibility_live_region(bundle: Path) -> None:
    """`accessibilityLiveRegion` reads a status message out when it changes.

    No desktop models this as a property of a view: GTK announces at a moment
    through `gtk_accessible_announce`, macOS posts an
    `NSAccessibilityAnnouncementRequested` notification, and Windows raises a
    `UiaRaiseNotificationEvent`, which is the only call in UI Automation that
    speaks a string. So honouring the prop is
    change detection, and the two rules worth asserting end to end are that the
    *first* text says nothing -- a screen appearing is not news, and a host that
    got this wrong would have every screen with a status line read itself out --
    and that a change says the new text once.

    e2e/a11y.tsx has a status line that goes from "Saving" to "Saved" a second
    after mount. Nothing in an automated run is connected to AT-SPI, or running
    VoiceOver or Narrator, so each host logs what it announced, which is what
    this reads.
    """
    app = bundle_app(bundle.parent, "a11y")

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "3000"
    for name in ("BASALT_TEST_TAP", "BASALT_TEST_SECONDARY_TAP", "BASALT_TEST_TYPE",
                 "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL",
                 "BASALT_TEST_MENU", "BASALT_TEST_CLOSE_WINDOW"):
        env.pop(name, None)

    result = run_host_process(
        [str(HOST), str(app), "BasaltA11y"],
        cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
    )
    _remember_output(result.stderr)
    check_output(result.stderr, result.returncode)
    logged = result.stdout + result.stderr

    announcements = re.findall(r"announced(?: \(assertive\))?: (.+)", logged)
    if not any(text.strip() == "Saved" for text in announcements):
        raise Failure(
            "the status line changed to 'Saved' and nothing was announced.\n"
            + tail_text(logged)
        )
    # "Saving" is what the region said when it appeared, and a screen appearing is
    # not news: announcing it would make every screen with a status line read
    # itself out on arrival.
    if any(text.strip() == "Saving" for text in announcements):
        raise Failure(
            "the region announced its first text, so this host announces on mount "
            f"rather than on change.\n{announcements}"
        )
    # Once, not once per transaction: a view re-renders for every reason under the
    # sun and React Native re-sends identical props on each mutation.
    if len([text for text in announcements if text.strip() == "Saved"]) != 1:
        raise Failure(
            f"'Saved' was announced {len(announcements)} times; once is the whole "
            f"point.\n{announcements}"
        )


def test_filter(bundle: Path) -> None:
    """A `filter` list reaches the view, composed.

    The dump carries what the filter's colour matrix makes of one probe colour,
    (1, 0.5, 0.25), rather than sixteen numbers per view. That is what makes this
    an assertion about arithmetic and not only about plumbing: e2e/views.tsx asks
    for `grayscale(1) brightness(1.2)`, so the probe's luminance is
    0.2126 + 0.5*0.7152 + 0.25*0.0722 = 0.588, and 1.2 of that is 0.7056, which
    is 0xb4. A host that dropped the second function reports #969696, one that
    used the older luminance weights reports #a4a4a4, and one that transposed the
    matrix reports three different channels.

    All three hosts resolve the list through the same shared code, so the trees
    agreeing on this line is what says none of them did its own arithmetic.
    """

    app = bundle_app(bundle.parent, "views")

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "3000"
    for name in ("BASALT_TEST_TAP", "BASALT_TEST_SECONDARY_TAP", "BASALT_TEST_TYPE",
                 "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL",
                 "BASALT_TEST_MENU", "BASALT_TEST_CLOSE_WINDOW"):
        env.pop(name, None)

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env["BASALT_DUMP_TREE"] = str(dump)
        result = run_host_process(
            [str(HOST), str(app), "BasaltViews"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        if not dump.exists():
            raise Failure("host wrote no widget tree")
        tree = dump.read_text()

    # The colour-matrix one, by its probe: the app also has a view whose filter
    # is a drop shadow, which the same line reports differently -- see
    # test_drop_shadow_filter.
    carrying = [line for line in tree.splitlines() if "filter=(probe=" in line]
    if len(carrying) != 1:
        raise Failure(
            f"{len(carrying)} views report a colour filter; the app sets one.\n{tree}"
        )
    if "filter=(probe=#b4b4b4ff)" not in carrying[0]:
        raise Failure(
            "the filter's matrix is not what grayscale(1) brightness(1.2) comes "
            "to. #969696 is the second function dropped, #a4a4a4 is the older "
            "luminance weights, and three different channels is a transposed "
            f"matrix.\n{carrying[0]}"
        )


def test_outline(bundle: Path) -> None:
    """The `outline` family reaches the view with all four of its numbers.

    CSS's outline is not a border: it is drawn outside the box, takes no layout
    space and has one width, colour, offset and style for the whole ring. So the
    frame says nothing about whether it arrived, and neither does a snapshot on
    macOS -- the ring is a CAShapeLayer stroke, which Core Animation draws during
    compositing and `renderInContext:` does not draw at all. The dump is the
    observable, and the three hosts spell it the same way, so this one assertion
    covers all of them.

    All four numbers, because each is a thing a host can lose while still drawing
    a plausible ring: an offset ignored leaves the ring against the box, a style
    dropped makes it solid, and a colour read through the border's four-sided
    cascade comes out transparent. e2e/views.tsx asks for a 3pt dashed #e0484d
    ring 2pt out.

    Where the ring actually lands is each host's own suite's business, in pixels:
    five tests on GTK, seven layer tests on AppKit, and six here for Win32,
    which strokes it with Direct2D.
    """

    app = bundle_app(bundle.parent, "views")

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "3000"
    for name in ("BASALT_TEST_TAP", "BASALT_TEST_SECONDARY_TAP", "BASALT_TEST_TYPE",
                 "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL",
                 "BASALT_TEST_MENU", "BASALT_TEST_CLOSE_WINDOW"):
        env.pop(name, None)

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env["BASALT_DUMP_TREE"] = str(dump)
        result = run_host_process(
            [str(HOST), str(app), "BasaltViews"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        if not dump.exists():
            raise Failure("host wrote no widget tree")
        tree = dump.read_text()

    carrying = [line for line in tree.splitlines() if "outline=" in line]
    if len(carrying) != 1:
        raise Failure(
            f"{len(carrying)} views report an outline; the app sets one.\n{tree}"
        )
    if "outline=(3,2,#e0484dff,dashed)" not in carrying[0]:
        raise Failure(
            "the outline is not the one the app asked for. A missing offset "
            "reads (3,0,...), a dropped style has no trailing word, and a "
            f"colour that did not resolve is #00000000.\n{carrying[0]}"
        )


def test_drop_shadow_filter(bundle: Path) -> None:
    """`filter: drop-shadow(...)` reaches the view as a shadow of its alpha.

    The last of the nine filter functions, and the one that is not a colour map:
    it shadows the subtree's alpha rather than its box, which is the difference
    from `boxShadow` and is why it could not be folded into the colour matrix
    the other seven collapse to.

    The number worth comparing is the blur. React Native parses the third length
    into a field it calls `standardDeviation` and hands that over, and the two
    hosts want different things with it: `CALayer.shadowRadius` is a standard
    deviation and takes it unchanged, while GSK's shadow radius is CSS's and
    takes twice it -- which a GTK pixel test measured rather than assumed, by
    comparing a shadow against a blur of the same number. So both dumps print
    the standard deviation, and a host that doubled or halved on the way reports
    6 or 1.5 where this asks for 3.

    e2e/views.tsx asks for `drop-shadow(4px 6px 3px rgba(0, 0, 0, 0.5))`.

    All three hosts now: `CLSID_D2D1Shadow` takes a standard deviation too, so
    the Win32 half converts nothing either.
    """

    app = bundle_app(bundle.parent, "views")

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "3000"
    for name in ("BASALT_TEST_TAP", "BASALT_TEST_SECONDARY_TAP", "BASALT_TEST_TYPE",
                 "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL",
                 "BASALT_TEST_MENU", "BASALT_TEST_CLOSE_WINDOW"):
        env.pop(name, None)

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env["BASALT_DUMP_TREE"] = str(dump)
        result = run_host_process(
            [str(HOST), str(app), "BasaltViews"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        if not dump.exists():
            raise Failure("host wrote no widget tree")
        tree = dump.read_text()

    carrying = [line for line in tree.splitlines() if "filter=(shadow=" in line]
    if len(carrying) != 1:
        raise Failure(
            f"{len(carrying)} views report a drop shadow; the app sets one.\n{tree}"
        )
    if "filter=(shadow=(4,6,3,#00000080))" not in carrying[0]:
        raise Failure(
            "the drop shadow is not the one the app asked for. A 6 or a 1.5 is a "
            "standard deviation converted on the way here rather than in the "
            "host, and an offset of (4,-6) is a sign that did not survive this "
            f"platform's coordinates.\n{carrying[0]}"
        )


def test_view_flattening(bundle: Path) -> None:
    """A view with nothing to draw is not mounted, and `collapsable` says so.

    `ViewShadowNode::initialize` decides this, not a host: a view with no
    background, border, shadow, image, outline, test id, transform, opacity,
    event handler or accessibility of its own leaves the `FormsView` trait
    unset, and the differentiator never mounts it. `collapsable: false` is the
    opt-out, and `collapsableChildren` the same for a subtree.

    So neither prop is for a host to read -- and that is worth one scenario
    rather than a sentence, because flattening is the difference between the view
    tree React describes and the widget tree a host builds, and nothing here had
    ever checked that the two differ in the way Fabric intends. A host that
    mounted everything would be slower and would still pass every other test in
    this file.

    e2e/views.tsx has two bare views: a 7x3 one that should be flattened away and
    a 9x3 one that asks not to be. The sizes are what tells them apart, being
    independent of where a flex row puts them.
    """
    app = bundle_app(bundle.parent, "views")

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "3000"
    for name in ("BASALT_TEST_TAP", "BASALT_TEST_SECONDARY_TAP", "BASALT_TEST_TYPE",
                 "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL",
                 "BASALT_TEST_MENU", "BASALT_TEST_CLOSE_WINDOW"):
        env.pop(name, None)

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env["BASALT_DUMP_TREE"] = str(dump)
        result = run_host_process(
            [str(HOST), str(app), "BasaltViews"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        if not dump.exists():
            raise Failure("host wrote no widget tree")
        tree = dump.read_text()

    flattened = [line for line in tree.splitlines() if "7x3" in line]
    if flattened:
        raise Failure(
            "a view with nothing to draw was mounted anyway, so Fabric's view "
            "flattening is not reaching this host.\n" + "\n".join(flattened)
        )

    kept = [line for line in tree.splitlines() if "9x3" in line]
    if len(kept) != 1:
        raise Failure(
            f"{len(kept)} views match the one that asked not to be flattened; "
            "`collapsable: false` is the only thing keeping it, so this is that "
            f"prop being dropped.\n{tree}"
        )


def test_on_layout(bundle: Path) -> None:
    """`onLayout` fires, which nothing here had ever checked.

    It is the one prop in `BaseViewProps` that no host reads and none should:
    `YogaLayoutableShadowNode` collects the nodes whose layout changed into
    `layoutContext.affectedNodes` and the Scheduler dispatches the event from
    them, so the whole path is ReactCommon's and a host that implemented it
    would be implementing it twice.

    Which is exactly why it is worth one scenario: "handled upstream" is a claim
    about code nobody here wrote, and the parts of ReactCommon this platform does
    not drive the way Android and iOS do are where the gaps have been -- the cxx
    `TextLayoutManager` has no `measureLines` at all, and that was found the same
    way, by asserting something that was assumed to work.

    e2e/views.tsx logs the size of a fixed 40x40 box. The size rather than the
    position, because that is the same number on every host where a place in a
    flex row is not.
    """
    app = bundle_app(bundle.parent, "views")

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "3000"
    for name in ("BASALT_TEST_TAP", "BASALT_TEST_SECONDARY_TAP", "BASALT_TEST_TYPE",
                 "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL",
                 "BASALT_TEST_MENU", "BASALT_TEST_CLOSE_WINDOW"):
        env.pop(name, None)

    result = run_host_process(
        [str(HOST), str(app), "BasaltViews"],
        cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
    )
    _remember_output(result.stderr)
    check_output(result.stderr, result.returncode)

    expect_logged(
        "onLayout 40x40",
        "no onLayout event reached the app, so every component that measures "
        "itself -- which is most of them -- is broken on this platform",
    )


def test_background_size_position_repeat(bundle: Path) -> None:
    """`backgroundSize`, `backgroundPosition` and `backgroundRepeat` reach the view.

    All three apply to a gradient, CSS treating one as an image, and all three
    were ignored by every host until 2026-10-09 on a note that said they were
    waiting for an image loader. React Native's iOS half applies them to
    gradients too, and core/BackgroundLayers.h is ported from it.

    The dump carries where the image goes and the tile it repeats in, which is
    the whole of what these props do. e2e/views.tsx asks for a 20pt image 5pt in
    from the top left of a 60x40 box, repeating -- and what is drawn is the
    *first* tile, which is one period before the painting area so that the
    pattern covers it rather than starting mid-box: 5 - 20 is -15. The authored
    position is in there, 5 being -15 plus one period, and the three props are
    still told apart by it:

      a 60x40 image          the size was ignored
      an image at (0,0)      the position was ignored, which backs up to 0
      no tile at all         the repeat was ignored

    """
    app = bundle_app(bundle.parent, "views")

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "3000"
    for name in ("BASALT_TEST_TAP", "BASALT_TEST_SECONDARY_TAP", "BASALT_TEST_TYPE",
                 "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL",
                 "BASALT_TEST_MENU", "BASALT_TEST_CLOSE_WINDOW"):
        env.pop(name, None)

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env["BASALT_DUMP_TREE"] = str(dump)
        result = run_host_process(
            [str(HOST), str(app), "BasaltViews"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        if not dump.exists():
            raise Failure("host wrote no widget tree")
        tree = dump.read_text()

    # By the image's own rectangle: every gradient reports a tile, `repeat` being
    # React Native's default for all of them.
    carrying = [line for line in tree.splitlines() if "at=(-15,-15 20x20)" in line]
    if len(carrying) != 1:
        tiled = [line for line in tree.splitlines() if "gradient=" in line]
        raise Failure(
            "no view reports the 20pt image the three props ask for. The "
            "gradients that did arrive are below.\n" + "\n".join(tiled)
        )
    if "at=(-15,-15 20x20),tile=(-15,-15 20x20)" not in carrying[0]:
        raise Failure(
            "the background image is not tiled the way the props ask.\n" + carrying[0]
        )


def test_radial_gradient(bundle: Path) -> None:
    """`backgroundImage: 'radial-gradient(...)'` reaches the view, resolved.

    The ending shape is the specified half and the one worth comparing across
    hosts: CSS gives six ways to size it and four corners to measure to, and
    every wrong answer still draws a radial gradient. Both hosts resolve it
    through core/Gradients.h, so the two dumps agreeing is what says neither did
    its own arithmetic.

    e2e/views.tsx asks for `circle at 30% 30%` on a 60x40 box with no size, which
    is CSS's default of `farthest-corner`: from (18, 12) the farthest corner is
    the bottom right, 42 across and 28 down, so the radius is hypot(42, 28),
    which is 50.4777. A host that took the closest corner reports 21.6, one that
    took the farthest side reports 42, and one that defaulted the centre reports
    a radius of 36 at (30, 20).

    """
    app = bundle_app(bundle.parent, "views")

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "3000"
    for name in ("BASALT_TEST_TAP", "BASALT_TEST_SECONDARY_TAP", "BASALT_TEST_TYPE",
                 "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL",
                 "BASALT_TEST_MENU", "BASALT_TEST_CLOSE_WINDOW"):
        env.pop(name, None)

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env["BASALT_DUMP_TREE"] = str(dump)
        result = run_host_process(
            [str(HOST), str(app), "BasaltViews"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        if not dump.exists():
            raise Failure("host wrote no widget tree")
        tree = dump.read_text()

    carrying = [line for line in tree.splitlines() if "gradient=(radial" in line]
    if len(carrying) != 1:
        raise Failure(
            f"{len(carrying)} views report a radial gradient; the app sets one.\n{tree}"
        )
    if "gradient=(radial (18,12) 50.4777x50.4777,2 stops,at=(0,0 60x40)" not in carrying[0]:
        raise Failure(
            "the ending shape is not the one CSS asks for: a circle through the "
            "farthest corner from (18,12), which is hypot(42,28). The closest "
            "corner is 21.6, the farthest side is 42, and a defaulted centre is "
            f"36 at (30,20).\n{carrying[0]}"
        )


def test_legacy_shadow(bundle: Path) -> None:
    """The four iOS shadow props arrive as the CSS shadow they describe.

    `shadowColor`, `shadowOffset`, `shadowOpacity` and `shadowRadius` are React
    Native's older spelling of a drop shadow, and they are in every component
    written before `boxShadow` existed. They are iOS-only -- Android's view
    config does not carry them -- and these hosts honour them anyway, which
    core/LegacyShadow.h is the decision and the reason for.

    Two conversions are not identity, and this is what reads them back: the
    radius doubles, a `CALayer` blur radius being a standard deviation where
    CSS's is twice one, and the opacity multiplies the colour's alpha. So
    e2e/views.tsx asking for radius 3 at opacity 0.5 in black has to arrive as a
    blur of 6 at #00000080. A host that passed the radius through reports a 3,
    and one that dropped the opacity reports #000000ff.

    """
    app = bundle_app(bundle.parent, "views")

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "3000"
    for name in ("BASALT_TEST_TAP", "BASALT_TEST_SECONDARY_TAP", "BASALT_TEST_TYPE",
                 "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL",
                 "BASALT_TEST_MENU", "BASALT_TEST_CLOSE_WINDOW"):
        env.pop(name, None)

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env["BASALT_DUMP_TREE"] = str(dump)
        result = run_host_process(
            [str(HOST), str(app), "BasaltViews"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        if not dump.exists():
            raise Failure("host wrote no widget tree")
        tree = dump.read_text()

    carrying = [line for line in tree.splitlines() if "shadow=(2,4,6,0,#00000080)" in line]
    if len(carrying) != 1:
        shadowed = [line for line in tree.splitlines() if "shadow=" in line]
        raise Failure(
            "no view reports the shadow the legacy props ask for: 2 and 4 out, a "
            "blur of 6 -- twice the radius -- in black at half alpha. The views "
            "that do report a shadow are below.\n" + "\n".join(shadowed)
        )


def test_mix_blend_mode(bundle: Path) -> None:
    """`mixBlendMode` reaches the view as the keyword all three hosts take.

    What the blend does to the pixels is asserted on GTK and on Windows, where
    the picture is readable and a blended pixel can be compared against an
    unblended one. macOS composites in the window server, and
    `renderInContext:` -- which is what this project's snapshots use --
    composites nothing, so there is no picture to compare there. The dump is
    what all three can be asked, and it carries the keyword rather than each
    host's own spelling: GTK maps it to a `GskBlendMode`, macOS to a Core Image
    filter and Windows to a `D2D1_BLEND_MODE`, and core/BlendModes.h is the one
    place that says what the value is called.

    e2e/views.tsx asks for `multiply` on a child of a red box, which is also the
    arrangement the pixel tests use.
    """
    app = bundle_app(bundle.parent, "views")

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "3000"
    for name in ("BASALT_TEST_TAP", "BASALT_TEST_SECONDARY_TAP", "BASALT_TEST_TYPE",
                 "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL",
                 "BASALT_TEST_MENU", "BASALT_TEST_CLOSE_WINDOW"):
        env.pop(name, None)

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env["BASALT_DUMP_TREE"] = str(dump)
        result = run_host_process(
            [str(HOST), str(app), "BasaltViews"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        if not dump.exists():
            raise Failure("host wrote no widget tree")
        tree = dump.read_text()

    carrying = [line for line in tree.splitlines() if "blend=" in line]
    if len(carrying) != 1:
        raise Failure(
            f"{len(carrying)} views report a blend mode; the app sets one.\n{tree}"
        )
    if "blend=multiply" not in carrying[0]:
        raise Failure(
            "the blend mode is not the one the app asked for. A host that mapped "
            "the enum by position reports a neighbour's keyword, which is what "
            f"core/BlendModes.h exists to make impossible.\n{carrying[0]}"
        )


def test_font_scaling(bundle: Path) -> None:
    """`allowFontScaling` and `maxFontSizeMultiplier`, against a desktop that scales.

    The pair that decides whether a label grows when the desktop is set to large
    text. Both hosts multiplied by `fontSizeMultiplier` and read neither of them
    until 2026-10-09, which is the usual shape of this: the prop that does the
    work was wired and the two that control it were not.

    `BASALT_TEST_FONT_SCALE` supplies the scale, and is an instrument rather
    than a shortcut: GTK reads a real one from `GtkSettings:gtk-xft-dpi`, macOS
    publishes none at all, and a scenario that could only run where the desktop
    happened to be set to large text would run nowhere. What it does not skip is
    anything above it -- the scale goes in where the platform's own goes, and
    core/FontScaling.h decides the rest.

    Three paragraphs in one run. The plain one grows, the one with
    `allowFontScaling={false}` does not move at all, and the one with
    `maxFontSizeMultiplier={1.25}` grows by less than the plain one, the scale
    having asked for 1.6. Heights rather than widths: a width depends on where
    the card wrapped.
    """
    if PLATFORM == "windows":
        raise Skipped("the Win32 host reads no text scale; UISettings is WinRT")

    app = bundle_app(bundle.parent, "text")

    def heights(scale):
        env = dict(os.environ)
        env["BASALT_QUIT_AFTER_MS"] = "3000"
        for name in ("BASALT_TEST_TAP", "BASALT_TEST_SECONDARY_TAP", "BASALT_TEST_TYPE",
                     "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL",
                     "BASALT_TEST_MENU", "BASALT_TEST_CLOSE_WINDOW"):
            env.pop(name, None)
        if scale is None:
            env.pop("BASALT_TEST_FONT_SCALE", None)
        else:
            env["BASALT_TEST_FONT_SCALE"] = scale

        with tempfile.TemporaryDirectory() as directory:
            dump = Path(directory) / "tree.txt"
            env["BASALT_DUMP_TREE"] = str(dump)
            result = run_host_process(
                [str(HOST), str(app), "BasaltText"],
                cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
            )
            _remember_output(result.stderr)
            check_output(result.stderr, result.returncode)
            if not dump.exists():
                raise Failure("host wrote no widget tree")
            tree = dump.read_text()

        # By the string each label holds, which is what the dump prints and what
        # survives a card being reordered.
        found = {}
        for label, needle in (
            ("scaling", "Scales with the desktop"),
            ("fixed", "Fixed whatever the desktop says"),
            ("capped", "Capped at a quarter larger"),
        ):
            for line in tree.splitlines():
                if 'text="' + needle + '"' not in line:
                    continue
                frame = re.search(r"frame=\(([-\d.]+),([-\d.]+) ([\d.]+)x([\d.]+)\)", line)
                if frame is None:
                    raise Failure(f"no frame on the {label} label's line: {line}")
                found[label] = float(frame.group(4))
                break
        if len(found) != 3:
            raise Failure(
                "the three font-scaling labels are not all in the tree; "
                f"found {sorted(found)}.\n{tree}"
            )
        return found

    plain = heights(None)
    scaled = heights("1.6")

    if not scaled["scaling"] > plain["scaling"] + 1.0:
        raise Failure(
            "a desktop text scale of 1.6 did not grow a plain paragraph: "
            f'{plain["scaling"]} -> {scaled["scaling"]}. On GTK the scale comes '
            "from gtk-xft-dpi and on macOS from nothing at all, so both honour "
            "the instrument instead; a host ignoring it reads as no change."
        )

    if abs(scaled["fixed"] - plain["fixed"]) > 0.5:
        raise Failure(
            "allowFontScaling={false} grew anyway: "
            f'{plain["fixed"]} -> {scaled["fixed"]}'
        )

    # Between the two: a quarter larger rather than three fifths.
    if not plain["capped"] < scaled["capped"] < scaled["scaling"]:
        raise Failure(
            "maxFontSizeMultiplier={1.25} did not cap the growth at a quarter: "
            f'{plain["capped"]} -> {scaled["capped"]}, while the uncapped '
            f'paragraph went {plain["scaling"]} -> {scaled["scaling"]}'
        )


def test_runtime_font(bundle: Path) -> None:
    """A font loaded while the app runs is the font the paragraph is laid out in.

    `expo-font` on a non-web platform is one call, `ExpoFontLoader.loadAsync`,
    and then `fontFamily` working afterwards. The app chose the name, the file
    calls itself something else, and core/FontRegistry.h is what makes the two
    the same font. Both hosts' suites assert the platform half; what only an app
    can show is the whole path -- JavaScript, the JSI host function, the
    registry, the measurement cache, the paragraph on screen.

    Two paragraphs with the same string at the same size: one in the host's
    default font, one in the loaded family. The widths must differ, which they
    only can if the family was found. A monospaced file is loaded for that
    reason, since two proportional faces can measure a string identically.

    **The load happens after the first render**, which is how `useFonts` works
    and the reason the hosts drop their measurement caches when a font arrives.
    The second paragraph is mounted by the state change the load causes, so it
    is measured on the far side of the registration.

    e2e/fonts.tsx tries several paths for a monospaced system font. Which exist
    is a property of the machine, so a box with none of them skips rather than
    fails, and the skip names what was tried.
    """
    app = bundle_app(bundle.parent, "fonts")

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "4000"
    for name in ("BASALT_TEST_TAP", "BASALT_TEST_SECONDARY_TAP", "BASALT_TEST_TYPE",
                 "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL",
                 "BASALT_TEST_MENU", "BASALT_TEST_CLOSE_WINDOW",
                 "BASALT_TEST_FONT_SCALE"):
        env.pop(name, None)

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env["BASALT_DUMP_TREE"] = str(dump)
        result = run_host_process(
            [str(HOST), str(app), "BasaltFonts"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        if not dump.exists():
            raise Failure("host wrote no widget tree")
        tree = dump.read_text()

    said = result.stderr
    if "font: no ExpoFontLoader" in said:
        # The whole Expo runtime is behind BASALT_HAS_EXPO, so a plain checkout
        # and every CI job have no loader to call: runtime fonts are an Expo
        # feature on every React Native platform, not only on these. Both hosts'
        # own suites assert the registry and the layout without it.
        raise Skipped(
            "needs a host built with -DBASALT_EXPO_MODULES_CORE; without Expo "
            "there is no ExpoFontLoader to call"
        )
    if "font: none of the candidates loaded" in said:
        tried = [line.split("font: ", 1)[1] for line in said.splitlines() if "did not load" in line]
        raise Skipped(
            "this machine has no monospaced font at any of the paths e2e/fonts.tsx "
            f"knows: {'; '.join(tried) or 'none reported'}"
        )

    loaded = [line for line in said.splitlines() if "font: loaded " in line]
    if len(loaded) != 1:
        raise Failure(
            f"{len(loaded)} fonts reported as loaded; the app loads one.\n"
            f"{tail_text(said, 30)}"
        )
    if "font: isLoaded true" not in said:
        raise Failure(
            "the loader does not consider the font loaded immediately after "
            f"resolving, which is what `useFonts` asks it.\n{tail_text(said, 30)}"
        )
    if "font: names BasaltRuntimeFont" not in said:
        raise Failure(
            "getLoadedFonts does not report the name the app chose, which is the "
            f"only name the app knows it by.\n{tail_text(said, 30)}"
        )

    widths = []
    for line in tree.splitlines():
        if 'text="Handgloves 0123"' not in line:
            continue
        frame = re.search(r"frame=\(([-\d.]+),([-\d.]+) ([\d.]+)x([\d.]+)\)", line)
        if frame is None:
            raise Failure(f"no frame on a specimen's line: {line}")
        widths.append(float(frame.group(3)))

    if len(widths) != 2:
        raise Failure(
            f"{len(widths)} specimens in the tree rather than 2, so the loaded "
            f"font's paragraph never mounted.\n{tree}"
        )
    if abs(widths[0] - widths[1]) < 1.0:
        raise Failure(
            "the same string measures the same in the loaded font as in the "
            f"default one ({widths[0]} and {widths[1]}), so the family was not "
            f"found and the paragraph fell back.\n{tree}"
        )


def test_text_checking(bundle: Path) -> None:
    """`spellCheck` and `autoCorrect` reach the field.

    The pair a search box turns off, and neither host read either until
    2026-10-09. Each toolkit has a different half of it: GTK has an input hint
    for spell checking and nothing at all for autocorrection, AppKit has both as
    properties of the NSTextView a field is or borrows. So the dump carries what
    the app asked for, in both hosts' words, and each host's own suite asserts
    what its toolkit did with it.

    Asserting arrival is this scenario's job and it is not a formality: a
    TextInput prop reaches C++ only if `RCTTextInputViewConfig.js` declares it,
    and this platform learned that the hard way with `accessibilityViewIsModal`,
    which ReactCommon parses for everyone and only iOS declares.

    `e2e/input.tsx` carries the field. Windows mounts it too and reads neither
    prop, so it is skipped by name there.
    """
    if PLATFORM == "windows":
        raise Skipped("the Win32 field reads neither prop; an edit control has no spell check")

    app = bundle_app(bundle.parent, "input")

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "3000"
    for name in ("BASALT_TEST_TAP", "BASALT_TEST_SECONDARY_TAP", "BASALT_TEST_TYPE",
                 "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL",
                 "BASALT_TEST_MENU", "BASALT_TEST_CLOSE_WINDOW"):
        env.pop(name, None)

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env["BASALT_DUMP_TREE"] = str(dump)
        result = run_host_process(
            [str(HOST), str(app), "BasaltInput"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        if not dump.exists():
            raise Failure("host wrote no widget tree")
        tree = dump.read_text()

    asked = [line for line in tree.splitlines() if "spellcheck=" in line]
    if len(asked) != 1:
        raise Failure(
            f"{len(asked)} fields report a spell-check setting; e2e/input.tsx sets one.\n{tree}"
        )
    if "spellcheck=off" not in asked[0] or "autocorrect=off" not in asked[0]:
        raise Failure(f"the field did not ask for what the app wrote.\n{asked[0]}")
    # The other fields said nothing, and nothing is the third state rather than
    # a default: a host that resolved unset to false would mark all of them.
    if len([line for line in tree.splitlines() if "autocorrect=" in line]) != 1:
        raise Failure(
            "more than one field reports an autocorrect setting; unset is not "
            f"false.\n{tree}"
        )

    # `autoCapitalize` and `keyboardType` ride along on the same field, and are
    # the other way round: GTK honours both and AppKit can honour neither, so
    # both hosts *report* them and the support page carries the difference.
    # Every field reports these two, both props being plain enums with React
    # Native's own defaults rather than optionals.
    if "autocapitalize=none" not in asked[0] or "keyboard=email-address" not in asked[0]:
        raise Failure(
            f"the field did not ask for the capitalisation and keyboard the app "
            f"wrote.\n{asked[0]}"
        )
    defaults = [line for line in tree.splitlines() if "autocapitalize=sentences" in line]
    if not defaults:
        raise Failure(
            "no field reports React Native's default capitalisation, which every "
            f"field that said nothing should.\n{tree}"
        )


def test_writing_direction(bundle: Path) -> None:
    """`writingDirection` reaches the paragraph.

    The prop decides which edge a paragraph starts from, and each engine takes
    it in its own terms: a Pango context's base direction, an
    `NSParagraphStyle`'s `baseWritingDirection`, DirectWrite's
    `SetReadingDirection`. None of the three honoured it until 2026-10-09.

    What this asserts is the arrival, which is the half only an app can prove:
    `writingDirection` is a *style* prop, so it travels through
    `ReactNativeStyleAttributes` and the style flattener rather than through
    `validAttributes`, and a prop that is dropped there reaches no host at all.
    Whether each engine then *honoured* it is a picture, and all three suites
    take one: Latin text in a right-to-left paragraph has the same box and the
    same string, so the pixels are the only difference.

    Windows prints only the right-to-left case, where the other two also print
    `ltr` and `natural`: those two keep the name the app used, and that host
    keeps a boolean, DirectWrite taking a direction rather than a "decide for
    me". The line this reads is the same on all three.
    """

    app = bundle_app(bundle.parent, "text")

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "3000"
    for name in ("BASALT_TEST_TAP", "BASALT_TEST_SECONDARY_TAP", "BASALT_TEST_TYPE",
                 "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL",
                 "BASALT_TEST_MENU", "BASALT_TEST_CLOSE_WINDOW"):
        env.pop(name, None)

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env["BASALT_DUMP_TREE"] = str(dump)
        result = run_host_process(
            [str(HOST), str(app), "BasaltText"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        if not dump.exists():
            raise Failure("host wrote no widget tree")
        tree = dump.read_text()

    directed = [line for line in tree.splitlines() if "writing-dir=" in line]
    if len(directed) != 1:
        raise Failure(
            f"{len(directed)} paragraphs report a writing direction; e2e/text.tsx "
            f"sets one.\n{tree}"
        )
    if "writing-dir=rtl" not in directed[0]:
        raise Failure(
            f"the paragraph's direction is not the one the app asked for.\n{directed[0]}"
        )


def test_text_shadow(bundle: Path) -> None:
    """`textShadowColor`, `textShadowOffset` and `textShadowRadius` reach the view.

    Three props that every pre-CSS React Native title sets and that no host read
    until 2026-10-09. They are `TextAttributes`, so they arrive per fragment,
    and neither text engine here can draw a different shadow per run -- so
    core/TextShadows.h takes the first fragment that asks for one, which is the
    limit backlog/text.md records.

    The radius is the number worth comparing. React Native's iOS half puts
    `textShadowRadius` into `NSShadow.shadowBlurRadius`, which is a standard
    deviation rather than CSS's blur radius: AppKit hands it to
    `CGContextSetShadowWithColor` unchanged and GTK doubles it for `GskShadow`,
    so both dumps print what React Native parsed. e2e/text.tsx asks for 4.

    All three hosts print it, and the third takes the standard deviation
    unchanged too: `CLSID_D2D1Shadow`'s blur property is a standard deviation,
    so nothing converts there either.
    """

    app = bundle_app(bundle.parent, "text")

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "3000"
    for name in ("BASALT_TEST_TAP", "BASALT_TEST_SECONDARY_TAP", "BASALT_TEST_TYPE",
                 "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL",
                 "BASALT_TEST_MENU", "BASALT_TEST_CLOSE_WINDOW"):
        env.pop(name, None)

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env["BASALT_DUMP_TREE"] = str(dump)
        result = run_host_process(
            [str(HOST), str(app), "BasaltText"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        if not dump.exists():
            raise Failure("host wrote no widget tree")
        tree = dump.read_text()

    carrying = [line for line in tree.splitlines() if "text-shadow=" in line]
    if len(carrying) != 1:
        raise Failure(
            f"{len(carrying)} paragraphs report a text shadow; the app sets one.\n{tree}"
        )
    if "text-shadow=(2,3,4,#4d8cf2ff)" not in carrying[0]:
        raise Failure(
            "the text shadow is not the one the app asked for. An 8 or a 2 is a "
            "standard deviation converted on the way here rather than in the "
            f"host, and (2,-3) is a sign that did not survive.\n{carrying[0]}"
        )


def test_text_transform(bundle: Path) -> None:
    """`textTransform` changes the string the engine lays out.

    Not a paint-time effect: "shout" and "SHOUT" are different widths, so the
    transform has to happen before measurement or the paragraph wraps in the
    wrong place. That also makes it visible in the tree dump, which prints the
    text each host actually laid out -- so this reads it back on both.

    Both hosts use their own toolkit's Unicode case mapping, GLib's and
    NSString's, because a byte-wise transform looks right in English and leaves
    every accented letter alone. The unit tests on each side check that against
    "café" and "straße"; what this checks is that the prop arrives and that the
    two agree.

    `capitalize` is asserted too, surprising parts included: React Native's rule
    lowercases the rest of each word, so "iOS" becomes "Ios" on every platform.

    All three hosts now, each through its own platform's case mapping: GLib's,
    NSString's and `LCMapStringEx` with `LCMAP_LINGUISTIC_CASING`. The strings
    here are ASCII, so what this asserts is that the prop arrives and that the
    three agree; whether they agree on the awkward letters is asserted in each
    host's own suite, against the cases that platform's mapping can get wrong.
    """

    app = bundle_app(bundle.parent, "text")

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "3000"
    for name in ("BASALT_TEST_TAP", "BASALT_TEST_SECONDARY_TAP", "BASALT_TEST_TYPE",
                 "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL",
                 "BASALT_TEST_MENU", "BASALT_TEST_CLOSE_WINDOW"):
        env.pop(name, None)

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env["BASALT_DUMP_TREE"] = str(dump)
        result = run_host_process(
            [str(HOST), str(app), "BasaltText"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        if not dump.exists():
            raise Failure("host wrote no widget tree")
        tree = dump.read_text()

    for needle, why in (
        ('text="SHOUT QUIETLY"', "textTransform: 'uppercase' did not reach the text engine"),
        ('text="Ios And Android"', "textTransform: 'capitalize' did not reach the text engine"),
    ):
        if needle not in tree:
            raise Failure(f"{why}; expected {needle}.\n{tree}")
    # And the untransformed strings are gone, which is what says the engine laid
    # out the transformed text rather than drawing over the original.
    for leftover in ('text="shout quietly"', 'text="iOS and android"'):
        if leftover in tree:
            raise Failure(
                f"{leftover} is still in the tree, so the transform happens after "
                f"layout rather than before it.\n{tree}"
            )


def test_cursor_style(bundle: Path) -> None:
    """The `cursor` style property reaches the view.

    A desktop prop, and the one most obviously missing: until now every view on
    every host showed an arrow, a `<Pressable>` included. GTK takes CSS's keyword
    straight through `gtk_widget_set_cursor_from_name`; macOS maps it onto the
    NSCursors it has, and installs nothing for the half-dozen it has no cursor
    for, so those inherit rather than snapping back to an arrow.

    Two keywords, because they fail differently: `pointer`, which every host
    has, and `ns-resize`, which is hyphenated and is where a mapping table goes
    wrong.

    Runs e2e/views.js, the app scripts/compare_hosts.sh diffs between desktops,
    which is also where the backlog was wrong about this: its cursor entry
    described what is left as the work "beyond what the `cursor` style property
    covers", and nothing covered it.

    """
    app = bundle_app(bundle.parent, "views")

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "3000"
    for name in ("BASALT_TEST_TAP", "BASALT_TEST_SECONDARY_TAP", "BASALT_TEST_TYPE",
                 "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL",
                 "BASALT_TEST_MENU", "BASALT_TEST_CLOSE_WINDOW"):
        env.pop(name, None)

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env["BASALT_DUMP_TREE"] = str(dump)
        result = run_host_process(
            [str(HOST), str(app), "BasaltViews"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        if not dump.exists():
            raise Failure("host wrote no widget tree")
        tree = dump.read_text()

    lines = tree.splitlines()
    for keyword in ("pointer", "ns-resize"):
        if not any(f"cursor={keyword}" in line for line in lines):
            raise Failure(
                f"no view reports cursor={keyword}, so the prop did not reach the "
                f"view layer.\n{tree}"
            )
    # Exactly those two. A host that applied one view's cursor to the whole tree
    # would otherwise pass: the dump would still contain both keywords.
    carrying = [line for line in lines if "cursor=" in line]
    if len(carrying) != 2:
        raise Failure(
            f"{len(carrying)} views report a cursor; the app sets one on two.\n"
            + "\n".join(carrying)
        )


def test_window_limits(bundle: Path) -> None:
    """How big the window may be, and the fact that it is not the same list
    everywhere.

    Runs the second app in e2e/window.js: a minimum of 500x400, a maximum of
    800x600, and two buttons that ask for sizes outside both. A size is a
    request; a limit is what the window manager answers it with.

    Two halves, and the first is the one that runs everywhere.

      capabilities   what this desktop says it does. Asserted against what each
                     host actually implements, because the whole point of
                     answering is that an app can trust the answer -- a
                     `capabilities` that said "yes" and then did nothing would
                     be worse than no capabilities at all. macOS and Windows do
                     all five. Linux does the minimum and the resizable flag,
                     and does not do position, maximum size or always-on-top:
                     GTK4 removed `gtk_window_set_geometry_hints` and
                     `gtk_window_set_keep_above` because Wayland has no protocol
                     for either, so those two are settled rather than pending.

      clamping       that a request outside a limit comes back clamped. Only
                     where the platform claims the limit, and only where there
                     is a window manager to enforce it -- CI runs the Linux host
                     under Xvfb, which has neither.

    `setResizable` and `setAlwaysOnTop` are called and not asserted, which is
    the honest limit rather than an oversight: neither is observable from inside
    the app. A window that cannot be resized is still whatever size it is, and
    one that floats is still where it was. What this says about them is that the
    native path runs on all three without taking the host with it.
    """
    app = bundle_app(bundle.parent, "window")

    # The same layout as the app above: 24 of padding, a 22-tall label, then a
    # 48-tall row. The buttons are 150 wide from x=24, so their middles are
    # x=99 and x=261 at y=70.
    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "12000"
    env["BASALT_TEST_TAP"] = "99,70;261,70"
    for name in ("BASALT_TEST_TYPE", "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS",
                 "BASALT_TEST_SCROLL", "BASALT_TEST_MENU"):
        env.pop(name, None)

    result = run_host_process(
        [str(HOST), str(app), "BasaltWindowLimits"],
        cwd=REPO, env=env, capture_output=True, text=True, timeout=150,
    )
    _remember_output(result.stderr)
    check_output(result.stderr, result.returncode)
    logged = result.stdout + result.stderr

    if "window flags: resizable and always-on-top reached the host" not in logged:
        raise Failure(
            "setResizable or setAlwaysOnTop did not return. Neither can be "
            "asserted from inside the app; that they run at all is what this "
            f"checks.\n{tail_text(logged)}"
        )

    said = [line for line in logged.splitlines() if "window capabilities: " in line]
    if not said:
        raise Failure(f"the app never asked what the window can do.\n{tail_text(logged)}")
    answer = said[-1].split("window capabilities: ", 1)[1].strip()

    # What each host actually implements. Spelled out rather than derived, so
    # that a host quietly losing one of these fails here.
    expected = {
        "macos": "position=true minimumSize=true maximumSize=true resizable=true "
                 "alwaysOnTop=true",
        "windows": "position=true minimumSize=true maximumSize=true resizable=true "
                   "alwaysOnTop=true",
        "linux": "position=false minimumSize=true maximumSize=false resizable=true "
                 "alwaysOnTop=false",
    }[PLATFORM]
    if answer != expected:
        raise Failure(
            f"this host describes itself as\n  {answer}\nand the platform does\n"
            f"  {expected}\nAn app that trusts `capabilities` and gets a no-op is "
            "worse off than one with no capabilities at all."
        )

    if PLATFORM == "linux" and not has_window_manager():
        print(
            "        (no window manager, so the clamping half of this scenario "
            "could not run)"
        )
        return

    # What the window became after each request, rather than every size it has
    # ever been. The window opens at 900x700 and a limit set afterwards does not
    # reach back and shrink it -- no desktop does that, and neither does
    # Electron -- so the sizes before the first request are not evidence of
    # anything. What is being asserted is that a request made while a limit is
    # in force comes back inside it.
    def after(request: str) -> tuple[float, float] | None:
        _, marker, rest = logged.partition(f"window asked for {request}")
        if not marker:
            raise Failure(f"the app never asked for {request}.\n{tail_text(logged)}")
        for line in rest.splitlines():
            if "window limited bounds: " not in line:
                continue
            size = line.split("window limited bounds: ", 1)[1].strip()
            width, _, height = size.partition("x")
            return float(width), float(height)
        return None

    # Asking for 300x200 against a minimum of 500x400.
    smaller = after("300x200")
    if smaller is None:
        raise Failure(f"asking for 300x200 changed nothing.\n{tail_text(logged)}")
    if smaller[0] < 500 or smaller[1] < 400:
        raise Failure(
            f"setSize(300, 200) against a minimum of 500x400 gave {smaller}. "
            "AppKit's setFrame: clamps down to a maximum and not up to a "
            "minimum, which is why this is applied in core rather than left to "
            "the toolkit."
        )

    if "maximumSize=true" in expected:
        # Asking for 1400x1100 against a maximum of 800x600. Allowed to come
        # back smaller -- a display that cannot fit 800x600 is a window manager
        # doing its job -- but never larger.
        larger = after("1400x1100")
        if larger is None:
            raise Failure(f"asking for 1400x1100 changed nothing.\n{tail_text(logged)}")
        if larger[0] > 800 or larger[1] > 600:
            raise Failure(
                f"setSize(1400, 1100) against a maximum of 800x600 gave {larger}."
            )


def test_application_menu(bundle: Path) -> None:
    """The application menu, and the thing its absence quietly broke.

    Runs e2e/menu.js, which describes a File menu of its own and an Edit menu
    built entirely out of roles, and dumps whatever menu the platform actually
    installed.

    Read back from the platform rather than compared against what was sent,
    which is the point: it says a description became a real NSMenu or HMENU,
    with the shortcuts the platform attached to its roles. It is also the only
    way an automated run can see a menu bar -- a menu cannot be opened without
    a person, and BASALT_TEST_MENU answers popups rather than bars.

    The Edit menu is the half that matters and the half that is invisible. On
    macOS AppKit routes every key equivalent through the main menu before the
    responder chain sees it, so `role="copy"` is what makes Cmd-C reach a text
    field at all -- and this host shipped with a one-item Quit menu until the
    menu model existed, which meant copy, cut, paste, undo and select-all did
    nothing in every <TextInput> on macOS. This is what would catch that coming
    back.

    Linux asserts the opposite and asserts it deliberately: GNOME's guidelines
    have said to use a header bar with a menu button since GNOME 3 and GTK4
    removed the menu bar widget, so `Menu.isSupported` is false there and the
    dump is empty. That is a platform answering honestly rather than a gap.
    """
    app = bundle_app(bundle.parent, "menu")

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "menu.txt"
        env = dict(os.environ)
        env["BASALT_QUIT_AFTER_MS"] = "7000"
        env["BASALT_DUMP_MENU"] = str(dump)
        for name in ("BASALT_TEST_TAP", "BASALT_TEST_TYPE", "BASALT_TEST_HOVER",
                     "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL", "BASALT_TEST_MENU"):
            env.pop(name, None)

        result = run_host_process(
            [str(HOST), str(app), "BasaltMenu"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=150,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        menu = dump.read_text() if dump.exists() else ""

    both = result.stdout + result.stderr
    supported = "menu supported: true" in both
    if not supported and "menu supported: false" not in both:
        raise Failure(f"the app never said whether menus are supported:\n{tail_text(both)}")

    if not supported:
        # The honest answer, not a gap. See the docstring.
        if menu.strip() != "":
            raise Failure(
                "this platform reports no application menu and installed one "
                f"anyway:\n{menu}"
            )
        return

    # The app's own menu, with its own items.
    for expected in ("File", "New", "Open", "Edit"):
        if expected not in menu:
            raise Failure(f"no {expected!r} in the installed menu:\n{menu}")

    # The roles, which the platform filled in: neither the label nor the
    # shortcut came from the app.
    for role in ("Copy", "Paste", "Undo", "Select All"):
        if role not in menu:
            raise Failure(
                f"no {role!r} in the installed menu. A role is meant to arrive "
                f"with the platform's own word for it.\n{menu}"
            )

    # Disabled is carried through, which is the one item property a menu can
    # get wrong without anyone noticing until they click it.
    if "(disabled)" not in menu:
        raise Failure(f"an item disabled by the app was installed enabled:\n{menu}")


def test_debugging_overlay(bundle: Path) -> None:
    """React DevTools' element highlighter, which is driven only by commands.

    Runs e2e/overlay.js, which issues the commands DevTools would: the filled
    blue box over an inspected element, and the outline around something that
    just re-rendered. Directly rather than through DevTools, because DevTools
    is the only other thing that would and it needs a session attached.

    Two runs, because the second half of the contract is a disappearance. A
    trace update is meant to flash -- React Native's own overlay clears them
    rather than waiting to be told, since a box left behind after a component
    stopped re-rendering says the opposite of what it means -- so the later run
    asserts that nothing is left.

    The tree dump reports how many rectangles a view is drawing, because they
    are otherwise invisible to everything but a screenshot, and a command that
    arrived and drew nothing is exactly the failure worth catching.
    """
    app = bundle_app(bundle.parent, "overlay")

    def highlights(module: str, run_ms: int) -> tuple[int, str]:
        with tempfile.TemporaryDirectory() as directory:
            dump = Path(directory) / "tree.txt"
            env = dict(os.environ)
            env["BASALT_DUMP_TREE"] = str(dump)
            env["BASALT_QUIT_AFTER_MS"] = str(run_ms)
            for name in ("BASALT_TEST_TAP", "BASALT_TEST_TYPE", "BASALT_TEST_HOVER",
                         "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL"):
                env.pop(name, None)
            result = run_host_process(
                [str(HOST), str(app), module],
                cwd=REPO, env=env, capture_output=True, text=True,
                timeout=run_ms / 1000 + 90,
            )
            _remember_output(result.stderr)
            check_output(result.stderr, result.returncode)
            tree = dump.read_text() if dump.exists() else ""
            return tree.count("highlights="), result.stdout + result.stderr

    # An inspected element, which stays until it is cleared. Whenever the tree is
    # dumped, it is there -- which is what makes this the half that says the
    # commands are routed at all.
    drawn, logged = highlights("BasaltOverlay", 5000)
    if "overlay: highlighted an element" not in logged:
        raise Failure(f"the app never issued the command:\n{tail_text(logged)}")
    if drawn == 0:
        raise Failure(
            "the overlay commands arrived and drew nothing. The view mounts, so "
            "this is the command routing rather than the component."
        )

    # A trace update, which takes itself down. Five seconds against a lifetime
    # of one and a half, so the answer does not depend on how fast the machine
    # is -- an earlier version dumped at exactly the moment it expired and read
    # its own success as a failure on a slow runner.
    left, logged = highlights("BasaltOverlayTrace", 5000)
    if "overlay: highlighted a trace update" not in logged:
        raise Failure(f"the app never issued the command:\n{tail_text(logged)}")
    if left != 0:
        raise Failure(
            "a trace update was still on screen after its lifetime. It is meant "
            "to flash; one that stays says the opposite of what it means."
        )


def test_windows(bundle: Path) -> None:
    """More than one window, which is more than one React tree.

    Runs e2e/windows.js: a counter in the first window's state, a button to open
    a second, and the same counter rendered again over there.

    A window is a surface is a React root -- that is Fabric's grain rather than
    a decision this made -- so the second window is not the first one's tree
    moved across. What is asserted is the three things that follow from it:

      it renders        the second window has a tree of its own, under its own
                        header in the dump.

      input is routed   a tap in the second window is hit-tested against the
                        second window's view tree. Each window has its own touch
                        dispatcher, which is the whole point of them, and the
                        first one's would happily hit-test a tree that is not on
                        screen and report a press on whatever happened to be at
                        those coordinates. `BASALT_TEST_TAP` takes "x,y@3" for
                        exactly this.

      state crosses     pressing in the second window calls a setter that lives
                        in the first window's tree, and *both* re-render. That
                        is the half that says `<Window>` is passing elements
                        through rather than running something separate.

    Closing is asserted too, and it is the part with a real ordering hazard
    behind it: stopping a surface unmounts its tree, which produces one last
    transaction of mutations, and destroying the window before those arrive
    leaves them naming views that are gone.
    """
    app = bundle_app(bundle.parent, "windows")

    def run(taps: str, run_ms: int) -> tuple[str, str]:
        with tempfile.TemporaryDirectory() as directory:
            dump = Path(directory) / "tree.txt"
            env = dict(os.environ)
            env["BASALT_DUMP_TREE"] = str(dump)
            env["BASALT_QUIT_AFTER_MS"] = str(run_ms)
            env["BASALT_TEST_TAP"] = taps
            for name in ("BASALT_TEST_TYPE", "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS",
                         "BASALT_TEST_SCROLL", "BASALT_TEST_MENU"):
                env.pop(name, None)
            result = run_host_process(
                [str(HOST), str(app), "BasaltWindows"],
                cwd=REPO, env=env, capture_output=True, text=True,
                timeout=run_ms / 1000 + 90,
            )
            _remember_output(result.stderr)
            check_output(result.stderr, result.returncode)
            tree = dump.read_text() if dump.exists() else ""
            return tree, result.stdout + result.stderr

    def run_closing(taps: str, surfaceId: int, run_ms: int) -> tuple[str, str]:
        with tempfile.TemporaryDirectory() as directory:
            dump = Path(directory) / "tree.txt"
            env = dict(os.environ)
            env["BASALT_DUMP_TREE"] = str(dump)
            env["BASALT_QUIT_AFTER_MS"] = str(run_ms)
            env["BASALT_TEST_TAP"] = taps
            # Closes the window the way its own close button does, rather than
            # the way the app does. See docs/TESTING.md.
            env["BASALT_TEST_CLOSE_WINDOW"] = str(surfaceId)
            for name in ("BASALT_TEST_TYPE", "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS",
                         "BASALT_TEST_SCROLL", "BASALT_TEST_MENU"):
                env.pop(name, None)
            result = run_host_process(
                [str(HOST), str(app), "BasaltWindows"],
                cwd=REPO, env=env, capture_output=True, text=True,
                timeout=run_ms / 1000 + 90,
            )
            _remember_output(result.stderr)
            check_output(result.stderr, result.returncode)
            tree = dump.read_text() if dump.exists() else ""
            return tree, result.stdout + result.stderr

    if "windows supported: true" not in run("", 5000)[1]:
        raise Skipped("this host cannot open a second window")

    # The button that opens one is at (134, 110) in the app's own layout: 24 of
    # padding, a 22-tall label, a 40-tall count, then 48-tall buttons.
    #
    # The second tap is in the *second* window, at the same place in its own
    # layout -- which is only the same number by coincidence, and is why the
    # window has to be named.
    tree, logged = run("134,110;134,110@3", 11000)

    if "--- window 3 ---" not in tree:
        raise Failure(
            "the second window opened and rendered nothing.\n"
            f"{tail_text(logged)}\n{tree}"
        )
    if "counted up from the second window" not in logged:
        raise Failure(
            "a tap in the second window did not reach it. Each window has its "
            "own touch dispatcher; this is what says the right one was used.\n"
            f"{tail_text(logged)}"
        )

    # Both trees show the same number, from the one piece of state, which lives
    # in the first window's tree and was changed from the second's.
    counts = [line for line in tree.splitlines() if 'text="count ' in line]
    if len(counts) != 2:
        raise Failure(f"expected a count in each window, found {len(counts)}:\n{tree}")
    if 'text="count 1"' not in counts[0] or 'text="count 1"' not in counts[1]:
        raise Failure(
            "the two windows disagree about the one piece of state they share.\n"
            + "\n".join(counts)
        )

    # And closing takes it away again.
    tree, logged = run("134,110;134,110", 11000)
    if "--- window 3 ---" in tree:
        raise Failure(f"the second window was still open after being closed:\n{tree}")

    # Closed by the person rather than by the app, which is a different path
    # through the host and the one that can go wrong quietly: the window is
    # destroyed either way, and only this one can leave the host holding a
    # record whose window is gone and the app believing it is still open.
    tree, logged = run_closing("134,110", 3, 11000)
    if "the second window closed itself" not in logged:
        raise Failure(
            "a window the person closed did not tell the app. Its `open` flag "
            "stays true, the next render tries to close a window that has "
            f"already closed, and it can never be reopened.\n{tail_text(logged)}"
        )
    if "--- window 3 ---" in tree:
        raise Failure(f"a window the person closed is still in the tree:\n{tree}")


def test_drop_target(bundle: Path) -> None:
    """A file dropped on the app reaches the view under the pointer.

    React Native has no API for this, so what is being checked is this
    platform's own: a view marks itself with a `nativeID`, the host hit-tests
    the drop point, walks up to the nearest marked ancestor, and tells that
    one. Two targets, one inside the other, because the rule that can be
    wrong is *which* view is told.

    The drag is entered below the toolkit, the way taps are. A real drag
    needs a source outside the process and there is no way to conjure one
    from a test -- so what this exercises is the hit test, the ancestor walk,
    the accept check, the event and React's half, and not GtkDropTarget or
    NSDraggingDestination themselves. Said plainly here because a scenario
    that looks like it drives a real drag and does not is worse than one that
    admits it.
    """
    app = bundle_app(bundle.parent, "drop")

    def drop(spec: str) -> tuple:
        with tempfile.TemporaryDirectory() as directory:
            dump = Path(directory) / "tree.txt"
            env = dict(os.environ)
            env["BASALT_DUMP_TREE"] = str(dump)
            env["BASALT_QUIT_AFTER_MS"] = "6000"
            env["BASALT_TEST_DROP"] = spec
            for name in ("BASALT_TEST_TAP", "BASALT_TEST_TYPE", "BASALT_TEST_HOVER",
                         "BASALT_TEST_FOCUS", "BASALT_TEST_QUIT"):
                env.pop(name, None)

            result = run_host_process(
                [str(HOST), str(app), "BasaltDrop"],
                cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
            )
            _remember_output(result.stderr)
            check_output(result.stderr, result.returncode)
            return (dump.read_text() if dump.exists() else ""), result.stderr

    # Inside the inner target, which is inside the outer one. The innermost
    # accepting view wins, so the outer must not be the one told.
    tree, logged = drop("200,200:/tmp/hello.txt")
    if "inner drop /tmp/hello.txt" not in logged:
        raise Failure(f"the inner target was not told:\n{logged[-1500:]}")
    if "outer drop" in logged:
        raise Failure("both targets were told; the walk did not stop at the innermost")
    if 'text="inner /tmp/hello.txt"' not in tree:
        raise Failure(f"the drop did not reach React:\n{tree[-1200:]}")

    # Inside the outer target and below the inner one. The same walk, one
    # level up -- and the case that fails if the hit test answers with
    # whatever is nearest rather than what is under the point.
    tree, logged = drop("200,280:/tmp/outer.txt")
    if "outer drop /tmp/outer.txt" not in logged:
        raise Failure(f"the outer target was not told:\n{logged[-1500:]}")
    if "inner drop" in logged:
        raise Failure("the inner target was told about a drop outside it")


def test_view_key_events(bundle: Path) -> None:
    """A view's declared keyboard shortcuts, pressed.

    `onKeyPress` existed on a `<TextInput>` and a `<View>` had nothing, so an app
    could not bind a shortcut to anything it draws -- which on a desktop is most
    of the interface. This is the scenario that says the whole path works, because
    thirty-six unit tests say the three key tables, the registry and the matching
    are right and none of them presses anything.

    Four assertions, because four things fail independently:

    - a declared combination fires
    - an undeclared one does not, which is what keeps the menu and a focused text
      field working
    - a modifier is part of the match, so Cmd+Z is not Z
    - a list that *changes* takes effect

    The last is the reason this scenario exists more than the others. A
    `<KeyHandler>` registers through a module rather than by `nativeID`, and the
    failure that invites -- silently going quiet after a re-render -- is the one
    `<DropTarget>` warns about in so many words. `e2e/keys.js` declares `m` and
    adds `j` only once `m` has arrived, so `j` firing is proof the re-registration
    happened. If the list were captured once, the log would stop after one line
    and everything else here would still pass.
    """
    app = bundle_app(bundle.parent, "keys")

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "9000"
    # m is declared. j becomes declared once m has been handled. q is never
    # declared. z+meta is declared with its modifier.
    env["BASALT_TEST_KEY"] = "m;j;q;z+meta"
    for name in ("BASALT_TEST_TAP", "BASALT_TEST_TYPE", "BASALT_TEST_HOVER",
                 "BASALT_TEST_FOCUS", "BASALT_TEST_QUIT"):
        env.pop(name, None)

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env["BASALT_DUMP_TREE"] = str(dump)
        result = run_host_process(
            [str(HOST), str(app), "BasaltKeys"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=150,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        logged = result.stdout + result.stderr
        tree = dump.read_text() if dump.exists() else ""

    # The instrument ran at all. Without this a scenario that pressed nothing
    # would pass every assertion below by pressing nothing.
    if "BASALT_TEST_KEY" not in logged:
        raise Failure(
            "the host never pressed a key; BASALT_TEST_KEY did not reach it.\n"
            f"{tail_text(logged)}")

    pressed = [
        line.split("[js] key ", 1)[1].strip()
        for line in logged.splitlines()
        if "[js] key " in line
    ]

    if "m" not in pressed:
        raise Failure(
            "a declared combination did not reach JavaScript, so no shortcut "
            f"works at all.\ngot: {pressed}\n{tail_text(logged)}")

    if "j" not in pressed:
        raise Failure(
            "the second combination never fired, so a changed list is not "
            "honoured -- `j` is declared only after `m` arrives. This is the "
            "failure registering by tag rather than by nativeID invites: a "
            "handler that silently goes quiet after a re-render.\n"
            f"got: {pressed}")

    if "z+meta" not in pressed:
        raise Failure(
            "a combination with a modifier did not fire, so Cmd+Z and Z are not "
            f"being told apart.\ngot: {pressed}")

    if any(entry == "q" for entry in pressed):
        raise Failure(
            "an undeclared key was delivered. A view that takes keys it never "
            "declared swallows them from the application menu, from a focused "
            f"<TextInput> and from a scrolling ancestor.\ngot: {pressed}")

    # From the tree as well as the log, which says React re-rendered rather than
    # only that a handler ran.
    if 'text="pressed 3: m j z+meta"' not in tree:
        raise Failure(
            "the presses did not reach React's own state, so a handler ran and "
            f"the app did not see it.\n{tree[-1200:]}")


def test_turbomodule_proxy(bundle: Path) -> None:
    """That `globalThis.__turboModuleProxy` exists and answers for both sides.

    A bridgeless runtime does not get one: `TurboModuleBinding::install` gives
    `__turboModuleProxy` to a non-bridgeless runtime and `nativeModuleProxy` to
    this one, so every lookup arrives by `TurboModuleRegistry`'s second question.
    That is fine on the React Native this is built against, where the fallback is
    unconditional, and fatal on 0.82 and earlier, where it is gated behind three
    flags that are all false here -- every `getEnforcing` fails, starting with
    `PlatformConstants`, and an app cannot start without prepending a flag to its
    own bundle. See core/TurboModuleProxy.h.

    **The assertion that matters is `PlatformConstants`, not `BasaltWindows`.**
    The proxy is an alias for `nativeModuleProxy[name]` rather than a provider of
    this platform's own making, and the difference only shows on a name React
    Native provides: a provider of our own would answer null for that, and on the
    versions this exists for a null from the proxy does not fall through. So it
    would have failed exactly where it was needed while passing any test that only
    asked about our modules.

    And an unknown name must come back empty rather than throw, because
    `TurboModuleRegistry.get` is allowed to return null and callers rely on it --
    every optional module is found that way.
    """
    app = bundle_app(bundle.parent, "turbomodules")

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "4000"
    for name in ("BASALT_TEST_TAP", "BASALT_TEST_TYPE", "BASALT_TEST_HOVER",
                 "BASALT_TEST_FOCUS", "BASALT_TEST_QUIT"):
        env.pop(name, None)

    result = run_host_process(
        [str(HOST), str(app), "BasaltTurboModules"],
        cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
    )
    _remember_output(result.stderr)
    check_output(result.stderr, result.returncode)
    logged = result.stdout + result.stderr

    for needle, why in (
        ("turbo proxy: function",
         "__turboModuleProxy was not installed, so TurboModuleRegistry's first "
         "question is unanswerable and every lookup depends on the fallback"),
        ("turbo proxy BasaltWindows: found",
         "the proxy did not answer for a module this platform provides"),
        ("turbo proxy PlatformConstants: found",
         "the proxy did not answer for a module React Native provides, which is "
         "what an alias for nativeModuleProxy is for -- a provider of our own "
         "would fail here, and this is the half that matters on 0.82 and earlier"),
        ("turbo proxy unknown name: null",
         "an unknown name did not come back empty; TurboModuleRegistry.get is "
         "allowed to return null and every optional module is found that way"),
    ):
        if needle not in logged:
            raise Failure(f"{why}.\nexpected {needle!r} in:\n{tail_text(logged)}")


def test_crash_handler(bundle: Path) -> None:
    """What the host says when it dies.

    It used to say nothing. A reload teardown took SIGSEGV on CI's Mac on
    2026-10-05, twice, and three attempts produced no address, no stack and no
    thread: the harness noticed the process was gone and reported "host exited
    -11", which is the least informative failure there is. The bug was found by
    reading `ReactHost` instead.

    So this is the handler being exercised deliberately, because a handler is
    otherwise only ever run on the day it is needed and a handler that has never
    run is a guess. BASALT_TEST_CRASH raises the signal on purpose; see
    core/CrashHandler.h for why the signal set excludes SIGTERM, which the
    harness uses to end a host that is working.

    Three things are asserted, and the third is the one that would break a
    scenario rather than merely disappoint somebody reading a log:

      the marker    that the handler ran at all
      a frame       that it produced a stack rather than one line
      the status    that it still exits with the signal, so every other scenario
                    that reads a return code is unaffected
    """
    env = dict(os.environ)
    env["BASALT_TEST_CRASH"] = "1"
    # Nothing else: this never gets as far as a window.
    for name in ("BASALT_TEST_TAP", "BASALT_TEST_SECONDARY_TAP", "BASALT_TEST_TYPE",
                 "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL",
                 "BASALT_TEST_MENU", "BASALT_QUIT_AFTER_MS"):
        env.pop(name, None)

    result = run_host_process(
        [str(HOST), str(bundle), MODULE],
        cwd=REPO, env=env, capture_output=True, text=True, timeout=60,
    )
    logged = result.stdout + result.stderr
    _remember_output(result.stderr)

    marker = "*** basalt: " + ("exception" if PLATFORM == "windows" else "SIGSEGV")
    if marker not in logged:
        raise Failure(
            f"the crash handler printed nothing. Expected {marker!r}, which is "
            "the whole point of it: without this line a crash on a platform that "
            f"only runs in CI is four words and no address.\n{tail_text(logged)}"
        )

    # A stack, not just a header. One frame is not a stack; the handler asks for
    # up to 64.
    #
    # Counted by position rather than matched by shape, because the shape is
    # different everywhere and two attempts at a pattern were wrong:
    #
    #   macOS      0   basalt_appkit   0x0000000104e8c040 _ZN6basalt... + 156
    #   glibc      build/basalt_gtk(+0x875a5c)[0x55cef4472a5c]
    #   Windows     0  00007FF621EE0094
    #
    # A leading index on macOS and Windows and none on glibc; `0x` lower case on
    # two and absent on the third. The first pattern wanted a lower-case `0x` and
    # failed twelve good Windows frames; the second wanted a leading index and
    # failed nine good glibc ones.
    #
    # Worse, neither could be caught here: `--platform linux` on a Mac is the GTK
    # host against *macOS's* libc, so it prints macOS's format. The only place the
    # glibc shape exists is CI.
    #
    # So: the handler writes the marker, then the frames, then a blank line. The
    # frames are what is between, whatever they look like.
    lines = logged.splitlines()
    start = next((i for i, line in enumerate(lines) if marker in line), None)
    frames = []
    for line in lines[start + 1:] if start is not None else []:
        if line.strip() == "":
            break
        frames.append(line)
    # And they are frames rather than prose: every format carries an address.
    frames = [line for line in frames if re.search(r"[0-9a-fA-F]{6,}", line)]
    if len(frames) < 3:
        raise Failure(
            "the crash handler printed a header and no usable stack "
            f"({len(frames)} line(s) with an address).\n{tail_text(logged)}"
        )

    # And the status is still the signal. A handler that swallowed it would make
    # every scenario reading a return code quietly wrong, which is worse than the
    # silence this replaced.
    if PLATFORM == "windows":
        if result.returncode == 0:
            raise Failure(
                "the host exited 0 after an access violation, so the handler "
                f"swallowed it.\n{tail_text(logged)}"
            )
    elif result.returncode != -signal.SIGSEGV:
        raise Failure(
            f"the host exited {result.returncode}, not -{int(signal.SIGSEGV)}. The "
            "handler is meant to re-raise, so that an exit status still says what "
            f"killed it.\n{tail_text(logged)}"
        )


def test_clipboard(bundle: Path) -> str:
    """`Clipboard`, which ships on all three hosts and had never been tested.

    It became load-bearing before it was covered. A menu role's behaviour is
    asserted by seeding the clipboard and pasting, and while writing that it turned
    out the clipboard does not reliably round-trip under the display CI runs on.
    Nothing in this suite exercised the module, so nobody knew.

    Seven checks, each small enough that a failure names one thing: a round trip,
    the second write winning, an empty string clearing rather than being ignored,
    text outside ASCII, 64KB to reach X11's chunked path, whitespace kept as it
    was, and `getString` answering a string rather than null.

    All of them write before they read, deliberately. On Linux `clipboardText`
    sees only this application's own clipboard, `GdkClipboard` reading
    asynchronously and the seam being synchronous, so reading what another
    application put there is a limit rather than a bug and is already recorded in
    backlog/desktop-capabilities.md. What is asserted here is the half that is
    meant to work on all three.

    **And the exit is an assertion too.** A host was once seen to log
    `BASALT_QUIT_AFTER_MS elapsed; quitting` after a run that read the clipboard
    and then not exit, which is the serious half of that backlog entry: an app
    that reads the clipboard and quits is an ordinary app. Every run of this app
    reads it and then quits, so this is the reproduction attempt. The timeout is
    short on purpose, so a hang costs the suite seconds rather than two minutes.

    The exit time comes back as a note rather than only being checked, because a
    threshold guessed above a 7s timer is as likely to invent a failure as to
    catch one. Measured since: 7.2s on CI's Linux, against 7.3s here, so the host
    costs nothing over its timer and the 17.7s the whole scenario took on that
    runner was bundling. The limit below stays well clear of both rather than
    being tightened onto one reading.
    """
    app = bundle_app(bundle.parent, "clipboard")

    env = dict(os.environ)
    env["BASALT_QUIT_AFTER_MS"] = "7000"
    for name in ("BASALT_TEST_TAP", "BASALT_TEST_SECONDARY_TAP", "BASALT_TEST_TYPE",
                 "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL",
                 "BASALT_TEST_MENU"):
        env.pop(name, None)

    started = time.monotonic()
    try:
        result = run_host_process(
            [str(HOST), str(app), "BasaltClipboard"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=45,
        )
    except subprocess.TimeoutExpired as expired:
        raise Failure(
            "the host did not exit after reading the clipboard, against a quit "
            "timer of 7s and a limit of 45. This is the hang recorded in "
            "docs/backlog/testing.md, and reaching it here would put the "
            "clipboard back under suspicion, which one run has already argued "
            "against.\n"
            + "\n".join(f"        {line}"
                         for line in hang_text(expired.stderr).splitlines())
        )
    elapsed = time.monotonic() - started
    logged = result.stdout + result.stderr
    _remember_output(result.stderr)
    check_output(result.stderr, result.returncode)

    if "clipboard checks: 7/7" not in logged:
        failures = [line for line in logged.splitlines() if "FAIL: " in line]
        raise Failure(
            "the clipboard did not round-trip.\n"
            + ("\n".join(f"        {line.strip()}" for line in failures) if failures
               else tail_text(logged))
        )

    # Exiting at all is the assertion; the timeout above is what makes it one.
    # This is the softer half: a host that exits but takes its time is the same
    # suspect as one that never does, so the number is reported every run and
    # fails only where no slow runner could plausibly land.
    if elapsed > 30.0:
        raise Failure(
            f"the host took {elapsed:.1f}s to exit against a 7s quit timer. It did "
            "exit, so this is not quite the hang, but a shutdown that slow after a "
            f"clipboard read is the same suspect.\n{tail_text(logged)}"
        )

    return (f"the host read the clipboard and exited in {elapsed:.1f}s, against a "
            "7s quit timer")


def test_inline_views(bundle: Path) -> str:
    """An inline `<View>` inside a `<Text>`, which every text engine used to drop.

    React Native gives a text engine one fragment holding U+FFFC and the size it
    measured for the view, and expects back a box reserved in the line and the
    frame that box ended up in. No host did either: the character reserved
    whatever width the font gives a missing glyph, and every attachment was
    reported at the origin with no size, so a view inside a sentence rendered as
    a dot in the corner.

    Pango reserves it with a shape attribute, Core Text with a run delegate and
    DirectWrite with an `IDWriteInlineObject`. What is asserted here is the half
    that is the same on all three: the size comes from React Native, so it must
    match exactly, and the position comes from the font, so only its
    relationships can be.

    Three cases, each on a line of its own in e2e/inline.tsx so a failure names
    one of them rather than "inline views are wrong".
    """
    app = bundle_app(bundle.parent, "inline")

    env = dict(os.environ)
    env["BASALT_QUIT_WHEN_SETTLED"] = SETTLE_MS
    for name in ("BASALT_TEST_TAP", "BASALT_TEST_SECONDARY_TAP", "BASALT_TEST_TYPE",
                 "BASALT_TEST_HOVER", "BASALT_TEST_FOCUS", "BASALT_TEST_MENU"):
        env.pop(name, None)

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env["BASALT_DUMP_TREE"] = str(dump)
        result = run_host_process(
            [str(HOST), str(app), "BasaltInline"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        if not dump.exists():
            raise Failure("the host wrote no widget tree")
        tree = dump.read_text()

    # Found by background colour, which is what the app gives each one for the
    # purpose: a tag would change the moment the app grows another view.
    wanted = {
        "2f6fed": ("the view between two runs of text", 48.0, 24.0),
        "e0484d": ("the view taller than its line", 24.0, 64.0),
        "1f9d55": ("the view before any text", 32.0, 16.0),
    }
    found = {}
    for body, (x, y), (width, height) in _views(tree):
        for colour in wanted:
            if colour in body:
                found[colour] = (x, y, width, height)

    missing = [wanted[c][0] for c in wanted if c not in found]
    if missing:
        raise Failure(
            "not every inline view reached the tree, so this is about mounting "
            f"rather than about text: {missing}\n{tree[-1200:]}"
        )

    # The size is React Native's own measurement of the view, rounded up to the
    # pixel grid by ParagraphShadowNode, so it is exact rather than approximate
    # and the same on both hosts. A size that came from the font instead would be
    # a glyph's width, which is nothing like 48 points.
    for colour, (what, width, height) in wanted.items():
        x, y, measuredWidth, measuredHeight = found[colour]
        if not (width <= measuredWidth <= width + 1 and height <= measuredHeight <= height + 1):
            raise Failure(
                f"{what} measured {measuredWidth}x{measuredHeight}, and it asked "
                f"for {width}x{height}. A box reserved from the font rather than "
                f"from the view looks exactly like this.\n{tree[-1200:]}"
            )

    # Position, in the only terms that survive two different shapers over two
    # different system fonts.
    #
    # Against the paragraphs' own left edge, not against zero: `_views` reports
    # absolute coordinates, so every one of these carries the page's padding, and
    # asserting x == 0 for the view that starts its line fails for a reason that
    # has nothing to do with text. The three <Text> rows are siblings of the same
    # width, so they share one left edge and it is the thing to measure from.
    edges = {round(x) for body, (x, _y), _size in _views(tree) if "role=text" in body}
    if len(edges) != 1:
        raise Failure(
            "the three paragraphs do not share a left edge, so there is nothing "
            f"to measure an inline view against: {sorted(edges)}\n{tree[-1200:]}"
        )
    left = edges.pop()

    midX = found["2f6fed"][0]
    tallX = found["e0484d"][0]
    leadingX = found["1f9d55"][0]

    if midX <= left or tallX <= left:
        raise Failure(
            "an inline view with text before it sits at its paragraph's left "
            f"edge ({left}), so the frame did not come from the layout: mid at "
            f"x={midX}, tall at x={tallX}.\n{tree[-1200:]}"
        )
    if round(leadingX) != left:
        raise Failure(
            "the inline view that starts its line is not at its paragraph's left "
            f"edge: x={leadingX} against {left}. Nothing precedes it, so nothing "
            f"should offset it.\n{tree[-1200:]}"
        )

    return (f"inline views {midX - left:.0f} and {tallX - left:.0f} points into "
            "their lines, and the third at the start of its own")


def test_displays(bundle: Path) -> None:
    """What screens the desktop has.

    React Native's `Dimensions` reports the window, which is a different
    question -- an app placing a window is asking about the screen it is on
    and the ones beside it. The lookups already existed on every host, for
    `center()` and full screen, and were simply not passed on.

    What can be asserted on a machine nobody has described: that there is at
    least one display, that exactly one of them is primary, that the scale
    factor is a ratio rather than a DPI, and that the work area is *inside*
    the bounds. The last is the one that catches a coordinate mistake: on
    macOS the bounds are flipped from AppKit's upward-growing y and the work
    area has to be flipped the same way, and a work area that escaped its own
    display would be the symptom.
    """
    app = bundle_app(bundle.parent, "displays")

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env = dict(os.environ)
        env["BASALT_DUMP_TREE"] = str(dump)
        env["BASALT_QUIT_AFTER_MS"] = "4000"
        for name in ("BASALT_TEST_TAP", "BASALT_TEST_TYPE", "BASALT_TEST_HOVER",
                     "BASALT_TEST_FOCUS", "BASALT_TEST_QUIT"):
            env.pop(name, None)

        result = run_host_process(
            [str(HOST), str(app), "BasaltDisplays"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=120,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        tree = dump.read_text() if dump.exists() else ""
        logged = result.stderr

    # The count reaches React, which is what says the list crossed the bridge
    # rather than merely existing in C++.
    count = re.search(r'text="displays (\d+)"', tree)
    if count is None:
        raise Failure(f"the app never reported a display count:\n{tree[-1200:]}")
    if int(count.group(1)) < 1:
        raise Failure("the desktop reported no displays at all; the cache was "
                      "read before the host primed it")

    # From the tree rather than the log, which has one line per display and
    # not one per render. The log repeats: the app reports the list whenever
    # it changes, and it changes once between the first render and the effect
    # that re-reads it -- so counting log lines counted that twice and said
    # two displays claimed to be primary when one did, twice.
    found = re.findall(
        r'text="(\d+(?:\.\d+)?)x(\d+(?:\.\d+)?) scale (\d+(?:\.\d+)?)( primary)?"',
        tree,
    )
    if not found:
        raise Failure(f"no displays in the tree:\n{tree[-1200:]}\n{logged[-600:]}")
    if len(found) != int(count.group(1)):
        raise Failure(
            f"the app says {count.group(1)} displays and rendered {len(found)}"
        )

    primaries = 0
    for width, height, scale, primary in found:
        if float(width) <= 0 or float(height) <= 0:
            raise Failure(f"a display measured {width}x{height}")
        # A ratio, not a DPI: 96 here would mean the Windows host forgot to
        # divide, which is the mistake that reads as plausible.
        if not 0.5 <= float(scale) <= 8.0:
            raise Failure(f"a scale factor of {scale} is a DPI rather than a ratio")
        if primary:
            primaries += 1

    # Exactly one, on every desktop -- including the two whose toolkits have
    # no notion of a primary display and report the first.
    if primaries != 1:
        raise Failure(f"{primaries} displays claim to be primary, out of {len(found)}")


def test_quit_request(bundle: Path) -> None:
    """Being asked before the *application* quits, and refusing.

    `useCloseRequest` guards a window and is not enough: macOS routes Cmd-Q
    through `applicationShouldTerminate:` and asks no window whether it
    minds, and a session ending on Linux or Windows does the same. An app
    with unsaved work that guarded only its windows would lose it.

    Two runs, because a refusal alone proves half of it and the wrong half:

      refused   the app says no and the process is still here afterwards,
                which the host's own timer then ends. If interception did
                nothing, this run would end early and the tree would be
                missing.

      agreed    the app refuses once, is asked again, and lets it through.
                The process must end *before* its timer -- an app that could
                refuse and never agree would be a process nobody can quit,
                which is a worse bug than the one this feature prevents.
    """
    app = bundle_app(bundle.parent, "quit")

    def run(asks: int, run_ms: int) -> tuple:
        with tempfile.TemporaryDirectory() as directory:
            dump = Path(directory) / "tree.txt"
            env = dict(os.environ)
            env["BASALT_DUMP_TREE"] = str(dump)
            env["BASALT_QUIT_AFTER_MS"] = str(run_ms)
            # Asks the application to quit the way a person would: Cmd-Q on
            # macOS, the session ending on Linux. Deliberately not the same
            # thing as BASALT_QUIT_AFTER_MS, which is the harness ending the
            # process and is not refusable -- without that exemption this
            # demo would refuse the harness too and every run would hang.
            env["BASALT_TEST_QUIT"] = str(asks)
            for name in ("BASALT_TEST_TAP", "BASALT_TEST_TYPE", "BASALT_TEST_HOVER",
                         "BASALT_TEST_FOCUS", "BASALT_TEST_CLOSE_WINDOW"):
                env.pop(name, None)

            started = time.monotonic()
            result = run_host_process(
                [str(HOST), str(app), "BasaltQuit"],
                cwd=REPO, env=env, capture_output=True, text=True,
                timeout=run_ms / 1000 + 60,
            )
            elapsed = time.monotonic() - started
            _remember_output(result.stderr)
            check_output(result.stderr, result.returncode)
            tree = dump.read_text() if dump.exists() else ""
            return tree, result.stderr, elapsed

    # Whether the refusal held is measured rather than read out of a log
    # line. The first version of this grepped for "BASALT_QUIT_AFTER_MS
    # elapsed", which only two of the three hosts print -- so on Windows it
    # failed whatever the host did, and said "the process ended early" about
    # something it had not timed. How long the process lived is the property
    # actually under test, and every host has a clock.
    refused_ms = 6000
    tree, logged, elapsed = run(asks=1, run_ms=refused_ms)
    if "quit refused 1" not in logged:
        raise Failure(f"the app was never asked to quit:\n{logged[-1200:]}")
    if "quit allowed" in logged:
        raise Failure("the app agreed to a quit it was supposed to refuse")
    if 'text="refused 1"' not in tree:
        raise Failure(f"the refusal did not reach React:\n{tree[-1200:]}")
    # It had to live until its own timer. The ask lands about a second and a
    # half in, so a quit that went through would end the process well short
    # of this.
    if elapsed < refused_ms / 1000 - 2:
        raise Failure(
            f"the process ended after {elapsed:.0f}s of a {refused_ms // 1000}s run, "
            "so the quit was not refused"
        )

    # Agreed. The timer is deliberately far away, so that ending before it is
    # evidence rather than a coincidence.
    _, logged, elapsed = run(asks=2, run_ms=30000)
    if "quit refused 2" not in logged:
        raise Failure(f"the app was asked once rather than twice:\n{logged[-1200:]}")
    if "quit allowed" not in logged:
        raise Failure(f"the app never agreed to quit:\n{logged[-1200:]}")
    if elapsed > 20:
        raise Failure(
            f"the process took {elapsed:.0f}s of a 30s run to quit after agreeing, "
            "so its own timer is what ended it"
        )


def test_window_close_request(bundle: Path) -> None:
    """Being asked before a window closes, and refusing.

    The other half of `onClose`, and the earlier one. `onClose` says a window
    *has* closed; this says somebody is trying to, and it has not -- which is
    the only place an app can put "are you sure", because by the time the window
    has gone there is nothing left to ask about.

    It cannot work the way Electron's `preventDefault` does. The handler is
    JavaScript on another thread and the window manager wants a synchronous yes
    or no, so the decision has to exist before the attempt: registering a
    handler is what makes it exist, and the host then refuses every close and
    reports it. See native/core/WindowHost.h.

    Three runs, because there are three answers and a screen with more than one
    of them is a screen whose answer depends on when you look:

      refused       a second window with a handler does not close, and the app
                    re-renders knowing it was asked.

      agreed        the same interception taken all the way round -- refused,
                    reported, and closed by the app with the `close` it was
                    handed. Without this half, intercepting would be a way to
                    make a window nobody can shut.

      the app's own the case that matters most and breaks worst. An app with
                    unsaved work wants to refuse *its own* window, and a host
                    that cannot then be shut down at all is the failure. This
                    run refuses and asserts the host still exited on its own
                    timer.
    """
    app = bundle_app(bundle.parent, "windows")

    def run(component: str, closing: int, run_ms: int) -> tuple[str, str]:
        with tempfile.TemporaryDirectory() as directory:
            dump = Path(directory) / "tree.txt"
            env = dict(os.environ)
            env["BASALT_DUMP_TREE"] = str(dump)
            env["BASALT_QUIT_AFTER_MS"] = str(run_ms)
            # Closes the window the way its own close button does, which is the
            # only thing an interception ever refuses: an app closing its own
            # window is not asking anybody. See docs/TESTING.md.
            env["BASALT_TEST_CLOSE_WINDOW"] = str(closing)
            for name in ("BASALT_TEST_TAP", "BASALT_TEST_TYPE", "BASALT_TEST_HOVER",
                         "BASALT_TEST_FOCUS", "BASALT_TEST_SCROLL", "BASALT_TEST_MENU"):
                env.pop(name, None)
            result = run_host_process(
                [str(HOST), str(app), component],
                cwd=REPO, env=env, capture_output=True, text=True,
                timeout=run_ms / 1000 + 90,
            )
            _remember_output(result.stderr)
            check_output(result.stderr, result.returncode)
            tree = dump.read_text() if dump.exists() else ""
            return tree, result.stdout + result.stderr

    tree, logged = run("BasaltWindowsGuarded", 3, 11000)
    if "the second window was asked to close, and said no" not in logged:
        raise Failure(
            "closing a guarded window told the app nothing. Refusing without "
            f"reporting is a window that cannot be closed and never says why.\n"
            f"{tail_text(logged)}"
        )
    if "--- window 3 ---" not in tree:
        raise Failure(f"a window that refused to close closed anyway:\n{tree}")
    if 'text="Really close?"' not in tree:
        raise Failure(
            "the window stayed open but the app did not re-render knowing it "
            f"had been asked, which is the whole point of being told.\n{tree}"
        )

    tree, logged = run("BasaltWindowsConfirming", 3, 11000)
    if "the second window was asked to close, and agreed" not in logged:
        raise Failure(f"the app was never asked:\n{tail_text(logged)}")
    if "--- window 3 ---" in tree:
        raise Failure(
            "the app agreed to close the window and it is still open. An "
            f"interception that cannot be lifted is a window nobody can shut.\n{tree}"
        )

    # The app's own window. Nothing here closes it in the end -- the run stops
    # on its own timer, which is the assertion: a host whose main window refuses
    # to close still shuts down when the harness says so, and a host where that
    # is not true hangs rather than fails.
    tree, logged = run("BasaltWindowsGuarded", 1, 8000)
    if "the main window was asked to close, and said no" not in logged:
        raise Failure(
            "closing the app's own window told it nothing. This is the one an "
            f"app with unsaved work most wants to refuse.\n{tail_text(logged)}"
        )
    if "--- window 1 ---" not in tree and "view tag=1" not in tree:
        raise Failure(f"the app's own window closed after refusing to:\n{tree}")


def test_screen_stack(bundle: Path) -> None:
    """A stack shows its top screen and hides what is under it.

    react-navigation's native stack renders every screen it has pushed and
    leaves it to the platform to show one. On iOS that is
    UINavigationController. There is no equivalent here, so
    RNSScreenStackShadowNode does it: every screen below the topmost opaque one
    is given `DisplayType::None` after layout, which all three mounting
    managers already turn into a hidden widget.

    `activityState` is deliberately 2 on all three, because that is what a real
    native stack sends. It never drops to 0 there, react-native-screens throws
    `activityState cannot be decreased in NativeStack` if it would, and a
    version of this that keyed on the prop passed while doing nothing.

    e2e/screens.js puts a transparent modal on top, so one run checks both
    halves: a see-through screen covers nothing and the opaque one under it
    stays visible, while everything under *that* is hidden.
    """
    app = bundle_app(bundle.parent, "screens")

    with tempfile.TemporaryDirectory() as directory:
        dump = Path(directory) / "tree.txt"
        env = dict(os.environ)
        env["BASALT_DUMP_TREE"] = str(dump)
        env["BASALT_QUIT_AFTER_MS"] = "4000"
        result = run_host_process(
            [str(HOST), str(app), "BasaltScreens"],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=64,
        )
        _remember_output(result.stderr)
        check_output(result.stderr, result.returncode)
        tree = dump.read_text() if dump.exists() else ""

    def line_for_text(text: str) -> str:
        for line in tree.split("\n"):
            if f'text="{text}"' in line:
                return line
        raise AssertionError(f"no {text!r} in the tree:\n{tree}")

    def line_for(label: str) -> str:
        for line in tree.split("\n"):
            if f'text="{label} screen"' in line:
                return line
        raise AssertionError(f"no {label} screen in the tree:\n{tree}")

    # The text node is inside the screen, so the screen is the line above its
    # own text. Asserting on the text line itself would pass whatever the
    # screen did, since a hidden parent does not mark its children.
    lines = tree.split("\n")
    def screen_above(label: str) -> str:
        index = lines.index(line_for(label))
        assert index > 0, f"{label} screen has no parent line"
        return lines[index - 1]

    assert "hidden" in screen_above("bottom"), (
        "the bottom screen is covered by an opaque screen and should be "
        f"hidden:\n{tree}"
    )
    assert "hidden" not in screen_above("middle"), (
        f"the middle screen is the topmost opaque one and should show:\n{tree}"
    )
    assert "hidden" not in screen_above("top"), (
        f"the top screen should always show:\n{tree}"
    )

    # The header, which the middle screen carries and the bottom one carries
    # empty. Heights are asserted as "a bar rather than a line of text" rather
    # than as a number: the bar is its content plus a margin, and the three
    # platforms do not agree on how tall a line of text is.
    def origin_y(line: str) -> float:
        found = re.search(r"frame=\((?:[\d.]+),([\d.]+) ", line)
        assert found is not None, f"no frame in {line!r}"
        return float(found.group(1))

    def height(line: str) -> float:
        found = re.search(r"frame=\([^)]*x([\d.]+)\)", line)
        assert found is not None, f"no frame in {line!r}"
        return float(found.group(1))

    # The text sits inside a subview, and a frame is relative to its parent, so
    # the padding shows on the subview and the bar is one line above that.
    title = line_for_text("middle title")
    subview = lines[lines.index(title) - 1]
    header = lines[lines.index(title) - 2]
    assert height(header) > 40, (
        f"a header with a title in it should be a bar, not a line of text:\n{tree}"
    )
    assert origin_y(subview) > 0, (
        f"the title should be padded down from the top of the bar:\n{tree}"
    )

    # And the content below it, rather than behind it. react-native-screens
    # gives the content wrapper absoluteFill, so without the screen moving it
    # the first 50 points of every screen are under the bar.
    body = line_for("middle")
    assert origin_y(body) == height(header), (
        f"the body should start where the header ends:\n{tree}"
    )

    # An empty header takes itself out rather than leaving a blank strip, which
    # is what a plain string title produces: react-navigation renders that
    # through a prop, for a toolbar this platform does not have.
    empty = lines[lines.index(line_for("bottom")) + 1]
    assert "hidden" in empty, (
        f"a header with nothing in it should not reserve a bar:\n{tree}"
    )


SCENARIOS = [
    ("initial render", test_initial_render),
    ("scrollToEnd, and a tap that bubbles from a label", test_scroll_to_end),
    ("scroll away and back", test_scroll_round_trip),
    ("focus a TextInput, type, and see it round-trip through React", test_text_input),
    ("click a TextInput with a real mouse and see it focus", test_click_focuses_a_field),
    ("hover across nested views and see enter, leave, over and out", test_hover),
    ("pointerEvents decides what four taps land on", test_pointer_events),
    ("Tab reaches a Pressable, and Enter presses it", test_keyboard_focus),
    ("a console error opens LogBox's inspector", test_logbox),
    ("Linking.getInitialURL answers with the URL the app was opened with",
     test_initial_url),
    ("Alert.alert shows a dialog and says which button was pressed", test_alert),
    ("Share.share reaches the platform and settles both ways", test_share),
    ("expo-notifications imports and answers on every desktop", test_notifications),
    ("react-native-macos's keyboard props work on a plain View", test_macos_key_props),
    ("a command runs, and its output and exit reach JavaScript", test_subprocess),
    ("an Expo app's fetch works rather than naming a module it has not got", test_expo_fetch),
    ("ActivityIndicator, Switch, Modal and RefreshControl mount and answer",
     test_controls),
    ("a screen stack shows its top screen and hides what is under it",
     test_screen_stack),
    ("the native file dialogs answer with a path, or with a cancel",
     test_file_dialogs),
    ("scrollTo({animated: true}) moves rather than jumps", test_animated_scroll),
    ("showsVerticalScrollIndicator={false} takes the bar and not the scrolling",
     test_scrollbar_can_be_turned_off),
    ("contentInset changes the range, and scrollIndicatorInsets only the bar",
     test_content_inset),
    ("locationX and locationY are relative to the view that was pressed",
     test_press_location),
    ("the layout style props arrive, and Yoga lays them out", test_layout_styles),
    ("hitSlop grows what a press can land on", test_hit_slop),
    ("Image.getSize answers, and a missing file rejects", test_image_get_size),
    ("an animated GIF is animated", test_animated_image),
    ("tintColor and blurRadius reach the view", test_image_tint_and_blur),
    ("borderStyle reaches the view, dashed and dotted", test_border_style),
    ("the cursor style property reaches the view", test_cursor_style),
    ("boxShadow reaches the view, inset and all", test_box_shadow),
    ("a linear-gradient backgroundImage reaches the view", test_linear_gradient),
    ("a filter list reaches the view, composed", test_filter),
    ("the outline family reaches the view", test_outline),
    ("a drop-shadow filter reaches the view", test_drop_shadow_filter),
    ("onLayout fires with the view's size", test_on_layout),
    ("a view with nothing to draw is flattened away", test_view_flattening),
    ("backgroundSize, Position and Repeat reach the view", test_background_size_position_repeat),
    ("a radial gradient reaches the view, resolved", test_radial_gradient),
    ("the legacy iOS shadow props reach the view", test_legacy_shadow),
    ("mixBlendMode reaches the view", test_mix_blend_mode),
    ("spellCheck and autoCorrect reach the field", test_text_checking),
    ("a font loaded at runtime is the font the paragraph uses", test_runtime_font),
    ("writingDirection reaches the paragraph", test_writing_direction),
    ("a text shadow reaches the paragraph", test_text_shadow),
    ("a desktop text scale, and the props that refuse it", test_font_scaling),
    ("textTransform changes what the engine lays out", test_text_transform),
    ("accessibilityLabelledBy resolves a nativeID", test_accessibility_labelled_by),
    ("a testID reaches the view and the platform", test_test_id),
    ("accessibilityViewIsModal reaches the view", test_modal_view),
    ("experimental_accessibilityOrder resolves to views", test_accessibility_order),
    ("accessibilityLiveRegion announces a change", test_accessibility_live_region),
    ("a window reports its own size, and the state changes that are not resizes",
     test_window),
    ("a window says how big it may be, and what this desktop can do about it",
     test_window_limits),
    ("the application menu is installed, roles and all", test_application_menu),
    ("a context menu opens where you press, and says what was chosen",
     test_context_menu),
    ("a role in a context menu performs it, not only reports it",
     test_context_menu_role),
    ("a second window is a second React tree, and the two stay in step",
     test_windows),
    ("a window can refuse to close, and say so", test_window_close_request),
    ("a dropped file reaches the view under it", test_drop_target),
    ("a view's declared keyboard shortcuts fire, and only those",
     test_view_key_events),
    ("__turboModuleProxy answers for this platform and for React Native",
     test_turbomodule_proxy),
    ("an inline view inside a Text is given a box and told where it is",
     test_inline_views),
    ("the clipboard round-trips, and the host still exits afterwards",
     test_clipboard),
    ("the desktop says what displays it has", test_displays),
    ("a crash says where it died, and still exits with the signal",
     test_crash_handler),
    ("an application can refuse to quit, and then agree", test_quit_request),
    ("DevTools' overlay draws a highlight, and a trace update takes itself down",
     test_debugging_overlay),
    ("the developer menu reloads, and shows the element inspector", test_dev_menu),
    ("edit the demo and watch Fast Refresh apply it", test_fast_refresh),
]


def parse_shard(text: str) -> tuple:
    """`"2/4"` as `(2, 4)`, or a ValueError naming what was wrong.

    One argument rather than two, because the two are only ever meaningful
    together and a CI matrix writes them as one string anyway.
    """
    parts = text.split("/")
    if len(parts) != 2:
        raise ValueError(f"--shard wants I/N, not {text!r}")
    try:
        index, count = int(parts[0]), int(parts[1])
    except ValueError:
        raise ValueError(f"--shard wants numbers, not {text!r}") from None
    if count < 1:
        raise ValueError(f"--shard needs at least one shard, not {count}")
    if not 1 <= index <= count:
        raise ValueError(f"--shard {index} is outside 1..{count}")
    return (index, count)


def shard_of(scenarios: list, index: int, count: int) -> list:
    """The Ith of N shards, striding rather than slicing.

    Striding, because the scenarios are registered in the order they were
    written and neighbours tend to cost about the same -- the four scroll
    scenarios sit together, and so do the two that wait twelve seconds for a
    window. A contiguous slice would hand one shard all of them and leave
    another with the cheap ones, and a shard set is only as fast as its
    slowest member.

    Every scenario lands in exactly one shard, which
    `scripts/test_shards.py` checks: a sharding bug that drops one would make
    CI greener while testing less, which is the worst direction for a bug in
    a test harness to point.
    """
    return scenarios[index - 1 :: count]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--bundle", default=None)
    parser.add_argument("--input", choices=["auto", "real", "injected"], default="auto")
    parser.add_argument(
        "--platform",
        choices=["auto", *HOSTS],
        default="auto",
        help="which host to run; auto picks the one that is built",
    )
    # Running one scenario is the common case while working on it, and running
    # all of them takes a quarter of an hour. Substring, case-insensitive,
    # repeatable -- `-k window -k menu` runs both.
    parser.add_argument(
        "-k",
        "--scenario",
        action="append",
        default=[],
        metavar="TEXT",
        help="only scenarios whose name contains TEXT; repeatable",
    )
    parser.add_argument(
        "--shard",
        metavar="I/N",
        help="run only the Ith of N shards, 1-based; see shard_of",
    )
    parser.add_argument(
        "--list",
        action="store_true",
        help="print the scenario names and exit",
    )
    arguments = parser.parse_args()

    if arguments.list:
        # Through the shard too, so that `--shard 2/4 --list` answers what
        # that shard would run -- which is the question somebody balancing a
        # matrix is actually asking.
        listed = SCENARIOS
        if arguments.shard:
            try:
                listed = shard_of(listed, *parse_shard(arguments.shard))
            except ValueError as error:
                print(f"error: {error}", file=sys.stderr)
                return 1
        for name, _ in listed:
            print(name)
        return 0

    global INPUT_MODE, HOST, PLATFORM
    build = REPO / arguments.build_dir

    if arguments.platform == "auto":
        # Whichever is built. No machine has more than one -- a host needs its
        # toolkit -- so there is nothing to disambiguate in practice, and
        # --platform is there for the case where there somehow is.
        built = [name for name, binary in HOSTS.items() if (build / binary).exists()]
        if not built:
            print(
                f"error: no host built in {build}\n"
                f"       looked for {', '.join(HOSTS.values())}",
                file=sys.stderr,
            )
            return 1
        PLATFORM = built[0]
    else:
        PLATFORM = arguments.platform

    HOST = build / HOSTS[PLATFORM]
    if arguments.bundle is None:
        arguments.bundle = f"{arguments.build_dir}/main.jsbundle.js"
    if arguments.input == "auto":
        INPUT_MODE = "real" if real_input_available() else "injected"
    else:
        INPUT_MODE = arguments.input
    if INPUT_MODE == "real" and not real_input_available():
        print("error: --input real needs DISPLAY set and xdotool installed", file=sys.stderr)
        return 1

    bundle = (REPO / arguments.bundle).resolve()
    if not HOST.exists():
        print(f"error: {HOST} not built", file=sys.stderr)
        return 1
    if not bundle.exists():
        print(
            f"error: no bundle at {bundle}\n"
            "       run scripts/bundle.sh ../react-native --prod first",
            file=sys.stderr,
        )
        return 1

    wanted = SCENARIOS
    if arguments.scenario:
        needles = [text.lower() for text in arguments.scenario]
        wanted = [
            entry for entry in SCENARIOS
            if any(needle in entry[0].lower() for needle in needles)
        ]
        if not wanted:
            print(
                f"error: no scenario matches {arguments.scenario}\n"
                "       run with --list to see the names",
                file=sys.stderr,
            )
            return 1

    shard = None
    if arguments.shard:
        try:
            shard = parse_shard(arguments.shard)
        except ValueError as error:
            print(f"error: {error}", file=sys.stderr)
            return 1
        wanted = shard_of(wanted, *shard)
        if not wanted:
            # An empty shard is legitimate -- more shards than scenarios -- but
            # it must not read as a run that passed everything.
            print(f"shard {shard[0]} of {shard[1]} has no scenarios in it")
            return 0

    note = (
        "real pointer events through the X server"
        if INPUT_MODE == "real"
        else "taps injected at the dispatcher, skipping the window system"
    )
    of_total = "" if shard is None else f" (shard {shard[0]} of {shard[1]})"
    print(f"running {len(wanted)} scenarios against {bundle.name} on {PLATFORM}{of_total}")
    print(f"input: {INPUT_MODE} -- {note}")
    failed = 0
    skipped = 0
    for name, scenario in wanted:
        # Set here rather than in each scenario: the runner is the one place that
        # knows which scenario is about to run, so opting in costs a line in one
        # set instead of an edit in twenty function bodies.
        # Never in `real` input mode. A settle accounts for what the *host*
        # scheduled, and real input is driven from outside the process: the
        # harness waits four seconds for a window and then moves the pointer
        # with xdotool, having set none of the BASALT_TEST_* variables. So
        # nothing is requested, the host settles at the first mount, and it has
        # quit before the click arrives.
        #
        # Linux CI is the only place this runs -- it is chosen when DISPLAY is
        # set and xdotool is installed -- so every local run on a Mac passed and
        # all three Linux shards failed. The rule underneath is the same one the
        # exclusion list is about, one step further out: a settle cannot stand in
        # for a duration it cannot see, and it cannot see another process.
        #
        # BASALT_NO_SETTLE turns the whole thing off, which is how the saving was
        # measured and how to tell "this scenario is broken" from "this scenario
        # needed longer than it was given".
        if (scenario.__name__ in SETTLES_EARLY
                and INPUT_MODE != "real"
                and not os.environ.get("BASALT_NO_SETTLE")):
            os.environ["BASALT_QUIT_WHEN_SETTLED"] = SETTLE_MS
        else:
            os.environ.pop("BASALT_QUIT_WHEN_SETTLED", None)
        started = time.time()
        try:
            # A scenario may return a note: something it could not check on this
            # machine, where the rest of it still ran. Silence would be the
            # alternative, and a scenario that quietly checks less than its name
            # says is worse than one that skips outright.
            note = scenario(bundle)
            # The duration, because this suite's cost is the thing most often
            # being worked on and it was previously only visible as a total.
            print(f"  ok    {name}  [{time.time() - started:.1f}s]")
            if note:
                print(f"        {note}")
        except Skipped as reason:
            skipped += 1
            print(f"  skip  {name}")
            print(f"        {reason}")
        except Failure as failure:
            failed += 1
            print(f"  FAIL  {name}\n        {failure}")
        except subprocess.TimeoutExpired as expired:
            failed += 1
            print(f"  FAIL  {name}\n        the host did not exit")
            # What it had said before it stopped saying anything. Without this a
            # hang is the least informative failure there is -- `capture_output`
            # swallows the pipes, so the report was four words and nothing else,
            # which on a platform that only runs in CI is nothing to work from.
            #
            # `run_host_process` asks a stuck host for a backtrace before killing
            # it, so on POSIX this is usually the frames rather than the tail.
            for stream, label in ((expired.stdout, "stdout"), (expired.stderr, "stderr")):
                if not stream:
                    continue
                shown = hang_text(stream)
                what = ("where the host was"
                        if CRASH_MARKER in shown else f"last of the host's {label}")
                print(f"        --- {what} ---")
                for line in shown.splitlines():
                    print(f"        {line}")

    total = len(wanted) - skipped
    tally = f"\n{total - failed}/{total} passed"
    if skipped:
        tally += f", {skipped} skipped"
    print(tally)
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
