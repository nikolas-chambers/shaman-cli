#!/bin/sh
# Install shaman: curl -fsSL https://raw.githubusercontent.com/nikolas-chambers/shaman-cli/main/scripts/install.sh | sh
#
# Picks the release binary for this machine (Linux x64/arm64, macOS arm64/x64, Android Termux) and puts it in
# ~/.local/bin (Termux: $PREFIX/bin). Options via environment:
#   SHAMAN_VERSION=v0.2.0      a specific release (default: latest)
#   SHAMAN_INSTALL_DIR=/usr/local/bin
set -eu

repo="${SHAMAN_REPO:-nikolas-chambers/shaman-cli}"
base="${SHAMAN_INSTALL_BASE:-https://github.com/$repo/releases}"  # test hook
version="${SHAMAN_VERSION:-latest}"

os="$(uname -s)"; arch="$(uname -m)"
case "$arch" in x86_64|amd64) arch=x64 ;; aarch64|arm64) arch=arm64 ;; *) echo "unsupported CPU: $arch" >&2; exit 1 ;; esac
if [ -n "${PREFIX:-}" ] && case "$PREFIX" in *com.termux*) true ;; *) false ;; esac; then
  asset="shaman-android-termux-aarch64"; dir="${SHAMAN_INSTALL_DIR:-$PREFIX/bin}"
  echo "Termux: installing libcurl and openssl first"; pkg install -y libcurl openssl >/dev/null
else
  case "$os" in
    Linux) asset="shaman-linux-$arch" ;;
    Darwin) asset="shaman-macos-$arch" ;;
    *) echo "unsupported OS: $os (on Windows use install.ps1)" >&2; exit 1 ;;
  esac
  dir="${SHAMAN_INSTALL_DIR:-$HOME/.local/bin}"
fi

if [ "$version" = latest ]; then url="$base/latest/download/$asset"; else url="$base/download/$version/$asset"; fi
tmp="$(mktemp)"; trap 'rm -f "$tmp"' EXIT
echo "Downloading $asset ($version)..."
if command -v curl >/dev/null; then curl -fsSL "$url" -o "$tmp"; else wget -qO "$tmp" "$url"; fi
mkdir -p "$dir"
chmod 755 "$tmp"
mv "$tmp" "$dir/shaman"
trap - EXIT
echo "Installed $dir/shaman ($("$dir/shaman" --version 2>/dev/null || echo "run it to check"))"
case ":$PATH:" in *":$dir:"*) ;; *) echo "Add $dir to your PATH, e.g.: echo 'export PATH=\"$dir:\$PATH\"' >> ~/.profile" ;; esac
echo "Next: shaman auth login <provider>   (or just run shaman if you use the GitHub CLI)"
