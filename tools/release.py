#!/usr/bin/env python3
"""Release notes and file labels for a GitHub release.

GitHub lists a release's files from A to Z and there is no way to reorder
them, so the notes carry the list in a useful order (NumPlay first, then its
discreet versions, then each game on its own in the launcher's order) with a
link to each file, and every file gets a label saying what it is.

- --notes FILE: writes the notes, .github/release.md (the introduction) and
  then the list;
- prints one `path#label` per line, the arguments of `gh release create`.

Usage: release.py --tag v1.3.0 --notes build/release-notes.md
"""
import argparse
import json
import os
import re

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
REPO = "https://github.com/Mason363/NumPlay"
# in games.json, but not games
EXTRAS = {"numvisuals"}
# games too big to fit next to NumPlay on the calculator: only on their own, in a section of their own
ALONE = {
    "celeste": ("Celeste", "the whole climb up Celeste Mountain. Too big to share the calculator with NumPlay: install it "
                "on its own"),
    "tycoon": ("NumTycoon", "build a business empire, from a lemonade stand to a space port. NumPlay's share of the "
               "calculator's app space is full: install it on its own"),
    "hollowknight": ("Hollow Knight", "from King's Pass to Hornet in Greenpath. Too big to share the calculator with "
                     "NumPlay: install it on its own"),
    "championisland": ("Champion Island", "the Doodle Champion Island Games. Too big to share the calculator with "
                       "NumPlay: install it on its own"),
}
VARIANTS = [
    ("NumPlay-Invisible.nwa", "The same app, hidden: a blank icon with no name"),
    ("NumPlay-Matrices.nwa", "The same app, disguised as a math app called Matrices"),
]


def standalone_names():
    """Game id -> its own .nwa, from the Makefile (APP_<id> = path:Name.nwa)."""
    names = {}
    for line in open(os.path.join(ROOT, "Makefile")):
        m = re.match(r"APP_(\w+)\s*=\s*\S+:(\S+\.nwa)\s*$", line)
        if m:
            names[m.group(1)] = m.group(2)
    return names


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tag", required=True)
    ap.add_argument("--notes", required=True)
    ap.add_argument("--build", default="build")
    a = ap.parse_args()
    games = json.load(open(os.path.join(ROOT, "games", "games.json")))
    apps = standalone_names()
    count = sum(1 for g in games if g["id"] not in EXTRAS)
    extras = " and ".join(g["title"] for g in games if g["id"] in EXTRAS)
    everything = f"All {count} games" + (f" and {extras}" if extras else "") + " in one app"

    def link(name):
        return f"[{name}]({REPO}/releases/download/{a.tag}/{name})"

    files = [(os.path.join(a.build, "NumPlay.nwa"), f"NumPlay.nwa · Start here: {everything[0].lower() + everything[1:]}")]
    notes = ["### Start here", "", "| File | |", "| --- | --- |",
             f"| **{link('NumPlay.nwa')}** | **{everything}.** Start here! |", "",
             "### The same app, in disguise", "", "| File | |", "| --- | --- |"]
    for name, what in VARIANTS:
        files.append((os.path.join(a.build, name), f"{name} · {what}"))
        notes.append(f"| {link(name)} | {what} |")
    notes += ["", "### Each one on its own", "", "In the launcher's order.", "", "| File | |", "| --- | --- |"]
    def alone_entry(gid):
        title, what = ALONE[gid]
        name, what = apps[gid], f"{title}: {what}"
        files.append((os.path.join(a.build, "apps", name), f"{name} · {what}"))
        notes.append(f"| {link(name)} | {what} |")

    for g in games:
        name = g.get("app") or apps.get(g["id"])
        if not name:
            continue
        tagline = g["tagline"][0].lower() + g["tagline"][1:]
        what = f"Only {g['title']}: {tagline}"
        files.append((os.path.join(a.build, "apps", name), f"{name} · {what}"))
        notes.append(f"| {link(name)} | {what} |")
    alone = [gid for gid in ALONE if gid in apps]
    if alone:
        notes += ["", "### Too big for NumPlay", "", "| File | |", "| --- | --- |"]
        for gid in alone:
            alone_entry(gid)
    notes += ["", "<sub>The file list below is sorted A to Z by GitHub. Each file's label says what it is.</sub>"]

    intro = open(os.path.join(ROOT, ".github", "release.md")).read().rstrip()
    with open(a.notes, "w") as f:
        f.write(intro + "\n\n" + "\n".join(notes) + "\n")
    for path, label in files:
        if not os.path.exists(path):
            raise SystemExit(f"release: missing {path}")
        print(f"{path}#{label}")


if __name__ == "__main__":
    main()
