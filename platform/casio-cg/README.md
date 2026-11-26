# NumPlay on the Casio fx-CG50

The fx-CG50, Graph 90+E, fx-CG10 and fx-CG20, as add-ins (`.g3a`), with [libfxcg](https://github.com/Jonimoose/libfxcg) and newlib on GCC for SuperH.

```
make port TARGET=casio-cg        (in a game's folder): build/casio-cg/<game>.g3a
```

`FXCG` points to the folder `toolchain.sh` fills: GCC and binutils for `sh3eb-elf`, newlib built for the calculator's calling convention, libstdc++, libfxcg. No Planet Casio tools are needed. `platform/casio-cg/toolchain.sh DIR` builds them in DIR from their sources, on Linux or macOS (about an hour).

The game's 320 x 240 picture sits in the middle of the 384 x 216 screen, one row in ten left out. An add-in can be 2 MB at most: Celeste, FNAF World and Champion Island are bigger.

The screen: a game draws into the calculator's screen memory, sent to the LCD whole (`Bdisp_PutDisp_DD`, which the calculator waits for) where a frame ends: where the game waits (`eadk_display_wait_for_vblank`, `eadk_timing_msleep`, `eadk_event_get`) or, in a game that doesn't wait between frames, where it reads the keys. A frame drawn for long with neither (a loading screen) is shown as it goes, after twice the time between the last two frames (300 ms to 1 s). As on the TI-Nspire.

Floats: the SuperH has no FPU. `np_float.c` does single precision arithmetic in place of libgcc's, which unpacked each number into a struct (two hundred instructions an addition), with the same results in a third to a half of the time.

| Key | NumWorks key |
| --- | --- |
| Arrows | Arrows |
| EXE | OK |
| F1 | EXE |
| EXIT | Back |
| MENU, AC/ON | Home (save and quit) |
| DEL | ⌫ |
| SHIFT | shift |
| ALPHA | alpha |
| OPTN | toolbox |
| VARS | var |
| X,θ,T | x,n,t |
| 0 to 9, + − × ÷ . , ( ) ^ x² log ln sin cos tan EXP | the same |
| (−) | Ans |
| a b/c, F↔D, → , F2 | e^x, π, √, i |

Saves: `<game>.sav` in the storage memory, written when the game saves (a few seconds apart at most) and when it ends.
