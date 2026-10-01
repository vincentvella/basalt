#!/usr/bin/env bash
#
# Build Skia for Linux, for @shopify/react-native-skia 2.11.1.
#
# The package publishes prebuilt archives for Apple and Android and nothing for
# a Linux desktop, so a GTK host that wants Skia has to build it. This produces
# the nine archives the package's own non-Apple CMake build imports, laid out
# the way its Android prebuilt is laid out.
#
# ## Run it in WSL2, not on Windows
#
# Windows cannot produce Linux archives. WSL2 can, and it is the best host
# available here: it is x86_64 Linux, which is exactly what CI's ubuntu-24.04
# is, so what this produces is what CI could consume -- unlike an arm64 VM on a
# Mac, whose output no runner in this project could link.
#
# The same distro scripts/wsl_setup.sh already sets up, for the same reason it
# gives: a build under /mnt/c crosses WSL's 9p bridge on every file read, and
# Skia is tens of thousands of files.
#
#     wsl -d Ubuntu-24.04 -u root -e bash scripts/wsl_setup.sh    # once
#     wsl -d Ubuntu-24.04 -u root -e bash scripts/build_skia_linux.sh
#
# wsl_setup.sh installs most of what this needs -- clang, ninja, pkg-config,
# git, python3. What it has no reason to install is Skia's font and GL headers,
# and the preflight below names them.
#
# ## What is pinned, and why it has to be
#
# The npm package ships Skia's *headers* in cpp/skia/include. Archives built
# from a different Skia revision than those headers will link and then misbehave
# in ways that look like memory corruption, so the revision is not a detail:
#
#   skia      chromium.googlesource.com/skia, branch chrome/m152
#   commit    2a9b593bab4b2fd019fa494c8d401ff1fab0b883
#
# That is the externals/skia submodule at react-native-skia's v2.11.1 tag, and
# it agrees with the package: cpp/skia/include/core/SkMilestone.h says
# SK_MILESTONE 152, and the prebuilt dependencies are all versioned 152.0.0.
#
# The GN arguments below are react-native-skia's own, read from
# packages/skia/scripts/skia-configuration.ts at v2.11.1, rather than invented.
# One of them is load-bearing and non-obvious enough to repeat their comment:
# m152 turns PartitionAlloc on by default for clang builds, which leaves
# raw_ptr/PartitionAddressSpace undefined when linking against a prebuilt
# libskia.a, because the allocator lives in a target nobody ships. Hence
# skia_use_partition_alloc=false.
#
# ## Where this deliberately differs from their Android build
#
# There is no Linux target in react-native-skia's configuration at all, so the
# Linux argument set is Android's with the NDK removed and three changes, each
# of which is a judgement call rather than a transcription:
#
#   system freetype and fontconfig, where Android bundles freetype. A GTK host
#   already links pango and cairo, which drag in freetype and fontconfig, and
#   two freetypes in one process is a real crash rather than a tidiness
#   complaint. It also gives us SkFontMgr_New_FontConfig, which is what
#   RNSkPlatformContext::createFontMgr wants to return on Linux.
#
#   embedded ICU data, where Android uses runtime ICU. Android does that to keep
#   the APK small at the cost of shipping icudtl.dat and finding it at runtime.
#   A desktop host does not have that constraint and does not need that bug.
#   Set SKIA_RUNTIME_ICU=1 to go the other way.
#
#   -fPIC, which Android gets implicitly from the NDK. Ubuntu links
#   position-independent executables by default, and a non-PIC archive fails at
#   the very end of a long link with relocation errors against R_X86_64_32S.
#
set -euo pipefail

SKIA_COMMIT=2a9b593bab4b2fd019fa494c8d401ff1fab0b883
SKIA_BRANCH=chrome/m152
SKIA_REPO=https://chromium.googlesource.com/skia

# Embedded ICU data by default; see the note above.
SKIA_RUNTIME_ICU=${SKIA_RUNTIME_ICU:-0}

# Not $HOME directly: the invocation this script's own header recommends --
# `wsl -d Ubuntu-24.04 -u root -e bash scripts/build_skia_linux.sh` -- runs a
# non-login shell with HOME unset, and $HOME/skia-linux-build is then
# /skia-linux-build at the root of the filesystem. The passwd entry is the
# answer the shell would have given had it been a login one.
HOME_DIR=${HOME:-$(getent passwd "$(id -u)" | cut -d: -f6)}
[ -n "$HOME_DIR" ] || HOME_DIR=/tmp
WORK=${SKIA_BUILD_DIR:-$HOME_DIR/skia-linux-build}
# Named after `uname -m`, the way the package names Android's ABIs: the
# archives are per architecture and nothing but the architecture's own name
# tells two sets apart on disk.
case "$(uname -m)" in
  x86_64)  SKIA_CPU=x64;   SKIA_ARCH=x86_64 ;;
  aarch64) SKIA_CPU=arm64; SKIA_ARCH=aarch64 ;;
  *)       SKIA_CPU=$(uname -m); SKIA_ARCH=$(uname -m) ;;
