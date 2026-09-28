# Input

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (4):**

1. setIsJSResponder is a no-op
2. PanResponder works, verified with real pointer motion through an X server agai
3. Multi-touch is not modelled: one pointer, identifier 0
4. Wayland input is unverified

- ~~No hover.~~ Done on all three hosts. `onPointerEnter`, `onPointerLeave`,
  `onPointerOver`, `onPointerOut` and `onPointerMove` fire, and `js/hover.js`
  plus a scenario in the end-to-end suite prove it. Worth recording what the
  work turned out to be, because the obvious implementation is wrong: a host
  emits `pointerMove` and nothing else. `PointerEventsProcessor` in ReactCommon
  already keeps the hover path and derives enter, leave, over and out from a
  single move, with the listener filtering and the capture rules. Emitting them
  from a platform as well produces each one two or three times over -- see
  core/HoverTracker.h, which is now only the gate that keeps an app with no
  hover listeners from paying for a JavaScript round trip per motion event.
  What is still missing is `onMouseEnter`/`onMouseLeave`, which are not React
  Native core props at all: they exist only in the macOS and Windows forks.
- ~~No momentum scrolling.~~ `onMomentumScrollBegin` and `onMomentumScrollEnd`
  fire on macOS and Linux, and the two platforms need opposite amounts of work
  for it. macOS decelerates a scroll itself and puts a `momentumPhase` on the
  event, so the AppKit host reports what it is handed. GTK emits one
  `decelerate` signal carrying the velocity the gesture ended at and then
  stops, so the coasting is modelled here -- core/ScrollMomentum.h, exponential
  friction at React Native's own `decelerationRate`.

  Windows has neither. A precision touchpad reports `WM_MOUSEWHEEL` with fine
  deltas and no fling: inertia there belongs to Direct Manipulation, which
  wants to own the viewport. So a Windows fling coasts no further than the
  fingers took it, and the two events never fire. Driving the shared model off
  a wheel would be worse than silence -- a wheel is not a throw.
- `setIsJSResponder` is a no-op. It matters once something scrolls natively, so
  it lands with `ScrollView`.
- ~~`pointerEvents` is ignored.~~ All four values work on all three hosts, and
  `describeTree` prints `pe=` so the cross-host diff can see the prop arrived.
  AppKit and Win32 own their hit tests, so the three modes are four lines each
  there. GTK expresses exactly one: `none` is `can-target`, which makes
  `gtk_widget_pick` skip the widget and everything inside it. `box-only` is
  resolved by walking up from the pick, and `box-none` by making the view
  untargetable for the length of one more pick and putting it back -- which is
  the only way to reach the sibling *behind* an overlay without replacing
  GTK's picking, and that would mean redoing the transform and clip handling it
  already gets right.
- ~~`Touch::offsetPoint` carries page coordinates rather than coordinates
  relative to the target view.~~ Fixed on all three. It is what
  `locationX`/`locationY` are built from, so the numbers an app read were
  larger than its view by exactly that view's position -- right only at the
  surface's origin.

  The old entry said there was no cheap way to get the target's absolute
  origin. There is, and it was already there: the hit test computes the local
  point at every level on the way down and throws it away. GTK asks
  `gtk_widget_compute_point`, which is correct through a transform because this
  platform composes transforms into allocations. AppKit and Win32 walk up
  composing `rnLocalToParent` / `localToParent` and invert the chain -- the
  same matrix the hit test inverts one level at a time, so a press and the
  coordinates it reports cannot disagree about where a view is.

  Not `-[NSView convertPoint:fromView:]`, which ignores the layer transform and
  would answer for a rotated view as though it were not rotated.

  Ten tests, five per host, against the walk rather than the event -- no unit
  test here can read a touch -- plus an end-to-end scenario on a button inset
  by its page's padding, so page and local differ by a number it can name.
- ~~No keyboard focus model outside `<TextInput>`.~~ Tab reaches a
  `<Pressable>` on all three hosts, Enter and space press it, and `onFocus` and
  `onBlur` fire. The decision the old entry asked for went this way, and both
  halves came from React Native's own Android implementation:

  **What is focusable:** `accessible`. React Native's `focusable` prop never
  arrives -- ReactCommon parses it only into Android's and tvOS's
  `HostPlatformViewProps`, and the C++ host's is a bare alias of
  `BaseViewProps`. `accessible` is what `<Pressable>` sets on everything it
  renders and what a screen reader stops on, so Tab order follows the
  accessibility tree, which is what both desktops do with their own widgets.

  **What activation is:** `topClick` with an empty payload, which is what
  `ReactViewManager.setFocusable` dispatches on Android. Pressability turns it
  into `onPress`, but only for a payload with no `pointerType` -- so it cannot
  go through `TouchEventEmitter::onClick`, which always carries one.

  The chain is the one place the three genuinely differ. GTK hands its focus
  chain over outright. AppKit has a key-view loop that is unusable for a window
  built without a nib -- `selectNextKeyView:` simply does nothing -- so the
  order is walked in tree order there. Win32 has no focus to speak of, because a
  React Native view is not a window, so all of it is this project's.

  ~~What is still missing is **key events**~~: a `<View>` has them now. React
  Native has no cross-platform key event API, which is what made this a decision
  -- but it has two *platform* ones and they agree, so `<KeyHandler>` follows
  react-native-macos and react-native-windows: `keyDownEvents` declaring
  combinations, `onKeyDown` reporting them, W3C key names. See
  `core/KeyEvents.h`, and `openspec/changes/add-view-key-events` for why the list
  is declarative and why it departs from the `nativeID` idiom.

  Two things about it worth knowing before building on it. With nothing focused,
  a declared combination fires wherever it was declared -- a window-level handler
  is a child of the root and so on no focus path, and the alternative was every
  app's shortcuts never firing. And `<KeyHandler>` sets `focusable`, so binding
  Tab inside one breaks focus movement there, which is the app's choice and is
  what a browser does too.

  Still absent: `onKeyUp`. The declaration and the plumbing are in place for it
  and no host raises it yet.
- `PanResponder` works, verified with real pointer motion through an X server
  against a probe that drags a view. Worth stating because it was never
  deliberately built, and a real app uses it for every drag it has.
- Multi-touch is not modelled: one pointer, identifier 0.
- Wayland input is unverified. Rendering is checked on Wayland and input on
  X11, but not both at once: a headless compositor has no seat, so there is no
  pointer to move. Needs a desktop session or real hardware.
