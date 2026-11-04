# NumPlay's platforms

Every NumPlay game talks to its calculator through one interface: [EADK](include/eadk.h), the NumWorks's app interface (a 320 x 240 screen, its keypad, a clock), and a few more functions in [np_port.h](include/np_port.h) (files, the start and end of the app). On a NumWorks they are the calculator's own. Every other target implements them in its folder here, so a game is written once and builds everywhere.

| Target | Folder | Builds | Status |
| --- | --- | --- | --- |
| NumWorks | (the calculator's own EADK) | `.nwa` | every game |
| A computer, a Raspberry Pi | [sdl](sdl/) | a program | every game |
| A web browser | [sdl](sdl/) with Emscripten | a page | every game but Undertale and Deltarune |
| TI-Nspire CX, CX II | [nspire](nspire/) | `.tns` | every game |
| Casio fx-CG50, Graph 90+E | [casio-cg](casio-cg/) | `.g3a` | every game up to 2 MB |
| TI-84 Plus CE, TI-83 Premium CE | [ti84ce](ti84ce/) | `.8xp` | the light games |
| Casio Graph 35+E II | casio-fx | `.g1a` | later |

`games/games.json` lists each game's targets (`"ports"`). `python3 tools/ports.py build <target>` builds them all, `python3 tools/ports.py run` plays each computer build for a few seconds without a window and saves its screens.

## A game on another target

A game's Makefile names its files and includes [port.mk](port.mk):

```make
PORT_ID = motox3m
PORT_NAME = Moto X3M
PORT_SRC = $(filter-out src/plat_host.c,$(wildcard src/*.c))
PORT_ASM = src/data.S
PORT_FLAGS = -Isrc -DEPSILON_XL=1
include ../../platform/port.mk
```

Then `make port TARGET=sdl`. Built for another target, `NP_PORT` is defined: the app's name and icon sections (`eadk_app_name`...) stay out (`#ifndef NP_PORT`).

## A new target

Implement the functions of `include/eadk.h` and `include/np_port.h` in `platform/<target>/`, and its rule in `port.mk`. [sdl/np_sdl.c](sdl/np_sdl.c) is the model: about 300 lines.

- **Screen:** keep a 320 x 240 RGB565 picture; show it (scaled, or its middle on a smaller screen).
- **Keys:** return the NumWorks keys held, as bits (`eadk_key_*`). Map yours onto them: arrows, OK, Back, Home at least.
- **Time:** milliseconds since the start; sleep.
- **Files:** one buffer of records, kept when `np_storage_changed()` is called.
- **Heap:** `_heap_start` and `_heap_end` around the RAM a game may use beyond its own.
- **Data:** a game's `.S` data files assemble as they are; for WebAssembly, `embed.py` turns them into C.

## Writing for every target

- **An `int` is 24 bits on the TI-84 Plus CE.** Use `uint32_t` for bit masks and anything past 8 million: `(uint32_t)1 << 31`, not `1u << 31`. The CE build stops on a shift past an int's width.
- **The Nspire, the fx-CG50 and the CE have no floating-point unit.** Their float is done in software, dozens of times slower than the NumWorks's, and so is division. `NP_SOFT_FLOAT` is 1 there: put integer versions of the hot loops under `#if NP_SOFT_FLOAT`, so the NumWorks build stays as it is, with the helpers of [np_fixed.h](include/np_fixed.h) (a float's value as an integer, a reciprocal, a square root). They may also keep what they decode, the Nspire having 64 MB. `make port TARGET=sdl NP_SOFT_FLOAT=1` tries them on a computer.
- Keep every call to the calculator in one file (`plat_eadk.c`), as the games with a `plat.h` do.
