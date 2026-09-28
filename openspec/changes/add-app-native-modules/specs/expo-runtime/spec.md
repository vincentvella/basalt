# expo-runtime Specification

## ADDED Requirements

### Requirement: An app's own native module is compiled into the host

The system SHALL compile an app's local native modules into the host binary, so
that an Expo module the app wrote and never published can have a desktop half
beside its Apple and Android ones.

A local module SHALL be found where Expo's autolinking looks for one -- a
directory under the app's `modules` -- and SHALL declare a desktop half by
containing one, rather than by declaring it in a manifest, because a local module
has no manifest.

A build failure SHALL name an app's own module among the contributors, for the
same reason it names a capability package: its compiler errors arrive looking
exactly like the platform's own.

#### Scenario: A local module with a desktop half is built

- **WHEN** an app has a local module carrying native build instructions
- **THEN** that module's native code is compiled into the host and its Expo
  modules are registered

#### Scenario: A local module with no desktop half is not an error

- **WHEN** an app has a local module with an Apple half and no desktop one
- **THEN** the build succeeds and contributes nothing for that module

### Requirement: A contributing module names what it links

The system SHALL let a capability package or an app's own module declare the
libraries its platform implementation needs, so that a module reaching for a
framework does not require the host to know it exists.

#### Scenario: A module's framework is linked into the host

- **WHEN** a module contributes a platform implementation and declares a library
  it needs
- **THEN** the host and its test binary link that library, and the module's
  implementation resolves

#### Scenario: A host names no module's libraries

- **WHEN** a capability ships as a package
- **THEN** the libraries it needs are declared by that package rather than by the
  host package it is built into

### Requirement: An Expo module can reach JavaScript later

The system SHALL let an Expo module emit an event after the call that started the
work has returned, and from a thread that is not the JavaScript one, so that a
module whose work finishes later can report it at all.

An emit SHALL find its module by name at the moment of the emit, so that a module
reinstalled by a reload is the one that receives it.

An emit with nowhere to go -- no host, or no listener -- SHALL do nothing and
report that it did nothing, rather than failing: a native reader with an event and
no audience is ordinary.

#### Scenario: An event reaches a listener

- **WHEN** native work finishes on another thread and emits on its Expo module
- **THEN** a JavaScript listener added with `addListener` receives it, with the
  payload the emitter built

#### Scenario: An event outlives the runtime it was meant for

- **WHEN** native work emits after a reload has replaced the runtime
- **THEN** the module installed in the new runtime receives it, and nothing
  touches the old one

#### Scenario: A promise settles after the call returned

- **WHEN** an Expo module's function returns a promise for something a person has
  to do, such as choosing a folder
- **THEN** the promise settles when they have done it, rather than immediately

## MODIFIED Requirements

### Requirement: A capability package registers its own Expo modules

The system SHALL let a package outside core contribute Expo modules, discovered
from the app's own dependencies at build time, so that an optional capability is
an install rather than an edit to core.

The system SHALL discover, on the same contract, a native module in the app's own
source tree. Neither SHALL be contributed by a dependency the app did not ask
for: compiling C++ into the host binary is not something a transitive dependency
arranges.

#### Scenario: An installed capability package is reachable

- **WHEN** an app depends on a capability package and builds
- **THEN** that package's Expo modules are registered and answer

#### Scenario: A capability absent from a desktop no-ops

- **WHEN** a capability package is installed on a desktop that cannot provide it
- **THEN** its modules are present and answer honestly rather than being missing

#### Scenario: A transitive dependency contributes nothing

- **WHEN** a package declaring native code is installed but is not a dependency
  the app named
- **THEN** its native code is not compiled into the host
