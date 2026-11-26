---
title: Make your own NumWorks calculator game
description: "How to make a game or app for the NumWorks calculator (N0110, N0115, N0120) in C: set up on macOS, Windows or Linux, start from NumPlay's template, draw in 2D or 3D, save, test in an emulator, and share your .nwa."
---

# Make your own NumWorks game

NumPlay is a collection of games for the NumWorks calculator, and it is also a kit for making your own. Every piece its games use is here for you to use too: a 2D renderer that fits the calculator's small memory, fonts and text in three languages, saves that survive a reinstall, compression, 3D renderers, physics, an emulator to test without a calculator, and tools for icons and screenshots. It's all free and open source (GPL-3.0).

Games are written in C (Rust works too: see Tetris). A game becomes a `.nwa` file, a NumWorks app, that anyone can install from [my.numworks.com/apps](https://my.numworks.com/apps).

## 1. Get ready (10 minutes)

You need [Python 3](https://www.python.org/downloads/), [Node.js](https://nodejs.org) and git. Then, from a copy of NumPlay:

```bash
git clone https://github.com/Mason363/NumPlay.git
```

```bash
python3 NumPlay/tools/setup.py
```

`tools/setup.py` downloads the ARM compiler the calculator needs (xPack's arm-none-eabi-gcc, about 1 GB unpacked), nwlink (NumWorks' tool that makes `.nwa` apps) and the Python packages of the tools, all into one folder in your home, `~/.numplay`. No administrator password, nothing changed elsewhere. It works on:

| Computer | Notes |
| --- | --- |
| **macOS** (Apple silicon or Intel) | If `make` is missing, run `xcode-select --install` first. Then, in each new terminal: `source ~/.numplay/env.sh` |
| **Linux** | Same as macOS: `source ~/.numplay/env.sh` |
| **Windows** | `tools/setup.py` prints the PowerShell line to use. `python tools/build.py` builds NumPlay natively (it gets GNU make for you), or use WSL |

## 2. Start a game

```bash
python3 tools/new_game.py "My Game"
```

This copies NumPlay's template, **Star Catcher** (`sdk/template`), to `games/mygame/`. It's a whole small game in one file, already doing what every game should: it draws, reads the keys, runs at a steady speed on every calculator, pauses with Back, quits with Home, and saves the best score. Build it:

```bash
make -C games/mygame
```

You get `games/mygame/output/mygame.nwa`. Now change `games/mygame/src/main.c` into your game.

## 3. Try it without a calculator

`tools/emu.py` runs a `.nwa` in an emulator of the calculator's ARM chip, with its screen, keys, clock and files. It takes pictures and GIFs, so you can see your game without installing it:

```bash
python3 tools/emu.py games/mygame/output/mygame.nwa --ms 6000 --keys "1500-1600:ok,2000-4000:right" --shots 1400,3900 --out shots
```

To play it with your mouse and keyboard instead, open it in the live emulator, a calculator in a window (made by LilyTheCuteCatgirl). With `--live` it builds your game again each time you save a file and reloads it:

```bash
python3 tools/emulator.py games/mygame/output/mygame.nwa
```

`tools/emu.py`'s `--keys` holds keys over time ranges in milliseconds (`ok`, `back`, `left`, `up`, `exe`, `home`, `1`...`9`, `shift`, `alpha`...). `--gif 0-5000` records a GIF, `--model n0110` emulates the slower N0110 and N0115 (if your game is smooth there, it's smooth everywhere), `--system upsilon` emulates Upsilon. It runs at about the speed of an N0110.

## 4. Put it on a calculator

Plug the calculator in, open [my.numworks.com/apps](https://my.numworks.com/apps) in Chrome or Edge, and drop your `.nwa` there. Or, from the terminal: `nwlink install-nwa games/mygame/output/mygame.nwa`.

## What you can build on

| What | Where | Used by |
| --- | --- | --- |
| **2D drawing** in 16-row strips, so no frame buffer has to fit in RAM: rectangles, rounded boxes, circles, gradients, see-through fills, palette pictures, masks | `launcher/src/gfx.h` | Block Blast, Sokoban, Sudoku, Space Invaders, 2 Players, the template |
| **Text**: NumPlay's three fonts, centered, right-aligned and word-wrapped | `launcher/src/gfx.h`, `font.h` | every small game |
| **Translations**: mark texts with `T("...")`, add `lang/fr.txt` and `lang/zh.txt`, and the French and Chinese builds are made for you (accents and Chinese letters included) | `games/common/np_text.h`, `tools/lang.py` | every game in NumPlay |
| **Saves**: files in the calculator's storage, read and written in place | `games/common/epsilon_files.h` (`ef_read`, `ef_write`) | almost every game |
| **Saves that survive a reinstall**: a copy kept in a Python script, which the NumWorks website keeps | `games/celeste/src/save.c` (`save_copy`) | Celeste, Hollow Knight, Undertale, the big games |
| **Safety**: Home handled cleanly, the slower models and Upsilon recognised, keys let go before quitting | `games/common/epsilon_app.h` | every game |
| **Compression**: LZ for pictures, DEFLATE for levels | `games/common/np_lz.h`, `launcher/src/inflate.c` | Chrome Dino, Sokoban, NumDash |
| **3D**: perspective-correct textured triangles drawn a few columns at a time, each sent in step with the screen's refresh so nothing tears, with fog and light | [`src/render.c`](https://github.com/Mason363/NumWorks-Super-Mario-64/blob/main/src/render.c) in NumWorks-Super-Mario-64 | Super Mario 64 |
| **3D**: shaded, textured models and scenery | `games/subway/src/r3d.c` | Subway Surfers |
| **Voxels**: a Minecraft-style world renderer | `games/numblocks/src/render.c` | NumBlocks |
| **Physics**: rigid bodies, joints, contacts and raycasts | `games/numdrive/src` | NumDrive |
| **Vehicle physics** on hills | `games/hillclimb/src` | Hill Climb Racing |
| **A port of a C game engine** running on the calculator: Doom, Sonic, and Super Mario 64's decompilation ([its own repository](https://github.com/Mason363/NumWorks-Super-Mario-64)) | `games/doom`, `games/sonic` | |

And the tools, all in `tools/`:

| Tool | What it does |
| --- | --- |
| `setup.py` | Gets the compiler, nwlink and the Python packages |
| `new_game.py` | Starts a game from the template |
| `emu.py` | Runs a `.nwa` in an emulator: pictures, GIFs, scripted keys, files kept between runs, profiling |
| `emulator.py` | The emulator in a window: click the keys or use your keyboard; `--live` rebuilds when you save, with `build.py` |
| `build.py` | Builds NumPlay on macOS, Linux and Windows and puts the finished apps in `build/compiled/`; `--game ID` builds one game, `--watch` builds again when you save |
| `icon.py` | Makes the 55 x 56 app icon from any picture, with the rounded corners every NumPlay icon has |
| `record.py` | Makes GIFs from the emulator's frames |
| `sizes.py` | How much flash and RAM each game takes inside NumPlay |
| `lang.py` | Builds the French and Chinese versions |
| `../tests/test_games.py` | Plays every app in the emulator and checks its saves survive a reinstall |
| `../tests/test_saves.py` | Checks [NumPlay Studio's Saves](saves.md) reads every game's save files and puts them back as the calculator does |

## What the calculator gives you

| | |
| --- | --- |
| **Screen** | 320 x 240 pixels, 16-bit colour (RGB565) |
| **Speed** | N0120: 550 MHz. N0110 and N0115: 216 MHz, about 2.5 times slower. Run your game's logic at a fixed rate and catch up when drawing is slow, so it plays at the same speed everywhere |
| **RAM** | About 133 KB for your app on every recent NumWorks software (148 KB on most), plus a 32 KB stack; 107 KB on Upsilon. Put big buffers on `main`'s stack, not in global variables |
| **Space** | About 1.9 MB of flash for all installed apps together. Small games are 5 to 50 KB |
| **Files** | About 42 KB of storage for every app's saves and the user's scripts |
| **Keys** | The arrows, OK, Back, Home and the 40 others. No touch screen |
| **Sound** | None: the calculator has no speaker |

## Make it a good game

The games in NumPlay follow a few rules, and players notice when one doesn't:

- **The keys are obvious.** Arrows move, OK or EXE acts, Back pauses or goes back, Home quits from anywhere. Write the keys on the screen when there is anything to learn.
- **The pause menu fits the game.** Draw it in your game's own look, with a Quit game choice that asks first.
- **It never breaks.** No crash, no lost save: save as packed bytes with a version number, and read old or broken files without crashing.
- **It's fast.** 30 frames a second on the N0120, and the same game speed on the N0110.
- **Credit honestly.** If your game is based on another, say so and name the original's makers, and say you are not affiliated with them.

## Share it

Your game is yours: put the `.nwa` anywhere. If you'd like it in NumPlay, read [CONTRIBUTING.md](../CONTRIBUTING.md) and open an issue first, so we can talk about it before you build it.
