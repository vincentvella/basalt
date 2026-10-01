#!/usr/bin/env bash
# Builds the demo app into a Metro bundle the host can load.
#
# Usage:  scripts/bundle.sh [/path/to/react-native] [--dev|--prod] [--platform P]
#                          [--build-dir DIR] [--entry FILE] [--out NAME]
#
# Produces a production bundle by default. A --dev one does load and run since
# phase 32 -- a __DEV__ bundle pulls in LogBox, which reads the DevSettings
# TurboModule at module scope, and until this project supplied one outside dev
# mode that import took the app down. What it still does not get you is a
# development *mode*: nothing reloads it and nothing refreshes it, and LogBox's
# own images are not among the assets copied next to it.
#
# Development is Metro plus BASALT_DEV=1, not a --dev bundle:
#
#   scripts/metro.sh ../react-native
#   BASALT_DEV=1 ./build/basalt_gtk build/main.jsbundle.js BasaltDemo
#
# Built for the `linux` platform: packages/basalt-core supplies the
# Platform module and the Metro configuration that makes Metro resolve it, so
# an app sees Platform.OS === 'linux' and can use .linux.js files.
#
# --platform android still works, and is what this built before the platform
# package existed. Useful for telling apart "broken on this platform" from
# "broken everywhere".

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEV=false
PLATFORM=linux
# The app to bundle, relative to js/, and what to call the output. js/views.js
# is the one macOS can run: see its header.
ENTRY=index.js
OUT_NAME=main.jsbundle
# Which build tree to write into. Not always `build`: testing against a second
# React Native version means a second configure, and the bundle has to land
# beside the host that will load it.
BUILD_DIR=build

args=()
while [ $# -gt 0 ]; do
  case "$1" in
    --dev) DEV=true; shift ;;
    --prod) DEV=false; shift ;;
    --platform) PLATFORM="$2"; shift 2 ;;
    --entry) ENTRY="$2"; shift 2 ;;
    --out) OUT_NAME="$2"; shift 2 ;;
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    *) args+=("$1"); shift ;;
  esac
done

RN_DIR="${args[0]:-${RN_DIR:-}}"
if [ -z "$RN_DIR" ]; then
  # react-native-src is where CI fetches it and where scripts/wsl_setup.sh
  # clones it, and scripts/build_ts.sh already looks there. Without it this
  # found nothing on any machine not laid out like the one it was written on,
  # and everything that bundles without naming a checkout -- test_all.sh, every
  # scenario integration_test.py bundles for itself -- failed with "pass the
  # React Native checkout path". CI never saw it: it sets RN_DIR.
  for candidate in "$REPO_ROOT/../react-native" "$REPO_ROOT/react-native-src" \
                   "$HOME/Workspace/react-native"; do
    [ -d "$candidate/packages/react-native/ReactCommon" ] && { RN_DIR="$candidate"; break; }
  done
fi
[ -n "$RN_DIR" ] || { echo "error: pass the React Native checkout path, or set RN_DIR" >&2; exit 1; }
RN_DIR="$(cd "$RN_DIR" && pwd)"

[ -x "$RN_DIR/node_modules/.bin/metro" ] || {
  echo "error: no Metro in $RN_DIR/node_modules; run scripts/bootstrap.sh first" >&2
  exit 1
}

# basalt-core's `main` points into its `dist/`, so it has to be built
# before Metro can resolve it. Cheap when it is already up to date.
"$REPO_ROOT/scripts/build_ts.sh" "$RN_DIR" >/dev/null


OUT="$REPO_ROOT/$BUILD_DIR/$OUT_NAME"
mkdir -p "$REPO_ROOT/$BUILD_DIR"

echo "==> bundling js/$ENTRY (platform=$PLATFORM, dev=$DEV) against $RN_DIR"
if [ "$DEV" = true ]; then
  echo "    note: a --dev bundle runs, but nothing reloads or refreshes it." >&2
  echo "          For development use Metro and BASALT_DEV=1. See the header." >&2
fi
# metro appends .js to --out.
# The entry is resolved against projectRoot (js/), so it is named relative to
# that, not to this script's working directory.
RN_DIR="$RN_DIR" "$RN_DIR/node_modules/.bin/metro" build "$ENTRY" \
  --platform "$PLATFORM" \
  --dev "$DEV" \
  --out "$OUT" \
  --config "$REPO_ROOT/js/metro.config.js"

# The images the bundle `require()`s. Metro's `build` has no --assets-dest, so
# without this an <Image> from a require() lays out at the right size and draws
# nothing -- which is what kept LogBox's own icons off the screen. See the
# script's header for how the paths are worked out.
node "$REPO_ROOT/scripts/copy_assets.js" "$OUT.js" "$REPO_ROOT/js" "$REPO_ROOT/$BUILD_DIR"

echo "==> wrote $OUT.js"
