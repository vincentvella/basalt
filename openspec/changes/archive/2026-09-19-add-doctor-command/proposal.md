# Tell people what is wrong before the compiler does

## Why

A first desktop build compiles Hermes and React Native's C++ from source. It
takes tens of minutes, and every prerequisite it is missing is discovered at
the moment it is needed rather than at the start: no cmake, no ninja, the GTK
development headers absent, vcpkg not set up, a React Native outside
`supported-versions.json`. Each of those arrives as a compiler or CMake error
naming a path, twenty minutes in, to somebody who has not seen this build
before.

`init` already knows how to check things and say what it found -- every step
reports `=` already right, `+` changed, or `!` needs you, and it has a
no-write path that says what it *would* do. What it checks is only what it can
fix by editing `package.json`. Nothing checks the machine, and the machine is
where the expensive failures are.

There is a second question with the same shape. `init` now installs a host
package per desktop, all three by default, because `package.json` is committed
and which desktops an app builds for is the project's business. An app that
wants fewer has nowhere to say so.

## What Changes

- `npx basalt-core doctor`: the checks `init` makes, with writing off,
  plus the ones it cannot fix -- toolchain, system libraries, React Native
  version -- each reporting what is wrong and what to run.
- `app.json`'s `basalt.desktops` narrows which host packages `init` adds. All
  three unless it says otherwise; an unknown name is reported rather than
  quietly installing one package fewer.
- Not `update`, deliberately. See below.

## Capabilities

### Modified Capabilities
- `packaging`

## Impact

- `cli/init.ts`'s step machinery becomes shared, and `doctor` is its read-only
  caller. The states already exist; what is new is checks that have no fix.
- A new `cli/doctor.ts`, and a second `bin` entry or a subcommand on the
  existing one -- `init` is already a verb on `basalt-core`, so
  `doctor` is the second.
- `docs/PORTING.md` lists what a build needs per platform; that list is what
  the toolchain checks encode, and the two must not drift.

## Out of scope

**`update` waits for something to update to.** Every package here is
`private: true` at `0.0.0`, so there is no published version to move between
and nothing an update command could do today but edit a number. The thing it
would exist to protect -- that the five packages stay in lockstep, because they
share a C++ ABI with the host -- is already asserted from the other end by
`release.yml`'s `versions` job, which fails a release when they disagree.

It is cheap to add later: the dependency editing is `init`'s, the version rule
is in `docs/DECISIONS.md` under "Version together, at first", and the check
that would catch a mistake already runs. Worth doing at the first release that
is not also the first.

**Expo's `platforms` field is not the source for `basalt.desktops`.** A bare
React Native app does not have one, and its accepted values are Expo's to
define rather than this platform's to borrow. The `basalt` key in `app.json` is
already this platform's escape hatch there -- `cli/packageApp.ts` reads
`basalt.identifier` and `basalt.scheme` from it.
