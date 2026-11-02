/*
 * p8_emu.h
 *
 *  Created on: Dec 13, 2023
 *      Author: bbaker
 */

#ifndef P8_EMU_H
#define P8_EMU_H

#if defined(_WIN32)
#include <direct.h>   // _mkdir
#else
#include <sys/stat.h> // mkdir
#endif
/* Changed for NumPico: the calculator's platform (pico.c) instead of SDL and FreeRTOS, and PICO-8's
 * memory as far as 0x7fff (the upper 32 KB is left out: see P8_RAM_SIZE). */
#include <limits.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define PROGNAME "femto8"

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? a : b)
#define MIN(a, b) ((a) < (b) ? a : b)
#endif
#define IS_EVEN(n) (((n) ^ 1) == ((n) + 1))
#define NIBBLE_SWAP(n) (((n) << 4) | ((n) >> 4))

#ifndef PATH_MAX
#define PATH_MAX 256
#endif

#define P8_WIDTH 128
#define P8_HEIGHT 128
#define MEMORY_SPRITES 0
#define MEMORY_SPRITES_SIZE 0x1000
#define MEMORY_SPRITES_MAP 0x1000
#define MEMORY_SPRITES_MAP_SIZE 0x1000
#define MEMORY_MAP 0x2000
#define MEMORY_MAP_SIZE 0x1000
#define MEMORY_SPRITEFLAGS 0x3000
#define MEMORY_SPRITEFLAGS_SIZE 0x100
#define MEMORY_MUSIC 0x3100
#define MEMORY_MUSIC_SIZE 0x100
#define MEMORY_SFX 0x3200
#define MEMORY_SFX_SIZE 0x1100
#define MEMORY_WORKRAM 0x4300
#define MEMORY_WORKRAM_SIZE 0x1b00
#define MEMORY_FONT 0x5600
#define MEMORY_FONT_SIZE 0x2000
#define MEMORY_CARTDATA 0x5e00
#define MEMORY_CARTDATA_SIZE 0x100
#define MEMORY_DRAWSTATE 0x5f00
#define MEMORY_DRAWSTATE_SIZE 0x40
#define MEMORY_HARDWARESTATE 0x5f40
#define MEMORY_HARDWARESTATE_SIZE 0x40
#define MEMORY_GPIO 0x5f80
#define MEMORY_GPIO_SIZE 0x80
#define MEMORY_SCREEN 0x6000
#define MEMORY_SCREEN_SIZE 0x2000

#define MEMORY_SIZE (1024 * 64)
#define CART_MEMORY_SIZE 0x4300

#define MEMORY_PALETTES 0x5f00
#define MEMORY_CLIPRECT 0x5f20
#define MEMORY_LEFT_MARGIN 0x5f24
#define MEMORY_PENCOLOR 0x5f25
#define MEMORY_CURSOR 0x5f26
#define MEMORY_CAMERA 0x5f28
#define MEMORY_SCREEN_TRANSFORM 0x5f2c
#define MEMORY_DEVKIT_MODE 0x5f2d
#define MEMORY_AUDIO_PAUSE 0x5f2f
#define MEMORY_SUPPRESS_PAUSE 0x5f30
#define MEMORY_FILLP 0x5f31
#define MEMORY_FILLP_ATTR 0x5f33
#define MEMORY_COLOR_FILLP 0x5f34
#define MEMORY_LINE_VALID 0x5f35
#define MEMORY_MISCFLAGS 0x5f36
#define MEMORY_TLINE_MASK_X 0x5f38
#define MEMORY_TLINE_MASK_Y 0x5f39
#define MEMORY_TLINE_OFFSET_X 0x5f3a
#define MEMORY_TLINE_OFFSET_Y 0x5f3b
#define MEMORY_LINE_X 0x5f3c
#define MEMORY_LINE_Y 0x5f3e
#define MEMORY_RNG_STATE 0x5f44
#define MEMORY_BUTTON_STATE 0x5f4c
#define MEMORY_SPRITE_PHYS 0x5f54
#define MEMORY_SCREEN_PHYS 0x5f55
#define MEMORY_MAP_START 0x5f56
#define MEMORY_MAP_WIDTH 0x5f57
#define MEMORY_TEXT_ATTRS 0x5f58
#define MEMORY_SGET_DEFAULT 0x5f59
#define MEMORY_MGET_DEFAULT 0x5f5a
#define MEMORY_PGET_DEFAULT 0x5f5b
#define MEMORY_TEXT_CHAR_SIZE 0x5f59
#define MEMORY_TEXT_CHAR_SIZE2 0x5f5a
#define MEMORY_TEXT_OFFSET 0x5f5b
#define MEMORY_AUTO_REPEAT_DELAY 0x5f5c
#define MEMORY_AUTO_REPEAT_INTERVAL 0x5f5d
#define MEMORY_RW_MASK 0x5f5e
#define MEMORY_HIGH_COLOUR_MODE 0x5f5f
#define MEMORY_PALETTE_SECONDARY 0x5f60

