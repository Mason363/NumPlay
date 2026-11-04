# Run NumPlay on anything

NumPlay's games are plain C. Each one talks to its machine through about twenty functions: a screen to draw on, keys, a clock and a few files. Give them to it and every game builds on your hardware: a Linux box, a Raspberry Pi, an ESP32 or RP2040 with a screen, an old handheld.

## What a game needs

| | |
| --- | --- |
| Screen | 320 x 240, 16-bit colour (RGB565). Smaller screens can scale or crop. |
| Keys | Arrows, OK, Back, Home at least. More for some games (the NumWorks keypad has 46). |
| Clock | Milliseconds since the start, and a sleep. |
| Files | One buffer of up to 64 KB, kept between runs (flash, an SD card, a file). |
| RAM | 32 KB for the smallest games, 140 KB for the biggest, plus a 32 KB stack. |
| Storage | 20 KB to 2 MB of program and data, read-only. |

## How

The functions are those of [`platform/include/eadk.h`](../platform/include/eadk.h) and [`platform/include/np_port.h`](../platform/include/np_port.h). Copy [`platform/sdl/np_sdl.c`](../platform/sdl/np_sdl.c), the computer's version, about 300 lines, and change what touches the machine:

1. **`eadk_display_push_rect(rect, pixels)`:** copy a rectangle of RGB565 pixels to the screen. Games draw in bands of a few rows, so a driver that sends a window of pixels (an ST7789, an ILI9341) maps straight onto it.
2. **`eadk_keyboard_scan()`:** return the keys held as bits, one per NumWorks key (`eadk_key_left`...). Map your buttons onto them.
3. **`eadk_timing_millis()`, `eadk_timing_msleep(ms)`:** the clock.
4. **`np_storage(&size)`, `np_storage_changed()`:** a buffer for the games' files; write it out when told.
5. **`eadk_random()`:** any random 32 bits.

The rest can stay as in the computer's version: battery and backlight answer fixed values, `eadk_display_draw_string` comes from [`platform/common/eadk_text.c`](../platform/common/eadk_text.c).

Then build a game with your compiler, `-DNP_PORT=1 -I platform/include`, the game's sources, your file, `platform/common/eadk_text.c` and `launcher/src/font_data.c`. A game's Makefile lists its sources in `PORT_SRC`.

## An example: an ESP32 with an ST7789 screen and eight buttons

```c
uint64_t eadk_timing_millis(void) { return esp_timer_get_time() / 1000; }
void eadk_timing_msleep(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

void eadk_display_push_rect(eadk_rect_t r, const eadk_color_t *px) {
  st7789_set_window(r.x, r.y, r.x + r.width - 1, r.y + r.height - 1);
  st7789_write_pixels(px, r.width * r.height); /* (RGB565, high byte first on most panels) */
}

eadk_keyboard_state_t eadk_keyboard_scan(void) {
  eadk_keyboard_state_t k = 0;
  if (!gpio_get_level(PIN_LEFT)) k |= 1ull << eadk_key_left;
  if (!gpio_get_level(PIN_RIGHT)) k |= 1ull << eadk_key_right;
  if (!gpio_get_level(PIN_UP)) k |= 1ull << eadk_key_up;
  if (!gpio_get_level(PIN_DOWN)) k |= 1ull << eadk_key_down;
  if (!gpio_get_level(PIN_A)) k |= 1ull << eadk_key_ok;
  if (!gpio_get_level(PIN_B)) k |= 1ull << eadk_key_back;
  if (!gpio_get_level(PIN_START)) k |= 1ull << eadk_key_exe;
  if (!gpio_get_level(PIN_SELECT)) k |= 1ull << eadk_key_home;
  return k;
}
```

The files: keep the 64 KB buffer in RAM and write it to a flash partition (or a file on an SD card) in `np_storage_changed()`.

## Good to know

- **Big games' data** sits in a `.S` file that includes `data.bin`; your assembler takes it as is (GNU `as`, Clang).
- **Some games want more RAM** after their own: define `_heap_start` and `_heap_end` around a block (see `np_sdl.c`).
- **An `int` of 16 or 24 bits** (an AVR, the TI-84 Plus CE) breaks code written for 32: keep to 32-bit chips, or check the warnings.
- **Speed:** the games run at 30 fps on a 216 MHz Cortex-M7 (the NumWorks N0110). An ESP32 runs the classics and most 2D games; a Raspberry Pi runs everything.
