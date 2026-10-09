#!/usr/bin/env bash
#
# Every test this machine can run, in the order CI runs them.
#
# The sequence existed only inside .github/workflows/ci.yml, which meant the
# way to run everything locally was to read a YAML file and retype it -- and
# meant a suite could quietly stop being run by anybody outside CI. This is the
# same list, in one place, and CI's jobs and this script are meant to stay two
# spellings of one thing.
#
# What runs depends on what is built and what is installed, and each step says
# which when it skips. Nothing here builds a host: a test run that silently
# rebuilds is a test run whose failure might be yesterday's binary.
#
#   scripts/test_all.sh              # everything available
#   scripts/test_all.sh --quick      # skip the end-to-end and parity suites
#   scripts/test_all.sh --host gtk   # the other host, where a machine has two
#   scripts/test_all.sh --only unit  # just the steps whose name contains "unit"
#   scripts/test_all.sh --list       # the step names
#
# Narrowing matters: the whole thing is about a quarter of an hour per host, and
# most changes want one step. The end-to-end suite narrows further on its own --
# `integration_test.py -k window` runs one scenario -- and so do the unit
# binaries, which take test-name substrings as arguments.
#   BASALT_BUILD_DIR=build-x scripts/test_all.sh
#
# Exits non-zero if any step failed, after running all of them: one broken
# suite hiding the state of the other six is exactly what this is for.

set -uo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

build="${BASALT_BUILD_DIR:-build}"
quick=0
want=""
only=""
while (( $# )); do
  case "$1" in
    --quick) quick=1 ;;
    --host) want="${2:-}"; shift ;;
    --host=*) want="${1#*=}" ;;
    --only) only="${2:-}"; shift ;;
    --only=*) only="${1#*=}" ;;
    --list) sed -n 's/^ *step "\([^"]*\)".*/\1/p' "$0" |
              sed 's/\$suite/<host>_tests/' | sort -u; exit 0 ;;
    -h|--help) sed -n '2,23p' "$0" | sed 's|^#\ \?||'; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
  shift
done

# Which host is built, which is also which platform the suites below run as.
# No machine has more than one -- a host needs its toolkit -- except this
# repository's own development machine, where GTK is installed on macOS; there
# the AppKit one wins by default, because that is the platform the machine is.
#
# `--host gtk` is for exactly that machine. Both hosts are built from one
# directory there, and a change to the shared core has to be answered by both --
# which until now meant driving integration_test.py by hand, so the GTK side was
# the one that got run less.
host=""
platform=""
case "$want" in
  ""|auto) ;;
  appkit|macos) host="$build/basalt_appkit"; platform="macos" ;;
  gtk|linux) host="$build/basalt_gtk"; platform="linux" ;;
  win32|windows) host="$build/basalt_win32"; platform="windows" ;;
  *) echo "unknown host: $want (appkit, gtk or win32)" >&2; exit 2 ;;
esac
if [[ -n "$host" && ! -x "$host" ]]; then
  echo "no $want host built in $build" >&2; exit 2
fi
if [[ -z "$host" ]]; then
  if [[ -x "$build/basalt_appkit" ]]; then
    host="$build/basalt_appkit"; platform="macos"
  elif [[ -x "$build/basalt_gtk" ]]; then
    host="$build/basalt_gtk"; platform="linux"
  elif [[ -x "$build/basalt_win32.exe" || -x "$build/basalt_win32" ]]; then
    host="$build/basalt_win32"; platform="windows"
  fi
fi

failed=()
skipped=()

# Runs one step, prints a banner, and remembers the verdict. Output is left
# alone: a failing suite's own report is the useful part.
#
# `--only TEXT` narrows to steps whose name contains TEXT, because the whole
# thing takes a quarter of an hour per host and most of the time you changed one
# thing. A step nobody selected is not skipped -- it is not mentioned, so the
# summary stays about what you asked for.
step() {
  local name="$1"; shift
  if [[ -n "$only" && "$name" != *"$only"* ]]; then
    return 0
  fi
  printf '\n\033[1m== %s\033[0m\n' "$name"
  "$@"
  local status=$?
  if [[ $status -eq 0 ]]; then
    printf '\033[32mok\033[0m  %s\n' "$name"
  elif [[ $status -eq 77 ]]; then
    # 77 is automake's "skipped": the step ran and found it had nothing it
    # could honestly check here. compare_all.sh answers it with one host.
    printf '\033[33mskipped\033[0m  %s\n' "$name"
    skipped+=("$name")
  else
    printf '\033[31mFAILED\033[0m  %s\n' "$name"
    failed+=("$name")
  fi
}

