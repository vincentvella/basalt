# Decisions

So they are not re-litigated.

## Path A over a Node-based reimplementation — 2026-09-08

**Rejected:** building on GTKX (`gtkx-org/gtkx`) / `react-native-gtkx`
(`itsmepetrov/react-native-gtkx`), which reimplement the RN API on Node with a
JS-side Yoga tree and no ReactCommon.

**Chosen:** a true out-of-tree platform embedding ReactCommon/Fabric/Hermes.

**Why:** the Node approach ships far sooner but yields a parallel ecosystem —
no existing RN native module works, ever. Path A costs much more up front and
gives a real porting path. Accepted cost: each native library still needs a
Linux backend written; "portable in principle" is not "works on day one".

## clang, not GCC — 2026-09-08

RN builds `-Wall -Werror -Wpedantic` and is a clang codebase. GCC fails on
`#pragma mark`, folly's `__int128` under `-Wpedantic`, and
`-Wsubobject-linkage` in `NetworkIOAgent`. Rather than paper over RN's own
warnings with `-Wno-*`, the project uses clang. Revisit only if GCC support
becomes a distribution requirement.

## Vendor folly/fast_float, use system glog/boost/fmt — 2026-09-08

folly is pinned to RN's exact version (`2024.11.18.00`) because RN builds a
trimmed subset of it and version skew is likely to hurt. glog and boost are
taken from the system despite RN pinning older versions, because the API
surface RN uses is stable — with one exception already hit, glog >= 0.6
requiring `GLOG_USE_GLOG_EXPORT`.

## RnLayout does no layout — 2026-09-08

Yoga resolves absolute frames before any mutation arrives, so `measure` returns
0 and `allocate` places children at their assigned rects. Letting GTK
participate in sizing would mean two layout systems disagreeing.
`react-native-gtkx` reached the same conclusion independently.

## The registry owns views between Remove and Delete — 2026-09-08

`g_object_ref_sink` on Create, `g_object_unref` on Delete. Fabric's Remove
detaches without destroying, and a view may be re-Inserted before its Delete
arrives, so parenting alone cannot own the lifetime.

## The first surface is driven by hand-written JS, not React — 2026-09-08

`ReactHost::startSurface` with an empty module name registers a shadow tree
without calling `AppRegistry.runApplication`: `SurfaceHandler::start` only
reaches into JS when a module name is set. That leaves a surface a plain script
can commit into through `nativeFabricUIManager`, the same JSI binding React's
Fabric renderer drives.

Taking that path meant phase 2 needed no `react` package, no Metro and no
bundler, so the milestone proves exactly one thing: the mutation stream now
comes from Fabric rather than from a harness. Bringing in React would have
mixed a bundler problem into a runtime problem.

The asymmetry to remember: `stopSurface` is *not* guarded the same way.
`UIManager::stopSurface` always calls `RN$stopSurface` in JS, so a script with
no React must install that global itself or every shutdown reports a fatal JS
error.

## The GTK4 replacement for size-allocate is the layout manager — 2026-09-08

`ReactHost::setSurfaceConstraints` has to be driven from the window's real
size, and the phase-2 plan assumed `GtkWidget::size-allocate`. GTK4 removed
that signal. A layout manager's `allocate` is the supported replacement and is
the one place a widget is told the size it actually got, so `RnLayout::allocate`
reports it through an optional callback on `RnView` and the host attaches one to
the surface root.

## http and websocket are host seams, and start out unimplemented — 2026-09-08

`getHttpClientFactory()` and `getWebSocketClientFactory()` are declared by
ReactCxxPlatform and defined nowhere in it — like
`getDefaultComponentRegistryFactory()`, every host supplies its own. Fantom
stubs both. `ReactHost` throws without them even when nothing makes a request,
so `src/LinuxNetworking.cpp` provides implementations that fail politely and
log. React Native already ships a working C++ websocket client at
`ReactCxxPlatform/react/http/platform/cxx/WebSocketClient.cpp` that its own
CMakeLists does not compile; wiring that up is phase 3 work, since the packager
connection is what needs it.

## Bundles are built for the `android` platform — 2026-09-09

`ReactCxxPlatform`'s `PlatformConstantsModule` returns
`PlatformConstantsAndroid`, and `DevServerHelper` hardcodes
`DEFAULT_PLATFORM = "android"` in the bundle URL it requests. React Native's JS
therefore already believes it is on Android when hosted through this platform,
and Metro has to resolve the matching `.android.js` files.

