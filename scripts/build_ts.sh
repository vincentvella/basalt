#!/usr/bin/env bash
# Compiles the JavaScript packages into their `dist/`.
#
# Usage:  scripts/build_ts.sh [/path/to/react-native] [--watch]
#
# TypeScript comes from the React Native checkout's node_modules, which is where
# this repository borrows every node tool from -- see scripts/bundle.sh, which
# finds metro the same way. Nothing here has node_modules of its own, and that
# is deliberate: see e2e/package.json.
#
# Everything that bundles needs this to have run first, because `main` points
# into `dist/`. scripts/bundle.sh runs it for you.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

args=()
WATCH=false
for arg in "$@"; do
  case "$arg" in
    --watch) WATCH=true ;;
    *) args+=("$arg") ;;
  esac
done

RN_DIR="${args[0]:-${RN_DIR:-}}"
if [ -z "$RN_DIR" ]; then
  for candidate in "$REPO_ROOT/../react-native" "$REPO_ROOT/react-native-src"; do
    [ -d "$candidate/packages/react-native/ReactCommon" ] && { RN_DIR="$candidate"; break; }
  done
fi
[ -n "$RN_DIR" ] || { echo "error: pass the React Native checkout path, or set RN_DIR" >&2; exit 1; }
RN_DIR="$(cd "$RN_DIR" && pwd)"

TSC="$RN_DIR/node_modules/.bin/tsc"
[ -x "$TSC" ] || {
  echo "error: no tsc at $TSC" >&2
  echo "       run yarn in the React Native checkout first" >&2
  exit 1
}

# Which React Native the types come from, said out loud when it is not the one
# CI uses.
#
# This check is only ever as good as the checkout it runs against, and the two
# differ on purpose: `scripts/react-native.pin` is a release because
# applications pin releases, and development happens against `main`. So a type
# that exists only on main type-checks here and fails the CI job that gates
# everything else -- which is what `experimental_backgroundSize` did on
# 2026-10-09, green locally and red in the one job that runs first.
#
# A warning rather than an error: working against main is the normal case, and a
# line saying so is enough to make the next red obvious instead of mysterious.
RN_VERSION="$(node -e 'try { process.stdout.write(require(process.argv[1] + "/package.json").version) } catch (e) { process.stdout.write("unknown") }' "$RN_DIR/packages/react-native" 2>/dev/null || echo unknown)"
RN_PIN="$(grep -v '^#' "$REPO_ROOT/scripts/react-native.pin" | tr -d '[:space:]')"
if [ "v$RN_VERSION" != "$RN_PIN" ]; then
  echo "==> type-checking against React Native $RN_VERSION; CI pins $RN_PIN"
  echo "    a type that exists in only one of them will not be caught here"
fi

# Where TypeScript finds `react` and `react-native`.
#
# Nothing here has node_modules of its own -- see e2e/package.json -- so tsc
# cannot resolve either by walking up from the source. Metro is told the same
# thing through `nodeModulesPaths` in e2e/metro.config.js; this is the type
# checker's half of that arrangement, and it is generated rather than committed
# because the checkout it points at is wherever this machine put it.
#
# Which file holds React Native's types depends on what kind of React Native
# this is, and all three cases are real:
#
#   types_generated/  what `exports["."].types` names. Built at release, so a
#                     published package has it and a source checkout does not.
#   ReactNativeApi.d.ts  the API snapshot, checked in. Present at a release tag
#                     and on main, which makes it the one to prefer here.
#   types_DEPRECATED/ the old community typings. On main, and *not* at v0.87.1 --
#                     which is how this was got wrong the first time: it is what
#                     a main checkout has, so it worked locally and failed on CI
#                     against the pinned release with twenty errors that all said
#                     "Cannot find module 'react-native'".
#
# A consumer of this package resolves React Native's own types normally and
# never sees any of this.
#
# Which one is picked changes what is declared, so it changes what compiles.
# `types_generated/` pulls in src/types/globals.d.ts, which hand-declares Blob,
# File, FileReader and the animation-frame functions; `ReactNativeApi.d.ts` is a
# flat snapshot that declares no globals and references nothing, so against it
# those names come from @types/node or from nowhere. A developer who has
# generated the types is therefore compiling against a different environment
# from CI, and the `typecheck` job in ci.yml exists because of it.
#
# To get CI's answer on a machine that has generated them, move them aside:
#
#   mv "$RN_DIR/packages/react-native/types_generated" /tmp/aside && make ts
#   mv /tmp/aside "$RN_DIR/packages/react-native/types_generated"
RN_TYPES=""
for candidate in \
  "$RN_DIR/packages/react-native/types_generated/index.d.ts" \
  "$RN_DIR/packages/react-native/ReactNativeApi.d.ts" \
  "$RN_DIR/packages/react-native/types_DEPRECATED/index.d.ts"; do
  [ -f "$candidate" ] && { RN_TYPES="$candidate"; break; }
