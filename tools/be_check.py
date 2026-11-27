#!/usr/bin/env python3
"""Does a game work on a big-endian calculator (the Casio fx-CG50's SuperH)? Without one: the game built from its
Casio sources, data and flags for an ARM CPU twice, big- and little-endian, played in an emulator (Unicorn) with the
same keys on the same clock, and each frame compared. A frame that differs is a number read in the wrong byte order
(data read in place, two pixels written as one word...). A word read from an address it isn't aligned on stops the
game there, as it does the SuperH (the compiler checks each one: -fsanitize=alignment).

  python3 tools/be_check.py [ID ...] [--ms N] [--keys ...] [-j N] [--out DIR]

ID: games (default: every game with "casio-cg" in games.json "ports"). --keys as tools/ce_emu.py's ("1500-1600:ok",
NumWorks key names), --ms the game's time (20000). The first frame that differs is saved in DIR (build/be_check) as
<id>_big.png and <id>_little.png.

Needs newlib built both ways (tools/be_check/newlib.sh: ~/Documents/Assets/toolchains/armeb), the ARM GCC
(~/.numplay/env.sh) and `pip install unicorn`."""
import argparse
import array
import concurrent.futures as cf
import hashlib
import json
import os
import shlex
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HERE = os.path.join(ROOT, "tools", "be_check")
ARMEB = os.path.expanduser(os.environ.get("ARMEB", "~/Documents/Assets/toolchains/armeb"))
ARCH = ["-marm", "-mcpu=cortex-a15", "-mfpu=vfpv4", "-mfloat-abi=hard", "-mno-unaligned-access"]
KEYS = {"left": 0, "up": 1, "down": 2, "right": 3, "ok": 4, "back": 5, "home": 6, "shift": 12, "alpha": 13, "xnt": 14,
        "var": 15, "toolbox": 16, "backspace": 17, "exe": 52, "zero": 48, "one": 42, "two": 43, "three": 44, "four": 36,
        "five": 37, "six": 38, "seven": 30, "eight": 31, "nine": 32, "plus": 45, "minus": 46}
# OK a few times (past a title, a menu, a first screen), the arrows, OK again
DEFAULT_KEYS = ("1500-1600:ok,3000-3100:ok,4500-4600:ok,6000-6100:exe,7000-8000:right,8000-9000:left,9000-10000:up,"
                "10000-11000:down,11500-11600:ok,13000-14500:right,15000-15100:ok,16000-18000:up")
GAP = 16  # (frames compared once the clock has gone past the last key, to see the game run on its own)


def tool(name):
    return os.path.join(os.path.dirname(subprocess.run(["bash", "-c", "source ~/.numplay/env.sh >/dev/null 2>&1; "
                                                        "command -v arm-none-eabi-gcc"], capture_output=True,
                                                       text=True).stdout.strip()), name)


def port_vars(game_dir, target):
    r = subprocess.run(["make", "-s", "-f", "Makefile", "-f", os.path.join(HERE, "vars.mk"), f"TARGET={target}",
                        "be-vars"], cwd=game_dir, capture_output=True, text=True)
    v = {}
    for line in r.stdout.splitlines():
        if "=" in line:
            k, _, val = line.partition("=")
            v[k] = val.strip()
    return v


