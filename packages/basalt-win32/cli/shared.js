/**
 * Reaching basalt-core, the shared half, from this package.
 *
 * The one file in a platform package's CLI that cannot itself be shared,
 * because it is what finds the thing everything else is shared through. Two
 * layouts matter and only one of them resolves by name.
 *
 * Installed into an app, basalt-core is a sibling in node_modules and
 * `require('basalt-core/...')` finds it. In a checkout of this
 * repository the two packages are sibling directories with nothing installed
 * between them -- deliberately, since this repo has no node_modules of its own
 * -- and the same require fails. So: by name first, because that is the layout
 * an app is in, and beside us second, because that is the layout it is
 * developed in.
 *
 * Both reach the *built* package. basalt-core is TypeScript: by name
 * resolves through its `exports` map, which points into `dist/`, and the
 * sibling path has to say so itself. A checkout that has not run
 * scripts/build_ts.sh has nothing here to find, which is what the error says.
 *
 * @format
 */

// @ts-check
'use strict';

const path = require('path');

const SIBLING = path.resolve(__dirname, '..', '..', 'basalt-core');

/**
 * Where basalt-core is on disk. Same two layouts, same order.
 * @returns {string}
 */
function sharedPackageDir() {
  try {
    return path.dirname(require.resolve('basalt-core/package.json'));
  } catch (error) {
    if (/** @type {NodeJS.ErrnoException} */ (error).code !== 'MODULE_NOT_FOUND') {
      throw error;
    }
    return SIBLING;
  }
}

/**
 * One of basalt-core's modules, by subpath.
 * @param {string} subpath
 * @returns {any}
 */
function shared(subpath) {
  const name = `basalt-core/${subpath}`;
  try {
    return require(name);
  } catch (error) {
    if (/** @type {NodeJS.ErrnoException} */ (error).code !== 'MODULE_NOT_FOUND') {
      // The module was found and threw on its way up. Passing it through keeps
      // a real error in that file from being reported as a missing package.
      throw error;
    }
    try {
      return require(path.join(SIBLING, 'dist', subpath));
    } catch (siblingError) {
      if (/** @type {NodeJS.ErrnoException} */ (siblingError).code === 'MODULE_NOT_FOUND') {
        throw new Error(
          `could not load ${name}. In a checkout, basalt-core has to be ` +
            'built first: run scripts/build_ts.sh.',
        );
      }
      throw siblingError;
    }
  }
}

module.exports = {shared, sharedPackageDir};
