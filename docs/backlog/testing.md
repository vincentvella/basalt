# Testing

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (10):**

1. No rendering assertions on GTK
2. Nothing exercises the JS thread and the main thread concurrently
3. The Fast Refresh scenario is skipped in CI
4. The GTK `<TextInput>` focus scenario flaked on a Mac, and nothing explains it
5. Nothing tests tap-to-focus
6. A `<TextInput>`'s wrapper is still an element of its own on Windows
7. The hover scenario cannot assert its order on GTK-over-quartz
8. One flaky end-to-end scenario, and a reload teardown that crashes on CI's Mac
9. An app build compiles this repository's test suites
10. A cancelled job reads as a job that ran

- **The `image` comparison flaked once and nobody can say why.** It differed on
  one CI run, passed on a rerun of the same commit, and passes locally: run
  with the right module, which is a trap of its own: `compare_hosts.sh image`
  defaults to `BasaltViews`, mounts nothing on either host, and compares two
  empty trees. The diff CI produced was discarded by `compare_all.sh`, which
  said "rerun that one through compare_hosts.sh"; advice that cannot work for
  something that does not reproduce. It prints the diff now, so the next
  occurrence is diagnosable; until then there is nothing to fix and guessing
  would be inventing a cause.

- **No rendering assertions on GTK.** The widget tree says a view has a colour
  and a frame, not that the right pixels reached the screen. This is not
  theoretical, GTK's cairo renderer mangled every transform in the demo and no
  test noticed. See `docs/TESTING.md`.

  Windows has them as of phase 39, because Direct2D renders offscreen with no
  window and no display; `tests/test_win32_paint.cpp` is fourteen of them.

  **macOS has them too, by a cheaper route than this entry predicted.** It
  guessed at an offscreen `NSWindow` and a display cycle. What the three files
  doing it actually use is a `CGBitmapContext` and the view's own `drawRect:`:
  no window, no display, no permission: `test_appkit_image.mm` asserts where the
  ink of each resize mode lands, `test_appkit_text.mm` that a paragraph draws
  where its alignment says, and `test_appkit_scrollbar.mm` that the overlay
  thumb is at the trailing edge.

  What is left is GTK, which needs a display server and a `GdkTexture`
  read-back, and is the one host where nothing checks the pixels.
- Nothing exercises the JS thread and the main thread concurrently.
- ~~The end-to-end scenarios hard-code tap coordinates from the demo's
  layout.~~ They find the button by its label now, in a tree measured from one
  extra run of the host per bundle. The three demo scenarios that tapped
  survive a restyle: reversing the button row leaves them passing, where the
  old constants sent the second tap of "scroll away and back" into *focus the
  field* and left the list at 1623.

  It also measures per host, which the constants could not: the three shapers
  disagree about how wide "scroll to end" is, so the centre of that label is a
  few points apart on each desktop. What is still hard-coded is the other
  apps' coordinates: `e2e/hover.js`'s boxes, the devtools taps, the menu
  taps, which are boxes rather than labels and have no text to find.
- ~~CI builds and tests Linux and Windows on every push. macOS is built only
  by `release.yml`, which has not run yet, and otherwise by whoever is
  developing on a Mac.~~ macOS is a job in `ci.yml` now, running on every
  push: build, unit tests, CLI tests, the end-to-end suite, and
  `compare_all.sh`, which no other runner can do because no other has both
  hosts.

  It was in `release.yml` because macOS minutes bill at ten times Linux's,
  which was worth avoiding on a private repository. Standard runners are free
  on a public one, so the reason expired the day the repository went public
  and the job moved. `release.yml` no longer defines its own: the cold build
  calls `ci.yml`, so a release gets the same job rather than a second copy of
  it.

  What the gap cost, measured rather than guessed: `release.yml` ran for the
  first time that same day and found a scenario that had never passed on
  macOS: LogBox's toast sits at a different height there, and the tap that
  hit it on Linux missed by 22 points. AppKit had 257 unit tests and no
  per-push check, and the only thing standing between a macOS regression and
  a release was whoever happened to run the suite on a Mac.