Bundling with `--platform linux` is not a small change of flag. React Native's
own internals have no `.linux.js` variants, starting with `Platform`, so
resolution fails outright. A real `linux` platform means shipping a JS package
that supplies its own `Platform` module and native component registry, as
`react-native-windows` and `react-native-macos` do. Until then, adding `linux`
to `resolver.platforms` would only move the failure somewhere less obvious.

## The demo app has no node_modules — 2026-09-09

`js/` is not an installed npm package. `metro.config.js` points `watchFolders`
and `resolver.nodeModulesPaths` at the React Native checkout that
`scripts/bootstrap.sh` already prepared, so `react`, `react-native`, Metro and
the Babel preset all resolve from there. Nothing is installed twice, and the
demo cannot drift from the source tree the C++ side is compiled against.

Two sharp edges. `extraNodeModules` pins `react` to one copy, because two React
copies in a graph produce the usual invalid-hook-call at runtime. And the
workspace packages must be required as package *specifiers*, not absolute
paths -- their entry points live only in an `exports` field, which Node ignores
when you require a directory by path.

## http is libcurl; the websocket is React Native's own — 2026-09-09

`getHttpClientFactory()` is implemented in `src/LinuxNetworking.cpp` over
libcurl, one thread per request. Every exit path ends in `onResponseComplete`,
because `DevServerHelper::downloadBundleResourceSync` blocks on a `std::future`
that only the callbacks complete: a client that quietly drops them does not
fail there, it hangs.

`getWebSocketClientFactory()` is *not* written here. React Native already ships
a working boost::beast client at
`ReactCxxPlatform/react/http/platform/cxx/WebSocketClient.cpp` which its own
CMakeLists never compiles, because the `react/http` glob covers only that one
directory. This project builds it as `rn_websocket`. The cost is two extra
dependencies: folly's `Uri.cpp`, which React Native does not build either, and
`boost_regex`, which `Uri.cpp` needs.

## Fast Refresh arrives as a reload, not a patch — 2026-09-09

Editing a module and seeing the window update goes through two channels.
React Native's JS HMR client connects to Metro over `WebSocketModule`, and
`ReactHost` separately opens a packager connection whose reload message calls
`reloadReactInstance()`. For an edit to a module with no refresh boundary --
`js/index.js` registers the app, so it has none -- the observed path is
`DevSettingsModule::reloadWithReason: Fast Refresh - No root boundary`, i.e. a
full instance reload. That is the same behaviour as iOS and Android for that
kind of edit, not a limitation of this platform.

## Text: replace React Native's stub, do not add a platform variant — 2026-09-09

`TextLayoutManager` has a header in React Native's cxx platform variant and one
implementation there, a stub that ignores every attribute and returns
`layoutConstraints.minimumSize`. The header is already generic, so this project
keeps it and supplies only the .cpp: `src/PangoTextLayoutManager.cpp` defines the
same symbols against Pango, and `cmake/ReactNativeCore.cmake` removes the stub
source from `react_renderer_textlayoutmanager` after `add_subdirectory` so the
two do not collide.

The alternative was a full `platform/linux` tree with a duplicate header. That
buys nothing while the interface is unchanged, and it would have to be kept in
sync with upstream by hand.

## One layout builder for measuring and painting — 2026-09-09

`src/PangoTextLayout.cpp` is used by both `TextLayoutManager::measure` and
`GtkMountingManager`. This is not tidiness: if the two built layouts differently
-- a different default font, a different wrap mode -- Yoga would allot a box
computed one way and the widget would paint text laid out another way, and the
result is clipped or overlapping text that looks like a rendering bug rather
than a measurement one. Sharing the builder makes that class of bug impossible.

Pango's font map is not documented as reentrant and this is reached from Fabric's
layout thread and the GTK main thread, so a single mutex covers every use. That
serialises all text measurement, which the `textMeasureCache_` mostly hides.

## Font sizes are absolute, not points — 2026-09-09

`pango_font_description_set_size` takes points and resolves them against the
context's resolution, so at the default 96dpi a `fontSize` of 16 renders at about
21px. React Native's `fontSize` is in density-independent pixels, and every
coordinate on this platform -- Yoga's frames, the widget's allocation -- lives in
that same logical space. So sizes go through `set_absolute_size`, and nothing
here multiplies by `pointScaleFactor`: GTK applies the display scale when it
renders the widget tree.

