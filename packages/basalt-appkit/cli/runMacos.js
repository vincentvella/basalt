/**
 * `react-native run-macos`.
 *
 * Four strings and a path. Everything a desktop run actually does is in
 * basalt-core's cli/desktop.js, shared with `run-linux` and
 * `run-windows`, because none of it is about a toolkit -- see the note at the
 * top of that file.
 *
 * The command name is react-native-macos', deliberately, for the same reason
 * the platform name is: an app that already knows `react-native run-macos`
 * should not have to learn a second spelling to try this one.
 *
 * @format
 */

// @ts-check
'use strict';

const path = require('path');

const {shared, sharedPackageDir} = require('./shared');

const {makeRunCommand} = shared('cli/desktop');

module.exports = makeRunCommand({
  platform: 'macos',
  label: 'macOS',
  command: 'run-macos',
  binary: 'basalt_appkit',
  nativeDir: path.resolve(__dirname, '..', 'native'),
  coreDir: path.join(sharedPackageDir(), 'native'),
  toolchain: 'the Xcode command line tools',
});
