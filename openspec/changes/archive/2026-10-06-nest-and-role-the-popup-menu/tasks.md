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

- [x] A *text* role's behaviour, 2026-10-07. The way in was not a new instrument:
      it was fixing `autoFocus`, which had never worked and which gives the field
      focus at mount, before any tap. See backlog/textinput.md for what was wrong
      with it.

      The observable is `paste`, and it is `paste` for a reason. `selectAll` is
      unusable: focus already selects the contents on macOS, so the role changes
      nothing and `onSelectionChange` does not even fire, the handler dropping a
      selection equal to the last reported one. Typing over a selection is
      unusable too, the GTK instrument inserting at the end through
      `gtk_editable_insert_text` rather than replacing. `paste` needs neither: the
      app puts a string on the clipboard through basalt's own Clipboard module,
      never writes it into the field, and the field containing it afterwards can
      only be the platform's doing.

      Asserted as a substring of a `field text:` line rather than an exact match,
      deliberately: whether a paste replaces or inserts depends on what focus left
      selected, both hosts here replace, and a freshly focused Windows EDIT has the
      caret at 0 with nothing selected. The role worked either way.

      Verified by stubbing `performMenuRole` to return: the scenario fails and says
      the clipboard was seeded and the platform did nothing with it.

- [ ] `copy` and `selectAll` specifically. The mechanism is the same
      `performMenuRole` the two asserted roles go through, so what is unproven is
      each one's mapping rather than the path. `copy` would need the suite to read
      the system clipboard after the host exits, which on X11 dies with the owner
      unless a clipboard manager is running; `selectAll` needs a field focused
      *without* its contents selected, which no prop here offers, since
      `selectTextOnFocus` is handled on none of the three hosts.