## Ellipsization needs a line limit, or it eats the paragraph — 2026-09-09

React Native's `ellipsizeMode` defaults to `Tail`, and setting
`pango_layout_set_ellipsize(END)` without also setting a height does not mean
"ellipsize on overflow": with no height, Pango ellipsizes to a *single line*.
Translating the default faithfully therefore collapsed every wrapping paragraph
to one line. Ellipsization is only applied when `maximumNumberOfLines` is set,
which is also the only case where React Native means it.

## The event beat is not optional, and it is a GSource — 2026-09-09

`EventQueue::onEnqueue` only sets a flag on the `EventBeat`. Nothing an
`EventEmitter` produces reaches JavaScript until something calls
`RunLoopObserverManager::onRender()`. Phases 2 to 4 never noticed, because mounts
arrive through `executeMount` rather than the event queue; input is the first
thing that depends on it, and without it touches are enqueued and silently
dropped.

React Native asks for `Activity::BeforeWaiting`: run once the loop has drained
its work and is about to sleep. The GLib equivalent is a `GSource` that does the
work in `prepare()` and never reports itself ready -- `prepare()` runs once per
main-loop iteration before the poll, so it costs one call when the loop is busy
and nothing when the app is idle.

A `gtk_widget_add_tick_callback` on the root would also work and was the obvious
first idea, but it holds the frame clock open and wakes the process at display
rate for as long as the window is mapped. That is the wrong trade for a desktop
app that spends most of its life still.

## Input is touch events, and one set of controllers on the root — 2026-09-09

React Native's Pressability -- what backs every `onPress` -- runs on the
responder system in JavaScript, and the responder system is fed by
touchstart/touchmove/touchend. W3C pointer events exist alongside them but are
consulted only for hover, behind `shouldPressibilityUseW3CPointerEventsForHover`.
So a desktop pointer is reported as a single touch point, which is also what
React Native for Windows and macOS do.

The controllers live on the surface root rather than on each view. Per-widget
controllers would have to be created and destroyed on every mutation, and would
still need a hit test; `gtk_widget_pick` already walks the tree and returns the
deepest widget at a point, which is the answer React Native's hit testing wants
now that every view is allocated at the frame Yoga assigned it.

Two details that are easy to get wrong. A gesture reports against the view it
*started* on for its whole life, even after the pointer leaves, because that is
what the responder system expects. And on touchend the touch must not appear in
`touches`, only in `changedTouches` -- leaving it in convinces the responder
system a finger is still down and it swallows the next press.

## `<Image>` loads its own pixels — 2026-09-09

React Native's cxx `ImageManager` is a stub: `requestImage` returns
`ImageRequest{source, nullptr, {}}`, so no `ImageResponse` ever arrives and
`ImageState` never carries anything to render. The platform view is expected to
load its own image, which is what Android does too -- Fresco, from
`ReactImageView`, not from the shadow node.

So `GtkImageLoader` reads the URI off `ImageProps::sources` and produces a
`GdkTexture`. I/O runs on a worker thread; the decode happens back on the main
thread, because `GdkTexture` is a GObject and the expensive part is the read.

Two lifetime rules fall out. The completion looks the view up by tag rather than
capturing the widget, because a view can be deleted while its image is in
flight. And a mutation that changed only layout must not restart the load, or an
`<Image>` flickers whenever its parent resizes -- so the current URI is tracked
per tag and an unchanged one is served from cache.

## `<ScrollView>` is an offset, not a GtkScrolledWindow — 2026-09-09

`GtkScrolledWindow` sizes its child through the measure/allocate protocol, and
this platform's whole invariant is that React Native decides sizes and
`RnLayout` only places things. Using it would mean teaching `RnLayout` to report
a real size, i.e. two layout systems disagreeing.

Instead a ScrollView is an `RnView` that clips and carries a scroll offset, and
`RnLayout::allocate` subtracts that offset from each child's frame. Yoga has
already laid the content out at full size -- the ScrollView's node carries
`overflow: scroll`, which is what lets its child exceed the viewport -- so the
offset is the only thing missing. Placing children at their scrolled positions
also means `gtk_widget_pick` follows the scroll, so hit testing needs no
special case.

