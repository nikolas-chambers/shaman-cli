#!/usr/bin/env bash
# Build and install shaman inside Termux on Android.
#   git clone https://github.com/nikolas-chambers/shaman-cli && cd shaman-cli && bash scripts/termux-install.sh
# Re-run it to update. Optional extras: `pkg install termux-api ripgrep` (plus the Termux:API app) for
# notifications, clipboard and opening links, and faster search.
set -euo pipefail

if [ -z "${PREFIX:-}" ] || [[ "$PREFIX" != *com.termux* ]]; then
  echo "This script is for Termux on Android. Elsewhere, see the README's build section." >&2
  exit 1
fi

cd "$(dirname "$0")/.."
pkg install -y clang cmake ninja git libcurl openssl nlohmann-json
cmake -S . -B build-termux -G Ninja -DCMAKE_BUILD_TYPE=Release -DSHAMAN_BUILD_TESTS=OFF
cmake --build build-termux
install -m 755 build-termux/shaman "$PREFIX/bin/shaman"
echo
echo "Installed $PREFIX/bin/shaman ($(shaman --version))."
echo "Add a model key with: shaman auth login opencode   (or anthropic, openai, google, ...)"
echo "Then run: shaman"