done
[ -n "$RN_TYPES" ] || {
  echo "error: found no React Native type declarations under $RN_DIR" >&2
  echo "       looked for types_generated/index.d.ts, ReactNativeApi.d.ts," >&2
  echo "       and types_DEPRECATED/index.d.ts" >&2
  exit 1
}

REACT_TYPES="$RN_DIR/node_modules/@types/react"
[ -d "$REACT_TYPES" ] || {
  echo "error: no @types/react at $REACT_TYPES" >&2
  echo "       run yarn in the React Native checkout first" >&2
  exit 1
}

# The paths written below are read by tsc, which on Windows is a native program
# while this script runs under Git Bash. `pwd` there answers /c/Users/..., which
# tsc reads as a drive-relative path and resolves to nothing. The symptom is not
# one missing module but all of them at once -- react, react-native and
# @types/node together -- because every entry in this file is equally
# unreachable, and the error list says "Cannot find module 'react'" rather than
# anything about paths.
#
# `cygpath -m` answers C:/Users/... : a native path that still uses forward
# slashes, so it needs no escaping inside JSON. On Linux and macOS there is no
# cygpath and nothing to convert.
to_json_path() {
  if command -v cygpath >/dev/null 2>&1; then
    cygpath -m "$1"
  else
    printf '%s' "$1"
  fi
}

PATHS_FILE="$REPO_ROOT/packages/basalt-core/tsconfig.paths.json"
cat > "$PATHS_FILE" <<JSON
{
  "//": "Generated by scripts/build_ts.sh. Not source; see .gitignore.",
  "compilerOptions": {
    "paths": {
      "react-native": ["$(to_json_path "$RN_TYPES")"],
      "react": ["$(to_json_path "$REACT_TYPES")"],
      "react/*": ["$(to_json_path "$REACT_TYPES")/*"],
      "basalt-core": ["$(to_json_path "$REPO_ROOT")/packages/basalt-core/dist/src/index.d.ts"],
      "basalt-core/*": ["$(to_json_path "$REPO_ROOT")/packages/basalt-core/*"],
      "basalt-subprocess": ["$(to_json_path "$REPO_ROOT")/packages/basalt-subprocess/dist/index.d.ts"],
      "basalt-navigation": ["$(to_json_path "$REPO_ROOT")/packages/basalt-navigation/dist/index.d.ts"]
    },
    "typeRoots": ["$(to_json_path "$RN_DIR")/node_modules/@types"],
    "types": ["node"]
  }
}
JSON

PACKAGES=(
  "$REPO_ROOT/packages/basalt-core"
  "$REPO_ROOT/packages/basalt-subprocess"
  "$REPO_ROOT/packages/basalt-navigation"
)

# Checked but not built. Everything in these is in react-native.config.js's
# load path, which React Native's CLI reads as plain CommonJS out of
# node_modules, so none of it can go behind a build. The files opt into
# checking with `// @ts-check`.
CHECKED=(
  "$REPO_ROOT/packages/basalt-gtk"
  "$REPO_ROOT/packages/basalt-appkit"
  "$REPO_ROOT/packages/basalt-win32"
)

for package in "${PACKAGES[@]}"; do
  [ -f "$package/tsconfig.json" ] || continue
  echo "==> building $(basename "$package")"
  if $WATCH; then
    "$TSC" --build "$package" --watch
  else
    "$TSC" --build "$package"
  fi
done

if ! $WATCH; then
  for package in "${CHECKED[@]}"; do
    [ -f "$package/tsconfig.json" ] || continue
    echo "==> checking $(basename "$package")"
    "$TSC" --noEmit -p "$package"
  done

  # The demo apps, last: they are the only thing here that reads the packages'
  # published types the way an app does, so a prop that was renamed in src/ and
  # not in the apps is caught here and nowhere else. They are checked against
  # the dist/ just built above, which is why this cannot run earlier.
  #
  # Note `$TSC` and not `npx tsc`: there is an unrelated package on npm called
  # `tsc`, and from a directory with no local typescript `npx` fetches *that*,
  # which prints a banner and exits 0. A green that means nothing.
  if [ -f "$REPO_ROOT/e2e/tsconfig.json" ]; then
    echo "==> checking e2e"
    "$TSC" --noEmit -p "$REPO_ROOT/e2e"
  fi
fi

echo "==> built"
