#!/usr/bin/env python3
"""A game's TI-84 CE build, checked (platform/ti84ce/README.md). Run by `make port TARGET=ti84ce` on its folder:

  python3 platform/ti84ce/check.py build/ti84ce/<game>

prints the .8xp's size, the RAM it takes (the program, its static data) and how deep its stack goes, and fails when
one of them is past the calculator's room. Also, in a game's folder:

  python3 ../../platform/ti84ce/check.py --ints build/ti84ce/<game>

lists what loses bits only with the CE's 24-bit int: the sources compiled for the CE and for a 32-bit calculator,
their implicit conversions compared, then again with the casts to int taken out (each one to look at)."""
import glob
import os
import re
import shlex
import shutil
import subprocess
import sys

STATIC_ROOM = 0xD13FD8 - 0xD052C6  # BSSHEAP_HIGH - BSSHEAP_LOW: static data and malloc's heap
STACK_ROOM = 4096                  # 0xD1987E up to 0xD1A87E
RAM_ROOM = 150000                  # a calculator's free RAM at best: the program as stored, then unpacked
VAR_ROOM = 65500                   # a variable's bytes


def sections(map_text):
    sec = {}
    for name, size in re.findall(r"^\.(\w+)\s+0x[0-9a-f]+\s+0x([0-9a-f]+)", map_text, re.M):
        sec[name] = int(size, 16)
    return sec


def stack(asm):
    """The deepest call chain from main: each function's frame (__frameset's size, its return address and ix),
    the arguments it pushes before a call; a call through a pointer may reach any function whose address is
    taken. Functions that call themselves count once (their depth is the game's to bound)."""
    funcs, cur, taken = {}, None, set()
    for line in asm.split("\n"):
        m = re.match(r"^(_[\w.$]+):", line)
        if m:
            cur = m.group(1)
            funcs[cur] = dict(frame=None, calls={}, pushes=0, maxpush=0, indirect=False, n=0, pending=0)
            continue
        if cur is None:
            continue
        f, t = funcs[cur], line.strip()
        if not t or t.startswith(";"):
            continue
        if t.startswith("."):
            if t.startswith((".d24", ".long")):
                taken.update(re.findall(r"(_[A-Za-z]\w*)", t))
            continue
        f["n"] += 1
        m = re.match(r"ld\s+hl,\s*(-?\d+)$", t)
        if f["frame"] is None and f["n"] <= 2 and m:
            f["pending"] = int(m.group(1))
            continue
        if f["frame"] is None:
            f["frame"] = 6 if t == "call\t__frameset0" else 6 - f["pending"] if t == "call\t__frameset" else 3
            if t.startswith("call\t__frameset"):
                continue
        if t.startswith("push"):
            f["pushes"] += 1
            f["maxpush"] = max(f["maxpush"], f["pushes"])
        elif t.startswith("pop"):
            f["pushes"] = max(0, f["pushes"] - 1)
        m = re.match(r"(?:call|jp)\s+(?:[a-z]+,\s*)?(_[\w.$]+)$", t)
        if m and not m.group(1).startswith("_.L"):
            if m.group(1).startswith("__indcall"):
                f["indirect"] = True
            f["calls"][m.group(1)] = max(f["calls"].get(m.group(1), 0), 3 * f["pushes"])
        taken.update(re.findall(r"^(?:ld|push)\s+\w+,\s*(_[A-Za-z]\w*)$", t))
    for f in funcs.values():
        f["frame"] = f["frame"] or 3
    taken &= set(funcs)
    memo, path, loops = {}, [], set()

    def depth(fn):
        if fn in memo:
            return memo[fn]
        if fn in path:
            loops.add(fn)
            return 0, []
        f = funcs.get(fn)
        if f is None:  # a routine of the C library
            return 40, [fn]
        path.append(fn)
        calls = dict(f["calls"])
        if f["indirect"]:
            for t in taken:
                calls.setdefault(t, 3 * f["maxpush"])
        best = max(((p + depth(c)[0], depth(c)[1]) for c, p in calls.items()), default=(3 * f["maxpush"], []))
        path.pop()
        memo[fn] = (f["frame"] + best[0], [fn] + best[1])
        return memo[fn]

    if "_main" not in funcs:
        return 0, [], set()
    d, chain = depth("_main")
    return d, chain, {l for l in loops if l != "_main"}


