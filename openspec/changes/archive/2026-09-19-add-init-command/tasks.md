# Tasks

## 1. The command

- [x] `cli/init.ts`, as the package's `basalt-core` bin -- not a React
      Native CLI command, because before it runs the package is not a
      dependency yet and the CLI has no config to read
- [x] Detect the app: `package.json`, an Expo or React Native dependency, a Metro config
- [x] Add `@react-native/metro-config` and `@react-native-community/cli` as dev dependencies
- [x] Wrap the Metro config in `withDesktopPlatforms`, preserving whatever is already there
- [x] Add a script per supported desktop

## 2. Idempotence and refusal

- [x] Re-running reports each step as already done and writes nothing
- [x] An unrecognised directory is refused, naming what was expected
- [x] A Metro config the command cannot safely edit is reported rather than overwritten

## 3. Verification

- [x] Run against a fresh `create-expo-app`, following `release.yml`'s install job
- [x] Bundle and build the result for one desktop, to prove the configuration is complete
- [x] Have `release.yml` run the command rather than write the config by hand,
      so the job proves the command and not only the platform
- [x] Replace the README's manual instructions with the command

## 4. Records

- [x] Strike the entry in `docs/backlog/ecosystem.md`
- [x] Update the README's "Next"


## 5. What the verification found

Five bugs, none of them visible to a checkout. Recorded here because the
lesson is the same each time: the install path and the repository path are
different code paths, and only one of them was being exercised.

- [x] `init` read `init` as the directory to configure, so every invocation
      the README gives resolved `./init`, found no package.json, and refused
      the app it was standing in
- [x] `init` refused an app with no `metro.config.js` -- which is what a stock
      `create-expo-app --template blank` is -- and reported by hand the one
      step it exists to do. It writes one now, on the app's own defaults
- [x] Capability packages were added only by the repository's root
      CMakeLists, and the CLI configures `-S <host package>/native`, so an
      app got none: `core/portability_probe.cpp` failed on a `Notifications.h`
      nothing had put on the path
- [x] Each host read `BASALT_PACKAGE_<HOST>_SOURCES` at the top of its
      CMakeLists, before the packages that set it were added. A checkout was
      fine because the root adds them first; an app linked without the
      platform half and failed on every notification seam
- [x] `src/overrides/setUpXHR.ts` imported the abort API from where React
      Native 0.87 keeps it. 0.86 -- which `supported-versions.json` lists, and
      which a current Expo app installs -- polyfills it from the
      `abort-controller` package under a different export name