- **The Fast Refresh *edit* is skipped in CI.** Not the scenario: it runs on
  all three platforms and guards the host half of development mode: dev mode,
  the dev server helper, the websocket, `DevSettings`, and a bundle Metro is on
  record as having served this process. What is unguarded is narrower than this
  entry used to claim, and one thing it used to claim was never true: the check
  that the app fetched from Metro counted `BUNDLE` lines from the start of
  Metro's log, which `prewarm` had already written two of, so it matched before
  the host started and could not fail. Fixed, and guarded by
  `scripts/test_harness.py`. Metro on a GitHub runner never notices an edit:
  the file changes on disk with a fresh mtime, a newly requested bundle still
  carries the old text, and Metro logs nothing. Ruled out already: `fs.watch`
  sees the same edit on the same runner; the inotify limits are 655360 watches
  and 1280 instances; both sides run Node 24.20.0; and CI's layout, with React
  Native inside the checkout, reproduces green in a local VM. The scenario
  passes on macOS and on Linux in a VM, so this is about the runner rather than
  the code.

  **Watchman was the standing theory, and it is now ruled out.** It was tried
  on the `ci` branch and did not fix the scenario. What the run established,
  so that none of it needs doing again:

  - The upstream build was used, not Ubuntu's 4.9.0 from 2017, so a failure
    here says something about the theory rather than about an ancient client.
    It has to come from a GitHub release: watchman is not in apt, and
    facebook/watchman stopped attaching release assets after v2026.07.27.00,
    which is the last version that ships a Linux binary at all.
  - It ran. `watchman --version` answered `20260727.012849.0`, and
    `watchman watch-project js` reported `"watcher": "inotify"` over exactly
    the root Metro was given.
  - A `.watchmanconfig` was added at the project root: React Native ships one
    and this repo never had it -- so watch-project resolved the root Metro
    asked about rather than some parent. That file is still in the repo, since
    it is correct regardless of CI.
  - The scenario failed the same way, and `serves_edit` answered **no**: a
    freshly requested bundle still carried the old text, so Metro had not seen
    the change at all.

  What that leaves is the one thing the run did not establish: whether
  *metro-file-map* used watchman, or found it and fell back anyway. The
  `watch-project` call above was made by the workflow, not by Metro, so a watch
  existing proves nothing about which watcher Metro chose.

  Next thing to try, in order of effort:

  1. **`DEBUG=metro:*` on the CI Metro**, to see what the watcher thinks it is
     doing rather than inferring it from what the bundle contains -- starting
     with whether it picked watchman at all, which is the question the run
     above left open.
  2. Shrink `watchFolders`. The React Native checkout is the bulk of the eight
     thousand directories; if the watcher is falling over on volume, a narrower
     watch would show it.
  3. Have the scenario poll Metro for the edit rather than waiting on the host,
     which would at least separate "Metro never saw it" from "Metro saw it and
     the client missed it" without another CI round trip per hypothesis.
- **The GTK `<TextInput>` focus scenario flaked on a Mac, and nothing explains
  it.** `focus a TextInput, type, and see it round-trip through React` failed
  three runs in a row on 2026-09-13 with *"the focus command did not move focus
  to the field"*, then passed six in a row, on the same build and the same
  machine. No change was made between the two states that touches focus.

  It was running the **GTK host on macOS**, over the quartz backend, which
  `docs/ARCHITECTURE.md` lists as a risk rather than the target: keyboard focus is
  the window server's to give, and the scenario needs the window to have it
  before `TextInput.focus()` can mean anything. CI, on real Linux under Xvfb,
  was green across the whole period and has never reproduced it.

  What was ruled out: it is not a code change. The one edit in flight touched
  `main_gtk.cpp` and was reverted, rebuilt and re-run, and the failure
  survived that, so it was already failing before anything that day touched
  the host. The suspicion is that a pile of stray `basalt_gtk` and
  `basalt_appkit` processes from earlier runs were holding or stealing focus,
  since killing them is the only thing that happened between the last failure
  and the first pass. That is a correlation and nothing more; it was not
  tested by reproducing it.

  Worth doing before trusting it: reproduce deliberately, leave a host
  running and start another, and if that is it, have the scenario fail with
  "another host is already running" rather than with a focus error, which is
  the misleading half. If it cannot be reproduced that way, the next suspect
  is the quartz backend itself, and the answer is that this scenario should
  not be believed off a real Linux session at all.

