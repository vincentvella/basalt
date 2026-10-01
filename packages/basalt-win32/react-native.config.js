/**
 * What React Native's CLI reads out of this package.
 *
 * Just the `run-windows` command. The platform *declarations* -- which make
 * `--platform` accept this one at all -- are in basalt-core, because
 * they are the same on every desktop and an app targeting only one of them
 * still needs them.
 *
 * The CLI reads a react-native.config.js out of every dependency and merges
 * them, so an app with more than one of these packages installed gets the
 * platforms from the shared one and a command from each of the others, without
 * any of them knowing about the rest.
 *
 * @format
 */

// @ts-check
'use strict';

module.exports = {
  commands: [require('./cli/runWindows')],
};
