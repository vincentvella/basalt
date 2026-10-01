/**
 * What React Native's CLI reads out of this package.
 *
 * Just the `run-linux` command. The platform *declarations* -- which make
 * `--platform linux` a thing the CLI accepts at all -- are in
 * basalt-core, because they are the same on every desktop and an app
 * targeting only macOS still needs them.
 *
 * The CLI reads a react-native.config.js out of every dependency and merges
 * them, so an app with both packages installed gets the platforms from one and
 * the command from the other without either knowing about the other.
 *
 * @format
 */

// @ts-check
'use strict';

module.exports = {
  commands: [require('./cli/runLinux')],
};
