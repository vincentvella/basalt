/**
 * `react-native package-macos`: a self-contained, signed `.app`, not launched.
 *
 * The same target as `run-macos`; everything it does is in basalt-core's
 * cli/desktop.js and cli/releaseMacos.js.
 *
 * @format
 */

// @ts-check
'use strict';

const path = require('path');

const {shared, sharedPackageDir} = require('./shared');

const {makePackageCommand} = shared('cli/desktop');

module.exports = makePackageCommand({
  platform: 'macos',
  label: 'macOS',
  command: 'run-macos',
  binary: 'basalt_appkit',
  nativeDir: path.resolve(__dirname, '..', 'native'),
  coreDir: path.join(sharedPackageDir(), 'native'),
  toolchain: 'the Xcode command line tools',
});