skip() {
  if [[ -n "$only" && "$1" != *"$only"* ]]; then
    return 0
  fi
  printf '\n\033[1m== %s\033[0m\n\033[33mskipped\033[0m -- %s\n' "$1" "$2"
  skipped+=("$1")
}

# --- Unit suites -------------------------------------------------------------
#
# Each platform's own, which also carries core's: the tests in
# packages/basalt-core/native/tests belong to neither toolkit, so they
# are compiled into every suite rather than into one.
for suite in basalt_gtk_tests basalt_appkit_tests basalt_win32_tests; do
  if [[ -x "$build/$suite" ]]; then
    step "unit: $suite" "$build/$suite"
  elif [[ -x "$build/$suite.exe" ]]; then
    step "unit: $suite" "$build/$suite.exe"
  fi
done

# --- Node suites -------------------------------------------------------------
#
# These need nothing built and cannot answer differently on another platform,
# so CI runs each of them in exactly one job. Here they always run.
if command -v node >/dev/null 2>&1; then
  # `run-linux`, `run-macos` and `run-windows` are one function with three
  # names, so this checks all three wherever it runs.
  step "javascript build" "$root/scripts/build_ts.sh"
  step "cli" node --test scripts/test_cli.js
  # basalt-core's JavaScript that needs no React Native to run.
  step "platform javascript" node --test scripts/test_platform_js.js
  # A header reaching <cstdint> through windows.h took CI down for twenty-three
  # commits and nobody developing on Windows could see it, and `M_PI` in a shared
  # header did it again on a smaller scale. See the file. Its own tests run
  # first, a guard that has stopped matching being worse than no guard.
  step "include hygiene tests" node --test scripts/test_check_includes.js
  step "include hygiene" node scripts/check_includes.js
  # The backlog's own arithmetic: its table and each file's header are counts
  # maintained by hand, and on 2026-10-08 one of them had been wrong for long
  # enough that nobody could say since when. Same arrangement, tests first.
  step "backlog count tests" node --test scripts/test_check_backlog.js
  step "backlog counts" node scripts/check_backlog.js
else
  skip "node suites" "node is not installed"
fi

if [[ $quick -eq 1 ]]; then
  printf '\n--quick: end-to-end and parity suites not run\n'
elif [[ -z "$host" ]]; then
  skip "end-to-end" "no host built in $build"
  skip "cross-host parity" "no host built in $build"
else
  # --- End to end ------------------------------------------------------------
  #
  # Needs the demo bundled for this platform. Built here rather than depended
  # on, because a stale bundle is a suite that passes against yesterday's
  # JavaScript -- which has happened.
  # No checkout named: bundle.sh finds one the way build_ts.sh does, beside the
  # repository or inside it as react-native-src, or takes RN_DIR. Naming
  # ../react-native here made this step fail on every machine where React
  # Native lives anywhere else -- a Linux checkout from wsl_setup.sh among them.
  step "bundle the demo" scripts/bundle.sh --prod --platform "$platform"
  step "end-to-end" python3 scripts/integration_test.py --platform "$platform"

  # --- Cross-host parity -----------------------------------------------------
  #
  # Every app in e2e/ through every host that is built, diffed. Needs two hosts;
  # with one, compare_all.sh says so and exits 77, and this counts as skipped.
  # On Windows, BASALT_COMPARE_WSL=Ubuntu-24.04 supplies the second.
  step "cross-host parity" scripts/compare_all.sh
fi

# --- Verdict -----------------------------------------------------------------
printf '\n\033[1m== summary\033[0m\n'
if [[ ${#skipped[@]} -gt 0 ]]; then
  printf 'skipped: %s\n' "${skipped[*]}"
fi
if [[ ${#failed[@]} -eq 0 ]]; then
  printf '\033[32meverything that ran, passed\033[0m\n'
  exit 0
fi
printf '\033[31mfailed: %s\033[0m\n' "${failed[*]}"
exit 1
