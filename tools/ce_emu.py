#!/usr/bin/env python3
"""Run a TI-84 Plus CE program without a calculator or a ROM: CEmu's core with a small OS of its own (tools/ce_emu).

  python3 tools/ce_emu.py PROG.8xp [VARS.8xv ...] [--keys ...] [--shots ...] [--ms N] [--out DIR]
  python3 tools/ce_emu.py game ID [same options]       a NumPlay game's CE build (games/ID/build/ti84ce)
  python3 tools/ce_emu.py sheet [ID ...] [--ms N] [-j N] [--out DIR]
                                                       every CE build (games.json "ports") for 40 s: OK twice,
                                                       the arrows, a sheet of screens (build/ce_emu/sheet.png)
  python3 tools/ce_emu.py autotest DIR/autotest.json ...
                                                       CE toolchain tests (CEmu's autotester JSON): their keys
                                                       and delays, their screens' CRCs checked

Options:
  --keys "1500-1600:2nd,3000:clear"   keys held from one time to another (ms; one time alone: 100 ms). CE names:
                                      2nd mode del alpha math enter clear up down left right 0-9 add sub mul div
                                      power square ln log sin cos tan comma lparen rparen dot chs vars stat prgm
                                      apps xton sto recip yequ window zoom trace graph on; or NumWorks names on
                                      NumPlay's layout: ok (2nd) exe (enter) back (clear) home (mode)...
  --shots "2000,4000:play.png"        the screen at these times (PNG)
  --ms N                              how long, in the calculator's time (5000)
  --ram NAME,...                      these variables in RAM, the others where their files say (the archive)
  --libs FILE|none                    the CE C libraries (default: clibs.8xg v15.0, downloaded once into
                                      ~/Documents/Assets/toolchains/ce-libs)
  --frame SYMBOL|ADDR                 a frame each time the CPU gets there (default: eadk_display_wait_for_vblank
                                      or eadk_keyboard_scan when the program has them, from its .obj next to it)
  --frames-from MS                    count frames (and profile) from then on (past a slow title)
  --profile N                         the N functions the CPU was found in most
  --vars-out DIR                      the appvars and programs at the end, as files
  --rev m|i|pre-a                     the hardware revision (m, every CE since 2019)
  --float-scale X                     TI's float routines' cost times X (their cycles are estimates: TI's boot
                                      code isn't public; the report says how much time went to them)
  --trace                             every OS call
  --json                              the full report

It stops at a crash (a write to flash or to the stack limit, the reset vector, unmapped memory, an OS call it
lacks), a hang, an OS error, or the program's end, and says which. Frame rates are the calculator's: 48 MHz,
the LCD's memory traffic and the flash's wait states included."""
import concurrent.futures as cf
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HERE = os.path.join(ROOT, "tools", "ce_emu")
EXE = os.path.join(HERE, "build", "ce_emu")
LIBS_DIR = os.path.expanduser("~/Documents/Assets/toolchains/ce-libs/v15.0")
LIBS_URL = "https://github.com/CE-Programming/libraries/releases/download/v15.0/clibs.8xg"
LIBS_SHA = "450bdefb41fe263f8d2d161b7cf431bbf99711f2ef78f02f796d530ed12dfbf0"
# a frame: NumPlay's per-frame call when it's there (exact), else its key scan once the screen changed
FRAME_SYMBOLS = (("_eadk_display_wait_for_vblank", False), ("_eadk_keyboard_scan", True))
IDLE_SYMBOLS = ("_ticksleep", "_msleep", "_delay")


def build():
    r = subprocess.run(["make", "-s", "-C", HERE], capture_output=True, text=True)
    if r.returncode:
        raise SystemExit("ce_emu: the build failed\n" + r.stdout + r.stderr)


def libs():
    path = os.path.join(LIBS_DIR, "clibs.8xg")
    if not os.path.exists(path):
        os.makedirs(LIBS_DIR, exist_ok=True)
        data = urllib.request.urlopen(LIBS_URL).read()
        if hashlib.sha256(data).hexdigest() != LIBS_SHA:
            raise SystemExit("ce_emu: clibs.8xg isn't the one released")
        open(path, "wb").write(data)
    return path


