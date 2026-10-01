# Spec Delta

## ADDED Requirements

### Requirement: Notifications ship as their own package

The system SHALL provide notifications in a package separate from
`basalt-core`, which an app installs when it wants them.

Notifications are the shipped capability the packaging rule catches: the
operating system asks the person for consent, which is the first of its three
tests. The JavaScript API is already `expo-notifications`, a package, so the
native seam was the only part in the wrong place.

#### Scenario: An app that wants notifications installs them

- **WHEN** an app installs the notifications package and builds a host
- **THEN** its notification module is reachable from JavaScript

#### Scenario: An app that does not, does not pay for them

- **WHEN** an app has not installed the notifications package
- **THEN** the host builds without any notification code in it
