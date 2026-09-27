# Tasks

## 1. The shared decision

- [x] `core/KeyEvents.h`: W3C key names, a modifier set, and a combination
- [x] A registry keyed by tag: which view claims which combinations
- [x] `handlesKey(tag, event)` and `handledBy(path, event)` -- the synchronous
      question every host asks, and the innermost-wins walk that answers it
- [x] Unit tests: nine, including a claim that does not match on a modifier the
      app did not ask for (Shift+Cmd+Z must not fire Cmd+Z), the innermost claim
      winning, a view outside the focus path not being asked, and a declaration
      replacing rather than merging. Verified to fail by merging instead

## 2. The hosts

- [ ] AppKit: `keyDown:` on the surface, claimed keys consumed and the rest to
      `super`; the menu already had its chance in `performKeyEquivalent:`
- [ ] GTK: the `key-pressed` handler returns whether it claimed the key
- [ ] Win32: `WM_KEYDOWN`, and `DefWindowProc` for anything unclaimed
- [ ] The focus path: a claim is honoured for the focused view and its ancestors,
      and a focused `<TextInput>` keeps its keys

## 3. The API

- [ ] `src/KeyHandler.tsx`: declares combinations, reports presses, re-registers
      when the list changes, unregisters on unmount
- [ ] The TurboModule the registration goes through
- [ ] Types matching react-native-macos' `HandledKeyEvent` shape

## 4. Tests

- [ ] An instrument to press a key, on all three hosts -- or the scenario skips
      where it is missing, which is the rule docs/TESTING.md records
- [ ] A scenario: a declared combination fires, an undeclared one does not, and a
      re-render that changes the list is honoured
- [ ] A scenario: a focused `<TextInput>` keeps its keys

## 5. Records

- [ ] `docs/backlog/input.md`: no longer a decision
- [ ] `docs/TESTING.md`: the new instrument
- [ ] Whether kino's `hotkeyViewProps()` can keep its shape, recorded either way