def symbols(prog):
    """The program's symbols, from the .obj (ELF) the CE toolchain leaves next to the .8xp."""
    obj = os.path.splitext(prog)[0] + ".obj"
    if not os.path.exists(obj):
        return {}
    try:
        from elftools.elf.elffile import ELFFile
    except ImportError:
        return {}
    out = {}
    with open(obj, "rb") as f:
        tab = ELFFile(f).get_section_by_name(".symtab")
        for s in tab.iter_symbols() if tab else []:
            if s.name and s["st_value"] >= 0xD00000 and not s.name.startswith(".L"):
                out[s.name] = s["st_value"]
    # the static functions, which only the map names (one section each: .text._name)
    mp = os.path.splitext(prog)[0] + ".map"
    if os.path.exists(mp):
        text = open(mp).read()
        for m in re.finditer(r"^ \.text\.(_[\w.$]+)\s+0x([0-9a-f]+)\s+0x[0-9a-f]+", text, re.M):
            out.setdefault(m.group(1), int(m.group(2), 16))
    return out


def profile_lines(samples, syms, n):
    import bisect
    import collections
    rom = {}
    for line in open(os.path.join(HERE, "build", "rom.map")):
        m = re.match(r"\s+0x([0-9a-f]{8})\s+(_\S+)\s*$", line)
        if m:
            rom["rom " + m.group(2)] = int(m.group(1), 16)
    table = sorted([(a, k) for k, a in syms.items()] + [(a, k) for k, a in rom.items()])
    addrs = [a for a, _ in table]
    hits = collections.Counter()
    total = 0
    for line in open(samples):
        a, k = line.split()
        a, k = int(a, 16), int(k)
        total += k
        i = bisect.bisect_right(addrs, a) - 1
        hits[table[i][1] if i >= 0 and a - addrs[i] < 0x2000 else f"?{a:06X}"] += k
    return [f"{100 * k / max(total, 1):5.1f}%  {name}" for name, k in hits.most_common(n)]


def run(files, keys="", shots="", ms=5000, out=".", ram="", lib=None, frame=None, rev="m", trace=False,
        vars_out=None, profile=0, float_scale=1.0, script=None, frames_from=0):
    """One run; its report (a dict)."""
    build()
    prog = next(f for f in files if f.lower().endswith(".8xp"))
    syms = symbols(prog)
    addr, drawn = None, False
    if frame:
        addr = int(frame, 0) if re.match(r"0x|\d", frame) else syms.get("_" + frame.lstrip("_"))
        if addr is None:
            raise SystemExit(f"ce_emu: no symbol {frame} in {os.path.splitext(prog)[0]}.obj")
    else:
        frame, drawn = next(((s, d) for s, d in FRAME_SYMBOLS if s in syms), (None, False))
        addr = syms.get(frame) if frame else None
    idle = next((syms[s] for s in IDLE_SYMBOLS if s in syms), None)
    with tempfile.TemporaryDirectory() as tmp:
        rep = os.path.join(tmp, "report.json")
        cmd = [EXE, "--rom", os.path.join(HERE, "build", "rom.bin"), "--names", os.path.join(HERE, "build", "ti84pceg.inc"),
               "--ms", str(ms), "--out", out, "--report", rep, "--rev", rev]
        cmd += ["--keys", keys] if keys else []
        cmd += ["--shots", shots] if shots else []
        cmd += ["--ram", ram] if ram else []
        cmd += ["--frame", hex(addr)] + (["--frame-drawn"] if drawn else []) if addr else []
        cmd += ["--idle", hex(idle)] if idle else []
        cmd += ["--frames-from", str(frames_from)] if frames_from else []
        cmd += ["--trace"] if trace else []
        cmd += ["--float-scale", str(float_scale)] if float_scale != 1 else []
        cmd += ["--vars-out", vars_out] if vars_out else []
        cmd += ["--profile", os.path.join(tmp, "profile")] if profile else []
        if script:
            open(os.path.join(tmp, "script"), "w").write(script)
            cmd += ["--script", os.path.join(tmp, "script")]
        cmd += files + ([lib] if lib else [])
        os.makedirs(out, exist_ok=True)
        r = subprocess.run(cmd, capture_output=True, text=True)
        if not os.path.exists(rep):
            raise SystemExit(f"ce_emu: {r.stderr.strip() or 'no report'}")
        report = json.load(open(rep))
        report["frame_symbol"] = (frame.lstrip("_") + (" once drawn" if drawn else "")) if frame and addr else None
        report["stderr"] = r.stderr
        if profile:
            report["profile"] = profile_lines(os.path.join(tmp, "profile"), syms, profile)
    return report


