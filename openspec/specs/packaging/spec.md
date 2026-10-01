# packaging Specification

## Purpose

Turning a host and a bundle into something the desktop treats as an application.

## Requirements

### Requirement: One command builds and runs an app per desktop

The system SHALL provide `run-linux`, `run-macos` and `run-windows` as one
command with three names, each bundling the app and launching the host.

#### Scenario: An app runs from one command

- **WHEN** a developer runs the command for their desktop
- **THEN** the app is bundled and the host launches it

### Requirement: An app is packaged as the desktop expects

The system SHALL give the application the identity its desktop requires.

On macOS that is a real `.app` with `Info.plist`, an identifier, declared URL
schemes and an ad-hoc signature -- without which macOS will not grant
notification permission at all. On Linux it is a `.desktop` entry. On Windows it
is an AppUserModelID and a Start Menu shortcut carrying it.

#### Scenario: macOS gets a bundle it will trust

- **WHEN** an app is packaged for macOS
- **THEN** it is a bundle with an identifier and an ad-hoc signature

#### Scenario: Packaging does not change the developer's session

- **WHEN** an app is packaged for Linux
- **THEN** a `.desktop` entry is written and not installed into the session

### Requirement: The package declares its own types

The system SHALL publish type declarations for its public API, so that an app
written in TypeScript -- which is what this platform's own installer produces --
gets types from the import rather than `any`.

The declarations SHALL be generated from the implementation rather than
maintained beside it, so the two cannot disagree.

Type checking SHALL run in continuous integration, so that a type that has
stopped describing the code fails a build rather than misleading a reader.

#### Scenario: An app gets types from the import

- **WHEN** a TypeScript app imports this platform's public API
- **THEN** the imported values are typed, and a wrong call is a type error

#### Scenario: The published package carries declarations

- **WHEN** the package is packed
- **THEN** it contains type declarations for its public entry points, and
  `package.json` names them

#### Scenario: Types that have gone stale fail the build

- **WHEN** an implementation changes so that its types no longer describe it
- **THEN** continuous integration fails

### Requirement: One command configures an existing app for the desktop

The system SHALL provide a command that adds desktop support to an app that
already exists, performing every configuration step this platform needs rather
than documenting them.

The command SHALL be idempotent: run against an already-configured app it SHALL
report what is already correct and change nothing.

The command SHALL refuse an app it does not recognise, naming what it expected,
rather than writing a partial configuration -- a half-configured app fails later
and further away than one that was refused.

#### Scenario: A fresh app becomes buildable

- **WHEN** the command is run in an app that has no desktop configuration
- **THEN** the app can afterwards be bundled and built for each supported
  desktop with no further manual edits

#### Scenario: Running it twice changes nothing

- **WHEN** the command is run again on an app it has already configured
- **THEN** it reports the configuration as already present and writes nothing

#### Scenario: An unrecognised project is refused

- **WHEN** the command is run somewhere it cannot identify as an app
- **THEN** it stops, names what it expected to find, and leaves the directory
  untouched

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
