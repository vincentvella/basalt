/**
 * Finds a standard header a file uses and does not include.
 *
 * This exists because of one bug that took CI down for twenty-three commits.
 * `core/DevBundle.h` declared a function taking a `uint32_t` and included
 * `<optional>` and `<string>`. It compiled on Windows, because windows.h had
 * already been pulled in behind it, and on macOS for a similar reason. On Linux
 * with libstdc++ it did not, and the Linux job is the only one that builds the
 * whole thing -- so the failure was real, immediate, and invisible from the
 * machine the work was being done on.
 *
 * A compiler is the right tool for this and there is no substitute. What this
 * is instead is the cheapest possible stand-in for the *one* compiler nobody
 * developing here can run: it needs no build, no toolkit and no React Native,
 * so it can run on every platform's CI job and on a developer's machine before
 * the twenty-minute round trip.
 *
 * Deliberately conservative. It knows a short list of names whose header is
 * unambiguous, and it counts a project header included by the file as
 * providing whatever that header includes -- one level, which is enough for the
 * "the .cpp gets it from its own .h" arrangement this codebase uses everywhere.
 * A false positive costs an argument; a false negative costs what it already
 * cost once.
 *
 * Run with:  node scripts/check_includes.js
 *
 * @format
 */

'use strict';

const fs = require('node:fs');
const path = require('node:path');

const REPO = path.resolve(__dirname, '..');

// Where this project's own C++ lives. React Native's is not ours to police.
const ROOTS = [
  'packages/basalt-core/native/core',
  'packages/basalt-core/native/tests',
  'packages/basalt-gtk/native/gtk',
  'packages/basalt-gtk/native/tests',
  'packages/basalt-appkit/native/appkit',
  'packages/basalt-appkit/native/tests',
  'packages/basalt-win32/native/win32',
  'packages/basalt-win32/native/tests',
];

/**
 * Each rule is a use, and the headers any one of which supplies it.
 *
 * Only names with one obvious home. `std::move` is not here -- it is in
 * `<utility>` and reachable from most of the library besides -- and neither is
 * anything a platform header is entitled to provide.
 */
const RULES = [
  {
    what: 'a fixed-width integer type',
    use: /\b(?:u?int(?:8|16|32|64)_t|uintptr_t|intptr_t)\b/,
    headers: ['<cstdint>', '<stdint.h>'],
  },
  {
    what: 'std::min, std::max or an <algorithm> function',
    use: /\bstd::(?:min|max|clamp|sort|stable_sort|any_of|all_of|none_of|find_if|copy|fill|remove_if)\s*[(<]/,
    headers: ['<algorithm>'],
  },
  {
    what: 'a <cmath> function',
    use: /\bstd::(?:isnan|isinf|fabs|lround|llround|floor|ceil|round|sqrt|pow|hypot)\s*\(/,
    headers: ['<cmath>'],
  },
  {
    what: 'a <cstring> function',
    use: /\bstd::(?:memcpy|memmove|memset|memcmp|strlen|strcmp|strncmp)\s*\(/,
    headers: ['<cstring>'],
  },
  {
    what: 'std::function',
    use: /\bstd::function\s*</,
    headers: ['<functional>'],
  },
  {
    what: 'std::array',
    use: /\bstd::array\s*</,
    headers: ['<array>'],
  },
  {
    what: 'a standard exception type',
    use: /\bstd::(?:runtime_error|logic_error|invalid_argument|out_of_range)\b/,
    headers: ['<stdexcept>'],
  },
  {
    what: 'std::numeric_limits',
    use: /\bstd::numeric_limits\s*</,
    headers: ['<limits>'],
  },
];

const SOURCE = /\.(?:h|hpp|cpp|mm|m)$/;

/**
 * Everything a translation unit can be said to have asked for: its own text,
 * plus the text of every project header it includes by name.
 *
 * One level deep on purpose. Following the whole graph would make this a
 * compiler, and the arrangement it needs to understand is the shallow one --
 * a .cpp reaching <cstdint> through its own .h, or a platform file reaching
 * <functional> through core/PlatformServices.h.
 */
function askedFor(file) {
  const own = fs.readFileSync(file, 'utf8');
  let all = own;

  for (const match of own.matchAll(/#include "([^"]+)"/g)) {
    // Beside the file, or anywhere else in the project by basename -- the
    // include directories are set per target and this does not read CMake.
    const beside = path.join(path.dirname(file), match[1]);
    const found = fs.existsSync(beside) ? beside : findByName(path.basename(match[1]));
    if (found) {
      all += fs.readFileSync(found, 'utf8');
    }
  }
  return {own, all};
}

let index = null;

function findByName(name) {
  if (index === null) {
    index = new Map();
    for (const root of ROOTS) {
      const dir = path.join(REPO, root);
      if (!fs.existsSync(dir)) {
        continue;
      }
      for (const entry of fs.readdirSync(dir)) {
        if (/\.(?:h|hpp)$/.test(entry) && !index.has(entry)) {
          index.set(entry, path.join(dir, entry));
        }
      }
    }
  }
  return index.get(name) ?? null;
}

function main() {
  const findings = [];
  let checked = 0;

  for (const root of ROOTS) {
    const dir = path.join(REPO, root);
    if (!fs.existsSync(dir)) {
      continue;
    }
    for (const entry of fs.readdirSync(dir).sort()) {
      if (!SOURCE.test(entry)) {
        continue;
      }
      const file = path.join(dir, entry);
      const {own, all} = askedFor(file);
      checked++;

      for (const rule of RULES) {
        if (!rule.use.test(own)) {
          continue;
        }
        if (rule.headers.some(header => all.includes(`#include ${header}`))) {
          continue;
        }
        findings.push(`${path.join(root, entry)}: uses ${rule.what} without ${rule.headers[0]}`);
      }
    }
  }

  for (const finding of findings) {
    console.log(finding);
  }
  console.log(
    `${checked} files checked, ${findings.length} missing include${findings.length === 1 ? '' : 's'}`,
  );
  return findings.length === 0 ? 0 : 1;
}

process.exitCode = main();
