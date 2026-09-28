# Design

## Which API

**react-native-macos's**, near enough to be a drop-in: `keyDownEvents` declares
combinations, `onKeyDown` reports them, and the key names are W3C's.

`keyUpEvents` and `onKeyUp` are that API's other half and are **not built**. The
registry and the reporting are indifferent to which it was, so adding them is a
second signal in each host rather than a second design -- but until a host raises
one, an app that binds a key-up gets silence, and that is worth saying here rather
than leaving a reader to infer it from the absence.

The backlog called this "a decision as well as an implementation" because React
Native has no cross-platform key event API to be compatible with. It has two
*platform* ones, and they agree with each other: react-native-macos and
react-native-windows both spell it this way. Inventing a third to sit between
them would be worse than picking one, and the one to pick is the one the app this
is for already uses -- kino's `hotkeys.ts` would keep its `PressedKey` shape and
its registry untouched.

## Why the list is declarative

Because the host has to answer "was that handled?" before it can do anything
else, and it cannot ask JavaScript.

A key that nothing handled must go on: to the menu, to a focused `<TextInput>`,
to a `<ScrollView>` that scrolls on arrows. A key that was handled must not. On
AppKit that decision is the return value of `performKeyEquivalent:` or whether
`keyDown:` calls `super`; on GTK it is what the `key-pressed` handler returns; on
Win32 it is whether `WM_KEYDOWN` is passed to `DefWindowProc`. All three are
synchronous and none can wait for a round trip through the JavaScript thread.

So the alternative -- deliver every key to JavaScript and let it decide -- cannot
work without swallowing keys the app did not want. An app would break Tab, arrow
scrolling and Cmd+Q by listening at all.

## Why not `nativeID`

`<DropTarget>` and `<TitleBar.DragRegion>` mark views by `nativeID`, and that
file explains why: it is the one prop a plain `<View>` carries to every host, so
there is no registration call and no lifetime to get wrong.

Key events depart from it, for two reasons that are about this feature rather
than about taste:

- **The list is long and it changes.** kino registers on the order of twenty
  combinations and adds to them as panes mount. `nativeID` would carry a string
  rebuilt on every registration, and a `<View>`'s identity would churn with it.
- **`nativeID` can hold one thing.** `<DropTarget>` already documents that a
  drop target cannot also carry an app's own `nativeID`. A view that wants keys
  *and* is a drop target is an ordinary thing to want -- a timeline that accepts
  dropped media and has shortcuts -- and two features competing for one string
  is a limit that would be hit immediately.

So a `<KeyHandler>` registers `(tag, combinations)` with a TurboModule and
unregisters on unmount. The failure mode `DropTarget` warns about -- silently
stopping after a re-render -- is real and is what the tests are pointed at: the
component re-registers when its list changes and the scenario asserts a shortcut
still fires after a re-render that changes it.

## What core owns

`core/KeyEvents.h`: the W3C name for a platform key code, the modifier set, and
`handles(view, event)`. Three hosts each have a different key code space and the
same question to answer, and the answer is not a toolkit call.

The registry is core too, keyed by tag, because "which view claims this key" is
the same decision everywhere and a host that kept its own copy would be a fourth
place for it to drift.
