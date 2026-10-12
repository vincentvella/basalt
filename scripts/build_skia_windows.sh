#!/usr/bin/env bash
#
# Build Skia and Dawn for Windows, for react-native-skia 3.x (Graphite).
#
# `cmake/Skia.cmake` has told people to run this file for a while and the file
# did not exist, which is the bug this fixes first. The second is what it
# builds: 3.x is not a version to catch up to, it is a second renderer. Skia
# Graphite draws through WebGPU, WebGPU here is Dawn, and nobody publishes
# either for Windows -- `react-native-skia-graphite-apple-macos` and
# `react-native-webgpu-dawn` carry Apple and Android slices and nothing else.
#
# ## Run it in Git Bash, not WSL
#
# Unlike scripts/build_skia_linux.sh, which must run in WSL because Windows
# cannot produce ELF archives, this must run *on* Windows: it produces
# `.lib` files against the MSVC runtime and links D3D12.
#
#     scripts/build_skia_windows.sh
#
# It needs Visual Studio 2022 with the "Desktop development with C++" workload
# (for clang-cl, lld-link and the Windows SDK), CMake, Ninja, Python 3 and git.
# The preflight below names any that are missing.
#
# ## One checkout, two builds, and why that is not an accident
#
# react-native-skia's own scripts say it plainly, in
# packages/skia/scripts/skia-configuration.ts:
#
#     Dawn is not taken from the GN build: skia_use_dawn makes it build Dawn's
#     non-monolithic libdawn_combined.a as a dependency of the Graphite
#     sources, but what ships is the monolithic libwebgpu_dawn that
#     build-dawn.ts builds from the same checkout.
#
# So this does the same: GN builds Skia, then CMake builds Dawn's `webgpu_dawn`
# target out of `third_party/dawn` in that same Skia checkout. "The same
# checkout" is the whole guarantee -- the headers an app compiles against ship
# in react-native-webgpu-dawn, and a Dawn built from any other revision links
# and then misbehaves in ways that look like memory corruption. It is the same
# trap the Skia headers have, and the same answer.
#
# ## What is pinned
#
#   skia      github.com/wcandillon/react-native-skia, externals/skia at v3.4.0
#   commit    2466dcf3937437e217e7f284afe0e1aae15891ce   (SK_MILESTONE 154)
#   dawn      whatever Skia's DEPS pins at that commit, which react-native-webgpu-dawn
#             records as 3d786993a7ded64c4ebb4884b9b079db9ad0e580
#
# The Dawn commit is not passed to anything here: `tools/git-sync-deps` fetches
# the one Skia's DEPS names, and the check at the end compares it against the
# number above rather than trusting it. If they ever disagree, the archives and
# the published headers disagree too, and that is worth stopping for.
#
# ## Where this differs from their Apple build, and why
#
# There is no Windows target in react-native-skia's configuration at all, so
# the argument set is their common one plus three decisions of ours:
#
#   libgrapheme for paragraph, where Android uses runtime ICU. The same choice
#   scripts/build_skia_linux.sh explains in reverse: basalt links the operating
#   system's ICU on Windows through `icu.lib` (ThirdParty.cmake, for Hermes),
#   and Skia's ICU-backed skunicode carries a static one, so the two collide on
#   every unsuffixed name. Apple ships libgrapheme for its own reasons and it
#   is the backend with no such problem.
#
#   D3D12 for Dawn, where Apple gets Metal and Android gets Vulkan. It is the
#   backend Windows has without installing anything, which matters for a
#   library an app links rather than a machine an engineer sets up.
#
#   No GL and no Metal in Skia. Graphite does not use either, and leaving
#   skia_use_gl on would build a backend nothing reaches.
#
set -euo pipefail

SKIA_COMMIT=2466dcf3937437e217e7f284afe0e1aae15891ce
SKIA_BRANCH=chrome/m154
SKIA_REPO=https://chromium.googlesource.com/skia
# What react-native-webgpu-dawn@154.3.0 says it was built from. Checked, not used.
DAWN_EXPECTED=3d786993a7ded64c4ebb4884b9b079db9ad0e580

HOME_DIR=${HOME:-$(cd ~ && pwd)}
[ -n "$HOME_DIR" ] || HOME_DIR=/tmp
WORK=${SKIA_BUILD_DIR:-$HOME_DIR/skia-windows-graphite}
OUT_NAME=win-x64-graphite
SKIA_DEST=${SKIA_OUT_DIR:-$WORK/libs/windows/x86_64}
DAWN_DEST=${DAWN_OUT_DIR:-$WORK/dawn/windows/x86_64}

