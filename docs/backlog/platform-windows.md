# Windows, the platform

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (12):**

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
11. ~~`hitSlop` is not part of the hit test~~
12. No accessibility announcements, so `accessibilityLiveRegion` is silent
13. ~~`filter` is not applied~~
14. ~~`textTransform` is ignored, so an uppercase label is not uppercase~~
15. ~~The `outline` family is not drawn~~
16. `mixBlendMode` blends nothing
17. ~~No text shadow, so `textShadowColor` and friends do nothing~~
18. The desktop's text scale is not read, so large text does not enlarge text
19. ~~No text decoration~~, except the styles DirectWrite has no form of
20. ~~`fontVariant` and a fragment's `opacity` are not read~~
21. ~~`writingDirection` is not read~~, and `start` and `end` are relative here
22. ~~An animated GIF is painted as a still~~

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

  - ~~`blurRadius` on an `<Image>`.~~ Done 2026-10-09, with
    `CLSID_D2D1GaussianBlur` over the device context the text shadow proved
    reachable. Half the radius is the standard deviation, which is what the
    other two hosts measured rather than chose, so nothing was measured again.

    **In the view's coordinates rather than the image's pixels**, which is the
    same rule and got the same treatment: the image is drawn into a bitmap the
    size of its box by this function calling itself with no radius, and the blur
    is applied to that. So the fit, the tint and the tiling are already
    accounted for, because the recursive call does all three. Clipped to the
    box, as both other hosts clip theirs, since a blur spreads beyond its input
    and an `<Image>` never paints outside its own frame.

    Five tests. The ramp between the two halves of the two-tone image is the
    instrument, as it is on AppKit: a hard edge is a column or two of
    antialiasing and a twelve point blur is several times that. One of them
    renders the same image into two box sizes with the same radius and asserts
    the ramp is the same width, which is what says the blur is in view
    coordinates rather than source pixels.
  - ~~`borderStyle`, dotted and dashed.~~ Done 2026-10-09, the same way and with
    the same decisions: one stroked path around the rounded box replacing the
    four filled edges, the first side that asks for something other than solid
    deciding the whole border, the path inset by half the width because a stroke
    straddles it, and the style read from the *resolved* metrics rather than from
    `props->borderStyles`, which is the mistake that made every dashed border in
    a React app draw solid on the other two hosts. It shares the dash pattern
    with the outline, which landed the same day, so Direct2D's dash lengths are
    converted into multiples of the stroke width in one place. Three tests: ink
    along the top with gaps, dots gapping more than dashes, nothing painted
    outside the frame, and the dump.
  - `boxShadow`, outset and inset, any number of them. GSK has a shadow node per
    kind and macOS has CALayer's shadow properties; Direct2D has
    `CLSID_D2D1Shadow`, which takes a blurred alpha mask of what is drawn, so the
    work is the geometry rather than the blur. backlog/correctness.md has what the
    other two decided, including that a negative blur radius has to be clamped.
  - `backgroundImage`, linear and radial gradients both, with
    `backgroundSize`, `backgroundPosition` and `backgroundRepeat`. The hard half
    is done and is shared: `core/Gradients.h` resolves the angle or the ending
    shape and the colour stops, and `core/BackgroundLayers.h` resolves where the
    image goes and the tile it repeats in. So this host needs
    `ID2D1LinearGradientBrush` over the first and `CreateRadialGradientBrush`
    over the second, whose `D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES` takes a centre
    and two radii directly -- no coordinate scaling, which is what the AppKit
    half needed -- and then either `ID2D1BitmapBrush`'s extend modes or the same
    per-tile loop AppKit uses, each tile clipped to the image's rectangle.
    One list rather than two, `background-image` being one list that paints the
    first on top, and the positioning area is the padding box where the painting
    area is the border box.
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

- ~~**`hitSlop` is not part of the hit test.**~~ Done, 2026-10-09, and it was
  the one-line change this entry predicted: `hitTest`'s bounds check takes the
  insets, and nothing else reads them. Each host widens one view's own test
  rather than the walk that reached it, which is what keeps a slop that reaches
  outside its parent unreachable there, the same bound iOS has.

  Five tests, the same four questions tests/test_hittest.cpp asks of GTK plus
  the dump: a slop that grows the target, each edge applied where it was asked
  for rather than symmetrically, a slop taken away again, and a slop that does
  not win over a sibling drawn on top of it. The end-to-end scenario, which taps
  twice in one run, now runs on all three.

