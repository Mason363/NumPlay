# NumPlay on the TI-84 Plus CE

The TI-84 Plus CE, TI-84 Plus CE Python and TI-83 Premium CE, with the [CE C toolchain](https://ce-programming.github.io/toolchain/) (v15).

```
make port TARGET=ti84ce        (in a game's folder): build/ti84ce/<game>/bin/<GAME>.8xp
```

`CEDEV` points to the toolchain (default `~/Documents/Assets/toolchains/CEdev`). `platform/ti84ce/toolchain.sh DIR` puts its release (v15.0) in DIR, on Linux or macOS. On the calculator the games need the CE C libraries (`clibs.8xg`), and on OS 5.5 or later [arTIfiCE](https://yvantt.github.io/arTIfiCE/) to start them.

| Key | NumWorks key |
| --- | --- |
| Arrows | Arrows |
| 2nd | OK |
| enter | EXE |
| clear | Back |
| mode, on | Home (save and quit) |
| del | ⌫ |
| alpha | alpha |
| y= | shift |
| math | toolbox |
| vars | var |
| x,T,θ,n | x,n,t |
| (−) | ans |
| 0 to 9, + − × ÷ . , ( ) ^ x² ln log sin cos tan | the same |

2nd is OK because it sits by the arrows, as OK does on a NumWorks, and enter is EXE at the bottom right. An arrow or del held down repeats, as on a NumWorks. ×10ˣ, eˣ, i, π and √ have no key: no game needs them.

Saves: the appvar `NP` + the game's id, its first 5 letters and its last when it's longer than 6 (`NPflappd` for Flappy Bird), archived when the game ends.

## Room

A CE program is unpacked into RAM when it starts. It has about 150 KB for itself (stored and unpacked), 60 KB for its static data, a 4 KB stack, and an `int` of 24 bits. Each build prints what the game takes and stops when it's too much (`check.py`). In a game's folder,

```
python3 ../../platform/ti84ce/check.py --ints build/ti84ce/<game>
```

lists what loses bits only with a 24-bit `int`: conversions, shifts, and the casts to `int` to look at.

The CE is slow (48 MHz, and `float` is done in software): the classics and the light 2D games run on it.

## Testing without a calculator

`tools/ce_emu.py` runs a CE program on [CEmu](https://github.com/CE-Programming/CEmu)'s core (the eZ80, the LCD controller and panel, the keypad, the timers, the flash and the memory protection, as on the calculator) with a small OS of its own in place of TI's ROM, so no ROM dump is needed. Its first run builds it (`tools/ce_emu`, needs CEdev and a C compiler; CEmu is fetched into `~/Documents/Assets/toolchains/ce-src`) and gets the CE C libraries into `~/Documents/Assets/toolchains/ce-libs`.

```
python3 tools/ce_emu.py prog.8xp [vars.8xv ...] --keys "1500-1600:2nd,3000:clear" --shots 1000,4000 --ms 6000 --out DIR
python3 tools/ce_emu.py game snake --keys ... --shots ...     a game's CE build
python3 tools/ce_emu.py sheet [game ...]                      every CE build for 40 s, build/ce_emu/sheet.png
python3 tools/ce_emu.py autotest test/.../autotest.json        a CE toolchain test, its screens' CRCs checked
```

Keys are the CE's (`2nd mode del alpha enter clear up down left right 0`-`9` ...) or the NumWorks's on NumPlay's layout (`ok exe back home`), held from one time to another in milliseconds. It says how the run ended, `ok` (the time ran out), `exited`, `crash` (a write to flash or past the stack, an invalid instruction or a jump to 0, unmapped memory), `hang`, `os-error` or `unimplemented` (an OS routine `ce_emu` doesn't have yet, named), then the frame rate a real CE gets, the work a frame takes, the stack used and the RAM left.

- `--frames-from MS` counts frames from then on, past a slow title. A frame is a call to `eadk_display_wait_for_vblank`, or a key scan once the screen changed (when the build inlined it), or a graphx buffer swap.
- `--profile 15` names the functions the time goes to.
- `--vars-out DIR` keeps the appvars at the end; give them back to the next run to test saves.
- `--ram NAME` puts a variable in RAM instead of the archive.

TI's float routines (`float`, `sqrtf`...) are done by the host at an estimated cost, and the report says how much of the time they took (`--float-scale` changes it). TI-OS's own screens and fonts aren't drawn (the home screen's text is written with NumPlay's font and listed in the report), and TI-OS's menus, the parser and USB aren't there.
