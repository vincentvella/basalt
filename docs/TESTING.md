# Testing

All of it, in the order CI runs it:

```bash
make test           # everything this machine can run
make test-quick     # unit and Node suites only
```

## Running one thing

The whole suite is about a quarter of an hour per host, and most changes want
one part of it. Everything here narrows, and each narrows the same way: a
substring of a name, and `--list` to see the names.

The `make` targets above are these with the arguments filled in, and they pass
`BASALT_BUILD_DIR` through, so `make test BUILD=build-087` tests the tree it
built. Narrowing is what the scripts are for, and is below.

```bash
scripts/test_all.sh --list                  # the step names
scripts/test_all.sh --only unit             # steps whose name contains "unit"
scripts/test_all.sh --host gtk --only end   # the other host, end-to-end only

python3 scripts/integration_test.py --list                    # scenario names
python3 scripts/integration_test.py -k window                 # one scenario
python3 scripts/integration_test.py -k window -k menu         # or several
python3 scripts/integration_test.py --platform macos -k hover # and which host

./build/basalt_gtk_tests --list             # test names
./build/basalt_gtk_tests limits             # tests whose name contains "limits"
./build/basalt_gtk_tests window switch      # or several
```

A filter that matches nothing says so and exits non-zero, rather than reporting
that everything passed. That is not hypothetical: the unit binaries used to
accept an argument and ignore it, so `basalt_gtk_tests hover` ran all 185 tests
and printed a pass, which looks exactly like a filter that worked.

It builds nothing but the demo bundle, and skips what is not built or not
installed rather than failing, so on a machine with one host it runs that
host's suites and says which it left out. CI's jobs and that script are two
spellings of one list; a suite added to one belongs in the other.

The pieces, for when one of them is the thing being worked on. The Linux ones
need a display, because everything in them is a GTK program, and CI runs them on
Linux, which is where they matter: see `.github/workflows/ci.yml`.

```bash
./build/basalt_gtk_tests            # unit, Linux
./build/basalt_appkit_tests         # unit, on a Mac
./build/basalt_win32_tests.exe      # unit, on Windows

make bundle                         # a production bundle to test against
make e2e                            # end to end, on whichever host is built

make compare-hosts                  # every host built, same app, diffed
make compare                        # every app in e2e/, through every host
node --test scripts/test_cli.js     # run-linux, run-macos and run-windows
node scripts/check_includes.js      # a header used and not included
```

The last two need nothing built, and run in both CI jobs. `check_includes.js`
exists because a header that reached `<cstdint>` through windows.h took the
Linux job down for twenty-three commits and could not be seen from the machine the
work was on; it reads text, so it cannot fail on one platform and pass on
another.

`integration_test.py` picks whichever host it finds and runs the same scenarios
against it, because they are about React and Fabric rather than about a
toolkit, `--platform` overrides the guess. Every scenario runs on every
platform; the packager the two development-mode ones need is started through
`scripts/metro.js`, in Node, because a shell script is not a way to start one
on Windows.

On a headless machine, run either under a virtual display:

```bash
xvfb-run -a ./build/basalt_gtk_tests
xvfb-run -a scripts/integration_test.py
```

`basalt_gtk_tests` exits 77 rather than failing when it cannot reach a display, which
is the convention for "skipped".

### Screenshotting a GTK window on macOS

`screencapture -l <window-id>` wants a CGWindow id, which nothing in the shell
exposes. A five-line Swift program over `CGWindowListCopyWindowInfo`, filtered
by `kCGWindowOwnerPID`, produces one. Compile it once with `swiftc`: `swift
<file>` interprets slowly enough to miss the window's first frame.

`basalt_appkit_tests` needs neither a display nor a window: nothing in it is presented.
It is built only on a Mac, and CMake omits the target everywhere else.

`basalt_win32_tests` needs neither either, and unlike the other two it makes
real pictures while not needing them: Direct2D renders into a WIC bitmap with no
window, no device and no display connection. That is why the rendering
assertions this project has wanted since GTK live there and nowhere else. Built
only on Windows; CMake omits the target elsewhere.

| File | What it pins down |
|---|---|
| `native/tests/test_win32_view.cpp` | The view layer as a tree. Tags and frames, insert indices, that a Remove detaches without destroying, that an Insert reparents, that destroying a view leaves nothing pointing at it, that zIndex does not touch the child list, and that `describeTree` is byte-for-byte what the other two hosts print. |
| `native/tests/test_win32_paint.cpp` | What reached the pixels. Placement, opacity as a subtree layer, clipping under `overflow`, a quarter turn about the centre *and in the right direction*, zIndex paint order, the scroll offset, and the corner radius. Each is something a tree dump cannot show. |
| `native/tests/test_win32_hittest.cpp` | Picking. Depth, sibling order, zIndex, misses, scroll offsets, hidden views, and that a press follows a `transform`, which this platform has to invert itself, having no toolkit picking to inherit. |
| `native/tests/test_win32_text.cpp` | DirectWrite measurement. Wrapping, `numberOfLines`, that a bigger font measures bigger, that alignment does not change the measured size, and two regression tests carried over from the other hosts: that font sizes are absolute rather than points, and that the default ellipsize mode does not collapse a wrapping paragraph. Plus one they cannot make: that the glyphs actually move when the alignment does. |
| `native/tests/test_win32_image.cpp` | The four resize modes, against a real bitmap and real pixels rather than against the destination rect: `cover` that scales correctly and forgets to clip looks right in a rect and wrong on a screen. Also the WIC decode, round-tripped through the snapshot encoder. |
| `native/tests/test_win32_accessibility.cpp` | The UI Automation mapping, asked the way a screen reader asks it: build the provider and read property ids. Roles, the group fallback, that a plain `<View>` stays out of the tree, labels and hints, the tri-state states, and that a role can change after mount, which GTK cannot do, because a GtkAccessible role is construct-only. |
| `native/tests/test_win32_mounting.cpp` | The mutation walk, through the real Win32MountingManager. Deliberately the same nine questions asked of GTK and AppKit, in the same order with the same tags, so a divergence in the shared walk fails here rather than in an app. Built only when the build was pointed at a React Native. |
| `native/tests/test_win32_imageloader.cpp` | The image loader. Decoding a file URI, the cache answering without going back to disk, the three ways a load can fail, and that a decoded image survives the loader that produced it. Built only when the build was pointed at a React Native, because the fetch is core/ImageBytes.cpp. |