def build(gid, endian, out):
    """the game built for ARM, big- or little-endian: its ELF, or an error"""
    gdir = os.path.join(ROOT, "games", gid)
    v = port_vars(gdir, "casio-cg" if endian == "big" else "nspire")
    if not v.get("SRC"):
        return None, "no port variables"
    for dep in v["DEPS"].split():
        r = subprocess.run(["make", "-s", f"TARGET={'casio-cg' if endian == 'big' else 'nspire'}", dep], cwd=gdir,
                           capture_output=True, text=True)
        if r.returncode:
            return None, f"{dep}: {r.stderr[-400:]}"
    lib = os.path.join(ARMEB, f"newlib-{endian}", "arm-none-eabi")
    flags = [f for f in shlex.split(v["FLAGS"]) if not f.startswith("-m")]
    common = ARCH + [f"-m{endian}-endian", "-Os", "-ffunction-sections", "-fdata-sections", "-fno-common",
                     "-fsanitize=alignment", "-fsanitize-trap=alignment", "-isystem", os.path.join(lib, "include"),
                     "-DNP_PORT=1", "-DNP_SOFT_FLOAT=1", f"-DNP_APP_ID=\"{v['ID']}\"", f"-DNP_APP_NAME=\"{v['NAME']}\"",
                     "-I" + os.path.join(ROOT, "platform", "include"), "-w"] + flags
    odir = os.path.join(os.path.abspath(out), gid, endian)
    os.makedirs(odir, exist_ok=True)
    gcc, gxx = tool("arm-none-eabi-gcc"), tool("arm-none-eabi-g++")
    units = [(f, gcc, ["-std=gnu11"]) for f in v["SRC"].split() + v["COMMON"].split() + v["ASM"].split()]
    units.append((os.path.join(HERE, "np_be.c"), gcc, ["-std=gnu11"]))
    if v["CXX"]:
        cxx = ["-std=gnu++11", "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics"] + shlex.split(v["CXXFLAGS"])
        units += [(f, gxx, cxx) for f in v["CXX"].split()] + [(os.path.join(HERE, "np_be_cxx.cpp"), gxx, cxx)]
    objs = []
    for i, (f, cc, extra) in enumerate(units):
        o = os.path.join(odir, f"u{i}.o")
        r = subprocess.run([cc] + extra + common + ["-c", f, "-o", o], cwd=gdir, capture_output=True, text=True)
        if r.returncode:
            return None, f"{f}: {r.stderr[-600:]}"
        objs.append(o)
    helpers = os.path.join(odir, "helpers.o")
    r = subprocess.run([gcc, "-std=gnu11", "-O2"] + ARCH + [f"-m{endian}-endian", "-ffreestanding", "-c",
                        os.path.join(HERE, "helpers.c"), "-o", helpers], capture_output=True, text=True)
    if r.returncode:
        return None, r.stderr[-600:]
    elf = os.path.join(odir, "game.elf")
    r = subprocess.run([tool("arm-none-eabi-ld"), "-EB" if endian == "big" else "-EL", "-T",
                        os.path.join(HERE, "link.ld"), "--gc-sections", "-u", "np_be_start", "--no-warn-rwx-segments",
                        "--no-warn-execstack"] + objs + [helpers, "-L", os.path.join(lib, "lib"), "--start-group",
                        "-lc", "-lm", "--end-group", "-o", elf], capture_output=True, text=True)
    if r.returncode:
        return None, r.stderr[-1200:]
    return elf, None


def symbols(elf):
    out = subprocess.run([tool("arm-none-eabi-nm"), elf], capture_output=True, text=True).stdout
    s = {}
    for line in out.splitlines():
        p = line.split()
        if len(p) == 3:
            s[p[2]] = int(p[0], 16)
    return s


def name_at(syms, addr):
    best = max(((a, n) for n, a in syms.items() if a <= addr and not n.startswith("$")), default=(0, "?"))
    return f"{best[1]}+{addr - best[0]:#x}"


def parse_keys(text):
    out = []
    for part in filter(None, text.split(",")):
        t, _, k = part.partition(":")
        a, _, b = t.partition("-")
        out.append((int(a), int(b) if b else int(a) + 100, 1 << KEYS[k]))
    return out


