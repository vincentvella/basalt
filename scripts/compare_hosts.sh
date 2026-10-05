#!/usr/bin/env bash
#
# Runs the same app through every host that is built and diffs the view tree
# each produced.
#
# This is the check the whole architecture is for. The same React, the same
# Fabric, the same Yoga, and three completely different view layers underneath
# -- if the trees differ, one of the platforms is wrong, and which one is a
# question the diff answers before anybody looks at a screenshot.
#
# It checks two things, because they fail differently. The trees have to match,
# and each host has to have got the platform it asked for: a bundle built for
# the wrong platform can still render correctly and be wrong about everything
# `Platform.OS` guards.
#
# Text is the exception, and BASALT_COMPARE_IGNORE_FRAMES=1 is how to say so.
# Pango over the system sans, Core Text over San Francisco and DirectWrite over
# Segoe UI are different shapers over different fonts: the same paragraph is a
# few points taller on one than another, and every frame below it shifts.
# Demanding equality there would mean the check could never be turned on for
# text at all. With frames ignored what is still compared is everything that
# must match -- the tree shape, the strings, the colours, the clip and opacity
# flags -- which is where a real bug would show up.
#
# Scrollbar geometry is ignored alongside the frames, for the same reason and
# in the same breath: a thumb's offset and length are computed from the content
# size, so text that measures taller on one host moves it there and nowhere
# else.
#
# ## Which hosts run
#
# Whichever are built, and at least two are needed for there to be a
# comparison. That is deliberately lenient: no machine has all three. A Mac with
# GTK installed has two, a Linux box has one, and a Windows box has two once WSL
# has a Linux host built (below) -- so in practice this runs on a developer's
# machine rather than in CI, where each host only exists on its own side.
#
# ## Usage
#
#   scripts/compare_hosts.sh                       # the views-only React app
#   scripts/compare_hosts.sh <app> [moduleName]    # build/<app>.<platform>.jsbundle.js
#   scripts/compare_hosts.sh e2e/demo.js ''         # one file, every host
#
# A React app needs a bundle per platform, because the platform is baked in at
# bundle time, so an app name is given rather than a path and the per-platform
# bundles are found by convention. A raw-Fabric script does not, so a path to
# one is used for every host as it stands.
#
# BASALT_BUILD_DIR moves where both the hosts and the bundles are looked for,
# which is what a second build tree needs -- testing against another React
# Native version means a second configure, and the hosts land beside it.

set -uo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

build="${BASALT_BUILD_DIR:-build}"
app="${1:-views}"
module="${2-BasaltViews}"

# Taps, in surface-root coordinates, forwarded to every host. They read the same
# variable, which is one of the things one env-var prefix bought. Each tap is a
# second apart and the first is at 1500ms, so a run with taps needs a longer
# quit than the default.
taps="${BASALT_COMPARE_TAP:-}"
quit_after="${BASALT_COMPARE_QUIT_AFTER_MS:-4000}"

