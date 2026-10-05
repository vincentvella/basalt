# e2e

The applications the end-to-end suite runs.

Each one is a small React Native app built around a single thing: `scroll.tsx`
is a `<ScrollView>` and almost nothing else, `press.tsx` reacts to a press,
`screens.tsx` is a stack of screens. `index.tsx` is the demo, the one most
scenarios use, and `demo.tsx` is a surface with no React in it at all, driving
Fabric's JSI binding by hand.

The assertions are not here. `scripts/integration_test.py` runs an app against
a host, and asserts on the widget tree the host dumps; these are its inputs.
`scripts/compare_all.sh` runs every one of them through every host that is
built and diffs the trees.

Nothing is installed here. `react`, `react-native`, Metro and the Babel preset
all resolve out of the React Native checkout that `scripts/bootstrap.sh`
prepared, which is what keeps these in step with the exact source the C++ is
built against. See `metro.config.js`.
