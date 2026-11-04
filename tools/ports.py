#!/usr/bin/env python3
"""Every game on NumPlay's other targets (platform/README.md).

  python3 tools/ports.py probe [game...]     what each game's calculator build compiles, as PORT_* lines
  python3 tools/ports.py build TARGET [game...] [-j N]
                                             `make port TARGET=...` in each game's folder, a line each
  python3 tools/ports.py run [game...]       the computer build (sdl) run headless for a few seconds, its
                                             screen saved to build/ports/shots/<game>.png; fails if one doesn't run
  python3 tools/ports.py list TARGET         the games ported to TARGET
  python3 tools/ports.py pack TARGET out.zip [game...]
                                             the built files in a zip, with a README.txt on installing them
  python3 tools/ports.py kit DIR             DIR/ports.json for NumPlay Studio, from DIR/<target>/ (the zips' files)

Without games named, build, run and pack take the games whose "ports" (games.json) list the target.
`probe` reads the calculator build's own commands (make -Bn build), so a game's Makefile stays the one
list of its sources."""
import concurrent.futures as cf
import json
import os
import re
import shlex
import subprocess
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAMES = os.path.join(ROOT, "games")
NWLINK = os.environ.get("NWLINK", "npx --yes -- nwlink@1.0.0")
SKIP = {"common", "numdance", "tetris"}  # tetris: Rust, for the calculator's ARM only
TARGETS = ("sdl", "web", "ti84ce", "nspire", "casio-cg")
CALCULATORS = ("ti84ce", "nspire", "casio-cg")
LIMIT = {"casio-cg": 2 << 20}  # (an add-in's largest size)


def entries():
    gs = json.load(open(os.path.join(GAMES, "games.json")))
    bad = sorted({p for g in gs for p in g.get("ports", []) if p not in TARGETS})
    if bad:
        raise SystemExit(f"games.json: no target {', '.join(bad)} ({', '.join(TARGETS)})")
    return gs


def games(args, target=None):
    """The games named, or the games whose "ports" list the target, or with no target every game with a Makefile."""
    if args:
        return args
    if target:
        if target not in TARGETS:
            raise SystemExit(f"ports: no target {target} ({', '.join(TARGETS)})")
        return [g["id"] for g in entries() if target in g.get("ports", [])]
    ids = [g["id"] for g in entries()]
    return [g for g in ids if g not in SKIP and os.path.exists(os.path.join(GAMES, g, "Makefile"))]


def ce_name(g):
    """The game's program name on the TI-84 Plus CE, as port.mk makes it: 8 letters and digits."""
    return re.sub(r"[^A-Z0-9]", "", g.upper())[:8]


def outputs(target, g):
    """The files `make port` leaves for the calculator: (the path, its name in a zip)."""
    b = os.path.join(GAMES, g, "build", target)
    if target == "ti84ce":
        return [(os.path.join(b, g, "bin", ce_name(g) + ".8xp"), ce_name(g) + ".8xp")]
    if target == "nspire":
        return [(os.path.join(b, g + ".tns"), g + ".tns")]
    if target == "casio-cg":
        return [(os.path.join(b, g + ".g3a"), g + ".g3a")]
    if target == "web":
        return [(os.path.join(b, g + ext), f"{g}/{g}{ext}") for ext in (".html", ".js", ".wasm")]
    raise SystemExit(f"ports: nothing to pack for {target}")


README = {
    "ti84ce": """NumPlay for the TI-84 Plus CE and the TI-83 Premium CE

How to install
1. Send the .8xp files to the calculator with TI Connect CE (education.ti.com).
2. Send clibs.8xg too. It's the CE C libraries the games need, from
   github.com/CE-Programming/libraries/releases
3. On OS 5.5 and later the calculator won't start the games by itself.
   Install arTIfiCE (yvantt.github.io/arTIfiCE) and start them from it.
   On an older OS, press prgm, pick a game and press enter twice.

Keys
Arrows move, enter is OK, clear is Back, mode saves and quits.
When a game says EXE, press 2nd.
""",
    "nspire": """NumPlay for the TI-Nspire CX and CX II

How to install
1. Install Ndless on the calculator (ndless.me). It runs on the OS versions listed there.
2. Send the .tns files to the calculator with TI-Nspire Computer Link or the TI-Nspire
   software, into any folder.
3. Open a game from My Documents.

Keys
Arrows move, enter is OK, esc is Back, home saves and quits.
When a game says EXE, press the return key.
""",
    "casio-cg": """NumPlay for the Casio fx-CG50 and Graph 90+E (and the fx-CG10 and fx-CG20)

How to install
1. Connect the calculator to the computer with its USB cable and choose USB Flash (F1).
2. Copy the .g3a files onto the calculator's drive.
3. Eject the drive. The games are in the main menu.

Keys
Arrows move, EXE is OK, EXIT is Back, MENU saves and quits.
When a game says EXE, press F1.
""",
    "web": """NumPlay in a web browser

Browsers don't run these straight from the disk. Serve this folder, for example with
  python3 -m http.server
run in it, then open http://localhost:8000 and a game's .html file.

Keys
Arrows move, Enter is OK, Escape is Back, Home saves and quits.
When a game says EXE, press Space.
""",
}