- ~~CI has no rendering assertions, so it cannot catch what the cairo renderer
  did.~~ It has them, on Windows: the job runs `basalt_win32_tests.exe`, and
  `test_win32_paint.cpp`'s fourteen are in it. What CI still cannot catch is
  what the *cairo* renderer does, because the GTK job has none, which is the
  entry above, and a narrower claim than this one was making.


- **Nothing tests tap-to-focus.** Clicking a `<TextInput>` focuses it on both
  hosts: verified with a real `CGEvent` mouse click, after which a keystroke
  round-trips, but no automated test can check that, and two obvious ways of
  trying give a false negative.

  `BASALT_TEST_TAP` enters at the touch dispatcher, below the window system, so
  it moves React Native's responder but never reaches the peer widget that
  actually takes focus. That is the same deliberate limitation `BASALT_TEST_TYPE`
  has with the key controller, and it looks exactly like a broken feature: the
  `<Pressable>` beside the field responds to an injected tap and the field does
  not. System Events' `click at` is no better: it performs an accessibility
  press, which is why it answers with the name of the element it found, and a
  text field does nothing with one.

  So a tap-to-focus regression would be invisible: `integration_test.py`'s focus
  scenario taps a *button* that calls `focus()`, and every `<TextInput>` feature
  added in phases 51 and 52 was probed by focusing programmatically. Testing it
  needs a real click, which on macOS means `CGEventPost` and the accessibility
  permission that goes with it, and on Linux means xdotool, which CI already
  has, and which is where this is worth adding.

- **A `<TextInput>`'s wrapper is still an element of its own on Windows.**
  Fixed on the other two in phase 53: the accessible name lands on the peer,
  which is what a screen reader reaches, and the wrapper leaves the tree:
  `accessibilityElement = NO` on AppKit, and `GTK_ACCESSIBLE_ROLE_PRESENTATION`
  chosen at construction on GTK, which is the only moment a GtkAccessible role
  can be chosen at all.

  Windows has not been looked at. UI Automation is the one that works
  differently: a provider answers questions rather than a view carrying
  properties, so the question there is whether the wrapper's provider should
  refuse to be a control, and whether the `EDIT` peer is exposed as its own
  element at all. `RnWin32Accessible.cpp` is where it would go.

  Whatever the answer, it must not print in `describeTree`: the other two say
  nothing about this view and a third vocabulary would put the cross-host diff
  back where phase 53 found it.

- ~~**No unit test can observe an event.**~~ Both suites can, through
  `native/tests/EventRecorder.h`. Five tests use it so far: a change reaching
  React on each host, blur before endEditing on AppKit, a prop that must not
  report itself as typing on GTK, and on both a field that has only been
  mounted staying silent, which is there to catch a recorder wired up wrong,
  since every other assertion rests on it hearing what it should.

  **The stub emitter this entry ruled out is still ruled out, and the way
  round it was to stop stubbing.** `EventDispatcher` takes a listener (  `std::function<bool(const RawEvent &)>`) and consults it at the top of
  `dispatchEvent`, before the logger and before the queue; returning true says
  the event was handled and stops the default dispatch, so nothing downstream
  runs and no beat has to flush. A real emitter over a real dispatcher, and no
  production code carrying a test hook, which is the trade this entry was
  weighing.

  The one thing standing in the way was that `EventQueue`'s constructor calls
  `setBeatCallback` immediately, so the beat cannot be null, and `EventBeat`
  holds a `RuntimeScheduler &`. That reads like the whole JavaScript
  machinery and is not: `RuntimeScheduler` takes a `RuntimeExecutor`, which is
  a `std::function`, and a runtime appears only when something invokes it.
  Nothing does.

  **What still cannot be asserted is payload values.** Every `TextInput` event
  builds a `jsi::Object` through a `ValueFactory`, and reading one back needs
  a `jsi::Runtime`. So what a value ends up as stays the end-to-end suite's
  to check; which event fired, and in what order, is now this one's.

  **`onKeyPress` before `onChange` is the one case this entry named that is
  still the end-to-end suite's**, and the window it needed turned out to be
  half enough. AppKit tests can make a real window, focus the field and post a
  key through `NSApp`, which is what invokes a local event monitor, and so
  what makes the monitor the thing under test rather than a direct call to
  `handleKeyDown`. Three tests came out of that: focus reported, a key press
  reported, and nothing reported for a key that types nothing.

  What cannot follow is the edit. `NSApp` routes a key to the *key* window,
  and a window belonging to an inactive process cannot become one (  `makeKeyWindow` leaves `isKeyWindow` false) so the field editor never sees
  the keystroke and no `onChange` follows it. Sending the event a second time
  straight to the window does perform the edit, and then the ordering is the
  test's arrangement rather than AppKit's, which is not worth calling a test.
  GTK is unexamined; its focus controller needs a realised window, which that
  suite has never needed.