- **No accessibility announcements, so `accessibilityLiveRegion` is silent.** The
  change detection is done and shared -- `core/LiveRegions.h` decides what counts
  as news, and the only two rules that matter are already written down there --
  so what is left here is the announcement itself. UI Automation raises it as a
  `UIA_SystemAlertEventId` on the provider, or as a live-region property change
  with `UIA_LiveSettingPropertyId` set; GTK calls `gtk_accessible_announce` and
  macOS posts `NSAccessibilityAnnouncementRequested`. The host would also need to
  collect a region's text, which the other two do by walking the subtree for
  paragraphs. The end-to-end scenario is skipped here by name.

- ~~**`filter` is not applied.**~~ Done 2026-10-09, as the entry predicted: an
  effect graph over the view's own picture and no new arithmetic.
  `core/Filters.h` collapses the nine CSS functions to one colour matrix, one
  blur, one opacity and the drop shadows in order, and this host spends them on
  `CLSID_D2D1ColorMatrix`, `CLSID_D2D1GaussianBlur` and `CLSID_D2D1Shadow`.
  Nothing converted: Direct2D's shadow takes a standard deviation, as the
  AppKit one does, where GSK takes CSS's radius and the GTK half doubles it.

  **CSS applies a filter to an element and its descendants**, so `paint` is now
  split: `paintContents` draws this view and everything inside it with the
  target's transform already placing its origin at zero, and a filtered view
  draws that into a compatible bitmap and runs the graph over it. The split is
  worth having beyond this entry, since `mixBlendMode` needs the same picture.

  Two details that a test would otherwise have found later. Direct2D multiplies
  a *row* vector by its matrix where core's rows are outputs, so the matrix goes
  in transposed, which is why the unit test rotates the channels rather than
  swapping two: a symmetric matrix passes either way round, and a rotation comes
  out blue when it is right and green when it is not. And the matrix needs
  `D2D1_COLORMATRIX_ALPHA_MODE_STRAIGHT`, because CSS defines its filters on
  unpremultiplied colour while Direct2D defaults to premultiplied: the red
  channel of a half-transparent red pixel is 0.5 one way and 1.0 the other, and
  the test that pins it forces the alpha opaque and asks what the red came out
  as.

  **A child that overflows a filtered view is cropped to the view's box here**,
  because the offscreen bitmap is the view's own size. GSK blurs the overflow,
  its node tree carrying the subtree's real extent, which nothing in this host
  computes. Recorded rather than left to be discovered; closing it means asking
  the mounting manager for a subtree's union of frames, which nothing needs yet.

  Eight tests, and the two end-to-end scenarios now run on all three hosts: the
  one that reads `filter=(probe=#b4b4b4ff)` out of the tree, where a transposed
  matrix reports three different channels, and the one that reads the drop
  shadow's standard deviation back.

- ~~**`textTransform` is ignored, so an uppercase label is not uppercase.**~~
  Done, 2026-10-09, in `DirectWriteLayout.cpp` where the runs are built, which
  is before anything measures the string: the transformed text is a different
  width, so a transform applied later would wrap in the wrong place.

  `LCMapStringEx` with `LCMAP_UPPERCASE | LCMAP_LINGUISTIC_CASING`, through
  UTF-16 both ways, which is what that function takes and what DirectWrite
  counts its ranges in anyway. Its two-call form is there because a case mapping
  can change the length. `capitalize` follows React Native's own rule, the one
  the other two hosts already copied from `RCTAttributedTextUtils.mm`: split on
  single spaces, lowercase the word, uppercase its first character unless it is
  a digit, so "iOS" becomes "Ios".

  The end-to-end scenario now runs on all three.

  **The three hosts disagree about the German sharp s**, measured on CI on
  2026-10-09 because this is the only Windows the repository has.
  `LCMapStringEx` maps it to itself, so uppercasing "Strasse" written with it
  keeps the letter; GLib's `g_utf8_strup` and NSString's `uppercaseString` both
  answer "STRASSE", and so does every browser on Windows. The locale API does
  the simple case mapping and the other two do Unicode's full one, which is the
  one that may change a string's length.

  `win32_text_transform_keeps_the_sharp_s` asserts what Windows actually does
  rather than tolerating either answer, so the day this changes the test says
  so. What would close the gap is `u_strToUpper`, from the ICU that Windows 10
  1703 and later ship: it does the full mapping, it is in `icu.h` in the
  Windows SDK, and it can be reached with `GetProcAddress` on `icu.dll` rather
  than linking, which keeps a host that has needed no new dependency free of
  one. Whether a toolkit should correct its platform here is the decision, not
  the call: a German label reading STRAßE is wrong everywhere else an app
  runs.

