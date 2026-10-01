#!/usr/bin/env bash
#
# Every app in js/, through every host that is built, diffed.
#
# This is the summary that says where the platforms actually stand. Each app is
# tried twice: first demanding identical trees, then ignoring frames. An app
# that only passes the second is one whose layout depends on text measurement,
# where Pango over the system sans, Core Text over San Francisco and DirectWrite
# over Segoe UI cannot agree and never will -- so what is compared there is the
# tree shape, the strings, the colours, the roles and the flags.
#
# Needs at least two hosts. Bundles are built if missing, for whichever
# platforms are going to run -- inside the distro, for a Linux host in WSL.
#
#   scripts/compare_all.sh
#   BASALT_COMPARE_WSL=Ubuntu-24.04 scripts/compare_all.sh    # from Windows

set -uo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

build="${BASALT_BUILD_DIR:-build}"

# app entry : registered module name
APPS=(
  "views:BasaltViews"
  "press:BasaltPress"
  "pointerevents:BasaltPointerEvents"
  "focus:BasaltFocus"
  "share:BasaltShare"
  "scroll:BasaltScroll"
  "image:BasaltImage"
  "text:BasaltText"
  "a11y:BasaltA11y"
  "input:BasaltInput"
  "appearance:BasaltAppearance"
  "blob:BasaltBlob"
  "modules:BasaltModules"
  "controls:BasaltControls"
  "dialogs:BasaltDialogs"
  "menu:BasaltMenu"
  # js/window.js is deliberately absent: its tree prints the window's own
  # size, which differs between a host that was given one and a host whose
  # window manager had an opinion. That is a property of the machine rather
  # than of the platform, the same reason js/hover.js is not here.
  "probe:BasaltProbe"
  # js/hover.js is deliberately absent. Its tree depends on where the cursor
  # is: a box under the pointer takes a different background, and a window that
  # opens under someone's mouse is hovered before either host has drawn
  # anything. That is a property of the machine rather than of the platform, so
  # it makes a bad parity fixture -- what checks hover is the end-to-end suite,
  # which drives the pointer itself and runs on both hosts.
  #
  # The real demo, last: the richest app there is, and the one whose tree
  # agreeing means the most.
  "index:BasaltDemo"
)

# Which hosts exist, and therefore which platforms need bundles. Kept in step
# with compare_hosts.sh by asking the same questions of the same places.
platforms=()
[[ -x "$build/basalt_gtk" ]] && platforms+=("linux")
[[ -x "$build/basalt_appkit" ]] && platforms+=("macos")
{ [[ -x "$build/basalt_win32.exe" ]] || [[ -x "$build/basalt_win32" ]]; } && platforms+=("windows")

# A Linux host inside WSL counts as the Linux host when there is no local one.
# See compare_hosts.sh for why this is the only way one machine gets two hosts,
# and for why every path handed to wsl.exe has conversion turned off.
wsl_distro="${BASALT_COMPARE_WSL:-}"
wsl_repo="${BASALT_COMPARE_WSL_REPO:-/root/basalt-core}"
wsl_run() {
  MSYS2_ARG_CONV_EXCL='*' MSYS_NO_PATHCONV=1 wsl.exe -d "$wsl_distro" -u root -e "$@"
}
linux_in_wsl=""
if [[ -n "$wsl_distro" && ! " ${platforms[*]} " =~ " linux " ]]; then
  if wsl_run test -x "$wsl_repo/build/basalt_gtk" 2>/dev/null; then
    platforms+=("linux")
    linux_in_wsl=1
  else
    echo "BASALT_COMPARE_WSL is set but $wsl_repo/build/basalt_gtk is not built in $wsl_distro" >&2
    exit 1
  fi
fi

