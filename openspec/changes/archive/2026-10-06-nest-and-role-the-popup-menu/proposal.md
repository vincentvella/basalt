# A popup menu that nests, and items the platform performs

## Why

`ContextMenuItem` declared `role`, `submenu` and `accelerator`. The native reader
took `label`, `enabled`, `shortcut` and `separator` and nothing else, so three of
those fields did nothing and said nothing. The type was corrected first, in
da88247, which left the honest version: a popup here is a flat list of items the
app implements itself.

The reason given for the flatness was the platforms, and that was wrong. NSMenu
has `submenu`, GMenu has `g_menu_append_submenu`, an HMENU takes `MF_POPUP`. What
actually stopped it was the answer: `show()` resolves with an index, and a tree
has no single position.

Roles were a second gap and a different one. A role is the Copy that actually
copies, which an app cannot implement itself: on macOS the key equivalent never
reaches the field without one. The menu bar has had them since it existed, over
`selectorForRole` on AppKit and `commandForRole` on Win32. A popup had none, and
on Linux, which has no menu bar at all, nothing had any.

## What Changes

- Indexes become a pre-order walk of the whole menu, counting separators and
  parents. For a flat list that is the position in the list, so every caller that
  predates nesting keeps the meaning it had.
- Submenus on all three hosts, built from each one's own nesting primitive.
- Roles in a popup on all three, performed on the UI thread immediately before
  the index is reported, so an app gets the behaviour *and* the callback.
- A role a desktop cannot perform is left out of the menu and keeps its index.
- Linux gets roles for the first time: twelve of the thirteen, over GtkText's and
  GtkTextView's own actions and the window's own API.

## Non-goals

- `about` on Linux or Windows. Neither has a platform about panel to ask for, so
  the window is the app's to show and an ordinary item is how to open it.
- Making the popup and the menu bar one model. They still disagree on purpose:
  the bar answers with an id and is installed once, a popup answers with an index
  and is asked a question. `shortcut` and `accelerator` stay different fields for
  the same reason, one being decoration and the other a binding.
- Checkbox and radio items, and dynamic enabling. Still open; see
  docs/backlog/desktop-capabilities.md.