- ~~**The `outline` family is not drawn.**~~ Done, 2026-10-09, with
  `DrawGeometry` rather than `DrawRoundedRectangle`: the geometry is the one
  `Win32Clip.h` already builds for the background and the border, so a ring
  around elliptical radii needs no special case, which is the same argument the
  other two hosts' stroked rings make.

  The clipping half this entry warned about came free. CSS does not clip an
  element's own outline for `overflow: hidden`, and `paintChildren` scopes its
  clip, so by the time the ring is drawn the clip is already popped. GTK gets it
  the same way; AppKit is the host that has to move the ring into the parent's
  layer.

  **The one thing that did need converting is the dash pattern.** Direct2D's
  dash lengths are multiples of the stroke width where the other two hosts'
  arrays are absolute, so {3w, 2w} becomes {3, 2}, and dots are a zero-length
  dash with round caps. Four times too long is a ring with no gaps at all, which
  is what `win32_paint_a_dashed_outline_is_not_solid` is for.

  Six tests: the five tests/test_gtk_paint.cpp asks, in the same order and with
  the same geometry so the two hosts' rings land in the same place, plus the
  dump. The end-to-end scenario now reads `outline=(3,2,#e0484dff,dashed)` on
  all three hosts.

- **`mixBlendMode` blends nothing.** The keyword reaches this host's props and
  nothing reads it. Direct2D has the arithmetic: `CLSID_D2D1Blend`'s
  `D2D1_BLEND_PROP_MODE` is CSS's list of modes plus a few extras of its own, so
  there is no matrix to write. What it needs is the backdrop as an input, the
  effect taking two bitmaps -- so the parent has to paint what is beneath a
  blended child into an intermediate `ID2D1BitmapRenderTarget` and feed that in,
  which is the same look-ahead the GTK half does for `gtk_snapshot_push_blend`.
  backlog/correctness.md has what the other two decided, including which
  backdrop each of them blends with and why the two differ. The end-to-end
  scenario is skipped here by name.

- ~~**`writingDirection` is not read, so a right-to-left paragraph starts on
  the left.**~~ Done, 2026-10-09, with
  `IDWriteTextFormat::SetReadingDirection`: it is a format property rather than
  a range one, which suits the prop, since `writingDirection` belongs to a
  paragraph and not to a span.

  **The second half cost both other hosts a first attempt, and cost this one
  nothing**, which is worth recording as a property of the engine rather than
  as cleverness here. Setting a direction gets the glyphs into the right order
  inside a line; which edge the line starts from is a separate question, and
  Pango only flips a natural alignment with `auto_dir` on while Core Text
  resolves one only inside a frame. DirectWrite's alignments are *relative*:
  LEADING is the right edge in a right-to-left paragraph, and LEADING is also
  the default, so a natural alignment follows the direction with no arithmetic
  at all.

  What that leaves is the opposite problem, which this entry's title did not
  see coming: the two *physical* alignments have to swap. `textAlign: 'left'`
  means the left edge in React Native and on the other two hosts, so under a
  right-to-left direction it maps to TRAILING here. `toDWriteAlignment` takes
  the direction for that reason.

  **And `start` and `end` stay relative on this host**, where the other two
  fold `end` into physical right. In a left-to-right paragraph the three agree;
  in a right-to-left one, `textAlign: 'end'` is the left edge here and the
  right edge there. That is DirectWrite having TRAILING and the other two
  engines not, so it is recorded in backlog/text.md against them rather than
  made worse here.

  Three pixel tests, the same instrument the alignment test next to them uses:
  a natural alignment that follows the direction, physical alignments that do
  not, and `end` that does.

