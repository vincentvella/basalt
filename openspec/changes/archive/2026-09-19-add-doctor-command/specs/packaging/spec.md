# packaging

## ADDED Requirements

### Requirement: One command reports what a build will need

The system SHALL provide a command that reports whether this machine and this
app can build a desktop host, checking what `init` checks without writing
anything, and additionally what `init` cannot fix: the build toolchain, the
system libraries each desktop needs, and whether the app's React Native is one
`supported-versions.json` lists.

Each failing check SHALL name what is wrong and what to do about it. A check
the command cannot make -- a desktop it is not running on -- SHALL be reported
as unchecked rather than as passing.

#### Scenario: A machine missing a build tool

- **WHEN** the command runs where cmake or ninja is not installed
- **THEN** it reports that tool as missing, and how to install it
- **AND** it exits non-zero without starting a build

#### Scenario: An app on an unsupported React Native

- **WHEN** the command runs in an app whose React Native is not listed in
  `supported-versions.json`
- **THEN** it reports the version it found and the versions that are supported

#### Scenario: A configured app on a ready machine

- **WHEN** everything `init` writes is in place and the toolchain is present
- **THEN** every check reports as already right, and the command exits zero

#### Scenario: Checking changes nothing

- **WHEN** the command runs in an app that `init` has never configured
- **THEN** it reports each step as needing doing
- **AND** neither `package.json` nor the Metro config is modified

### Requirement: An app chooses which desktops it builds for

The system SHALL add a host package per desktop when configuring an app, and
SHALL add all of them unless the app names a subset in `app.json` under
`basalt.desktops`.

A name that is not a desktop this platform supports SHALL be reported, and the
full set used, rather than silently installing fewer packages than the app
asked for.

#### Scenario: An app that says nothing

- **WHEN** an app with no `basalt.desktops` is configured
- **THEN** a host package for every supported desktop is added
- **AND** each is pinned to the same version as `basalt-core`

#### Scenario: An app that names one desktop

- **WHEN** `app.json` has `"basalt": {"desktops": ["macos"]}`
- **THEN** only that desktop's host package is added

#### Scenario: An app that misspells one

- **WHEN** `basalt.desktops` names something that is not a supported desktop
- **THEN** the command reports the name it did not recognise and what it
  expected
- **AND** it adds the host package for every supported desktop, so the app is
  configured rather than left half-done
