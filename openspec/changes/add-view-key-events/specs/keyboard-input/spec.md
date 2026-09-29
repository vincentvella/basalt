# keyboard-input Specification

## ADDED Requirements

### Requirement: A view can be told about key presses

The system SHALL let a view declare the key combinations it handles, and SHALL
tell it when one of those is pressed.

A combination SHALL be a key name together with the modifiers that must be held.
Key names SHALL be the ones a browser reports, so that `"a"`, `" "`,
`"ArrowLeft"` and `"Escape"` mean what they do elsewhere.

A key the declaring view did not claim SHALL continue to whatever would have
received it otherwise.

#### Scenario: A declared combination reaches the app

- **WHEN** a view declares a combination and that combination is pressed while it
  or a descendant has focus
- **THEN** the view is told, with the key name and the modifiers that were held

#### Scenario: An undeclared key is left alone

- **WHEN** a key is pressed that no view in the focus path declared
- **THEN** the application menu, a focused text field, or a scrollable ancestor
  receives it as though nothing had listened

#### Scenario: Typing is not a shortcut

- **WHEN** a text field has focus and a key is pressed that an ancestor declared
- **THEN** the text field receives it and the ancestor is not told

  Holds by construction rather than by a check, and the distinction is worth
  stating. A text field's peer is a real control on every one of the three
  desktops -- an `NSTextView`'s field editor, a `GtkText`, an `EDIT` window -- and
  it consumes the key where it sits. The ancestor's `keyDown:`, `key-pressed` or
  `WM_KEYDOWN` is therefore never reached, so nothing asks whether the ancestor
  claimed it.

  **`BASALT_TEST_KEY` cannot check this.** It enters after the platform
  translation, so it walks the focus path itself and would find the ancestor's
  claim that a real press never offers it. An assertion written with that
  instrument would fail while the behaviour was correct, or pass while it was
  not, depending on which way the instrument was made to lie. Testing it needs a
  real keystroke reaching a real field, which is the thing no automated run on any
  of these desktops can produce -- the same limit `BASALT_TEST_TAP` has and says
  so about.

#### Scenario: A changed declaration takes effect

- **WHEN** a view's declared combinations change as the application re-renders
- **THEN** the new combinations are reported and the removed ones are not

### Requirement: A declared shortcut works without anything being focused

The system SHALL deliver a declared key to the view that claimed it even when
nothing in the application has keyboard focus.

This is the ordinary case rather than an edge one: an application that declares
its shortcuts once, on a view near the root, has nothing focused until the person
clicks something. A platform that only delivers to the focused view delivers
nothing at all, and does it silently -- the keys are claimed, the press is
matched, and the app never hears.

#### Scenario: A press with nothing focused

- **WHEN** a key a view declared is pressed and no view has focus
- **THEN** the view that declared it is told

#### Scenario: A focused field keeps its keys

- **WHEN** a text field has focus and a key it would type is pressed
- **THEN** the field receives it and no window-level claim takes it

### Requirement: The keyboard props of react-native-macos are honoured

The system SHALL accept `keyDownEvents`, `onKeyDown` and `focusable` on a view,
with the shapes react-native-macos gives them, so that an application written
against that platform declares its shortcuts here without being rewritten.

The callback SHALL receive `{nativeEvent}`, because that is what such an
application reads.

An unrecognised prop is dropped in JavaScript without an error, so a platform
that does not implement these does not fail -- it does nothing, which is
indistinguishable from an application with no shortcuts at all.

#### Scenario: A view declares shortcuts the way react-native-macos does

- **WHEN** an app spreads `keyDownEvents`, `onKeyDown` and `focusable` onto a
  plain view and one of those combinations is pressed
- **THEN** `onKeyDown` is called with the key and modifiers under `nativeEvent`

#### Scenario: A view that declares none costs nothing

- **WHEN** a view is rendered without `keyDownEvents`
- **THEN** it behaves exactly as React Native's own view

### Requirement: A claim outlives the module that carried it

The system SHALL keep delivering declared keys for as long as the application is
running, regardless of how the platform's own native modules are created and
destroyed.

Stated because it was not true: the reporting seam was held by a single module
instance, the platform builds a new one for every lookup, and the instance that
happened to hold it was destroyed while others were still serving. Every declared
shortcut in the application stopped working, with nothing failing and nothing
logged.

#### Scenario: Shortcuts survive the platform's own churn

- **WHEN** an application runs long enough for the platform to build and discard
  several of its internal modules
- **THEN** a declared key pressed afterwards is still delivered

