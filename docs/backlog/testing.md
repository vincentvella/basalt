# Testing

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (9):**

1. ~~No rendering assertions on GTK~~
2. Nothing exercises the JS thread and the main thread concurrently
3. The Fast Refresh scenario is skipped in CI
4. The GTK `<TextInput>` focus scenario flaked on a Mac, and nothing explains it
5. Nothing tests tap-to-focus
6. A `<TextInput>`'s wrapper is still an element of its own on Windows
7. ~~The hover scenario cannot assert its order on GTK-over-quartz~~
8. One flaky end-to-end scenario, and a reload teardown that crashes on CI's Mac
9. An app build compiles this repository's test suites
10. A cancelled job reads as a job that ran
11. GTK's clipboard paste does not complete under CI's display

- **GTK's clipboard paste does not complete under the display CI runs on, and
  the host hung instead. The hangs are explained and fixed; the paste is not.**
  Found 2026-10-07 while asserting that a text menu
  role performs. The same scenario pastes on the GTK host on a developer's Mac,
  which is not an X server at all but the quartz backend, as recorded below.

  The first diagnosis was wrong and is worth recording as wrong. One run showed
  three failures at once, and the conclusion drawn was that the clipboard did not
  round-trip, that `autoFocus` did not take, and that the host hung. The next run
  showed the first two working and only the paste missing, so two of those three
  were timing rather than capability: the readback is itself asynchronous and had
  not arrived before it was looked for.

  What is left, and is reproducible: `gtk_widget_activate_action(focus,
  "clipboard.paste")` does not deliver under that display. The preconditions pass,
  the menu answers, and the text never arrives. Not established: why. A read that
  never completes fits, there being no clipboard manager and the owner being the
  same process, but nothing was instrumented.

  **The host also stops exiting, and that is two bugs, both on the main thread
  and both named by backtrace on 2026-10-07.** Neither is the clipboard module,
  which appears in neither stack.

  **One: focusing a field claims an X11 selection and blocks.**

      main -> g_application_run -> g_main_context_iteration
        -> applyPendingMount -> GtkMountingManager::applyTransaction
          -> GtkTextInputManager::flushAutoFocus
            -> gdk_clipboard_set_content -> gdk_x11_get_server_time
              -> XIfEvent -> pthread_cond_wait, for good

  GtkText treats a programmatic focus as keyboard focus and selects all of its
  text; selected text on X11 is a PRIMARY selection; claiming one needs a server
  timestamp, and `gdk_x11_get_server_time` does an `XChangeProperty` and then
  blocks in `XIfEvent` with no timeout. Under CI's display that round trip does
  not come back. Introduced by 3e75c42 the same day, which is why the hang
  appeared 45 minutes after it.

  **Fixed** by e13a649: single-line fields focus through
  `gtk_text_grab_focus_without_selecting`, so nothing is selected, no PRIMARY
  claim is made and no server timestamp is needed. Measured rather than
  asserted: four hunts in a row stalled on the first attempt on every shard, and
  the hunt after the fix ran eight attempts on each of three runners with no
  stall and no frames. Multiline keeps the ordinary call, GtkTextView not
  selecting on focus. It is also the better behaviour, autoFocus being meant to
  leave a caret rather than to select what is already there.

  **Two: tearing the host down waits for a JavaScript thread that never quits.**

      main -> g_application_run -> g_signal_emit (shutdown)
        -> onShutdown -> ~ReactHost -> destroyReactInstance
          -> MessageQueueThreadImpl::quitSynchronous
            -> TaskDispatchThread::quit -> std::__basic_future<void>::wait()

  This is the original observation, the one that logged `BASALT_QUIT_AFTER_MS
  elapsed; quitting` and then hung. Open: why the JS thread does not finish. A
  task of its own waiting on main-thread work that will never run, the loop
  having left, is the shape to look for, and proving it wants the *other*
  thread's stack, which the handler does not dump but gdb now does.

  **Not reproduced since, in about 555 teardowns.** Two hunts after the focus
  hang was fixed: 45 attempts of the context-menu scenarios, which reach
  `destroyReactInstance` once per host at quit, then 75 of the reload scenario,
  which reaches it twice, at the reload and at the quit. No stall of any kind.

  Worth noticing and not worth believing yet: **every sighting of this one
  predates the focus fix.** Both were before e13a649, and a main thread stuck in
  a blocking X round trip inside a mount is exactly the shape that leaves a
  JavaScript thread waiting on UI work that never completes. But it was seen
  twice in a small sample and has been absent from a large one, and absence of a
  rare thing is weak, so this is recorded as a correlation rather than a cause.

  Not hunted further. The instrument is on main now, gdb included, so the next
  occurrence on any run dumps every thread by itself, which is the one piece of
  evidence that has always been missing. Grinding runner time at something seen
  twice has worse odds than waiting for it.

  Together they account for the evidence that was previously read off absences.
  The first blocks the loop inside a mount, before the 1500ms tap timer and the
  8s quit timer, which is why neither fired and why there is no quitting line.
  The second happens after the quit line, which is why the first sighting had
  one. **What this file used to say, that the main loop "stopped dispatching
  about a second and a half in", was wrong**: it was dispatching, inside a mount,
  and blocked there.

  It is intermittent but not rare, and the first estimate here was twice too
  kind. Five Linux runs that reached the end-to-end suite on 2026-10-07: hung at
  f46f2b4, clean on a rerun of that commit, hung at a5b6dea, hung at 5ef9faf,
  clean at bb47bd6. Three in five. Counting runs is the wrong instinct anyway,
  since a run starts the host a dozen times over and only one of them stalls.

  **And one of those two hangs reported success.** At a5b6dea it landed in `a role
  in a context menu performs it`, which skipped rather than failed when the host
  did not exit, so the job was green with the bug in it. The clipboard scenario
  passed in that same job, its host exiting in 7.2s, which is the clipboard ruled
  out as the cause on one runner in one run.

  That skip is now a failure, as of 2026-10-07. A skip is right for a clipboard
  this environment cannot round-trip and wrong for a host that hangs, which is
  the same shape as "a cancelled job reads as a job that ran" further up this
  file. It was expected to be red on the runs where it stalled, which is what a
  failure is for. With the focus hang fixed, what is left to redden it is the
  teardown hang, which is rarer: it was seen once, and did not reappear in the
  24 attempts that cleared the other, so that is not evidence it has gone.

  The 84.6s the passing rerun took is nine hosts against an 8s quit timer, one per
  index the scenario checks, not a slow runner.

  **Three wrong answers, recorded so they are not tried again.** The clipboard
  module was blamed first; it is in neither stack, and the scenario below writes
  and reads seven times and exits on the same runner in the same run. Then Xvfb
  was blamed, and `-noreset` was added as the fix: with the flag confirmed in
  effect all three shards still stalled on the first attempt, so it is not the
  environment, though the flag was kept on its own merits. Then the widget was
  thought to be unmapped when focused; the next frames read `flushAutoFocus`
  directly rather than the `map` handler, so it was mapped and `grab_focus`
  blocked anyway.

  **A hang now has an instrument**, added 2026-10-07. Every scenario that
  launches a host goes through `run_host_process`, which on a timeout sends
  `SIGABRT` before killing: that runs core/CrashHandler.cpp, which writes a
  marker and up to 64 frames to stderr and re-raises, so the frames arrive on the
  pipe the harness is already reading. A hang now reports the way a crash does.
  It has now read both hangs above, so it is proven rather than plausible, and
  three things it gained in the reading are worth keeping: 64 frames rather than
  24, because the first stack was cut off just above `g_closure_invoke` and so
  did not say whether it reached `main`; the handler stating outright whether the
  signalled thread is the main thread, `SIGABRT` going to whichever thread has it
  unblocked; and `addr2line` with an `nm` fallback over our own frames, since
  `backtrace_symbols_fd` names every library frame and leaves ours as bare
  offsets, which is backwards for reading a hang.

  **What it still cannot do is dump a thread other than the one that stalled**,
  which is exactly what the second hang needs. `gdb -p` or `gcore` on the runner
  is the way in. **That was added**, and it is what found the answer below.

  **Three: the clipboard write itself, from the JavaScript thread. Found
  2026-10-08 and fixed.** The first occurrence after the gdb dump landed printed
  every thread, and the two that matter were:

      Thread 3 "MessageQueue" (the JavaScript thread)
        DesktopClipboardModule::setString -> basalt::setClipboardText
          -> gdk_clipboard_set -> gdk_clipboard_set_content
            -> gdk_x11_get_server_time -> XIfEvent -> xcb_wait_for_event

      the main thread
        g_application_run -> g_main_context_iteration -> GTK's frame clock
          -> XSyncSetCounter -> pthread_mutex_lock, for good

  Claiming a selection needs a server timestamp, which is a blocking round trip;
  making it from a second thread is two threads on one `Display`, and the main
  thread stops on the lock the first one holds. It is the same blocking call as
  hang one, from a different thread, and `core/PlatformServices.h` said "called
  on the main thread" while `DesktopClipboardModule` called it from the
  JavaScript thread. That comment is now the truth instead.

  **The fix** is the pattern the rest of PlatformServicesGtk.cpp already used for
  `showAlert`: the string is copied and `g_idle_add_full` hands the write to the
  main thread, unless the caller already is the main thread, which the share
  picker is. A write waiting for the main thread is what `clipboardText()`
  answers with, so a `getString()` straight after a `setString()` still sees it.

  `g_main_context_invoke` was the first attempt and is wrong here: it runs the
  function inline whenever the calling thread can *acquire* the context, and
  nothing holds the default context while the main loop is between iterations --
  so the JavaScript thread acquires it and makes the blocking call itself. A test
  with no loop running proved that rather than the documentation being read
  twice, which is also the first thing in this repository to exercise the two
  threads concurrently, the second entry in the list above.

  **And this corrects a wrong answer recorded below.** "The clipboard module was
  blamed first; it is in neither stack" was true of the two stacks that had been
  read by then. The module is still not at fault -- it is portable code doing
  nothing wrong -- but the clipboard *service* under it was, and it is in this
  stack. The lesson stands with a correction: absence from the stacks you have is
  not absence from the ones you have not read.

  **How both were caught: a `hunt` input on ci.yml's `workflow_dispatch`**, which
  replaces the end-to-end step with a loop over the two context-menu scenarios
  until a host does not exit. Waiting for a stall to turn up on an unrelated push
  wasted the occurrence when it did; looping them back to back stalls on the
  *first* attempt on all three shards, which is also the sharpest fix test
  available here.

  The branch it lived on is deleted, so it is written down instead. `ci.yml`
  takes a boolean `hunt` input on its `workflow_dispatch`, the end-to-end step
  gets `if: ${{ !inputs.hunt }}`, and a step before it gets
  `if: ${{ inputs.hunt }}` and this, which all three shards run as three
  independent sets of hosts:

      for i in $(seq 1 25); do
        echo "=== attempt $i of 25"
        if ! python3 scripts/integration_test.py \
             --platform linux --build-dir build -k "developer menu reloads"; then
          echo "=== caught a stall on attempt $i"
          exit 1
        fi
      done

  `ci.yml` triggers on a `ci` branch as well as main, so a hunt needs no change
  to main to run. Pick the scenario for the teardown being chased: `-k "context
  menu"` starts nine hosts an attempt and reaches `destroyReactInstance` once
  each, `-k "developer menu reloads"` starts one and reaches it twice.

  Two things it does not promise. The frames are of whichever thread took the
  signal, and `kill` may deliver to any thread that has it unblocked, so a
  backtrace that looks unrelated to the hang is a reason to doubt the thread
  rather than the reading. And it is POSIX only: Windows has no `SIGABRT` to send
  from another process and its half of the handler is an unhandled-exception
  filter, so there a hang is still a kill. The hang this was built for is on GTK.

  The scenario's own compromise: the preconditions skip on the conditions rather
  than the platform, so GTK keeps the coverage it has. The exception is the host
  not exiting, which fails, for the reason above. The paste itself skips on
  Linux specifically when it does not arrive, which does mean **a regression that
  broke `paste` on Linux would come back as a skip and not a failure.** The hard
  assertion holds on AppKit and Windows, and `close` covers the mechanism on all
  three.

  ~~Worth knowing separately: **nothing else in the suite exercises `Clipboard` at
  all**, on any host.~~ Covered 2026-10-07 by "the clipboard round-trips, and the
  host still exits afterwards": a round trip, the second write winning, an empty
  string clearing rather than being ignored, text outside ASCII, 64KB to reach
  X11's chunked path, whitespace kept, and `getString` answering a string rather
  than null. Seven checks on all three hosts.

  That scenario is also the reproduction attempt for the hang above, every run of
  it reading the clipboard and then quitting, and it asserts the exit rather than
  only the reading. It passed on Linux CI, which is what moved the suspicion off
  the clipboard.

  Passing locally says less than it looks like, and the commit that added it
  claimed more: `build/basalt_gtk` on a developer's Mac is a Mach-O binary
  against Homebrew's GTK4, which links no X11 at all. There is no X server in
  that run and never was. The local GTK host is quartz-only and cannot reach the
  Xvfb path CI uses, which is the GTK-over-quartz caveat recorded further down
  and is why neither this nor the paste problem reproduces here.

