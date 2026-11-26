# Contributing to NumPlay

Thanks for wanting to help! Bug reports, game ideas, fixes, translations and whole games are all welcome. This page says how, from setting up your computer to opening a pull request.

**Just want to make your own NumWorks game?** Read [Make your own NumWorks game](docs/make-a-game.md): it starts from NumPlay's template and doesn't need any of the rules below.

## Before you start

- **Found a bug?** [Open a bug report](https://github.com/Mason363/NumPlay/issues/new/choose) with your calculator's model and software (Settings, then About), the NumPlay version and what happened.
- **Want a game?** Check the [roadmap](README.md#roadmap) first, then [ask for it](https://github.com/Mason363/NumPlay/issues/new/choose), one game per issue.
- **Want to make a game for NumPlay?** Open an issue first and say what you'd like to build and how. Space on the calculator is short, so we'll agree on where it goes (inside NumPlay or as its own app) before you spend time on it.
- **Want to work on an issue?** Say so in a comment and it's yours: an issue with someone assigned is taken, any other is free. Issues are assigned only to whoever is working on them.
- **Want to talk it over first?** Use [Discussions](https://github.com/Mason363/NumPlay/discussions): what you're working on, questions, ideas. Changes to `main` are listed under [commits](https://github.com/Mason363/NumPlay/commits/main); pull before you start.
- Small fixes (a typo, a crash, a clearer key hint) can go straight to a pull request.

## Set up your computer

NumPlay builds on **macOS**, **Linux** and **Windows**.

```bash
git clone https://github.com/Mason363/NumPlay.git
```

```bash
python3 NumPlay/tools/setup.py
```

`tools/setup.py` gets the ARM compiler, nwlink and the Python packages into `~/.numplay` and tells you the one line that puts them on your PATH (`source ~/.numplay/env.sh` on macOS and Linux). On macOS, run `xcode-select --install` first if `make` is missing. Rust is needed only for Tetris: [rustup.rs](https://rustup.rs), then `rustup target add thumbv7em-none-eabihf`. On Windows, `python tools/build.py` builds NumPlay natively (`--doctor` checks your setup), or use WSL.

Then:

| Command | What it does |
| --- | --- |
| `make -j4 NWLINK=nwlink` | NumPlay, its versions and every game on its own, in `build/` |
| `make check NWLINK=nwlink` | Checks that NumPlay fits the calculator's space and every game its RAM |
| `make lang NWLINK=nwlink` | The French and Chinese versions |
| `make -C games/<game> NWLINK=nwlink` | One game on its own |
| `make -C games/<game> test` | That game's tests on your computer (most games have them) |
| `python3 tests/test_games.py build/compiled/games` | Plays every app in the emulator and checks its saves |
| `python3 tools/emu.py app.nwa --ms 5000 --shots 2000 --out shots` | Runs any `.nwa` in the emulator and takes pictures |
| `python3 tools/emulator.py app.nwa` | The emulator in a window, to play with the mouse and keyboard; `--live` rebuilds as you save |
| `python3 tools/build.py` | Builds everything and puts the finished apps in `build/compiled/` (`--game ID` for one game, `--watch` to build again when you save) |

## How the code is laid out

| Folder | What's in it |
| --- | --- |
| `launcher/` | NumPlay itself: the home screen, settings, saves, the 2D drawing every small game shares |
| `games/<game>/` | One game: `src/` (with `icon.png`), `Makefile`, `README.md`, `docs/` pictures, `lang/` translations, `tests/` |
| `games/common/` | What every game uses: Home safety, files, text, compression |
| `games/games.json` | The games in NumPlay, in the launcher's order, with their colours, screenshots and save files |
| `sdk/template/` | Star Catcher, the template new games start from |
| `tools/` | Builders, the emulator, icons, translations, sizes |
| `tests/` | Tests of NumPlay and every app |
| `docs/` | How to play, the guide to making games, the pictures |

A **small game inside NumPlay** follows Block Blast's pattern (`games/blockblast`): built with `-DNP_MODULE=1`, it draws with the launcher's `gfx_*` functions and fonts and saves with `np_file_read` and `np_file_write`, so it costs only its rules and its screen. On its own it compiles those files in. `tools/new_game.py "My Game" --numplay` starts one this way.

A **big game** builds as its own `.nwa` (like `games/finalearth2`), with a build for your computer to test it. It's built with `-DEPSILON_XL=1`, so in a NumPlay that goes past the calculator's app area (Nwagra) it still finds its saves.

## What every game needs

- **Faithful to what it's based on**: its rules, numbers, levels and look. Nothing cheap, nothing important missing.
- **Keys anyone gets**: arrows move, OK or EXE acts, Back pauses or goes back, Home quits from anywhere. The keys are written on the screen whenever there's something to learn.
- **A pause menu in the game's own look**, with Quit game, which asks first.
- **An icon** of 55 x 56 pixels with NumPlay's rounded corners: `python3 tools/icon.py picture.png games/<game>/src/icon.png`. For a port, start from the original's own icon.
- **Saves that always come back**: packed bytes with a magic and a version number; old versions read or reset cleanly, never a crash. Save on Home, on quitting and every so often while playing. Name every file the game writes in `games/games.json` (`"records"`): NumPlay's reset, its copy of the saves and [NumPlay Studio's Saves](docs/saves.md) all go by that list.
- **Every calculator**: it runs on the N0110, N0115 and N0120 (the first two are 2.5 times slower: keep the game's speed with a fixed tick rate and catch-up), fits the RAM (`make check`), and says so if it needs more than Upsilon gives (107 KB).
- **Speed**: 30 frames a second on the N0120.
- **Honest credits**: the original's makers, every borrowed picture, font and library, and "not affiliated with" them.

## Translations

Texts players read are marked `T("...")` in the code. Each game has `lang/fr.txt` and `lang/zh.txt`: one line per text, the English, a tab, then the translation. `make lang` builds NumPlay in French and Chinese from them. To fix or add a translation, edit those files; new texts in the launcher go in `launcher/lang/`.

## Testing

Before a pull request:

1. `make -C games/<game> test` if your game has tests, and add some if it hasn't: its rules against the original, a scripted playthrough, and a few hundred random-key runs that must not crash.
2. Run it in the emulator (`tools/emu.py`), also with `--model n0110`.
3. `make -j4 check NWLINK=nwlink` if you touched NumPlay or a game inside it.
4. `python3 tests/test_saves.py` if you added a game or changed the files one saves to: it checks NumPlay Studio can back them up and put them back.
5. Play the first minute as someone who has never seen the game.

If you can, try it on a real calculator too, and say which one in the pull request.

## Pull requests

- One subject per pull request: a fix, a game, a translation.
- Say what it does, how you tested it (computer, emulator, which calculator) and include a screenshot or GIF for anything you can see.
- Keep the README's plain style if you change it: short sentences a beginner understands.
- By sending a pull request you agree that your code is shared under NumPlay's [GPL-3.0 license](LICENSE).

Thank you!