# The archives the non-Apple CMake build imports, named rather than globbed: a
# missing one is a link failure in somebody else's symbols much later, and a
# list says which we expected. Graphite replaces Ganesh, so there is no
# skunicode_icu here and no GL anything.
ARCHIVES=(
  skia.lib
  skshaper.lib
  svg.lib
  skottie.lib
  sksg.lib
  skparagraph.lib
  skunicode_core.lib
  skunicode_libgrapheme.lib
)

say() { printf '\n\033[1m==> %s\033[0m\n' "$*"; }
note() { printf '    %s\n' "$*"; }
die() { printf '\n\033[31merror: %s\033[0m\n' "$*" >&2; exit 1; }

# --------------------------------------------------------------- preflight --

say "checking this machine can do the job"

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) ;;
  *) die "this builds Windows .lib files and must run on Windows, in Git Bash. For Linux archives see scripts/build_skia_linux.sh" ;;
esac

# git and python first, because they are their own installs and nothing below
# provides them. cmake, ninja and clang-cl are checked *after* vcvars, which is
# what puts them on PATH -- checking them here was this script failing on its
# own first run with "cmake is not on PATH" on a machine that has cmake.
command -v git >/dev/null 2>&1 || die "git is not on PATH, and it is its own install rather than part of Visual Studio."

# Python, and not the one Windows offers you.
#
# A stock Windows has `python` and `python3` on PATH as App Execution Aliases:
# zero-byte stubs that open the Microsoft Store. They satisfy `command -v`,
# they exit 0, and they print
#
#     Python was not found; run without arguments to install from the
#     Microsoft Store...
#
# which this script cheerfully reported as `ok  Python was not found`. Skia
# then fails somewhere inside git-sync-deps, a few GB of clone later. So the
# test is whether it can actually run something, and PYTHON is what the rest
# of this file calls rather than bare `python`.
PYTHON=""
for candidate in "${PYTHON_BIN:-}" python3 python py; do
  [ -n "$candidate" ] || continue
  command -v "$candidate" >/dev/null 2>&1 || continue
  if "$candidate" -c "import sys; sys.exit(0 if sys.version_info[0] == 3 else 1)" >/dev/null 2>&1; then
    PYTHON=$(command -v "$candidate")
    break
  fi
done
[ -n "$PYTHON" ] || die "no working Python 3 on PATH. Windows' own \"python\" may be the Microsoft Store stub, which is not one; install Python 3 and put it ahead of it, or set PYTHON_BIN."
export PYTHON
note "ok   $("$PYTHON" --version 2>&1) at $PYTHON"

# And a `python3` on PATH that is the same one.
#
# Finding a working Python for this script is not enough: Skia's generated
# ninja files run `cmd.exe /c python3 gn/cp.py ...` for every copy rule, and
# `python3` on a stock Windows is the Microsoft Store alias -- which prints
# "Python was not found" and exits 9009, three hundred targets into the build.
# `gn --script-executable` does not help: it governs GN's own script runs at
# generate time, not the commands in the rules it writes.
#
# So a shim directory goes on the front of PATH. `.bat` rather than a symlink
# because the caller is cmd.exe, and `%*` rather than `%1 %2` because cp.py
# takes paths with spaces.
PYTHON_REAL=$("$PYTHON" -c "import sys; print(sys.executable)" 2>/dev/null | tr -d '\r')
[ -n "$PYTHON_REAL" ] || die "could not ask $PYTHON where it lives"
PYTHON_REAL_WIN=$(cygpath -w "$PYTHON_REAL" 2>/dev/null || printf '%s' "$PYTHON_REAL")
SHIM_DIR="$WORK/pythonshim"
mkdir -p "$SHIM_DIR"
for name in python3 python; do
  {
    printf '@echo off\r\n'
    printf '"%s" %%*\r\n' "$PYTHON_REAL_WIN"
  } > "$SHIM_DIR/$name.bat"
