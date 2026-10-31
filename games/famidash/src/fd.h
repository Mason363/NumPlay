/* Famidash on the NumWorks calculator: what the game's files share.
 *
 * game.c is a port of Famidash's game code (tfdsoft, MIT: SAUCE/ and LIB/asm/nesdash.s) to plain C,
 * keeping its variable names, its 8 and 16 bit arithmetic and the order things happen in a frame, so
 * that the physics stay the NES's to the subpixel. ppu.c draws what the NES's picture chip would:
 * the level's tiles, the parallax, sprites, palettes. main.c has the menus, the keys and the saves. */
#ifndef FD_H
#define FD_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "data.h"

#define high_byte(a) (*((uint8_t *)&(a) + 1))
#define low_byte(a) (*((uint8_t *)&(a)))
#define MSB(x) ((uint8_t)(((uint16_t)(x)) >> 8))
#define LSB(x) ((uint8_t)(x))

/* the NES controller (neslib's bits) */
enum { PAD_A = 0x80, PAD_B = 0x40, PAD_SELECT = 0x20, PAD_START = 0x10, PAD_UP = 0x08, PAD_DOWN = 0x04,
       PAD_LEFT = 0x02, PAD_RIGHT = 0x01 };
typedef struct { uint8_t hold, press, release; } pad_t;
extern pad_t joypad1, joypad2, *controllingplayer;

/* game states */
enum { STATE_MENU = 1, STATE_GAME = 2, STATE_LVLDONE = 3, STATE_LEVELSELECT = 6 };

enum { GAMEMODE_CUBE, GAMEMODE_SHIP, GAMEMODE_BALL, GAMEMODE_UFO, GAMEMODE_ROBOT, GAMEMODE_SPIDER, GAMEMODE_WAVE,
       GAMEMODE_SWING, GAMEMODE_NINJA, GAMEMODE_POGO, GAMEMODE_SNAKE, GAMEMODE_FOOTBALL };
#define COIN_1 0x01
#define COIN_2 0x02
#define COIN_3 0x04
#define GRAVITY_DOWN 0x00
#define GRAVITY_UP 0xFF
#define MAX_PRACTICE_POINTS 8
#define max_loaded_sprites 16
#define MAX_ICONS 19

/* options bits */
#define platformer 0x04

/* the save: everything the NES kept in SRAM, and the settings */
typedef struct {
  uint8_t coin1[NLEVELS], coin2[NLEVELS], coin3[NLEVELS], complete[NLEVELS], normal[NLEVELS], practice[NLEVELS];
  uint8_t icoin1[NLEVELS], icoin2[NLEVELS], icoin3[NLEVELS], icomplete[NLEVELS], inormal[NLEVELS], ipractice[NLEVELS];
} progress_t;
extern progress_t prog;

/* ---- game.c */
extern uint8_t gameState, level, gamemode, dual, icon, icon_colors[3], options, invisible, invisblocks, retro_mode,
    discomode, cam_seesaw, viseffects, trails, auto_practicepoints, gameboy_mode, twoplayer;
extern uint8_t practice_point_count, curr_practice_point, coins, kandoframecnt, cube_data[2], attemptCounter[7];
extern uint16_t jumps;
extern uint32_t scroll_x;
extern uint16_t scroll_y;
extern uint8_t speed, player_gravity[2], player_mini[2], END_LEVEL_TIMER, pauseStatus, lastgcolortype,
    lastbgcolortype, outline_color, parallax_scroll_x, no_parallax, current_spike_set, current_block_set,
    current_saw_set, current_deco_type, minicoins, all_levels_complete;
extern uint16_t player_relx[2], player_rely[2];
extern int16_t player_vel_x[2], player_vel_y[2];
extern uint8_t PAL_BUF_RAW[32];
extern uint32_t rng_seed;
uint8_t newrand(void);

/* a level: state_game() in steps */
void game_begin(void);           /* level chosen: attempts from 1 */
void game_start(void);           /* state_game's set up and reset_level */
void game_step(void);            /* one frame of play (everything_else) */
bool game_dead(void);            /* cube_data says the player died this frame */
void game_death_frame(int f);    /* the 30 frames of the explosion */
void game_reset(void);           /* reset_level (after the fade) */
void game_draw(void);            /* draw_sprites into the sprite list */
void game_store_practice(void);  /* pause, B: practice mode on */
void update_level_completeness(void);
void set_completion_data(void);
uint8_t level_percent(void);
void pal_col(uint8_t i, uint8_t c);
uint8_t oneShadeDarker(uint8_t c);
uint8_t colBrightness(uint8_t c, uint8_t b);

/* ---- ppu.c: the NES's picture, drawn into 320x240 (the NES's 256 columns in the middle) */
#define NES_X0 32
typedef struct { int16_t x, y; uint8_t tile, attr; } spr_t;
#define MAX_SPR 160
extern spr_t oam[MAX_SPR];
extern int oam_n;
extern uint8_t pal_bright_level; /* 0..8, 4 normal */
extern uint8_t emphasis;         /* NES PPU mask bits 0 (grey) and 5-7 (tints) */
extern uint8_t chr_bg[4], chr_spr[2]; /* NES CHR banks: four 1 KB for the tiles, two 2 KB for sprites */
void oam_clear(void);
void oam_spr(int x, int y, uint8_t tile, uint8_t attr);
void oam_meta_spr(int x, int y, const uint8_t *data);
void oam_meta_spr_flipped(uint8_t flip, int x, int y, const uint8_t *data);
void oam_meta_spr_disco(int x, int y, const uint8_t *data);
/* what the background shows */
enum { BG_LEVEL, BG_NAMETABLE };
extern uint8_t bg_mode;
extern uint8_t nametable[2][1024]; /* two nametables side by side, attributes included */
extern int nt_scroll_x, nt_scroll_y;
extern int nt_split_y0, nt_split_y1, nt_split_x; /* rows y0..y1 scroll by nt_split_x instead */
void ppu_set_palette(const uint8_t *bg16, const uint8_t *spr16);
void ppu_strip(uint16_t *out, int y0, int y1); /* rows y0..y1 of the screen, RGB565 */
/* the level: columns of metatiles, as the game decodes them */
uint8_t level_metatile(int32_t col, int row);
void ppu_init(void);
#endif