The child structure matters and is easy to get wrong: a mounted ScrollView has
**one** child, not N. React Native's JS wraps the children in an
`RCTScrollContentView`, which `componentNameByReactViewName` rewrites to a plain
`View`, so no extra descriptor is needed.

## Two things happen on every scroll, and only one is throttled — 2026-09-09

`onScroll` goes to JavaScript and is throttled by `scrollEventThrottle`, which
is the platform's job on every platform. Without it VirtualizedList never
renders past its first window.

Writing `contentOffset` back into `ScrollViewState` is separate and must not be
throttled. `ScrollViewShadowNode::getContentOriginOffset` reads it, and through
that so do `measure`, `measureLayout`, C++ hit testing (`findNodeAtPoint`) and
view culling. Throttling it leaves all of those quietly reporting an unscrolled
position.

One more trap: `ScrollEvent::zoomScale` defaults to **0**, not 1, and
VirtualizedList only repairs negative values. Leaving the default makes every
list measurement come out as zero.

## Accessible roles are chosen at construction — 2026-09-09

GTK4 has no per-instance setter for an accessible role: it is a construct-only
property, or is set once per widget class. `RnView` is one class for every React
Native view, so the role has to be decided when the widget is made. That works
because Fabric delivers a view's props with the Create mutation that makes it,
but it does mean `accessibilityRole` cannot change after mount. Everything else
-- label, hint, states -- updates freely.

An unrecognised role falls back to `GENERIC` rather than a guess. A wrong role
is worse than none: it makes a widget announce itself as something it is not.
Where the app says nothing, the component decides -- a `<Text>` is a label and
an `<Image>` an image whether or not anyone asked.

States are tri-state on purpose. Leaving `checked` unset is not the same as
setting it false: a view that never mentions being checked is not an unchecked
checkbox, and a screen reader should not read it as one.

## `<TextInput>` is blocked on shipping our own JS component — 2026-09-09

Both of React Native's built-in text inputs are unusable here, for different
reasons.

Bundles are built for the `android` platform, so JavaScript asks for
`AndroidTextInput`. Its descriptor includes `<fbjni/fbjni.h>` and reaches into
a Java `FabricUIManager` for theme padding, so it cannot be compiled off
Android at all.

The iOS descriptor, `TextInputComponentDescriptor`, *is* portable -- it needs
only a `TextLayoutManager`, which this platform now has. But its component name
is `TextInput`, and JavaScript only asks for that name when the bundle is built
for iOS: `componentNameByReactViewName` maps `SinglelineTextInputView` and
`MultilineTextInputView` onto it. Bundling as iOS to reach it would contradict
`PlatformConstantsModule`, which reports Android, and `DevServerHelper`, which
hardcodes `platform=android` into the bundle URL.

So `<TextInput>` needs a JavaScript component of our own, mapping to a
component name this platform defines -- which is the same blocker as a real
`linux` Metro platform. It is a packaging problem wearing a rendering problem's
clothes, and doing it by halves would mean either an unbuildable descriptor or
a bundle that lies about what platform it is on.

## transform is composed during layout, not at paint time — 2026-09-09

`gtk_snapshot_transform` would have been the obvious place, and it would have
been wrong: a transform applied while painting moves the pixels but not the
widget, so `gtk_widget_pick` still finds the view at its untransformed frame and
a rotated button is clickable where it *used* to be.

Composing it into the `GskTransform` that `RnLayout::allocate` hands to
`gtk_widget_allocate` moves the widget itself, and GTK's own picking follows --
the same reason the scroll offset lives there.

The anchor is the view's centre. `resolveTransform` folds in `transformOrigin`
only when one is set, and the offsets it produces are measured *from the
centre*, so the platform has to supply that anchor. Every other React Native
platform does the same, iOS through its layer's default anchor point.

React Native's matrix is CSS `matrix3d` order and graphene's is nominally
row-major. The two coincide in memory for translation and scale but are
transposes for rotation, which no unit test on an axis-aligned box can tell
apart. The demo therefore rotates a card with a marker in one corner: a positive
angle turns clockwise, so the marker must end up on the other side. It does, so
no transpose is needed.

## zIndex reorders painting, never the child list — 2026-09-09

Fabric's Insert and Remove mutations carry an `index` into the parent's child
list, so that list has to stay in mutation order. `RnView::snapshot` therefore
sorts a *copy* by zIndex when any child has one, and skips the sort entirely
when none does, which is the common case. The sort is stable, so equal zIndex
keeps document order -- what CSS and React Native both promise.

