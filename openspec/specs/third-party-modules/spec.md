# third-party-modules Specification

## Purpose

Libraries that ship Objective-C, Java and Kotlin, and what it takes for them to
work here.

This is the claim the architecture makes: a third-party native module is a
TurboModule or a Fabric component with C++ codegen, so porting one is writing a
backend rather than reimplementing a library. gesture-handler and Reanimated are
the two that prove or disprove it, because everything else depends on them.

## Requirements

### Requirement: A gesture library's contract is honoured, not its code

The system SHALL implement the behaviour a gesture library's JavaScript depends
on -- the handler state machine, the events, and the relations between handlers
-- rather than porting the library's platform code.

A pan SHALL begin on contact and activate once it has moved far enough; a tap
SHALL be a press that neither moved nor was held too long, and SHALL fail rather
than hang when it cannot happen.

#### Scenario: A pan activates once it has moved

- **WHEN** a pointer presses and then moves past the threshold
- **THEN** the handler begins, then activates
- **AND** its translation is measured from where the gesture started

#### Scenario: A tap is a press that did not move or linger

- **WHEN** a pointer presses and releases without wandering or being held
- **THEN** the tap handler activates
- **AND** a press that wanders, or is held too long, fails instead

#### Scenario: A position is reported in the handler's own coordinates

- **WHEN** a gesture reaches a handler
- **THEN** the pointer's position is relative to that handler's view

### Requirement: Handlers resolve conflicts between themselves

The system SHALL cancel every other handler tracking the same pointer when one
activates, unless the two were declared simultaneous, and SHALL hold a handler
that was told to wait until the handler it waits for has failed.

#### Scenario: An activating handler cancels the others

- **WHEN** one handler activates while others track the same pointer
- **THEN** the others are cancelled

#### Scenario: Simultaneous handlers are left alone

- **WHEN** two handlers are declared simultaneous
- **THEN** one activating does not cancel the other

#### Scenario: A handler waits for the one it was told to wait for

- **WHEN** a handler is declared to wait for another
- **THEN** it stays pending until that one has failed

#### Scenario: A gesture on an ancestor still arrives

- **WHEN** the pressed view has no handler but an ancestor does
- **THEN** the ancestor's handler is offered the gesture

### Requirement: An animation library runs its worklets

The system SHALL provide the runtime a worklet-based animation library needs, so
that animations declared in JavaScript run without the library's own platform
code.

#### Scenario: An animated component animates

- **WHEN** an app renders a component animated by a worklet library
- **THEN** the animation runs

### Requirement: A dropped handler stops hearing about the pointer

The system SHALL stop offering a gesture to a handler whose view has been
unmounted, and SHALL never offer one to a handler attached to nothing.

#### Scenario: Unmounting detaches a handler

- **WHEN** a view carrying a handler is unmounted mid-gesture
- **THEN** that handler receives nothing further

### Requirement: A native dependency is decided per target, and a missing one is said out loud

The system SHALL decide whether to build an optional native dependency from the
platform being built for, never from the platform doing the building. Cross
building is ordinary here -- a GTK host builds on a Mac -- so the two are
routinely different, and asking the wrong one configures a dependency the host
cannot link.

Where a dependency cannot be built for a target, the system SHALL say so during
the build, naming the target, the package, and the failure the app will hit
instead. A native module that is absent is not absent quietly: the JavaScript
side of these libraries calls `TurboModuleRegistry.getEnforcing`, which throws
at import, so an app that merely imports the package -- without rendering
anything from it -- does not start. That failure carries no trace of the build
decision that caused it, so the build is the only place it can be explained.

The system SHALL NOT register a placeholder in place of a missing dependency.
A module that exists and refuses is not better than one that is absent: Skia's
own setup throws unless `install()` returns exactly `true`, so the placeholder
buys a different exception at the same line, and it costs the honest signal
that the dependency is not there.

A dependency's portable half compiling is not the same as a host having it.
Where a library divides into portable C++ and a per-window-system half, the
system SHALL treat the host half as the thing that decides whether the module
exists. The portable half can compile for a host that never links it, and the
app sees the same `getEnforcing` failure as if nothing had been built at all.

#### Scenario: The portable half builds but no host half exists

- **WHEN** a GTK host is built on macOS, where Skia's Apple binaries are usable
- **THEN** skia_core compiles, and the host still has no RNSkiaModule
- **AND** the reason is that the module and the canvas are written for AppKit only

#### Scenario: A host is built for a target the dependency has no binaries for

- **WHEN** an app depending on @shopify/react-native-skia is built for Linux or Windows
- **THEN** Skia is not passed to the configure, whatever platform the build runs on
- **AND** the build says the target has no RNSkiaModule and that importing the package will end the app

#### Scenario: The same app is built for the target the dependency supports

- **WHEN** that app is built for macOS
- **THEN** Skia is passed to the configure and reported as built
