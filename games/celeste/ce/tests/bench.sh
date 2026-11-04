#!/bin/bash
# Drawing speed in the emulator (tools/ce_emu.py): a build that goes straight into a room and moves the camera about it
# (every frame drawn whole), and the game's own build standing still in the first room (what changed drawn only).
#   tests/bench.sh [ROOM] [extra ce_emu options]
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
ROOM="${1:-11}"
shift || true
cd "$HERE"
export PATH="$HOME/Documents/Assets/toolchains/CEdev/bin:$PATH"
make -s OBJDIR=build/bench/obj BINDIR=build/bench/bin BENCH=-DCEL_BENCH=$ROOM 2>&1 | grep -E "error|success" || true
make -s 2>&1 | grep -E "error|success" || true
echo "camera moving (room $ROOM)"
python3 ../../../tools/ce_emu.py build/bench/bin/CELESTE.8xp bin/CEL*.8xv --frame _plat_frame --ms 12000 \
  --frames-from 2000 --shots 4000 --out build/bench "$@" | tail -n +3
echo "standing still (room 1)"
python3 ../../../tools/ce_emu.py bin/CELESTE.8xp bin/CEL*.8xv --frame _plat_frame --keys "1000-1100:2nd,2000-2100:2nd" \
  --ms 16000 --frames-from 6000 --shots 8000 --out build/bench/still "$@" | tail -n +3