- ~~**`fontVariant` and a fragment's `opacity` are not read.**~~ Both done,
  2026-10-09, and both were one call over arithmetic that was already shared.

  `fontVariant` is an `IDWriteTypography` per range, built from the tags
  `core/FontVariants.h` names and `DWRITE_MAKE_OPENTYPE_TAG`, released as soon
  as the layout has taken its reference. `opacity` is `buildTextStyle` taking
  its colour from `textForegroundColor()` instead of reading the prop, which
  also picked up React Native's default of opaque black that this host used to
  spell for itself.

  Nothing asserts a pixel for the features, and that is deliberate: whether a
  face has `smcp` is the face's business, DirectWrite asks and a font without
  it renders unchanged. What is asserted is the translation, plus that asking
  does not take the paragraph down, which is the failure mode of a wrong tag.
  The AppKit host made the same choice for the same reason.

  What the entry said when it was open, kept because the comparison is the
  useful part:

  `fontVariant` is the easier of the two here than on either other host:
  `core/FontVariants.h` resolves the bitmask to OpenType tags -- `smcp`, `tnum`,
  `ss07` -- and `fontFeatureSettings()` returns them in the form Pango takes.
  DirectWrite wants the same tags as `DWRITE_FONT_FEATURE_TAG`, which is
  `DWRITE_MAKE_OPENTYPE_TAG('s','m','c','p')`, added to an
  `IDWriteTypography` with `AddFontFeature` and set on the range with
  `IDWriteTextLayout::SetTypography`. No table to copy: the tags are in core.
  AppKit is the host that needs a translation, Core Text wanting Apple's older
  AAT selectors instead.

  `opacity` multiplies the alpha of the foreground and the background, which
  `core/TextColors.h` does; what is left is for `buildTextStyle` to take its
  colour from `textForegroundColor()` rather than reading the prop itself. That
  would also pick up React Native's default of opaque black, which this host
  currently spells for itself.

- ~~**No text decoration, so `textDecorationLine` and the two props beside it
  draw nothing.**~~ The line is drawn as of 2026-10-09, through
  `core/TextDecorations.h` like the other two hosts, so all three now agree on
  which lines an app asked for. `IDWriteTextLayout::SetUnderline(TRUE, range)`
  and `SetStrikethrough(TRUE, range)`, per run, beside where the font size and
  weight are already set.

  One wrinkle worth knowing: a single-style paragraph has no runs at all here,
  its style living on the `IDWriteTextFormat`, and a format has no underline
  property. So that case sets the decoration over the whole string instead,
  which is the only range there is.

  Three tests, against pixels rather than against the style: the longest
  horizontal run of ink is a line and a few pixels is a glyph, an underline's
  row is below a strikethrough's, and both at once draw two lines. A test on the
  struct would pass through exactly the bug this entry described.

  **What is left is the style and the colour**, which is why this entry keeps a
  paragraph. DirectWrite draws an underline in the text's own colour and
  with no pattern, so `textDecorationColor` and `textDecorationStyle` need a
  custom renderer: `IDWriteTextRenderer::DrawUnderline` and `DrawStrikethrough`
  are the callbacks, and they hand over a `DWRITE_UNDERLINE` with the geometry
  already worked out, so what is left is a `FillRectangle` in the asked-for
  colour, or a dashed `ID2D1StrokeStyle` for a dotted or dashed one. That would
  make this host the *most* capable of the three for style, since neither Pango
  nor Core Text has all five.

- **The desktop's text scale is not read, so Windows's large-text setting does
  not enlarge anything.** `allowFontScaling` and `maxFontSizeMultiplier` *are*
  read here, through `core/FontScaling.h`, so a `<Text>` that refuses scaling
  refuses it on all three hosts and a ceiling caps what it caps. What this host
  passes as the platform's scale is 1.

  The scale exists: `Windows::UI::ViewManagement::UISettings::TextScaleFactor`,
  which is 1.0 to 2.25 and raises `TextScaleFactorChanged` when the user moves
  the slider in Settings > Accessibility > Text size. It is WinRT, and this host
  is Win32 with no WinRT in it, so reaching it means either `RoActivateInstance`
  by hand or taking a C++/WinRT dependency into a host that has so far needed
  neither. That is the decision, not the call.

  Until then the group is testable but not useful here: `BASALT_TEST_FONT_SCALE`
  supplies a scale, which is what the e2e scenario uses on macOS too, since
  macOS publishes no scale either. The scenario is skipped by name on Windows
  because the *platform* half is what is missing.

