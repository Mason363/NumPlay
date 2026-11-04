#!/usr/bin/env python3
"""NumPlay in other languages: NumPlay-French.nwa, NumPlay-Chinese.nwa.

Texts players read are marked in the sources: T("...") in C, T!("...") in Rust
(games/common/np_text.h). Each part has its translations, one per line, the
English text as it is written in the source, a tab, then the translation (the
same escapes: \\n, \\"; UTF-8):

    launcher/lang/<code>.txt      the launcher, and the games' names, taglines and credits (games/games.json)
    games/<game>/lang/<code>.txt  a game

  lang.py strings [PART]        every marked text of a part (or all), for translating
  lang.py check CODE            the texts still missing a translation, part by part
  lang.py tree CODE OUT         copies what builds NumPlay to OUT with the translations in place
                                (a missing one stays English), NP_TEXT_EXTRA on, the launcher's
                                accented letters (tools/fontgen.py) and the 12-pixel font's letters
                                (tools/xfont.py) for the texts there
"""
import json
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
LANGS = {"fr": "French", "zh": "Chinese"}
MARK = re.compile(r'\bT(!?)\(\s*"((?:[^"\\\n]|\\.)*)"\s*\)')
CODE_EXT = (".c", ".h", ".rs")
# not part of a build
SKIP = re.compile(r"\.(gif|mp4|mov)$")


def parts():
    out = {"launcher": [os.path.join(ROOT, "launcher")]}
    for g in json.load(open(os.path.join(ROOT, "games", "games.json"))):
        if g.get("default", True):  # (the kit's games are not in the language builds)
            out[g["id"]] = [os.path.join(ROOT, "games", g["id"])]
    return out


def code_files(dirs):
    for d in dirs:
        for base, subdirs, files in os.walk(d):
            subdirs[:] = [s for s in subdirs if s not in ("node_modules", "target", "output", "build", "lang")]
            for f in files:
                if f.endswith(CODE_EXT):
                    yield os.path.join(base, f)


def marked(path):
    try:
        text = open(path, encoding="utf-8").read()
    except UnicodeDecodeError:
        return []
    return [m.group(2) for m in MARK.finditer(text)]


def strings(part):
    found = []
    for f in code_files(parts()[part]):
        for s in marked(f):
            if s not in found:
                found.append(s)
    if part == "launcher":
        for g in json.load(open(os.path.join(ROOT, "games", "games.json"))):
            if not g.get("default", True):
                continue
            for k in ("title", "tagline", "credit"):
                if not g.get(k):
                    continue
                v = json.dumps(g[k], ensure_ascii=False)[1:-1]
                if v not in found:
                    found.append(v)
    return found


def table_path(part, code):
    base = os.path.join(ROOT, "launcher") if part == "launcher" else os.path.join(ROOT, "games", part)
    return os.path.join(base, "lang", f"{code}.txt")


def load_table(part, code):
    p = table_path(part, code)
    table = {}
    if not os.path.exists(p):
        return table
    for n, line in enumerate(open(p, encoding="utf-8"), 1):
        line = line.rstrip("\n")
        if not line or line.startswith("#"):
            continue
        if "\t" not in line:
            raise SystemExit(f"{p}:{n}: no tab between the English and the translation")
        en, tr = line.split("\t", 1)
        table[en] = tr
    return table


def check(code):
    missing = 0
    for part in parts():
        t = load_table(part, code)
        todo = [s for s in strings(part) if s not in t]
        if todo:
            print(f"{part}: {len(todo)} missing")
            for s in todo:
                print(f"    {s}")
        missing += len(todo)
    print(f"{LANGS.get(code, code)}: {missing} texts missing")
    return missing


