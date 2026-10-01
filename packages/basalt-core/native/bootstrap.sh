#!/usr/bin/env bash
# Fetches and builds everything this project needs that is not in the repo:
# vendored third-party sources, Hermes, and React Native's codegen output.
#
# Usage:  scripts/bootstrap.sh /path/to/react-native-checkout
#
# Idempotent: each step is skipped if its output already exists. Pass --force
# to redo everything.

set -euo pipefail

RN_DIR_ARG="${1:-}"
FORCE="${2:-}"
[ "${1:-}" = "--force" ] && { FORCE="--force"; RN_DIR_ARG=""; }

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Where the vendored dependencies go. Not beside this script: this script now
# ships inside the npm package, and npm deletes and rewrites node_modules, which
# is not a place to leave a Hermes build. An app passes its own directory;
# scripts/bootstrap.sh in this repository passes the repository root.
TP="${BASALT_THIRD_PARTY:-$PWD/third_party}"
mkdir -p "$TP"
TP="$(cd "$TP" && pwd)"

# Only used to guess where a React Native checkout might be, and only when one
# was not named. Two levels up from the package is the repository root in a
# development checkout, and node_modules in an installed one, where the guess
# will simply fail and the path has to be given.
REPO_ROOT="$(cd "$HERE/../../.." && pwd)"

# Versions are React Native's own pins, from
# packages/react-native/gradle/libs.versions.toml. Check them after an RN bump.
#
# Hermes is not here. It is pinned per supported React Native in
# ../supported-versions.json, because it is coupled to ReactCommon and one
# version cannot serve several React Natives.
FOLLY_VERSION="2024.11.18.00"
FAST_FLOAT_VERSION="8.0.0"
NLOHMANN_VERSION="3.11.3"

# React Native requires ^22.13.0 || ^24.3.0 || >= 26.0.0. Node 25.x is
# excluded and yarn install fails an engine check in a preinstall hook.
NODE_VERSION="24"

log() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }

# --- which desktop is this ---------------------------------------------------
#
# Windows means Git Bash or MSYS, which is where this script runs there -- it is
# a bash script and there is no reason for a second one in PowerShell that would
# drift from it. `uname -s` reports MINGW64_NT-10.0 or MSYS_NT-10.0 under those.
#
# What differs on Windows is not the shape of the bootstrap but four details:
# there is no pkg-config and no GTK, the compiler is clang-cl rather than
# clang++, Hermes needs three extra flags and a one-word patch, and the library
# it produces is called something else. Each is handled where it arises rather
# than in a parallel branch.
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) HOST_OS=windows ;;
  Darwin)               HOST_OS=macos ;;
  *)                    HOST_OS=linux ;;
esac

# `nice` is POSIX and Git Bash does not have it. Used to keep a long compile
# from making the machine unusable, which is a courtesy rather than a
# requirement, so on Windows it simply becomes nothing.
if [ "$HOST_OS" = windows ]; then
  NICE=()
else
  NICE=(nice -n 10)
fi
run_nice() { ${NICE[@]+"${NICE[@]}"} "$@"; }

