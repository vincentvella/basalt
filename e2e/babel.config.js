// Metro needs a Babel config in the project root. React Native's preset is the
// one that understands JSX, Flow, and the transforms its own JS is written
// against. Like everything else here it comes from the React Native checkout,
// resolved as a package specifier because the preset is a workspace package
// whose entry point lives only in its "exports" field.
const path = require('path');
const {createRequire} = require('module');

const rnDir = path.resolve(
  process.env.RN_DIR ?? path.resolve(__dirname, '..', '..', 'react-native'),
);
const rnRequire = createRequire(path.join(rnDir, 'package.json'));

// The worklets plugin, when the demo app has worklets installed.
//
// A `'worklet'` function is not ordinary JavaScript: the plugin compiles each
// one into something the worklet runtime can take, and without it a bundle
// looks fine and fails at runtime with "Cannot read property 'bytecode' of
// undefined". An app gets this from babel-preset-expo; this project does not
// use that preset, so it is named here.
//
// Conditional, because a checkout that has not run `npm install --prefix e2e`
// has no plugin to resolve and every other app in e2e/ still has to bundle.
// Resolved from e2e/ rather than from the React Native checkout, because this
// is the demo app's own dependency and not React Native's.
const e2eRequire = createRequire(path.join(__dirname, 'package.json'));

let workletsPlugin;
try {
  workletsPlugin = e2eRequire.resolve('react-native-worklets/plugin');
} catch {
  workletsPlugin = null;
}

module.exports = {
  presets: [rnRequire.resolve('@react-native/babel-preset')],
  // Last, which the plugin's own documentation requires: it has to see the
  // code after the other transforms have run.
  plugins: workletsPlugin == null ? [] : [workletsPlugin],
};
