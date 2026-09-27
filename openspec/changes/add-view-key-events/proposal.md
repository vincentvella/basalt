# Key events on a view, not only in a text field

## Why

`onKeyPress` exists on `<TextInput>` and there is nothing for a `<View>`, so an
app cannot bind a shortcut to anything it draws. On a phone that is no loss. On a
desktop it is most of the interface: kino is a video editor whose whole editing
model is J/K/L shuttle, `i`/`o` for a loop region, `m` for a marker, Cmd+Z, and
zoom on Cmd+=/-/0. Every one of those is dead here.

It is also the last thing keeping kino on `react-native-macos`. Its
`src/keys/hotkeys.ts` returns `focusable`, `keyDownEvents` and `onKeyDown` --
react-native-macos view props -- and a desktop host that cannot offer an
equivalent cannot replace it, whatever else it does.

## What Changes

- A view can declare the key combinations it handles, and be told when one is
  pressed.
- The vocabulary is W3C `KeyboardEvent.key` names -- `"a"`, `" "`,
  `"ArrowLeft"`, `"Escape"` -- with `altKey`, `ctrlKey`, `metaKey` and
  `shiftKey`, which is what both react-native-macos and react-native-windows
  use. Nothing is invented.
- Declarative, not a callback the platform waits on. A host has to decide
  *synchronously* whether a key was handled, because an unhandled one must still
  reach the menu, the focused text field or the scroll view underneath. Asking
  JavaScript is not available at that moment, so the list is what answers.

## Capabilities

### Modified Capabilities
- `keyboard-input`

## Impact

- `native/core/KeyEvents.{h,cpp}`: the vocabulary and the matching, shared. Which
  keys a view claims is policy and belongs in core; `NSEvent`, `GdkEvent` and
  `WM_KEYDOWN` are the hosts'.
- Each host's key handling, which already exists for `<TextInput>` and for Tab.
- `packages/react-native-basalt/src/KeyHandler.tsx`: the component apps use.
- `docs/backlog/input.md`: this entry stops being a decision.
