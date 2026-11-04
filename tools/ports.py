#!/usr/bin/env python3
"""Every game on NumPlay's other targets (platform/README.md).

  python3 tools/ports.py probe [game...]     what each game's calculator build compiles, as PORT_* lines
  python3 tools/ports.py build TARGET [game...] [-j N]
                                             `make port TARGET=...` in each game's folder, a line each
  python3 tools/ports.py run [game...]       the computer build (sdl) run headless for a few seconds, its
                                             screen saved to build/ports/shots/<game>.png

`probe` reads the calculator build's own commands (make -Bn build), so a game's Makefile stays the one
list of its sources."""
import concurrent.futures as cf
import json
import os
import re
import shlex
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAMES = os.path.join(ROOT, "games")
NWLINK = os.environ.get("NWLINK", "npx --yes -- nwlink@1.0.0")
SKIP = {"common", "numdance", "tetris"}  # tetris: Rust, for the calculator's ARM only


def games(args):
    if args:
        return args
    ids = [g["id"] for g in json.load(open(os.path.join(GAMES, "games.json")))]
    return [g for g in ids if g not in SKIP and os.path.exists(os.path.join(GAMES, g, "Makefile"))]


def probe(g):
    """The C and assembly files and the -D/-I flags of the game's calculator build."""
    out = os.path.join(ROOT, "build", "data", "games", g)
    r = subprocess.run(["make", "-C", os.path.join(GAMES, g), "-Bn", "build", f"BUILD_DIR={out}", f"OUT={out}",
                        f"NWLINK={NWLINK}", "Q="], capture_output=True, text=True)
    src, asm, flags = [], [], []
    for line in r.stdout.splitlines():
        line = line.strip()
        if not re.match(r"(cd \S+ && )?arm-none-eabi-(gcc|g\+\+)\b", line):
            continue
        try:
            words = shlex.split(line)
        except ValueError:
            continue
        for w in words:
            if w.endswith(".c") and "launcher/src/" not in w or w.endswith(("launcher/src/gfx.c", "launcher/src/xfont.c",
                                                                             "launcher/src/inflate.c")):
                if w not in src:
                    src.append(w)
            elif w.endswith((".S", ".s")) and w not in asm:
                asm.append(w)
            elif (w.startswith("-D") and not w.startswith("-DPLATFORM_DEVICE")) or \
                    (w.startswith("-I") and "nwlink" not in w and "eadk" not in w):
                if w not in flags:
                    flags.append(w)
    return src, asm, flags


def port_lines(g):
    src, asm, flags = probe(g)
    return src, asm, flags


def build(target, g):
    r = subprocess.run(["make", "-C", os.path.join(GAMES, g), "port", f"TARGET={target}", f"NWLINK={NWLINK}"],
                       capture_output=True, text=True)
    err = [l for l in (r.stdout + r.stderr).splitlines() if "error" in l.lower()]
    return r.returncode == 0, err[:6]


def run(g, keys="1500-1600:ok,3500-3600:ok", quit_ms=7000):
    """The computer build, headless: OK twice, the screen at 1.2 s and 6 s, then Home. Its result: how it ended
    (ok: it saved and quit when told), and the screens as PNG in build/ports/shots."""
    import tempfile
    from PIL import Image
    exe = os.path.join(GAMES, g, "build", "sdl", g)
    if not os.path.exists(exe):
        return "not built", []
    shots = os.path.join(ROOT, "build", "ports", "shots")
    os.makedirs(shots, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        env = dict(os.environ, NP_HEADLESS="1", NP_SAVES=tmp, NP_KEYS=keys, NP_QUIT_MS=str(quit_ms),
                   NP_SHOTS=f"1200:{tmp}/a.ppm,{quit_ms - 1000}:{tmp}/b.ppm")
        try:
            r = subprocess.run([exe], cwd=os.path.join(GAMES, g), env=env, capture_output=True, timeout=quit_ms / 1000 + 15)
            how = "ok" if r.returncode == 0 else f"exit {r.returncode}"
        except subprocess.TimeoutExpired:
            how = "didn't quit"
        out = []
        for n in "ab":
            f = os.path.join(tmp, n + ".ppm")
            if os.path.exists(f):
                im = Image.open(f).convert("RGB")
                png = os.path.join(shots, f"{g}_{n}.png")
                im.save(png)
                out.append((png, len(set(im.resize((80, 60)).getdata())) > 1))
        if not out:
            how += ", no screen"
        elif not any(v for _, v in out):
            how += ", blank screen"
        return how, [p for p, _ in out]


def sheet(results, path):
    """All the games' screens side by side (2 per game), named."""
    from PIL import Image, ImageDraw
    w, h = 160, 120
    cols = 6
    rows = (len(results) + cols - 1) // cols
    im = Image.new("RGB", (cols * (2 * w + 8), rows * (h + 14)), "white")
    d = ImageDraw.Draw(im)
    for i, (g, how, pngs) in enumerate(results):
        x, y = (i % cols) * (2 * w + 8), (i // cols) * (h + 14)
        for k, p in enumerate(pngs[:2]):
            im.paste(Image.open(p).resize((w, h)), (x + k * w, y + 14))
        d.text((x + 2, y + 1), f"{g}: {how}", fill="black" if how == "ok" else "red")
    im.save(path)


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        return 1
    cmd = a.pop(0)
    jobs = 6
    if "-j" in a:
        i = a.index("-j")
        jobs = int(a[i + 1])
        del a[i:i + 2]
    if cmd == "probe":
        for g in games(a):
            src, asm, flags = probe(g)
            print(f"{g}\n  PORT_SRC = {' '.join(src)}\n  PORT_ASM = {' '.join(asm)}\n  PORT_FLAGS = {' '.join(flags)}")
    elif cmd == "build":
        target = a.pop(0)
        todo = games(a)
        ok = 0
        with cf.ThreadPoolExecutor(jobs) as ex:
            for g, (good, err) in zip(todo, ex.map(lambda g: build(target, g), todo)):
                ok += good
                print(f"{'ok  ' if good else 'FAIL'} {g}" + ("" if good else "\n      " + "\n      ".join(err)))
        print(f"{ok}/{len(todo)} build for {target}")
        return 0 if ok == len(todo) else 1
    elif cmd == "run":
        todo = games(a)
        with cf.ThreadPoolExecutor(jobs) as ex:
            results = [(g, *r) for g, r in zip(todo, ex.map(run, todo))]
        for g, how, _ in results:
            print(f"{'ok  ' if how == 'ok' else 'BAD '} {g}: {how}")
        sheet(results, os.path.join(ROOT, "build", "ports", "shots.png"))
        print(f"{sum(h == 'ok' for _, h, _ in results)}/{len(results)} run; build/ports/shots.png")
    else:
        print(__doc__)
        return 1


if __name__ == "__main__":
    sys.exit(main())
