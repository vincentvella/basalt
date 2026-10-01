# TypeScript, and a package that ships types

## Why

The package ships no types. `package.json` sets no `types`, there is no `.d.ts`
anywhere, and there was no `tsconfig.json` in the repository. `create-expo-app`
gives you TypeScript, so the first line the intended user writes is
`import {useWindow} from 'basalt-core'` and it resolves to `any`.

That was drift rather than a decision -- nothing in `docs/DECISIONS.md` argued
for JavaScript, which is where the argument would have been. It is now a
decision, recorded there.

This lands before `init` and before publishing on purpose. `init` is the command
that puts this package into somebody else's app, and publishing is what makes
that possible; both get harder to change afterwards, and the public surface is
what they expose.

## What Changes

- The packages are written in TypeScript and compiled before publishing.
- The published package declares its types, so an app gets them from the import.
- Type checking runs in CI, so the types are load-bearing rather than decoration.
- What tools outside this project read -- `react-native.config.js`,
  `metro-config.js`, the bin entries -- keeps working from the same paths, as
  CommonJS.

## Capabilities

### Modified Capabilities
- `packaging`

## Impact

- A build step where there was none. `main` stops pointing at source, which is
  the change with the longest tail: an unbuilt checkout cannot bundle, where
  today it can, so every script and CI job that bundles needs the build first.
- `PLATFORM_OVERRIDES` hands Metro absolute paths ending in `.js` and Metro
  resolves them directly. Those paths must name compiled output, and the
  platform extension files must keep their exact names -- `Platform.linux.js`
  and its siblings are resolved by that name.
- 30 files and about 4,900 lines in the core package; three small files in each
  host package.
- `cli/init.js` is written and unwired; it lands as TypeScript rather than being
  written twice.
