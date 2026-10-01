# desktop-notifications Specification

## Purpose

Showing a system notification, and the identity a desktop requires before it
will show one.

## Requirements

### Requirement: An app can present a notification

The system SHALL present a notification with a title and body through the
platform's own notification service, and SHALL report the permission state.

#### Scenario: A presented notification reaches the system

- **WHEN** an app presents a notification and permission has been granted
- **THEN** the platform's notification service shows it

#### Scenario: Permission is asked for rather than assumed

- **WHEN** an app requests permission
- **THEN** the answer reports whether it was granted

### Requirement: An application must have an identity to notify

The system SHALL give the host an application identity before notifying, because
every desktop keys notifications on one.

macOS raises rather than returns without a bundle identifier, and will not grant
permission to an unsigned bundle at all. Windows requires an AppUserModelID and
a Start Menu shortcut carrying it.

#### Scenario: A packaged macOS app can notify

- **WHEN** an app is packaged with an identifier and an ad-hoc signature
- **THEN** it can request permission and present notifications

#### Scenario: A Windows app registers before notifying

- **WHEN** the Win32 host starts
- **THEN** it has an AppUserModelID and a shortcut carrying it

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
