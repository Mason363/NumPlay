#!/usr/bin/env python3
"""Bad Time Simulator's attacks: jcw87's attack scripts (Files/*.csv of github.com/jcw87/c2-sans-fight) turned
into the game's tables, and played by a second, separate reading of them to check the game's own.

  attacks.py REF [SPEC]    REF: the project's folder; writes src/attacks.c, src/attacks.h, tests/expect.h
                           (and SPEC/attacks.md, every attack tick by tick, when SPEC is given)

A script line is: a delay in seconds (a number or $variable), a function, its arguments. The game keeps every line
(labels and sounds become NOPs, so line numbers stay right for the jumps), the arguments as numbers from a pool or
variables, the delays turned into ticks of 1/30 s when the game runs them. tests/expect.h holds, for each attack,
what this reading of the scripts does with a fixed random seed: the tick it ends, the number of things it makes and
a hash of them (the zone, the heart, the bones, blasters, platforms, the opponent's poses), which the game's own
interpreter must match exactly (make test). Read the reference with python3 -I; never run it."""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.join(HERE, "..")

# the scripts, in the order the fight first plays them (the game's attack numbers)
ATTACKS = ["sans_intro", "sans_bonegap1", "sans_bluebone", "sans_bonegap2", "sans_platforms1", "sans_platforms2",
           "sans_platforms3", "sans_platforms4", "sans_platformblaster", "sans_platforms4hard", "sans_bonegap1fast",
           "sans_boneslideh", "sans_platformblasterfast", "sans_spare", "sans_multi1", "sans_randomblaster1",
           "sans_multi2", "sans_bonestab1", "sans_bonestab2", "sans_randomblaster2", "sans_boneslidev", "sans_multi3",
           "sans_bonestab3", "sans_final"]

OPS = ["NOP", "SET", "ADD", "SUB", "MUL", "DIV", "MOD", "FLOOR", "SIN", "COS", "ANGLE", "RND", "DEG", "RAD",
       "JMPABS", "JMPREL", "JMPZ", "JMPNZ", "JMPE", "JMPNE", "JMPL", "JMPNL", "JMPG", "JMPNG", "TLPause", "TLResume",
       "EndAttack", "BlackScreen", "BoneH", "BoneV", "BoneHRepeat", "BoneVRepeat", "SineBones", "BoneStab",
       "GasterBlaster", "Platform", "PlatformRepeat", "HeartMode", "HeartTeleport", "HeartMaxFallSpeed", "SansSlam",
       "SansSlamDamage", "CombatZoneSpeed", "CombatZoneResize", "CombatZoneResizeInstant", "GetHeartPos",
       "SansAnimation", "SansBody", "SansTorso", "SansHead", "SansSweat", "SansX", "SansRepeat", "SansEndRepeat",
       "SansText"]
NOPS = {"Sound", "Music"}
# the opponent's poses, as numbers
ANIM = {"": 0, "Idle": 1, "HeadBob": 2, "Tired": 3}
BODY = {"HandUp": 0, "HandDown": 1, "HandLeft": 2, "HandRight": 3}
TORSO = {"Default": 0, "Shrug": 1}
HEAD = {"Default": 0, "LookLeft": 1, "Wink": 2, "ClosedEyes": 3, "NoEyes": 4, "BlueEye": 5, "Tired1": 6, "Tired2": 7}
# the two lines the scripts say: the game says its own words there (text.c)
TALK = {"ready?": 0, "here we go.": 1}
# first arguments that name a variable to write
WRITES = {"SET", "ADD", "SUB", "MUL", "DIV", "MOD", "FLOOR", "SIN", "COS", "ANGLE", "RND", "DEG", "RAD"}
JUMPS = {"JMPABS", "JMPZ", "JMPNZ", "JMPE", "JMPNE", "JMPL", "JMPNL", "JMPG", "JMPNG"}


def die(msg):
    sys.exit("attacks.py: " + msg)


