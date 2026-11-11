# NumPlay on the TI-Nspire

The TI-Nspire CX and CX II (and their CAS models), with [Ndless](https://ndless.me).

```
make port TARGET=nspire        (in a game's folder): build/nspire/<game>.tns
```

`NDLESS_SDK` points to Ndless's `ndless-sdk` folder, with its toolchain built (`toolchain/build_toolchain.sh`), then `make build-libsyscalls build-libndls build-thirdparty build-tools` in it. `platform/nspire/toolchain.sh DIR` does all of it from Ndless's source, on Linux or macOS (about an hour): `NDLESS_SDK` is then `DIR/ndless-sdk`.

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
| 0 to 9, + − × ÷ . , ( ) ^ x² π tan EE (−) | the same, (−) is Ans |
| e^x | e^x |
| A to Z, space, ?! | the NumWorks key with that letter (alpha's): A e^x, B ln, C log, D i, E , F ^, G sin, H cos, I tan, J π, K √, L x², M to O 7 to 9, P (, Q ), R to T 4 to 6, U ×, V ÷, W to Y 1 to 3, Z +, space −, ?! 0 |

Saves: `/documents/ndless/numplay_<game>.sav.tns`.

Time: the second fast timer counts at 32768 Hz while a game runs and is put back after. Waits read it: never Ndless's `msleep`, which waits for the timer's interrupt with every other one masked, ON's too, and on real calculators hung for good in the games that call it hundreds of times a second. `tools/nspire_emu.py` reports a call to it.