#define TICKS_PER_SECOND 128
#define SCREEN_SIZE 128 * 128 * 4
#define GLYPH_WIDTH 4
#define GLYPH_HEIGHT 6
#define SPRITE_WIDTH 8
#define SPRITE_HEIGHT 8
#define BUTTON_COUNT 7
#define BUTTON_REPEAT_COUNT 6
#define BUTTON_INTERNAL_COUNT 13
#define PLAYER_COUNT 2

#define STAT_MEM_USAGE 0
#define STAT_CPU_USAGE 1
#define STAT_SYSTEM_CPU_USAGE 2
#define STAT_CURRENT_DISPLAY 3
#define STAT_CLIPBOARD 4
#define STAT_VERSION 5
#define STAT_PARAM 6
#define STAT_FRAMERATE 7
#define STAT_TARGET_FRAMERATE 8
#define STAT_NUM_DISPLAYS 11
#define STAT_PAUSE_MENU_X1 12
#define STAT_PAUSE_MENU_Y1 13
#define STAT_PAUSE_MENU_X2 14
#define STAT_PAUSE_MENU_Y2 15
#define STAT_RAW_KEYBOARD 28
#define STAT_KEY_PRESSED 30
#define STAT_KEY_NAME 31
#define STAT_MOUSE_X 32
#define STAT_MOUSE_Y 33
#define STAT_MOUSE_BUTTONS 34
#define STAT_MOUSE_WHEEL 36
#define STAT_MOUSE_XREL 38
#define STAT_MOUSE_YREL 39
#define STAT_YEAR_UTC 80
#define STAT_MONTH_UTC 81
#define STAT_DAY_UTC 82
#define STAT_HOUR_UTC 83
#define STAT_MINUTE_UTC 84
#define STAT_SECOND_UTC 85
#define STAT_YEAR 90
#define STAT_MONTH 91
#define STAT_DAY 92
#define STAT_HOUR 93
#define STAT_MINUTE 94
#define STAT_SECOND 95
#define STAT_RAW_GC 99
#define STAT_BREADCRUMB 100
#define STAT_BBS_CART_ID 101
#define STAT_LOAD_RESULT 107
#define STAT_PCM_BUFFER_SIZE 108
#define STAT_PCM_APP_BUFFER 109
#define STAT_CURRENT_PATH 124

#define DEFAULT_AUTO_REPEAT_DELAY 15
#define DEFAULT_AUTO_REPEAT_INTERVAL 4

#define OVERLAY_TRANSPARENT_COLOR 0

/* the memory the calculator keeps: 0x0000 to 0x7fff (reads past it give 0, writes are dropped) */
#define P8_RAM_SIZE 0x8000

enum
{
    PALTYPE_DRAW,
    PALTYPE_SCREEN,
    PALTYPE_SECONDARY
};

enum
{
    DRAWTYPE_DEFAULT,
    DRAWTYPE_GRAPHIC,
    DRAWTYPE_SPRITE
};

enum {
    COMPAT_OK = 0,
    COMPAT_SOME,
    COMPAT_NONE
};

extern unsigned m_fps;
extern unsigned m_frames;

extern uint8_t *m_memory;
extern const uint8_t *m_cart_memory;

extern char m_param_string[256];

void __attribute__ ((noreturn)) p8_abort(void);
void __attribute__ ((noreturn)) p8_restart(void);
void p8_check_for_next_frame(void);
unsigned p8_elapsed_time(void);
void p8_flip(void);
void p8_render(void);
void p8_reset(void);
void p8_seed_rng_state(uint32_t seed);
bool p8_open_cartdata(const char *id);
void p8_delayed_flush_cartdata(void);
void p8_pump_events(void);
void p8_show_pause_menu(void);
/* load(): another of the carts, by name; returns only when there's none */
bool p8_load_cart(const char *name, const char *param);
/* reload() from another cart: its ROM, or NULL */
const uint8_t *p8_cart_rom(const char *name);

#endif
