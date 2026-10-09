# TextInput

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (7):**

1. ~~`autoFocus` does nothing, and nothing had ever asked it to~~
2. A controlled field's value is applied by heuristic rather than from state
3. The synthetic typing instruments cannot observe either event on GTK
4. No shared focus registry: TextInput
5. ~~`blur` grabs focus for the window rather than dropping it~~
6. placeholderTextColor, selectionColor and cursorColor are parsed and ignored **
7. src/overrides/TextInput
8. autoCapitalize, autoCorrect, spellCheck, keyboardType, returnKeyType, clearBut
9. ~~`autoFocus` selected the field's text, on two hosts, for the same reason~~
10. `TextInputProps` and its traits have no rows on the support page

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
- ~~**`blur` grabs focus for the window rather than dropping it, because GTK
  models focus as moving, not as absent.**~~ Fixed 2026-10-08. The premise was
  wrong: `gtk_window_set_focus` is documented as nullable, and with NULL it
  "unsets the focus widget for the root", which is exactly what `blur` means
  everywhere else. `blur` now does that, falling back to the old behaviour only
  when the widget's root is not a window, which is a test or a widget not yet
  shown.

  The entry was right that `onBlur` fired either way, since the focus controller
  reports a leave on both paths, and that is why nothing had noticed. What was
  observably wrong is what had focus afterwards: the window itself, so a Tab
  after a `blur` started from the window rather than from the top of the tab
  order, and anything asking what was focused got the window instead of nothing.
  `focus_blur_drops_focus_instead_of_handing_it_to_the_window` asserts
  `gtk_window_get_focus` is NULL after the command, in a real window, and fails
  on the old behaviour.
- `placeholderTextColor`, `selectionColor` and `cursorColor` are parsed and
  ignored **on Windows**. Done on GTK 2026-10-07; AppKit already honoured the
  placeholder colour, so Windows is the one host left.

  Not through a per-widget provider, which is what the entry used to propose.
  The only per-widget route is `gtk_widget_get_style_context`, deprecated since
  GTK 4.10 and gone in 5, and this package had already settled that question for
  a `<Switch>`'s track colour. So it follows that pattern: one display-wide
  provider holding a rule per distinct colour combination, with a CSS class on
  the peer named after the colours, so two fields with the same colours share a
  rule and a controlled field re-sending identical props installs nothing.

  The part that would have been a bug: `SharedColor`'s undefined value is zero,
  so passing it through unconditionally paints every caret black. Each colour
  crosses the seam as a pointer that is null when unset, which is what keeps
  "absent" distinguishable from "transparent". `cursorColor` falls back to
  `selectionColor`, which is React Native's documented contract.

  Multiline is the exception: a `GtkTextView` has no `placeholder` CSS node
  because `RnTextView::snapshot` draws the placeholder itself, so the colour is
  held as a value and used there. Selection and caret still arrive by CSS.

  Windows is the harder half and is untouched. A plain `EDIT` has no CSS,
  `EM_SETCUEBANNER` gives no colour control, the selection is the system
  highlight unless the control is owner-drawn or swapped for a RichEdit, and the
  caret is a bitmap you install yourself.
- `src/overrides/TextInput.js` is a fork of React Native's component, and the only fork
  in the tree. Every prop upstream adds is a prop it will not have.
- `autoCapitalize`, `autoCorrect`, `spellCheck`, `keyboardType`,
  `returnKeyType`, `clearButtonMode`, `selectTextOnFocus` and
  `clearTextOnFocus` are ignored.

- ~~**`autoFocus` selected the field's text, on two hosts, for the same
  reason.**~~ Fixed 2026-10-07, after the fix above and separately from it.

  Focusing a field programmatically is not the same as focusing it. `GtkText`
  treats a programmatic focus as keyboard focus and selects all of its text;
  `NSTextField` installs the window's shared field editor on becoming first
  responder and does the same. So `autoFocus` on a field with a `defaultValue`
  left every character selected and armed the next keystroke to replace them,
  which is the opposite of what putting a caret in a field means.

  **The symptom was already written down and nobody had read it as one.** The
  paste role's scenario carried a comment saying that "both hosts here replace"
  the field's contents while Windows inserts at the caret, and explained it as
  the three platforms simply disagreeing. A paste replaces what is selected, so
  that comment was the bug, recorded as a quirk.

  GTK uses `gtk_text_grab_focus_without_selecting`, which exists for exactly
  this. AppKit has no equivalent, so it collapses the field editor's selection
  afterwards, to a caret at the end. Measured rather than assumed: the field now
  reads `select mePASTEDBYROLE` where it used to read `PASTEDBYROLE`.

  Windows needed nothing. Its `EDIT` is not dialog-managed, select-on-focus
  being the dialog manager's behaviour, so a bare `SetFocus` leaves the caret at
  0 and nothing selected.

  Two things found alongside it and worth keeping. **Nothing asserted that
  `autoFocus` took effect on any host**, and the one scenario that observes focus
  raised a skip rather than a failure when no field took it, so a regression on
  any host turned the suite green: that is now a failure, which is safe because
  all three hosts focus today. And **both hosts failed silently**, AppKit
  discarding the `BOOL` from `makeFirstResponder:` and messaging a nil window
  being a no-op; AppKit now says which of the two happened.

- **`TextInputProps` and its traits have no rows on the support page.** The page
  scrapes four prop structs plus Yoga's, and neither `BaseTextInputProps` nor
  the `TextInputTraits` beside it is one of them. So the thirty-odd props an app
  writes on a `<TextInput>` have no row at all: `autoCapitalize`,
  `keyboardType`, `secureTextEntry`, `returnKeyType`, `selectionColor`,
  `maxLength`, `multiline`, `placeholder` and the rest. That is the same hole
  the layout props had until 2026-10-09, which is neither implemented nor
  recorded but absent, with nothing able to say so.

  Several of them are done and some are deliberately not, which is exactly why
  the rows are worth having: `maxLength`, `multiline`, `spellCheck`,
  `autoCorrect`, `autoCapitalize` and `keyboardType` landed between 2026-10-05
  and 2026-10-09, two of them as "no macOS call at all" rather than as work;
  entries 6 and 8 here cover others. A reader cannot tell which from this file,
  and that is what a table is for.

  `BaseTextInputProps.h` has the same `#pragma mark - Props` shape the four
  scraped structs have, so the scrape is one entry in `STRUCTS`. The judgement
  per field is the work, and it wants doing after the Image half of the same
  job: backlog/image.md entry 5 has the shape of it.