esac
OUT_NAME=linux-$SKIA_ARCH
DEST=${SKIA_OUT_DIR:-$WORK/libs/linux/$SKIA_ARCH}

# The nine archives the package's non-Apple CMake build imports. Named rather
# than globbed: a missing one is a link failure in somebody else's symbols much
# later, and a list says which we expected. libpathops.a is not here on purpose
# -- android/CMakeLists.txt only imports it inside its SK_GRAPHITE branch, and
# the Android prebuilt does not ship it either.
ARCHIVES=(
  libskia.a
  libskshaper.a
  libsvg.a
  libskottie.a
  libsksg.a
  libjsonreader.a
  libskparagraph.a
  libskunicode_core.a
  libskunicode_icu.a
)

say() { printf '\n\033[1m==> %s\033[0m\n' "$*"; }
note() { printf '    %s\n' "$*"; }
die() { printf '\n\033[31merror: %s\033[0m\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------- preflight --

say "checking this machine can do the job"

# wsl_setup.sh runs as root, where sudo is usually not installed at all.
if [ "$(id -u)" = "0" ]; then SUDO=""; else SUDO="sudo "; fi

[ "$(uname -s)" = "Linux" ] || die "this builds Linux archives and must run on Linux. In WSL2: wsl -d Ubuntu"

ARCH=$(uname -m)
if [ "$ARCH" != "x86_64" ]; then
  note "architecture is $ARCH, not x86_64."
  note "That is a valid thing to build, but CI's ubuntu-24.04 is x86_64 and"
  note "could not link the result. Set SKIA_ALLOW_ARCH=1 if you meant it."
  [ "${SKIA_ALLOW_ARCH:-0}" = "1" ] || die "refusing to build for $ARCH by accident"
fi

# A WSL2 build under /mnt/c goes through the Windows filesystem translation
# layer for every one of Skia's tens of thousands of files. It does finish. It
# takes so much longer that it reads as a hang, so this refuses rather than
# letting somebody discover it overnight.
case "$WORK" in
  /mnt/*)
    die "$WORK is on the Windows filesystem, where this build is unusably slow.
    Use a path inside WSL's own filesystem, such as \$HOME/skia-linux-build."
    ;;
esac

MISSING=()
for tool in git python3 clang clang++ ninja pkg-config; do
  command -v "$tool" >/dev/null 2>&1 || MISSING+=("$tool")
done
for pkg in freetype2 fontconfig gl egl; do
  pkg-config --exists "$pkg" 2>/dev/null || MISSING+=("pkg-config:$pkg")
done

if [ ${#MISSING[@]} -gt 0 ]; then
  note "missing: ${MISSING[*]}"
  echo
  note "This script diagnoses rather than installs -- your toolchain is yours."
  note "On Ubuntu the line is:"
  echo
  echo "  ${SUDO}apt-get update && ${SUDO}apt-get install -y \\"
  echo "    build-essential clang git python3 ninja-build pkg-config \\"
  echo "    libfreetype6-dev libfontconfig1-dev libgl1-mesa-dev libegl1-mesa-dev"
  echo
  [ "${SKIA_INSTALL_DEPS:-0}" = "1" ] || die "install the above, then run this again"
  say "installing dependencies because SKIA_INSTALL_DEPS=1"
  ${SUDO}apt-get update
  ${SUDO}apt-get install -y build-essential clang git python3 ninja-build pkg-config \
    libfreetype6-dev libfontconfig1-dev libgl1-mesa-dev libegl1-mesa-dev
fi

# Skia's checkout plus its third_party deps plus one release build is about
# 20GB. Checked up front because running out at hour two is the worst way to
# find out.
AVAIL_GB=$(df -BG --output=avail "$(dirname "$WORK")" 2>/dev/null | tail -1 | tr -dc '0-9')
if [ -n "$AVAIL_GB" ] && [ "$AVAIL_GB" -lt 25 ]; then
  die "only ${AVAIL_GB}GB free under $(dirname "$WORK"); this needs about 25GB"
fi
note "free space: ${AVAIL_GB:-unknown}GB"
note "build root: $WORK"
note "jobs:       $(nproc)"

mkdir -p "$WORK"

# ------------------------------------------------------------------- skia ---

if [ ! -d "$WORK/skia/.git" ]; then
  say "fetching Skia at $SKIA_BRANCH"
  # Not a shallow clone: the pinned commit is not the branch tip, and a
  # --depth 1 clone cannot check it out. Fetching the one commit directly keeps
  # this from pulling Skia's whole history anyway.
  git init -q "$WORK/skia"
  git -C "$WORK/skia" remote add origin "$SKIA_REPO"
  if git -C "$WORK/skia" fetch --depth 1 origin "$SKIA_COMMIT" 2>/dev/null; then
    git -C "$WORK/skia" checkout -q FETCH_HEAD
  else
    # Not every server allows asking for a bare commit. Falling back to the
    # branch costs history we do not want but always works, and the checkout
    # below is still the pinned commit rather than whatever the tip is today.
    note "server would not serve the commit directly; fetching $SKIA_BRANCH"
    git -C "$WORK/skia" fetch origin "$SKIA_BRANCH"
    git -C "$WORK/skia" checkout -q "$SKIA_COMMIT"
  fi
else
  note "Skia already fetched"
fi

HAVE=$(git -C "$WORK/skia" rev-parse HEAD)
[ "$HAVE" = "$SKIA_COMMIT" ] || die "Skia is at $HAVE, expected $SKIA_COMMIT.
    Delete $WORK/skia and run again."
note "Skia at $SKIA_COMMIT"

say "syncing Skia's third-party dependencies"
note "First time through this pulls a few GB."
( cd "$WORK/skia" && python3 tools/git-sync-deps )

# --------------------------------------------------------------------- gn --
#
# Skia's own bin/fetch-gn, rather than depot_tools.
#
# depot_tools was here first and does not work from a plain clone: its `gn` is
# a wrapper that fetches the real binary through cipd on first use, and nothing
# short of `gclient` triggers that. Cloned and put on PATH it answers every
# invocation with
#
#   python3_bin_reldir.txt not found. need to initialize depot_tools by
#   running gclient, update_depot_tools or ensure_bootstrap
#
# and configure died there the first time this script was run end to end.
# Bootstrapping it is another moving part for the one tool it was providing:
# ninja here is the system's, so gn was all depot_tools was for. Skia ships a
# downloader for exactly that, it needs no initialisation, and it lands the
# binary in the checkout this script already pins.
#
# After git-sync-deps, because fetch-gn lives in the Skia tree.
if [ ! -x "$WORK/skia/bin/gn" ]; then
  say "fetching gn"
  ( cd "$WORK/skia" && python3 bin/fetch-gn )
else
  note "gn already fetched"
fi
GN="$WORK/skia/bin/gn"
[ -x "$GN" ] || die "no gn at $GN, after bin/fetch-gn said it had fetched one"

# --------------------------------------------------------------- configure --

if [ "$SKIA_RUNTIME_ICU" = "1" ]; then
  ICU_ARGS='skia_use_runtime_icu=true'
  note "ICU: runtime (you will need to ship icudtl.dat)"
else
  ICU_ARGS='skia_use_runtime_icu=false'
  note "ICU: embedded in the archives"
fi

# react-native-skia's commonArgs, verbatim, then the Linux set.
GN_ARGS="
skia_use_piex=true
skia_use_system_expat=false
skia_use_system_libjpeg_turbo=false
skia_use_system_libpng=false
skia_use_system_libwebp=false
skia_use_system_zlib=false
skia_enable_tools=false
is_official_build=true
skia_enable_skottie=true
is_debug=false
skia_enable_pdf=false
paragraph_tests_enabled=false
is_component_build=false
skia_enable_graphite=false
skia_use_dawn=false
skia_use_partition_alloc=false

target_os=\"linux\"
target_cpu=\"$SKIA_CPU\"
cc=\"clang\"
cxx=\"clang++\"

skia_use_gl=true
skia_use_egl=true

skia_use_system_freetype2=true
skia_use_freetype=true
skia_use_fontconfig=true

skia_enable_skparagraph=true
skia_use_harfbuzz=true
skia_use_system_harfbuzz=false
skia_use_icu=true
skia_use_system_icu=false
$ICU_ARGS

extra_cflags=[\"-fPIC\",\"-DSKIA_C_DLL\"]
extra_cflags_cc=[\"-fexceptions\",\"-frtti\"]
"

say "configuring"
( cd "$WORK/skia" && "$GN" gen "out/$OUT_NAME" --args="$(echo "$GN_ARGS" | tr '\n' ' ')" )

# ------------------------------------------------------------------ build ---

say "building"
note "This is the long part -- tens of minutes to a couple of hours."
note "It is resumable: re-running this script picks up where ninja left off."
( cd "$WORK/skia" && ninja -C "out/$OUT_NAME" )

# ---------------------------------------------------------------- collect ---

say "collecting the archives"
mkdir -p "$DEST"
BUILT="$WORK/skia/out/$OUT_NAME"
for a in "${ARCHIVES[@]}"; do
  [ -f "$BUILT/$a" ] || die "ninja did not produce $a.
    Look in $BUILT for what it did build; a missing module usually means a GN
    argument above was dropped."
  cp "$BUILT/$a" "$DEST/$a"
done
note "into $DEST"

# ------------------------------------------------------------------ verify --
#
# The point of this section is that "the build finished" and "we got what we
# need" are different claims. The Apple archives this replaces are Metal-only --
# 0 GL interface symbols against 508 Metal ones -- and a Linux build that
# somehow came out the same way would be useless in exactly the same silent
# fashion. So the GL check is the one that matters, not the file listing.

say "checking what was actually built"
FAILED=0

for a in "${ARCHIVES[@]}"; do
  size=$(stat -c%s "$DEST/$a")
  kind=$(file -b "$DEST/$a")
  case "$kind" in
    *archive*) ;;
    *) note "FAIL $a is not an archive: $kind"; FAILED=1; continue ;;
  esac
  [ "$size" -gt 1024 ] || { note "FAIL $a is $size bytes"; FAILED=1; continue; }
  printf '    ok   %-24s %8s KB\n' "$a" "$((size / 1024))"
done

GL=$(nm -g --defined-only "$DEST/libskia.a" 2>/dev/null | grep -c "GrGLInterface\|GrGLMakeNativeInterface" || true)
if [ "${GL:-0}" -gt 0 ]; then
  note "ok   GL backend present ($GL GrGLInterface symbols)"
else
  note "FAIL no GL symbols in libskia.a -- skia_use_gl did not take effect."
  FAILED=1
fi

FC=$(nm -g --defined-only "$DEST/libskia.a" 2>/dev/null | grep -c "FontMgr_New_FontConfig\|SkFontMgr_New_FontConfig" || true)
if [ "${FC:-0}" -gt 0 ]; then
  note "ok   fontconfig font manager present"
else
  note "warn no SkFontMgr_New_FontConfig -- createFontMgr will need another source"
fi

# Asked of a member, not of the archive.
#
# `file` on an ar archive reports "current ar archive" and stops: whether it
# goes on to describe what is inside depends on its version, and on 24.04 it
# does not. So this warned that a perfectly good x86-64 build "does not report
# as x86-64" -- a check that fires on correct output is worse than no check,
# because the next person has to rule it out by hand. An object file has no
# such ambiguity.
# `|| true` on the listing, which is load-bearing. libskia.a holds 1159
# members, `head -1` closes the pipe after the first, `ar` dies of SIGPIPE,
# and under `set -euo pipefail` that is exit 141 and the end of the script --
# one line before the manifest is written. The nine archives were already
# built and verified by then, so it failed having done all the work, silently,
# and left behind output nothing could identify.
FIRST=$(ar t "$DEST/libskia.a" 2>/dev/null | head -1 || true)
# What `file` calls this architecture, which is not what `uname -m` calls it.
case "$SKIA_ARCH" in
  x86_64)  WANT="x86-64" ;;
  aarch64) WANT="ARM aarch64" ;;
  *)       WANT="$SKIA_ARCH" ;;
esac
if [ -n "$FIRST" ] && ar p "$DEST/libskia.a" "$FIRST" > "$WORK/.arch-probe.o" 2>/dev/null; then
  ELF=$(file -b "$WORK/.arch-probe.o" | grep -c "$WANT" || true)
  rm -f "$WORK/.arch-probe.o"
  if [ "${ELF:-0}" -gt 0 ]; then
    note "ok   $WANT objects"
  else
    note "warn $FIRST in libskia.a is not $WANT"
  fi
else
  note "warn could not read an object out of libskia.a to check its architecture"
fi

# A manifest, so that archives found on disk in six months can be identified.
cat > "$DEST/skia-build.json" <<JSON
{
  "skia_commit": "$SKIA_COMMIT",
  "skia_branch": "$SKIA_BRANCH",
  "sk_milestone": 152,
  "for_react_native_skia": "2.11.1",
  "target": "linux-$SKIA_ARCH",
  "backend": "ganesh-gl",
  "runtime_icu": $([ "$SKIA_RUNTIME_ICU" = "1" ] && echo true || echo false),
  "built_on": "$(uname -sr)",
  "built_at": "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
}
JSON
note "manifest written to $DEST/skia-build.json"

if [ "$FAILED" -ne 0 ]; then
  die "the build produced archives, but not usable ones -- see the FAILs above"
fi

say "done"
echo
note "Archives:  $DEST"
echo
note "These are the nine the package's non-Apple CMake build imports. They do"
note "not make anything render on their own: basalt's Skia host half -- the"
note "RNSkiaModule, the context and the canvas -- exists for AppKit only, so a"
note "GTK host that links these still has no RNSkiaModule to find."
echo
note "Next: teach packages/react-native-basalt/native/cmake/Skia.cmake its"
note "non-Apple path, and confirm these link into basalt_gtk."