def play(elf, endian, keys, ms, expect=None, stop_at=None):
    """each frame's time and picture's hash (RGB565 as the little-endian CPU has it), and how the game stopped. With
    expect (another run's hashes), it stops at the first frame that differs; with stop_at, at that frame. The picture
    it stopped at comes back too."""
    from unicorn import Uc, UcError, UC_ARCH_ARM, UC_MODE_ARM, UC_MODE_BIG_ENDIAN, UC_MODE_LITTLE_ENDIAN
    from unicorn import UC_HOOK_CODE
    from unicorn import arm_const as A
    syms = symbols(elf)
    binf = elf[:-4] + ".bin"
    subprocess.run([tool("arm-none-eabi-objcopy"), "-O", "binary", elf, binf], check=True)
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM | (UC_MODE_BIG_ENDIAN if endian == "big" else UC_MODE_LITTLE_ENDIAN))
    uc.ctl_set_cpu_model(A.UC_CPU_ARM_MAX)
    uc.mem_map(0, 64 << 20)
    uc.mem_write(0x10000, open(binf, "rb").read())
    uc.reg_write(A.UC_ARM_REG_C1_C0_2, uc.reg_read(A.UC_ARM_REG_C1_C0_2) | 0xF << 20)
    uc.reg_write(A.UC_ARM_REG_FPEXC, 0x40000000)
    bo = ">" if endian == "big" else "<"
    uc.mem_write(syms["np_be_script"], b"".join(struct.pack(bo + "IIQ", a, b, k) for a, b, k in keys[:63]))
    frames, state = [], {"end": None, "px": None}
    fb, us = syms["np_be_fb"], syms["np_be_us"]

    def at_frame(u, addr, size, ud):
        t = struct.unpack(bo + "Q", u.mem_read(us, 8))[0] // 1000
        px = array.array("H", bytes(u.mem_read(fb, 320 * 240 * 2)))
        if endian == "big":
            px.byteswap()
        h = hashlib.md5(px.tobytes()).hexdigest()
        i = len(frames)
        frames.append((t, h))
        if expect is not None and (i >= len(expect) or expect[i][1] != h):
            state["end"], state["px"] = f"differs at frame {i}", px
            u.emu_stop()
        elif stop_at == i:
            state["end"], state["px"] = "frame", px
            u.emu_stop()
        elif t >= ms:
            state["end"] = "time"
            u.emu_stop()

    def at_exit(u, addr, size, ud):
        state["end"] = f"exit {u.reg_read(A.UC_ARM_REG_R0)}"
        u.emu_stop()
    uc.hook_add(UC_HOOK_CODE, at_frame, begin=syms["np_be_frame"], end=syms["np_be_frame"])
    uc.hook_add(UC_HOOK_CODE, at_exit, begin=syms["np_be_exit"], end=syms["np_be_exit"])
    uc.reg_write(A.UC_ARM_REG_SP, (64 << 20) - 64)
    try:
        uc.emu_start(syms["np_be_start"], 0, timeout=600 * 1000 * 1000)
        if state["end"] is None:
            state["end"] = "too slow (10 min)"
    except UcError as e:
        pc = uc.reg_read(A.UC_ARM_REG_PC)
        word = bytes(uc.mem_read(pc, 4))
        trap = (word[::-1] if endian == "little" else word) == bytes.fromhex("e7ffdefe")   # (udf: a misaligned word)
        state["end"] = f"{'misaligned word' if trap else e} at {name_at(syms, pc)}"
        px = array.array("H", bytes(uc.mem_read(fb, 320 * 240 * 2)))
        if endian == "big":
            px.byteswap()
        state["px"] = px
    return frames, state["end"], state["px"]


def save_png(px, path):
    from PIL import Image
    im = Image.new("RGB", (320, 240))
    im.putdata([((c >> 11) << 3, (c >> 5 & 63) << 2, (c & 31) << 3) for c in px])
    im.save(path)


def check(gid, keys, ms, out):
    elfs = {}
    for e in ("little", "big"):
        elfs[e], err = build(gid, e, out)
        if err:
            return gid, f"build ({e}): {err.strip().splitlines()[-1] if err.strip() else err}"
    lf, lend, _ = play(elfs["little"], "little", keys, ms)
    if not lend.startswith(("time", "exit")):
        return gid, f"little-endian too: {lend} after {len(lf)} frames"
    bf, bend, bpx = play(elfs["big"], "big", keys, ms, expect=lf)
    if bend.startswith("differs") or bpx is not None:
        i = len(bf) - 1
        _, _, lpx = play(elfs["little"], "little", keys, ms, stop_at=i)
        save_png(bpx, os.path.join(out, f"{gid}_big.png"))
        if lpx is not None:
            save_png(lpx, os.path.join(out, f"{gid}_little.png"))
        at = f"frame {i} ({bf[i][0]} ms)" if bf else "the start"
        return gid, f"{'DIFFERS' if bend.startswith('differs') else 'STOPS: ' + bend} at {at}"
    if bend != lend or len(bf) != len(lf):
        return gid, f"big: {bend} after {len(bf)} frames; little: {lend} after {len(lf)} frames"
    return gid, f"ok: {len(lf)} frames the same ({lend})"


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("ids", nargs="*")
    ap.add_argument("--ms", type=int, default=20000)
    ap.add_argument("--keys", default=DEFAULT_KEYS)
    ap.add_argument("-j", type=int, default=4)
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "be_check"))
    a = ap.parse_args()
    games = json.load(open(os.path.join(ROOT, "games", "games.json")))
    games = games if isinstance(games, list) else games.get("games", games)
    ids = a.ids or [g["id"] for g in games if "casio-cg" in g.get("ports", [])]
    os.makedirs(a.out, exist_ok=True)
    keys, bad = parse_keys(a.keys), 0
    with cf.ProcessPoolExecutor(a.j) as ex:
        for gid, verdict in ex.map(check, ids, [keys] * len(ids), [a.ms] * len(ids), [a.out] * len(ids)):
            bad += not verdict.startswith("ok")
            print(f"{gid:16} {verdict}", flush=True)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
