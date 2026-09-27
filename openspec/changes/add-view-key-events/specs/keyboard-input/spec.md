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

#### Scenario: A changed declaration takes effect

- **WHEN** a view's declared combinations change as the application re-renders
- **THEN** the new combinations are reported and the removed ones are not
