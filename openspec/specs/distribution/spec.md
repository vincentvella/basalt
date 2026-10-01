# distribution Specification

## Purpose

Where a capability lives, and how it reaches the host binary.

React Native shipped one package with everything in it and spent the decade
since extracting the parts that did not belong. This capability is the
decision made in advance instead: what `basalt-core` keeps, what ships
beside it, and the one mechanism by which anything outside core gets compiled
into an app's host.

The boundary itself -- core is the application's own surface, a package is
what reaches outside it -- is argued in `docs/DECISIONS.md`. What is specified
here is the behaviour that boundary has to produce: discovery by manifest
rather than by name, a capability that does nothing where it cannot work but
can always be asked, and core that builds whether or not any of it is
installed.

## Requirements

### Requirement: A package can contribute native code to the host build

The system SHALL discover packages that declare native code in their own
manifest and compile them into the host, without the command-line tool knowing
those packages by name.

A package that declares nothing SHALL be ignored, and a package whose native
code fails to build SHALL fail the build with its name rather than silently
producing a host without it.

#### Scenario: An installed capability package reaches the host

- **WHEN** an app installs a package declaring a native entry point and builds a
  host
- **THEN** that package's native code is compiled into the host
- **AND** its JavaScript can reach the module it registers

#### Scenario: A package that declares nothing changes nothing

- **WHEN** an app installs a package with no native declaration
- **THEN** the host builds exactly as it would have

### Requirement: Core is the application's own surface

The system SHALL keep in `basalt-core` the application's own surface --
its windows, menus, dialogs, title bar, components, input, and the APIs React
Native itself exposes.

A capability SHALL live in its own package when it needs the person's consent,
when it touches hardware or another application, or when it acts outside the
app's own windows.

#### Scenario: A permission-gated capability is not in core

- **WHEN** a capability requires an operating-system permission prompt
- **THEN** it ships as its own package rather than in core

#### Scenario: Ordinary window furniture stays in core

- **WHEN** a capability is part of the app's own window -- a menu, a dialog, a
  title bar
- **THEN** it stays in core, because a platform that made you install a package
  for a menu would be worse rather than smaller

### Requirement: A capability ships as one package and does nothing where it cannot

The system SHALL ship a capability as a single package regardless of how many
desktops implement it, and that package SHALL accept every call on a desktop
that cannot perform it rather than failing.

Doing nothing SHALL NOT be the only answer available: a capability that is
unavailable SHALL also be reportable, so that an app can hide a control instead
of offering one that does nothing.

#### Scenario: The same app runs unchanged on a desktop that cannot do it

- **WHEN** an app using a capability runs on a desktop with no implementation
- **THEN** the calls do nothing and nothing throws

#### Scenario: An app can ask before offering the control

- **WHEN** an app asks whether the capability is supported here
- **THEN** it gets an answer it can render from

### Requirement: Core builds with no capability package installed

The system SHALL build `basalt-core` in an app that has installed none
of them. Core SHALL NOT include a capability package's header or name its
symbols; a package whose code is compiled into core SHALL also supply whatever
a build with no platform needs in order to link.

The dependency only ever points one way. An app installs the capabilities it
asks for, so core depending on one -- even to stub it -- makes the ordinary
case the broken one.

#### Scenario: An app that installs no capability package

- **WHEN** an app installs the platform and its host packages, and no
  capability package
- **THEN** the host builds and runs

#### Scenario: A package satisfies its own seam

- **WHEN** a capability package contributes code to core that calls its own
  platform seam
- **THEN** the package also supplies the stub a platform-less build links
  against, rather than core carrying it
