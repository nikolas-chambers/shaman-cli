#!/usr/bin/env bash
# Build a release zip for this machine's platform into releases/:
#   releases/shaman-<version>-<os>-<arch>.zip  (shaman, the desktop app, shaman.ini.example, README.md, LICENSE)
# Unzip anywhere; rename shaman.ini.example to shaman.ini next to the binary for portable mode.
# --personal also writes a -personal zip with your shaman.ini (and its keys) included.
set -euo pipefail
cd "$(dirname "$0")/.."
version="$(sed -n 's/^project(shaman-cli VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
os="$(uname -s | tr '[:upper:]' '[:lower:]')"; arch="$(uname -m)"
[ -n "${PREFIX:-}" ] && [[ "$PREFIX" == *com.termux* ]] && os="android-termux"
case "$arch" in x86_64|amd64) arch=x64 ;; aarch64|arm64) arch=arm64 ;; esac
name="shaman-$version-$os-$arch"

desktop=OFF  # include the desktop app when its web view library is available
if [ "$os" = darwin ] || pkg-config --exists webkit2gtk-4.1 2>/dev/null; then desktop=ON; fi
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DSHAMAN_BUILD_TESTS=OFF -DSHAMAN_STATIC=ON \
  -DSHAMAN_BUILD_DESKTOP=$desktop >/dev/null
cmake --build build-release
stage="$(mktemp -d)/$name"
mkdir -p "$stage" releases
cp build-release/shaman "$stage/"
[ -f build-release/shaman-desktop ] && cp build-release/shaman-desktop "$stage/"
[ -d build-release/Shaman.app ] && cp -R build-release/Shaman.app "$stage/"
strip "$stage/shaman" "$stage/shaman-desktop" 2>/dev/null || true
cp shaman.ini.example README.md LICENSE "$stage/"
(cd "$(dirname "$stage")" && rm -f "$OLDPWD/releases/$name.zip" && zip -qr "$OLDPWD/releases/$name.zip" "$name")
echo "releases/$name.zip"
# --personal: a second zip with your own shaman.ini (keys included) next to the binary. Never upload it.
if [ "${1:-}" = "--personal" ] && [ -f shaman.ini ]; then
  cp shaman.ini "$stage/shaman.ini"
  (cd "$(dirname "$stage")" && rm -f "$OLDPWD/releases/$name-personal.zip" && zip -qr "$OLDPWD/releases/$name-personal.zip" "$name")
  echo "releases/$name-personal.zip  (contains your keys: keep it private)"
fi

# A Windows CLI zip too, when a cross-build exists (cmake/mingw-w64.cmake into build-win/).
if [ -f build-win/shaman.exe ]; then
  wname="shaman-$version-windows-x64"
  wstage="$(mktemp -d)/$wname"
  mkdir -p "$wstage"
  cp build-win/shaman.exe shaman.ini.example README.md LICENSE "$wstage/"
  x86_64-w64-mingw32-strip "$wstage/shaman.exe" 2>/dev/null || true
  (cd "$(dirname "$wstage")" && rm -f "$OLDPWD/releases/$wname.zip" && zip -qr "$OLDPWD/releases/$wname.zip" "$wname")
  echo "releases/$wname.zip"
  if [ "${1:-}" = "--personal" ] && [ -f shaman.ini ]; then
    cp shaman.ini "$wstage/shaman.ini"
    (cd "$(dirname "$wstage")" && rm -f "$OLDPWD/releases/$wname-personal.zip" && zip -qr "$OLDPWD/releases/$wname-personal.zip" "$wname")
    echo "releases/$wname-personal.zip  (contains your keys: keep it private)"
  fi
fi

# A Linux arm64 zip too, when a cross-build exists (cmake/aarch64-linux-gnu.cmake into build-arm64/).
if [ -f build-arm64/shaman ] && [ "$arch" != arm64 ]; then
  aname="shaman-$version-linux-arm64"
  astage="$(mktemp -d)/$aname"
  mkdir -p "$astage"
  cp build-arm64/shaman shaman.ini.example README.md LICENSE "$astage/"
  aarch64-linux-gnu-strip "$astage/shaman" 2>/dev/null || true
  (cd "$(dirname "$astage")" && rm -f "$OLDPWD/releases/$aname.zip" && zip -qr "$OLDPWD/releases/$aname.zip" "$aname")
  echo "releases/$aname.zip"
  if [ "${1:-}" = "--personal" ] && [ -f shaman.ini ]; then
    cp shaman.ini "$astage/shaman.ini"
    (cd "$(dirname "$astage")" && rm -f "$OLDPWD/releases/$aname-personal.zip" && zip -qr "$OLDPWD/releases/$aname-personal.zip" "$aname")
    echo "releases/$aname-personal.zip  (contains your keys: keep it private)"
  fi
fi
