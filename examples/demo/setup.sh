#!/usr/bin/env bash
# Makes this directory look enough like an installed app for React Native's CLI.
#
# The CLI finds a project's React Native, and its platform plugins, by walking
# node_modules. This example is not installed from npm -- there is nothing to
# install yet -- so the two entries it needs are linked in by hand. That is the
# whole trick, and it is also a fair rehearsal of what `npm link` would do.
#
# Usage:  examples/demo/setup.sh [/path/to/react-native]

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/../.." && pwd)"

RN_DIR="${1:-${RN_DIR:-}}"
if [ -z "$RN_DIR" ]; then
  for candidate in "$REPO_ROOT/../react-native" "$REPO_ROOT/../react-native-0.87"; do
    [ -d "$candidate/node_modules/react-native" ] && { RN_DIR="$candidate"; break; }
  done
fi
[ -n "$RN_DIR" ] || { echo "error: pass a React Native checkout, or set RN_DIR" >&2; exit 1; }
RN_DIR="$(cd "$RN_DIR" && pwd)"

[ -d "$RN_DIR/node_modules/react-native" ] || {
  echo "error: $RN_DIR has no installed dependencies; run scripts/bootstrap.sh first" >&2
  exit 1
}

mkdir -p "$HERE/node_modules"
ln -sfn "$RN_DIR/node_modules/react-native" "$HERE/node_modules/react-native"
# Every package. The shared half carries the JavaScript platform layer and the
# bundler; each platform half carries a host and one run command. React Native's
# CLI reads a react-native.config.js out of each and merges them, which is how
# an app gets the platforms from one and a command from each of the others.
#
# All three even on a machine that can only run one of them, deliberately: a
# command whose host does not exist says which paths it looked in and how to
# build one, which is more useful than the command not existing.
ln -sfn "$REPO_ROOT/packages/basalt-core" "$HERE/node_modules/basalt-core"
for platform in gtk appkit win32; do
  ln -sfn "$REPO_ROOT/packages/basalt-core-$platform" \
    "$HERE/node_modules/basalt-core-$platform"
done

# react-native's cli.js refuses to run unless it finds this in the *project's*
# node_modules, so linking react-native alone is not enough.
mkdir -p "$HERE/node_modules/@react-native-community"
ln -sfn "$RN_DIR/node_modules/@react-native-community/cli" \
  "$HERE/node_modules/@react-native-community/cli"

echo "==> linked react-native from $RN_DIR"
echo "==> linked basalt-core and basalt-gtk from $REPO_ROOT/packages"
echo
echo "Now, from $HERE:"
echo "  RN_DIR=$RN_DIR node node_modules/react-native/cli.js run-linux"
