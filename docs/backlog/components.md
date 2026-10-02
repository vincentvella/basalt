# Components not implemented

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (5):**

1. `Modal` is an overlay, not a window
2. A `<Switch>`'s colours are honoured on two hosts of three
3. Nothing is keyboard-reachable that is not `accessible` or a control
4. No dev menu item toggles Fast Refresh
5. AT-SPI actions

`View`, `Text`, `Image`, `ScrollView`, `TextInput`, `ActivityIndicator`,
`Switch`, `Modal` and `RefreshControl` are done on all three hosts, and touch
input, `PanResponder` and command routing with them. What is left:

- **`Modal` is an overlay, not a window.** The shadow node is a root-kind node
  sized from its state and positioned absolutely, so a modal fills the surface
  it is in, which is what it is on the web, and what needs no multiple-window
  support. A second real window is more native on a desktop and is still the
  better answer eventually; it depends on multiple windows, above.
  `animationType` is ignored, and so are `presentationStyle` and the iOS
  orientation props.
- **A `<Switch>`'s colours are honoured on two hosts of three.** GTK gets them
  through a CSS provider and Windows draws them; `NSSwitch` follows the system
  accent colour and exposes nothing per instance, so `trackColor` and
  `thumbColor` do nothing on macOS. The tree dump does not print colours, so
  this is a difference in pixels rather than in behaviour.
- **Nothing is keyboard-reachable that is not `accessible` or a control.**
  React Native's `focusable` prop never reaches this platform: see the
  accessibility section, so `accessible` is the signal, plus being a control:
  a `<Switch>` is a Tab stop and Enter toggles it, and a disabled one is skipped.
  Everything else an app wants in the tab order still has to say `accessible`.
- **No dev menu item toggles Fast Refresh.** The menu has Reload, Toggle
  Element Inspector and Open Debugger; `DevSettings.setHotLoadingEnabled` is a
  no-op stub in ReactCxxPlatform, so turning Fast Refresh off means calling
  `HMRClient` directly, which the override could do and does not yet.
- ~~**`DebuggingOverlay` mounts and draws nothing.**~~ All three draw it now:
  the filled blue box over an inspected element and the outline around a trace
  update, which takes itself down after a moment because it is meant to flash.
  What is left is the other half of a DevTools session: there is no inspector
  overlay of React Native's own beyond the one the developer menu toggles, and
  nothing drives these commands except DevTools itself.
- **AT-SPI actions**, against the accessibility hooks `IMountingManager` already
  declares. See the accessibility section.

Notably *not* on this list, because a real application turned out to use none of
them: `FlatList`, `SectionList`, `Animated` with a native driver, `SafeAreaView`,
`KeyboardAvoidingView`.
