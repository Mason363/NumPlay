"""circloO's levels, from the game's web build, as a C table (src/levels.h).

    python3 -I tools/levels.py <the web build's circloO.js> src/levels.h

The 2014 web build keeps each level as a script ("c 270 30 30" is a circle...), read by its level creator
(obj_createlevel). This reads them the same way, with the same numbers, and writes what they make: the objects
with their places in pixels, the joints between them, and the texts. The calculator builds the physics from that.
"""
import json
import math
import re
import sys

CX = CY = 1500.0           # the room's middle (3000 x 3000)
RING0 = RING_STEP = 200.0  # obj_createlevel: y6 = 200, C6 = 200

# record types (src/level.h)
T_END, T_Y, T_C, T_I, T_IO, T_MC, T_TMC, T_B, T_T, T_RR, T_LINE, T_CHAIN, T_ROPE, T_DIST, T_FRIC, T_INFO = range(16)
NAMES = ["END", "Y", "C", "I", "IO", "MC", "TMC", "B", "T", "RR", "LINE", "CHAIN", "ROPE", "DIST", "FRIC", "INFO"]


def lengthdir(length, d):
    """GameMaker's lengthdir_x/y, with its snapping to whole numbers"""
    def snap(v):
        r = round(v)
        return r if abs(v - r) < 0.0001 else v
    return snap(length * math.cos(d * math.pi / 180)), snap(-(length * math.sin(d * math.pi / 180)))


def point_direction(x1, y1, x2, y2):
    dx, dy = x2 - x1, y2 - y1
    if dx == 0:
        return 270.0 if dy > 0 else (90.0 if dy < 0 else 0.0)
    a = 180.0 * math.atan2(dy, dx) / math.pi
    a = int(round(a * 1000000)) / 1000000.0
    return -a if a <= 0 else 360 - a


def is_number(s):
    if s == "":
        return False
    i = 1 if s[0] in "+-" else 0
    dot = False
    for ch in s[i:]:
        if ch == ".":
            if dot:
                return False
            dot = True
        elif not ch.isdigit():
            return False
    return i < len(s) or False


def valid_triangle(xs, ys):
    """polygon_is_box2d_valid (the game's script)"""
    n = len(xs)
    f = [point_direction(xs[i], ys[i], xs[(i + 1) % n], ys[(i + 1) % n]) for i in range(n)]
    total = 0
    for i in range(n):
        g = 360 - ((f[(i + 1) % n] - f[i] + 360) % 360)
        if g > 180:
            return False
        total += g
    return 359.99 <= total <= 360.01


