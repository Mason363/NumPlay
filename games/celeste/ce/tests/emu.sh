#!/bin/bash
# The CE build in the emulator (tools/ce_emu.py): Forsaken City from the title, running right and jumping.
#   tests/emu.sh OUT_DIR [extra ce_emu options]
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$HERE/build/emu}"
shift || true
cd "$HERE"
PATH="$HOME/Documents/Assets/toolchains/CEdev/bin:$PATH" make -s 2>&1 | grep -E "error|success" || true
python3 ../../../platform/ti84ce/check.py .
python3 ../../../tools/ce_emu.py bin/CELESTE.8xp bin/CEL*.8xv --frame _plat_frame \
  --keys "1000-1100:2nd,2000-2100:2nd,3000-3100:2nd,6000-8000:right,7000-7300:2nd,8500-9000:left,9200-9400:clear" \
  --shots 1500,2500,3500,6500,7500,8800,10000 --ms 11000 --frames-from 5500 --out "$OUT" "$@"
