/**
 * The Metro configuration a real app using this platform would write.
 *
 * The only line that is about this project is `withDesktopPlatforms`. Everything else
 * is here because this example is not installed from npm: it resolves React
 * Native out of a checkout rather than out of its own node_modules, the same
 * arrangement e2e/metro.config.js uses and for the same reason, so the app stays
 * in step with the source tree the host is built from.
 *
 * @format
 */

'use strict';

const fs = require('fs');
const path = require('path');
const {createRequire} = require('module');

const rnDir = path.resolve(
  process.env.RN_DIR ?? path.resolve(__dirname, '..', '..', '..', 'react-native'),
);

if (!fs.existsSync(path.join(rnDir, 'node_modules', 'react-native'))) {
  throw new Error(
    `No React Native checkout with installed dependencies at ${rnDir}.\n` +
      'Point RN_DIR at one, and run scripts/bootstrap.sh if its node_modules is empty.',
  );
}

const rnRequire = createRequire(path.join(rnDir, 'package.json'));
const {getDefaultConfig, mergeConfig} = rnRequire('@react-native/metro-config');

// The built package: basalt-core is TypeScript, and requiring the
// source here does not fail cleanly -- Node 24 loads the .ts sibling and
// reports `Unexpected token 'export'`, which names neither the file nor the
// reason. Run scripts/build_ts.sh; scripts/bundle.sh does it for you.
const {withDesktopPlatforms} = require('../../packages/basalt-core/dist/metro-config');

module.exports = withDesktopPlatforms(
  mergeConfig(getDefaultConfig(__dirname), {
    projectRoot: __dirname,
    watchFolders: [rnDir],
    resolver: {
      nodeModulesPaths: [path.join(rnDir, 'node_modules')],
      extraNodeModules: {
        react: path.join(rnDir, 'node_modules', 'react'),
        'react-native': path.join(rnDir, 'node_modules', 'react-native'),
      },
    },
  }),
);