def summary(rep):
    s = f"{rep['program']}: {rep['status']}" + (f" ({rep['detail']})" if rep["detail"] else "")
    s += f"\n  {rep['ms'] / 1000:.2f} s in {rep['wall_s']:.2f} s ({rep['speed']:.1f}x)"
    if rep["frames"] > 1:
        how = rep["frame_symbol"] if rep["frame_kind"].startswith("marker") else "LCD buffer swaps"
        s += (f"\n  {rep['fps']:.1f} fps ({how}): {rep['frame_ms']:.1f} ms a frame, 95% under "
              f"{rep['frame_ms_p95']:.1f}, slowest {rep['frame_ms_max']:.1f}; {rep['work_ms_per_frame']:.1f} ms of work "
              f"a frame ({rep['cycles_per_frame'] * rep['work_ms_per_frame'] / max(rep['frame_ms'], 1e-9) / 1e6:.2f} M cycles)")
    if rep["float_ms"] >= 1:
        s += f"\n  {rep['float_ms'] / 1000:.2f} s ({100 * rep['float_ms'] / max(rep['ms'], 1):.0f}%) in TI's float routines (estimated)"
    s += f"\n  stack {rep['stack_bytes']} of 4096 bytes, {rep['free_ram']} bytes of RAM free, {rep['screen_changes']} screen changes"
    if rep["text"]:
        s += "\n  home screen: " + rep["text"].replace("\n", " / ")
    if rep["warnings"]:
        s += "\n  warnings: " + rep["warnings"].replace("\n", " / ")
    for line in rep.get("profile", []):
        s += "\n  " + line
    return s


CE_KEYS = {
    "graph": (1, 0), "trace": (1, 1), "zoom": (1, 2), "window": (1, 3), "yequ": (1, 4), "2nd": (1, 5), "mode": (1, 6),
    "del": (1, 7), "on": (2, 0), "sto": (2, 1), "ln": (2, 2), "log": (2, 3), "square": (2, 4), "recip": (2, 5),
    "math": (2, 6), "alpha": (2, 7), "0": (3, 0), "1": (3, 1), "4": (3, 2), "7": (3, 3), "comma": (3, 4), "sin": (3, 5),
    "apps": (3, 6), "xton": (3, 7), "dot": (4, 0), "2": (4, 1), "5": (4, 2), "8": (4, 3), "lparen": (4, 4),
    "cos": (4, 5), "prgm": (4, 6), "stat": (4, 7), "chs": (5, 0), "3": (5, 1), "6": (5, 2), "9": (5, 3),
    "rparen": (5, 4), "tan": (5, 5), "vars": (5, 6), "enter": (6, 0), "add": (6, 1), "sub": (6, 2), "mul": (6, 3),
    "div": (6, 4), "power": (6, 5), "clear": (6, 6), "down": (7, 0), "left": (7, 1), "right": (7, 2), "up": (7, 3)}
AUTOTEST_KEYS = {"y=": "yequ", "wind": "window", "^2": "square", "sq": "square", "-1": "recip", "inv": "recip",
                 ",": "comma", "xtn": "xton", ".": "dot", "(": "lparen", "lpar": "lparen", ")": "rparen",
                 "rpar": "rparen", "(-)": "chs", "neg": "chs", "+": "add", "-": "sub", "*": "mul", "/": "div",
                 "^": "power", "pow": "power", "clr": "clear"}
