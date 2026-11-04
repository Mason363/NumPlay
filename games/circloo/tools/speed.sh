#!/bin/sh
# Speed of a level in the emulator (about an N0110's speed): tools/speed.sh LEVEL [ms] [keys]
# LEVEL: 1-14, or 101-106 for hard mode. Builds a copy that starts in that level (CO_START); SPEED_EXTRA=-DCO_RINGS=6
# opens every ring first.
set -e
cd "$(dirname "$0")/.."
ROOT=$(cd ../.. && pwd)
L=$1
MS=${2:-8000}
KEYS=${3:-"500-1500:right,2000-3000:left,3500-4500:right"}
OUT=${TMPDIR:-/tmp}/circloo_speed_$L
rm -rf "$OUT"
make -s build NWLINK="node $ROOT/games/numdash/node_modules/nwlink/bin/nwlink" EXTRA="-DCO_START=$L $SPEED_EXTRA" BUILD_DIR="$OUT" >/dev/null
PATH="$ROOT/games/numdash/node_modules/.bin:$PATH" python3 "$ROOT/tools/emu.py" "$OUT/circloo.nwa" --ms "$MS" \
  --keys "$KEYS" --shots "$((MS - 100))" --out "$OUT" --ram-length 133652 | tail -1
