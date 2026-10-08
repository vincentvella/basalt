/**
 * Tests for scripts/check_includes.js, which is itself a stand-in for a
 * compiler.
 *
 * The point of a guard is the day it fires, and a guard that has quietly stopped
 * matching is worse than no guard: the walk still prints "0 unportable
 * spellings" and nobody looks again. So the rules are fed text here rather than
 * files, which is also the only way to check the cases the tree does not
 * contain -- every spelling on the unportable list is unused on purpose.
 *
 * Run with:  node --test scripts/test_check_includes.js
 *
 * @format
 */

'use strict';

const assert = require('node:assert');
const {test} = require('node:test');

const {RULES, UNPORTABLE, stripComments, unportableIn} = require('./check_includes.js');

// The one that cost a red Windows build. MSVC has the POSIX math constants only
// behind _USE_MATH_DEFINES, which this project does not set for its own sources.
test('M_PI is caught', () => {
  const found = unportableIn('float a = 180.0 / M_PI;');
  assert.strictEqual(found.length, 1);
  assert.match(found[0].what, /math constant/);
});

test('the rest of the math constants are caught too', () => {
  for (const spelling of ['M_PI_2', 'M_SQRT2', 'M_LN10', 'M_2_SQRTPI']) {
    assert.strictEqual(unportableIn(`double x = ${spelling};`).length, 1, spelling);
  }
});

// The explanation of why not to use a spelling contains the spelling. Without
// this, core/Gradients.h -- which says "Not the POSIX M_PI, which MSVC ..." --
// would fail the check it exists to satisfy.
test('a comment mentioning a banned name is not a use', () => {
  assert.deepStrictEqual(unportableIn('// Not M_PI, which MSVC lacks.\nfloat a = kPi;'), []);
  assert.deepStrictEqual(unportableIn('/* M_PI is not portable. */\nfloat a = kPi;'), []);
  // And the stripper leaves the code either side of a comment alone.
  assert.match(stripComments('int a = 1; // M_PI\nint b = M_PI;'), /int b = M_PI;/);
});

// A file that branches on the platform has already thought about this, and
// core/CrashHandler.cpp is the real case: pthread.h, unistd.h and ssize_t under
// `#if !defined(_WIN32)`.
test('a file that mentions _WIN32 is left alone', () => {
  const guarded = '#ifdef _WIN32\n_putenv_s(name, value);\n#else\nsetenv(name, value, 1);\n#endif';
  assert.deepStrictEqual(unportableIn(guarded), []);
  // Without the branch, the same call is a finding.
  assert.strictEqual(unportableIn('setenv(name, value, 1);').length, 1);
});

test('the other listed spellings are caught', () => {
  assert.strictEqual(unportableIn('if (strcasecmp(a, b) == 0) {}').length, 1);
  assert.strictEqual(unportableIn('printf("%s", __PRETTY_FUNCTION__);').length, 1);
  assert.strictEqual(unportableIn('localtime_r(&now, &parts);').length, 1);
  assert.strictEqual(unportableIn('char *buffer = (char *)alloca(16);').length, 1);
});

// The check is conservative on purpose: a false positive costs an argument, so
// names that merely contain a banned one must not match.
test('a longer name that contains a banned one is not a use', () => {
  assert.deepStrictEqual(unportableIn('int M_PICKED = 3;'), []);
  assert.deepStrictEqual(unportableIn('rn_setenv_shim(name, value);'), []);
  assert.deepStrictEqual(unportableIn('basalt::setenvForTests(name);'), []);
});

// Ordinary code must stay silent, including the things the other half of the
// script looks for.
test('plain code is not a finding', () => {
  assert.deepStrictEqual(unportableIn('const double kPi = 3.14159;\nfloat a = kPi / 2;'), []);
  assert.deepStrictEqual(unportableIn('std::vector<int> values;\nvalues.push_back(1);'), []);
});

// Both rule lists are reachable and non-empty, which is what stops a refactor
// from leaving the walk testing nothing.
test('the rules are wired up', () => {
  assert.ok(RULES.length > 0);
  assert.ok(UNPORTABLE.length > 0);
  for (const rule of UNPORTABLE) {
    assert.ok(rule.what && rule.instead && rule.use instanceof RegExp);
  }
});
