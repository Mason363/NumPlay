#!/usr/bin/env python3
"""Five Nights at Freddy's 2: gathers the public material tools/pack.py turns into src/data.bin.

    python3 tools/fetch.py REF        (then fill in REF/map.txt, then: make data REF=REF)

The game's own pictures, as a fan web port keeps them in its repository, at the commit used:
VocalNutria9174's fnaf-2_html5 (github.com/VocalNutria9174/fnaf-2_html5), the game's Clickteam HTML5 build,
whose pictures are numbered files in resources/. It is cloned with git into REF/html5.

Then REF/map.txt is written (if it isn't there): a line for each picture pack.py needs, "NAME ?" and what it
shows. Put the file each comes from in place of the "?" (from REF, html5/resources/0018.png say); a jumpscare
gets a line for each of its frames, in order."""
import os
import subprocess
import sys

SOURCES = [
    ("html5", "https://github.com/VocalNutria9174/fnaf-2_html5", "9dce599551feb34bd5c2aca199b7a9dfde70f866"),
]
EXTRA = [("TITLE_TEXT", "the title's words, white on transparent"), ("STAR", "the title's star"),
         ("ICON", "a face for the app's icon (it is cut square)")]


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    ref = sys.argv[1]
    os.makedirs(ref, exist_ok=True)
    for name, url, commit in SOURCES:
        d = os.path.join(ref, name)
        if not os.path.isdir(os.path.join(d, ".git")):
            subprocess.run(["git", "clone", "--quiet", url, d], check=True)
        subprocess.run(["git", "-C", d, "checkout", "--quiet", commit], check=True)
        print(f"{name}: {commit[:10]}")
    path = os.path.join(ref, "map.txt")
    if os.path.exists(path):
        return
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from pack import PICTURES  # noqa: E402 (the list of pictures, nothing else of it runs)
    with open(path, "w") as f:
        f.write("# NAME file (from this folder). A jumpscare: a line a frame, in order.\n")
        for name, kind, what in PICTURES:
            if kind == "face" or not kind.startswith("patch"):
                f.write(f"{name} ?    # {what}\n")
            else:
                f.write(f"{name} ?    # {what} (the whole office: pack.py cuts the part)\n")
        for name, what in EXTRA:
            f.write(f"{name} ?    # {what}\n")
    print(f"wrote {path}: fill it in, then make data REF={ref}")


if __name__ == "__main__":
    main()
