#!/usr/bin/env python3
"""Makes src/levels.h: Meowdoku's puzzles, taken from the game's own level banks, in the order the game deals them.

The banks come from the Android package (com.oakever.meowdoku 1.23.0): its .bin banks, XOR-ed with the game's key
and unpacked from Godot variants into JSON (kept in ~/Documents/Assets/gamesrc/meowdoku/lv, never in the repo).

Meowdoku deals its puzzles from pools: one per board size and difficulty rank, each the game's regular bank
followed by its LinkedIn-style bank (and its GC bank for 10 x 10 rank 1), minus puzzles with more than one
one-cell region. On the main path (level 51 on) a LinkedIn-modified puzzle comes after every four others, and
10 x 10 ranks 3 and 4 skip the regular bank. This keeps the first puzzles of each pool, as many as a player
meets in the first 300 levels (24 at most); when a pool runs out the game turns and mirrors it (8 ways).

Each puzzle is stored as its colour regions, cell by cell: 1 if the cell is in the region on its left, else 01 if
in the one above, else 00 and the region's number in 4 bits (with no bit for a side the cell hasn't got). Solutions are found on the calculator (each puzzle has
exactly one; the tests check it).

Usage: levels.py [bank_dir] [src/levels.h]
"""
import json
import os
import sys
from collections import Counter

BANKS = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("~/Documents/Assets/gamesrc/meowdoku/lv")
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), "..", "src", "levels.h")

SIZES_1_10 = [4, 5, 6, 6, 8, 6, 7, 8, 9, 7]
SIZES_11 = [8, 10, 10, 9, 10, 10, 9, 10, 10, 10]
SPECIAL = {10: ("sp", 44), 20: ("sp", 45), 30: ("sp", 36), 40: ("sp", 9), 50: ("sp", 37), 55: ("sp", 7),
           60: ("sp", 34), 62: ("sp", 8), 70: ("sp", 32), 75: ("sp", 6), 80: ("sp", 43), 90: ("sp", 33),
           100: ("sp", 5), 123: ("sp", 1), 200: ("lk", 30), 250: ("lk", 75), 314: ("lk", 141), 456: ("sp", 2)}
LK_MOD_RESERVED = {20, 30, 53, 71, 72, 75, 114, 141, 164}
PLAYED = 300  # levels a pool is sized for
MOST = 24  # puzzles a pool keeps at most


def load(name):
    return json.load(open(os.path.join(BANKS, name + ".json")))


def regions(e):
    rm = e["regionMap"]
    if rm and isinstance(rm[0], list):
        rm = [v for row in rm for v in row]
    return [int(v) for v in rm]


def solution(e):
    return [int(v) for v in e["solution"]]


def size_of(e):
    return int(e.get("size", 0)) or int(round(len(regions(e)) ** 0.5))


def singles(e):
    return sum(1 for v in Counter(regions(e)).values() if v == 1)


# ------------------------------------------------------------------ the banks
regular, lkstyle, gc = {}, {}, {}
for sz in range(4, 13):
    for kind, table in (("bankData%dx%d", regular), ("bankDataLKStyle%dx%d", lkstyle)):
        try:
            d = load(kind % (sz, sz))
        except FileNotFoundError:
            continue
        for rk, levels in d.items():
            if rk.isdigit():
                table[sz, int(rk)] = levels
    try:
        for e in load("bankDataGC%dx%d" % (sz, sz))["levels"]:
            gc.setdefault((sz, int(e.get("r", 0))), []).append(e)
    except FileNotFoundError:
        pass
lk_mod_all = load("bankDataLKModified")["levels"]
sp_all = load("bankDataSP")["levels"]
lk_all = load("bankDataLK")


def by_tier(levels, tier):
    return [e for e in levels if e.get("tier", "") == tier] if tier else list(levels)


def tier_for(sz, rank):
    """TierFallbackModifier: N, or '' when neither the regular nor the LinkedIn-style bank has N puzzles."""
    if by_tier(regular.get((sz, rank), []), "N") or by_tier(lkstyle.get((sz, rank), []), "N"):
        return "N"
    return ""


def pool(sz, rank, main):
    """LevelData.build_pools: the puzzles in dealing order, the filters applied (lk_mod apart)."""
    tier = "N" if rank == 5 else tier_for(sz, rank)
    reg = by_tier(regular.get((sz, rank), []), tier)
    lks = by_tier(lkstyle.get((sz, rank), []), tier)
    if tier and not reg and not lks:
        reg, lks = regular.get((sz, rank), []), lkstyle.get((sz, rank), [])
    if main and sz == 10 and rank in (3, 4):
        reg = []
    g = []
    if (sz == 10 and rank == 1) or sz == 11:
        g = by_tier(gc.get((sz, rank), []), tier) or gc.get((sz, rank), [])
    out, seen = [], set()
    for e in reg + lks + g:
        if singles(e) > 1 or e.get("_pid_h") in seen:
            continue
        seen.add(e.get("_pid_h"))
        out.append(e)
    return out


