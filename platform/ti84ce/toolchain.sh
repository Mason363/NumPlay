#!/usr/bin/env bash
# The CE C toolchain (CEdev v15.0) in the folder given, as its makers build it for Linux and macOS
# (github.com/CE-Programming/toolchain/releases). port.mk's CEDEV is that folder.
#   platform/ti84ce/toolchain.sh ~/toolchains/CEdev
set -euo pipefail
V=v15.0
DIR=${1:?usage: toolchain.sh DIR}
case "$(uname -s)-$(uname -m)" in
  Linux-x86_64) F=CEdev-Linux.tar.gz SUM=85f4794a617b134249a9cba8a084414e86e16d2a68377b1d72ace30c636dbdf9 ;;
  Darwin-arm64) F=CEdev-macOS-arm.dmg SUM=3cc6cbe542ca851f8c9f55fbfc0add512b1b8e05853e33e3bbf1de9951f9d30e ;;
  Darwin-x86_64) F=CEdev-macOS-intel.dmg SUM=231d84d1af49c07d42a22e39e08bdb9f92538a53f4fd9bfbd37b2650b9e39115 ;;
  *) echo "toolchain.sh: CEdev $V has no build for $(uname -sm)" >&2; exit 1 ;;
esac
if [ "$(cat "$DIR/.numplay" 2>/dev/null)" = "$F $SUM" ] && [ -x "$DIR/bin/cedev-config" ]; then
  echo "CEdev $V is in $DIR"
  exit 0
fi

sha256() { if command -v sha256sum > /dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi | cut -d' ' -f1; }
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
curl -fsSL --retry 3 -o "$tmp/$F" "https://github.com/CE-Programming/toolchain/releases/download/$V/$F"
[ "$(sha256 "$tmp/$F")" = "$SUM" ] || { echo "toolchain.sh: $F isn't the one released" >&2; exit 1; }
mkdir -p "$tmp/x" "$DIR"
case $F in
  *.tar.gz) tar -xzf "$tmp/$F" -C "$tmp/x" ;;
  *.dmg)
    hdiutil attach -quiet -nobrowse -readonly -mountpoint "$tmp/mnt" "$tmp/$F"
    cp -R "$tmp/mnt/CEdev" "$tmp/x/"
    hdiutil detach -quiet "$tmp/mnt" ;;
esac
# (only CEdev's own folders are replaced: DIR may hold other things)
for d in "$tmp"/x/CEdev/*; do rm -rf "${DIR:?}/$(basename "$d")"; done
cp -R "$tmp/x/CEdev/." "$DIR/"
echo "$F $SUM" > "$DIR/.numplay"
"$DIR/bin/cedev-config" --version
echo "CEdev $V is in $DIR"
