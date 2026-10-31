#!/usr/bin/env python3
"""NumPlay Studio's own .nwa apps (customize/apps.js): its npmodule() makes every game's module of a build to the
byte as tools/npmodule.py did (from BUILD/modules/<game>.2.o to <game>.o), and an app's entry links into a NumPlay.

Usage: test_apps.py BUILD   (a build with its modules: build/data after tools/build.py)"""
import json
import os
import subprocess
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
JS = os.path.join(ROOT, "customize", "apps.js")
RUN = r"""
const fs = require('fs'), a = require(process.argv[1]);
for (const [inp, out, game, index, entry] of JSON.parse(process.argv[2])) {
  try { fs.writeFileSync(out, a.npmodule(new Uint8Array(fs.readFileSync(inp)), game, index, entry).obj); }
  catch (e) { console.error(game + ': ' + e.message); process.exitCode = 1; }
}
"""


def main():
    build = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "data")
    mods = os.path.join(build, "modules")
    jobs, tmp = [], os.path.join(build, "test-apps")
    os.makedirs(tmp, exist_ok=True)
    for f in sorted(os.listdir(mods)):
        if not f.endswith(".json"):
            continue
        g = f[:-5]
        info = json.load(open(os.path.join(mods, f)))
        src = os.path.join(mods, g + ".2.o")
        if os.path.exists(src):
            # (the Makefile's ENTRY_<game>: Tetris, made in Rust, starts at np_tetris_main)
            entry = "np_tetris_main" if g == "tetris" else "main"
            jobs.append((src, os.path.join(tmp, g + ".o"), g, info["index"], entry))
    if not jobs:
        sys.exit(f"test_apps: no modules in {mods}")
    if subprocess.run(["node", "-e", RUN, JS, json.dumps(jobs)]).returncode:
        sys.exit("test_apps: npmodule() failed")
    bad = [g for _, out, g, _, _ in jobs if open(out, "rb").read() != open(os.path.join(mods, g + ".o"), "rb").read()]
    if bad:
        sys.exit(f"test_apps: npmodule() differs from tools/npmodule.py for {', '.join(bad)}")
    print(f"test_apps: npmodule() makes {len(jobs)} modules as tools/npmodule.py does")


if __name__ == "__main__":
    main()
