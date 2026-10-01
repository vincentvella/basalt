/**
 * `react-native run-linux`.
 *
 * Four strings and a path. Everything a desktop run actually does is in
 * basalt-core's cli/desktop.js, shared with `run-macos` and
 * `run-windows`, because none of it is about a toolkit -- see the note at the
 * top of that file.
 *
 * @format
 */

// @ts-check
'use strict';

const path = require('path');

const {shared, sharedPackageDir} = require('./shared');

const {makeRunCommand} = shared('cli/desktop');

module.exports = makeRunCommand({
  platform: 'linux',
  label: 'Linux',
  command: 'run-linux',
  binary: 'basalt_gtk',
  nativeDir: path.resolve(__dirname, '..', 'native'),
  coreDir: path.join(sharedPackageDir(), 'native'),
  toolchain: 'a C++20 compiler and the GTK4 and Pango development packages',
});
