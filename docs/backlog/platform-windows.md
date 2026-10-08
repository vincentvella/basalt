# Windows, the platform

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (10):**

1. The Hermes patch is applied by hand and nothing reapplies it
2. React Native's own warnings are not enforced on Windows
3. `BASALT_SNAPSHOT` cannot see a `<TextInput>` on Windows
4. A `<TextInput>` on Windows is always on top of everything
5. The Windows choreographer is a 16ms timer
6. ~~`scripts/integration_test.py` skips Fast Refresh on Windows~~; what is
   left of it is an open entry in testing.md
7. Nothing makes a red build hard to ignore
8. An inline `<View>` inside a `<Text>` is not positioned
9. Five style props the other two hosts draw and this one ignores
10. `accessibilityLabelledBy` sets no relation
11. `hitSlop` is not part of the hit test

Since phase 47 Windows is a peer rather than a port in progress. It mounts every
component the other two desktops do: `<View>`, `<Text>`, `<Image>`,
`<ScrollView>` and `<TextInput>`, and Hermes evaluates a bundle, Fabric diffs
a shadow tree, Direct2D paints it in an HWND, a click on a `<Pressable>` runs
its `onPress`, a wheel scrolls a list, and a character typed into a field
reaches React and comes back. It runs the same end-to-end suite, it has a
`run-windows`, and `compare_hosts.sh` knows how to run it.

Struck-through entries below are what that took, kept because the order they
were done in is the useful part. What is not struck through is real and named,
and none of it is a missing half.

- ~~**No mounting manager.**~~ Phase 42. Phase 19 had already done the expensive
  part: the mutation walk is portable, so what Windows owed was seven operations
  and a props translation, and `core/MountingWalk.h` was not modified.
- ~~**React Native's C++ core has never been compiled with MSVC.**~~ It has, in
  phase 41: 442 objects, `basalt_core.lib`, every translation unit in
  ReactCommon, ReactCxxPlatform, folly and Yoga under clang-cl. What is left of
  it is below.
- ~~**Hermes does not build from source on Windows.**~~ It does, with three
  fixes, and it had to: the prebuilt everyone else consumes cannot be used here.
  `Microsoft.JavaScript.Hermes` ships jsi headers with no `IRuntime` at all and
  React Native 0.87's have 186 references to it, so every JSI symbol mangles
  differently. That is structural rather than bad luck: react-native-windows
  targets React Native 0.84 and react-native-macos 0.81, and this is on 0.87, so
  nobody has built the prebuilt yet. `supported-versions.json` says Windows is a
  platform with a prebuilt Hermes; that is true in general and false for any
  React Native newer than the forks, and it should say so.
- **The Hermes patch is applied by hand and nothing reapplies it.**
  `HERMES_EMPTY_BASES` has to be added to `VM::Environment` in
  `include/hermes/VM/Callable.h`, and `third_party/hermes` is a downloaded
  tarball that `bootstrap.sh --force` deletes. Until bootstrap applies it, a
  fresh checkout gets a Hermes that fails on a static assertion. Upstream should
  take this: the macro is defined in `Support/Compiler.h` for exactly this
  purpose, with a comment naming `HERMESVM_CONTIGUOUS_HEAP`, and is applied to
  nothing in the entire tree.
- **React Native's own warnings are not enforced on Windows.** Its CMake applies
  `-Wall -Werror -Wpedantic`, all of which clang-cl either misreads or takes
  from an INTERFACE property that lands after anything that could counteract
  them, so phase 41 strips them. Linux stays the build that guards upstream
  warning cleanliness. Restoring them properly means `/clang:-Wall` or an
  equivalent that survives the link graph, and has not been attempted.
- ~~**`native/bootstrap.sh` has no Windows path.**~~ It has one, and it was run
  end to end: it detects Git Bash, finds a vcpkg that actually has the packages,
  patches Hermes, builds it with the three Windows flags, and prints a configure
  line that works. What it does **not** do is install anything: vcpkg and its
  packages are still a manual prerequisite, and so is a Node new enough for
  React Native 0.87, which wants `^22.13 || ^24.3 || >= 26` and is not what a
  machine is likely to have.
