#!/usr/bin/env python3
"""Packs the kit the customizer website links a NumPlay of your own from (`make kit`).

The website (site/) runs GNU ld in the browser on the same objects the Makefile links
NumPlay.nwa from: base.o (the launcher), arena.o (the RAM the games share), an app name and
icon, and one object per game, with a script made of script.json's parts (tools/gen_games.py).
This copies them to OUT, gzipped, with kit.json (the games, their sizes, colours and
limits), each game's icon and thumbnail, and checks that every game has the clip the website plays
(docs/media/previews/ or gallery/), that linking the default games from OUT
gives NumPlay.nwa's bytes on the calculator, and that each big game ("xl", for calculators
with Enlarged Extra Apps: up to xlAppSpace bytes of apps) links with the launcher alone. The
blocks of the script go in the games' order ("index"), so the big games come last: past the
app area, where Enlarged Extra Apps put them, only they look for the calculator's files there
(EPSILON_XL in games/common/epsilon_app.h). A big game too big to fit beside the launcher in the
app area ("alone") comes as its own app too (apps/<id>.nwa.gz, --alone): the website hands it out
as it is, for calculators without Enlarged Extra Apps.

The calculator's space for apps starts where its software ends, so it depends on the software (--spaces, kit.json
"spaces": 0x90200000 or 0x90180000 to 0x903F0000, or on to 0x90800000 with Enlarged Extra Apps). For each, this
links the fullest NumPlay the website lets you make, NumPlay's games and the big games in their order while they
fit, at that space's start with its length, as the calculator's installer does; and checks each big game goes
beside the launcher there, or has its own app that fits.

A game's "ram" in kit.json is what it takes in the arena (its .data, its .bss and the least heap
it needs): it fits a calculator when ram + launcherRam (NumPlay's own RAM, before the arena) is
at most what the calculator's software gives apps ("ram", "ramLimit", "upsilonRam"). Every game must fit
"ram" (custom builds of 25.2, the least of the calculators NumPlay runs on but Upsilon).

Usage: kit.py games.json BUILD OUT --version V --variants "..." --default "ids" --upsilon "ids"
              --app-space N --xl-app-space N --spaces "id:start:end ..." --ram N --ram-limit N
              --upsilon-ram N --nwlink NWLINK --alone "id=game.nwa:Name.nwa ..."
"""
import argparse
import gzip
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SHF_ALLOC = 2
SHT_NOBITS = 8
LINK = ["-r", "--gc-sections", "-z", "noexecstack", "-e", "main",
        "-u", "eadk_app_name", "-u", "eadk_app_icon", "-u", "eadk_api_level"]


