# Design

## What must not move

Three things are read by tools this project does not control, out of an app's
`node_modules`, as CommonJS:

- `react-native.config.js` -- React Native's CLI, to believe these are platforms
- `metro-config.js` -- the app's own `metro.config.js` requires it by that path
- the `bin` entries -- `npx` resolves them from `package.json`

They can be authored in TypeScript, but they must be *emitted* as CommonJS at
exactly those paths. The `exports` map already names them, so the map is the
contract: whatever the layout becomes, those specifiers resolve to the same
shapes.

## The overrides are converted too

*Revised twice. First to "copy them", when three of them turned out to carry
the Flow annotations of the React Native files they rewrite and `tsc` could not
even copy them through. Then to this, because keeping a dialect nothing else in
the toolchain uses is a cost paid on every read of those files, and the
diffability it bought was against upstream files this project has already
rewritten rather than tracked.*

So all thirteen are TypeScript, and `dist/src/overrides/*.js` is compiled
output like everything else. The emitted filenames are what Metro selects by --
`Platform.linux.js` and its siblings -- and compilation preserves them, which
is checked by bundling and asking what `Platform.OS` says.

What they import cannot be typed and should not be. The
`basalt-core/upstream/*` prefix is not a package: metro-config.ts turns
it into an absolute path inside whichever React Native is being bundled, and
behind it are React Native's internals at paths it reserves the right to move.
`src/overrides/upstream.d.ts` declares the whole prefix as `any` in the
shorthand form, which is the honest answer: a fabricated type there would be a
guess the compiler then enforced, at the seam this project controls least.

## The override paths were the hazard

`PLATFORM_OVERRIDES` does not return module specifiers. It returns absolute file
paths ending in `.js`, and Metro's `resolveRequest` uses them directly:

    path.join(OVERRIDE_DIR, 'BaseViewConfig.js')
    platform => path.join(OVERRIDE_DIR, `Platform.${platform}.js`)

So `OVERRIDE_DIR` has to point at compiled output, and compilation has to
preserve those filenames -- including the platform extensions, which are
`Platform.linux.js`, `Platform.macos.js`, `Platform.windows.js` and are selected
by that name rather than by anything in the module system.

This is the failure that would not look like a type error. It looks like an app
bundling with React Native's own `Platform` instead of this one, and the symptom
is `Platform.OS` being `android` a long way from here.

## An unbuilt checkout stops working

Today `main` points at `src/index.js` and a fresh clone can bundle immediately.
After this it cannot, until `tsc` has run. Everything that bundles has to build
first: `scripts/bundle.sh`, `scripts/integration_test.py`, `compare_all.sh`, the
CI jobs, and the demo apps in `js/`.

Making the build cheap matters more than making it clever. One `tsc -b`, no
bundler, no transform beyond what TypeScript does.

## Strictness

`strict: true` from the start, because retrofitting it is the expensive version
of this work and there are only 5,000 lines to argue with. Where a React Native
internal has no usable type -- and the overrides are full of them, since they
shadow files React Native does not export types for -- the honest answer is a
narrow local type with a comment saying what it approximates, not `any` spread
outward.

## Order

The overrides last. They are the files most likely to need an escape hatch, and
doing them first would set the strictness bar by the worst case.

1. `tsconfig`, the build, and CI running it -- with no files converted, so the
   scaffolding is proven before it carries anything.
2. The public surface, `src/*.ts`, which is what consumers see.
3. The CLI and the config files, where the emitted paths matter.
4. The overrides.