def lk_mod(sz, rank, strict):
    out = []
    for i, e in enumerate(lk_mod_all):
        if i + 1 in LK_MOD_RESERVED or size_of(e) != sz:
            continue
        if int(e.get("r", 1) if strict else e.get("maxR", e.get("r", 1))) == rank:
            out.append(e)
    return out


# ------------------------------------------------------------------ the dealer (as main.c deals)
def size_at(lv):
    return SIZES_1_10[lv - 1] if lv <= 10 else SIZES_11[(lv - 11) % 10]


def is_hard(lv):
    return lv >= 21 and lv % 10 == 0


def max_strategy(lv):
    return 6 if lv >= 201 else 5 if lv >= 101 else 4 if lv >= 51 else 3 if lv >= 21 else 2


def simulate(player, levels, rnd):
    """Pool use (the furthest index reached) for a player who wins (player 0), or fails once each level (1)."""
    use, lk_use = Counter(), Counter()
    cur, clean, fails, pend = 1, 0, 0, False
    cursor = {}
    for lv in range(1, levels + 1):
        retried = False
        if lv in SPECIAL:
            pass
        else:
            sz = size_at(lv)
            if is_hard(lv):
                key = (sz, 5, lv >= 51, True)
            else:
                if lv >= 51 and cur < 2:
                    cur = 2
                s = 1 if lv <= 5 else max(cur, 2) if lv >= 51 else cur
                s = min(s, 4 if lv >= 51 else 3 if lv >= 21 else 2)
                if s >= 3:
                    s = 2 + rnd() % (s - 1)
                key = (sz, s, lv >= 51, False)
            c = cursor.setdefault(key, [0, 0, 0])  # idx, since_lk, lk_idx
            if key[2] and c[1] >= 4 and c[2] < len(lk_mod(key[0], key[1], key[3])):
                c[2] += 1
                c[1] = 0
                lk_use[key[0], key[1], key[3]] = max(lk_use[key[0], key[1], key[3]], c[2])
            else:
                c[0] += 1
                c[1] += 1
                pk = (key[0], key[1], key[2] and key[0] == 10 and key[1] in (3, 4))
                use[pk] = max(use[pk], c[0])
        if player == 1 and lv >= 6:
            retried = True
            clean, fails = 0, fails + 1
        if lv >= 6:
            mx = max_strategy(lv)
            mn = 2 if mx >= 5 else 1
            demoted = False
            if not retried:
                clean += 1
                if clean >= (1 if lv >= 51 else 2) and cur < mx:
                    cur, clean = cur + 1, 0
            else:
                clean = 0
            if fails >= (2 if lv >= 21 else 1) and cur > mn:
                cur, demoted = cur - 1, True
            fails = 0
            if pend and not demoted:
                cur, pend, demoted = max(mn, cur - 1), False, True
            if retried and not demoted:
                nxt = lv + 1
                if is_hard(nxt) or nxt in SPECIAL:
                    pend = True
                else:
                    cur = max(mn, cur - 1)
    return use, lk_use


def rng(seed):
    s = [seed]

    def r():
        s[0] ^= (s[0] << 13) & 0xFFFFFFFF
        s[0] ^= s[0] >> 17
        s[0] ^= (s[0] << 5) & 0xFFFFFFFF
        return s[0]
    return r


need, lk_need = Counter(), Counter()
for player in (0, 1):
    for seed in (1, 7, 99, 12345):
        u, l = simulate(player, PLAYED, rng(seed))
        for k, v in u.items():
            need[k] = max(need[k], v)
        for k, v in l.items():
            lk_need[k] = max(lk_need[k], v)


# ------------------------------------------------------------------ checks
def solve(n, rm, limit=2):
    """Solutions of the region map, up to limit."""
    sols, cols = [], [0] * n

    def ok(r, c, placed):
        for pr, pc in enumerate(placed):
            if pc == c or rm[pr * n + pc] == rm[r * n + c] or (r - pr == 1 and abs(pc - c) <= 1):
                return False
        return True

    def go(r, placed):
        if len(sols) >= limit:
            return
        if r == n:
            sols.append(list(placed))
            return
        for c in range(n):
            if ok(r, c, placed):
                placed.append(c)
                go(r + 1, placed)
                placed.pop()
    go(0, [])
    return sols