## The `linux` platform is nine redirects and one real file — 2026-09-09

Bundling for a platform React Native has never heard of fails in three
different ways, and only the third is the interesting one.

Nine files are self-importing shims: their whole body is
`import X from './X'; export default X;`, marked "backwards compatibility of
subpath (deep) imports". They exist so `react-native/Libraries/Image/Image`
resolves, and they assume a platform-specific sibling will win. On `linux` each
resolves to itself and exports undefined, and they fail one at a time, far from
the cause -- `Platform.constants` undefined, then a view config undefined, then
a component undefined. Finding them by crashing takes an afternoon; finding them
with one grep for that note takes a minute, which is why the list is spelled out
in `metro-config.js` rather than discovered.

Each is answered with React Native's own `.android.js` sibling rather than a
copy. This platform reports `PlatformConstantsAndroid` from C++, shares
ReactCommon's prop parsing, and drives the components Android's JavaScript
drives, so Android's implementation is the one that matches what is actually
here. Nine forks would drift from upstream in silence.

A second kind does not resolve at all -- `ReactDevToolsSettingsManager` ships
only as `.android.js` and `.ios.js` -- so there is no resolution to rewrite,
only a failure to catch. That one is a real file, a no-op, because the
TurboModule behind it does not exist here either.

`Platform` is the only module this platform genuinely implements: `OS: 'linux'`,
and a `select` that prefers `linux`.

And one thing the C++ side simply cannot be told: `DevServerHelper` builds its
bundle URL from `constexpr DEFAULT_PLATFORM = "android"`, with no hook. Left
alone, an app would be `Platform.OS === 'android'` under Fast Refresh and
`'linux'` in a release build -- a worse trap than either value on its own. Metro's
`server.rewriteRequestUrl` corrects the request on arrival instead.

## `<TextInput>` reuses iOS's C++ and forks its JavaScript — 2026-09-09

Both halves of that were forced, in opposite directions.

The C++ was free. React Native's Android text input includes `fbjni` and calls
a Java `FabricUIManager`; its iOS one includes nothing but ReactCommon and
measures through whatever `TextLayoutManager` is installed, which here is the
Pango one. So `TextInputComponentDescriptor`, `TextInputProps`,
`TextInputShadowNode`, `TextInputState` and `TextInputEventEmitter` are compiled
unchanged, under the name they declare, `TextInput`. React Native's own
`componentNameByReactViewName` already maps `RCTSinglelineTextInputView` to it.

The JavaScript could not be. `TextInput.js` is
`if (Platform.OS === 'android') { ... } else if (Platform.OS === 'ios') { ... }`
with no third branch, so on `linux` every component and command binding stays
undefined and React reports an invalid element type. The alternatives were a
third branch upstream, a patch, or a smaller file of our own. The first is not
ours to make; the second drifts silently; the third is honest about being a
subset. It is the only fork in the tree and should stay that way -- it is a
debt, not a pattern.

The editing itself is a real `GtkText` rather than a caret drawn on a
`PangoLayout`. Input methods, selection, the clipboard and every Linux
keybinding come with it, and each is easy to get subtly wrong. The cost is that
the widget holds state React Native believes it owns, which is what the
`applying` flag, the preserved cursor position and the `eventCount` check exist
to reconcile.

## Imperative commands run on the main thread — 2026-09-09

`schedulerDidDispatchCommand` arrives on the JavaScript thread, inside the event
loop's rendering update, exactly as `executeMount` does. `dispatchCommand` was
calling straight into GTK from there, which was survivable only because the only
commands were `ScrollView`'s: moving an adjustment touches nothing reentrant.
`gtk_text_grab_focus` reaches the platform input method, and on macOS AppKit
asserts it is on the main thread and traps the process.

Commands now queue through `g_idle_add_full` at the same priority as a mount.
That is not just for safety: equal-priority idle sources run in the order they
were added, so a command still lands behind the transaction that created the
view it names, which is the ordering React Native's `focus()`-on-mount depends
on.

## On Windows a view is not a window — 2026-09-11

**Rejected:** a child `HWND` per React Native view, which is the direct
translation of what GTK does with `GtkWidget` and macOS with `NSView`.

