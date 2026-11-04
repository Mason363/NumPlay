# NumPlay on the TI-Nspire

The TI-Nspire CX and CX II (and their CAS models), with [Ndless](https://ndless.me).

```
make port TARGET=nspire        (in a game's folder): build/nspire/<game>.tns
```

`NDLESS_SDK` points to Ndless's `ndless-sdk` folder, with its toolchain built (`toolchain/build_toolchain.sh`), then `make build-libsyscalls build-libndls build-thirdparty build-tools` in it.

| Key | NumWorks key |
| --- | --- |
| Arrows (touchpad or clickpad) | Arrows |
| enter, the touchpad's middle | OK |
| return (↵) | EXE |
| esc | Back |
| home, on | Home (save and quit) |
| del | ⌫ |
| shift | shift |
| ctrl | alpha |
| menu | toolbox |
| var | var |
| tab | x,n,t |
| 0 to 9, + − × ÷ . , ( ) ^ x² π sin cos tan EE (−) | the same (−) is Ans |

Saves: `/documents/ndless/numplay_<game>.sav.tns`.

Time: the second fast timer counts at 32768 Hz while a game runs and is put back after. To check on a CX II.