def check(e, n):
    rm = regions(e)
    assert len(rm) == n * n and sorted(set(rm)) == list(range(n)), (n, e.get("id"))
    s = solve(n, rm)
    assert len(s) == 1, ("not unique", n, e.get("id"))
    if "solution" in e:
        assert s[0] == solution(e), ("solution differs", e.get("id"))


# ------------------------------------------------------------------ packing
class Bits:
    def __init__(self):
        self.out, self.acc, self.n = bytearray(), 0, 0

    def put(self, v, k):
        for i in range(k - 1, -1, -1):
            self.acc = self.acc << 1 | (v >> i & 1)
            self.n += 1
            if self.n == 8:
                self.out.append(self.acc)
                self.acc = self.n = 0

    def end(self):
        if self.n:
            self.put(0, 8 - self.n)
        return bytes(self.out)


def encode(e, n, cmap=None):
    rm, b = regions(e), Bits()
    for i, v in enumerate(rm):
        r, c = divmod(i, n)
        if c and rm[i - 1] == v:
            b.put(1, 1)
            continue
        if c:
            b.put(0, 1)
        if r and rm[i - n] == v:
            b.put(1, 1)
            continue
        if r:
            b.put(0, 1)
        b.put(v, 4)
    for v in cmap or []:
        b.put(v, 4)
    return b.end()


blob, offs = bytearray(), []


def add(e, n, cmap=None):
    check(e, n)
    offs.append(len(blob))
    blob.extend(encode(e, n, cmap))
    return len(offs) - 1


pools = []  # (size, rank, main_only, first, count)
for (sz, rank, main_only), k in sorted(need.items()):
    p = pool(sz, rank, main_only)
    k = min(len(p), MOST, max(k, 4))
    first = len(offs)
    for e in p[:k]:
        add(e, sz)
    pools.append((sz, rank, int(main_only), first, k))
lkpools = []  # (size, rank, strict, first, count)
for (sz, rank, strict), k in sorted(lk_need.items()):
    p = lk_mod(sz, rank, strict)[:k]
    first = len(offs)
    for e in p:
        add(e, sz)
    lkpools.append((sz, rank, int(strict), first, len(p)))
specials = []  # (level, size, puzzle, has colour map)
for lv, (src, idx) in sorted(SPECIAL.items()):
    e = (sp_all if src == "sp" else lk_all)[idx - 1]
    n = size_of(e)
    cm = [int(v) for v in e.get("colorMap", [])] if src == "sp" else None
    specials.append((lv, n, add(e, n, cm), int(bool(cm))))

offs.append(len(blob))
with open(OUT, "w") as f:
    f.write("/* Made by tools/levels.py from Meowdoku's own level banks: %d puzzles, %d bytes. */\n"
            % (len(offs) - 1, len(blob)))
    f.write("static const uint8_t lv_data[%d] = {\n" % len(blob))
    for i in range(0, len(blob), 24):
        f.write("  " + ",".join(str(b) for b in blob[i:i + 24]) + ",\n")
    f.write("};\nstatic const uint16_t lv_at[%d] = {" % len(offs))
    f.write(",".join(str(o) for o in offs) + "};\n")
    f.write("/* the pools: size, rank, main path only (10 x 10 ranks 3 and 4), first puzzle, count */\n")
    f.write("static const uint8_t lv_pool[%d][5] = {%s};\n" % (len(pools), ",".join(
        "{%d,%d,%d,%d,%d}" % (a, b, c, d & 255, e) for a, b, c, d, e in pools)))
    f.write("static const uint8_t lv_pool_hi[%d] = {%s};\n" % (len(pools), ",".join(str(p[3] >> 8) for p in pools)))
    f.write("/* LinkedIn-modified puzzles of the main path: size, rank, strict, first, count */\n")
    f.write("static const uint16_t lv_lk[%d][5] = {%s};\n" % (len(lkpools), ",".join(
        "{%d,%d,%d,%d,%d}" % p for p in lkpools)))
    f.write("/* special levels: level, size, puzzle, its own colours */\n")
    f.write("static const uint16_t lv_special[%d][4] = {%s};\n" % (len(specials), ",".join(
        "{%d,%d,%d,%d}" % s for s in specials)))
print("%d puzzles, %d bytes, %d pools, %d lk pools" % (len(offs) - 1, len(blob), len(pools), len(lkpools)))
for p in pools:
    print("  pool", p)
for p in lkpools:
    print("  lk", p)