**Chosen:** one `HWND` per surface, and a plain C++ `RnWin32View` painted by a
recursive Direct2D walk.

**Why:** three reasons, and any one of them would be enough. An `HWND` is a
kernel object and USER32 caps a process at ten thousand by default, so a long
list would spend a real fraction of the budget on things that are conceptually
rectangles. An `HWND` clips rectangularly and cannot be rotated, so `transform`
and rounded `overflow: hidden` — both of which GTK already has — would need a
parallel implementation regardless. And the message routing a window buys is the
part this project least wants: React Native does its own hit testing, and
`gtk_widget_pick` was a convenience rather than a requirement.

The exception, and it is the same exception both other platforms make:
`<TextInput>` gets a real `EDIT` peer. Input methods, selection, the clipboard
and every Windows key binding come with it, and each is easy to get subtly
wrong.

The consequence to remember is that this layer is closer to GTK's snapshot walk
than to AppKit's layer tree, so where the AppKit side maps a prop onto a
`CALayer` property, the Windows side composes it by hand in `paint` — exactly as
`rn_view_snapshot` does.

## Painting is immediate, not composited — 2026-09-11

**Rejected:** DirectComposition, a visual per view, which is the closer analogue
of AppKit's layer-backed views and would put transform and opacity in the
compositor.

**Chosen:** one Direct2D walk, rendering to a window or to a WIC bitmap.

**Why:** the offscreen render is not a debugging convenience on this platform,
it is the test. A composited tree would need a second, separate path to produce
a picture, and the value of the picture is that it is made by the same code that
draws the app. Revisit when there is a real frame budget to defend, which needs
an app first.

## Rendering assertions live on Windows, because they are free there — 2026-09-11

Direct2D renders into a WIC bitmap with no window, no device and no display
connection. So the backlog item that has been open since GTK — "the widget tree
says a view has a colour and a frame, not that the right pixels reached the
screen" — costs a function call here, where it costs a display server on Linux
and an offscreen window plus a display cycle on macOS.

`tests/test_win32_paint.cpp` is therefore where this project asserts things no
tree dump can show: that a transform is composed in the right order *and the
right direction*, that opacity composites a subtree rather than a brush, that a
clip clips. What those tests check is not Windows-specific; only their being
cheap is. They are the shape for the other two hosts to borrow, not a reason to
leave them unchecked.

## Two places to write things down, not four — 2026-09-18

**Rejected:** `plan/`, in every form. It had grown a numbered journal of 49
phase files, a backlog, and this decisions log, and the journal had become a
second and incompatible numbering: `docs/ARCHITECTURE.md` carried a roadmap of
phases 0-27 while `plan/` ran 02-50, so "phase 22" meant the rename in one and
an unstarted CLI command in the other. A comment saying "found in phase 46" sent
a reader to a table with no 46 in it.

**Chosen:** everything is a spec or an architecture doc.

    openspec/specs/     what the platform is required to do
    openspec/changes/   work proposed or under way
    docs/               how it is built, why, what is missing, how to test

`plan/decisions.md` became `docs/DECISIONS.md` -- this file. `plan/backlog*`
became `docs/BACKLOG.md` and `docs/backlog/`. The five phase files the build
scripts actually depended on were distilled into `docs/PORTING.md`: the Hermes
and MSVC fixes, the pinned version policy, and the `ReactCxxPlatform` packaging
gap, which is content no spec can carry and no other file had. The other 44 were
deleted.

**Why:** a phase file said "here is what I did and why", which git already
dates and which goes stale the moment the code moves; a spec says "here is what
this platform is required to do", which is checkable
(`openspec validate --specs --strict`) and diffable against reality. Keeping
both meant the honest answer to "what does this do" depended on which file you
happened to open -- and the roadmap that claimed to summarise it was wrong about
four of its own rows, including one marked blank that two committed ports had
already finished.

**The cost, honestly.** About 5,000 lines of working notes are now only in git
history. Roughly forty comments cited a phase file; each was read, and in almost
every case the comment already carried the whole explanation and the pointer was
decoration. Where it was not -- `bootstrap.sh`, which cites the Hermes patches
seven times between them -- the content moved to `docs/PORTING.md` rather than
being dropped.

## TypeScript, with a compile step — 2026-09-18

**Rejected:** staying on JavaScript with JSDoc types and generated `.d.ts`,
which was the cheaper path and keeps the package buildless.

