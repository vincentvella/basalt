# Tasks

## 1. Share what `init` already does

- [x] Lift the step machinery -- the three states and the summary -- out of
      `cli/init.ts` so both commands report the same way
- [x] Give each existing check a read-only form, which `init` already has for
      the Metro config and does not for the rest

## 2. Check the machine

- [x] cmake, ninja and a compiler, per platform, from the list in
      `docs/PORTING.md`
- [x] The system libraries each desktop needs: GTK4 development headers on
      Linux, the macOS SDK, vcpkg on Windows
- [x] The app's React Native against `supported-versions.json`, reporting the
      version found and the versions supported
- [x] Report a desktop that cannot be checked from here as unchecked, never as
      passing

## 3. The command

- [x] `cli/doctor.ts`, as a second verb on the `basalt-core` bin
- [x] Non-zero exit when anything is wrong, so CI can run it
- [x] Have `release.yml`'s Expo job run it after `init`, where it would have
      caught the host packages being missing

## 4. Which desktops

- [x] `init` adds a host package per desktop, all three by default
- [x] `app.json`'s `basalt.desktops` narrows the list
- [x] An unrecognised name is reported and the full set used
- [x] Document the key in the README beside `init`

## 5. Records

- [x] Update the `packaging` spec
- [x] A line in `docs/DECISIONS.md` only if the toolchain checks turn out to
      need a rule; the packaging boundary already has one -- they did not, so
      there is none. What the checks encode is `docs/PORTING.md`'s list, which
      is a fact about each platform rather than a decision about this one

## 6. What building it found

- [x] A fourth step state. `unchecked` was going to be `changed`, which read
      as "init would fix this" for a GTK check on a Mac, and then counted
      towards a summary telling the person to run `init`. Neither a failure
      nor a success, so it is counted as neither and marked `?`
- [x] `init`'s dependency and script steps said "added" whether or not they
      had. Harmless while `init` was the only caller and a lie in `doctor`'s
      mouth, which is the one thing a read-only command must never say
- [x] The new bin had no `#!/usr/bin/env node`, so the shell tried to run
      JavaScript as a shell script. Both existing bins carry one in their
      source and nothing in the build adds it, so a third was always going to
      miss it -- found by running the command out of an installed tarball
      rather than out of `dist/`
- [x] The Metro check had no read-only form for a config that already exists:
      with writing off it pushed no step at all, so `doctor` would have said
      nothing about the file most likely to be wrong
