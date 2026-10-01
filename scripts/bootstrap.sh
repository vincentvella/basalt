#!/usr/bin/env bash
# Bootstraps a development checkout.
#
# The real script moved into the npm package, at
# packages/basalt-core/native/bootstrap.sh, along with everything else
# native, so that installing the package brings it. This forwards to it with the
# one thing a checkout knows and the package cannot assume: that third_party
# belongs at the root of the repository rather than inside node_modules.
#
# Usage:  scripts/bootstrap.sh [/path/to/react-native] [--force]

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export BASALT_THIRD_PARTY="${BASALT_THIRD_PARTY:-$REPO_ROOT/third_party}"
exec "$REPO_ROOT/packages/basalt-core/native/bootstrap.sh" "$@"