def sections(path):
    """(name, type, flags, size, align) of each section of an ELF object."""
    d = open(path, "rb").read()
    shoff, = struct.unpack_from("<I", d, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", d, 0x2E)
    hdr = [struct.unpack_from("<10I", d, shoff + i * shentsize) for i in range(shnum)]
    strtab = hdr[shstrndx]
    names = d[strtab[4]:strtab[4] + strtab[5]]
    out = []
    for h in hdr:
        n = names[h[0]:names.index(b"\0", h[0])].decode()
        out.append((n, h[1], h[2], h[5], max(h[8], 1)))
    return out


def installed_size(nwa):
    """Bytes the installer writes to flash for an .nwa: its 32 byte header, then its sections
    in the order of nwlink's script (the name, the icon, code, read-only data, the data image)."""
    secs = [s for s in sections(nwa) if s[2] & SHF_ALLOC and s[1] != SHT_NOBITS]
    def group(s):
        n = s[0]
        if n == ".rodata.eadk_app_name": return 0
        if n == ".rodata.eadk_app_icon": return 1
        if n == ".text" or n.startswith(".text."): return 2
        if n == ".rodata" or n.startswith(".rodata."): return 3 if n != ".rodata.eadk_api_level" else -1
        if n == ".data" or n.startswith(".data."): return 4
        return 3
    at = 32
    for g in range(5):
        if g in (2, 4): at = (at + 3) & ~3
        for s in secs:
            if group(s) == g:
                at = (at + s[4] - 1) & ~(s[4] - 1)
                at += s[3]
    return at


def icon_of(gid):
    for p in ("src/icon.png", "assets/icon.png", "tetris/src/data/icon.png", "docs/icon.png"):
        f = os.path.join(ROOT, "games", gid, p)
        if os.path.exists(f):
            return f
    return None


def preview_of(gid):
    """The clip the website plays on the screen for a game: from the emulator (tools/previews.py), else its
    gallery GIF (docs/media/previews/ or docs/media/gallery/, as kit.json's "preview" names it)."""
    return next((f"{d}/{gid}.gif" for d in ("previews", "gallery")
                 if os.path.exists(os.path.join(ROOT, "docs", "media", d, f"{gid}.gif"))), None)


def rounded(src, dst):
    """An app icon as the website shows it: the calculator's white outside its corners made transparent, with
    the corners every game icon has (make_icon.corner_mask), so it sits on the page as they do."""
    from PIL import Image, ImageChops
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from make_icon import corner_mask
    im = Image.open(src).convert("RGBA")
    im.putalpha(ImageChops.darker(im.getchannel("A"), corner_mask(*im.size)))
    im.save(dst, optimize=True)


def gz(src, dst):
    with open(src, "rb") as f, open(dst, "wb") as g:
        g.write(gzip.compress(f.read(), 9, mtime=0))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("manifest")
    ap.add_argument("build")
    ap.add_argument("out")
    ap.add_argument("--version", required=True)
    ap.add_argument("--variants", required=True)
    ap.add_argument("--default", required=True)
    ap.add_argument("--upsilon", default="")
    ap.add_argument("--app-space", type=int, required=True)
    ap.add_argument("--xl-app-space", type=int, required=True)
    ap.add_argument("--spaces", default="", help="the calculators' spaces for apps, id:start:end ...")
    ap.add_argument("--ram", type=int, required=True)
    ap.add_argument("--ram-limit", type=int, default=148928)
    ap.add_argument("--upsilon-ram", type=int, required=True)
    ap.add_argument("--nwlink", default="nwlink")
    ap.add_argument("--langs", default="", help="the languages make kit-langs adds (site-kit/<code>)")
    ap.add_argument("--alone", default="", help="each big game's own app, id=game.nwa:Name.nwa")
    ap.add_argument("--english", help="a language's kit: the English kit, where the website gets the big games' own apps")
    a = ap.parse_args()
    B, O = a.build, a.out
    manifest = json.load(open(a.manifest))
    # every game's icon plays its clip on the website, so a game without one stops the kit (a language's
    # kit has none: its tree has no docs/, and the website plays the English kit's)
    if not a.english:
        missing = [g["id"] for g in manifest if not preview_of(g["id"])]
        if missing:
            raise SystemExit(f"kit: no preview clip for {', '.join(missing)}. Add docs/media/gallery/<id>.gif "
                             "(tools/emu.py --frames, then tools/record.py --frames) or docs/media/previews/<id>.gif "
                             "(tools/previews.py)")
    index = {g["id"]: n for n, g in enumerate(manifest, 1)}
    default, upsilon = a.default.split(), set(a.upsilon.split())
    script = json.load(open(os.path.join(B, "gen", "script.json")))
    if os.path.exists(O):
        shutil.rmtree(O)
    for d in ("games", "icons", "thumbs", "variants", "apps"):
        os.makedirs(os.path.join(O, d))

    # ---- the objects, stripped of what no link needs, and gzipped
    tmp = tempfile.mkdtemp()
    def put(src, rel):
        t = os.path.join(tmp, os.path.basename(rel))
        subprocess.run(["arm-none-eabi-strip", "--strip-unneeded", src, "-o", t], check=True)
        gz(t, os.path.join(O, rel + ".gz"))
        return t
    local = {"base.o": put(os.path.join(B, "kit", "base.o"), "base.o"),
             "arena.o": put(os.path.join(B, "arm", "arena.o"), "arena.o")}
    variants = []
    for v in a.variants.split():
        for part in ("name", "icon"):
            local[f"{v}-{part}.o"] = put(os.path.join(B, "variant", f"{v}-{part}.o"), f"variants/{v}-{part}.o")
        png = "icon.png" if v == "NumPlay" else "icon-" + v.split("-", 1)[1].lower() + ".png"
        rounded(os.path.join(ROOT, "launcher", "assets", png), os.path.join(O, "variants", v + ".png"))
        variants.append(v)

    games, raw = [], {}
    for g in manifest:
        i = g["id"]
        obj = os.path.join(B, "kit", "games", i + ".o")
        if not os.path.exists(obj):
            continue
        local[i] = put(obj, f"games/{i}.o")
        n = index[i]
        # its block's bytes ("flash" adds the most padding it can need before it: blocks end on a word)
        flash, align = 0, 4
        for s in sections(obj):
            if s[2] & SHF_ALLOC and s[1] != SHT_NOBITS:
                flash = (flash + s[4] - 1) & ~(s[4] - 1)
                flash += s[3]
                align = max(align, s[4])
        raw[i] = flash
        flash += align - 4
        mod = json.load(open(os.path.join(B, "modules", i + ".json")))
        ram = ((((mod["data"] + 7) & ~7) + mod["bss"] + 7) & ~7) + g.get("heap", 0)
        icon = icon_of(i)
        if icon:
            shutil.copy(icon, os.path.join(O, "icons", i + ".png"))
        preview = preview_of(i)
        thumb = os.path.join(ROOT, "docs", "media", f"thumb_{i}.png")
        if os.path.exists(thumb):
            shutil.copy(thumb, os.path.join(O, "thumbs", i + ".png"))
        games.append({
            "id": i, "index": n, "rank": g.get("rank", 99), "title": g["title"], "tagline": g["tagline"], "credit": g.get("credit"),
            "top": g["top"], "bottom": g["bottom"], "accent": g["accent"],
            "flash": flash, "ram": ram, "heap": g.get("heap", 0), "default": i in default, "upsilon": i in upsilon,
            "roms": bool(g.get("roms")), "systems": g.get("systems"), "xl": bool(g.get("xl")),
            "bytes": os.path.getsize(os.path.join(O, "games", i + ".o.gz")),
            "icon": bool(icon), "thumb": os.path.exists(thumb), "preview": preview})

    # ---- check: the default games, linked from the kit, are NumPlay.nwa on the calculator
    def link(ids, nwa):  # (the blocks in the order of ids)
        ld_script = nwa[:-4] + ".ld"
        open(ld_script, "w").write(script["head"] + "".join(script["blocks"][i] for i in ids) + script["tail"])
        subprocess.run(["arm-none-eabi-ld"] + LINK + ["-T", ld_script, local["base.o"], local["arena.o"],
                        local["NumPlay-name.o"], local["NumPlay-icon.o"]] + [local[i] for i in ids] +
                       ["-o", nwa], check=True)
        subprocess.run(["arm-none-eabi-strip", "--strip-unneeded", nwa], check=True)
        # only what the installer gives may be left undefined (a game's weak `numplay_launcher`, say,
        # must find the launcher's; a game's heap is in the arena, its malloc's the launcher's)
        nm = subprocess.run(["arm-none-eabi-nm", "-u", nwa], capture_output=True, text=True, check=True).stdout
        loose = [l.split()[-1] for l in nm.splitlines()
                 if not re.match(r"(eadk_|_heap_end$|_data_section_|_bss_section_)", l.split()[-1])
                 and l.split() not in (["w", "__sf"], ["w", "_printf_float"])]  # (newlib's own weak hooks)
        if loose:
            sys.exit(f"kit: {ids[0] if len(ids) == 1 else 'NumPlay'} leaves undefined: {' '.join(loose)}")
    nwa = os.path.join(tmp, "custom.nwa")
    link(default, nwa)
    # a big game calls nothing in base.o but the launcher (KIT_API in the Makefile): linked alone with it
    for g in games:
        if g["xl"]:
            link([g["id"]], os.path.join(tmp, f"xl-{g['id']}.nwa"))
    ram = a.ram_limit
    bins = []
    for f in (nwa, os.path.join(B, "NumPlay.nwa")):
        b = f + ".bin" if f == nwa else os.path.join(tmp, "make.bin")
        subprocess.run([a.nwlink, "nwa-bin", "--ram-length", str(ram), f, b], check=True,
                       stdout=subprocess.DEVNULL)
        bins.append(open(b, "rb").read())
    if bins[0] != bins[1]:
        sys.exit("kit: the default games linked from the kit are not NumPlay.nwa")
    # the installer adds its own start code and EADK calls: the same few hundred bytes for any games
    runtime = len(bins[0]) - installed_size(nwa)
    if not 0 <= runtime < 4096:
        sys.exit(f"kit: installed_size says {installed_size(nwa)}, nwlink {len(bins[0])}")
    # NumPlay's own bytes, with the library functions its games call (a big game brings its own): with each
    # game's "flash", what a NumPlay with any of them takes at most ("base")
    base = len(bins[0]) - sum(raw[g["id"]] for g in games if g["default"])
    if base + sum(g["flash"] for g in games if g["default"]) > a.app_space:
        sys.exit(f"kit: NumPlay's games' sizes add up to more than the app space")
    # ...and with big games only, the launcher alone, as each big game linked with it shows (and the most padding
    # its code can need after their blocks)
    tail = max([4] + [s[4] for s in sections(local["base.o"]) if s[2] & SHF_ALLOC and s[1] != SHT_NOBITS])
    base_alone = max(installed_size(os.path.join(tmp, f"xl-{g['id']}.nwa")) + runtime - g["flash"]
                     for g in games if g["xl"]) + tail - 4
    # a big game that only fits the app area without the launcher goes on its own, as its own app
    apps = dict(x.split("=", 1) for x in a.alone.split())
    for g in games:
        if not g["xl"] or base_alone + g["flash"] <= a.app_space:
            continue
        if g["id"] not in apps:
            sys.exit(f"kit: {g['id']} doesn't fit beside the launcher, and --alone hasn't got its own app")
        src, name = apps[g["id"]].split(":")
        b = os.path.join(tmp, "alone.bin")
        subprocess.run([a.nwlink, "nwa-bin", "--ram-length", str(ram), src, b], check=True, stdout=subprocess.DEVNULL)
        if os.path.getsize(b) > a.app_space:
            sys.exit(f"kit: {name} doesn't fit the app area even on its own")
        gz(src, os.path.join(O, "apps", g["id"] + ".nwa.gz"))
        g["alone"] = {"file": name, "size": os.path.getsize(b)}
        # (a language's kit: the website hands out the same app from the English kit, make kit-langs puts it there)
        theirs = os.path.join(a.english or O, "apps", g["id"] + ".nwa.gz")
        if os.path.exists(theirs) and gzip.open(theirs).read() != open(src, "rb").read():
            sys.exit(f"kit: {g['id']} goes on its own here, but {theirs} isn't its app")
    # NumPlay's own RAM before the arena (the installer's few bytes too), as the calculator lays it out
    elf = os.path.join(tmp, "custom.elf")
    subprocess.run([a.nwlink, "nwa-elf", nwa, elf, "--ram-start", "0x20000000", "--ram-length", str(ram)],
                   check=True, stdout=subprocess.DEVNULL)
    nm = subprocess.run(["arm-none-eabi-nm", elf], capture_output=True, text=True, check=True).stdout
    launcher_ram = next(int(l.split()[0], 16) for l in nm.splitlines() if l.split()[-1] == "np_arena") - 0x20000000
    hungry = max(games, key=lambda g: g["ram"])
    over = [f"{g['id']} ({launcher_ram + g['ram']})" for g in games if launcher_ram + g["ram"] > a.ram]
    if over:
        sys.exit(f"kit: more RAM than custom builds of 25.2 give apps ({a.ram} bytes): {', '.join(over)}")

    # ---- a NumPlay for each space, linked where the calculator's installer puts it
    spaces = []
    for x in a.spaces.split():
        sid, start, end = x.split(":")
        sp = {"id": sid, "start": int(start, 0), "end": int(end, 0)}
        sp["size"] = sp["end"] - sp["start"]
        size, ids, used = sp["size"], list(default), base + sum(g["flash"] for g in games if g["default"])
        for g in sorted(games, key=lambda g: g["index"]):
            if g["xl"] and used + g["flash"] <= size:
                ids.append(g["id"])
                used += g["flash"]
        f = os.path.join(tmp, f"space-{sid}.nwa")
        link(ids, f)
        b = f + ".bin"
        r = subprocess.run([a.nwlink, "nwa-bin", "--flash-start", hex(sp["start"]), "--flash-length", str(size),
                            "--ram-length", str(ram), f, b], capture_output=True, text=True)
        if r.returncode or os.path.getsize(b) > size:
            sys.exit(f"kit: a NumPlay of {used} bytes doesn't install in {sid} ({size} bytes at {sp['start']:#x}): "
                     f"{(r.stdout + r.stderr).strip()}")
        n = os.path.getsize(b)
        if n > used:
            sys.exit(f"kit: in {sid}, NumPlay installs as {n} bytes, but its games' sizes add up to {used}")
        # a big game that doesn't go beside the launcher there must have its own app that fits
        for g in games:
            if g["xl"] and base_alone + g["flash"] > size and (not g.get("alone") or g["alone"]["size"] > size):
                sys.exit(f"kit: {g['id']} fits neither beside the launcher nor on its own in {sid}")
        big = [i for i in ids if i not in default]
        print(f"kit: {sid}, {size} bytes from {sp['start']:#x}: NumPlay with {len(ids)} games ({', '.join(big) or 'no big ones'}) "
              f"installs as {n} bytes, up to {sp['start'] + n:#x}")
        spaces.append(sp)

    # a language's kit: a game whose object is the English kit's (the big games, in English) comes from there
    if a.english:
        for g in games:
            mine, theirs = (os.path.join(d, "games", g["id"] + ".o.gz") for d in (O, a.english))
            if os.path.exists(theirs) and gzip.open(mine).read() == gzip.open(theirs).read():
                os.remove(mine)
                g["en"] = True
    kit = {"version": a.version, "appSpace": a.app_space, "xlAppSpace": a.xl_app_space, "ram": a.ram,
           "ramLimit": a.ram_limit, "upsilonRam": a.upsilon_ram, "launcherRam": launcher_ram,
           "arena": int(re.search(r"\.space (\d+)", open(os.path.join(B, "gen", "arena.s")).read()).group(1)),
           "spaces": spaces, "base": base, "baseAlone": base_alone, "runtime": runtime, "numplay": len(bins[0]), "link": LINK, "variants": variants,
           "langs": ["en"] + a.langs.split(), "script": script, "games": games}
    json.dump(kit, open(os.path.join(O, "kit.json"), "w"), indent=1)
    shutil.rmtree(tmp)
    print(f"kit: {len(games)} games, NumPlay.nwa's {len(bins[0])} bytes linked again from {O}; RAM: NumPlay's own "
          f"{launcher_ram} bytes, {hungry['id']}'s {hungry['ram']}: {launcher_ram + hungry['ram']} of {a.ram}")


if __name__ == "__main__":
    main()