if [ -n "$FORCE" ]; then
  log "--force: removing fetched trees"
  rm -rf "$TP/folly" "$TP/fast_float" "$TP/nlohmann_json" "$TP/hermes" \
         "$TP/hermes-build" "$TP/node"
  # Not the whole codegen dir: its CMakeLists.txt is checked in and replaces
  # the Android-only one the generator emits. Only the generated tree goes.
  rm -rf "$TP/codegen/react" "$TP"/codegen/*.h "$TP"/codegen/*.cpp
fi

# --- locate the React Native checkout ---------------------------------------
RN_DIR="${RN_DIR_ARG:-${RN_DIR:-}}"
if [ -z "$RN_DIR" ]; then
  for candidate in "$REPO_ROOT/../react-native" "$HOME/Workspace/react-native"; do
    [ -d "$candidate/packages/react-native/ReactCommon" ] && { RN_DIR="$candidate"; break; }
  done
fi
[ -n "$RN_DIR" ] || die "pass the React Native checkout path, or set RN_DIR.
  git clone --depth 1 https://github.com/react/react-native"
RN_DIR="$(cd "$RN_DIR" && pwd)"

# Two shapes are accepted. A source checkout, where the package sits under
# packages/react-native, and an installed one, where the directory given *is*
# the package. The second is what an app has, and it very nearly works: React
# Native ships its C++, the codegen script, the Hermes pin and the version table
# in the npm package. The one thing missing is ReactCxxPlatform, which is absent
# from the package's `files` list, and which is the layer this host is built on.
# See docs/PORTING.md.
if [ -d "$RN_DIR/packages/react-native/ReactCommon" ]; then
  RN_PKG="$RN_DIR/packages/react-native"
  RN_LAYOUT=checkout
elif [ -d "$RN_DIR/ReactCommon" ]; then
  RN_PKG="$RN_DIR"
  RN_LAYOUT=installed
else
  die "not a React Native checkout or package: $RN_DIR"
fi
RN_VERSION="$(sed -n 's/.*"version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' \
  "$RN_PKG/package.json" | head -1)"
log "React Native $RN_VERSION ($RN_LAYOUT): $RN_PKG"

# --- is this version supported ----------------------------------------------
# Asked here, before anything is downloaded or compiled, because the answer for
# an unsupported version used to arrive as a compile error inside Hermes.
HERMES_VERSION="$(node -e '
  const table = require(process.argv[1]);
  const version = process.argv[2];
  if (version === table.development.reactNative) {
    process.stdout.write(table.development.hermes + " " + table.development.hermesTarget);
  } else {
    const minor = version.split(".").slice(0, 2).join(".");
    const entry = table.supported.find(s => s.reactNative === minor);
    if (!entry) {
      const ok = [...table.supported.map(s => s.reactNative + ".x"), table.development.reactNative];
      console.error(`basalt-core does not support React Native ${version}.`);
      console.error(`Supported: ${ok.join(", ")}.`);
      console.error("");
      console.error("packages/basalt-core/supported-versions.json says what is");
      console.error("tested and why the rest is not simply allowed. Adding a version");
      console.error("means building against it and running both suites, not editing");
      console.error("a range.");
      process.exit(1);
    }
    process.stdout.write(entry.hermes + " " + entry.hermesTarget);
  }
' "$HERE/../supported-versions.json" "$RN_VERSION")" || exit 1
HERMES_TARGET="${HERMES_VERSION##* }"
HERMES_VERSION="${HERMES_VERSION%% *}"

# --- ReactCxxPlatform -------------------------------------------------------
# The one thing React Native does not put in its npm package.
#
# `files` in packages/react-native/package.json lists ReactCommon, ReactAndroid
# and ReactApple but not ReactCxxPlatform, so an installed React Native has
# everything needed to build a C++ host except the host layer itself. A checkout
# has it; an app does not.
#
# Fetched at the app's *exact* version rather than vendored into this package,
# because vendoring cannot work: this layer tracks ReactCommon closely, and
# `main`'s copy fails to compile against 0.87.1 on a missing ResizeObserver
# header and a ReactInstance method that did not exist yet. One copy cannot
# serve several React Natives, so the copy has to come from the version in use.
#
# A sparse, blobless clone of one directory at one tag: about 3MB and a few
# seconds, which is the same bargain as the folly and Hermes downloads below.
if [ "$RN_LAYOUT" = installed ] && [ ! -d "$RN_PKG/ReactCxxPlatform" ]; then
  CXX_PLATFORM="$TP/ReactCxxPlatform"

  if [ ! -f "$CXX_PLATFORM/.version" ] || \
     [ "$(cat "$CXX_PLATFORM/.version" 2>/dev/null)" != "$RN_VERSION" ]; then
    log "fetching ReactCxxPlatform for React Native $RN_VERSION"
    rm -rf "$CXX_PLATFORM" "$TP/.rn-sparse"
    git clone -q --depth 1 --filter=blob:none --sparse \
      --branch "v$RN_VERSION" https://github.com/facebook/react-native \
      "$TP/.rn-sparse" 2>/dev/null || die "no React Native tag v$RN_VERSION on GitHub.
  ReactCxxPlatform is not in the npm package, so it has to come from the tag
  matching your react-native, and there is no such tag. See
  docs/PORTING.md."
    (cd "$TP/.rn-sparse" && git sparse-checkout set --no-cone \
      packages/react-native/ReactCxxPlatform >/dev/null)
    [ -d "$TP/.rn-sparse/packages/react-native/ReactCxxPlatform" ] \
      || die "tag v$RN_VERSION has no ReactCxxPlatform"
    mv "$TP/.rn-sparse/packages/react-native/ReactCxxPlatform" "$CXX_PLATFORM"
    rm -rf "$TP/.rn-sparse"
    echo "$RN_VERSION" > "$CXX_PLATFORM/.version"
  fi
  log "ReactCxxPlatform $RN_VERSION at $CXX_PLATFORM"
fi

# --- toolchain --------------------------------------------------------------
#
# The compiler differs by name rather than by kind. clang everywhere, because
# React Native is a clang codebase built -Wall -Werror -Wpedantic and
# docs/DECISIONS.md declines to paper over its own warnings with -Wno-*; on
# Windows that same compiler is called clang-cl and takes MSVC's command line.
# cl is not an alternative for the core build -- it understands neither -Werror
# nor clang's -Wall, and Static Hermes uses __builtin_expect, which it does not
# have. See docs/PORTING.md.
if [ "$HOST_OS" = windows ]; then
  BOOTSTRAP_TOOLS="cmake ninja clang-cl curl tar node"
else
  BOOTSTRAP_TOOLS="cmake ninja clang++ pkg-config curl tar node"
fi
for tool in $BOOTSTRAP_TOOLS; do
  command -v "$tool" >/dev/null || die "missing required tool: $tool"
done

if [ "$HOST_OS" = windows ]; then
  # vcpkg is what Windows has instead of apt or Homebrew.
  #
  # Checked by looking for a header this build cannot do without, not by looking
  # for vcpkg itself, and the difference is not pedantry: **vcvars64.bat sets
  # VCPKG_ROOT** to the copy bundled with Visual Studio, which has nothing
  # installed in it. A check for the directory passes against that one and hands
  # back a toolchain file that finds no packages, which then fails much later
  # inside CMake with a message about glog.
  vcpkg_has_packages() {
    [ -n "$1" ] && [ -f "$1/scripts/buildsystems/vcpkg.cmake" ] &&
      [ -f "$1/installed/x64-windows/include/glog/logging.h" ]
  }

  for candidate in "${VCPKG_ROOT:-}" "$HOME/Tools/vcpkg" "$HOME/vcpkg" "C:/vcpkg"; do
    if vcpkg_has_packages "$candidate"; then
      VCPKG_ROOT="$candidate"
      break
    fi
    VCPKG_ROOT=""
  done

  [ -n "$VCPKG_ROOT" ] || die \
"no vcpkg with this build's packages installed in it.

Note that vcvars64.bat sets VCPKG_ROOT to the copy bundled with Visual Studio,
which is empty -- so having that variable set is not the same as having the
packages. Set it to one that does, or clone your own:

  git clone https://github.com/microsoft/vcpkg \$HOME/Tools/vcpkg
  \$HOME/Tools/vcpkg/bootstrap-vcpkg.bat

then install what React Native's C++ needs, which on Linux comes from apt:

  vcpkg install --triplet x64-windows glog fmt double-conversion \\
      boost-regex boost-beast boost-asio boost-thread openssl curl

boost-thread is not on React Native's own list and is needed anyway: folly's
Windows PThread.cpp shim is written over boost::thread."
  export VCPKG_ROOT
  log "vcpkg at $VCPKG_ROOT"
else
  pkg-config --exists gtk4 || die "gtk4 development files not found (pkg-config gtk4)"
  log "gtk4 $(pkg-config --modversion gtk4), pango $(pkg-config --modversion pango 2>/dev/null || echo '?')"
fi

# mise is optional; without it, the ambient node must satisfy RN's engines.
if command -v mise >/dev/null; then
  NODE_RUN=(mise exec "node@${NODE_VERSION}" --)
  mise ls node 2>/dev/null | grep -q "^node *${NODE_VERSION}\." || {
    log "installing node@${NODE_VERSION} via mise"
    mise install "node@${NODE_VERSION}"
  }
else
  # Empty array; expanded below with the ${arr[@]+"${arr[@]}"} idiom because
  # bash < 4.4 (macOS ships 3.2) treats "${arr[@]}" of an empty array as unbound
  # under set -u.
  NODE_RUN=()
  node_major="$(node -p 'process.versions.node.split(".")[0]')"
  case "$node_major" in
    22|24|26|27|28) : ;;
    *) die "node $node_major is outside RN's supported range (^22.13 || ^24.3 || >=26).
  Install mise, or switch node." ;;
  esac
fi

mkdir -p "$TP"

# --- vendored headers -------------------------------------------------------
if [ ! -d "$TP/folly" ]; then
  log "fetching folly $FOLLY_VERSION"
  curl -fsSL "https://github.com/facebook/folly/archive/v${FOLLY_VERSION}.tar.gz" \
    | tar xz -C "$TP" && mv "$TP/folly-${FOLLY_VERSION}" "$TP/folly"
fi

if [ ! -d "$TP/fast_float" ]; then
  log "fetching fast_float $FAST_FLOAT_VERSION"
  curl -fsSL "https://github.com/fastfloat/fast_float/archive/v${FAST_FLOAT_VERSION}.tar.gz" \
    | tar xz -C "$TP" && mv "$TP/fast_float-${FAST_FLOAT_VERSION}" "$TP/fast_float"
fi

if [ ! -f "$TP/nlohmann_json/include/nlohmann/json.hpp" ]; then
  log "fetching nlohmann_json $NLOHMANN_VERSION"
  mkdir -p "$TP/nlohmann_json/include/nlohmann"
  curl -fsSL -o "$TP/nlohmann_json/include/nlohmann/json.hpp" \
    "https://github.com/nlohmann/json/releases/download/v${NLOHMANN_VERSION}/json.hpp"
fi

# --- Hermes -----------------------------------------------------------------
# Which Hermes, according to the supported-versions table rather than according
# to the app.
#
# Following the app was tried and abandoned. Hermes is coupled to ReactCommon as
# tightly as ReactCxxPlatform is, so an old React Native brings an old Hermes,
# and 0.81's wants a jsi header newer React Native does not ship and names its
# build target differently. Chasing that leads three compile errors deep into a
# version nobody claimed to support. The table says what was actually tested,
# and this refuses anything else rather than half-working.
log "Hermes $HERMES_VERSION (target $HERMES_TARGET) for React Native $RN_VERSION"

# Keyed by version: switching React Native means a different Hermes, and a
# stale build of the wrong one fails deep in a compile rather than here.
HERMES_STAMP="$TP/hermes/.version"
if [ -d "$TP/hermes" ] && [ ! -f "$HERMES_STAMP" ]; then
  # An unstamped tree predates this file. There was exactly one pinned Hermes
  # before it, so an existing tree is that one, and stamping it saves every
  # checkout a 200MB download and a rebuild it does not need. A wrong guess
  # here surfaces immediately as a compile error, not as a silent mismatch.
  log "assuming the existing Hermes is $HERMES_VERSION"
  echo "$HERMES_VERSION" > "$HERMES_STAMP"
fi
if [ -d "$TP/hermes" ] && [ "$(cat "$HERMES_STAMP" 2>/dev/null)" != "$HERMES_VERSION" ]; then
  log "Hermes was $(cat "$HERMES_STAMP"), rebuilding for $HERMES_VERSION"
  rm -rf "$TP/hermes" "$TP/hermes-build"
fi

if [ ! -d "$TP/hermes" ]; then
  log "fetching Hermes $HERMES_VERSION (~200MB)"
  mkdir -p "$TP/hermes"
  curl -fsSL "https://github.com/facebook/hermes/tarball/${HERMES_VERSION}" \
    | tar xz -C "$TP/hermes" --strip-components=1
  echo "$HERMES_VERSION" > "$HERMES_STAMP"
fi

# --- the one patch this project applies to anything it downloads -------------
#
# Static Hermes asserts that its C++ `Environment` and its C `SHEnvironment`
# have identical field offsets. `Environment` multiply-inherits from
# `VariableSizeRuntimeCell` and an *empty* base, `llvh::TrailingObjects`, and
# the MSVC ABI does not collapse empty bases the way the Itanium ABI does. So
# sizeof matches, the offsets do not, and the build stops on a static_assert
# that no flag can affect.
#
# Hermes already has the fix and does not use it. `Support/Compiler.h` defines
#
#     #define HERMES_EMPTY_BASES __declspec(empty_bases)
#
# with a comment saying it is "necessary for PointerBase alignment requirements
# in some cases when using HERMESVM_CONTIGUOUS_HEAP" -- the mode this builds in
# -- and applies it to nothing in the entire tree. This puts it on the one class
# that needs it.
#
# Patching a download is not something this project does lightly, and it is
# worth being clear about why this one is tolerable where patching React Native
# is not: third_party/hermes is a tarball this script fetched, not a checkout
# anybody works in, and --force deletes and refetches it. The real fix is
# upstream and the backlog says so.
if [ "$HOST_OS" = windows ]; then
  HERMES_CALLABLE="$TP/hermes/include/hermes/VM/Callable.h"
  if [ -f "$HERMES_CALLABLE" ] && ! grep -q "HERMES_EMPTY_BASES Environment" "$HERMES_CALLABLE"; then
    log "patching Hermes: HERMES_EMPTY_BASES on VM::Environment (see docs/PORTING.md)"
    # The macro comes from a header Callable.h does not already include.
    grep -q '#include "hermes/Support/Compiler.h"' "$HERMES_CALLABLE" || \
      sed -i '/#include "hermes\/VM\/ArrayStorage.h"/i #include "hermes/Support/Compiler.h"' \
        "$HERMES_CALLABLE"
    sed -i 's|^class Environment final$|class HERMES_EMPTY_BASES Environment final|' \
      "$HERMES_CALLABLE"
    grep -q "HERMES_EMPTY_BASES Environment" "$HERMES_CALLABLE" || die \
      "could not apply the HERMES_EMPTY_BASES patch to $HERMES_CALLABLE.
  Hermes $HERMES_VERSION may have changed that declaration. See
  docs/PORTING.md for what the patch is and why."
  fi
fi

# Whether Hermes is already built, asked in the naming convention of the
# platform. A static archive on Windows is `hermesvm_a.lib` rather than
# `libhermesvm.a`, and the `_a` is not decoration: the plain `hermesvm.lib` next
# to it is a one-kilobyte import library for a DLL that exports nothing, because
# a Windows DLL exports nothing without __declspec(dllexport) and Hermes does
# not annotate. See ThirdParty.cmake, which picks the archive for the same
# reason.
hermes_is_built() {
  if [ "$HOST_OS" = windows ]; then
    ls "$TP"/hermes-build/lib/"${HERMES_TARGET#lib}"_a.lib >/dev/null 2>&1
  else
    ls "$TP"/hermes-build/lib/lib"${HERMES_TARGET#lib}".* >/dev/null 2>&1 || \
    ls "$TP"/hermes-build/API/hermes/lib"${HERMES_TARGET#lib}".* >/dev/null 2>&1
  fi
}

if ! hermes_is_built; then
  log "building Hermes (slow; RN's own host flags)"

  # Three extra arguments on Windows, each answering something that only goes
  # wrong there. docs/PORTING.md has the detail; briefly:
  #
  #   HERMES_ALLOW_BOOST_CONTEXT=0  its vendored boost::context's *Windows*
  #                                 stack allocator throws where the POSIX one
  #                                 does not, and Hermes builds without
  #                                 exceptions. Hermes' own option, the one its
  #                                 ASAN and Emscripten builds use.
  #   HERMES_ENABLE_EH / _RTTI      React Native's jsi throws, and arrives here
  #                                 through JSI_DIR without passing through the
  #                                 helper that would give it /EHsc. clang with
  #                                 no flag defaults exceptions on; clang-cl
  #                                 defaults them off, which is the whole bug.
  #                                 The two must agree or Hermes refuses.
  #   CMAKE_CXX_FLAGS               /EHs /EHc rather than /EHsc, because Hermes
  #                                 string-REPLACEs the latter out of this
  #                                 variable. /DWIN32 /D_WINDOWS are CMake's own
  #                                 defaults and have to be repeated, because
  #                                 setting this replaces them -- and Hermes'
  #                                 OSCompat.h reaches for <unistd.h> without
  #                                 _WINDOWS.
  HERMES_ARGS=(-DJSI_DIR="$RN_PKG/ReactCommon/jsi"
               -DCMAKE_BUILD_TYPE=Release
               -DHERMES_ENABLE_DEBUGGER=True
               -DHERMESVM_HEAP_HV_MODE=HEAP_HV_PREFER32)
  if [ "$HOST_OS" = windows ]; then
    HERMES_ARGS+=(-DCMAKE_C_COMPILER=clang-cl
                  -DCMAKE_CXX_COMPILER=clang-cl
                  -DHERMES_ALLOW_BOOST_CONTEXT=0
                  -DHERMES_ENABLE_EH=ON
                  -DHERMES_ENABLE_RTTI=ON
                  "-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /EHs /EHc")
  else
    HERMES_ARGS+=(-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++)
  fi

  # One argument is excluded from Git Bash's path conversion, and exactly one.
  #
  # `/DWIN32` looks like an absolute POSIX path, so the MSYS runtime rewrites it
  # on its way to a native program and clang-cl is handed
  # `C:/Program Files/Git/DWIN32` -- which it reports as two missing input
  # files, naming something nobody wrote:
  #
  #   clang-cl: error: no such file or directory: 'C:/Program'
  #   clang-cl: error: no such file or directory: 'Files/Git/DWIN32'
  #
  # Turning conversion off wholesale is the obvious fix and is wrong: `-S` and
  # `-B` below are POSIX paths that a native CMake cannot read, and *they* are
  # only correct because the same conversion rewrites them. So the exclusion
  # names the one prefix that must be left alone.
  #
  # This path had never run. Hermes was built by hand in phase 41 and every
  # bootstrap since has found it already built, which is what docs/BACKLOG.md
  # meant by "nothing has run bootstrap from an empty third_party on Windows".
  if [ "$HOST_OS" = windows ]; then
    MSYS2_ARG_CONV_EXCL='-DCMAKE_CXX_FLAGS=' \
      cmake --log-level=ERROR -G Ninja -S "$TP/hermes" -B "$TP/hermes-build" "${HERMES_ARGS[@]}"
  else
    cmake --log-level=ERROR -G Ninja -S "$TP/hermes" -B "$TP/hermes-build" "${HERMES_ARGS[@]}"
  fi
  # -j is capped deliberately: a full-width build makes laptops unusable and
  # pins the fans for minutes afterwards.
  run_nice cmake --build "$TP/hermes-build" --target "$HERMES_TARGET" -j "${BUILD_JOBS:-12}"
fi

# --- yarn -------------------------------------------------------------------
# React Native's codegen shells out to `yarn install` itself, so yarn has to be
# *on PATH*, not merely invocable -- npx is not enough.
#
# Installing it globally is not an option either: on Linux npm's global prefix
# is root-owned (/usr/lib/node_modules) and `npm install -g` fails with EACCES,
# which a bootstrap script has no business needing sudo to avoid. Homebrew's
# node has a user-writable prefix, which is why this only bites off macOS.
#
# So yarn goes under third_party/ like every other fetched dependency.
if ! command -v yarn >/dev/null; then
  if [ ! -x "$TP/node/node_modules/.bin/yarn" ]; then
    log "installing yarn under third_party/node"
    mkdir -p "$TP/node"
    ${NODE_RUN[@]+"${NODE_RUN[@]}"} npm install --prefix "$TP/node" \
      --silent --no-fund --no-audit yarn@1.22.22
  fi
  export PATH="$TP/node/node_modules/.bin:$PATH"
fi
log "yarn $(yarn --version)"

# --- React Native codegen ---------------------------------------------------
# 24 targets need react_codegen_rncore, ReactCxxPlatform's react/runtime among
# them, so this is mandatory for a host that runs JS.
# --- React Native's dependencies --------------------------------------------
# Deliberately not folded into the codegen step below. Codegen needs these, but
# so does Metro when bundling, and the two are cached separately: a checkout
# with a populated third_party/codegen and an empty node_modules skips this
# step and then cannot bundle. That is exactly what CI hit the first time its
# third_party cache was warm.
#
# `node_modules` being populated is not enough to conclude yarn has run here,
# which is the same bug one layer down and cost a fortnight of red Windows
# builds. `@react-native/codegen` is a workspace symlink out of node_modules
# to `packages/react-native-codegen`, and the `lib/` that babel-plugin-codegen
# requires is built by yarn's own prepare step -- into the package, not into
# node_modules. So a cache of `node_modules` alone restores a tree that looks
# installed and cannot bundle: "Cannot find module
# '@react-native/codegen/lib/parsers/flow/parser'".
#
# Checking for that directory as well is what makes a warm cache honest. It
# showed on Windows and not on Linux only because the Linux job kept being
# cancelled by a newer push before it got this far; release.yml was green
# throughout because a cold build restores no cache and always installs.
RN_CODEGEN_LIB="$RN_DIR/packages/react-native-codegen/lib"
if [ "$RN_LAYOUT" = checkout ] && { [ ! -d "$RN_DIR/node_modules" ] || [ -z "$(ls -A "$RN_DIR/node_modules" 2>/dev/null)" ]; }; then
  log "installing React Native's monorepo dependencies"
  (cd "$RN_DIR" && run_nice ${NODE_RUN[@]+"${NODE_RUN[@]}"} yarn install --network-timeout 600000)
fi

# And codegen's own JavaScript, which the install above does not build.
#
# `packages/react-native-codegen` declares `prepare: yarn run build`, and
# yarn 1 does not run `prepare` for workspace children -- only for the root
# package. So a plain `yarn install` leaves `lib/` absent, and
# babel-plugin-codegen fails the first time Metro touches a file: "Cannot
# find module '@react-native/codegen/lib/parsers/flow/parser'".
#
# Nothing noticed because the two things that build it both happen only on a
# cold tree. The codegen step below shells out to React Native's own script,
# which builds it on the way past; with a warm third_party/codegen that step
# is skipped. So a machine with both caches warm had an installed checkout
# that could compile and could not bundle -- six red Windows runs, and green
# release runs throughout, because a release restores no cache at all.
#
# Its own check rather than folded into the install: the install is a
# five-minute network operation and this is a 1.6-second transpile, and the
# case that matters is precisely the one where the install is not needed.
if [ "$RN_LAYOUT" = checkout ] && [ ! -d "$RN_CODEGEN_LIB" ]; then
  log "building React Native's codegen"
  (cd "$RN_DIR/packages/react-native-codegen" &&
     run_nice ${NODE_RUN[@]+"${NODE_RUN[@]}"} yarn run build)
fi

# Codegen output belongs to one React Native version and to no other: the
# generated spec headers name every feature flag that version has, so building
# 0.87.1 against artifacts generated from `main` fails on the flags `main` added
# since. The directory is shared rather than per-version, so that
# third_party/codegen/CMakeLists.txt stays put and CI's cache key still works;
# a stamp is what makes reuse safe. Switching versions regenerates.
CODEGEN_STAMP="$TP/codegen/.react-native-version"

if [ ! -d "$TP/codegen/react" ] || [ "$(cat "$CODEGEN_STAMP" 2>/dev/null)" != "$RN_VERSION" ]; then
  if [ -d "$TP/codegen/react" ]; then
    log "codegen was generated from $(cat "$CODEGEN_STAMP" 2>/dev/null || echo 'an unknown version'), rebuilding for $RN_VERSION"
    rm -rf "$TP/codegen/react" "$TP"/codegen/*.h "$TP"/codegen/*.cpp
  fi
  log "generating codegen artifacts for React Native $RN_VERSION"
  CODEGEN_TMP="$(mktemp -d)"
  # Failure is judged by what it produced, not by its exit status. React Native
  # 0.81's codegen writes every artifact, prints "Done", and then exits non-zero
  # after failing to stat an output directory it never used, because -f does not
  # exist there and core artifacts went to its own fixed folder instead. The
  # check below is the real one.
  (cd "$RN_PKG" && ${NODE_RUN[@]+"${NODE_RUN[@]}"} node scripts/generate-codegen-artifacts.js \
      -p . -t android -o "$CODEGEN_TMP" -s library -f) || true

  # Where the output lands depends on the version. 0.87 and main honour -f and
  # write core artifacts to the path given. 0.81 has no such flag: its core
  # libraries have a fixed output folder inside React Native itself, so the
  # artifacts appear under the package's own ReactAndroid/build. That writes
  # generated files into node_modules, which is what React Native does on
  # Android too, and they are regenerable.
  #
  # Detected by looking rather than by version, so a future layout change fails
  # with the message below instead of being mistaken for one of these two.
  JNI="$CODEGEN_TMP/android/app/build/generated/source/codegen/jni"
  if [ ! -d "$JNI/react" ]; then
    JNI="$RN_PKG/ReactAndroid/build/generated/source/codegen/jni"
  fi
  [ -d "$JNI/react" ] || die "codegen produced no react/ tree, in $CODEGEN_TMP or $RN_PKG/ReactAndroid/build"
  mkdir -p "$TP/codegen"
  cp -R "$JNI/react" "$TP/codegen/"
  cp "$JNI"/*.h "$JNI"/*.cpp "$TP/codegen/" 2>/dev/null || true
  rm -rf "$CODEGEN_TMP"
  echo "$RN_VERSION" > "$CODEGEN_STAMP"
fi

# The CMakeLists that builds those artifacts is ours, not codegen's. Codegen
# writes one too and it links Android-only targets, so it is deliberately not
# copied out with the rest. Installed every time rather than only alongside a
# regeneration, so that a third_party directory from an older version of this
# package still gets the current one.
cp "$HERE/cmake/codegen/CMakeLists.txt" "$TP/codegen/CMakeLists.txt"

log "bootstrap complete, into $TP. Configure with:"
if [ "$HOST_OS" = windows ]; then
  # CMake here is a native Windows program and this shell is not, so the paths
  # have to change shape on the way out: it does not understand /c/Users/... and
  # silently fails to find the toolchain file, then reports the confusing
  # "unable to find a build program corresponding to Ninja". `cygpath -m` gives
  # C:/Users/... -- drive letter, forward slashes -- which both understand.
  winpath() { command -v cygpath >/dev/null && cygpath -m "$1" || printf '%s' "$1"; }

  # From a shell that has run vcvars64.bat: the Windows SDK's headers and
  # libraries are not on PATH otherwise, and clang-cl finds neither.
  cat <<EOF

  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \\
    -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl \\
    -DCMAKE_TOOLCHAIN_FILE=$(winpath "$VCPKG_ROOT")/scripts/buildsystems/vcpkg.cmake \\
    -DRN_DIR=$(winpath "$RN_PKG")
  cmake --build build -j \${BUILD_JOBS:-12}

Run it from a shell that has run vcvars64.bat, or clang-cl will not find the
Windows SDK. clang-cl rather than cl: see docs/PORTING.md.

CMAKE_BUILD_TYPE is not optional here, unlike on the other two desktops. Left
unset, MSVC picks the debug C runtime -- and vcpkg follows it to the debug
packages -- while Hermes above was built Release. The two cannot be linked
together: the failure is lld-link reporting a mismatch on _ITERATOR_DEBUG_LEVEL,
which names neither the build type nor the library that disagrees.

EOF
else
  cat <<EOF

  cmake -B build -G Ninja \\
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \\
    -DRN_DIR=$RN_PKG
  nice -n 10 cmake --build build -j \${BUILD_JOBS:-12}

EOF
fi