- **The `image` comparison flaked once and nobody can say why.** It differed on
  one CI run, passed on a rerun of the same commit, and passes locally: run
  with the right module, which is a trap of its own: `compare_hosts.sh image`
  defaults to `BasaltViews`, mounts nothing on either host, and compares two
  empty trees. The diff CI produced was discarded by `compare_all.sh`, which
  said "rerun that one through compare_hosts.sh"; advice that cannot work for
  something that does not reproduce. It prints the diff now, so the next
  occurrence is diagnosable; until then there is nothing to fix and guessing
  would be inventing a cause.

- ~~**No rendering assertions on GTK.**~~ **Done 2026-10-08**, and the entry was
  wrong about the cost. The widget tree says a view has a colour and a frame, not
  that the right pixels reached the screen, and that is not theoretical: GTK's
  cairo renderer mangled every transform in the demo and no test noticed.

  Windows has them as of phase 39, because Direct2D renders offscreen with no
  window and no display; `tests/test_win32_paint.cpp` is fourteen of them.

  **macOS has them too, by a cheaper route than this entry predicted.** It
  guessed at an offscreen `NSWindow` and a display cycle. What the three files
  doing it actually use is a `CGBitmapContext` and the view's own `drawRect:`:
  no window, no display, no permission: `test_appkit_image.mm` asserts where the
  ink of each resize mode lands, `test_appkit_text.mm` that a paragraph draws
  where its alignment says, and `test_appkit_scrollbar.mm` that the overlay
  thumb is at the trailing edge.

  **And GTK has them now, by a route this entry had half right.** It guessed a
  display server and a `GdkTexture` read-back. The read-back is exactly it, and
  the display server turned out to be needed only for part of it:
  `gsk_renderer_realize` takes a NULL surface, so a node tree rasterises into a
  texture with no window at all -- which is how the `filter` tests check a colour.
  A tree with *children* does need the window, because
  `gtk_widget_snapshot_child` draws nothing for an unmapped child, the same
  reason test_hittest.cpp shows one before picking. So `tests/GtkPixels.h` puts
  the tree in a window that is never ordered front, waits for the allocation, and
  hands back straight RGBA; `tests/test_gtk_paint.cpp` is eleven assertions over
  it, and the whole file runs in a second and a half.

  Deliberately the same eleven questions `test_win32_paint.cpp` asks of Direct2D,
  in the same order: a child at its frame, opacity over a whole subtree, a clip
  that clips only with `overflow: hidden`, children in zIndex order, a scroll
  offset that moves them, the background inside its corner radii and each corner
  on its own, a border inside the box and over the children, each edge its own
  colour, and a plain view painting nothing as the control. Three hosts asking
  one set of questions is the point: the answers are supposed to be the same
  picture.

  Each of those is a bug a tree dump cannot catch, which the sabotage checks show:
  a clip pushed over a box too big to clip anything fails the clip test and
  nothing else, and painting children in list order rather than zIndex order fails
  the ordering test and nothing else. Both leave every dump in the suite
  identical.

  One thing the helper has to say out loud is which renderer answered, and it
  logs it once per run: the GL one is what an application uses, cairo is the
  fallback on a machine without GL, and they are not interchangeable for every
  question -- the cairo renderer draws a transformed subtree unrotated, which is
  in [upstream.md](upstream.md). So the eleven are all questions both renderers
  answer the same way, and transforms are still asserted through
  `gtk_widget_compute_point` rather than through pixels.