def parse(text):
    """the level creator's script reader (_02 in the web build): a list of records"""
    out = []          # (type, [values]) ; values in pixels / degrees / as given
    inst = {}         # instance id -> (x, y, body index or None)
    nid = [0]
    bodies = [0]
    saved = {}
    S8 = T8 = U8 = V8 = 0.0
    X8 = 0
    Y8 = Z8 = -1
    chain_x = chain_y = None
    info = {}

    def new(x, y, body):
        nid[0] += 1
        b = None
        if body:
            b = bodies[0]
            bodies[0] += 1
        inst[nid[0]] = (x, y, b)
        return nid[0]

    def pos(a, d):
        if X8 == 0:
            return a + S8, d + T8
        lx, ly = lengthdir(RING0 + RING_STEP * (X8 - 1) - (d + V8), a + U8)
        return CX + lx + S8, CY + ly + T8

    F = text
    while True:
        A = F.find("\n")
        if A < 0:
            R, F = F, ""
        else:
            R, F = F[:A], F[A + 1:]
        R = R.rstrip("\r")
        sp = R.find(" ")
        W = R if sp < 0 else R[:sp]
        if W not in ("/", ""):
            R = R[sp + 1:] if sp >= 0 else R
            nums, strs = [], []
            while True:
                if R[:1] == "'":
                    R = R[1:]
                    q = R.find("'")
                    strs.append(R[:q] if q >= 0 else R)
                    cut = q + 1 if q >= 0 else 0
                    R = R[cut:]
                    if q < 0:
                        break
                else:
                    q = R.find(" ")
                    tok = R if q < 0 else R[:q]
                    if is_number(tok):
                        nums.append(float(tok))
                    if q < 0:
                        break
                    R = R[q + 1:]
            n = len(nums)
            exists = lambda i: i in inst
            if W == "l":
                if exists(Z8) and exists(Y8):
                    out.append((T_LINE, [inst[Z8][0], inst[Z8][1], inst[Y8][0], inst[Y8][1]]))
            elif W in ("chmake", "chmake_arc"):
                if chain_x:
                    pts = []
                    for x, y in zip(chain_x, chain_y):
                        pts += [x, y]
                    out.append((T_CHAIN, pts))
                chain_x = chain_y = None
            elif W in ("r", "d"):
                if exists(Z8) and exists(Y8) and inst[Z8][2] is not None and inst[Y8][2] is not None:
                    ax, ay, bx, by = inst[Z8][0], inst[Z8][1], inst[Y8][0], inst[Y8][1]
                    o = nums[:4] if n >= 4 else [0, 0, 0, 0]
                    ax, ay, bx, by = ax + o[0], ay + o[1], bx + o[2], by + o[3]
                    if W == "r":
                        L = math.hypot(bx - ax, by - ay)
                        if n in (1, 5):
                            L = nums[n - 1]
                        out.append((T_ROPE, [inst[Z8][2], inst[Y8][2], ax, ay, bx, by, L]))
                    else:
                        out.append((T_DIST, [inst[Z8][2], inst[Y8][2], ax, ay, bx, by, o[0], o[1], o[2], o[3]]))
            elif W == "f":
                if exists(Z8) and exists(Y8) and inst[Z8][2] is not None and inst[Y8][2] is not None:
                    out.append((T_FRIC, [inst[Z8][2], inst[Y8][2], nums[0] if n >= 1 else 10, nums[1] if n >= 2 else 0]))
            elif W in ("c", "y", "i", "io", "mc", "chp", "tmc", "t", "rr", "b"):
                if n >= 2:
                    b7, c7 = pos(nums[0], nums[1])
                    if W not in ("i", "y"):
                        Z8 = Y8
                    if W == "c":
                        r = nums[2] if n > 2 else 10
                        Y8 = new(b7, c7, True)
                        out.append((T_C, [b7, c7, r]))
                    elif W == "chp":
                        chain_x = (chain_x or []) + [b7]
                        chain_y = (chain_y or []) + [c7]
                    elif W == "mc":
                        r = nums[2] if n > 2 else 10
                        dens = nums[3] if n > 3 else 1
                        Y8 = new(b7, c7, True)
                        out.append((T_MC, [b7, c7, r, dens]))
                    elif W == "tmc" and n >= 6:
                        off = nums[6] if n >= 7 else 0
                        new(b7, c7, False)
                        out.append((T_TMC, [b7, c7, nums[2], nums[3], nums[4], nums[5], off]))
                    elif W == "b" and n >= 4:
                        Y8 = new(b7, c7, True)
                        out.append((T_B, [b7, c7, nums[2], nums[3]]))
                    elif W == "t" and n >= 6:
                        xs, ys = [], []
                        for k in range(3):
                            x, y = pos(nums[2 * k], nums[2 * k + 1])
                            xs.append(x)
                            ys.append(y)
                        degenerate = (xs[0] == xs[1] and ys[0] == ys[1]) or (xs[0] == xs[2] and ys[0] == ys[2]) or \
                            (xs[1] == xs[2] and ys[1] == ys[2])
                        if not degenerate and valid_triangle(xs, ys):
                            Y8 = new(b7, c7, True)
                            out.append((T_T, [b7, c7, xs[0], ys[0], xs[1], ys[1], xs[2], ys[2]]))
                        # (an invalid one is created, then destroyed: Y8 keeps pointing at it, gone)
                        else:
                            nid[0] += 1
                            Y8 = nid[0]
                    elif W == "rr" and n >= 4:
                        ang = nums[4] if n > 4 else 0
                        dens = nums[5] if n > 5 else 1
                        damp = nums[6] if n > 6 else -1
                        Y8 = new(b7, c7, True)
                        out.append((T_RR, [b7, c7, nums[2], nums[3], ang, dens, damp]))
                    elif W == "y":
                        new(b7, c7, False)
                        out.append((T_Y, [b7, c7]))
                    elif W == "i":
                        new(b7, c7, False)
                        out.append((T_I, [b7, c7]))
                    elif W == "io":
                        new(b7, c7, False)
                        out.append((T_IO, [b7, c7]))
            elif W == "<":
                if n >= 1 and Y8 in inst:
                    saved[int(nums[0])] = Y8
            elif W == ">":
                Z8 = Y8
                if n >= 1 and int(nums[0]) in saved:
                    Y8 = saved[int(nums[0])]
            elif W in ("+", "+*", "++", "++*"):
                a = nums[0] if n >= 1 else None
                b = nums[1] if n >= 2 else a
                if a is not None:
                    if W == "+":
                        S8, T8 = a, b
                    elif W == "+*":
                        U8, V8 = a, b
                    elif W == "++":
                        S8, T8 = S8 + a, T8 + b
                    else:
                        U8, V8 = U8 + a, V8 + b
            elif W == "=":
                S8 = T8 = U8 = V8 = 0
                X8 = 0
            elif W == "repeat":
                if n >= 1:
                    ua = F.find("\nend")
                    if ua >= 0:
                        body = F[:ua]
                        if nums[0] > 1:
                            F = (body + "\n") * math.ceil(nums[0] - 1) + F
            elif W == "*":
                if n >= 1:
                    X8 = nums[0]
            elif W == "info":
                if n >= 1 and len(strs) == 1 and 0 <= nums[0] <= 7:
                    info[int(nums[0])] = strs[0]
            # (infoHTML5: the same text in these levels; infoUnlocked*: not in the free levels)
        if A < 0:
            break
    for ring, s in sorted(info.items()):
        out.append((T_INFO, [ring, s]))
    return out