# 77 rather than 1: automake's "skipped", which test_all.sh reads as such. One
# host is not a broken comparison, it is no comparison -- the normal state of a
# Linux box, and of Windows until BASALT_COMPARE_WSL names a distro -- and
# counting it as a failure meant the full suite could never pass on any machine
# but the one with GTK installed beside AppKit. Still non-zero, so a person who
# asked for a comparison directly is told it did not happen.
if [[ ${#platforms[@]} -lt 2 ]]; then
  echo "need at least two hosts built to compare; found ${#platforms[@]} in $build" >&2
  if [[ "$(uname -s)" == MINGW* || "$(uname -s)" == MSYS* ]] && [[ -z "$wsl_distro" ]]; then
    echo "on Windows, BASALT_COMPARE_WSL=Ubuntu-24.04 counts the GTK host in WSL as the second; see docs/TESTING.md" >&2
  fi
  exit 77
fi
echo "hosts: ${platforms[*]}${linux_in_wsl:+ (linux in $wsl_distro)}"
echo

quit_after="${BASALT_COMPARE_QUIT_AFTER_MS:-2800}"
failures=0

# BASALT_COMPARE_SHARD=I/N runs the Ith of N shards, 1-based, striding rather
# than slicing -- the same arrangement scripts/integration_test.py uses and for
# the same reason: apps sit in the list in the order they were written, so a
# contiguous slice hands one shard several heavy ones. `index` counts every app
# so the stride is over the whole list rather than over what survives it.
#
# Each app is two host runs and a diff, and nothing is shared between apps, so
# this is free to split. It is the slowest step in the one job that has both
# hosts.
shard_index=0
shard_count=1
if [[ -n "${BASALT_COMPARE_SHARD:-}" ]]; then
  if [[ ! "$BASALT_COMPARE_SHARD" =~ ^[0-9]+/[0-9]+$ ]]; then
    echo "BASALT_COMPARE_SHARD wants I/N, not $BASALT_COMPARE_SHARD" >&2
    exit 1
  fi
  shard_index="${BASALT_COMPARE_SHARD%%/*}"
  shard_count="${BASALT_COMPARE_SHARD##*/}"
  if (( shard_count < 1 || shard_index < 1 || shard_index > shard_count )); then
    echo "BASALT_COMPARE_SHARD $BASALT_COMPARE_SHARD is out of range" >&2
    exit 1
  fi
  echo "shard $shard_index of $shard_count"
  echo
fi

index=0
# Counted rather than taken from the list: with a shard those are different
# numbers, and reporting the list would claim eighteen comparisons from three.
compared=0
for entry in "${APPS[@]}"; do
  if (( shard_count > 1 )); then
    index=$(( index + 1 ))
    if (( (index - 1) % shard_count != shard_index - 1 )); then
      continue
    fi
  fi
  compared=$(( compared + 1 ))
  name="${entry%%:*}"
  module="${entry##*:}"

  for platform in "${platforms[@]}"; do
    if [[ "$platform" == linux && -n "$linux_in_wsl" ]]; then
      if ! wsl_run test -f "$wsl_repo/build/$name.linux.jsbundle.js"; then
        echo "  building $name.linux.jsbundle.js in $wsl_distro"
        wsl_run bash -lc "cd '$wsl_repo' && scripts/bundle.sh react-native-src \
          --platform linux --entry '$name.js' --out '$name.linux.jsbundle'" >/dev/null 2>&1
      fi
      continue
    fi
    bundle="$build/$name.$platform.jsbundle.js"
    if [[ ! -f "$bundle" ]]; then
      echo "  building $bundle"
      scripts/bundle.sh --platform "$platform" --entry "$name.js" \
        --out "$name.$platform.jsbundle" --build-dir "$build" >/dev/null 2>&1
    fi
  done

  if BASALT_COMPARE_QUIT_AFTER_MS="$quit_after" \
     scripts/compare_hosts.sh "$name" "$module" >/dev/null 2>&1; then
    printf "  %-12s identical, frames included\n" "$name"
  elif BASALT_COMPARE_IGNORE_FRAMES=1 BASALT_COMPARE_QUIT_AFTER_MS="$quit_after" \
       scripts/compare_hosts.sh "$name" "$module" >/dev/null 2>&1; then
    printf "  %-12s identical, frames ignored\n" "$name"
  else
    printf "  %-12s DIFFERS\n" "$name"
    # And say how, here, now. Telling somebody to rerun it locally is advice
    # that only works when the disagreement is reproducible -- and the one
    # occurrence of this so far was not: `image` differed on a CI run, passed on
    # a rerun of the same commit, and passes locally. The evidence was thrown
    # away by the redirection above, so there was nothing to diagnose and
    # nothing to do but guess.
    #
    # Frames ignored, because that is the comparison that failed: the run above
    # it is allowed to differ on geometry. Re-run rather than captured from the
    # earlier invocation so the diff is of the same comparison that decided.
    echo "  --- how they differ ---"
    BASALT_COMPARE_IGNORE_FRAMES=1 BASALT_COMPARE_QUIT_AFTER_MS="$quit_after" \
      scripts/compare_hosts.sh "$name" "$module" 2>&1 | sed 's/^/  /' || true
    echo "  --- end ---"
    failures=$((failures + 1))
  fi
done

echo
if [[ $failures -eq 0 ]]; then
  echo "the ${#platforms[@]} hosts agree on all $compared apps"
else
  echo "$failures of $compared disagree; each diff is printed above" >&2
fi
exit $failures
