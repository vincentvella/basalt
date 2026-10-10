# TextInput

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (4):**

1. ~~`autoFocus` does nothing, and nothing had ever asked it to~~
2. A controlled field's value is applied by heuristic rather than from state
3. The synthetic typing instruments cannot observe either event on GTK
4. No shared focus registry: TextInput
5. ~~`blur` grabs focus for the window rather than dropping it~~
6. ~~placeholderTextColor, selectionColor and cursorColor are parsed and
   ignored~~ on all three hosts, except the Windows selection highlight
7. src/overrides/TextInput
8. ~~returnKeyType, clearButtonMode, selectTextOnFocus and clearTextOnFocus are
   ignored~~: the last two are done on all three hosts, and the first two are
   recorded as deliberately not -- a soft keyboard's label and a control iOS
   draws. ~~autoCapitalize, autoCorrect, spellCheck and keyboardType~~ are done
   on all three as well
9. ~~`autoFocus` selected the field's text, on two hosts, for the same reason~~
10. ~~`TextInputProps` and its traits have no rows on the support page~~

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

  **`multiline` and the `selection` prop followed on 2026-10-10**, so all three
  hosts now have every one of these. The Windows peer is rebuilt when the prop
  changes, `ES_MULTILINE` being read at creation and not settable afterwards --
  which is the same reason the GTK host swaps a `GtkText` for a `GtkTextView`
  -- and Enter in one inserts a newline rather than submitting, React Native's
  default `submitBehavior` for a multiline field. backlog/platform-windows.md
  has what that cost: a full-height child window paints square corners over a
  rounded background.

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
- ~~`placeholderTextColor`, `selectionColor` and `cursorColor` are parsed and
  ignored **on Windows**~~. Done on GTK 2026-10-07, **on AppKit 2026-10-10**,
  and **on Windows the same day** for two of the three.

  **This entry said AppKit already honoured the placeholder colour, and that was
  wrong**, found on 2026-10-09 when every `TextInput` prop got a row on the
  support page and the audit asked which host reads which.
  `AppKitTextInput.mm` painted `NSColor.placeholderTextColor`, which is the
  *system's* placeholder grey, and never read `props->placeholderTextColor`. An
  app asking for a red placeholder got grey, and the entry made that look done.

  **What AppKit took was the shape this entry predicted.** The placeholder is an
  attributed string, so its colour is a line beside the font that was already
  set. The caret and the selection are not: they are
  `NSTextView.insertionPointColor` and the background colour inside
  `selectedTextAttributes`, and an `NSTextField` has no text view of its own --
  it borrows the window's shared field editor while it is focused. So those two
  are remembered on the peer and applied in `becomeFirstResponder`, which is
  exactly where `spellCheck` already had to be applied for the same reason. One
  key of `selectedTextAttributes` is replaced rather than the dictionary, the
  rest of it being AppKit's, including how a selection dims when its window
  loses focus.

  Two things that would have been bugs. `SharedColor`'s unset value is zero, so
  each colour crosses as a pointer that is nil when the app asked for nothing --
  otherwise every caret is black and every placeholder transparent. And
  `cursorColor` falls back to `selectionColor`, which is React Native's
  documented contract rather than an invention: `selectionColor` is "the
  highlight, selection handle and cursor color", and `cursorColor` overrides the
  caret alone.

  Six tests, five of which fail with the colours not applied -- checked by
  sabotage -- and the sixth is the negative control: a field that asked for
  nothing keeps the system grey. One focuses a field in a real window and reads
  the field editor's own two properties back, which is the half the remembering
  exists for.

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

  **Windows was the harder half, and two of the three are done**, 2026-10-10.
  The shape of it is that an `EDIT` has a property for none of them, so each one
  is either drawn by this host or arranged out of something else.

  **The placeholder is drawn here.** `EM_SETCUEBANNER` is Windows' placeholder
  and takes no colour, so a field that named one is given an empty banner and
  the subclass draws the text itself: the control's own font, the asked-for
  colour, `SetBkMode(TRANSPARENT)` so the background stays the app's, inside the
  rectangle `EM_GETRECT` reports -- the formatting rectangle rather than the
  client area, because an `EDIT` insets its text by a margin of its own and a
  placeholder that ignored it would not start where the text it stands in for
  starts. A field that named no colour keeps the banner, which is the system's
  grey and the right answer for an app that asked for nothing.

  Drawn from `WM_PRINTCLIENT` as well as `WM_PAINT`, which is what makes it
  testable: `BASALT_SNAPSHOT` cannot see a `<TextInput>` on this host at all,
  the Direct2D snapshot rendering views rather than child windows, so the test
  renders the control into a memory DC the documented way and counts the red
  pixels. That is also a hint for the snapshot entry above.

  **The caret is a bitmap, and the colour is arithmetic.** `CreateCaret` says
  "the caret is drawn to the screen via the XOR operation", which is how a caret
  nobody coloured is visible against any background and why one that *was*
  coloured cannot simply be asked for. So the bitmap carries the asked-for
  colour XOR the ground it will be drawn against, and the system's XOR cancels
  the ground back out; the ground is known, being the same `backgroundColor` or
  `COLOR_WINDOW` that `controlColor` answers `WM_CTLCOLOREDIT` with. Installed
  from `WM_SETFOCUS` *after* the control has had it, because an `EDIT` creates
  its own caret there and `CreateCaret` replaces whatever shape came before.
  Where the caret crosses a glyph the XOR gives a third colour, exactly as the
  system's own caret does, so that is the platform's caret rather than a
  shortcut taken here.

  The one thing no test here can answer is what that looks like on a screen:
  these run headless and nothing can read a caret back -- there is no
  `GetCaret`. What is asserted is the bitmap the manager installed, read out of
  GDI with `GetDIBits`, and the XOR is the documented behaviour rather than a
  measurement of this repository's. A pair of eyes on a real desktop is what
  would close that, and nothing in CI can stand in for it.

  **`selectionColor` is the one left**, and it is `partial` rather than `no`
  because it does colour the caret: React Native's contract is that
  `selectionColor` is "the highlight, selection handle and cursor color" and
  `cursorColor` overrides the caret alone, so a field that named only the first
  gets a caret of it, as on both other hosts. What it does not get is the
  highlight. A classic `EDIT` draws its selection in `COLOR_HIGHLIGHT` and no
  message changes that; it is not owner-drawable either, there being no
  `ES_OWNERDRAW`.

  What would reach it is a **windowless RichEdit**: `CreateTextServices` hands
  back an `ITextServices` that draws into a device context of the host's, and
  the host implements `ITextHost`, whose `TxGetSysColor(int)` the editor asks
  for the colours it draws with -- documented as taking any `GetSysColor` index
  and as being allowed to answer differently from the system, with a warning
  about Accessibility for exactly that reason. Whether msftedit asks for
  `COLOR_HIGHLIGHT` specifically is not something this repository has measured,
  and the documentation does not enumerate the indices, so that is the thing to
  check first rather than the thing to assume. It is also a much larger change
  than a colour: a windowless editor has no HWND, which is most of the three
  consequences `Win32TextInput.h` opens with, and would want its own entry.