def check(ce_dir):
    maps, progs = glob.glob(os.path.join(ce_dir, "bin", "*.map")), glob.glob(os.path.join(ce_dir, "bin", "*.8xp"))
    if not maps or not progs:
        sys.exit(f"{ce_dir}: no .8xp built")
    sec = sections(open(maps[0]).read())
    prog = sum(sec.get(k, 0) for k in ("header", "init", "text", "rodata", "init_array", "fini_array", "data"))
    stored, bss = os.path.getsize(progs[0]), sec.get("bss", 0)
    lto = os.path.join(ce_dir, "obj", "lto.s")
    deep, chain, loops = stack(open(lto).read()) if os.path.exists(lto) else (0, [], set())
    name = os.path.basename(progs[0])
    print(f"CE      {name}: {stored} bytes; RAM: the program {prog}, its static data {bss} of {STATIC_ROOM}, "
          f"a stack of {deep} of {STACK_ROOM}" + (f" (and more: {', '.join(sorted(l[1:] for l in loops))} call "
                                                    f"themselves)" if loops else ""))
    bad = []
    if stored > VAR_ROOM:
        bad.append(f"{name} is past a variable's {VAR_ROOM} bytes")
    if stored + prog > RAM_ROOM:
        bad.append(f"stored and unpacked, it takes {stored + prog} bytes of the calculator's {RAM_ROOM}")
    if deep > STACK_ROOM - 512:
        bad.append(f"its stack may reach {deep} bytes: " + " > ".join(c.lstrip("_") for c in chain[:12]))
    for b in bad:
        print("CE      too big for a TI-84 CE: " + b)
    return not bad


def ints(ce_dir):
    """Implicit conversions that drop bits on the CE only, then the casts to int to look at."""
    mk = open(os.path.join(ce_dir, "makefile")).read()
    flags = [f for f in shlex.split(re.search(r"^CFLAGS = (.*)$", mk, re.M).group(1)) if f.startswith(("-D", "-I"))]
    srcs = [re.search(r'#include "(.*)"', open(f).read()).group(1) for f in sorted(glob.glob(os.path.join(ce_dir, "src", "*.c")))]
    srcs = [s for s in srcs if "/platform/" not in s and "/launcher/src/font_data.c" not in s]
    cedev = os.environ.get("CEDEV", os.path.expanduser("~/Documents/Assets/toolchains/CEdev"))
    ez80 = [os.path.join(cedev, "bin", "ez80-clang"), "-fsyntax-only", "-nostdinc", "-isystem", cedev + "/include",
            "-D__TICE__=1", "-std=gnu11"]
    gcc = shutil.which("arm-none-eabi-gcc")
    if not gcc:
        sys.exit("--ints needs arm-none-eabi-gcc on PATH (its C library's headers)")
    arm = ["clang", "--target=armv7em-none-eabi", "-fsyntax-only", "-std=gnu11", "-isystem",
           os.path.join(os.path.dirname(gcc), "..", "arm-none-eabi", "include")]
    warn = ["-Wimplicit-int-conversion", "-Wshorten-64-to-32", "-Wshift-sign-overflow", "-Wshift-count-overflow",
            "-Wshift-overflow", "-Wconstant-conversion", "-Wtautological-constant-out-of-range-compare", "-Wformat",
            "-Winteger-overflow", "-Wno-unused", "-ferror-limit=0"]

    def warnings(cmd, path):
        r = subprocess.run(cmd + flags + warn + [path], capture_output=True, text=True)
        return {(os.path.realpath(m[0]), int(m[1]), int(m[2]), m[4]): m[3] for m in
                re.findall(r"^(.*?):(\d+):(\d+): warning: (.*?) \[(-W[\w-]+)", r.stderr, re.M)}

    cast = re.compile(r"\((?:int|unsigned|unsigned int|signed|short|unsigned short)\)")
    for path in srcs:
        lines = open(path).read().split("\n")
        ce, a32 = warnings(ez80, path), warnings(arm, path)
        for (f, line, col, flag), msg in sorted(ce.items()):
            if (f, line, col, flag) not in a32:
                txt = open(f, errors="replace").read().split("\n")[line - 1].strip()
                print(f"{os.path.relpath(f)}:{line}:{col}: {msg} [{flag}]\n    {txt[:150]}")
        # the casts: taken out (same columns), the CE's warnings where a 32- or 64-bit value becomes an int
        tmp = path + ".ce-ints.c"
        open(tmp, "w").write(cast.sub(lambda m: " " * len(m.group(0)), "\n".join(lines)))
        try:
            got = warnings(ez80, tmp)
        finally:
            os.remove(tmp)
        for (f, line, col, flag), msg in sorted(got.items()):
            m = re.search(r"precision: '[^']*'(?: \(aka '([^']*)'\))? to '([^']*)'", msg)
            if f.endswith(".ce-ints.c") and m and "long" in (m.group(1) or msg) and m.group(2) in ("int", "unsigned int") \
                    and cast.search(lines[line - 1]):
                print(f"{os.path.relpath(path)}:{line}:{col}: a cast to int of a {m.group(1)} (look at its range)\n"
                      f"    {lines[line - 1].strip()[:150]}")


if __name__ == "__main__":
    a = sys.argv[1:]
    if a and a[0] == "--ints":
        ints(a[1])
    elif len(a) == 1:
        sys.exit(0 if check(a[0]) else 1)
    else:
        print(__doc__)
        sys.exit(1)