AUTOTEST_ADDR = {"vram_start": 0xD40000, "vram2_start": 0xD52C00, "vram_16_size": 153600, "vram_8_size": 76800}


def autotest(path, out, lib):
    """A CE toolchain test (CEmu's autotester JSON: its keys, delays and screen CRCs), run here; what matched."""
    cfg = json.load(open(path))
    d = os.path.dirname(os.path.abspath(path))
    files = [os.path.join(d, f) for f in cfg["transfer_files"]]
    hashes = cfg.get("hashes", {})
    num = lambda v: AUTOTEST_ADDR[v] if isinstance(v, str) and v in AUTOTEST_ADDR else int(str(v), 0)
    lines = []
    for step in cfg["sequence"]:
        op, _, arg = step.partition("|")
        if op == "delay":
            lines.append(f"delay {int(arg)}")
        elif op == "key":
            g, b = CE_KEYS[AUTOTEST_KEYS.get(arg, arg)]
            lines.append(f"key {g} {b}")
        elif op in ("hash", "hashWait"):
            h = hashes[arg]
            t = h.get("timeout", 1000 if op == "hashWait" else 0)
            crcs = ",".join(c for c in h["expected_CRCs"])
            lines.append(f"hash {arg} {num(h['start']):X} {num(h['size'])} {t} {crcs}")
    rep = run(files, ms=120000, out=out, lib=lib, script="\n".join(lines) + "\n")
    good = bad = 0
    print(f"{os.path.relpath(path)}: {rep['status']}" + (f" ({rep['detail']})" if rep["detail"] else ""))
    done = {h["id"]: h for h in rep["hashes"]}
    for k, h in hashes.items():
        r = done.get(k)
        what = h.get("description", k)
        if r is None:
            note = "not reached" + (" (the program had ended: TI-OS's home screen isn't drawn here)" if rep["exit_code"] >= 0 else "")
        elif r["match"]:
            note, good = "ok", good + 1
        elif r["exited"]:
            note = "skipped: after the program's end (TI-OS's home screen isn't drawn here)"
        elif rep["text"]:
            note = "skipped: TI-OS's text on screen (ce_emu writes it with NumPlay's font; see the home screen line)"
        else:
            note, bad = f"MISMATCH (got {r['crc']})", bad + 1
        print(f"  {k}: {what}: {note}")
    if rep["text"]:
        print("  home screen: " + rep["text"].replace("\n", " / "))
    return good, bad, rep


def game_files(g):
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import ports
    fs = [p for p, _ in ports.outputs("ti84ce", g)]
    if not all(os.path.exists(p) for p in fs):
        raise SystemExit(f"ce_emu: {g} isn't built for the CE (python3 tools/ports.py build ti84ce {g})")
    return fs


