// What a release would upload, and the two lists that have to agree.
//
// The check these exercise is cheap and the thing it prevents is not: a
// package that reaches the registry before anybody meant it to cannot be
// taken back -- npm holds the name and the version for good -- and one
// without a trusted publisher fails its credential exchange half way through
// a release, after the packages before it have gone out.

'use strict';

const assert = require('node:assert');
const {test} = require('node:test');

const {check, probedPackages} = require('./check_packages.js');

test('the repository as it stands is ready to publish', () => {
  // Not a tautology: this is the check running against the real packages and
  // the real workflow, which is the thing every other assertion here is
  // about. It failed when it was written, naming basalt-navigation.
  assert.deepEqual(check(), []);
});

test('the probed names are read out of the workflow rather than copied', () => {
  const names = probedPackages(`
        run: |
          for p in basalt-core basalt-appkit \\
                   basalt-gtk basalt-notifications \\
                   basalt-subprocess basalt-win32; do
            echo "$p"
          done
  `);
  assert.deepEqual(names, [
    'basalt-core',
    'basalt-appkit',
    'basalt-gtk',
    'basalt-notifications',
    'basalt-subprocess',
    'basalt-win32',
  ]);
});

test('a loop the check cannot find is a problem rather than an empty list', () => {
  // The failure that would otherwise be silent: a workflow rewritten into a
  // shape this cannot read would report every package as unprobed, or none,
  // depending on which way the mistake fell.
  assert.equal(probedPackages('run: npm publish everything'), null);
});
