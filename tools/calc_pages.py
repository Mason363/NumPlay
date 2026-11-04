#!/usr/bin/env python3
"""docs/calculators/<calculator>.md, one page a calculator: how to install NumPlay's games on it, its keys, and the
games that run on it (games/games.json "ports"). Run it after a game gets a new target.

  python3 tools/calc_pages.py"""
import json
import os
import re
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REL = "https://github.com/Mason363/NumPlay/releases/latest"
BETA = "https://github.com/Mason363/NumPlay/issues/new?template=beta-tester.yml"

PAGES = [
    ("ti-84-plus-ce", "ti84ce", "TI-84 Plus CE", "ti84ce.png",
     "the TI-84 Plus CE, TI-84 Plus CE Python, TI-84 Plus CE-T and TI-83 Premium CE",
     f"""1. Get `NumPlay-TI84CE.zip` from the [latest release]({REL}) and unzip it.
2. Send the games you want (`.8xp` files, and the `.8xv` files of Balatro and FNAF) with [TI Connect CE](https://education.ti.com/en/products/computer-software/ti-connect-ce-sw).
3. Press **prgm**, pick a game, press **enter**.

On OS 5.5 and newer, programs like these need [arTIfiCE](https://yvantt.github.io/arTIfiCE/) first. Each game also needs the CE C libraries (`clibs.8xg` from [the CE toolchain's releases](https://github.com/CE-Programming/libraries/releases/latest)), sent once. Balatro and FNAF need most of the RAM free: if one doesn't start, archive your other programs.

Or pick just the games you want in [NumPlay Studio](https://mason363.github.io/NumPlay/customize/?calc=ti84ce).""",
     "ti84ce", "The CE is the slowest of these calculators (48 MHz, no floating point unit) and has the least memory, so it gets the lighter games, and Balatro and Five Nights at Freddy's in versions made for it."),
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

INSTALL_NW = f"""You need your calculator, its USB cable, and a computer with **Google Chrome** or **Microsoft Edge**.

1. **Download** [NumPlay.nwa]({REL}/download/NumPlay.nwa), or a big game below. Want other games in it? Pick them in [NumPlay Studio](https://mason363.github.io/NumPlay/customize/) and it makes a NumPlay.nwa with just those.
2. **Plug** your calculator into the computer and turn it on.
3. **Go to** [my.numworks.com/apps](https://my.numworks.com/apps) and log in. No account? Sign up there, it's free.
4. Click **Connect**, then pick your calculator in the small window that opens.
5. **Add** the file you downloaded, then click **Install**. Keep the calculator plugged in until it's done.
6. **Play!** Press **Home**, go to the end of the apps and open **NumPlay**.

Updating? Install the new file the same way: your progress comes back by itself. On Upsilon, see [Other versions](../../README.md#how-to-install)."""

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


def numworks(games, out):
    """The NumWorks's page: NumPlay.nwa's games (the Makefile's GAMES) and the big games, each with its download."""
    mk = open(os.path.join(ROOT, "Makefile")).read()
    apps = dict(re.findall(r"^APP_(\w+) = \S+:(\S+\.nwa)$", mk, re.M))
    made = subprocess.run(["make", "-s", "-p", "-n", "check"], cwd=ROOT, capture_output=True, text=True).stdout
    inside = re.search(r"^GAMES := (.*)$", made, re.M).group(1).split()
    byid = {g["id"]: g for g in games}
    small = [byid[i] for i in inside]
    big = [g for g in games if g.get("xl") and g["id"] in apps]
    lines = ["# NumPlay on the NumWorks", "",
             '<img src="../media/calcs/numworks.png" height="200" align="right" alt="NumWorks">', "",
             f"{len(small)} free games in one app, NumPlay.nwa, and {len(big)} big games that install on their own. "
             "For the N0110, N0115 and N0120.", "",
             "## Install", "", INSTALL_NW, "", "## Keys", "",
             "The games use the calculator's own keys: arrows to move, **OK** and **EXE** to act, **Back** to pause or go back, "
             "and **Home** to save and quit.", "",
             f"## The {len(small)} games in NumPlay.nwa", "", "| Game | |", "| --- | --- |"]
    lines += [f"| [{g['title']}](../../games/{g['id']}/README.md) | {g['tagline']} |" for g in small]
    lines += ["", f"## The {len(big)} big games", "", "Each one installs on its own, the same way.", "",
              "| Game | | Download |", "| --- | --- | --- |"]
    lines += [f"| [{g['title']}](../../games/{g['id']}/README.md) | {g['tagline']} | [{apps[g['id']]}]({REL}/download/{apps[g['id']]}) |"
              for g in big]
    lines += ["", '<br clear="right">', "",
              "[All calculators](../../README.md#pick-your-calculator) · [How to play each game](../play.md)", ""]
    open(os.path.join(out, "numworks.md"), "w").write("\n".join(lines))
    print(f"docs/calculators/numworks.md: {len(small)} games, {len(big)} big games")


def main():
    games = json.load(open(os.path.join(ROOT, "games", "games.json")))
    games.sort(key=lambda g: g["rank"])
    out = os.path.join(ROOT, "docs", "calculators")
    os.makedirs(out, exist_ok=True)
    numworks(games, out)
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