- ~~**Nothing has run bootstrap from an empty `third_party`** on Windows.~~
  Done while writing the full Windows CI job, which would have been the first
  thing to do it, and the entry was right to worry. Every earlier bootstrap
  had found Hermes already built by hand, so its configure step had never run,
  and it failed on its first real execution: Git Bash's MSYS runtime rewrites
  an argument that looks like an absolute POSIX path, `/DWIN32` looks exactly
  like one, and clang-cl was handed `C:/Program Files/Git/DWIN32`. The fix
  excludes that one argument from conversion rather than all of them, because
  the `-S` and `-B` paths beside it are only correct *because* of the same
  conversion; turning it off wholesale was the first attempt and broke those
  instead. From empty it now fetches, patches and builds Hermes, installs yarn
  and runs codegen with no errors.
- ~~**`<Text>`**~~ (phase 40, over DirectWrite) and ~~**`<Image>`**~~ (phase 40,
  over WIC, with the fetch shared from `core/ImageBytes.cpp`).
- ~~**Accessibility.**~~ Phase 40, over UI Automation: the one place Windows
  is structurally different rather than differently spelled, since UIA wants a
  provider object where GTK and AppKit take properties on a view. What is left
  of it is `IRawElementProviderFragment` and the control patterns, both of which
  need the HWND fragment root and so belong with a later phase of the host.
- ~~**No input at all.**~~ Phase 44. `Win32TouchDispatcher` turns
  `WM_LBUTTONDOWN`/`WM_MOUSEMOVE`/`WM_LBUTTONUP` into React Native touches, and
  `e2e/press.tsx` counts presses on Windows. What is still missing is what a mouse
  has and a finger does not: **no right button**; **no keyboard**, which arrives
  with `<TextInput>` because there is nothing yet that focus could belong to. The
  wheel is not among them any more: phase 45 took it, and routed it through
  the mounting manager rather than the touch dispatcher, because what it needs
  is the list of tags that are ScrollViews.
- ~~**`offsetPoint` is page coordinates on all three platforms.**~~ Fixed on
  all three; see `docs/backlog/input.md` for how. Windows walks up from the
  target composing `localToParent` and inverts the chain, rather than building
  the gesture hit chain on every press as this entry proposed: the walk is
  only as deep as the view, and it is the same matrix `hitTest` inverts coming
  down, which is the property worth having.
- ~~**No `<ScrollView>` on Windows.**~~ Phase 45. What it does not have is
  scrollbars, momentum, and a precision touchpad, a Windows touchpad reports
  through `WM_POINTER*` rather than `WM_MOUSEWHEEL`, so a two-finger scroll
  arrives as notches like a wheel rather than as pixels, which neither other
  desktop confuses. `SPI_GETWHEELSCROLLLINES` is ignored on purpose so that one
  notch moves the same distance on all three.
- ~~**No `<TextInput>` on Windows.**~~ Phase 46, over a real `EDIT` control.
  Windows now mounts all five components the other two desktops do. What it
  does not have is multiline, `onKeyPress`, `onSelectionChange`, a tab order, or
  the colours the cue banner and the selection take from the system, and a
  field with no `backgroundColor` paints the system window colour, because a
  child window cannot see through itself to what Direct2D drew behind it.
- **`BASALT_SNAPSHOT` cannot see a `<TextInput>` on Windows.** It renders the
  `RnWin32View` tree offscreen and a text field's peer is a child window, not a
  view, so a field comes out as its background with no text, no placeholder
  and no caret. Not a bug in the snapshot, but it does mean the one host that
  can assert on pixels cannot assert on the one component whose appearance is
  hardest to get right. `PrintWindow` on the live window is the answer and
  nothing in the repository does it yet.
- **A `<TextInput>` on Windows is always on top of everything.** Its peer is a
  child window, so a later sibling cannot cover it and it does not clip to a
  scrolled ancestor; it is hidden when it leaves the ancestor's box instead,
  which looks right until something is half-scrolled. A transform on the view
  does not reach it either: a window cannot be rotated.
