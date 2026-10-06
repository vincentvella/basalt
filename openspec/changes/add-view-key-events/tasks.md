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
- [x] GTK: the `key-pressed` handler asks first and returns `GDK_EVENT_STOP` when
      a view claimed the key. `GtkKeyEvents`, ten tests mirroring AppKit's,
      because the two must agree -- an app binding "ArrowLeft" or metaKey has to
      get the same key on both. Super is meta, not GDK_META_MASK, which is a
      different key on X11 that almost no keyboard has
- [x] Win32: `WM_KEYDOWN` asks after Ctrl+D and before Escape, and returns 0 when
      a view claimed the key. `Win32KeyEvents`, seven tests covering the name
      table and the bare modifiers -- the modifier half is not covered because
      `GetKeyState` means asserting it would need a key physically held
- [x] Written blind: nothing here can compile this host, so CI is the check. The
      mapping file is in the test target for that reason
- [x] The focus path: innermost first from the focused view outwards. With
      *nothing* focused a claim fires wherever it is -- a window-level handler is a
      child of the root and so on no path, and without that fallback no app's
      shortcuts fired at all. Found by the scenario, not by reading

## 3. The API

- [x] `src/KeyHandler.tsx`: declares combinations, reports presses, re-registers
      when the list changes, unregisters on unmount
- [x] `setHandledKeys`/`clearHandledKeys` on BasaltWindows, and a `basaltKey`
      device event, beside the drop ones and for the same reasons
- [x] Types matching react-native-macos' shape: `key` plus `altKey`, `ctrlKey`,
      `metaKey`, `shiftKey`

## 4. Tests

- [x] `BASALT_TEST_KEY`, on all three hosts. Parsed once in core; six tests for
      the syntax, including a literal `+` as a key, which an editor binds for zoom
- [x] A scenario asserting all four, verified to fail by registering the list
      once: `got: ['m', 'z+meta']`, no `j`. Green on AppKit and on GTK
- [x] A focused `<TextInput>` keeps its keys -- *not* a scenario, and the spec
      delta says why: a field's peer consumes the key where it sits, so the
      ancestor's handler is never reached and nothing asks about its claim.
      BASALT_TEST_KEY enters after the translation and walks the path itself, so
      it would find a claim a real press never offers -- an assertion with it
      would lie in one direction or the other

## 5. Not built

- [ ] `onKeyUp` and `keyUpEvents`. Same registry, same reporting, a second signal
      per host. An app that binds a key-up gets silence today

## 5. Records

- [x] `docs/backlog/input.md`: no longer a decision
- [x] `docs/TESTING.md`: the new instrument, and what it does not cover
- [x] Whether kino's `hotkeyViewProps()` can keep its shape, recorded either way.
      **It can.** `src/overrides/View.tsx` accepts react-native-macos's
      `keyDownEvents`, `onKeyDown` and `focusable` on any view, so the app keeps
      one spelling for both platforms. The two go through one implementation --
      `src/useHandledKeys.ts` -- because two spellings that drifted would be
      worse than either.

## Found afterwards, by using it

- [x] A real press reaches a view nothing focused. It did not: `keyDown:` is
      only sent to the first responder, `focusable` makes a view eligible rather
      than focused, and `handledByAny` -- written for exactly this -- sat behind
      a call the press never reached. The window's key monitor now offers it to
      the same walk, and only when nothing of ours is in the responder chain.
- [x] The claim outlives the module carrying it. The reporting seam was cleared
      by a destructor; the platform builds a module per lookup, so the one
      holding it died while five others served. Every shortcut in an app went
      quiet, with nothing failing anywhere.
- [ ] A scenario that presses a *real* key. Everything here is driven by
      `BASALT_TEST_KEY`, which enters below the window system -- which is why
      both of the above passed every check this project had while no shortcut in
      a real app worked. Until something drives the window server, the suite
      cannot tell a working keyboard from a dead one.
- [x] `keyDownEvents` worked and could not be written in TypeScript. The override
      accepts it at run time, and nothing declared it, so a typed app spreading
      react-native-macos's props got an error on a prop that would have worked.
      Found on 2026-10-05 by converting the demo apps, which is the first thing
      here to read these types the way an app does. `src/index.ts` now augments
      react-native's `ViewProps` with it, because the Metro override puts that
      prop on every view in every basalt app.

      Only that prop. `onKeyDown` is already declared by react-native with a
      different event, and an interface augmentation may add members but not
      retype them, so an app wanting the `{nativeEvent}` shape still casts. If
      the two spellings ever need to be typed as one, that is the obstacle.
