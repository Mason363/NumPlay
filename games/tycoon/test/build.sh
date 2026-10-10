#!/bin/sh
# builds the PC test host into $TT (default /tmp/tt): build.sh [extra gcc flags]
set -e
cd "$(dirname "$0")"
TT=${TT:-/tmp/tt}
mkdir -p "$TT"
[ -f font.h ] || python3 -I genfont.py font.h
sed -e "s#\"../../common/#\"$TT/#" ../src/main.c > "$TT/tycoon_patched.c"
for h in epsilon_app epsilon_files; do cp ../../common/$h.h "$TT/$h.h"; done
# the host build has a RAM file system
python3 -I - "$TT/epsilon_app.h" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
old = "__attribute__((unused)) static uint8_t *epsilon_storage(uint32_t *size) {\n  (void)size;\n  return NULL;\n}"
assert old in s
s = s.replace(old, "uint8_t *host_storage(uint32_t *size);\n__attribute__((unused)) static uint8_t *epsilon_storage(uint32_t *size) { return host_storage(size); }")
open(p, "w").write(s)
PY
gcc -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -I"$TT" -Istub "$@" host.c -o "$TT/host"