- **The hover scenario cannot assert its order on GTK-over-quartz**, and is
  skipped there with a note rather than loosened. A real cursor sitting over the
  window when it maps has already entered the card before the first scripted
  move lands, so the `enter card` the sequence expects in the middle arrives at
  the start, and again at the end. Deterministic rather than flaky: three runs
  byte-identical. CI runs this host under Xvfb, which has no pointer, and
  asserts the full order, so nothing was weakened where it counts.

- **One flaky end-to-end scenario.** "focus a TextInput, type, and see it
  round-trip through React" failed once in five consecutive runs of
  `scripts/integration_test.py` with "the focus command did not move focus to
  the field", and passed the other four. The scenario schedules taps at fixed
  delays and assumes the host has caught up, which is a timing assumption rather
  than a synchronisation. Fixing it means waiting on something observable (  the tree, or a log line) instead of on a clock.

  **A second one, and this one crashes.** "the developer menu reloads, and shows
  the element inspector" failed twice on CI's macOS runner at 6e58901 and then
  passed on a third attempt with no change, so it is intermittent rather than
  broken. The two failures were not the same failure, which is the interesting
  part:

  | Attempt | Symptom |
  | --- | --- |
  | 1 | `the host did not exit`: TimeoutExpired after 110s |
  | 2 | `host exited -11`, after `Scheduler::~Scheduler()` and `Shutting down PlatformTimerRegistryImpl...` |
  | 3 | passed |

  A hang and a SIGSEGV from the same code is a race in the reload teardown, and
  the second one means a pointer is being used after something it belongs to has
  gone. That is a real bug in the host, not a test that needs a longer sleep:
  the scenario asks for a reload and then a quit, which an app's developer does
  by hand every day.

  It does not reproduce on a developer Mac. Four runs at the same commit passed:
  three of the scenario alone and one of the whole shard, in CI's order
  (`--platform macos --shard 1/3`, 11/11). So whatever orders those destructors
  differently is something about that runner, and the next step is a stack from
  it rather than another local run. The host writes nothing on a signal today,
  which is its own entry waiting to be written: a crash handler that logs where
  it died would have answered this in one attempt.

- **An app build compiles this repository's test suites.** `native/` is packed
  whole, tests included, and nothing gates them, so `react-native run-macos:
  build` in someone's app builds `basalt_appkit_tests`,
  `mount_harness_appkit` and `basalt_core_probe` before it builds their app.
  Minutes of a first build that is already the slow one, for binaries the app
  will never run.

  It is not only waste. Twice while verifying `add-init-command` a test-only
  file broke a user's app build: `tests/test_controls.cpp` and a new
  `transformOrigin` fixture both copied props, which React Native 0.86 forbids
  and 0.87 allows. Both were fixed, and neither should have been able to stop
  an app compiling.

  The shape is a `BASALT_BUILD_TESTS` option defaulting off, with the
  repository's root CMakeLists turning it on, which matches the split that
  already exists between the two entry points: the root is for a checkout, a
  host package's CMakeLists is what an app configures. Left undone deliberately
  rather than folded into a change about the init command.

- ~~**A cancelled job reads as a job that ran.**~~ Both halves are fixed.

  `cancel-in-progress` is now `${{ github.ref != 'refs/heads/main' }}`:
  cancelling is right for a branch, where nobody needs the result for a commit
  that has been replaced, and wrong for main, where it loses the only record
  that a commit was tested. The cost is that quick pushes to main queue instead
  of cancelling, which on a public repository is patience rather than money.

  And `scripts/ci_status.py` reads the last run that reached a *verdict* per
  job, rather than the last run, with the count of newer runs that did not,
  which is the size of the blind spot, and on the day this entry describes would
  have read 5. Shards fold together, and a job with one cancelled shard is
  undecided rather than green, because three green shards and one cancelled is
  not a tested commit. `scripts/test_ci_status.py` checks that logic against the
  exact history above, because a bug in this tool would point the same
  comfortable way the original problem did.
