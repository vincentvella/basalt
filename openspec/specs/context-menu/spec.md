# context-menu Specification

## Purpose

The menu that pops up where you press. Every desktop has one, including the one
with no menu bar, which makes it the more portable of the two kinds.

## Requirements

### Requirement: An app can show a context menu at a point

The system SHALL show a native popup menu from a list of items, separators,
disabled items and nested submenus, positioned at a point in the window's
coordinates, or at the pointer when no point is given.

#### Scenario: A menu opens and reports the item chosen

- **WHEN** an app shows a menu and the person chooses the third entry
- **THEN** the call answers with that entry's index
- **AND** indexes count separators, so they line up with the list passed in

### Requirement: An item may open a submenu

The system SHALL draw an item carrying nested items as a submenu, on every host.

An index SHALL be the item's position in a pre-order walk of the whole menu: an
item, then its children, then the next item, counting separators and parents. For
a list with nothing nested that is the position in the list, which is what an
index has always meant here.

A parent SHALL NOT be choosable. Choosing it opens its children.

#### Scenario: An item inside a submenu is chosen

- **WHEN** the person chooses the first item of a submenu whose parent is the
  sixth entry of the menu
- **THEN** the call answers with 6, the parent being 5
- **AND** that item's handler runs rather than the parent's

#### Scenario: A parent is not an answer

- **WHEN** the person opens a submenu and dismisses the menu without choosing a
  child
- **THEN** the call answers with null, as any other dismissal does

### Requirement: An item may carry a tick or a radio mark

The system SHALL draw a mark beside an item that asks for one, on every host, and
SHALL treat a marked item as an ordinary item in every other respect: drawn,
choosable, counted in the numbering, and answering with its own index.

The kind SHALL be the caller's to say rather than inferred, because one host
cannot infer it: a GMenu item carries no mark of its own and GTK draws a tick or a
circle according to the shape of the action behind it.

A radio group SHALL be a run of adjacent radio items, ended by a separator or by
any other kind of item, and SHALL need no group name. Exclusivity is the caller's:
the menu is built fresh each time it opens, so the system draws the marks it was
given.

The mark for a chosen member of a group MAY differ between desktops. macOS draws a
tick, `NSMenu` having no radio item and a tick being what its guidance prescribes
there. The system SHALL NOT refuse either kind anywhere on that account, all three
being able to mark a chosen item.

Nothing tri-state SHALL be offered, one desktop having a mixed state and the other
two having none.

#### Scenario: A marked item is still an item

- **WHEN** an app shows a menu whose fourth entry carries a tick
- **THEN** that entry is drawn with a tick
- **AND** choosing it answers with 3 and runs its handler, as an unmarked entry
  would

#### Scenario: A group needs no name

- **WHEN** an app shows three adjacent radio items, the second of them chosen
- **THEN** they are drawn as one group with the second marked
- **AND** choosing the third answers with the third's own index

### Requirement: An item may name a platform role

The system SHALL accept a role on an item, in Electron's spelling, and SHALL
perform it on whatever has focus when that item is chosen: the standard Cut,
Copy, Paste, Delete, Select All, Undo, Redo, Minimize, Zoom, Close,
Toggle Full Screen, Quit and About of the desktop it is running on.

A role SHALL still report its index and run the item's handler, so that an app
gets the platform's behaviour and hears about it too.

A role this desktop cannot perform SHALL be left out of the menu rather than
drawn doing nothing, and SHALL keep its index. An index is into the list the app
passed, so an item vanishing from the count would move every index after it onto
the wrong handler.

The three do not agree on the set, and the system SHALL NOT pretend otherwise:
macOS has all thirteen; Linux and Windows have twelve, both lacking `about`,
neither having a platform about panel to ask for.

A role SHALL NOT be the one way out of an application that asked to intervene: a
`quit` role goes through the same refusal a window's close button does.

#### Scenario: A role is performed and still reported

- **WHEN** the person chooses an item whose role is `selectAll`
- **THEN** the platform selects the text in the focused field
- **AND** the call answers with that item's index and runs its handler

#### Scenario: A role this desktop lacks is absent but still counted

- **WHEN** a menu carries an item whose role is `about` and an ordinary item
  after it
- **THEN** that item is absent on a desktop with no about panel
- **AND** the ordinary item after it answers with the same index it would have
  on a desktop that has one

#### Scenario: A dismissed menu is told apart from a chosen one

- **WHEN** the menu is dismissed without a choice
- **THEN** the call answers with null rather than an index

#### Scenario: The chosen item's handler runs

- **WHEN** an entry carrying a handler is chosen
- **THEN** that handler runs before the call answers

### Requirement: A secondary click asks for a context menu

The system SHALL deliver a secondary or middle click as a pointer event carrying
which button it was, and SHALL NOT deliver it as a press.

A secondary click is not an activation on any desktop: it must not reach the
responder system, a gesture handler, or `onPress`. This is what lets one view be
both a button and a context-menu target.

#### Scenario: A right-click reports its button

- **WHEN** the person right-clicks a view
- **THEN** the view's pointer-down handler runs with the secondary button number

#### Scenario: A right-click does not press what it lands on

- **WHEN** the person right-clicks a pressable view
- **THEN** that view's press handler does not run