- Nothing exercises the JS thread and the main thread concurrently. **One thing
  now does**, as of 2026-10-08: `packages/basalt-gtk/native/tests/test_gtk_clipboard.cpp`
  writes the clipboard from a second thread and asserts that GDK does not change
  until the main loop runs, which is what stops the deadlock in entry 11. It is
  one seam out of the several that cross those threads, so this stays open, but
  it is the shape the rest want: assert *which thread* did the work, by asserting
  what has not happened yet.
- ~~The end-to-end scenarios hard-code tap coordinates from the demo's
  layout.~~ They find the button by its label now, in a tree measured from one
  extra run of the host per bundle. The three demo scenarios that tapped
  survive a restyle: reversing the button row leaves them passing, where the
  old constants sent the second tap of "scroll away and back" into *focus the
  field* and left the list at 1623.

  It also measures per host, which the constants could not: the three shapers
  disagree about how wide "scroll to end" is, so the centre of that label is a
  few points apart on each desktop. What is still hard-coded is the other
  apps' coordinates: `e2e/hover.tsx`'s boxes, the devtools taps, the menu
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

  **The stray-host suspicion was tested on 2026-10-09 and is wrong.** Two
  leftover `basalt_gtk` hosts and a leftover `basalt_appkit` host were left on
  screen, all three with their own windows, and the scenario passed 3 of 3 runs.
  So the one thing that happened between the last failure and the first pass
  does not reproduce the failure, and the correlation recorded above is only
  that.

  Window activation was tested at the same time, because it is the other
  obvious candidate on a backend where focus is the window server's to give:
  another application's window was made active and placed over the host's, and
  the scenario passed 4 of 4. GTK's `focus()` does not need its window to be
  active on quartz. For the macOS host's *real-click* scenario that same
  condition is fatal rather than harmless, 0 of 3, which is a different bug and
  is fixed; `docs/TESTING.md` records it.

  So the next suspect is what the entry already named, the quartz backend
  itself, and the answer stands: this scenario should not be believed off a
  real Linux session at all. What is left to try is a run under `--input real`
  on a real Linux session against the same build, which is the only
  configuration CI has never been red in and a developer Mac has never been
  green in.

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

