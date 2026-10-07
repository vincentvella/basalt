# TextInput

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (7):**

1. ~~`autoFocus` does nothing, and nothing had ever asked it to~~
2. A controlled field's value is applied by heuristic rather than from state
3. The synthetic typing instruments cannot observe either event on GTK
4. No shared focus registry: TextInput
5. blur grabs focus for the window rather than dropping it, because GTK models fo
6. placeholderTextColor, selectionColor and cursorColor are parsed and ignored **
7. src/overrides/TextInput
8. autoCapitalize, autoCorrect, spellCheck, keyboardType, returnKeyType, clearBut

- ~~**An uncontrolled field loses what was typed into it.**~~ Found on Windows
  in phase 46 and fixed on all three in phase 47. React Native's `TextInput.js`
  sends `text={value ?? defaultValue}`, so an uncontrolled field with no default
  sends `undefined`: the empty string by the time it is C++, indistinguishable
  from a controlled field that was just cleared, and it re-sends
  `mostRecentEventCount` on every change, which is an Update mutation on its
  own. Applying the prop whenever it differed from the widget therefore wiped
  the field on the user's second keystroke. It is now applied when the *prop*
  changes, because a controlled field's value changes as the user types and an
  uncontrolled one's never does, and a prop older than the last keystroke is
  dropped without being forgotten. Nobody had noticed because `e2e/input.tsx`
  asserts on its *controlled* field.

- ~~**`autoFocus` does nothing, and nothing had ever asked it to.**~~ Fixed
  2026-10-07. No app in `e2e/` used the prop until the day before, when one was
  added to give a menu role something to act on, and it did not focus the field:
  `BASALT_TEST_TYPE` reported "no text field has focus".

  The cause was worse than the first guess. All three hosts read the prop under
  `inserted`, which is `try_emplace`'s "first time this manager saw this tag", not
  "this view was just put in its parent". `MountingWalk::create` applies props
  immediately after `createView` and before any Insert, so on AppKit
  `view.window` was nil and `makeFirstResponder:` was a message to nil, and on GTK
  the widget had no `GtkRoot` for `gtk_widget_grab_focus` to use. And because
  `inserted` is false by the time the Insert arrives, it never got a second
  chance. Two independent reasons it could not work.

  Each host now records the tag and focuses it from a `flushAutoFocus()` called at
  the end of the transaction, which is where `resolveRefreshControls` already
  lives for the same reason: Fabric inserts a subtree bottom-up, so even "just
  after insertChild" is too early. The call each one makes is the one its `focus`
  command already made, which worked because JavaScript asks later.

  Windows was the one host where it might have worked by accident, an EDIT being a
  child of the real top-level window from creation. It is deferred there too, so
  that the host nobody can test locally is not also the host that does it
  differently.

- **A controlled field's value is applied by heuristic rather than from state.**
  The fix above works; the shape iOS uses is better. It reads the shadow
  **state** rather than the prop, and writes the typed text into that state, so
  applying it back is a no-op and no heuristic is needed. That means a platform
  writing `TextInputState`, which none of these three do.
- ~~**No `multiline`.**~~ ~~**`onKeyPress` and `onSelectionChange` are never
  emitted.**~~ ~~**No `selection` prop.**~~ All three are done on GTK and
  AppKit, and were done some phases ago, these entries described the state
  when the section was written and were never revisited. Found by checking the
  section against the code rather than reading it.

  **Windows was the gap and now emits both.** `onKeyPress` comes off WM_CHAR,
  with 'Enter' and 'Backspace' by name and unprintable keys sending nothing,
  the same contract the other two follow. `onSelectionChange` has no
  notification to hang on, because EN_SELCHANGE belongs to RichEdit and a plain
  EDIT says nothing, so the caret is read after any message that could have
  moved it and compared with the last reported value.

  Still missing on Windows: **multiline**, which wants an `ES_MULTILINE` EDIT
  as the peer.

- **The synthetic typing instruments cannot observe either event on GTK**, and
  that is worth knowing before anyone tries to test them. `BASALT_TEST_TYPE`
  inserts through `gtk_editable_insert_text` on GTK and through the field editor
  on AppKit, both deliberately, because a synthesised key needs a seat an
  automated run does not have. So no key is ever sent on those two, and on GTK
  `notify::cursor-position` does not fire for a programmatic insert either:
  verified by instrumenting the handler, which is connected to a real GtkText
  with no warning and simply never runs.

  AppKit's field-editor insert *does* move the caret observably, and Windows
  sends real WM_CHARs, so between them the end-to-end suite covers both events,
   just never on the same host.
- No shared focus registry: `TextInput.State.currentlyFocusedInput()` does not
  exist, and nothing else can ask what has focus. React Native's own
  `TextInputState` module talks to a TurboModule this platform does not have.
- `blur` grabs focus for the window rather than dropping it, because GTK models
  focus as moving, not as absent. The `onBlur` event is still correct.
- `placeholderTextColor`, `selectionColor` and `cursorColor` are parsed and
  ignored **on GTK and Windows**. GtkText takes those from CSS, not from a
  `PangoAttrList`, and this platform has no per-widget CSS provider. AppKit
  honours the placeholder colour, so this is two hosts of three rather than all
  of them.
- `src/overrides/TextInput.js` is a fork of React Native's component, and the only fork
  in the tree. Every prop upstream adds is a prop it will not have.
- `autoCapitalize`, `autoCorrect`, `spellCheck`, `keyboardType`,
  `returnKeyType`, `clearButtonMode`, `selectTextOnFocus` and
  `clearTextOnFocus` are ignored.
