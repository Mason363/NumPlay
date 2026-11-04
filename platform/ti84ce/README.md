# NumPlay on the TI-84 Plus CE

The TI-84 Plus CE, TI-84 Plus CE Python and TI-83 Premium CE, with the [CE C toolchain](https://ce-programming.github.io/toolchain/) (v15).

```
make port TARGET=ti84ce        (in a game's folder): build/ti84ce/<game>/bin/<GAME>.8xp
```

`CEDEV` points to the toolchain (default `~/Documents/Assets/toolchains/CEdev`). On the calculator the games need the CE C libraries (`clibs.8xg`), and on OS 5.5 or later [arTIfiCE](https://yvantt.github.io/arTIfiCE/) to start them.

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