def parse_float(s):
    """JavaScript's parseFloat, for the scripts' numbers (0 when there's none)"""
    s = s.strip()
    n = 0
    while n < len(s) and (s[n].isdigit() or s[n] in "+-.eE"):
        n += 1
    while n:
        try:
            return float(s[:n])
        except ValueError:
            n -= 1
    return 0.0


def load(ref, name):
    rows = []
    with open(os.path.join(ref, "Files", name + ".csv"), encoding="utf-8") as f:
        for raw in f.read().splitlines():
            if not raw.strip():
                continue
            rows.append(raw.split(","))
    return rows


class Pool:
    def __init__(self):
        self.vals, self.at = [], {}

    def add(self, v):
        v = float(v)
        key = repr(v)
        if key not in self.at:
            self.at[key] = len(self.vals)
            self.vals.append(v)
        return self.at[key]


def compile_attack(rows, pool, name):
    """each line: (op, [delay, args...]) with args ('n', value) or ('v', variable)"""
    labels, vars_ = {}, {"pi": 0}
    for i, r in enumerate(rows, 1):
        f = r[1].strip() if len(r) > 1 else ""
        if f.startswith(":"):
            labels[f[1:]] = i

    def var(nm):
        if nm not in vars_:
            vars_[nm] = len(vars_)
        return vars_[nm]

    def arg(tok, kind="num"):
        tok = tok.strip()
        if tok.startswith("$"):
            return ("v", var(tok[1:]))
        if kind == "target":
            if tok.isdigit():
                return ("n", float(tok))
            if tok not in labels:
                die(f"{name}: no label {tok}")
            return ("n", float(labels[tok]))
        return ("n", parse_float(tok))

    out = []
    for i, r in enumerate(rows, 1):
        f = r[1].strip() if len(r) > 1 else ""
        a = [c for c in r[2:]]
        while a and a[-1].strip() == "":
            a.pop()
        delay = arg(r[0])
        if f.startswith(":") or f in NOPS:
            out.append(("NOP", [delay]))
            continue
        if f not in OPS:
            die(f"{name} line {i}: unknown function {f}")
        if f in WRITES:
            args = [("n", float(var(a[0].strip())))] + [arg(t) for t in a[1:]]
        elif f in JUMPS:
            args = [arg(a[0], "target")] + [arg(t) for t in a[1:]]
        elif f == "GetHeartPos":
            args = [("n", float(var(a[0].strip()))), ("n", float(var(a[1].strip())))]
        elif f == "SansAnimation":
            args = [("n", float(ANIM[a[0].strip() if a else ""]))]
        elif f == "SansBody":
            args = [("n", float(BODY[a[0].strip()]))]
        elif f == "SansTorso":
            args = [("n", float(TORSO[a[0].strip()]))]
        elif f == "SansHead":
            args = [("n", float(HEAD[a[0].strip()]))]
        elif f == "SansText":
            args = [("n", float(TALK[a[0].strip()]))]
        elif f == "CombatZoneResize":
            fin = a[4].strip() if len(a) > 4 else ""
            if fin not in ("", "TLResume"):
                die(f"{name} line {i}: finishing action {fin}")
            args = [arg(t) for t in a[:4]] + [("n", 1.0 if fin else 0.0)]
        else:
            args = [arg(t) for t in a]
        out.append((f, [delay] + args))
    for f, args in out:
        for k, v in args:
            if k == "n":
                pool.add(v)
    return out, vars_


# ---------------------------------------------------------------- the same sums as the game (sim.c), in doubles
PI = 3.14159265358979323846
D2R = PI / 180.0


def fl(x):
    return float(math.floor(x))