### Watching an event go past

`native/tests/EventRecorder.h` is how a unit test sees that an event fired.
Neither suite could: the managers ask `eventEmitterForTag` for an emitter and
the hand-built shadow views carry none, so what the text-input tests pinned
was, in their own words, "that the round trip did not throw".

A stub emitter cannot fix that, `TextInputEventEmitter` contains the word
`virtual` zero times, so the recorder builds the real thing and listens
underneath it. `EventDispatcher` takes a `std::function<bool(const RawEvent
&)>` and consults it before the logger and before the queue; returning true
stops the default dispatch, so no `RuntimeScheduler` ever has to run.

What it sees is the event's type and its position in the order. Payload
values need a `jsi::Runtime` to read, so what a value ends up as stays the
end-to-end suite's question.

## `build/basalt_gtk_tests`: the unit suite

Everything reachable without a JavaScript runtime. No React, no Metro, no
Hermes: mutations are hand-built, the way `mount_harness_gtk` builds them.

| File | What it pins down |
|---|---|
| `native/tests/test_mounting.cpp` | The mutation walk. Create/Insert/Remove/Delete/Update ordering, insert indices, nesting, that Remove detaches without destroying, that Delete unregisters, and that a stray Insert for a deleted tag is ignored rather than fatal. |
| `native/tests/test_view.cpp` | The widget layer. Frames, child order, that a scroll offset moves children, that `measure` reports zero so GTK never competes with Yoga, and that dispose unparents children. |
| `native/tests/test_text.cpp` | Pango measurement. Wrapping, `numberOfLines`, that a bigger font measures bigger, and two regression tests: that font sizes are absolute rather than points, and that the default ellipsize mode does not collapse a wrapping paragraph to one line. |
| `native/tests/test_hittest.cpp` | Hit testing. Depth, sibling order, misses, and that it follows a scroll offset. |
| `native/tests/test_image.cpp` | The image loader. Decoding, the cache answering synchronously, and the three ways a load can fail. |
| `native/tests/test_textinput.cpp` | The controlled-value loop. That applying a prop is not reported back as typing, that the caret survives a prop arriving mid-word, that a command carrying a stale `eventCount` is dropped, and that the `GtkText` peer is allocated inside the content inset. Props are built through React Native's own `RawProps` parser, because the fields that matter are const and only reachable that way. Two tests watch the *events* rather than the widget, through `native/tests/EventRecorder.h`. |

## `build/basalt_appkit_tests`: the macOS suite

Two files, sixteen tests, no window and no toolkit initialisation.

`native/tests/test_appkit_view.mm` is against `RnAppKitView` alone, with no Fabric and no
React Native in it. It pins the tag and frame, the child order including
insertion into the middle, that removing a view that is not yours is a no-op,
that background colour, opacity, clipping and corner radius reach the CALayer,
that the background is in sRGB rather than the display's space, and that
`describeTree` emits the same text the GTK side does.

`native/tests/test_appkit_accessibility.mm` asserts the *AppKit* role, not the
React Native name: the name is what `describeTree` reports and what the
cross-platform diff compares, and whether the mapping onto `NSAccessibility`
actually happened is a platform question. The GTK suite's
`test_accessibility.cpp` asserts the same thing about `GtkAccessibleRole`.

`native/tests/test_appkit_input.mm` is hit testing and the touch state machine.
It covers what `test_win32_hittest.cpp` does, including that a press follows a
`transform`: a translation, a quarter turn, and a `scale: 0` that has no
inverse and is skipped. Hit testing is a pure function of the view tree, so it
needs no mouse, no window server, and no permission to synthesise an event, which matters, because a
real click on macOS means `CGEvent` and accessibility permission an automated
run does not have. Unlike the GTK equivalent none of it needs a window:
`gtk_widget_pick` skips unmapped widgets, so those tests show a window and pump
the frame clock, and `RnAppKitHitTest` walks frames and answers straight away.

`native/tests/test_appkit_mounting.mm` is against `AppKitMountingManager`, and is
deliberately the same questions `native/tests/test_mounting.cpp` asks of GTK, in
the same order, with the same tags and frames. Since both managers now share
their mutation walk, most of what these test is whether that sharing still holds.
Built only when the build was pointed at a React Native; without one the view
tests still run.

The one worth naming is `mac_view_is_flipped_so_frames_are_top_left`. AppKit's
origin is bottom-left and every frame Fabric produces is top-left, so a child at
y=32 in a 420-tall parent belongs 32 from the top; unflipped it would sit at 228
and the layout would look plausible and be wrong.

`native/appkit/mount_harness_appkit.mm` is the mounting equivalent: the same two
transactions `gtk/mount_harness_gtk.cpp` runs, driven through the real mounting
manager with no JavaScript anywhere. `BASALT_SNAPSHOT_DIR=/tmp/out
./build/mount_harness_appkit` writes `mount-1.png` and `mount-2.png` and prints the
tree after each; with no directory set it opens a window and applies the second
transaction two seconds in, the way the GTK harness does.

`scripts/compare_all.sh` runs every app in `e2e/` through every host that is
built and prints where the platforms stand. Windows against Linux in WSL:

```
hosts: windows linux (linux in Ubuntu-24.04)

  views        identical, frames included
  press        identical, frames included
  scroll       identical, frames included
  image        identical, frames included
  text         identical, frames ignored
  a11y         identical, frames ignored
  input        identical, frames ignored
  appearance   identical, frames included
  blob         identical, frames included
  modules      identical, frames included
  probe        identical, frames included
  index        identical, frames ignored

the 2 hosts agree on all 12 apps
```

An app that only passes with frames ignored is one whose layout depends on text
measurement. Everything else (tree shape, strings, colours, roles, flags) is
still compared there.

`scripts/compare_hosts.sh` is the single-app version, and the one that cannot
run in CI. It runs `e2e/views.js` (a real React app made only of `<View>`) through every host that is built, and checks two things that fail differently:
that the view trees match, and that each host's log reports the `Platform.OS`
its bundle was built for. A bundle built for the wrong platform can render
perfectly and be wrong about everything `Platform.OS` guards.

It takes an app name rather than a bundle path per platform, and finds
`build/<app>.<platform>.jsbundle.js` for each host it is going to run. Three
paths on a command line was one too many.