done
PATH="$SHIM_DIR:$PATH"
export PATH
# Proved rather than assumed, because this is the second time the Store stub
# has got past a check that only looked.
#
# Through a file in the shim directory with a one-word name, and run from
# inside it. The obvious spelling -- `cmd //c "python3 -c \"print(42)\""` --
# is the same MSYS argument rewriting that eats vcvars' quoted path above: it
# came back empty and reported a working shim as broken. Nothing here has a
# quote or a space in it for MSYS to think about.
printf 'print(42)\n' > "$SHIM_DIR/shimcheck.py"
SHIM_SAYS=$(cd "$SHIM_DIR" && cmd //d //s //c "python3 shimcheck.py" 2>&1 | tr -d '\r' | tail -1)
rm -f "$SHIM_DIR/shimcheck.py"
[ "$SHIM_SAYS" = "42" ] || die "the python3 shim does not run: cmd.exe got '$SHIM_SAYS'"
note "ok   python3 shim in $SHIM_DIR -> $PYTHON_REAL_WIN"

# The compiler, the SDK headers and the build tools.
#
# `vcvars64.bat` is the supported way to get INCLUDE and LIB, and this runs it
# for that. What it does *not* do is trust the PATH it prints: that is four
# kilobytes of Windows paths which Git Bash cannot use until every one is
# converted, and getting that wrong is how this script twice reported that a
# machine with clang-cl does not have clang-cl. The three directories we
# actually need hang off the installation path at known places, so they are
# taken from there and converted one at a time, which either works or says
# which one is missing.
VSWHERE="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
[ -x "$VSWHERE" ] || die "no Visual Studio found (vswhere.exe is missing). This needs VS 2022 with the C++ workload."
VSDIR_WIN=$("$VSWHERE" -latest -products '*' -property installationPath | tr -d '\r')
[ -n "$VSDIR_WIN" ] || die "vswhere found no Visual Studio installation"
VSDIR=$(cygpath -u "$VSDIR_WIN")
note "Visual Studio at $VSDIR"

# INCLUDE and LIB, which only vcvars knows. Nothing else is taken from it.
#
# Run through PowerShell rather than invoked here, which looks like a detour
# and is not. `cmd //d //s //c "\"$VCVARS\" ... && set"` is the documented
# Git Bash spelling and it returns nothing: MSYS rewrites the arguments on the
# way to a native program, and a quoted path with spaces in it does not
# survive, with or without MSYS2_ARG_CONV_EXCL. Measured, not assumed -- the
# same vcvars through PowerShell prints INCLUDE and through cmd prints none.
VCVARS="$VSDIR_WIN\\VC\\Auxiliary\\Build\\vcvars64.bat"
while IFS= read -r line; do
  case "$line" in
    INCLUDE=*) export INCLUDE="${line#INCLUDE=}" ;;
    LIB=*) export LIB="${line#LIB=}" ;;
    LIBPATH=*) export LIBPATH="${line#LIBPATH=}" ;;
  esac
done < <(powershell -NoProfile -Command "cmd.exe /d /s /c '\"$VCVARS\" >nul 2>&1 && set'" | tr -d '\r')
[ -n "${INCLUDE:-}" ] || die "vcvars64.bat set no INCLUDE; the C++ workload may not be installed"
note "ok   INCLUDE and LIB from vcvars64.bat"

LLVM_BIN="$VSDIR/VC/Tools/Llvm/x64/bin"
CMAKE_BIN="$VSDIR/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin"
NINJA_BIN="$VSDIR/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja"
[ -x "$LLVM_BIN/clang-cl.exe" ] || die "no clang-cl at $LLVM_BIN. Add \"C++ Clang Compiler for Windows\" to the VS installation."
# cmake and ninja are allowed to come from anywhere; the VS copies are only a
# fallback, because a machine with its own newer ones should use them.
command -v cmake >/dev/null 2>&1 || PATH="$CMAKE_BIN:$PATH"
command -v ninja >/dev/null 2>&1 || PATH="$NINJA_BIN:$PATH"
PATH="$LLVM_BIN:$PATH"
export PATH

# Now the three the lines above are responsible for.
for tool in clang-cl cmake ninja; do
  command -v "$tool" >/dev/null 2>&1 || die "$tool is still not on PATH. Expected it under $VSDIR or on your own PATH."
done

note "ok   $(clang-cl --version 2>&1 | head -1)"
note "ok   $(cmake --version | head -1)"
note "ok   $(ninja --version) (ninja)"

# A build under a path with spaces is a day of quoting bugs in GN, ninja and
# Dawn's CMake at once. Refused rather than survived.
case "$WORK" in
  *" "*) die "the build directory has a space in it: $WORK. Set SKIA_BUILD_DIR to a path without one." ;;
esac

# ------------------------------------------------------------------ fetch --

mkdir -p "$WORK"
cd "$WORK"

if [ ! -d skia/.git ]; then
  say "cloning Skia (this is a few minutes and a few GB)"
  git clone --branch "$SKIA_BRANCH" "$SKIA_REPO" skia
