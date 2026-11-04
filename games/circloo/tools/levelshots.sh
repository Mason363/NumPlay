#!/bin/sh
# Each level with every ring open, one screenshot each, into a sheet: tools/levelshots.sh OUT.png LEVEL... (h5: hard 5)
set -e
cd "$(dirname "$0")/.."
OUT=$1
shift
D=build/levelshots
rm -rf "$D" && mkdir -p "$D"
for l in "$@"; do
  case $l in
  h*) ./build/play --level "${l#h}" --hard --rings 6 --frames 2 --shots 1 --out "$D" --saves "$D" >/dev/null ;;
  *) ./build/play --level "$l" --rings 6 --frames 2 --shots 1 --out "$D" --saves "$D" >/dev/null ;;
  esac
  mv "$D/shot_00001.ppm" "$D/$l.ppm"
done
/usr/bin/python3 tools/sheet.py "$OUT" $(for l in "$@"; do echo "$D/$l.ppm"; done)
