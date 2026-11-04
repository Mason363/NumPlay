# NumPlay on the TI-84 Plus CE

The TI-84 Plus CE, TI-84 Plus CE Python and TI-83 Premium CE, with the [CE C toolchain](https://ce-programming.github.io/toolchain/).

```
make port TARGET=ti84ce        (in a game's folder): build/ti84ce/<game>/bin/<GAME>.8xp
```

`CEDEV` points to the toolchain (default `~/Documents/Assets/toolchains/CEdev`).

| Key | NumWorks key |
| --- | --- |
| Arrows | Arrows |
| enter | OK |
| 2nd | EXE |
| clear | Back |
| mode, on | Home (save and quit) |
| del | ⌫ |
| alpha | alpha |
| y= | shift |
| math | toolbox |
| vars | var |
| x,T,θ,n | x,n,t |
| 0 to 9, + − × ÷ . , ( ) ^ x² ln log sin cos tan | the same |

Saves: the appvar `NP` + the game's id (6 letters), archived when the game ends.

The CE is slow (48 MHz) and has 154 KB of RAM: the classics and the light 2D games run on it.