It needs at least two hosts, so it runs on a developer's machine rather than on
CI, where each host only exists on its own side. On a Mac with GTK installed,
build the bundles first:

```bash
scripts/bundle.sh --platform linux --entry views.js --out views.linux.jsbundle
scripts/bundle.sh --platform macos --entry views.js --out views.macos.jsbundle
scripts/compare_hosts.sh
```

On Windows the second host is the GTK one, running inside WSL. Ubuntu 24.04,
because the GTK host needs GTK 4.10 and 22.04 ships 4.6, installed beside any
distro already there:

```bash
wsl --install -d Ubuntu-24.04 --no-launch
wsl -d Ubuntu-24.04 -u root -e bash scripts/wsl_setup.sh
BASALT_COMPARE_WSL=Ubuntu-24.04 scripts/compare_all.sh
```

`wsl_setup.sh` installs what the Linux CI job installs, clones this checkout
onto the distro's own filesystem; building across `/mnt/c` is too slow to be
worth it, and builds the host there the way CI does. So it compares the
*committed* state: rerun it after committing. `compare_all.sh` builds any
missing Linux bundles inside the distro, and runs the host under Xvfb rather
than WSLg, because Xvfb is what CI uses.

It also registers something to open `https` with. `Linking.canOpenURL` asks
GIO whether anything handles the scheme, a bare distro has nothing, and
`e2e/modules.js` then fails a check on Linux that a GitHub runner, which has a
browser, passes, which is exactly what the first comparison through WSL
reported as a difference.

`BASALT_COMPARE_TAP="x,y;x,y"` forwards taps to every host; they read the same
variable, so the input path is compared too, not just the initial render.
`e2e/press.js` is the app for that, and needs a longer quit than the default
since the first tap is at 1500ms:

```bash
BASALT_COMPARE_TAP="400,100;400,100;400,100" BASALT_COMPARE_QUIT_AFTER_MS=5000 \
  scripts/compare_hosts.sh press BasaltPress
```

`e2e/scroll.js` scrolls itself through `scrollTo` on a ref, which is how both
hosts can be driven identically from one file: a wheel has to be injected per
platform and a command does not, and is the only test of the command path:

```bash
scripts/bundle.sh --platform linux --entry scroll.js --out scroll.linux.jsbundle
scripts/bundle.sh --platform macos --entry scroll.js --out scroll.macos.jsbundle
BASALT_COMPARE_QUIT_AFTER_MS=3000 scripts/compare_hosts.sh scroll BasaltScroll
```

For `e2e/text.js` and `e2e/image.js`, frames have to be ignored:

```bash
scripts/bundle.sh --platform linux --entry text.js --out text.linux.jsbundle
scripts/bundle.sh --platform macos --entry text.js --out text.macos.jsbundle
BASALT_COMPARE_IGNORE_FRAMES=1 scripts/compare_hosts.sh text BasaltText
```

Pango over the system sans and Core Text over San Francisco are different
shapers over different fonts, so the same paragraph is a few points taller on
one than the other and every frame below it shifts. Demanding equality there
would mean the check could never be turned on for text at all. What is still
compared is everything that must match: the tree shape, the strings, the
colours, the clip and opacity flags.

As of phase 23 that comparison differs by exactly one thing, GTK emits
`role=label` on a paragraph and macOS emits nothing, because macOS has no
accessibility yet.

`native/appkit/demo_layout_appkit.mm` is the visual half of the view layer. It carries the same boxes at
the same coordinates as the GTK `demo_layout_gtk`, so the two can be compared
directly, and `BASALT_SNAPSHOT=out.png ./build/demo_layout_appkit` renders them
offscreen to a PNG. `BASALT_DUMP_TREE=1` prints the tree instead: useful, but
it would print the same thing whether or not the flip works, which is what the
snapshot is for.

## The harness

`native/tests/TestHarness.h`, about sixty lines, no dependencies, shared by all
three. `TestHarness.cpp` is the runner and knows about no toolkit; each suite
brings its own `main`: `tests/main_gtk.cpp` initialises GTK, and the macOS one
sits at the bottom of `test_appkit_view.mm`. Its
assertions are `EXPECT`, `EXPECT_EQ` and `EXPECT_NEAR` rather than `CHECK*`
because glog (which React Native pulls in almost everywhere) already defines
`CHECK` and `CHECK_EQ`. Those win the preprocessor silently, and a failing
assertion then aborts the process instead of being reported, taking every later
test with it. That happened; hence the naming.

Hit tests put their widgets in a real window. `gtk_widget_pick` skips widgets
that are not mapped, and a widget is only mapped inside a shown window.
Allocation then happens in the frame clock's layout phase, so the tests build
the tree first, show the window, and spin the main loop until a frame has
actually gone through. Draining the context without waiting is not enough: with
no frame due it returns immediately and the children are still unallocated.

## `scripts/integration_test.py`: the end-to-end suite

Runs the real host against the real Metro bundle, injects taps, and asserts on
the widget tree the host writes on the way out.

This is what `BASALT_DUMP_TREE` exists for. The path this project exists to
provide (JavaScript, React, Fabric, the mounting manager, GTK) can only be
exercised by running the whole thing, and before the dump the only way to check
the result was to look at a screenshot.

What the scenarios are is not listed here, and deliberately: `SCENARIOS` at the
bottom of `scripts/integration_test.py` is the list, and every one of them has a
docstring saying what it asserts and why that is the thing worth asserting. A
copy in this file was five entries long while the suite had twenty, which is
what a hand-maintained list of something the code already states turns into.

What is worth writing down is what is not in the code.

**They read coordinates from each app's own layout.** Most tap fixed points:
`e2e/index.js` for the demo, and its own file for each of the apps written for a
scenario. Change a demo's spacing and the coordinates need changing too. The
alternative, searching the dumped tree for a button by its label and tapping its
centre, would be more robust and is worth doing; the reason it has not been is
that a wrong coordinate fails loudly and immediately.

**Several apps set fixed heights on their labels for this reason.** Text
measurement is the one thing Pango, Core Text and DirectWrite will never agree
on, so an app whose rows are positioned by measured text has different
coordinates on each host. A `height` on a label is what keeps one set of taps
working on all three.