def sin_deg(d):
    """sin of d degrees: as the game's bt_sin_deg, step by step (same doubles, same rounding)"""
    d = d - 360.0 * fl(d / 360.0)
    if d == 0.0 or d == 180.0:
        return 0.0
    if d == 90.0:
        return 1.0
    if d == 270.0:
        return -1.0
    s = 1.0
    if d >= 180.0:
        d = d - 180.0
        s = -1.0
    if d > 90.0:
        d = 180.0 - d
    x = d * D2R
    x2 = x * x
    p = -2.5052108385441720e-08
    p = p * x2 + 2.7557319223985893e-06
    p = p * x2 - 1.9841269841269841e-04
    p = p * x2 + 8.3333333333333333e-03
    p = p * x2 - 1.6666666666666666e-01
    p = p * x2 + 1.0
    return s * (x * p)


def cos_deg(d):
    return sin_deg(d + 90.0)


def atan_unit(t):
    """atan of t in [0, 1], radians"""
    big = t > 0.4142135623730950
    if big:
        t = (t - 1.0) / (t + 1.0)
    t2 = t * t
    p = 0.0769230769230769
    p = p * t2 - 0.0833333333333333
    p = p * t2 + 0.0909090909090909
    p = p * t2 - 0.1
    p = p * t2 + 0.1111111111111111
    p = p * t2 - 0.1428571428571428
    p = p * t2 + 0.2
    p = p * t2 - 0.3333333333333333
    r = t + t * t2 * p
    if big:
        r = r + 0.7853981633974483
    return r


def atan2_deg(y, x):
    if x == 0.0 and y == 0.0:
        return 0.0
    ax, ay = abs(x), abs(y)
    if ay <= ax:
        a = atan_unit(ay / ax)
    else:
        a = 1.5707963267948966 - atan_unit(ax / ay)
    if x < 0.0:
        a = 3.141592653589793 - a
    if y < 0.0:
        a = -a
    return a / D2R


class Rng:
    """xorshift32, as the game's"""
    def __init__(self, seed):
        self.x = seed & 0xFFFFFFFF or 1

    def next(self):
        x = self.x
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        self.x = x
        return x

    def below(self, n):
        return (self.next() * n) >> 32 if n > 0 else 0


def ticks(sec):
    v = math.floor(sec * 30.0 + 0.5)
    return int(v) if v > 0 else 0


def ms(v):
    return int(math.floor(v * 1000.0 + 0.5))


MENU = (33.0, 251.0, 608.0, 391.0)
DX = (1, 0, -1, 0)
DY = (0, 1, 0, -1)
HEART_TEST = (320.0, 304.0)     # GetHeartPos while checking (the game's test hook says the same)