- `src/overrides/TextInput.js` is a fork of React Native's component, and the only fork
  in the tree. Every prop upstream adds is a prop it will not have.
- **`autoCapitalize`, `autoCorrect`, `spellCheck` and `keyboardType` are done
  on GTK and AppKit**, 2026-10-09, **and on Windows 2026-10-10**. Of the four
  beside them, **`selectTextOnFocus` and `clearTextOnFocus` are done on all
  three, 2026-10-10**; `returnKeyType` and `clearButtonMode` are still ignored
  everywhere.

  **The two focus props are three calls and one rule.** Each host already had
  the moment focus arrives -- a `GtkEventControllerFocus`, `becomeFirstResponder`
  and `EN_SETFOCUS` -- so the work was what to do there: clear the text with the
  host's own setter, and select everything with `rn_peer_select_region`,
  `RnPeerSetSelection` or `EM_SETSEL`. The rule is the order, which is the same
  on all three: **clear before select**, because selecting what has just been
  cleared selects nothing and clearing what has just been selected throws the
  selection away.

  The clear is *reported*, through the same path a keystroke takes. A controlled
  field cleared behind JavaScript's back is a field that puts the old value back
  on the next unrelated render with the user having done nothing, which is worse
  than not clearing.

  Two host-specific notes, both of which came out of the earlier `autoFocus`
  work. On AppKit, `selectTextOnFocus` is **not observable on the user-focus
  path**: an NSTextField selects everything on becoming first responder whatever
  the prop says, so a test that focused a field and found it selected passes with
  the prop removed -- established by sabotage, since the first version of that
  suite had exactly that test. What the prop changes there is the *programmatic*
  path, where `flushAutoFocus` collapses the selection on purpose, and that is
  where the AppKit suite asserts it from both sides. On GTK, selecting text is
  what `grabFocusForAutoFocus` avoids, because `gtk_widget_grab_focus` on an
  unmapped GtkText claims the PRIMARY selection and blocks; this runs from the
  focus-in signal with the main loop already going, which is the same place the
  controlled `selection` prop selects from.

  Fifteen tests across the three hosts. The GTK ones go through a public
  `handleFocus(tag)`, which mirrors the AppKit manager's: that suite cannot
  focus a widget at all -- no realised window -- so the seam is where the
  behaviour is asserted, and the AppKit suite, which can focus, exercises the
  same seam from the other end.

  What the four took, and what each toolkit really has, is in
  backlog/platform-macos.md: GTK has one input-hint bitmask that needs
  read-modify-write and no hint for `autoCorrect`; AppKit has
  `continuousSpellCheckingEnabled` and `automaticSpellingCorrectionEnabled` on
  the text view a field borrows, and no form of `keyboardType` at all, which is
  reported in the tree rather than pretended.

  **Windows honours one and a half of the four**, which is the most that host
  has, and the support page says which. `keyboardType` becomes an *input
  scope*: `SetInputScope` is the Windows equivalent of GTK's input purpose, read
  by the touch keyboard and by any text-services IME, so an `email-address`
  field gets the keyboard with the @ on it. The call is resolved with
  `GetProcAddress` rather than linked, because the Windows SDK ships no
  `msctf.lib` to import it from -- measured by a linker that could not open
  one -- which also means a system without the export gets no scope rather than
  failing to start. `autoCapitalize` becomes
  `ES_UPPERCASE` and only for `characters`, which that style does exactly;
  `words` and `sentences` need to know where a word or a sentence begins, which
  an `EDIT` does not, and upper-casing everything for `sentences` would be worse
  than leaving the text alone. `spellCheck` and `autoCorrect` have nothing at
  all there: Windows keeps spell checking in `ISpellChecker`, which checks
  strings and draws nothing, so honouring either would mean drawing the
  squiggles and offering the menu -- a text editor rather than a prop. Both are
  carried into the tree dump so an app can see they arrived, which is what the
  end-to-end scenario now reads on all three hosts.

  The support page added a row per trait on 2026-10-09, which is how the rest of
  this list stopped being a sentence: ~~`contextMenuHidden`~~, ~~`caretHidden`~~,
  `scrollEnabled`, `submitBehavior`, `onKeyPressSync`, `onChangeSync` and
  `acceptDragAndDropTypes` are the ones that read "not yet" there rather than
  "deliberately not", and each was a desktop question nobody had answered.

  **The first two are answered, 2026-10-10**, and they went in by three
  different routes each, which is the useful part.

  `caretHidden` is done everywhere. GTK already had the stylesheet the three
  colour props use, so it is a `caret-color` with a zero alpha -- transparent
  rather than absent, an absent rule being the theme's visible caret. AppKit
  already had a remembered caret colour for the field editor, so it is
  `NSColor.clearColor` through the same channel. **Windows gets it from the
  arithmetic**: a caret's colour there is a bitmap the system XOR-es onto the
  field, and anything XOR zero is itself -- so a caret of black bits is
  installed and invisible. On all three the hiding wins over `cursorColor`, a
  field having asked for the more specific thing, and each has a test for that.

  `contextMenuHidden` is `partial` on two of the three, and the limits were
  measured rather than guessed.

  - **Windows is complete.** A right-click, the Menu key and Shift+F10 all
    arrive as `WM_CONTEXTMENU`, so the subclass swallows one message and covers
    every route.
  - **GTK covers the pointer and not the keyboard.** A capture-phase
    `GtkGestureClick` on the secondary button claims the sequence before
    GtkText's own gesture sees it, which is the documented way to stop another
    gesture. The Menu key and Shift+F10 are bound *inside* GtkText and
    GtkTextView to an action nothing outside them has a handle on.
  - **AppKit covers a multiline field and not a single-line one.** The multiline
    peer is this project's own `NSTextView` subclass, so `-menuForEvent:`
    returning nil is the whole of it. A single-line field is different: the menu
    belongs to the window's shared *field editor*, which is a plain `NSTextView`
    nobody here subclasses, and it rebuilds its menu inside `-menuForEvent:` --
    assigning `menu = nil` on it does not stop it, which a probe test
    established before the comment was written. What would close it is supplying
    the field editor through `-windowWillReturnFieldEditor:toObject:`, which is
    the documented hook for exactly that and is a window-level change rather
    than a prop-level one.

  Both are in each host's tree dump, as `caret=hidden` and
  `context-menu=hidden`, which is the only thing the cross-host diff can read:
  each prop is the absence of something. That is also as far as the Windows unit
  suite goes for the menu -- sending `WM_CONTEXTMENU` to a field that does *not*
  hide it would run the EDIT's own handler, and `TrackPopupMenu` is a nested
  message loop that does not return until somebody dismisses the menu, so the
  control case would hang the run rather than fail it.
  `enablesReturnKeyAutomatically`, `keyboardAppearance`, `clearButtonMode`,
  `dataDetectorTypes`, `textContentType`, `passwordRules`, `smartInsertDelete`,
  `inputAccessoryViewID` and `disableKeyboardShortcuts` are iOS's own, and
  `showSoftInputOnFocus` wants a soft keyboard a desktop does not have.

  **`returnKeyType` moved from "not yet" to "deliberately not" on 2026-10-10**,
  which is a decision rather than work: it is the *label* on a soft keyboard's
  return key -- Go, Search, Send -- and no desktop has a soft keyboard to label.
  What the key itself does is already honoured everywhere, `onSubmitEditing`
  firing on Return and `submitBehavior`'s multiline default inserting a newline
  instead. A row saying "not yet" invited somebody to look for the call, and
  there is none to find on any of the three.

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

