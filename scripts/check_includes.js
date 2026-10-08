/**
 * Finds a standard header a file uses and does not include, and a spelling that
 * does not exist on all three toolchains.
 *
 * Two checks, one walk, because they are the same idea: a compiler nobody here
 * can run would have caught both, so this is the stand-in that runs in a second
 * on a laptop.
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

/**
 * Spellings that do not exist on all three toolchains, with what to do instead.
 *
 * The rule above is "you used something and forgot its header". This one is
 * "there is no header": the name is a POSIX or compiler extension that one of
 * the three toolchains does not have, so the fix is to write something else.
 *
 * `M_PI` is here because it cost a red Windows build on 2026-10-08. MSVC's
 * <cmath> defines the POSIX math constants only behind `_USE_MATH_DEFINES`,
 * which `core/cmake/ReactNativeCore.cmake` sets for React Native's own targets
 * and deliberately not for this project's -- so `core/Gradients.h` compiled on
 * the two hosts its author could build and not on the third. Everything else
 * here is unused in the tree today and is listed so that the first person to
 * reach for it is told in a second rather than in a twenty-minute CI round trip.
 *
 * Each of these is legitimate inside a platform branch, which is why a file that
 * mentions `_WIN32` anywhere is skipped: it has already thought about the
 * question. `core/CrashHandler.cpp` is the case -- `pthread.h`, `unistd.h` and
 * `ssize_t` under `#if !defined(_WIN32)` -- and a check that flagged it would be
 * noise rather than a finding.
 */
const UNPORTABLE = [
  {
    what: 'a POSIX math constant',
    use: /\bM_(?:PI|PI_2|PI_4|1_PI|2_PI|2_SQRTPI|E|LOG2E|LOG10E|LN2|LN10|SQRT2|SQRT1_2)\b/,
    instead: 'declare the constant yourself; MSVC has these only behind _USE_MATH_DEFINES, '
      + 'which this project does not set for its own sources',
  },
  {
    what: 'a POSIX string comparison',
    use: /\b(?:strcasecmp|strncasecmp)\s*\(/,
    instead: 'compare case by case yourself; MSVC spells it _stricmp',
  },
  {
    what: "a compiler's own function-name macro",
    use: /\b__PRETTY_FUNCTION__\b/,
    instead: 'use __func__, which is standard; MSVC spells the decorated one __FUNCSIG__',
  },
  {
    what: 'a POSIX time function',
    use: /\b(?:localtime_r|gmtime_r|asctime_r|ctime_r)\s*\(/,
    instead: 'MSVC has only the _s forms; branch on _WIN32 or use <chrono>',
  },
  {
    what: 'a stack allocation that is not standard',
    use: /\balloca\s*\(/,
    instead: 'use a std::vector or a fixed-size array',
  },
  {
    what: 'a POSIX environment call',
    use: /(?<![_\w])(?:setenv|unsetenv)\s*\(/,
    instead: 'MSVC has _putenv_s; branch on _WIN32 as core/tests/test_settle.cpp does',
  },
];

/**
 * The same text with its comments taken out.
 *
 * Needed only by the unportable check, and needed badly: the comment explaining
 * why not to use `M_PI` contains `M_PI`, and so does this file. A string
 * literal holding `//` loses its tail, which does not matter -- nothing here
 * looks for anything that could live in a URL.
 */
function stripComments(text) {
  return text.replace(/\/\*[\s\S]*?\*\//g, ' ').replace(/\/\/[^\n]*/g, ' ');
}

/** Every unportable spelling in one file's own text. */
function unportableIn(text) {
  if (text.includes('_WIN32')) {
    return [];
  }
  const code = stripComments(text);
  return UNPORTABLE.filter(rule => rule.use.test(code));
}

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
  const unportable = [];
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

      for (const rule of unportableIn(own)) {
        unportable.push(
          `${path.join(root, entry)}: uses ${rule.what}, which one toolchain lacks -- ${rule.instead}`,
        );
      }
    }
  }

  for (const finding of findings.concat(unportable)) {
    console.log(finding);
  }
  console.log(
    `${checked} files checked, ${findings.length} missing include${findings.length === 1 ? '' : 's'}`
      + `, ${unportable.length} unportable spelling${unportable.length === 1 ? '' : 's'}`,
  );
  return findings.length === 0 && unportable.length === 0 ? 0 : 1;
}

// Required by scripts/test_check_includes.js, which feeds text through the two
// checks directly: a guard with no test of its own can stop guarding quietly.
module.exports = {RULES, UNPORTABLE, stripComments, unportableIn};

if (require.main === module) {
  process.exitCode = main();
}
