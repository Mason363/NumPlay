# NumPlay on the TI-Nspire

The TI-Nspire CX and CX II (and their CAS models), with [Ndless](https://ndless.me).

```
make port TARGET=nspire        (in a game's folder): build/nspire/<game>.tns
```

`NDLESS_SDK` points to Ndless's `ndless-sdk` folder, with its toolchain built (`toolchain/build_toolchain.sh`), then `make build-libsyscalls build-libndls build-thirdparty build-tools` in it. `platform/nspire/toolchain.sh DIR` does all of it from Ndless's source, on Linux or macOS (about an hour): `NDLESS_SDK` is then `DIR/ndless-sdk`.

| Key | NumWorks key |
| --- | --- |
| Arrows (touchpad or clickpad) | Arrows |
| enter, the touchpad's middle (on its own: not with an arrow of the pad, nor within 150 ms of one) | OK |
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
| doc, cat, =, trig, templates, flag, scratchpad, 10^x, θ, " ' : \| > < ? | nothing |

A text file `numplay_keys.tns`, next to the game or in `/documents/ndless`, changes them for every game: lines `nspire key = numworks key` (`del = back`, `click = none`), read when the game starts ([docs/calculators/ti-nspire.md](../../docs/calculators/ti-nspire.md#your-own-keys) has the names). The game's folder comes from `main`'s `argv[0]`: `port.mk` links `main` through `__wrap_main` in `np_nspire.c`.

The screen: a game draws into a buffer, shown whole where a frame ends: where the game waits (`eadk_display_wait_for_vblank`, `eadk_timing_msleep`, `eadk_event_get`) or, in a game that doesn't wait between frames, where it reads the keys. A frame drawn for long with neither (a loading screen) is shown as it goes, after twice the time between the last two frames (300 ms to 1 s). A game reading the keys while it draws, and waiting only sometimes, marks its frames' end with `eadk_timing_msleep(0)` (NumDash).

Saves: every game's in one file, `/documents/ndless/numplay.sav.tns` (the file browser slows down with each file there is). It's a log, `NPS1` then entries (the id's length in a byte, the game's files id, the records' size and an FNV-1a check in 4 bytes each, the records): a save adds the game's records at the end, and the log is written again with each game's last entry when it's over 64 KB and twice what those take, or after an entry cut short (into `numplay.sav.new.tns`, then renamed). A game's own `numplay_<game>.sav.tns`, from an older NumPlay or written when the log couldn't be, is read instead, added to the log and removed.

Time: the second fast timer counts at 32768 Hz while a game runs and is put back after. Waits read it: never Ndless's `msleep`, which waits for the timer's interrupt with every other one masked, ON's too, and on real calculators hung for good in the games that call it hundreds of times a second. `tools/nspire_emu.py` reports a call to it.
