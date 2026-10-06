# e2e

The applications the end-to-end suite runs.

Each one is a small React Native app built around a single thing: `scroll.tsx`
is a `<ScrollView>` and almost nothing else, `press.tsx` reacts to a press,
`screens.tsx` is a stack of screens. `index.tsx` is the demo, the one most
scenarios use, and `demo.ts` is a surface with no React in it at all, driving
Fabric's JSI binding by hand -- hence `.ts`, there being no JSX in it.

The assertions are not here. `scripts/integration_test.py` runs an app against
a host, and asserts on the widget tree the host dumps; these are its inputs.
`scripts/compare_all.sh` runs every one of them through every host that is
built and diffs the trees.

Nothing is installed here. `react`, `react-native`, Metro and the Babel preset
all resolve out of the React Native checkout that `scripts/bootstrap.sh`
prepared, which is what keeps these in step with the exact source the C++ is
built against. See `metro.config.js`.

These are TypeScript so that an API that changes is caught here rather than at
run time, which is the whole reason for the extension: the apps call basalt's
own published types, and nothing else in the repository does. `make ts` checks
them, after it builds the packages they import, and CI runs the same command on
its own job before anything is compiled.

`tsconfig.json` checks and never emits. Metro reads these files directly and
Babel strips the types without being asked, so there is no build step here and
nothing to keep in sync. `types.d.ts` holds the ambient declarations for
packages with none, and `runtime.ts` names the globals React Native installs,
whose own declarations depend on how the checkout was prepared.