fi

say "pinning Skia to $SKIA_COMMIT"
cd "$WORK/skia"
git fetch --depth 1 origin "$SKIA_COMMIT" 2>/dev/null || git fetch origin
git checkout --detach "$SKIA_COMMIT"
note "HEAD is $(git rev-parse HEAD)"

# The define, not the first number in the file: that one is the copyright year,
# and reading it made this refuse a correct checkout for 'saying SK_MILESTONE
# 2016'. A check that fires on right answers is worse than no check.
MILESTONE=$(awk '$1 == "#define" && $2 == "SK_MILESTONE" { print $3 }' include/core/SkMilestone.h | head -1)
[ "$MILESTONE" = "154" ] || die "this checkout says SK_MILESTONE $MILESTONE, not 154. The pin above and react-native-skia 3.4.0 disagree."
note "ok   SK_MILESTONE $MILESTONE"

say "syncing Skia's dependencies, Dawn among them"
"$PYTHON" tools/git-sync-deps

# gn, which Skia fetches for the host rather than vendoring.
if [ ! -x bin/gn.exe ] && [ ! -x bin/gn ]; then
  say "fetching gn"
  "$PYTHON" bin/fetch-gn
fi
GN=bin/gn.exe
[ -x "$GN" ] || GN=bin/gn

DAWN_ACTUAL=$(git -C third_party/externals/dawn rev-parse HEAD 2>/dev/null || echo "")
if [ -n "$DAWN_ACTUAL" ] && [ "$DAWN_ACTUAL" != "$DAWN_EXPECTED" ]; then
  note "warn Skia's DEPS pins Dawn $DAWN_ACTUAL"
  note "warn react-native-webgpu-dawn was built from $DAWN_EXPECTED"
  note "warn Their headers ship with the package and will not match this build."
  [ "${SKIA_ALLOW_DAWN_SKEW:-0}" = "1" ] || die "refusing to build a Dawn the published headers do not describe. Set SKIA_ALLOW_DAWN_SKEW=1 if you mean it."
fi
[ -n "$DAWN_ACTUAL" ] && note "ok   Dawn $DAWN_ACTUAL"

# ------------------------------------------------------------- build Skia --

say "turning off C++ module scanning in Skia's Dawn build"

# Skia builds Dawn itself, through third_party/dawn/build_dawn.py, and that
# script hardcodes its CMake arguments -- there is no way to pass one in. So
# the argument is added to the script, idempotently, after every sync.
#
# What it fixes: Dawn has a C++20 module target, `dawncpp_module`, guarded by
# DAWN_SUPPORTS_CXX_MODULES. That guard turns itself off for compilers whose
# toolchain has no clang-scan-deps, and its comment names Visual Studio as
# one of them. Visual Studio 2022 does ship clang-scan-deps beside clang-cl,
# so the guard passes here -- and then CMake refuses the scan anyway, because
# it has no way to do it for the clang-cl driver:
#
#     CMake Error in src/dawn/CMakeLists.txt:
#       The target named "dawncpp_module" has C++ sources that may use
#       modules, but the compiler does not provide a way to discover the
#       import graph dependencies.
#
# CMAKE_CXX_SCAN_FOR_MODULES=OFF is what that error recommends and it is not
# enough: a target carrying a `TYPE CXX_MODULES` file set is invalid without
# scanning rather than reduced to an ordinary one, so turning scanning off
# leaves the same error. The target has to not exist, and Dawn already has the
# switch for that -- `if(DAWN_SUPPORTS_CXX_MODULES)` guards it. Setting that
# in the cache makes its `check_cxx_source_compiles` skip the probe and take
# the answer given, which is the one its own comment intended for a toolchain
# that cannot scan. Nothing in Skia or react-native-skia imports the C++
# module; they use the C and C++ headers.
BUILD_DAWN_PY="$WORK/skia/third_party/dawn/build_dawn.py"
[ -f "$BUILD_DAWN_PY" ] || die "no $BUILD_DAWN_PY -- Skia's Dawn integration has moved"
if grep -q "DAWN_SUPPORTS_CXX_MODULES" "$BUILD_DAWN_PY"; then
  note "ok   already patched"
else
  "$PYTHON" - "$BUILD_DAWN_PY" <<'PATCH'
import sys

path = sys.argv[1]
needle = '      "-DCMAKE_CXX_STANDARD=20",'
added = '      "-DCMAKE_CXX_SCAN_FOR_MODULES=OFF",\n      "-DDAWN_SUPPORTS_CXX_MODULES=0",\n'
with open(path, encoding="utf-8") as handle:
    text = handle.read()