- **The Windows choreographer is a 16ms timer**, not a display link. Doing it
  properly means `DwmGetCompositionTimingInfo` and `DwmFlush` on a thread of its
  own, because `DwmFlush` blocks and a blocked UI thread is worse than a
  slightly wrong interval. The same gap already recorded for worklets and
  Reanimated on both other desktops.
- ~~**Text colour is per-paragraph, not per-run.**~~ Phase 47. DirectWrite
  carries colour as a drawing effect rather than as a range attribute, which
  looks like it needs a custom `IDWriteTextRenderer`, but Direct2D's own
  renderer has exactly one special case, and a drawing effect that is an
  `ID2D1Brush` is it.
- ~~**No `react-native run-windows`.**~~ Phase 47, and `run-macos` with it. All
  three are one function in `basalt-core/cli/desktop.js` with four
  strings passed in, which is the same split the C++ half makes.
- ~~**CI has no Windows runner.**~~ Added in phase 39, and small: the view layer
  builds with MSBuild and needs no display, no React Native and no bootstrap.
- ~~**No machine has run `scripts/compare_hosts.sh` with Windows in it.**~~ It
  has, once WSL2 worked: the GTK host built in Ubuntu 24.04 by
  `scripts/wsl_setup.sh`, beside the Windows host, and `compare_all.sh` says the
  two agree on all twelve apps. Getting there found two things, neither of them
  in a view layer. The distro had nothing registered for `https`, so Linux
  honestly failed a `canOpenURL` check a GitHub runner passes, an environment
  gap, now covered by the setup script. And Windows failed a clipboard check in
  2 runs of 15, because a refused `OpenClipboard` silently dropped the write.
  It retries now, with a test that holds the clipboard from a window and fails
  without the retry; what was holding it was never caught, and 190 runs with
  logging on every refusal saw none.
- ~~**`scripts/integration_test.py` skips Fast Refresh on Windows**~~, because
  `scripts/metro.sh` is a shell script. Both skips have gone: `scripts/metro.js`
  starts the packager in Node, on any platform, and `metro.sh` now delegates to
  it so there is one implementation rather than two that resolve `RN_DIR`
  differently. It serves in-process rather than spawning `metro serve`, which is
  what makes it stoppable on Windows; terminating a process there does not
  terminate its children, so a wrapper would have left the packager behind.
  Fast Refresh already worked on Windows: an edit to a running Expo app
  reaches the tree, after three separate fixes: the host read its Metro entry
  from an argument nothing passed, React Native's `start` command needs
  `@react-native/metro-config` in the app, and every host reported a dev script
  URL naming `linux`, so Metro sent Windows HMR updates for a Linux graph. What
  is still off in CI is the *edit*, through `BASALT_SKIP_FAST_REFRESH` on all
  three jobs, for a reason that is Metro's file watching rather than Windows'.
  Everything before the edit runs on Windows now, including the developer menu
  scenario, which skipped here for the same shell-script reason.
- ~~**CI does not build the Windows host.**~~ It does, as `windows-full`:
  vcpkg, Hermes from source, React Native's core under clang-cl, every Windows
  test, the demo bundle and the end-to-end suite. Green on its first run, and
  51 minutes cold, vcpkg 20, Hermes 13, the build 17. What that cost is
  what the caching around it is for: every cache is now saved by its own step
  as soon as it exists rather than when the whole job succeeds, the Windows
  build goes through ccache, and a change to `bootstrap.sh` no longer throws
  away Hermes. Pushes that touch only prose start no jobs at all.
- ~~**CI had been red for twenty-three commits.**~~ Fixed in phase 47, and worth
  keeping written down. The last green run was "Blob, File and FileReader"; the
  Linux job failed on every commit after it, including all nine Windows phases,
  and nobody looked. Both causes were invisible from a Windows desk: a static
  archive cycle only GNU ld minds, and a header reaching `<cstdint>` through
  windows.h, which is the argument for `scripts/check_includes.js` and for
  reading the log rather than the tick.