**Chosen:** the packages are written in TypeScript and compiled before they are
published.

**Why:** the package ships no types at all today -- `package.json` sets no
`types`, there is no `.d.ts` anywhere, and there was no `tsconfig.json` in the
repository. That was drift rather than a decision: nothing here argued for
JavaScript, which is where the argument would have been. Meanwhile
`create-expo-app` gives you TypeScript, so the first line the intended user
writes is `import {useWindow} from 'basalt-core'`, and it resolves to
`any`.

JSDoc would have delivered types to consumers without a build. It was rejected
because the authoring experience is the part that lasts: this package's public
surface is small and its internals are not, and the checking that matters most
is on the seams between them -- `metro-config.js` alone is 530 lines of
resolution policy with no types on any of it.

**The cost, which is not only the build step.** `main` stops pointing at source.
`PLATFORM_OVERRIDES` hands Metro absolute paths ending in `.js` and Metro
resolves them directly, so those paths have to name compiled output -- which
means an unbuilt checkout cannot bundle, where today it can. The platform
extension files (`Platform.linux.js` and its siblings) must keep those exact
names through compilation. And `react-native.config.js` and `metro-config.js`
are read as CommonJS out of `node_modules` by tools this project does not
control, so they must be emitted as CommonJS at paths that do not move.

None of that is hard. All of it is the kind of thing that is discovered by an
app failing to bundle rather than by a type error, which is why it is written
down here before the work rather than after it.

## Core is the app's own surface; a package is what reaches outside it — 2026-09-19

React Native shipped one package with everything in it and has spent the decade
since extracting AsyncStorage, WebView, NetInfo, Clipboard, CameraRoll, Slider
and the rest. The extraction is the expensive half: every move is a breaking
change, a migration guide and a community package that has to be adopted before
core can drop the original. This platform has thirty-three desktop capabilities
catalogued as open and, until now, nothing deciding where the next one goes.

**The rule.** Core is the application's own surface — its windows, menus,
dialogs, title bar, components and input. A capability goes in a package of its
own if it fails **any one** of three tests:

1. **Does the operating system ask the person for consent?** A permission prompt
   is the system saying this is not ordinary application behaviour.
2. **Does it touch hardware, or another application?**
3. **Does it act outside the app's own windows?**

So: notifications, camera, microphone, screen capture, location, drag and drop,
the tray icon, global shortcuts, power and idle, secure storage, auto-update,
crash reporting, file-system watching, in-app purchase — each its own package.
Windows and their geometry, menus and context menus, file dialogs, the title
bar, components and input, clipboard, sharing, linking, appearance, developer
tools, packaging, displays, window state, dock progress — core.

**Rejected: "could an app ship without it?"** An earlier draft moved menus,
dialogs and the title bar on that reasoning. By that test almost everything is
optional and the boundary lands nowhere useful. A file dialog is the person
choosing a file inside the app's own flow, a menu is the window's furniture, and
a title bar is part of the window. A desktop platform that made you install a
package for a menu would be a worse platform, not a smaller one.

**Spell checking, which the design left open, is core — both halves.** The
question was posed as the platform's own dictionary being ordinary and a
downloaded one not being, and applying the rule rather than the intuition
settles it the other way: fetching a dictionary asks nobody for consent, touches
no hardware and no other application, and acts inside the app's own windows. It
fails none of the three tests. What a downloaded dictionary actually needs is a
cache-directory seam, which is a question about a seam and not about a package —
the same seam network assets need, and it is on the backlog under its own name.

That is the rule doing its job: it produced an answer that the reasoning which
raised the question did not.

**One package per capability, not one per desktop.** A capability only one
desktop can do still ships as one package, installs and imports the same way
everywhere, and does nothing where it cannot work. That is what the platform
already does — `<TitleBar>` accepts every call on a host with no title bar,
`windowControl`'s mutations are no-ops where GTK4 removed the call,
`Menu.isSupported` is false on GNOME. A package per desktop would mean an app
importing different modules per platform, which is the thing this platform
exists to avoid.

**A no-op is always paired with a way to ask.** Doing nothing quietly is right
for a call an app makes anyway; it is wrong as the only answer available, because
an app should be able to hide a control rather than offer one that does nothing.
Notifications is the shipped example: `notificationSupport()` answers with a
reason, and `getPermissionsAsync` reports `denied` and carries that reason to
where a developer will see it.

