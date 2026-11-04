# NumPlay on the Casio fx-CG50

The fx-CG50, Graph 90+E, fx-CG10 and fx-CG20, as add-ins (`.g3a`), with [libfxcg](https://github.com/Jonimoose/libfxcg) and newlib on GCC for SuperH.

```
make port TARGET=casio-cg        (in a game's folder): build/casio-cg/<game>.g3a
```

`FXCG` points to the folder `toolchain.sh` fills: GCC and binutils for `sh3eb-elf`, newlib built for the calculator's calling convention, libstdc++, libfxcg. No Planet Casio tools are needed. `platform/casio-cg/toolchain.sh DIR` builds them in DIR from their sources, on Linux or macOS (about an hour).

The game's 320 x 240 picture sits in the middle of the 384 x 216 screen, one row in ten left out. An add-in can be 2 MB at most: Celeste, FNAF World and Champion Island are bigger.

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