def sheet(todo, ms, out, jobs):
    """Every game for ms: its title (before OK), OK twice, then each arrow held 1.5 s; three screens each, side by
    side, and the frame rate from the second OK on."""
    from PIL import Image, ImageDraw
    t1, t2 = ms * 2 // 5, ms * 3 // 5
    keys = f"{t1}-{t1 + 100}:2nd,{t2}-{t2 + 100}:2nd," + ",".join(
        f"{t2 + 1000 + 2000 * i}-{t2 + 2500 + 2000 * i}:{k}" for i, k in enumerate(("right", "up", "left", "down")))
    times = [t1 - 50, t2 + 900, ms - 50]

    def one(g):
        d = os.path.join(out, g)
        try:
            return g, run(game_files(g), keys=keys, shots=",".join(f"{t}:{i}.png" for i, t in enumerate(times)), ms=ms,
                          out=d, lib=libs(), frames_from=t2)
        except SystemExit as e:
            return g, {"program": g, "status": "error", "detail": str(e)}

    build()
    with cf.ThreadPoolExecutor(jobs) as ex:
        results = list(ex.map(one, todo))
    w, h, cols = 160, 120, 4
    rows = (len(results) + cols - 1) // cols
    im = Image.new("RGB", (cols * (3 * w + 10), rows * (h + 26)), "white")
    dr = ImageDraw.Draw(im)
    lines = []
    for i, (g, rep) in enumerate(results):
        x, y = (i % cols) * (3 * w + 10), (i // cols) * (h + 26)
        for k in range(3):
            p = os.path.join(out, g, f"{k}.png")
            if os.path.exists(p):
                im.paste(Image.open(p).resize((w, h)), (x + k * w, y + 26))
        fps = f", {rep['fps']:.0f} fps" if rep.get("frames", 0) > 1 else ""
        bad = rep["status"] not in ("ok", "exited")
        dr.text((x + 2, y + 1), f"{g}: {rep['status']}{fps}", fill="red" if bad else "black")
        if bad:
            dr.text((x + 2, y + 12), rep["detail"][:80], fill="red")
        lines.append(summary(rep) if "ms" in rep else f"{g}: {rep['status']} ({rep['detail']})")
    path = os.path.join(out, "sheet.png")
    im.save(path)
    open(os.path.join(out, "summary.txt"), "w").write("\n".join(lines) + "\n")
    json.dump({g: r for g, r in results}, open(os.path.join(out, "reports.json"), "w"), indent=1)
    print("\n".join(lines))
    good = sum(r["status"] in ("ok", "exited") for _, r in results)
    print(f"{good}/{len(results)} run without a crash or hang; {path}")
    return 0 if good == len(results) else 1


def main():
    a = sys.argv[1:]
    if not a or a[0] in ("-h", "--help"):
        print(__doc__)
        return 1
    opts = {"keys": "", "shots": "", "ms": "5000", "out": ".", "ram": "", "libs": None, "frame": None, "rev": "m",
            "vars-out": None, "profile": "0", "j": "6", "float-scale": "1", "frames-from": "0"}
    flags = {"trace": False, "json": False}
    pos = []
    i = 0
    while i < len(a):
        k = a[i].lstrip("-")
        if a[i].startswith("-") and k in opts and i + 1 < len(a):
            opts[k] = a[i + 1]
            i += 2
        elif a[i].startswith("-") and k in flags:
            flags[k] = True
            i += 1
        else:
            pos.append(a[i])
            i += 1
    if pos and pos[0] == "autotest":
        lib = None if opts["libs"] == "none" else opts["libs"] or libs()
        good = bad = 0
        for p in pos[1:]:
            try:
                g, b, _ = autotest(p, opts["out"], lib)
            except SystemExit as e:
                print(f"{os.path.relpath(p)}: {e}")
                continue
            good, bad = good + g, bad + b
        print(f"{good} screens matched, {bad} didn't")
        return 1 if bad else 0
    if pos and pos[0] == "sheet":
        sys.path.insert(0, os.path.join(ROOT, "tools"))
        import ports
        todo = pos[1:] or ports.games([], "ti84ce")
        out = opts["out"] if opts["out"] != "." else os.path.join(ROOT, "build", "ce_emu")
        return sheet(todo, int(opts["ms"]) if opts["ms"] != "5000" else 40000, out, int(opts["j"]))
    if pos and pos[0] == "game":
        files = game_files(pos[1]) + pos[2:]
    else:
        files = pos
    if not any(f.lower().endswith(".8xp") for f in files):
        raise SystemExit("ce_emu: no .8xp to run")
    lib = None if opts["libs"] == "none" else opts["libs"] or libs()
    rep = run(files, keys=opts["keys"], shots=opts["shots"], ms=int(opts["ms"]), out=opts["out"], ram=opts["ram"],
              lib=lib, frame=opts["frame"], rev=opts["rev"], trace=flags["trace"], vars_out=opts["vars-out"], float_scale=float(opts["float-scale"]),
              frames_from=int(opts["frames-from"]),
              profile=int(opts["profile"]))
    if rep["stderr"]:
        sys.stderr.write(rep["stderr"])
    print(json.dumps({k: v for k, v in rep.items() if k != "stderr"}, indent=1) if flags["json"] else summary(rep))
    return 0 if rep["status"] in ("ok", "exited") else 1


if __name__ == "__main__":
    sys.exit(main())