- **Nothing makes a red build hard to ignore.** No branch protection, no
  required check, no notification anyone reads. Thirteen commits of a silently
  failing Linux job is what that cost once, and none of the work above changes
  it.

- **An inline `<View>` inside a `<Text>` is not positioned.** GTK and AppKit
  reserve a box for the attachment and report where it landed; DirectWrite is
  handed the same fragments and answers with a zero frame for each, so a view
  inside a sentence lands at the origin with no size. The end-to-end scenario is
  skipped here by name, and [text.md](text.md) has what the fix looks like:
  `SetInlineObject` with an `IDWriteInlineObject` that answers React Native's own
  metrics, read back with `HitTestTextPosition`.

- **Five style props the other two hosts draw and this one ignores**, all five
  landed on GTK and AppKit on 2026-10-07 and 2026-10-08, and all five recorded
  here the day the second host got them rather than later:

  - `blurRadius` on an `<Image>`. GSK has a blur node and Core Image a filter;
    Direct2D has `ID2D1Effect` with `CLSID_D2D1GaussianBlur`, which is the same
    shape of work as the AppKit half. Half the radius is the sigma on both other
    hosts, measured rather than chosen; see backlog/image.md so this one does not
    have to measure it again.
  - `borderStyle`, dotted and dashed. Both others stroke one path around the
    rounded rectangle with a dash pattern scaled to the width and let that replace
    the four filled edges. `ID2D1StrokeStyle` takes a dash array, so the same
    decision carries over, including that the first side which asks for something
    other than solid decides the whole outline.
  - `boxShadow`, outset and inset, any number of them. GSK has a shadow node per
    kind and macOS has CALayer's shadow properties; Direct2D has
    `CLSID_D2D1Shadow`, which takes a blurred alpha mask of what is drawn, so the
    work is the geometry rather than the blur. backlog/correctness.md has what the
    other two decided, including that a negative blur radius has to be clamped.
  - `backgroundImage`, as far as linear gradients. The hard half is done and is
    shared: `core/Gradients.h` resolves the angle and the colour stops and hands
    back two points and a ramp, so this host needs
    `ID2D1LinearGradientBrush` over the same numbers and nothing else.
  - The `cursor` style property. This is the one that differs in shape: Win32 has
    no per-view cursor, so `WM_SETCURSOR` has to be answered by the window with
    whatever view is under the pointer, which means a hit test on every cursor
    query rather than a property on a widget. `LoadCursor` with the `IDC_` family
    covers most of CSS's keywords.

  Each has a unit test per host to copy the assertions from, and an end-to-end
  scenario that is skipped on Windows by name. One consequence worth knowing
  before running it: `scripts/compare_hosts.sh` against Windows will now report
  the `blur=`, `border-style=`, `cursor=`, `shadow=` and `gradient=` lines as a
  tree difference, because they are one. That is the script doing its job, and it goes away as each lands.

- **`accessibilityLabelledBy` sets no relation.** The hard half is done and is
  shared: `core/LabelRegistry.h` resolves a `nativeID` to a tag and says when,
  which is the part that is easy to get wrong -- a field is mounted before the
  caption that names it. What is left here is one call. UI Automation has
  `UIA_LabeledByPropertyId`, so the Win32 host's provider answers it with the
  resolved view's provider; GTK sets an AT-SPI relation and macOS an
  `accessibilityTitleUIElement`. The end-to-end scenario is skipped here by name.

- **`hitSlop` is not part of the hit test.** The other two hosts grow the box
  their picking accepts -- GTK by widening `contains`, AppKit by widening the
  rect at the top of its own walk -- so a 24pt target can answer for 56pt.
  `RnWin32View`'s `hitTest` walks `childrenInPaintOrder` comparing against each
  child's frame, which is the same shape, so this is the same one-line change
  against the insets the props already carry. The end-to-end scenario taps twice
  in one run and is skipped here by name; when it is unskipped it will say which
  half is missing, because it reads `hit-slop=` out of the tree before it taps
  anything.
