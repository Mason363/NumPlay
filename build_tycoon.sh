#!/bin/sh
# Builds NumTycoon.nwa (only the new game) and, if you want, the whole NumPlay with it.
#   ./build_tycoon.sh          -> games/tycoon/output/tycoon.nwa  (copied to ./NumTycoon.nwa)
#   ./build_tycoon.sh all      -> also build/NumPlay.nwa (every game, NumTycoon included)
# Needs: arm-none-eabi-gcc (with newlib), Node.js 18+ (for nwlink, fetched by npx), internet for npx.
set -e
cd "$(dirname "$0")"
for t in arm-none-eabi-gcc arm-none-eabi-strip npx; do
  command -v "$t" >/dev/null || { echo "missing: $t"; exit 1; }
done
make -C games/tycoon clean
make -C games/tycoon build
make -C games/tycoon check          # links it like the calculator and prints its size
cp games/tycoon/output/tycoon.nwa NumTycoon.nwa
echo "OK: NumTycoon.nwa ($(wc -c < NumTycoon.nwa) bytes)"
if [ "$1" = all ]; then
  command -v cargo >/dev/null || { echo "missing: cargo (Rust, thumbv7em-none-eabihf target, for Tetris)"; exit 1; }
  make            # build/NumPlay*.nwa and build/apps/*.nwa
  make check
  echo "OK: build/NumPlay.nwa and build/apps/NumTycoon.nwa"
fi
