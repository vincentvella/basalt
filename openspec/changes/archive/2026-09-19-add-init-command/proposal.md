# One command adds a desktop to an existing app

## Why

Adding a desktop to an Expo app works and is entirely manual. Today it means
knowing to add `@react-native/metro-config` and `@react-native-community/cli` as
dev dependencies, to wrap the Metro config in `withDesktopPlatforms`, and to add
a script per desktop -- four steps, none of them discoverable, each of which
fails in its own way when it is missed or done in the wrong order.

Nobody else can use this platform yet, and this is the first thing they would
hit. `react-native-windows` and `react-native-macos` both have this command, and
for the same reason: the configuration is mechanical, which is exactly what a
person should not be asked to get right by hand.

## What Changes

- `npx basalt-core init` configures an existing app to build for the
  desktops this platform supports.
- It is idempotent: running it on a configured app reports what is already
  right and changes nothing, so it is safe to re-run after an upgrade.
- It refuses rather than half-configuring an app it does not recognise, naming
  what it expected to find.
- It does not create an app. `create-expo-app` does that; this adds a desktop to
  one that exists, which is the same division `run-linux` already assumes.

## Capabilities

### Modified Capabilities
- `packaging`

## Impact

- A new `cli/init.js`, registered the way the existing commands are in
  `react-native.config.js`.
- The dev dependencies and the Metro wrapper are the same ones the README
  documents by hand today; the README's manual instructions become the
  description of what the command does.
- Verified against a fresh `create-expo-app`, the way `release.yml`'s install
  job already installs the packed packages into one -- that job is the shape to
  follow, because it is the only thing here that has ever tested this platform
  from outside the repository.