# A path rather than an app name means a raw-Fabric script, used unchanged by
# every host. Told apart by being a file that exists, which is exact: no app is
# named after a path that is already on disk.
single_bundle=""
if [[ -f "$app" ]]; then
  single_bundle="$app"
  # A script talking to nativeFabricUIManager registers no module, and the
  # second argument defaults to a module name that would be wrong for it.
  [[ $# -ge 2 ]] || module=""
fi

# name : host binary : bundle suffix
HOSTS=(
  "linux:$build/basalt_gtk:linux"
  "macos:$build/basalt_appkit:macos"
  "windows:$build/basalt_win32.exe:windows"
)

present=()
for host in "${HOSTS[@]}"; do
  name="${host%%:*}"
  rest="${host#*:}"
  binary="${rest%%:*}"
  # Without .exe too: a Windows host built by a generator that does not append
  # it, and a hypothetical cross-compile, are both still this host.
  [[ -x "$binary" ]] || binary="${binary%.exe}"
  [[ -x "$binary" ]] || continue
  present+=("$name:$binary")
done

# A Linux host inside WSL, from Windows. Opt in by naming the distro:
#
#   BASALT_COMPARE_WSL=Ubuntu-24.04 scripts/compare_hosts.sh
#
# This is the only way one machine runs two of these hosts at once -- a Windows
# box cannot run AppKit, and GTK on Windows is not what CI builds -- which is
# why the comparison was never completed before WSL worked here. The Linux host
# and its bundles live in a checkout inside the distro (BASALT_COMPARE_WSL_REPO),
# because building under /mnt/c crosses WSL's 9p bridge on every file read.
# scripts/wsl_setup.sh makes that checkout and builds it the way the Linux CI
# job does.
wsl_distro="${BASALT_COMPARE_WSL:-}"
wsl_repo="${BASALT_COMPARE_WSL_REPO:-/root/basalt-core}"

# Every argument handed to wsl.exe is a path *inside* the distro, and Git
# Bash's MSYS runtime rewrites anything shaped like an absolute POSIX path on
# its way to a native program: /root/... arrives as C:/Program Files/Git/root/...,
# and `test -x` quietly answers no about a binary that is sitting right there.
#
# The same bug class bootstrap.sh hit with /DWIN32, and the opposite fix.
# There one argument had to be excluded and the paths beside it *needed*
# converting; here every argument is a Linux path by construction, so
# conversion is off for all of them.
wsl_run() {
  MSYS2_ARG_CONV_EXCL='*' MSYS_NO_PATHCONV=1 wsl.exe -d "$wsl_distro" -u root -e "$@"
}
if [[ -n "$wsl_distro" ]]; then
  already_linux=""
  for entry in "${present[@]}"; do
    [[ "${entry%%:*}" == linux ]] && already_linux=1
  done
  if [[ -z "$already_linux" ]] && command -v wsl.exe >/dev/null 2>&1; then
    if wsl_run test -x "$wsl_repo/build/basalt_gtk" 2>/dev/null; then
      present+=("linux:wsl")
    else
      echo "BASALT_COMPARE_WSL is set but $wsl_repo/build/basalt_gtk is not built in $wsl_distro" >&2
      exit 1
    fi
  fi
fi

if [[ ${#present[@]} -lt 2 ]]; then
  echo "need at least two hosts built to compare; found ${#present[@]}" >&2
  echo "  looked in $build for basalt_gtk, basalt_appkit and basalt_win32" >&2
  echo "  build them with: cmake --build $build" >&2
  exit 1
fi

out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT

# A native Windows binary cannot read an MSYS path, and mktemp hands out one on
# Git Bash. Everything else takes the path as it is.
native_path() {
  if command -v cygpath >/dev/null 2>&1; then
    cygpath -w "$1"
  else
    printf '%s' "$1"
  fi
}

status=0

for entry in "${present[@]}"; do
  name="${entry%%:*}"
  binary="${entry#*:}"

  if [[ "$binary" == wsl ]]; then
    # Everything on the far side of wsl.exe is a path inside the distro: the
    # bundle is the one the WSL checkout built, and the tree is written into
    # this run's temp directory through /mnt/c so the diff below can read it.
    # Xvfb because the host is a GTK program and the Linux CI job uses Xvfb --
    # a pass under WSLg's display server would not be the same claim.
    wsl_bundle="$wsl_repo/build/$app.linux.jsbundle.js"
    [[ -n "$single_bundle" ]] && wsl_bundle="$wsl_repo/$single_bundle"
    wsl_dump=$(wsl_run wslpath -u "$(native_path "$out/$name.txt")" | tr -d '\r')

    echo "running the $name host, in $wsl_distro..."
    wsl_run env \
      BUNDLE="$wsl_bundle" MODULE="$module" DUMP="$wsl_dump" REPO="$wsl_repo" \
      TAPS="$taps" QUIT="$quit_after" \
      bash -c '
        if [ ! -f "$BUNDLE" ]; then
          echo "missing $BUNDLE -- build it inside the distro with:" >&2
          echo "  scripts/bundle.sh react-native-src --platform linux --entry <app> --out <app>.linux.jsbundle" >&2
          exit 1
        fi
        if ! xdpyinfo -display :99 >/dev/null 2>&1; then
          Xvfb :99 -screen 0 1400x1000x24 >/tmp/xvfb.log 2>&1 &
          for _ in $(seq 1 30); do xdpyinfo -display :99 >/dev/null 2>&1 && break; sleep 0.5; done
        fi
        cd "$REPO"
        DISPLAY=:99 GDK_BACKEND=x11 BASALT_TEST_TAP="$TAPS" BASALT_DUMP_TREE="$DUMP" \
          BASALT_QUIT_AFTER_MS="$QUIT" ./build/basalt_gtk "$BUNDLE" "$MODULE"
      ' >"$out/$name.log" 2>&1 || true
    continue
  fi

  bundle="$single_bundle"
  [[ -n "$bundle" ]] || bundle="$build/$app.$name.jsbundle.js"
  if [[ ! -f "$bundle" ]]; then
    echo "missing $bundle -- build it with:" >&2
    echo "  scripts/bundle.sh --platform $name --entry $app --out $app.$name.jsbundle" >&2
    exit 1
  fi

  echo "running the $name host..."
  dump="$out/$name.txt"
  [[ "$name" == "windows" ]] && dump="$(native_path "$dump")"
  BASALT_TEST_TAP="$taps" BASALT_DUMP_TREE="$dump" BASALT_QUIT_AFTER_MS="$quit_after" \
    "$binary" "$bundle" "$module" >"$out/$name.log" 2>&1 || true
done

for entry in "${present[@]}"; do
  name="${entry%%:*}"
  if [[ ! -s "$out/$name.txt" ]]; then
    echo "the $name host wrote no tree; its log:" >&2
    tail -20 "$out/$name.log" >&2
    exit 1
  fi
done

# Each host should have got the platform its bundle was built for. Only checked
# when the app says so -- e2e/views.js logs it, a raw-Fabric script does not, and
# neither does a single bundle handed to every host.
for entry in "${present[@]}"; do
  name="${entry%%:*}"
  if grep -q 'Platform.OS is' "$out/$name.log"; then
    reported="$(sed -n 's/.*Platform\.OS is \([a-z]*\).*/\1/p' "$out/$name.log" | head -1)"
    if [[ -z "$single_bundle" && "$reported" != "$name" ]]; then
      echo "the $name host reports Platform.OS === '$reported'" >&2
      status=1
    else
      echo "  $name: Platform.OS is $reported"
    fi
  fi
done

# Frames stripped, when asked. Deliberately a whole-line substitution rather
# than a tolerance: "close enough" would need a number nobody can justify, and
# the sizes that differ are the ones this cannot check anyway.
if [[ -n "${BASALT_COMPARE_IGNORE_FRAMES:-}" ]]; then
  for entry in "${present[@]}"; do
    name="${entry%%:*}"
    # The scrollbar fields go with the frames. Both numbers in
    # `scrollbar-v=(offset,length)` are derived from the content's height, so
    # a paragraph that measures a few points taller on one shaper moves the
    # thumb on that host and nowhere else -- which is the same drift as a
    # frame, arriving in a field that is not one. What survives is the fact
    # that the scrollbar is there, which is the part that is not geometry.
    sed -E -e 's/ frame=\([^)]*\)//' \
           -e 's/ (scrollbar-[vh])=\([^)]*\)/ \1/g' \
           "$out/$name.txt" > "$out/$name.stripped"
    mv "$out/$name.stripped" "$out/$name.txt"
  done
  echo "  (frames ignored)"
fi

# Every pair, against the first host. Comparing all of them to one rather than
# to each other is the same check with fewer diffs to read: if A matches B and A
# matches C then B matches C, and if two disagree the one that is odd is the one
# named in the failure.
reference="${present[0]%%:*}"
for entry in "${present[@]:1}"; do
  name="${entry%%:*}"
  if ! diff -u "$out/$reference.txt" "$out/$name.txt"; then
    echo >&2
    echo "$reference and $name disagree." >&2
    status=1
  fi
done

if [[ $status -eq 0 ]]; then
  echo
  echo "all ${#present[@]} hosts produced the same tree:"
  sed 's/^/  /' "$out/$reference.txt"
fi

exit $status