def tree(code, out):
    """A copy of what builds NumPlay, translated."""
    files = subprocess.run(["git", "ls-files", "--cached", "--others", "--exclude-standard"], cwd=ROOT,
                           capture_output=True, text=True, check=True).stdout.split("\n")
    keep = [f for f in files if f and (f in ("Makefile", "VERSION") or f.startswith(("launcher/", "games/", "tools/", "assets/fonts/", "platform/")))
            and not SKIP.search(f) and not f.startswith("games/numdance/")]
    if os.path.exists(out):
        shutil.rmtree(out)
    tables = {part: load_table(part, code) for part in parts()}
    owner = {}
    for part, dirs in parts().items():
        for d in dirs:
            owner[os.path.relpath(d, ROOT)] = part
    used = []  # every translated text, for the fonts
    for f in keep:
        src, dst = os.path.join(ROOT, f), os.path.join(out, f)
        if not os.path.exists(src):
            continue
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        part = next((p for d, p in owner.items() if f == d or f.startswith(d + "/")), None)
        if f.endswith(CODE_EXT) and part:
            t = tables[part]
            text = open(src, encoding="utf-8", errors="surrogateescape").read()

            def put(m):
                tr = t.get(m.group(2))
                if tr is None:
                    return m.group(0)
                used.append(tr)
                return f'T{m.group(1)}("{tr}")'
            text = MARK.sub(put, text)
            if f == "games/common/np_text.h":
                text = text.replace("#define NP_TEXT_EXTRA 0", "#define NP_TEXT_EXTRA 1")
            open(dst, "w", encoding="utf-8", errors="surrogateescape").write(text)
        else:
            shutil.copy2(src, dst)
    # the common header (no part) still turns NP_TEXT_EXTRA on
    h = os.path.join(out, "games/common/np_text.h")
    text = open(h).read().replace("#define NP_TEXT_EXTRA 0", "#define NP_TEXT_EXTRA 1")
    open(h, "w").write(text)
    # the games' names, taglines and credits
    gj = os.path.join(out, "games/games.json")
    games = json.load(open(gj))
    lt = tables["launcher"]
    for g in games:
        for k in ("title", "tagline", "credit"):
            if not g.get(k):
                continue
            key = json.dumps(g[k], ensure_ascii=False)[1:-1]
            if key in lt:
                g[k] = json.loads(f'"{lt[key]}"')
                used.append(lt[key])
    # (the screenshots in 16 colours, not 48: room for the translations and their letters; at the
    # carousel's size they look the same)
    for g in games:
        g["colors"] = min(g.get("colors", 48), 16)
    json.dump(games, open(gj, "w"), indent=2, ensure_ascii=False)
    # node packages some games build with
    for nm in ("games/numdash/node_modules",):
        if os.path.isdir(os.path.join(ROOT, nm)):
            os.symlink(os.path.join(ROOT, nm), os.path.join(out, nm))
    # fonts: the launcher's accented letters, and the 12-pixel font for the rest
    text = "\n".join(used) + "\n" + "".join(chr(c) for c in range(0x21, 0x7F))
    latin = sorted({ord(c) for c in text if 0x80 <= ord(c) < 0x180})
    # the launcher's own texts get Nunito's letters; the games draw theirs with accents (np_latin)
    if os.path.isdir(os.path.join(ROOT, "tools/.fonts")):
        shutil.copytree(os.path.join(ROOT, "tools/.fonts"), os.path.join(out, "tools/.fonts"), dirs_exist_ok=True)
    subprocess.run([sys.executable, os.path.join(out, "tools/fontgen.py"), os.path.join(out, "launcher/src/font_data.c"),
                    "--extra", ",".join(str(c) for c in latin)], check=True, cwd=ROOT)
    all_text = os.path.join(out, "build-lang-texts.txt")
    open(all_text, "w", encoding="utf-8").write(text)
    subprocess.run([sys.executable, os.path.join(ROOT, "tools/xfont.py"), "table",
                    os.path.join(ROOT, "assets/fonts/fusion-pixel-12px.bin"),
                    os.path.join(out, "launcher/src/np_xfont.h"), all_text], check=True)
    missing = sum(1 for part in parts() for s in strings(part) if s not in tables[part])
    print(f"{out}: {LANGS.get(code, code)}, {len(used)} texts translated, {missing} still English")


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    cmd = sys.argv[1]
    if cmd == "strings":
        for part in ([sys.argv[2]] if len(sys.argv) > 2 else parts()):
            print(f"== {part}")
            for s in strings(part):
                print(s)
    elif cmd == "check":
        sys.exit(1 if check(sys.argv[2]) else 0)
    elif cmd == "tree":
        tree(sys.argv[2], sys.argv[3])
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
