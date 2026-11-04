/* NumBlocks on the TI-84 Plus CE: Minecraft in 3D, made for the CE's eZ80 (48 MHz, no FPU, 24-bit int).
 *
 * The world around the player lives in a window of 32 x 32 columns and 32 levels (of the world's 64) that
 * follows them: what leaves it is made again from the seed when it comes back, with the player's changes (the
 * edit log, world.c) on top. Each cell has a block and its light (sky light, block light). The 3D view (render.c)
 * draws the faces of the blocks front to back at 160 x 120, each pixel once, shown at 2x.
 *
 * Minecraft's axes: +X east, +Y up, +Z south. Yaw 0 faces south (+Z) and grows clockwise seen from above (a
 * quarter turn faces west); pitch > 0 looks down. Positions are fixed point, 12 bits of fraction (FX). */
#ifndef NBCE_H
#define NBCE_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "data.h"
#include "plat.h"

typedef int32_t fx;   /* 1.0 = 4096 */
#define FXS 12
#define FX1 (1L << FXS)
#define FXI(f) ((int)((f) >> FXS))   /* floor */

/* ---------------------------------------------------------------- the world (world.c) */
#define WS 32         /* the window: 32 x 32 columns */
#define WH 32         /* and 32 levels */
#define WORLD_H 64
#define SEA 32        /* sea level: water up to y 31 */
extern uint8_t *wblk;   /* blocks [y & 31][z & 31][x & 31] */
extern uint8_t *wlight; /* light: sky (low 4 bits) | block (high 4 bits) */
extern uint32_t wexp[WS * WS];   /* per column: the cells with a face to draw (bit y & 31) */
extern uint8_t wtop[WS * WS];    /* per column: the lowest y the sky shines down to (in the window) */
typedef struct { uint8_t h, biome, tf; } CCol;   /* a window column's ground (gen.c's Column, packed) */
extern CCol wcol[WS * WS];
#define wbiome(ci) (wcol[ci].biome)
extern int win_x, win_z, win_y; /* the world position of the window's low corner */
#define WI(x, y, z) ((((unsigned)(y) & 31) << 10) | (((unsigned)(z) & 31) << 5) | ((unsigned)(x) & 31))
#define CI(x, z) ((((unsigned)(z) & 31) << 5) | ((unsigned)(x) & 31))
static inline bool in_win(int x, int y, int z) {
  return (unsigned)(x - win_x) < WS && (unsigned)(z - win_z) < WS && (unsigned)(y - win_y) < WH;
}
int world_get(int x, int y, int z);
void world_set(int x, int y, int z, int b);   /* a change: kept in the edit log, the light follows */
int world_light(int x, int y, int z);         /* the light byte there */
void world_new(uint32_t seed);
void world_center(int x, int y, int z, bool all);   /* keeps the window around (x, y, z) */
bool world_busy(void);                        /* parts of the window still being made */
bool world_behind(void);                      /* so many that the view would see past them */
void world_step(void);                        /* makes a little more of them */
int world_top(int x, int z);                  /* the highest block's y + 1 (spawning) */
extern uint32_t world_seed;
/* the edit log: every block the player changed */
typedef struct { int16_t x, z; uint8_t y, b; } Edit;
extern Edit *edits;
extern unsigned n_edits, max_edits;
bool edits_room(unsigned n);    /* room for n more (false: memory full) */

/* ---------------------------------------------------------------- the terrain (gen.c) */
typedef struct {
  uint8_t h;          /* the ground's height (its top block's y) */
  uint8_t biome;
  uint8_t top, filler;/* the top block and the few below (grass and dirt, sand...) */
  uint8_t depth;      /* how many filler blocks */
} Column;
void gen_seed(uint32_t seed);
void gen_column(int x, int z, Column *c);
int gen_cell(int x, int y, int z, const Column *c);
void gen_cells(int x, int z, const Column *c, int y0, int y1, uint8_t *out);   /* gen_cell for [y0, y1) */   /* the terrain, caves, ores, water */
/* trees and plants whose blocks fall in [x0, x1) x [y0, y1) x [z0, z1): put(x, y, z, block) for each */
void gen_decorate(int x0, int z0, int x1, int z1, int y0, int y1, void (*put)(int, int, int, int));
int gen_plant(int x, int z, const Column *c);   /* the plant on top of a column (0: none) */
void gen_spawn(int *x, int *z);

/* ---------------------------------------------------------------- the player (player.c) */
typedef struct { uint8_t id, n; uint16_t dmg; } Stack;   /* id 0: empty; dmg: a tool's wear */
typedef struct {
  fx x, y, z;           /* feet */
  fx vx, vy, vz;        /* blocks a tick */
  uint16_t yaw;         /* 65536 a turn */
  int16_t pitch;        /* 16384 = 90 degrees down */
  bool on_ground, in_water, in_lava, sneaking, sprinting, flying, dead;
  uint8_t mode;         /* 0 survival, 1 creative */
  int hit_x, hit_y, hit_z, hit_face;   /* the block looked at (hit_face -1: none) */
  int break_ticks, break_need;         /* mining it: ticks so far, ticks it takes */
  int slot;             /* the hotbar slot held */
  Stack inv[36];        /* 0-8 the hotbar, then the three rows */
  Stack craft[4];       /* the inventory's 2 x 2 crafting grid */
  Stack cursor;         /* held by the cursor in a screen */
  int health;           /* half hearts, 0-20 */
  int air;              /* ticks of air left under water (300) */
  int hurt_time, regen, fall_from, fire;
  int spawn_x, spawn_y, spawn_z;
  int swing;            /* ticks of the arm's swing left */
  int jump_wait, fwd_tap, fwd_held;
} Player;
extern Player pl;
void player_spawn(void);
void player_tick(uint32_t keys, uint32_t pressed);   /* 20 a second */
int ray_pick(int reach_fx, int *hx, int *hy, int *hz, int *face);   /* the first block looked at, or 0 */
int inv_add(int id, int n, int dmg);   /* what does not fit */
static inline Stack *held(void) { return &pl.inv[pl.slot]; }
void player_hurt(int halves);
extern int item_dur(int id);
int item_max(int id);

