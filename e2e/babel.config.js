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

module.exports = {
  presets: [rnRequire.resolve('@react-native/babel-preset')],
};
