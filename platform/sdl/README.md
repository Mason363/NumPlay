# NumPlay on a computer

Windows, macOS, Linux and a Raspberry Pi, with [SDL 3](https://libsdl.org).

```
make port TARGET=sdl        (in a game's folder): build/sdl/<game>
```

| Key | NumWorks key |
| --- | --- |
| Arrows | Arrows |
| Enter | OK |
| Space, keypad Enter | EXE |
| Escape | Back |
| Home, closing the window | Home (save and quit) |
| Shift | shift |
| Alt | alpha |
| Tab | toolbox |
| Backspace, Delete | ⌫ |
| 0 to 9, + - * / . , [ ] | the same |
| V, X | var, x,n,t |

Saves go to `~/.numplay/<game>.files` (`NP_SAVES` to change). `NP_SCALE=1` to `6` sets the window's size.

**Tests:** `NP_HEADLESS=1` runs without a window. `NP_KEYS="1500:ok,3000-6000:up"` holds keys (milliseconds from the start), `NP_SHOTS="2000:title.ppm"` saves the screen, `NP_QUIT_MS=9000` presses Home.
