# window-geometry Specification

## MODIFIED Requirements

### Requirement: An app can read and change its window's geometry

The system SHALL let an app set the window's size, set its position, centre it,
make it full screen, minimise it, toggle maximised, and close it.

Size SHALL be a request rather than a command: a tiling window manager may
refuse, so the reported bounds are what happened and not what was asked for.

Every mutation SHALL be applied on the UI thread, because no toolkit here may be
touched from the JavaScript thread.

**Geometry SHALL be addressed by window.** An app reading or changing geometry
from inside a window SHALL read and change *that* window, whichever window the
person is looking at. An app that names no window SHALL mean the window the
application started in.

#### Scenario: A size request comes back as the size the window got

- **WHEN** an app calls `setSize(700, 500)`
- **THEN** the window resizes and `bounds` reports the size it actually became

#### Scenario: A state change that is not a resize is still reported

- **WHEN** the window goes full screen
- **THEN** `bounds.fullScreen` becomes true
- **AND** this holds on hosts where full screen changes a property or a style
  rather than the size

#### Scenario: A second window reads its own size

- **WHEN** an app opens a second window 600 by 400 and reads the bounds from
  inside it, while the main window is 900 by 700
- **THEN** it reads 600 by 400
- **AND** it reads them whether or not the second window is the active one

#### Scenario: A second window resizes itself and not the other

- **WHEN** an app inside a second window calls `setSize`
- **THEN** that window resizes
- **AND** the main window's bounds are unchanged

### Requirement: Bounds are live

The system SHALL re-render an app reading `bounds` whenever the window is
moved, resized, maximised or made full screen, by any cause: the person, the
window manager, or the app.

A change SHALL re-render the app reading the bounds of **the window that
changed**, and SHALL NOT re-render one reading another window's.

#### Scenario: Dragging a corner updates the app

- **WHEN** the window is resized by anything at all
- **THEN** the app that read `bounds` renders with the new numbers

#### Scenario: One window moving leaves the other alone

- **WHEN** the main window is resized while a second window is open
- **THEN** the second window's `bounds` are unchanged and report its own size