def run(code, nvars, seed, limit=30 * 120):
    """plays one attack: the log of what it makes, and the tick it ends"""
    rng = Rng(seed)
    vars_ = [0.0] * nvars
    vars_[0] = PI
    zone = list(MENU)
    target = list(MENU)
    speed = 480
    finish = talk = False
    running, line, t = True, 1, 0
    log = []
    tick = 0
    ended = None

    def val(a):
        k, v = a
        return vars_[v] if k == "v" else v

    def emit(*items):
        log.append(" ".join(str(x) for x in (tick,) + items))

    while ended is None and tick < limit:
        # the script
        if running:
            n = 0
            while running and ended is None and 1 <= line <= len(code):
                op, args = code[line - 1]
                d = ticks(val(args[0]))
                if t < d:
                    break
                a = [val(x) for x in args[1:]]
                jump = None
                if op == "NOP" or op == "TLResume":
                    pass
                elif op == "SET":
                    vars_[int(a[0])] = a[1]
                elif op == "ADD":
                    vars_[int(a[0])] = a[1] + a[2]
                elif op == "SUB":
                    vars_[int(a[0])] = a[1] - a[2]
                elif op == "MUL":
                    vars_[int(a[0])] = a[1] * a[2]
                elif op == "DIV":
                    vars_[int(a[0])] = a[1] / a[2] if a[2] != 0.0 else 0.0
                elif op == "MOD":
                    vars_[int(a[0])] = math.fmod(a[1], a[2]) if a[2] != 0.0 else 0.0
                elif op == "FLOOR":
                    vars_[int(a[0])] = fl(a[1])
                elif op == "SIN":
                    vars_[int(a[0])] = sin_deg(a[1])
                elif op == "COS":
                    vars_[int(a[0])] = cos_deg(a[1])
                elif op == "DEG":
                    vars_[int(a[0])] = a[1] / D2R
                elif op == "RAD":
                    vars_[int(a[0])] = a[1] * D2R
                elif op == "ANGLE":
                    vars_[int(a[0])] = atan2_deg(a[4] - a[2], a[3] - a[1])
                elif op == "RND":
                    vars_[int(a[0])] = float(rng.below(int(fl(a[1]))))
                elif op == "JMPABS":
                    jump = int(a[0])
                elif op == "JMPREL":
                    jump = line + int(fl(a[0]))
                elif op in JUMPS:
                    x = a[1]
                    y = a[2] if len(a) > 2 else 0.0
                    ok = {"JMPZ": x == 0.0, "JMPNZ": x != 0.0, "JMPE": x == y, "JMPNE": x != y, "JMPL": x < y,
                          "JMPNL": x >= y, "JMPG": x > y, "JMPNG": x <= y}[op]
                    if ok:
                        jump = int(a[0])
                elif op == "TLPause":
                    running = False
                elif op == "EndAttack":
                    ended = tick
                    emit("EndAttack")
                elif op == "SansText":
                    running = False
                    emit("SansText", int(a[0]))
                    talk = True          # (checking: said and read at once)
                elif op == "GetHeartPos":
                    vars_[int(a[0])], vars_[int(a[1])] = HEART_TEST
                    emit("GetHeartPos")
                elif op in ("BoneHRepeat", "BoneVRepeat", "PlatformRepeat"):
                    sx, sy, w, dr, sp, cnt, spc = a[0], a[1], a[2], a[3], a[4], int(fl(a[5])), int(fl(a[6]))
                    di = int(fl(dr)) & 3
                    for i in range(cnt):
                        x = sx - DX[di] * spc * i
                        y = sy - DY[di] * spc * i
                        nm = {"BoneHRepeat": "BoneH", "BoneVRepeat": "BoneV", "PlatformRepeat": "Platform"}[op]
                        tail = (0,)
                        emit(nm, int(fl(x)), int(fl(y)), int(fl(w)), int(fl(dr)), int(fl(sp)), *tail)
                elif op == "SineBones":
                    cnt, spc, sp, h = (int(fl(v)) for v in a[:4])
                    for i in range(cnt):
                        if spc > 0:
                            x, di = zone[2] + spc * i, 2
                        else:
                            x, di = zone[0] + spc * i, 0
                        sine = int(fl(sin_deg(i / 3.0 / D2R) * 28.0))
                        y = zone[1] + 6
                        emit("BoneV", int(fl(x)), int(fl(y)), h + sine, di, sp, 0)
                        y = zone[1] + 6 + h + sine + 39
                        emit("BoneV", int(fl(x)), int(fl(y)), int(fl(zone[3] - 5 - y)), di, sp, 0)
                elif op in ("BoneH", "BoneV"):
                    v = [int(fl(x)) for x in a[:6]] + [0] * (6 - len(a[:6]))
                    emit(op, *v)
                elif op == "Platform":
                    v = [int(fl(x)) for x in a[:5]]
                    emit(op, *v, 1 if len(a) > 5 and int(fl(a[5])) > 0 else 0)
                elif op == "BoneStab":
                    emit(op, int(fl(a[0])), int(fl(a[1])), ms(a[2]), ms(a[3]))
                elif op == "GasterBlaster":
                    emit(op, *[int(fl(x)) for x in a[:6]], ms(a[6]), ms(a[7]))
                elif op == "CombatZoneSpeed":
                    speed = int(fl(a[0]))
                    emit(op, speed)
                elif op == "CombatZoneResize":
                    target = [a[0], a[1], a[2], a[3]]
                    finish = a[4] != 0.0
                    emit(op, *[ms(x) for x in target], int(finish))
                elif op == "CombatZoneResizeInstant":
                    zone = [a[0], a[1], a[2], a[3]]
                    target = list(zone)
                    emit(op, *[ms(x) for x in zone])
                elif op in ("HeartMode", "HeartTeleport", "HeartMaxFallSpeed", "SansSlamDamage", "SansX",
                            "SansSweat", "BlackScreen", "SansAnimation", "SansBody", "SansTorso", "SansHead"):
                    emit(op, *[int(fl(x)) for x in a])
                elif op == "SansSlam":
                    emit(op, int(fl(a[0])))
                elif op in ("SansRepeat", "SansEndRepeat"):
                    emit(op)
                else:
                    die("can't play " + op)
                if jump is not None:
                    line = jump - 1
                t -= d
                line += 1
                n += 1
                if n >= 4000:
                    die("a loop that never waits")
            if running:
                t += 1
        # the zone
        step = speed / 30.0
        for i in range(4):
            dd = target[i] - zone[i]
            if dd > 0:
                zone[i] += min(step, dd)
            elif dd < 0:
                zone[i] -= min(step, -dd)
        if finish and zone == target:
            finish = False
            running = True
        if talk:
            talk = False
            running = True
        tick += 1
    return log, ended


