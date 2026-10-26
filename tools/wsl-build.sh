#!/bin/bash
# Builds the watchface inside the `pebble` WSL distro (SDK 4.33.1).
# Run from Windows:  wsl -d pebble -- bash /mnt/c/.../nadir/tools/wsl-build.sh [install-emulator]
set -euo pipefail
SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK=/root/nadir
mkdir -p "$WORK"
rm -rf "$WORK/src" "$WORK/resources"
cp -r "$SRC/src" "$SRC/resources" "$SRC/package.json" "$SRC/wscript" "$WORK/"
cd "$WORK"
export PATH="$HOME/.local/bin:$PATH"
pebble build
cp build/nadir.pbw "$SRC/nadir.pbw"
case "${1:-}" in
  install-emulator) pebble install --emulator emery ;;
  install-phone) pebble install --phone "${2:?usage: install-phone <host>:<port>, see tools/phone-relay.mjs}" ;;
esac