**Fast Refresh is the only scenario that runs in dev mode**, which is why it
exists: "development still works" was being taken on trust, and a wrong claim
about it reached the README. It starts its own Metro on port 8099, edits
`e2e/index.js` while the app is on screen, and restores the file after the host
has exited, not before, because putting the original back while the app still
has a Metro connection triggers a second refresh that undoes the edit before the
tree is dumped, which looks exactly like Fast Refresh being broken. A `kill -9`
of the test would leave the demo edited; nothing softer will.

**The edit does not work in CI.** Metro on a GitHub runner never notices one:
the file changes on disk with a fresh mtime, a newly requested bundle still
carries the old text, and Metro's log is empty of complaint. `fs.watch` sees the
same edit on the same runner, the inotify limits are already generous, both sides
run the same Node, and the upstream Watchman build reports `"watcher": "inotify"`
over exactly the root Metro was given and changes nothing. It passes on
developer machines, on macOS and on Linux.

`BASALT_SKIP_FAST_REFRESH` therefore drops **the edit, not the scenario**. It
used to drop the whole thing, which meant no machine other than somebody's
laptop checked dev mode at all, and what CI cannot do is one specific half of
it. Everything before the edit is the host's own code: dev mode, the dev server
helper, the websocket, the `DevSettings` TurboModule, and a bundle fetched from
Metro rather than the release one sitting on disk. Two bugs have already lived
in that half: the host read its Metro entry from an argument nothing passed,
and every platform reported a dev script URL naming `linux`, and both would
have been caught with no edit made. When the variable is set the scenario passes
with a note saying what it did not check, which is the point: a scenario that
quietly checks less than its name says is worse than one that skips outright.

## Input: real events, and where they are not

The end-to-end suite delivers taps one of two ways, and says which at the top of
its output.

**`real`**: `xdotool` moves the pointer and clicks, so the event goes through
the X server and GDK exactly as a person's would. This is the only mode that
exercises event delivery itself. Chosen automatically when `DISPLAY` is set and
`xdotool` is installed.

**`injected`**, `BASALT_TEST_TAP` enters at the touch dispatcher, skipping the
window system. The fallback where a real event cannot be synthesised, which is
both of the other desktops: macOS needs accessibility permission an automated
run does not have, and Windows needs `SendInput`, which moves the real cursor
and so cannot run beside anything else on the machine.

Force either with `--input real` or `--input injected`.

Typing splits the same way. In `real` mode `xdotool type` sends key events
through the X server, so GDK and the input method see them. In `injected` mode
`BASALT_TEST_TYPE` reaches whatever field has focus: through `GtkEditable` on
Linux, the field editor on macOS, and real `WM_CHAR` messages on Windows, which
is the closest of the three; what it skips there is the keyboard driver and
nothing above it.

Windows is the exception to the exception in one place. A real click on a
`<TextInput>` never reaches the touch dispatcher at all, because the field's
peer is a child window and USER32 routes the click to it; so `BASALT_TEST_TAP`
focuses the field under the point as well as dispatching the touch. That step
exists only for the injected path.

Hover has no `real` mode on any of the three. `BASALT_TEST_HOVER` takes the same
`"x,y;x,y"` as `BASALT_TEST_TAP` and moves the pointer without pressing it; a
negative point is the pointer leaving the surface. There is no real-event
equivalent because a real hover means moving the machine's actual cursor onto
the window and leaving it there, which takes the pointer away from whoever is
using the machine, `xdotool mousemove` included.

Keyboard focus is injected everywhere too, for the same reason.
`BASALT_TEST_FOCUS` takes actions separated by `;`: `tab`, `shift-tab`,
`activate` and `escape`, and enters at each host's focus manager. A real Tab
needs a window the display server considers focused, which an automated run does
not reliably have on any of the three; what the instrument skips is the delivery
of the keystroke and nothing above it.

`escape` and `devmenu` are the odd ones in that list and are there on purpose:
neither is a focus action. `escape` closes the topmost `<Modal>`, and `devmenu`
opens React Native's developer menu, the thing Ctrl+D (Cmd+D on macOS) opens.
This is the instrument for "a key was pressed and nothing on the window has to
be focused for it to arrive", which is what both of those are.

A popup menu is the second thing an automated run cannot get past, and for a
sharper reason than a dialog: on macOS `popUpMenuPositioningItem` runs the
menu's own tracking loop on the main thread, so a menu nobody dismisses stops
the process where it stands. `BASALT_TEST_MENU` answers it: an entry index, or
`dismiss`: exactly as `BASALT_TEST_DIALOG` answers an alert, and with the same
limit: only the presentation is skipped, and whatever the entry does still
happens.