if needle not in text:
    sys.exit("could not find the CMAKE_CXX_STANDARD line to patch beside")
text = text.replace(needle, added + needle, 1)
with open(path, "w", encoding="utf-8") as handle:
    handle.write(text)
print("patched")
PATCH
  grep -q "DAWN_SUPPORTS_CXX_MODULES" "$BUILD_DAWN_PY" || die "the patch did not take"
  note "ok   patched $BUILD_DAWN_PY"
fi

# CMake caches DAWN_SUPPORTS_CXX_MODULES, so a cmake_dawn left by an earlier
# run answers from the cache and the patch above looks ineffective. Removed
# rather than reconfigured: it is generated, and regenerating is seconds.
rm -rf "$WORK/skia/out/$OUT_NAME/cmake_dawn"

say "configuring Skia (Graphite over Dawn)"

GN_ARGS="
skia_use_dng_sdk=false
skia_use_piex=false
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

skia_enable_graphite=true
skia_use_dawn=true
skia_use_partition_alloc=false

target_os=\"win\"
target_cpu=\"x64\"
cc=\"clang-cl\"
cxx=\"clang-cl\"
clang_win=\"$(cygpath -m "$(dirname "$(dirname "$(command -v clang-cl)")")" 2>/dev/null || true)\"

skia_use_metal=false
skia_use_gl=false

skia_enable_skparagraph=true
skia_use_system_icu=false
skia_use_harfbuzz=true
skia_use_system_harfbuzz=false
skia_use_icu=false
skia_use_client_icu=false
skia_use_libgrapheme=true

