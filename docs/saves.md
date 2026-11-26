---
title: Saves in NumPlay Studio
description: "How NumPlay Studio backs up the saves of every NumPlay game from a NumWorks calculator over USB and puts them back, how it stays right for every new game, how it is tested, and how to try it without a calculator."
---

# Saves in NumPlay Studio

[NumPlay Studio](https://mason363.github.io/NumPlay/customize/?saves) has a **Saves** tab. Plug in a NumWorks and it shows every game's save on it, backs them all up in the browser, and puts them back after an update or a reset emptied the calculator.

This page is for whoever works on NumPlay. It says what Saves does, why a new game needs nothing more to be covered, how it talks to the calculator, how it is tested, and what to check on a real calculator.

## What it does

- **A backup each time the calculator is plugged in**, when its saves aren't the last backup's. Backups are kept in the browser, 40 for each calculator, and any of them downloads as a file.
- **A backup put back game by game.** Each game says whether it is the same on the calculator, different, or not there. The ones not there are ticked.
- **A game's save taken off the calculator**, after asking. It stays in the backups.
- **What fills the 42 KB**, game by game, with your Python scripts and other apps' files.
- **A file opened as a backup**, one of its own or a `numplay_saves.py` saved by NumWorks' website or another tool.

A save that is only in its copy (`numplay_saves.py`, or a big game's own `<game>_saves.py`) counts as there, since the game brings it back when it opens. So right after an update, nothing looks missing and nothing old is offered in its place.

## Why every new game is covered

One list feeds everything. A game names its files in `games/games.json` (`"records"`), as it already must for NumPlay's reset and its copy of the saves.

| From `records` | Made by | Used for |
| --- | --- | --- |
| the launcher's table | `tools/gen_games.py` | reset, `numplay_saves.py` |
| `kit.json`'s `"saves"` | `tools/kit.py` | Studio's Saves: whose save a file is |

So adding a game to `games.json` is all it takes. The kit is built again by CI on every push to main and the site is deployed with it.

Three things keep this true when something is forgotten.

- **A backup keeps every file**, whether or not the page knows whose it is. Only Epsilon's own kinds are left out (`sys`, `func`, `seq`, `exp`, `lis`, `mat`, `reg`, `pc`, `rgm`, `stm`, `eq`). A file no game names shows under "Other apps' files" and goes back like any other. A kit older than the page, or a game missing from it, loses a label and no save.
- **A backup is names and bytes.** It doesn't know a game's version or NumPlay's. A game that changes its save format reads its old saves as it always must, from a backup as from the calculator.
- **`tests/test_saves.py` fails** when a game names no file, or names one the page couldn't write. A name is `base.extension` with one dot, plain ASCII, 31 characters at most, and not one of Epsilon's extensions. A name ending with `*` stands for every file that starts with the rest.

## How it works

Three files in `customize/`, each with its own part.

| File | Its part |
| --- | --- |
| `saves.js` | The files as bytes. Reads the list, changes it, plans the writes, reads the copies, tells whose file it is, packs a backup. No browser needed. |
| `usb.js` | The calculator over WebUSB. Finds its files, reads them, makes a change and reads it back. |
| `saves-ui.js` | The tab, and the backups kept in IndexedDB. |

The page fetches the three when Saves is first opened, or right away with `?saves` in the address.

### The calculator's files

Epsilon keeps its files in 42 KB of RAM, between two magic words. Each file is a 16-bit size, its name and a zero, then its content. A zero size ends the list. `saves.js` changes this list exactly as the games do (`games/common/epsilon_files.h`), so what the page writes is what a game could have written.

### Over USB

Plugged in and out of any app, the calculator answers DFU requests from Epsilon itself, which waits meanwhile. This is how NumWorks' website reads and writes your scripts. What follows was read in Epsilon's source (`ion/src/device/shared/usb/dfu_interface.cpp` and `ion/src/device/shared/drivers/dfu_interfaces.cpp`).

- One interface with two settings, 0 for the flash and 1 for the RAM. Reads take any address under either. A write to RAM is a plain copy only under setting 1.
- A block is 2048 bytes at most, and a block written lands in one go.
- After any block is written, the address pointer is back at 0. So every block goes with its own "set address", as block 2.
- The same slip clears the error of a write that failed, and RAM addresses aren't checked. A write that "worked" proves nothing. So the page only writes inside the files (and the 8 bytes below), only after reading both magic words, and reads everything back.
- The files' address comes from the running software's header, which the slot info at the start of the RAM points to. `games/common/epsilon_app.h` describes it.

The page never writes flash, never restarts the calculator and never leaves DFU. Unplugging ends the session.

### A change that can't break the list

A change that fits one block is one write. A longer one is cut where the first changed file starts.

1. A zero size is written there. The list now ends before the change, shorter and whole.
2. Everything after those two bytes is written.
3. The two bytes are written. The list is whole again, with the change.

If the cable is pulled in between, the calculator has the files before the change and no broken list. The backup made a moment before has the rest, and the page offers it when the calculator comes back.

### Epsilon's memory of the last file

Epsilon remembers the last file it looked up, by a checksum of its name and a pointer, in the RAM after the files. If a file moves and that pointer isn't cleared, Epsilon reads the wrong bytes. The games clear it before moving a file (`ef_cache` in `epsilon_files.h`), and the page does the same over USB, found the same way. It is cleared before the first write, never after.

If it can't be found (a software laid out differently), the page still adds files and rewrites them in place, which moves nothing. It refuses what would move a file and says so. Putting saves back after a reset only adds files, so it always works.

The same three words hold the size Epsilon may use. In exam mode it is smaller, and the page writes nothing.

### NumPlay's copy

The page never writes `numplay_saves.py`. NumPlay makes it again when it opens. When a game's save is taken off, its lines in the copy are struck out in place (one byte each, nothing moves), or NumPlay would bring the save back.

## The tests

```bash
python3 tests/test_saves.py build/compiled/NumPlay.nwa
```

It needs Node and a C compiler. CI runs it with the other tests (`tests/ci.py`), and once more in the build job against the kit (`--kit`).

| What | How |
| --- | --- |
| Every game names its files | `games/games.json` against the rules above |
| The list changes as the games change it | Thousands of random changes through `saves.js` and through `epsilon_files.h` itself, built for the computer (`tests/saves/files.c`). The bytes must match after each one |
| A change never leaves a broken list or half a file | Every write of every plan is applied one at a time and the list read after each |
| The copies of saves | `np_lz.h` itself packs, `saves.js` unpacks. And the launcher's real copy, written in the emulator |
| The calculator over USB | `tests/saves/fake_calculator.js` answers as Epsilon's DFU code does, state by state. Ten kinds of calculator and software |
| The cable pulled | At every request of a change, about 1800 times. Whole files each time, Epsilon's memory never left pointing at a moved file, and the change again goes through |
| What must be refused | Exam mode, a software whose memory can't be found, files that changed since they were read, a write that did nothing |
| The launcher agrees | In the emulator. A save the page deleted stays deleted when NumPlay opens. Everything the page put back after a reset is there, and NumPlay's copy follows |

The fake calculator is strict where a real one is silent. A write outside the files, a write to RAM through the flash setting, a read of memory that isn't there, an erase, a restart are each a crash there, and the test fails.

## Try it without a calculator

Serve the repository and open Studio.

```bash
python3 -m http.server 8777
```

Open `http://localhost:8777/customize/?saves`, then run this in the browser's console.

```js
document.head.appendChild(Object.assign(document.createElement('script'), { src: '/tests/saves/try.js' }))
```

Press "Connect your calculator". `fake` in the console is the calculator. `fake.reset()` empties it, `fake.unplug()` and `fake.plug()` are the cable, `fake.unplugAfter(120)` pulls it in the middle of the next change. `tests/saves/try.js` lists the rest.

Studio needs a kit in `customize/kit/` to start. CI makes it. The site's own can be copied from the `kit` branch.

## On a real calculator

```bash
node tests/saves/calculator.js --write
```

This runs `usb.js` and `saves.js` themselves against the calculator plugged into the computer, over Node's USB in place of the browser's (the `usb` package, which nwlink brings). Without `--write` it only reads. With it, the calculator's files are first kept in `build/calculator/`, then test files are added after its own, rewritten, slid, the storage filled to the last byte, and everything put back as it was, to the byte. Each change is read back. The calculator's own files must not move, and no byte after the files may change but Epsilon's memory of the last file. It stops at the first thing that isn't right.

Run it on each model, and again when a new Epsilon comes out.

| Model | Epsilon | Over USB (`calculator.js --write`) | On the calculator after | By |
| --- | --- | --- | --- | --- |
| N0120 | 26.3.0 | passed, 2026-11-25. A backup of 62 real save files of 29 games also went on in 182 ms, two games came off, and the calculator was put back as it was | not yet | Mason's calculator |
| N0115 | | not yet | not yet | |
| N0110 | | not yet | not yet | |

What the script can't see is the calculator's own screen. After a run, and the first time Saves is used from the page on a model:

1. Unplug the calculator. It works as usual, and its Python scripts open.
2. In Studio, connect and take off the save of a small game. Unplug, open NumPlay. That game starts fresh and the others kept their saves.
3. Put it back. Unplug, open the game. The save is there.
4. Press the reset button on the back. Connect, put everything back. Unplug, open NumPlay. Every save is there.
5. Start a put back and pull the cable. Plug it back in. The page offers the backup from before, and putting it back works.

If a step fails, the browser's console has the request that failed. Open an issue with it, the model and Epsilon's version.

## Limits

- **The NumWorks only.** The other calculators keep their saves their own way, and Saves doesn't read them.
- **Chrome or Edge**, on a computer or an Android phone. Safari, Firefox and iPhones have no WebUSB.
- **On Linux** the browser needs the udev rule NumWorks' website asks for, or the calculator can't be opened.
- **Upsilon** is read the same way (its header and its memory of the last file are told apart), but nobody has tried it on one.
- **Exam mode** hides the files. The page reads what is left and writes nothing.
- **Backups live in the browser.** Clearing its data clears them. The ones that matter should be downloaded.
- **After a put back, open NumPlay once** before installing other apps. Until then NumPlay's copy of the saves isn't on the calculator.

## Where things are

| File | What it is |
| --- | --- |
| `customize/saves.js`, `usb.js`, `saves-ui.js` | Saves |
| `tools/kit.py` | Writes `kit.json`'s `"saves"` from `games.json` |
| `tests/test_saves.py` | The test |
| `tests/saves/files.c` | The games' file code and packer, built for the computer |
| `tests/saves/check.js`, `usb_check.js` | The checks of `saves.js` and `usb.js` |
| `tests/saves/fake_calculator.js` | A calculator that answers as Epsilon does |
| `tests/saves/apply.js` | Changes a list as the page does, for the emulator |
| `tests/saves/try.js` | The fake calculator in the page |
| `tests/saves/calculator.js` | The check on a real calculator |