**Version together, at first.** Separate packages need not mean independent
versions, and independent versions of packages sharing a C++ ABI with the host
is a support problem nobody here wants yet.

**A package declares itself** with a `basalt` key naming a CMake entry point,
and the CLI scans the app's dependencies for it. Expo, worklets and Reanimated
keep their hardcoded special cases because they are third-party packages that
will never carry the key. See `openspec/changes/split-optional-capabilities-into-packages`.

**An app's own module declares itself by being there.** The same mechanism, with
the declaration dropped: a directory under the app's `modules/` with a
`native/CMakeLists.txt` is compiled into the host, on the same contract as a
package. It has no manifest to put a `basalt` key in — `create-expo-module
--local` writes a config, an index and a platform directory and no package.json —
so presence is the declaration, the way `ios/` is how the Apple half declares
itself. `modules/` rather than a name of our own because that is where Expo's
autolinking already looks, which means an app that has written the Apple half of
a module has already put the desktop half's home in the right place. The rule the
dependency scan exists to keep is kept for free: this is the app's own source
tree, so nothing reaches the binary that the app did not write. See
`openspec/changes/add-app-native-modules`.

**And it names what it links.** A module contributing a platform implementation
usually needs a library for it, and until kino's audio module there was no way to
say so: this project's host CMakeLists named `-framework UserNotifications` for
the notifications package, which works only for as long as every package is ours.
So each host reads a `BASALT_PACKAGE_<HOST>_LINK_LIBRARIES` property alongside
the sources, and the notifications package now names its own framework — moved
there rather than left alone, so that the property is exercised by this
repository's CI and not only by an app outside it.

**An Expo app gets React Native's fetch, by default.** Expo replaces
`globalThis.fetch` with its own WinterCG implementation over a native module,
`ExpoFetchModule`, which is a pair of SharedObject classes with a streamed body
between them and is not ported here. The replacement goes in as a lazy getter,
so an app does not fail at import — it fails at its first call, inside the global
it was calling, where it cannot catch it. kino died exactly there, and then a
second time in the component whose data never arrived.

So `installExpoRuntime` sets expo's own `EXPO_PUBLIC_USE_RN_FETCH` before the
bundle evaluates, and React Native's fetch — which works here, over the same
curl client as the rest of the networking — stays. The variable rather than a
patch because it is expo's own documented way out, and the platform rather than
each app because "install basalt and the desktop works" stops being true if
every app has to discover this from a crash inside `fetch`. It survives because
Metro's prelude and React Native's `setUpGlobals` both keep an existing
`process.env`, and a developer who sets it themselves still wins, which is why it
is a plain assignment and not a `defineProperty`. What is given up is a streamed
response body, and an app importing `expo/fetch` directly still gets the error
naming the module — which is the honest answer until someone ports it.

**A TurboModule is not a singleton, and a listener must not assume it is.**
ReactCxxPlatform builds a new module for every lookup rather than caching one: an
app's start-up makes several, and they come and go while it runs. So a native
seam whose listener is installed by a module's constructor and cleared by its
destructor loses the listener the moment any one of them is released — including
while others are still serving. That is not hypothetical: it left every keyboard
shortcut in an app dead, with the keys claimed, the press matched, and the answer
dropped on the way to JavaScript. Nothing failed and nothing was logged.

A listener therefore belongs to the *set* of live modules, not to an instance:
installed when the first appears, handed to one still alive when another goes,
cleared only when the last does. `core/WindowsModule.cpp` is the worked example
and `basalt-subprocess` follows it. The tempting middle answer — an
ownership guard, where the destructor clears only if it is the one that installed
— was tried first and is wrong for the same reason: the installer is routinely
the one that dies first.

**An Expo module can emit.** A TurboModule is constructed with a `CallInvoker`
and an Expo module is not — it is built by a function given a runtime and nothing
else — so until a module needed to report something after its call returned, there
was no way for one to reach JavaScript later at all. The seam is
`core/JsRuntimeAccess.h`: each host leaves behind its
`ReactHost::runOnRuntimeScheduler`, and `emitExpoEvent` finds the module by name
at the moment of the emit rather than holding a `jsi::Object` that a reload would
outlive. Posting only, never waiting: the `CallInvoker` over it throws on
`invokeSync`, because a synchronous call from another thread into a busy runtime
is a deadlock and an invoker offering it would be lying about the door it has.
