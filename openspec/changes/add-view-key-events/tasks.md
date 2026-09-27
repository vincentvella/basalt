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

- [x] AppKit: `keyDown:` asks first, claimed keys consumed and the rest fall
      through to activation, Tab and then `super`. Asked through the
      `RnAppKitFocusHandler` protocol because the view library links no core --
      the same seam `rnActivateView:` uses, and its "returns YES if it was
      handled" contract is exactly the semantics needed
- [x] `AppKitKeyEvents`: NSEvent to combination, ten tests. `characters` rather
      than `charactersIgnoringModifiers` so Shift+A is "A", the letter rather
      than the control character for Ctrl+A, the private-use arrows, back-tab as
      Tab plus shift, and a bare modifier as no combination at all
- [ ] GTK: the `key-pressed` handler returns whether it claimed the key
- [ ] Win32: `WM_KEYDOWN`, and `DefWindowProc` for anything unclaimed
- [ ] The focus path: a claim is honoured for the focused view and its ancestors,
      and a focused `<TextInput>` keeps its keys

## 3. The API

- [x] `src/KeyHandler.tsx`: declares combinations, reports presses, re-registers
      when the list changes, unregisters on unmount
- [x] `setHandledKeys`/`clearHandledKeys` on BasaltWindows, and a `basaltKey`
      device event, beside the drop ones and for the same reasons
- [x] Types matching react-native-macos' shape: `key` plus `altKey`, `ctrlKey`,
      `metaKey`, `shiftKey`

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