def extract(js):
    s = open(js, encoding="utf-8-sig").read()
    levels = {}
    for fn, tag in (("_Y1", "base"), ("_Z1", "hard")):
        i = s.find("function %s(" % fn)
        j = s.find("function ", i + 10)
        for m in re.finditer(r'case (\d+):return ("(?:[^"\\]|\\.)*")', s[i:j]):
            levels[(tag, int(m.group(1)))] = json.loads(m.group(2)).replace("\n\r", "\n")
    return levels


# keypad words for the two texts that name a computer's keys
TEXTS = {"Use escape to pause.": "Press Back to pause."}


def emit(levels, path):
    order = [("base", i) for i in range(1, 15)] + [("hard", i) for i in range(1, 7)]
    texts = []
    data = []
    offsets = []
    stats = []
    for key in order:
        recs = parse(levels[key])
        offsets.append(len(data))
        counts = {}
        for t, v in recs:
            counts[NAMES[t]] = counts.get(NAMES[t], 0) + 1
            if t == T_INFO:
                s = TEXTS.get(v[1], v[1])
                if s not in texts:
                    texts.append(s)
                data += [t, 2, int(v[0]), texts.index(s)]
                continue
            q = []
            if t in (T_ROPE, T_DIST, T_FRIC):
                q = [int(v[0]), int(v[1])] + [round(x * 8) for x in v[2:]] if t != T_FRIC else \
                    [int(v[0]), int(v[1]), round(v[2] * 100), round(v[3] * 100)]
            elif t == T_MC:
                q = [round(x * 8) for x in v[:3]] + [round(v[3] * 100)]
            elif t == T_TMC:
                q = [round(x * 8) for x in v[:3]] + [round(v[3] * 100), int(v[4]), int(v[5]), int(v[6])]
            elif t == T_RR:
                q = [round(x * 8) for x in v[:4]] + [round(v[4] * 100), round(v[5] * 100), round(v[6] * 100)]
            elif t == T_CHAIN:
                q = [len(v) // 2] + [round(x * 8) for x in v]
            else:
                q = [round(x * 8) for x in v]
            for x in q:
                assert -32768 <= x <= 32767, (key, t, v)
            data += [t, len(q)] + q
        data.append(T_END)
        stats.append((key, counts))
    with open(path, "w") as f:
        f.write("/* circloO's levels, made by tools/levels.py from the game's web build: do not edit.\n")
        f.write(" * Records: type, count, values (places and sizes in 1/8 pixel, densities in 1/100). */\n")
        f.write("#define NLEVELS_BASE 14\n#define NLEVELS_HARD 6\n")
        f.write("static const int16_t level_data[%d] = {\n" % len(data))
        for i in range(0, len(data), 16):
            f.write("  " + ",".join(str(x) for x in data[i:i + 16]) + ",\n")
        f.write("};\n")
        f.write("static const uint16_t level_start[%d] = {%s};\n" % (len(offsets), ",".join(map(str, offsets))))
        f.write("static const char *const level_texts[%d] = {\n" % len(texts))
        for s in texts:
            f.write("  %s,\n" % json.dumps(s))
        f.write("};\n")
    for key, c in stats:
        print(key, c)


if __name__ == "__main__":
    emit(extract(sys.argv[1]), sys.argv[2])