- ~~**The hover scenario cannot assert its order on GTK-over-quartz.**~~ Fixed
  2026-10-07, and the cause recorded here before that was wrong twice over, which
  is worth keeping rather than quietly replacing.

  It said the scenario "is skipped there with a note rather than loosened" and
  blamed a real cursor sitting over the card when the window maps, arriving as an
  extra `enter card` at the start. Neither held. The skip is conditional on the
  subsequence check failing, so it never covered the failure actually seen, which
  was a hard red on a developer's Mac. And the observed log had no leading `enter
  card` at all: it passed the subsequence check and failed the later assertion
  that crossing between the card's own children must not leave the card.

  **What it was**, measured rather than reasoned about. GTK's real-pointer motion
  controller was attached with no gate and fed the same single `HoverTracker` the
  scripted sequence asserts on. Logging both real-pointer paths showed `onMotion`
  firing at the *same* coordinates over and over, once after each scripted move,
  with the mouse untouched: GTK re-runs crossing detection when the widget tree
  changes, and `e2e/hover.tsx` appends a pip to a tally row on every hover event,
  so each scripted move mutated the tree and brought a real motion at the
  stationary cursor. That cursor was over the page and below the card, so the hit
  chain found no hover listeners and `HoverTracker::admitMove` sent one more move
  to tell the processor the pointer had left, which unwound the path mid-sequence.

  Every step of that is correct in isolation, including React Native's ordering.
  The fault was the shared state, so the real pointer no longer touches the hover
  state while `BASALT_TEST_HOVER` is driving it. The touch half is deliberately
  left alone: an xdotool drag is real input and wants its motion.

  Why it only bit there: Xvfb has no pointer, so none of it ever fired on CI,
  which is why CI was asserting something real the whole time. AppKit never had
  it, its tracking area being `NSTrackingActiveInKeyWindow`, so a window that is
  not key delivers no real motion.

  Checked by running it three times where it had failed three times.

- **One flaky end-to-end scenario.** "focus a TextInput, type, and see it
  round-trip through React" failed once in five consecutive runs of
  `scripts/integration_test.py` with "the focus command did not move focus to
  the field", and passed the other four. The scenario schedules taps at fixed
  delays and assumes the host has caught up, which is a timing assumption rather
  than a synchronisation. Fixing it means waiting on something observable (  the tree, or a log line) instead of on a clock.

  **A second one, and this one crashed.** Believed fixed; the mechanism is below
  and the fix is a mount that cannot be applied after the instance that made it
  is gone. What cannot be claimed is a reproduction: it never failed on a
  developer Mac, so the fix is reasoned from the code and from the log, and the
  evidence that it holds is that CI stops doing it.

  "the developer menu reloads, and shows the element inspector" failed twice on
  CI's macOS runner at 6e58901 and then passed on a third attempt with no change,
  so it is intermittent rather than broken. The two failures were not the same
  failure, which is the interesting part:

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
  (`--platform macos --shard 1/3`, 11/11).

  **What it was.** `executeMount` is called on the JS thread and every host
  queues the transaction to its UI thread. `ReactHost::reloadReactInstance`
  starts a detached thread, stops the surfaces, destroys the Scheduler and the
  SurfaceManager, and builds new ones -- while the mounting manager survives,
  because React Native keeps that across a reload and only sets its scheduler
  task executor to null. So a transaction could still be sitting in the UI
  thread's queue, and be applied against a Scheduler that no longer existed,
  walking mutations whose event emitters pointed into the dead instance. A hang
  and a segfault are both what that looks like, which is why the two attempts
  failed differently.

  A pending mount now carries `MountingWalk::mountGuard()` and the epoch it was
  queued in, and drops itself when either says it is stale. The hosts bump the
  epoch from `setSchedulerTaskExecutor(nullptr)`, which is the only notice React
  Native gives that an instance is going away, and clear the event emitters there
  for the same reason. The guard is a `weak_ptr` rather than a `use_count()` test
  because several mounts can be queued at once and each one's own copy would
  otherwise look like the manager still holding its.

  Checked: the reload scenario still passes, which is the half that would break
  if the guard dropped a mount it should have applied -- three times on AppKit
  and once on GTK. Windows is unverified locally, it not compiling on a Mac.

  ~~**Still missing, and it is why two attempts produced no address:** the host
  writes nothing on a signal.~~ Done, 2026-10-06: core/CrashHandler.h. SIGSEGV,
  SIGBUS, SIGILL, SIGFPE and SIGABRT now print a marker, the faulting address,
  the thread and up to 64 frames, and re-raise so the exit status is still the
  signal. Not SIGTERM, which is how the harness ends a host that is working.

  Exercised rather than assumed: `BASALT_TEST_CRASH` raises the signal on
  purpose and the scenario "a crash says where it died, and still exits with the
  signal" asserts the marker, that there is a stack rather than one line, and
  that the status survives. A handler that has never run is a guess.

  So if this scenario fails again, the next run says where. Which is what it
  should have said the first time.

  **It failed again, at 1162ced, and the next run did say where.** The guard
  above is therefore incomplete:

      UIManager::reportMount(int) + 264
        <- AppKitMountingManager::applyTransaction
          <- applyPendingMount <- _dispatch_main_queue_drain

  `host exited -11` on CI's macOS runner, the same scenario, while every Linux
  and Windows shard passed. Not caused by the commit it failed on, which touched
  GTK's focus call, the crash handler's thread note and the harness.

  **What it is, read off the pinned React Native (v0.87.1) rather than guessed.**
  A mount hook destroyed without unregistering, and not one of ours.

  `UIManager::mountHooks_` is a `std::vector<UIManagerMountHook*>`: raw,
  non-owning pointers, where unregistering is the owner's job and
  `~UIManagerMountHook` does not do it. `Scheduler` registers one at
  `Scheduler.cpp:169`, `uiManager->registerMountHook(*eventPerformanceLogger_)`,
  and `~Scheduler` unregisters every *commit* hook at `:197` and never that. The
  only `unregisterMountHook` call in the whole tree is in
  `IntersectionObserverManager`.

  `ReactHost::destroyReactInstance` then reads, in order:

      stopAllSurfaces();                                   // the registry will now miss
      quitSynchronous();                                   // where the teardown hang blocks
      surfaceManager_ = nullptr;
      scheduler_ = nullptr;                                // EventPerformanceLogger freed here
      schedulerDelegate_ = nullptr;
      contextContainer->erase(RuntimeSchedulerKey);
      mountingManager->setSchedulerTaskExecutor(nullptr);  // our guard arms here

  So the dangling pointer appears three statements before we are told anything.
  A mount draining on the main queue in that gap passes both checks honestly, the
  UIManager still being alive because `UIManagerBinding` holds it in a runtime
  that is destroyed later, and `reportMount` finds no root shadow node for a
  stopped surface and calls `shadowTreeDidUnmount` virtually on freed memory.
  That is the SIGSEGV, and the width of the gap is why it is intermittent.

  **Worked around on this side, as of 2026-10-07, and the ordering is what makes
  it possible.** `stopAllSurfaces()` runs before the Scheduler is freed and takes
  the shadow trees out of the registry, so "the hook is dangling" always implies
  "the surface has gone". All three hosts now report through
  `reportMountedSurface` in core/UIManagerAccess.h, which asks whether the
  UIManager still has the surface and refuses if not: exactly the dangerous case,
  and a call that had nothing to report anyway, since with no root shadow node
  `reportMount` only tells every hook the tree unmounted.

  The window shrinks from three statements of teardown to the few instructions
  between the question and the call. It does not close. The fix is one line
  upstream, `~Scheduler` unregistering the mount hook it registers, tracked as
  entry 14 of [upstream.md](upstream.md) along with the decision not to send it,
  and both of the bugs left in this file are in that one function, the other
  being `quitSynchronous` above.

  **What is not checked, and should be said rather than implied:** nothing in
  the suite asserts that a mount hook ever ran. The call exists for Reanimated,
  the only animation scenario is `scrollTo({animated: true})` which does not go
  through a hook, and so a probe that wrongly refused a *live* surface would
  leave the suite green and Reanimated silent. A live surface is in the registry
  by construction, which is the same question `reportMount` asks itself, and the
  reload scenario passes on both hosts. That is the argument; it is not a test.

  What was ours and is fixed: `mountEpoch_` was a plain `std::uint64_t` written
  from the detached reload thread and read on the main thread, which is a data
  race in the guard that exists to prevent a use-after-free. It is atomic now.

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
  that a commit was tested.

  **One sentence here was wrong and is worth correcting rather than deleting.**
  It said the cost on main is that quick pushes "queue instead of cancelling,
  which on a public repository is patience rather than money". They do not.
  `cancel-in-progress` protects a run that is *in progress*; a run that is merely
  *queued* is still cancelled, because GitHub keeps one pending run per
  concurrency group and a new arrival replaces it. Measured 2026-10-07: the run
  for 8a60f17 sat queued for thirteen minutes with zero jobs started and was
  cancelled five seconds after the next push created its run. So the cost is a
  lost run, not patience.

  What saved it from mattering that day: the lost commit was an ancestor of the
  next one, so the run that did go ahead covered its changes. What is lost in
  general is per-commit attribution, which is the thing this entry is about.

  Closed by grouping on the SHA on main, so no two commits share a group, nothing
  queues behind anything and nothing is replaced. The bill moves from patience to
  concurrency, and what it meets next is the account's own job limit, which
  queues without cancelling. `cancel-in-progress` is moot on main now and is left
  spelled out, the two settings together being what make the behaviour readable.

  And `scripts/ci_status.py` reads the last run that reached a *verdict* per
  job, rather than the last run, with the count of newer runs that did not,
  which is the size of the blind spot, and on the day this entry describes would
  have read 5. Shards fold together, and a job with one cancelled shard is
  undecided rather than green, because three green shards and one cancelled is
  not a tested commit. `scripts/test_ci_status.py` checks that logic against the
  exact history above, because a bug in this tool would point the same
  comfortable way the original problem did.

  A lesson from using it in anger, 2026-10-07: the temporary hunt described
  further up this file reused the `build and test` job name, so a hunt run's
  verdict read as an ordinary suite verdict even though the job had run a loop of
  two scenarios instead of the suite. `ci_status.py` was right that the job
  reached a verdict; it had no way to know the job had been swapped out from
  under the name. A temporary job wants a temporary name.