- ~~**No text shadow, so `textShadowColor` and friends do nothing.**~~ Done,
  2026-10-09, the second of the two ways this entry named: `CLSID_D2D1Shadow`
  over a compatible bitmap the text is drawn into, which blurs that bitmap's
  alpha and colours it, drawn at the offset and under the glyphs. The standard
  deviation `core/TextShadows.h` resolves is what the effect's blur property
  takes, so nothing converts, and one shadow per paragraph is the same decision
  the other two hosts made for the same reason.

  **This is the first effect in this host, and what it needed is worth
  recording**: an effect wants an `ID2D1DeviceContext` where the view layer
  hands over an `ID2D1RenderTarget`. A target made by a Direct2D 1.1 factory
  answers that interface, and both of this host's targets are -- the window's
  `ID2D1HwndRenderTarget` and the suite's WIC bitmap -- so `drawShadow` asks the
  target for one rather than threading a second type through the view layer.
  `filter` and `mixBlendMode`, which are the other two entries that need
  effects, can take the same route.

  The fallback is the first way this entry named, and it is still here: without
  a device context there is no blur to be had, so the text is drawn once at the
  offset in the shadow colour, which is a hard shadow.
  `text_a_shadow_radius_spreads_it` is what notices the difference, by counting
  the pixels a blur covers and a hard edge does not, so a runner that stopped
  answering for a device context would fail rather than quietly go hard.

  Five tests: the shadow under the glyphs with both still visible, an offset
  that moves it on both axes, the blur that spreads it, a transparent colour
  that is no shadow, and the dump. The end-to-end scenario now reads
  `text-shadow=(2,3,4,#4d8cf2ff)` on all three hosts.

- ~~**An animated GIF is painted as a still.**~~ It plays as of 2026-10-09, so
  all three hosts animate one, and the pacing is `core/ImageAnimation.h`'s on
  every one of them: a delay of 10ms or less becomes 100 the way every browser
  does, and `imageAnimationStep` answers which frame an elapsed time lands on.
  What this host added is the decode, which WIC does not do by itself, and a
  clock.

  **The decode is `RnWin32Image::framesFromEncodedBytes`**, a second decode
  beside `fromEncodedBytes` rather than a replacement for it: every still
  `<Image>` goes through that one and must not change, and a file with one frame
  costs this one a header read. `GetFrameCount` says how many, `GetFrame(i)`
  hands over each, and each frame's `GetMetadataQueryReader` answers
  `/grctlext/Delay` in hundredths of a second and `/grctlext/Disposal`. The loop
  count is the decoder's own reader, `/appext/application` plus `/appext/data`
  for the NETSCAPE2.0 extension; a file with no extension at all plays once,
  which is what ImageIO reports for the same bytes.

  **The compositing is the part the other two decoders do invisibly.** A GIF
  frame is only the rectangle that changed, at `/imgdesc/Left` and
  `/imgdesc/Top` inside the logical screen `/logscrdesc/Width` and
  `/logscrdesc/Height` give, and its transparent pixels mean "leave what is
  under them". So the frames are built onto a persistent canvas, with disposal 2
  clearing that frame's rectangle afterwards and disposal 3 putting back what
  was there before. Each composited canvas becomes one `RnWin32Image`, which is
  why a frame is the size of the screen rather than of the rectangle.

  **The clock is the host's, and the window's own timer rather than the
  choreographer's.** `main_win32.cpp` already started and stopped a 16ms timer
  for an `<ActivityIndicator>`, for the reason entry 5 gives about not waking a
  laptop sixty times a second forever; that timer now also runs when
  `hasAnimatedImage()` and advances every animated image by a measured delta
  before invalidating. Measured rather than counted, because a GIF's delays are
  hundredths of a second and the tick is 16ms, so counting ticks would quantise
  every delay and a busy process would run the animation slow.

  The loader keeps the frames, asked for separately through
  `Win32ImageLoader::animation(uri)` rather than carried in the load callback:
  being animated is a property of the file and the callback's job is the pixels.
  Every frame counts against `core/ImageCache.h`'s budget, since every frame is
  a held bitmap, and an eviction drops the frames with the still image.

  Nine tests, against pixels rather than against the frame index: the frames
  decode with the file's own delays, red then blue then red as it is advanced,
  what is left until the next frame, a GIF that asks for no delay paced at 100
  (and the raw delay read as zero, which is what says the clamp is doing it here
  rather than WIC), the dump line, a re-apply of the props keeping the
  animation's place, a file that does not loop stopping on its last frame, a
  still PNG that is not an animation, and the loader handing back the same
  frames twice. The end-to-end scenario runs on all three hosts now.
