#!/usr/bin/env python3
"""docs/calculators/<calculator>.md, one page a calculator: how to install NumPlay's games on it, its keys, and the
games that run on it (games/games.json "ports"). Run it after a game gets a new target.

  python3 tools/calc_pages.py"""
import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REL = "https://github.com/Mason363/NumPlay/releases/latest"
BETA = "https://github.com/Mason363/NumPlay/issues/new?template=beta-tester.yml"

PAGES = [
    ("ti-84-plus-ce", "ti84ce", "TI-84 Plus CE", "ti84ce.png",
     "the TI-84 Plus CE, TI-84 Plus CE Python, TI-84 Plus CE-T and TI-83 Premium CE",
     f"""1. Get `NumPlay-TI84CE.zip` from the [latest release]({REL}) and unzip it.
2. Send the games you want (`.8xp` files) with [TI Connect CE](https://education.ti.com/en/products/computer-software/ti-connect-ce-sw).
3. Press **prgm**, pick a game, press **enter**.

On OS 5.5 and newer, programs like these need [arTIfiCE](https://yvantt.github.io/arTIfiCE/) first. Each game also needs the CE C libraries (`clibs.8xg` from [the CE toolchain's releases](https://github.com/CE-Programming/libraries/releases/latest)), sent once.""",
     "ti84ce", "The CE is the slowest of these calculators (48 MHz, no floating point unit) and has the least memory, so it gets the lighter games."),
    ("ti-nspire", "nspire", "TI-Nspire CX", "nspire.png",
     "the TI-Nspire CX and CX II, CAS or not",
     f"""1. Install [Ndless](https://ndless.me) on the calculator.
2. Get `NumPlay-Nspire.zip` from the [latest release]({REL}) and unzip it.
3. Copy the games you want (`.tns` files) to the calculator with TI-Nspire Link or the TI-Nspire computer software.
4. Open a game from **My Documents**.

Or pick just the games you want in [NumPlay Studio](https://mason363.github.io/NumPlay/customize/?calc=nspire).""",
     "nspire", ""),
    ("casio-fx-cg50", "casio-cg", "Casio fx-CG50", "casio_cg50.png",
     "the Casio fx-CG50, Graph 90+E, fx-CG10 and fx-CG20",
     f"""1. Get `NumPlay-CasioCG.zip` from the [latest release]({REL}) and unzip it.
2. Plug the calculator in with its USB cable and pick **USB Flash** on it: it shows up as a drive.
3. Copy the games you want (`.g3a` files) to it, then eject it.
4. Press **MENU** and pick a game.

Or pick just the games you want in [NumPlay Studio](https://mason363.github.io/NumPlay/customize/?calc=casio-cg).""",
     "casio-cg", "An add-in can be 2 MB at most, so Celeste, FNAF World and Champion Island aren't here."),
]

KEYS = {
    "ti84ce": [("Arrows", "move"), ("2nd", "OK"), ("enter", "EXE"), ("clear", "Back"), ("mode", "Home (save and quit)"),
               ("del", "⌫"), ("y=", "shift"), ("alpha", "alpha"), ("math", "toolbox"), ("vars", "var"),
               ("X,T,θ,n", "x,n,t"), ("0 to 9, + − × ÷ . , ( ) ^ x²", "the same")],
    "nspire": [("Arrows (the touchpad or clickpad)", "move"), ("enter, the touchpad's middle", "OK"), ("return", "EXE"),
               ("esc", "Back"), ("home, on", "Home (save and quit)"), ("del", "⌫"), ("shift", "shift"), ("ctrl", "alpha"),
               ("menu", "toolbox"), ("var", "var"), ("tab", "x,n,t"), ("0 to 9, + − × ÷ . , ( ) ^ x²", "the same")],
    "casio-cg": [("Arrows", "move"), ("EXE", "OK"), ("F1", "EXE"), ("EXIT", "Back"), ("MENU, AC/ON", "Home (save and quit)"),
                 ("DEL", "⌫"), ("SHIFT", "shift"), ("ALPHA", "alpha"), ("OPTN", "toolbox"), ("VARS", "var"),
                 ("X,θ,T", "x,n,t"), ("0 to 9, + − × ÷ . , ( ) ^ x²", "the same")],
}


def main():
    games = json.load(open(os.path.join(ROOT, "games", "games.json")))
    games.sort(key=lambda g: g["rank"])
    out = os.path.join(ROOT, "docs", "calculators")
    os.makedirs(out, exist_ok=True)
    for slug, target, name, photo, models, install, keys, note in PAGES:
        on = [g for g in games if target in g.get("ports", [])]
        lines = [f"# NumPlay on the {name}", "",
                 f'<img src="../media/calcs/{photo}" height="200" align="right" alt="{name}">', "",
                 f"{len(on)} free games for {models}: {', '.join(g['title'] for g in on[:6])} and more. "
                 "The same games as on the NumWorks, made for this calculator's screen and keys.", "",
                 "> [!IMPORTANT]",
                 f"> **Beta testers wanted.** These builds haven't run on a real {name} yet. "
                 f"[Sign up]({BETA}) to try each one first, and tell me what works.", "",
                 "## Install", "", install, "", "## Keys", "", "| Key | What it does |", "| --- | --- |"]
        lines += [f"| {k} | {v} |" for k, v in KEYS[keys]]
        lines += ["", "Games save by themselves, and when you leave with Home.", "", f"## The {len(on)} games", ""]
        if note:
            lines += [note, ""]
        lines += ["| Game | |", "| --- | --- |"]
        lines += [f"| [{g['title']}](../../games/{g['id']}/README.md) | {g['tagline']} |" for g in on]
        lines += ["", "<br clear=\"right\">", "",
                  f"[All calculators](../../README.md#pick-your-calculator) · [Run NumPlay on anything](../porting.md)", ""]
        open(os.path.join(out, slug + ".md"), "w").write("\n".join(lines))
        print(f"docs/calculators/{slug}.md: {len(on)} games")


if __name__ == "__main__":
    main()