extra_cflags_cc=[\"/std:c++20\"]
"

"$GN" gen "out/$OUT_NAME" --args="$(printf '%s' "$GN_ARGS" | tr '\n' ' ')" --script-executable="$PYTHON"

say "building Skia"
ninja -C "out/$OUT_NAME" skia skshaper svg skottie sksg skparagraph skunicode_core skunicode_libgrapheme

# ------------------------------------------------------------- build Dawn --
#
# Out of Skia's own third_party/dawn, with CMake rather than GN, because the
# monolithic `webgpu_dawn` target is CMake's. The -D set is react-native-skia's
# from scripts/dawn-configuration.ts, with Metal and Vulkan swapped for D3D12.

say "building Dawn (monolithic, D3D12)"
DAWN_SRC="$WORK/skia/third_party/externals/dawn"
[ -d "$DAWN_SRC" ] || die "no Dawn at $DAWN_SRC -- git-sync-deps did not fetch it, which means skia_use_dawn is not in this milestone's DEPS"

DAWN_BUILD="$WORK/dawn-build"
cmake -S "$DAWN_SRC" -B "$DAWN_BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang-cl \
  -DCMAKE_CXX_COMPILER=clang-cl \
  -DBUILD_SHARED_LIBS=OFF \
  -DDAWN_BUILD_MONOLITHIC_LIBRARY=STATIC \
  -DDAWN_BUILD_SAMPLES=OFF \
  -DDAWN_BUILD_TESTS=OFF \
  -DDAWN_BUILD_BENCHMARKS=OFF \
  -DDAWN_BUILD_PROTOBUF=OFF \
  -DDAWN_USE_GLFW=OFF \
  -DDAWN_ENABLE_DESKTOP_GL=OFF \
  -DDAWN_ENABLE_OPENGLES=OFF \
  -DDAWN_ENABLE_SWIFTSHADER=OFF \
  -DDAWN_ENABLE_INSTALL=OFF \
  -DDAWN_ENABLE_METAL=OFF \
  -DDAWN_ENABLE_VULKAN=OFF \
  -DDAWN_ENABLE_D3D12=ON \
  -DDAWN_ENABLE_D3D11=OFF \
  -DTINT_BUILD_TESTS=OFF \
  -DTINT_BUILD_CMD_TOOLS=OFF \
  -DTINT_BUILD_IR_BINARY=OFF \
  -DTINT_BUILD_BENCHMARKS=OFF \
  -DDAWN_FETCH_DEPENDENCIES=ON \
  -DCMAKE_CXX_SCAN_FOR_MODULES=OFF \
  -DDAWN_SUPPORTS_CXX_MODULES=0

ninja -C "$DAWN_BUILD" webgpu_dawn

# ---------------------------------------------------------------- collect --

say "collecting the archives"
mkdir -p "$SKIA_DEST" "$DAWN_DEST"
note "skia into $SKIA_DEST"
note "dawn into $DAWN_DEST"

for archive in "${ARCHIVES[@]}"; do
  found=$(find "$WORK/skia/out/$OUT_NAME" -maxdepth 2 -name "$archive" -print -quit 2>/dev/null || true)
  [ -n "$found" ] || die "Skia built no $archive"
  cp "$found" "$SKIA_DEST/$archive"
done

# The third-party archives a source build emits beside libskia. The Apple
# xcframeworks carry theirs inside; here they are files, and leaving one out is
# a link error in somebody else's symbols. Copied if present rather than
# demanded: which ones exist depends on the GN args above.
for extra in harfbuzz.lib libpng.lib libjpeg.lib libwebp.lib libwebp_sse41.lib zlib.lib expat.lib wuffs.lib libgrapheme.lib skcms.lib; do
  found=$(find "$WORK/skia/out/$OUT_NAME" -maxdepth 2 -name "$extra" -print -quit 2>/dev/null || true)
  [ -n "$found" ] && cp "$found" "$SKIA_DEST/$extra"
done

DAWN_LIB=$(find "$DAWN_BUILD" -name "webgpu_dawn.lib" -print -quit 2>/dev/null || true)
[ -n "$DAWN_LIB" ] || die "Dawn built no webgpu_dawn.lib"
cp "$DAWN_LIB" "$DAWN_DEST/webgpu_dawn.lib"

# ------------------------------------------------------------------ check --

say "checking what was actually built"

for archive in "${ARCHIVES[@]}"; do
  size=$(( $(stat -c%s "$SKIA_DEST/$archive" 2>/dev/null || echo 0) / 1024 ))
  [ "$size" -gt 0 ] || die "$archive is empty"
  printf '    ok   %-32s %8s KB\n' "$archive" "$size"
done
dawnsize=$(( $(stat -c%s "$DAWN_DEST/webgpu_dawn.lib" 2>/dev/null || echo 0) / 1024 ))
printf '    ok   %-32s %8s KB\n' "webgpu_dawn.lib" "$dawnsize"

# Graphite present: the point of the whole exercise, and cheap to confirm by
# name rather than by trusting the GN args went in.
#
# Two things this got wrong the first time, both of which reported a correct
# build as broken:
#
#   `| grep -q` under `set -o pipefail`. grep -q exits on the first match,
#   llvm-nm takes SIGPIPE, and pipefail makes the whole pipeline a failure --
#   so the more matches there are, the more reliably it "fails". Counting
#   reads every line and has no early exit.
#
#   `skgpu.*graphite` as the pattern. MSVC mangles a nested name inside out,
#   so `skgpu::graphite::Device` is `...@Device@graphite@skgpu@@...` and the
#   namespaces appear in the opposite order to the source. It matched anyway,
#   on other symbols, which is worse than not matching: right answer, wrong
#   reason.
if ! command -v llvm-nm >/dev/null 2>&1; then
  note "warn no llvm-nm, so Graphite was not checked for"
else
  GRAPHITE=$(llvm-nm --defined-only "$SKIA_DEST/skia.lib" 2>/dev/null | grep -c "graphite@skgpu" || true)
  if [ "${GRAPHITE:-0}" -gt 0 ]; then
    note "ok   Graphite present ($GRAPHITE symbols)"
  else
    note "warn no skgpu::graphite symbols in skia.lib -- skia_enable_graphite may not have taken"
  fi
fi

cat > "$SKIA_DEST/skia-build.json" <<JSON
{
  "skia_commit": "$SKIA_COMMIT",
  "skia_branch": "$SKIA_BRANCH",
  "sk_milestone": 154,
  "for_react_native_skia": "3.4.0",
  "target": "windows-x86_64",
  "backend": "graphite-dawn-d3d12",
  "dawn_commit": "${DAWN_ACTUAL:-unknown}",
  "built_at": "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
}
JSON
cp "$SKIA_DEST/skia-build.json" "$DAWN_DEST/dawn-build.json"

say "done"
echo
note "Skia:  $SKIA_DEST"
note "Dawn:  $DAWN_DEST"
echo
note "These do not make anything render on their own. basalt-skia's CMakeLists"
note "is Apple-only today -- xcframework paths throughout -- and MakeWGPUSurface"
note "in RNDawnContext.h is a two-branch #ifdef that gives Windows the Android"
note "one. A Win32 RNSkSurface over SurfaceSourceWindowsHWND is the other half."