def fnv(text):
    h = 2166136261
    for b in text.encode():
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def main():
    if len(sys.argv) < 2:
        die("attacks.py REF [SPEC]")
    ref = sys.argv[1]
    spec = sys.argv[2] if len(sys.argv) > 2 else None
    pool = Pool()
    pool.add(0.0)
    progs = []
    for name in ATTACKS:
        code, vars_ = compile_attack(load(ref, name), pool, name)
        progs.append((name, code, vars_))
    if len(pool.vals) >= 0x8000:
        die("too many numbers")

    # the tables
    words, lines, info = [], [], []
    for name, code, vars_ in progs:
        first = len(lines)
        for op, args in code:
            lines.append(len(words))
            words.append(OPS.index(op) | (len(args) - 1) << 8)
            for k, v in args:
                words.append(0x8000 | int(v) if k == "v" else pool.add(v))
        info.append((first, len(code), len(vars_)))
    lines.append(len(words))
    if len(words) >= 0x10000:
        die("too much code")
    src = os.path.join(GAME, "src")
    with open(os.path.join(src, "attacks.h"), "w") as f:
        f.write("/* Generated by tools/attacks.py from jcw87's attack scripts: don't edit. */\n")
        f.write("#ifndef BT_ATTACKS_H\n#define BT_ATTACKS_H\n#include <stdint.h>\n\n")
        f.write("enum {\n" + "".join(f"  OP_{o.upper()},\n" for o in OPS) + "  OP_COUNT\n};\n")
        f.write(f"#define N_ATTACKS {len(ATTACKS)}\n")
        f.write("enum {\n" + "".join(f"  A_{n[5:].upper()},\n" for n in ATTACKS) + "};\n")
        f.write("typedef struct {\n  uint16_t first, lines;   /* its lines in atk_line */\n"
                "  uint8_t vars;             /* its variables ($pi first) */\n} atk_info_t;\n")
        f.write("/* a line: op | args << 8, the delay, then the arguments; a word with its top bit set is a variable,\n"
                " * otherwise a number in atk_pool */\n")
        f.write("extern const double atk_pool[];\nextern const uint16_t atk_code[];\nextern const uint16_t atk_line[];\n")
        f.write("extern const atk_info_t atk_info[N_ATTACKS];\nextern const char *const atk_name[N_ATTACKS];\n")
        f.write("#endif\n")
    with open(os.path.join(src, "attacks.c"), "w") as f:
        f.write("/* Generated by tools/attacks.py from jcw87's attack scripts: don't edit. */\n#include \"attacks.h\"\n\n")
        f.write(f"const double atk_pool[{len(pool.vals)}] = {{\n")
        for i in range(0, len(pool.vals), 6):
            f.write("  " + ", ".join(repr(v) for v in pool.vals[i:i + 6]) + ",\n")
        f.write("};\n")
        f.write(f"const uint16_t atk_code[{len(words)}] = {{\n")
        for i in range(0, len(words), 14):
            f.write("  " + ", ".join(str(w) for w in words[i:i + 14]) + ",\n")
        f.write("};\n")
        f.write(f"const uint16_t atk_line[{len(lines)}] = {{\n")
        for i in range(0, len(lines), 14):
            f.write("  " + ", ".join(str(w) for w in lines[i:i + 14]) + ",\n")
        f.write("};\n")
        f.write("const atk_info_t atk_info[N_ATTACKS] = {\n")
        for a, (n, (first, cnt, nv)) in zip(ATTACKS, zip(ATTACKS, info)):
            f.write(f"  {{{first}, {cnt}, {nv}}},   /* {a} */\n")
        f.write("};\nconst char *const atk_name[N_ATTACKS] = {\n")
        names = ["Intro", "Bone gap", "Blue bones", "Bone gap 2", "Platforms", "Platforms 2", "Platforms 3",
                 "Platforms 4", "Platform blasters", "Platforms 4 hard", "Bone gap fast", "Bone slide",
                 "Platform blasters fast", "The break", "Multi 1", "Random blasters", "Multi 2", "Bone stab",
                 "Bone stab 2", "Random blasters 2", "Bone slide 2", "Multi 3", "Bone stab 3", "The final attack"]
        f.write("".join(f"  \"{n}\",\n" for n in names) + "};\n")

    # what the game's interpreter must do: per attack, its end, its count and hash, with seed 0x5EED0000 + n
    rows = []
    md = ["# The attacks, tick by tick\n",
          "Generated by games/badtime/tools/attacks.py from jcw87's scripts. One line per thing an attack makes or",
          "changes, at its tick (30 a second), in the original's 640 x 480 pixels: bones (x, y, length, direction 0",
          "right 1 down 2 left 3 up, speed px/s, colour 0 white 1 blue 2 orange), platforms (x, y, width, direction,",
          "speed, turns), bone stabs (side, depth, warning ms, stay ms), blasters (size, from x y, to x y, angle, wait",
          "ms, blast ms), the zone (left top right bottom, x1000), the heart and the opponent. Random choices with",
          "seed 0x5EED0000 + n; GetHeartPos says (320, 304).\n"]
    for n, (name, code, vars_) in enumerate(progs):
        log, end = run(code, len(vars_), 0x5EED0000 + n)
        if end is None:
            die(name + " never ends")
        text = "\n".join(log)
        rows.append((end, len(log), fnv(text)))
        md.append(f"## {n}. {name}: {end} ticks ({end / 30:.2f} s), {len(log)} events\n")
        md.append("```")
        md.extend(log)
        md.append("```\n")
    with open(os.path.join(GAME, "tests", "expect.h"), "w") as f:
        f.write("/* Generated by tools/attacks.py: each attack as a second reading of jcw87's scripts plays it (seed\n"
                " * 0x5EED0000 + n): the tick it ends, how many things it makes, the FNV-1a of their log. */\n")
        f.write("static const struct { uint32_t end, events, hash; } EXPECT[] = {\n")
        for (end, cnt, h), name in zip(rows, ATTACKS):
            f.write(f"  {{{end}, {cnt}, 0x{h:08X}u}},   /* {name} */\n")
        f.write("};\n")
    if spec:
        with open(os.path.join(spec, "attacks.md"), "w") as f:
            f.write("\n".join(md))
    print(f"{len(ATTACKS)} attacks, {len(lines) - 1} lines, {len(words) * 2} bytes of code, {len(pool.vals)} numbers")


if __name__ == "__main__":
    main()