def pack(target, out, todo):
    """The built files of the games in a zip, flat (a folder a game for the web), with README.txt."""
    if not todo:
        raise SystemExit(f"pack: no game lists {target} in its ports (games.json)")
    titles = {g["id"]: g["title"] for g in entries()}
    files, missing = [], []
    for g in todo:
        fs = outputs(target, g)
        if not all(os.path.exists(p) for p, _ in fs):
            missing.append(g)
        files += [(g, p, n) for p, n in fs]
    if missing:
        raise SystemExit(f"pack: not built for {target}: {' '.join(missing)} (python3 tools/ports.py build {target})")
    names = [n.lower() for _, _, n in files]
    twice = sorted({n for n in names if names.count(n) > 1})
    if twice:
        raise SystemExit(f"pack: two games make {' '.join(twice)}")
    first = {}
    for g, _, n in files:
        first.setdefault(g, n.split("/")[0])
    w = max(len(n) for n in first.values())
    readme = README[target] + "\nThe games\n" + "".join(f"{first[g]:<{w}}  {titles.get(g, g)}\n" for g in todo) + \
        "\ngithub.com/Mason363/NumPlay\n"
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        z.writestr("README.txt", readme)
        for _, p, n in files:
            z.write(p, n)
    print(f"{out}: {len(todo)} games for {target}, {os.path.getsize(out) // 1024} KB")
    for _, p, n in files:
        if os.path.getsize(p) > LIMIT.get(target, 1 << 62):
            print(f"  {n} is {os.path.getsize(p)} bytes: the calculator takes {LIMIT[target]} at most")


def kit(d):
    """DIR/ports.json, what NumPlay Studio reads: each target's games, their file in DIR/<target>/ and its size."""
    version = open(os.path.join(ROOT, "VERSION")).read().strip()
    commit = os.environ.get("GITHUB_SHA") or subprocess.run(["git", "-C", ROOT, "rev-parse", "HEAD"],
                                                            capture_output=True, text=True).stdout.strip()
    targets = {}
    for t in CALCULATORS:
        folder = os.path.join(d, t)
        if not os.path.isdir(folder):
            continue
        there = set(os.listdir(folder)) - {"README.txt"}
        listed = {}
        for g in games([], t):
            (_, name), = outputs(t, g)
            if name not in there:
                raise SystemExit(f"kit: {t}/{name} ({g}) missing")
            listed[g] = {"file": name, "size": os.path.getsize(os.path.join(folder, name))}
        extra = there - {v["file"] for v in listed.values()}
        if extra:
            raise SystemExit(f"kit: {t}/ has files of no game: {' '.join(sorted(extra))}")
        targets[t] = listed
    if not targets:
        raise SystemExit(f"kit: no {', '.join(CALCULATORS)} folder in {d}")
    with open(os.path.join(d, "ports.json"), "w") as f:
        json.dump({"version": version, "commit": commit, "targets": targets}, f, indent=1)
        f.write("\n")
    print(f"{d}/ports.json: " + ", ".join(f"{len(v)} {t}" for t, v in targets.items()))


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
        todo = games(a, target)
        if not todo:
            print(f"no game lists {target} in its ports (games.json)")
            return 0
        ok = 0
        with cf.ThreadPoolExecutor(jobs) as ex:
            for g, (good, err) in zip(todo, ex.map(lambda g: build(target, g), todo)):
                ok += good
                print(f"{'ok  ' if good else 'FAIL'} {g}" + ("" if good else "\n      " + "\n      ".join(err)))
        print(f"{ok}/{len(todo)} build for {target}")
        return 0 if ok == len(todo) else 1
    elif cmd == "run":
        todo = games(a, "sdl")
        if not todo:
            print("no game lists sdl in its ports (games.json)")
            return 0
        with cf.ThreadPoolExecutor(jobs) as ex:
            results = [(g, *r) for g, r in zip(todo, ex.map(run, todo))]
        for g, how, _ in results:
            print(f"{'ok  ' if how == 'ok' else 'BAD '} {g}: {how}")
        sheet(results, os.path.join(ROOT, "build", "ports", "shots.png"))
        good = sum(h == "ok" for _, h, _ in results)
        print(f"{good}/{len(results)} run; build/ports/shots.png")
        return 0 if good == len(results) else 1
    elif cmd == "list" and len(a) == 1:
        print("\n".join(games([], a[0])))
    elif cmd == "pack" and len(a) >= 2:
        target, out = a[0], a[1]
        pack(target, out, games(a[2:], target))
    elif cmd == "kit" and len(a) == 1:
        kit(a[0])
    else:
        print(__doc__)
        return 1


if __name__ == "__main__":
    sys.exit(main())
