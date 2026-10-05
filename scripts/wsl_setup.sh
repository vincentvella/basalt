#!/usr/bin/env bash
#
# A Linux host inside WSL, for scripts/compare_hosts.sh to run beside the
# Windows one.
#
# Run from Windows, against a distro installed for the purpose:
#
#   wsl --install -d Ubuntu-24.04 --no-launch
#   wsl -d Ubuntu-24.04 -u root -e bash scripts/wsl_setup.sh
#   BASALT_COMPARE_WSL=Ubuntu-24.04 scripts/compare_all.sh
#
# Ubuntu 24.04 rather than 22.04 because the GTK host needs GTK 4.10, and 22.04
# ships 4.6. Runs as root: a distro installed with --no-launch has no first-run
# user, and everything here needs root anyway. Idempotent -- each step checks
# before it acts, so a rerun after a failure resumes rather than repeats, and a
# rerun after a commit on the Windows side pulls it and rebuilds.
#
# The build is a clone on the distro's own filesystem rather than this checkout
# in place, because building under /mnt/c crosses WSL's 9p bridge on every file
# read. It is the committed state: uncommitted Windows-side changes do not reach
# it. BASALT_COMPARE_WSL_REPO in compare_hosts.sh defaults to where it lands.

set -euo pipefail

log() { printf '==> %s\n' "$*"; }

WIN_REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO="${BASALT_COMPARE_WSL_REPO:-/root/basalt-core}"
NODE_VERSION=24.21.0

# --- apt: the Linux CI job's list, and two more ------------------------------
log "apt packages"
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq \
  build-essential clang cmake ninja-build pkg-config ccache \
  libgtk-4-dev libpango1.0-dev libglib2.0-dev \
  libgoogle-glog-dev libboost-dev libboost-regex-dev libfmt-dev \
  libdouble-conversion-dev libgflags-dev libssl-dev libcurl4-openssl-dev \
  libgl1-mesa-dri xvfb xdotool x11-utils \
  git curl xz-utils rsync python3 \
  wslu desktop-file-utils >/dev/null

# --- something that opens https ----------------------------------------------
# `Linking.canOpenURL` asks GIO whether anything is registered for the scheme,
# and a bare distro has nothing -- so e2e/modules.js honestly fails a check a
# GitHub runner, which has a browser, passes. The first two-host comparison
# reported exactly that as a difference. wslview opens the Windows default
# browser, which makes it a real handler rather than a stub. The mimeinfo cache
# is not optional: without it GIO finds the desktop file and still reports no
# default.
if ! gio mime x-scheme-handler/https 2>/dev/null | grep -q '^Default application'; then
  log "wslview as the http and https handler"
  mkdir -p /usr/local/share/applications /etc/xdg
  cat >/usr/local/share/applications/wslview.desktop <<'EOF'
[Desktop Entry]
Type=Application
Name=Windows default browser (wslview)
Exec=wslview %u
NoDisplay=true
MimeType=x-scheme-handler/http;x-scheme-handler/https;
EOF
  cat >/etc/xdg/mimeapps.list <<'EOF'
[Default Applications]
x-scheme-handler/http=wslview.desktop
x-scheme-handler/https=wslview.desktop
EOF
  update-desktop-database /usr/local/share/applications
fi

# --- Node: 24.04 ships 18, which React Native 0.87 rejects -------------------
if ! node --version 2>/dev/null | grep -q "^v${NODE_VERSION}$"; then
  log "node v${NODE_VERSION}"
  tarball="node-v${NODE_VERSION}-linux-x64.tar.xz"
  curl -fsSL "https://nodejs.org/dist/v${NODE_VERSION}/${tarball}" -o "/tmp/${tarball}"
  tar -xJf "/tmp/${tarball}" -C /usr/local --strip-components=1
  rm -f "/tmp/${tarball}"
fi

# --- the repository ----------------------------------------------------------
if [ ! -d "$REPO/.git" ]; then
  log "cloning $WIN_REPO into $REPO"
  git clone -q "$WIN_REPO" "$REPO"
else
  log "pulling $REPO from $WIN_REPO"
  git -C "$REPO" pull -q --ff-only
fi
cd "$REPO"

# --- React Native at the pin, fetched the way CI fetches it ------------------
PIN=$(grep -v '^#' scripts/react-native.pin | tr -d '[:space:]')
if [ ! -d react-native-src/.git ]; then
  log "react native at ${PIN}"
  mkdir -p react-native-src
  git -C react-native-src init -q
  git -C react-native-src remote add origin https://github.com/react/react-native
  git -C react-native-src fetch -q --depth 1 origin "refs/tags/${PIN}"
  git -C react-native-src checkout -q FETCH_HEAD
fi

# --- the host, configured as the Linux CI job configures it ------------------
jobs=$(nproc)
log "bootstrap"
BUILD_JOBS="$jobs" scripts/bootstrap.sh react-native-src
log "build"
cmake -B build -G Ninja \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  -DRN_DIR=react-native-src/packages/react-native >/dev/null
cmake --build build -j "$jobs"

log "ready: $REPO/build/basalt_gtk (compare_all.sh builds the bundles it needs)"