A file dialog is the third, and the one with the most behind it: what an app
does with a path cannot be reached without a path. `BASALT_TEST_FILE_DIALOG`
answers one: `cancel`, or a list of paths separated by `:` (`;` on Windows,
where a path starts `C:\` and a colon would cut every one of them in two).

A tap may name a window: `BASALT_TEST_TAP="134,110;134,110@3"` taps in the app's
own window and then in the window whose surface is 3. Without it a second window
could not be reached at all: each has its own touch dispatcher, and the app's
would happily hit-test a tree that is not on screen and report a press on
whatever was at those coordinates. A point with no `@` means the app's own
window, so every spec written before windows existed still means what it did.

`BASALT_TEST_SECONDARY_TAP` takes the same spec as `BASALT_TEST_TAP` and clicks
the other button. Its own variable rather than a suffix on a point, because the
two assert opposite things, one presses what it lands on and the other must
not, and a scenario that mixed them in one string would be harder to read than
to write.

`BASALT_TEST_CLOSE_WINDOW` takes a surface id and closes that window the way a
*person* would, its own close button rather than the app asking. The two are
different paths through the host, and only that one can leave the host holding a
record whose window is gone while the app still believes it is open.

It is also the only thing an app's interception ever refuses, which is what
makes it the instrument for `onCloseRequest`: an app closing its own window is
not asking anybody. `1` names the app's own window, and a host whose main window
refuses to close still has to shut down when `BASALT_QUIT_AFTER_MS` says so:
on Windows that meant the quit timer stopping going through `WM_CLOSE`, because
otherwise the app would have refused the harness too and the failure would have
been a hang rather than a test.

## Skia, where the tests do not run in CI

`tests/test_appkit_skia.mm` is compiled only when the build was pointed at an app
that has `@shopify/react-native-skia`, because the package is borrowed from the
app rather than vendored, so these run on a developer's machine and never in
CI, which has no app. That is a real gap and is the price of not vendoning a
300MB dependency; the rest of the suite is unaffected either way, and
`BASALT_SKIA` unset means none of it is compiled.

What they assert is pixels, not objects. `RNSkiaModule: Skia installed` says the
bindings were constructed, and that is a long way from a pixel: a Metal device
that fails to create, a `GrDirectContext` that comes back null, a surface wrapping
a texture nothing can read, each installs perfectly well and then draws nothing.
So a known shape goes into an offscreen surface and the pixels come back out,
which is also the path an app rendering frames for export takes.

Verified falsifiable by drawing the rectangle somewhere else, which failed both
halves at once:

```
expected inside.describe()  == "rgba(255,0,0,255)" but got rgba(0,0,0,0)
expected outside.describe() == "rgba(0,0,0,0)"     but got rgba(255,0,0,255)
```

"Drew something" and "drew it where it was asked to" are separate claims, and
that is the pair that tells them apart. Corners rather than edges, for the reason
`test_appkit_image.mm` gives: antialiasing puts a boundary half a pixel either
way and pinning it fails on a future Skia for no useful reason.

## Knowing whether CI actually passed

`scripts/ci_status.py` says what each job last *decided*, which is not what `gh
run list` shows. `gh` shows the latest run; when that run was cancelled by a
newer push there is no verdict in it, and nothing distinguishes "not tested" from
"tested and fine"; a cancelled job is not a failed one.

That cost five runs once. The Linux job was cancelled on five consecutive pushes
to main while Windows failed on every one of them, and what exposed it was a
sixth push adding a macOS job, whose failure was then assumed to be new. Five red
runs read as four cancellations and a mystery.

```
  build and test           success   ace5dd3  2026-09-26T06:25:27Z
  the AppKit host          success   ace5dd3  2026-09-26T06:25:27Z
  the whole Windows stack  success   ace5dd3  2026-09-26T06:25:27Z
```

Each row is the newest run that job reached a verdict in, and a row gains `<- and
N newer run(s) never finished, so this is older than it looks` when there are
later runs it said nothing in. That count is the blind spot, and it is the number
that would have exposed the day above.

Shards fold together (`the AppKit host (shard 2)` is the AppKit host) and a
job counts as decided for a run only when *every* shard decided. Three green
shards and one cancelled is not a tested commit. It exits non-zero when any job's
last verdict was a failure, so it works as a check and not only a report.

The root cause is fixed too: `cancel-in-progress` is now
`${{ github.ref != 'refs/heads/main' }}`, so main never loses a result and
branches still free their runners. `scripts/test_ci_status.py` checks the logic
against that five-run history, because a bug in a tool like this points the same
comfortable way as the problem it was written for.

## Windows, on a machine you own

The Windows job is the one nothing here can check. No development machine in this
project runs Windows, so a Windows-only file is written, pushed, and answered
fifteen minutes later, and if it is wrong, that is fifteen minutes per attempt.
It is also the least of the problem: the Windows implementation of the subprocess
capability passed on its first run, and the two things that failed around it were
a test that assumed my machine and a missing include.

So this is worth setting up when there is Windows work in flight, and not
otherwise.

`scripts/setup_windows_runner.ps1`, run as administrator **on the Windows
machine**, enrols it under the label `basalt-windows` and installs the runner as
a service. It checks first and installs nothing: Visual Studio with the C++
workload *and* `C++ Clang tools for Windows` (the core is built with clang-cl,
not cl: see docs/PORTING.md), vcpkg with `VCPKG_INSTALLATION_ROOT` pointing at
it, Git for Windows for the bash that runs `bootstrap.sh`, cmake, ninja and node.
A machine missing any of them is told all of them at once.

Enrolling a machine does not move the job to it. That is a separate step:

```
gh variable set WINDOWS_RUNNER --body basalt-windows   # use it
gh variable delete WINDOWS_RUNNER                      # go back to GitHub's image
```

Two decisions rather than one, because **a self-hosted runner that is switched
off does not fail a job: it queues it**, for the job's six-hour timeout, and a
run that never reports looks exactly like a run that is still going. Deleting one
variable is a fix that can be made from a phone; taking a label out of a workflow
and pushing it is not.

What it buys is that ccache, vcpkg's binaries and the vendored `third_party`
survive on local disk between runs rather than being restored from a cache, so a
warm run is minutes rather than a quarter of an hour. What it costs is that the
machine's state is now part of the build: a green run on a machine with something
left over from last month is not the same evidence as a green run on a fresh
image. When a Windows failure looks impossible, unset the variable and let
GitHub's image answer.

## Sharding

`--shard I/N` runs the Ith of N shards of the end-to-end suite, and
`BASALT_COMPARE_SHARD=I/N` does the same for `compare_all.sh`. CI runs three of
each, on the same index, so a shard does a third of both.

Why three, what each job actually costs, and the ccache fault that hid inside
those numbers are recorded in
[docs/ci-performance.md](https://github.com/vincentvella/basalt/blob/main/docs/ci-performance.md),
which is in the repository but not on the site. None of it is needed to run a
shard.

## Test instruments, and what they cannot reach

**Dragging *out* has no automated coverage at all**, and cannot have: once a
drag begins the system owns it, and no instrument can put a file manager on the
other end to receive the drop. What the suite does prove is that a drag source
is *inert* when no view is marked: every press, scroll and context-menu
scenario passes with one attached, which is worth knowing and is not the same
as proving a drag works. The verification is `e2e/drop.js`'s green row and a
person dragging it somewhere.

**An instrument goes on all three hosts, or the scenario that uses it skips
where it is missing.** Written down because it has been got wrong twice, the
same way both times: `BASALT_TEST_QUIT` and then `BASALT_TEST_DROP` were added
to GTK and AppKit, the scenario was written against those two, and Windows
failed for a reason that had nothing to do with what the scenario tests. The
failure is especially misleading: "the inner target was not told" reads as a
broken feature rather than a missing instrument, and the feature was fine.

`BASALT_TEST_DROP` takes `x,y:path` and reports a file dropped at that point.
It enters below the toolkit, the way `BASALT_TEST_TAP` does and for the same
reason: a real drag needs a source outside the process, and no test can conjure
one. So what it exercises is the hit test, the walk up to the nearest marked
ancestor, the accept check, the event and React's half; every part that could
be wrong about *which view* is told and *what* it is told, and not
`GtkDropTarget` or `NSDraggingDestination` themselves. Worth stating, because a
scenario that looks like it drives a real drag and does not is worse than one
that admits it.

`BASALT_TEST_QUIT` is the same idea one level up: it asks the *application* to
quit the way a person does (Cmd-Q on macOS, the session ending on Linux) so a quit an app refuses can be driven from a script. A count rather than a
flag, because one ask cannot show both halves: a refusal is proved by the
process still being there afterwards, and the agreement that follows is proved
by it going before its own timer.

That the harness's shutdown is exempt is not a detail. `BASALT_QUIT_AFTER_MS`
quits AppKit by calling `terminate:`, which arrives at the very handler an app
uses to refuse, so the demo for this feature would have refused the harness,
and every scenario running it would have hung until its own timeout and
reported something other than what it was testing. The same trap as the
Windows one above, two years of API apart. GTK and Win32 need no exemption:
their session signals are not on the path `g_application_quit` and
`PostQuitMessage` take.

`BASALT_QUIT_WHEN_SETTLED=<ms>` ends a run when there is nothing left to wait
for, rather than on a budget chosen before the process started.

**No scenario opts into it today, and the reason is worth reading before
re-enabling any.** A settle waits for the first mount and for scripted input to
be *delivered*. It cannot wait for the round trip to JavaScript, and most
scenarios assert on what JavaScript logged, so the host quits while the
evidence is still in flight. `Alert.alert` failed on CI with "the alert's
callback never reached JavaScript"; the hover scenario failed locally with a
truncated event list. The opt-in list was built by opting everything in, running
the suite and keeping what passed, which is unsound: passing four times does not
establish the absence of a race. It was trimmed twice before the method rather
than the entries was accepted as the problem, and the suite went back to 445
seconds from 298. Those 147 seconds were not real.

**The right shape is per scenario, and the tool already exists:** wait for the
line the scenario is about to assert on, then write `BASALT_TEST_QUIT_FILE`. That
is what `stop_host` does for Fast Refresh. It is correct rather than
probabilistic, and it is *faster* than a settle, because it ends at the evidence
rather than at a fixed delay after input. **Settled** means
the first mount has been applied and any scripted input has been delivered, plus
a short settle for React's next commit. `BASALT_QUIT_AFTER_MS` stays as the
backstop, so nothing can get slower and a host that never settles still ends.

Measured on the whole macOS suite: **445s to 210s**, twice, to the second. The
developer menu scenario went from 39.5s to 8.2s, `test_controls` from 43.1s, the
application menu from 7.6s to 1.0s.

The first mount is recorded in `MountingWalk::applyMutations`, which is the one
place all three hosts mount through, so this is one code site rather than
three, for the reason `core/TestQuitFile.h` gives about shared spellings. Only
the *first* mount counts: taking the latest would push the settle out on every
commit, and an app that re-renders steadily would never settle at all.

**The first version saved nothing on the scenarios it was written for.** It
measured 2.96s against a 3s budget, because the hosts report their scripted
schedule whether or not any input was asked for, and that total is never zero,
it starts at 1500ms so a tap has a window to land on. Core ignores an empty
schedule now, which is what took the no-input case to 1.4s. Deciding whether a
schedule is real needs a list of the instruments that make one, and that list
must be *scheduled* instruments: `BASALT_TEST_MENU` and `BASALT_TEST_DIALOG` look
like they belong and do not, because they answer something when it appears rather
than being laid out on a timer. That error was caught by the check in
`scripts/test_harness.py`, which holds the list against what the three host mains
actually read, in both directions.

**Which scenarios opt in is a list of scenarios that have been run, not of
scenarios that looked fine.** Every one was opted in at once and the suite run;
seven failed, and all seven for the same reason, work after the last input with
nothing scheduling it. An animation *is* time. A refusal followed by an agreement
is two events deliberately separated by time. A trace update that takes itself
down again is measuring a decay. A settle cannot stand in for a duration that is
the thing under test, and those keep their budgets. The list and each exclusion's
failure text live in `scripts/integration_test.py`.

**Off entirely in `real` input mode.** A settle accounts for what the host
scheduled, and real input is driven from outside the process: the harness waits
four seconds for a window and then moves the pointer with `xdotool`, having set
none of the `BASALT_TEST_*` variables. Nothing is requested, so the host settles
at the first mount and has quit before the click lands. Linux CI is the only
place that mode runs, which is why every local run on a Mac passed and all three
Linux shards failed on the first push. The rule underneath is the exclusion list's
rule one step further out: **a settle cannot stand in for a duration it cannot
see, and it cannot see another process.** So the saving is a macOS and Windows
saving; Linux CI keeps its budgets.

Guessing is only safe because getting it wrong fails loudly: everything opted in
asserts on the dumped tree or a logged line, so quitting early produces a missing
assertion rather than a quiet success. `BASALT_NO_SETTLE=1` turns the whole thing
off, which is how the saving was measured and how to tell "this scenario is
broken" from "this scenario needed longer than it was given".

The suite also prints each scenario's duration now. Its cost is the thing most
often being worked on here and was only ever visible as a total, which is why
every estimate in this area had been a guess.

`BASALT_TEST_KEY` presses a key: `"m;j;q;z+meta"`, presses separated by `;` and
modifiers by `+`. Parsed once in `core/TestKeys.h` rather than in three hosts,
because three parsers is three chances to disagree quietly about what `z+meta`
means: the wrong answer is a combination that matches nothing, which reads as a
shortcut that did not fire rather than as a bad instrument.

**It enters after the platform translation**, not before it, so what it does not
exercise is the `NSEvent`, keyval and `WM_KEYDOWN` mapping, which is the half
most likely to be wrong, and is unit-tested per host instead (twenty-seven tests
across the three). A scenario passing here says the registry, the focus path and
the round trip to JavaScript work. It says nothing about whether pressing the left
arrow produces `"ArrowLeft"`.

On all three hosts, which is the rule below rather than a coincidence.

`BASALT_TEST_QUIT_FILE` is how the harness ends a run at a moment it picks,
rather than on a budget fixed before launch. The host polls for the path and
shuts down as soon as it exists, through exactly the path `BASALT_QUIT_AFTER_MS`
takes, so the tree is dumped and the exit code is the app's. Existence is the
whole message; nothing is read from the file, which is why there is no race
between creating it and seeing it.

It exists because the portable way to end a host used to be a signal, and there
is no portable signal: Windows has no SIGTERM and `TerminateProcess` cannot be
handled. The scenario that needed it is Fast Refresh, whose length is not known
in advance: an edit reaches the window in a second on a warm laptop and in up
to two and a half minutes on a cold CI machine, so no fixed budget is both quick
and safe.

Polled at 250ms rather than watched, on the timer each host already runs for
`BASALT_QUIT_AFTER_MS`. A file watcher is three APIs with three failure modes on
network and virtual filesystems, and a quarter-second poll cannot fail to be
delivered. Measured end to end on GTK and AppKit: 0.24s from writing the file to
the process exiting 0 with its tree on disk.

The skip path asserts that quitting this way **wrote the widget tree**, not
merely that the process ended. Without that line CI proves the poll fires and
says nothing about the half the instrument was added for, since the only
scenario that reads the tree after a quit-file shutdown is the edit path, and
the edit path cannot run on a CI machine. It costs a stat and it is the only
place the whole chain is checked by a machine rather than by hand.

A host that does not answer within 30 seconds **fails the scenario** rather than
being quietly killed. It was quietly killed at first, and the consequence was
immediate: the first Windows run reported green while proving nothing, because a
dead poll and a working one are indistinguishable when the fallback is silent.
Verifying it fired meant grepping a CI log for the shared line, and the grep
matched a compiler diagnostic quoting the header rather than a host logging it.
So the check moved into the harness, where it cannot be forgotten.

Not refusable, for the reason the timed quit is not, see below. The variable
name, the poll interval and the log line all live in `core/TestQuitFile.h` so the
three hosts cannot disagree about them; `BASALT_QUIT_AFTER_MS elapsed; quitting`
had to be retrofitted onto Windows after a scenario asserted on a string only two
hosts printed, and `scripts/test_quit_file.cpp` asserts the shared spellings
rather than trusting them.

`BASALT_DUMP_TREE` writes every window, each under a `--- window <n> ---`
header, for the same reason it already appended the error inspector: they are
separate trees on screen, and nesting one inside another would say something
untrue.

A menu *bar* cannot be reached by any of them, because a menu cannot be opened
without a person. `BASALT_DUMP_MENU` writes the menu the platform actually
installed instead, read back from AppKit or from the HMENU rather than from
the description that was sent, so it says a `<Menu>` became a real menu with the
shortcuts the platform attached to its roles. It is written even when it is
empty, because "this platform has no menu bar" is what a test on Linux asserts.

The wheel has its own: `BASALT_TEST_SCROLL` takes `"x,y,lines"` triples
separated by `;`, a wheel over a point, in surface-root coordinates, positive
lines scrolling down, which is the direction `contentOffset` reads. Each host
enters it at a different depth, deliberately: Windows sends a real
`WM_MOUSEWHEEL` through the real window procedure, macOS builds a real `NSEvent`
and hands it to the view its own hit test found, and Linux enters at the scroll
manager where `GtkEventControllerScroll` would. It is also the only way to reach
`<RefreshControl>`, whose gesture on a desktop is a wheel that keeps asking to go
up after the list has already reached its top.

A modal dialog is the one thing no instrument could reach and no person is
present for, so `BASALT_TEST_DIALOG` answers it instead of showing it: a button
index, or `dismiss` for the last button. It covers `Alert.alert` and the share
picker on every host, and it is the one place this project deliberately does not
drive the real thing: see `native/core/TestDialog.h`, which sets out why
(Apple's own `addUIInterruptionMonitor` is documented as not firing on recent
iOS versions, and reaching into the dialog through accessibility needs a
permission an automated run does not have).

What it skips is the presentation and nothing else. The request is built by the
same module from the same JavaScript, the answer travels back through the same
callback, and on the desktops whose share picker is built from a clipboard and a
mail client, a scripted "Copy" really copies, which is what the end-to-end
suite asserts on.

Without it, a run that opens a dialog hangs rather than failing: on macOS a
sheet that is up stops `[NSApp terminate:]` outright. That is a real bug rather
than a testing inconvenience (a person pressing Cmd-Q would see the same) so
the host now closes any open sheet before it tries to quit.

That cuts the other way too, and it is worth knowing before a tree looks wrong:
a window that opens under someone's cursor *is* hovered, before either host has
drawn anything. `e2e/hover.js` is left out of `scripts/compare_all.sh` for that
reason, and the end-to-end suite asserts the order events arrive in rather than
that nothing else happens.

## Expo, and the apps that need it

Two of the apps in `e2e/` import an Expo package, `notifications.js` imports
`expo-notifications`, and this directory is not an npm package, so there is
nothing for Metro to resolve them against. Both halves have to be pointed at an
app that does have them installed:

```bash
cmake -B build -DBASALT_EXPO_MODULES_CORE=/path/to/app/node_modules/expo-modules-core
BASALT_EXPO_APP=/path/to/app scripts/bundle.sh --entry notifications.js --out notifications.jsbundle
BASALT_EXPO_APP=/path/to/app scripts/integration_test.py
```

The same app for both, so that Expo's C++ and its JavaScript are the same
version, the same reason the host builds against the app's React Native.
Without them the host has no Expo at all and the scenario skips saying so, which
is what a plain checkout and every CI job do.

## Notifications, and the daemon that is never there

`GtkNotifications.cpp` sends `Notify` over the session bus, and neither a
developer's Mac nor a CI runner has a desktop's notification daemon listening
for it. So the suite brings its own: `basalt_notification_stub` owns
`org.freedesktop.Notifications`, answers `Notify` with an id and prints what it
was asked to show, and `scripts/integration_test.py` starts it on a session bus
of its own for the length of one scenario.

`dbus-daemon` directly rather than `dbus-run-session`: on macOS the latter
insists on launchd's socket and fails with "DBUS_LAUNCHD_SESSION_BUS_SOCKET is
empty". `brew install dbus` is enough on a Mac; a Linux runner has it already.

Without `dbus-daemon` the scenario still runs and asserts the other half: that
the host reports why it cannot send, which is also what macOS and Windows
report on every machine, daemon or not.

## Running on Linux

The project targets Linux and is developed on a Mac, so everything here should
be checked on Linux before it is believed. A VM is enough:

```bash
brew install lima
limactl start --name=basalt <a template with vz, 8 cpus, 16GiB, 120GiB>
limactl shell basalt
```

Apple Virtualization rather than QEMU: the guest is aarch64 like the host, so it
runs at native speed. Building React Native's core and Hermes under emulation
would take hours.

Inside, on Ubuntu 24.04:

```bash
sudo apt install build-essential clang cmake ninja-build pkg-config git curl \
  libgtk-4-dev libpango1.0-dev libglib2.0-dev libgoogle-glog-dev \
  libboost-dev libboost-regex-dev libfmt-dev libdouble-conversion-dev \
  libgflags-dev libssl-dev libcurl4-openssl-dev
# Node 24: React Native needs ^22.13 || ^24.3 || >= 26, and 24.04 ships 18.
curl -fsSL https://deb.nodesource.com/setup_24.x | sudo -E bash - && sudo apt install nodejs
```

Then the usual bootstrap, cmake, build.

### Rendering, on Wayland

A real compositor, headless, with no GPU:

```bash
sudo apt install sway grim mesa-vulkan-drivers libgl1-mesa-dri
printf 'output HEADLESS-1 resolution 1400x1000\ndefault_border none\n' > /tmp/sway.conf
XDG_RUNTIME_DIR=/run/user/$(id -u) WLR_BACKENDS=headless WLR_RENDERER=pixman \
  sway -c /tmp/sway.conf &
WAYLAND_DISPLAY=wayland-1 GSK_RENDERER=gl ./build/basalt_gtk build/main.jsbundle.js
grim shot.png
```

Two renderer choices, for two different layers, and they are not the same one.

`WLR_RENDERER=pixman` is the *compositor's*. Without a GPU, wlroots' default
fails with `drmGetDevices2 failed` and sway starts but draws nothing.

`GSK_RENDERER=gl` is *GTK's*, and it must not be `cairo`. GTK 4.14's cairo
renderer draws a transformed widget subtree unrotated and in the wrong colour;
the demo's rotated card comes out as a flat pink square. The GL renderer gets it
right, and works headless through Mesa's software rasteriser, so there is no
reason to reach for cairo. This cost an hour of suspecting the transform code,
which is fine on macOS and fine here under GL.

### Input, on X11

Real pointer events need a seat with input devices, which a headless Wayland
compositor does not have: sway sees no inputs, and `swaymsg seat … cursor`
silently does nothing. Getting one means logind, a session and `uinput`, which
is a lot of machinery for one assertion. X11 gives the same guarantee for far
less:

```bash
sudo apt install xvfb xdotool
Xvfb :99 -screen 0 1400x1000x24 &
DISPLAY=:99 GDK_BACKEND=x11 GSK_RENDERER=cairo scripts/integration_test.py
```

That is a genuine event through the X server and GDK. The wheel path checks out
too: five `xdotool click 5` notches move the content exactly 265 points, which
is the 53-point step in `GtkScrollViewManager` times five.

## What is still not covered

**Wayland input.** Rendering is verified on Wayland and input on X11, but not
both at once. Closing it needs a compositor with a real seat: a desktop session
in the VM, or a physical Linux machine.

**Rendering.** Nothing asserts on pixels, which is how GTK's cairo renderer
mangled every transform in the demo without a single test noticing. The widget tree says a view has a
background colour and a frame, not that the right pixels reached the screen. A
screenshot comparison would catch paint bugs the tree cannot: wrong z-order,
a missing clip, text drawn in the wrong colour, but it needs a reference image
per machine, and font rendering differs enough between them that the references
would not travel. Reasonable to add on one fixed machine, not as a portable
suite.

**Threading.** The mounting manager asserts it is on the main thread, and text
measurement takes a mutex, but nothing exercises the JS thread and the main
thread concurrently.

**Nothing, as of the day this repository went public.** All three hosts are
built and tested on every push: Linux and Windows have been for a while, and
macOS joined when standard runners became free and the ten-times billing
multiplier stopped being a reason to leave it out. The macOS job also runs
`compare_all.sh`, which no other runner can, because no other has both hosts
installed.

This paragraph used to say "CI is Linux only ... macOS is covered by whoever
is working; it is not covered by a machine", which was already wrong about
Windows and stopped being true of macOS on the same day. Left here as the
answer to the obvious question rather than deleted.

## CI

`.github/workflows/ci.yml`. Installs the dependencies, builds, and runs both
suites under Xvfb with `xdotool` present, so the end-to-end suite is in `real`
input mode.

It builds against a **pinned** React Native commit, in `scripts/react-native.pin`.
This project tracks `main`, which is right for development and wrong for CI: a
build that follows `main` turns every upstream change into a red tick on an
unrelated pull request. A separate `drift` job builds against `main` weekly and
is allowed to fail, so upstream churn is noticed without blocking anything.

Three caches carry the cost: React Native's `node_modules` and `third_party/`
(which holds the Hermes build) are keyed on the pin, and `ccache` on the commit
with a rolling fallback.

They are the difference between a **42 minute** run and a **2 minute** one.
Cold, a run installs React Native's dependencies (949MB of `node_modules`),
builds Hermes, and compiles 400-odd objects of React Native core. Warm, it
restores 304MB of `third_party`, hits ccache, and goes straight to the tests.

Two things about those caches that are easy to lose an hour to:

- **The `third_party` key hashes the whole of `bootstrap.sh`**, so editing a
  comment in it costs a Hermes rebuild. That is deliberate: over-invalidating is
  the safe direction, and the script changes rarely now.
- **A cache saved on a branch is invisible to `main`.** GitHub scopes caches to
  the branch that created them, and shares them only downwards, to that branch's
  descendants. So the first run after merging a branch is cold all over again,
  and only then do the caches land somewhere every later run can reach.
- **`actions/cache` will not save a path that escapes the workspace.** It says
  nothing about it either; the cache simply never appears. React Native is
  therefore cloned to `react-native-src/` *inside* the checkout rather than
  beside it. Getting this wrong is quiet until the day `third_party` hits: then
  bootstrap skips `yarn install` because codegen is already there, and bundling
  fails for want of `node_modules`.
