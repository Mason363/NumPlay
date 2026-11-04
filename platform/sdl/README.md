# NumPlay on a computer and in a browser

Windows, macOS, Linux and a Raspberry Pi, with [SDL 3](https://libsdl.org), and any web browser, the same code built with [Emscripten](https://emscripten.org).

```
make port TARGET=sdl        (in a game's folder): build/sdl/<game>
make port TARGET=web        build/web/<game>.html, .js and .wasm
```

In a browser, saves go to the page's local storage. A game with its data in a `.S` file names it for the web build: `PORT_DATA = symbol:file`.

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
