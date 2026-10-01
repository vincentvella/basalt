/**
 * `react-native run-windows`.
 *
 * Four strings and a path. Everything a desktop run actually does is in
 * basalt-core's cli/desktop.js, shared with `run-linux` and
 * `run-macos`, because none of it is about a toolkit -- see the note at the top
 * of that file.
 *
 * The command name is react-native-windows', deliberately: an app that already
 * knows `react-native run-windows` should not have to learn a second spelling
 * to try this one. Same argument as the `windows` platform name.
 *
 * @format
 */

// @ts-check
'use strict';

const path = require('path');

const {shared, sharedPackageDir} = require('./shared');

const {makeRunCommand} = shared('cli/desktop');

module.exports = makeRunCommand({
  platform: 'windows',
  label: 'Windows',
  command: 'run-windows',
  binary: 'basalt_win32.exe',
  nativeDir: path.resolve(__dirname, '..', 'native'),
  coreDir: path.join(sharedPackageDir(), 'native'),
  toolchain:
    'the Visual Studio Build Tools with clang-cl, a Git Bash to run bootstrap.sh in, ' +
    'and a vcpkg with glog, fmt, double-conversion, boost, gflags, openssl and curl',
});