- ~~**`TextInputProps` and its traits have no rows on the support page.**~~ Done
  2026-10-09: forty-five rows across `BaseTextInputProps`, the iOS
  `TextInputProps` this platform reuses, and `TextInputTraits`. Reading the
  traits needed one change to the scrape, since that class has no
  `#pragma mark` region to find its fields by and is read as a whole class
  instead.

  **Two things the rows found**, which is twice now that a scraped table has
  corrected this file: the entry about the placeholder and caret colours claimed
  AppKit already honoured the placeholder, and it paints the system grey; and
  entry 8's list of eight ignored props had four of them done since earlier the
  same day. Both are corrected above.

  Which host reads which was measured rather than assumed, by grepping each
  host's text-input file for the prop: the hosts read the *trait* rather than
  `BaseTextInputProps`' own `autoCapitalize` and `editable`, which is why those
  two rows say "deliberately not" and point at the traits beside them.

  What the entry said when it was open, kept because the argument is the
  reusable part: the page scraped four prop structs plus Yoga's and neither
  `BaseTextInputProps` nor the `TextInputTraits` beside it was one of them, so
  the thirty-odd props an app writes on a `<TextInput>` had no row at all.
  Several of them were done and some deliberately not, which is exactly why the
  rows were worth having: `maxLength`, `multiline`, `spellCheck`, `autoCorrect`,
  `autoCapitalize` and `keyboardType` landed between 2026-10-05 and 2026-10-09,
  two of them as "no macOS call at all" rather than as work. A reader could not
  tell which from this file, and that is what a table is for.

  `BaseTextInputProps.h` has the same `#pragma mark - Props` shape the four
  scraped structs have, so the scrape is one entry in `STRUCTS`. The judgement
  per field is the work, and it wants doing after the Image half of the same
  job: backlog/image.md entry 5 has the shape of it.