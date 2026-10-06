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

## Not covered

- [ ] That a role actually changes anything. The index and the handler are
      asserted on all three; the platform's half is not. `selectAll` chosen
      against this repo's own `<TextInput>` produced no `onSelectionChange`, and
      the field would not take a typed character either, so what was not
      established is whether the field was ever focused. The menu bar's roles
      have never been asserted behaviourally either, only that the menu was
      installed with them in it, so this is not a new gap -- but it is the gap
      worth closing next, and closing it needs a scenario that can prove focus.
