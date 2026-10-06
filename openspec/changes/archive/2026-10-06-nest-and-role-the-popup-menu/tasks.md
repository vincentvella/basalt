# Tasks

## The model

- [x] `MenuEntry` gains `role` and `submenu`, and `isParent()` beside
      `isSeparator()`.
- [x] `walkMenuEntries` and `menuEntryAt` in core/PlatformServices.h: one
      definition of the pre-order numbering, so the hosts and the test seam
      cannot drift from each other.
- [x] `menuEntryCount`, which is what a valid index is bounded by once anything
      nests.
- [x] `menuEntryShown`, and the rule that a dropped entry is still counted.
- [x] `entriesFrom` reads both fields, recursively, and filters nothing.

## The hosts

- [x] AppKit: a recursive builder, submenus, and roles through
      `[NSApp sendAction:to:nil]` reusing `selectorForRole` from the bar.
- [x] GTK: nested GMenu models, and roles for the first time on Linux. Clipboard,
      selection and undo over `gtk_widget_activate_action` on the focused widget,
      which basalt's own `<TextInput>` answers, being a GtkText or a
      GTK_TYPE_TEXT_VIEW subclass. Window roles over the window's own API.
- [x] Win32: nested HMENUs with MF_POPUP, and roles through
      `win32::handleMenuCommand`, which already posts WM_COPY and friends to
      whatever has focus. A second caller, not a second implementation.
- [x] The portability probe answers no to every role, which is what a platform
      that cannot show a menu performs in one.

## JavaScript

- [x] `ContextMenuItem` gains `role` and `submenu`, documented including which
      roles each desktop lacks.
- [x] `itemAt` walks the tree to find what an index named, replacing an array
      index that only worked when the menu was flat.

## Tests

- [x] The demo app grows a submenu, a `selectAll` role, an `about` role and an
      ordinary item after it. Everything new goes after Delete, because the suite
      names index 2 for Rename.
- [x] Scenarios: a submenu's child answers with its own index and runs its own
      handler; a parent answers as a dismissal; the item after a dropped role
      answers with the same index on all three; `about` is choosable on macOS and
      absent elsewhere.
- [x] The spec says all of it, including that an index is into the list the app
      passed.

## Found while doing it

- [x] The test seam bounded a scripted answer by `entries.size()`, the top level
      only, so any index inside a submenu would have been refused as past the end
      and answered as a dismissal. It bounds by `menuEntryCount` now, and refuses
      a parent, a separator and an entry the platform would not have drawn, so
      what a script can answer is what a person could have chosen.
- [x] Dropping unsupported roles in `entriesFrom` was the first attempt and it was
      wrong: the native side would have numbered the filtered list while
      JavaScript numbered the original, so on Linux every item after an `about`
      would have run the handler of the one before it. Caught by writing the
      scenario that names the index after the dropped item, which is why that
      scenario exists.
- [x] `performMenuRole` from the test seam ran on the JavaScript thread, because
      `showContextMenu` is a TurboModule call. AppKit from the wrong thread does
      not return: the symptom was `about` opening a panel and the promise never
      settling at all. It hops with `postToUiThread` now. The three hosts were
      already right, each performing the role inside its own UI-thread block.

## Covered afterwards

- [x] That a role actually changes anything, which this first shipped without.
      The menu now carries a `close` role at index 11, and the scenario "a role in
      a context menu performs it, not only reports it" drives it twice: choosing
      the role ends the host in about two seconds, and choosing the ordinary item
      beside it leaves it running to the eight-second quit timer. The control run
      is the half that matters, since a host dying on startup would also exit
      early.

      Verified by breaking it on purpose: with `performMenuRole` stubbed to
      return, the scenario fails and says the index came back and the platform did
      nothing. With it restored it passes. An assertion that has never failed is
      not known to discriminate.

      What that proves is the path -- `performMenuRole` reaching the platform on
      the UI thread -- which every role shares.

- [ ] A *text* role's behaviour specifically: `copy`, `selectAll`, `paste`. Those
      act on whatever has focus, and no instrument here can give a field focus
      before a tap opens the menu: the host threads one clock through its scripted
      input and `BASALT_TEST_FOCUS` runs after `BASALT_TEST_TAP`, so the tabs land
      after the menu has been answered. `autoFocus` looked like the way round it
      and does nothing at all, which is now its own entry in
      backlog/textinput.md.

      Two further things found while trying, both worth knowing before anyone
      tries again: tabbing into a field selects its contents on macOS, so
      `selectAll` is a no-op there and `onSelectionChange` does not fire for it
      (the handler drops a selection equal to the last reported one); and the GTK
      typing instrument inserts at the end through `gtk_editable_insert_text`
      rather than replacing the selection, so "type over the selection" is not an
      observable that works on both. The way in is probably a focus instrument
      that runs before the taps, or an app that opens its menu from `onFocus`.