/* ---------------------------------------------------------------- the 3D view (render.c) */
#define RW 160
#define RH 120
extern uint8_t *rbuf;          /* the picture, RW x RH */
void render_init(void);
bool render_frame(void);       /* from the player's eyes (false: the same picture as the last) */
extern uint8_t render_stale;   /* set when rbuf or what it shows changed otherwise */
void render_title(uint16_t yaw);   /* the title's backdrop: the world turning */
void render_picture(const uint8_t *p);   /* rbuf and the sky's rows as render_title left them, again */
const uint8_t *render_sky(void);         /* the sky's rows (RH) */
const uint8_t *render_veil_row(uint8_t level);   /* shade row level with the sky's colours darkened too */
void render_show(bool hud);    /* the picture onto the screen at 2x, the sky in it (and the HUD) */
void render_sky_palette(void); /* the sky's colours and the light, for the time of day */
extern uint32_t world_time;    /* ticks: 24000 a day, 0 sunrise */
extern int view_dist;          /* blocks seen */
extern uint32_t stat_faces, stat_spans, stat_px, stat_cells;
extern int crack_stage;        /* the cracks on the block mined (-1: none) */

/* ---------------------------------------------------------------- the screens (gui.c) */
enum { GUI_NONE, GUI_INVENTORY, GUI_CRAFTING, GUI_FURNACE, GUI_CHEST, GUI_CREATIVE, GUI_PAUSE, GUI_DEATH, GUI_TITLE,
       GUI_WORLDS, GUI_CREATE, GUI_CONFIRM, GUI_LOADING, GUI_CONTROLS, GUI_OPTIONS, GUI_SAVING };
extern int gui;
void gui_open(int screen);
void gui_input(uint32_t keys, uint32_t pressed);
void gui_draw(void);          /* the open screen, over the picture */
bool hud_draw(void);          /* the hotbar and the rest, over the picture (false: as it was) */
void gui_tick(void);          /* furnaces */
void text(int x, int y, const char *s, uint8_t c);   /* Minecraft's font, with its shadow */
int text_width(const char *s);
void gui_message(const char *s);
extern int menu_action;       /* what a menu asks main.c to do (ACT_*) */
enum { ACT_NONE, ACT_PLAY, ACT_NEW, ACT_QUIT, ACT_SAVE_QUIT, ACT_RESPAWN, ACT_TITLE };

/* ---------------------------------------------------------------- tiles: chests and furnaces (gui.c) */
typedef struct {
  int16_t x, z;
  uint8_t y, kind;     /* B_CHEST, B_FURNACE */
  uint8_t facing;      /* 2 north, 3 south, 4 west, 5 east: the face its front is on */
  uint8_t burn_max_hi;
  int16_t burn, cook, burn_max;
  Stack s[27];         /* a chest's 27; a furnace's input, fuel, output */
} Tile;
#define MAX_TILES 16
extern Tile tiles[MAX_TILES];
extern int n_tiles;
Tile *tile_at(int x, int y, int z);
Tile *tile_new(int x, int y, int z, int kind, int facing);
void tile_remove(int x, int y, int z);

/* ---------------------------------------------------------------- saves (save.c) */
#define MAX_WORLDS 5
typedef struct {
  char name[17];
  uint8_t mode;
  uint32_t seed;
  uint32_t played;
} WorldInfo;
bool world_info(int slot, WorldInfo *w);
bool save_world(void);
bool load_world(int slot);
void new_world(int slot, const char *name, uint32_t seed, int mode);
void delete_world(int slot);
int world_free_slot(void);
extern int cur_world;
extern char cur_name[17];

/* ---------------------------------------------------------------- the keys, as the game sees them (main.c) */
enum {
  K_LEFT = 1 << 0, K_RIGHT = 1 << 1, K_UP = 1 << 2, K_DOWN = 1 << 3,   /* arrows: look; in screens, move */
  K_FWD = 1 << 4, K_BACK = 1 << 5, K_SLEFT = 1 << 6, K_SRIGHT = 1 << 7, /* cos ( , ) */
  K_JUMP = 1 << 8, K_SNEAK = 1 << 9, K_SPRINT = 1 << 10,              /* y=, alpha, del */
  K_ATTACK = 1 << 11, K_USE = 1 << 12, K_INV = 1 << 13,               /* 2nd, clear, vars */
  K_PAUSE = 1 << 14, K_HOME = 1 << 15, K_DROP = 1 << 16, K_PICK = 1 << 17,   /* (-), mode, x,T,θ,n, log */
  K_ENTER = 1 << 18,                                                  /* enter */
  K_SLOT1 = 1 << 19   /* K_SLOT(n): digit n + 1 */
};
#define K_SLOT(n) ((uint32_t)K_SLOT1 << (n))
#define K_OK K_ATTACK   /* 2nd: OK in screens */
#define K_ESC K_USE     /* clear: back in screens */
int rnd(int n);
void game_save_quit(void);
void options_save(void);

/* fixed-point helpers */
int16_t fsin(uint16_t a);   /* 2.14 */
static inline int16_t fcos(uint16_t a) { return fsin((uint16_t)(a + 16384)); }
#endif
