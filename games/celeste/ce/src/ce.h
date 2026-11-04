/* Celeste for the TI-84 Plus CE: shared declarations.
 *
 * Inspired by Celeste, not affiliated with Maddy Makes Games.
 * Made by Mason Chen as part of NumPlay.
 *
 * The CE version of games/celeste: Celeste's rules (Player.cs, as src/player.c ports them for the NumWorks) in whole
 * numbers, drawn at 8 bits a pixel from the game's own pictures (tools/pack_ce.py), 60 updates a second.
 *
 * Numbers: an int has 24 bits on the CE (32 on a computer: everything here stays within 24). Positions are whole
 * pixels; what moves keeps the rest in MU-ths of a pixel; speeds are in 60ths of a pixel a second (a frame moves an
 * entity `speed` MU); times are in 600ths of a second (a frame is DTU of them). Celeste's float constants are exact in
 * these units, so its movement comes out the same, pixel for pixel (tests/physics.c checks it against the NumWorks
 * port's floats). */
#ifndef CE_H
#define CE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "gen.h"

#define MU 3600                       /* sub-pixels a pixel */
#define SPD(v) ((int)((v) * 60))      /* px/s -> speed units */
#define TIM(s) ((int)((s) * 600 + 0.5f))   /* seconds -> time units */
#define DTU 10                        /* a frame */
#define VIEW_W 320
#define VIEW_H 180
#define VIEW_Y 30                     /* the view's top on the screen */

/* ---------------------------------------------------------------- platform (plat_ce.c, host/plat_host.c) */
enum {
  K_LEFT = 1, K_RIGHT = 2, K_UP = 4, K_DOWN = 8, K_JUMP = 16, K_DASH = 32, K_GRAB = 64, K_PAUSE = 128,
  K_OK = 256, K_BACK = 512, K_QUIT = 1024, K_TALK = 2048
};
extern uint8_t *g_fb;                 /* the buffer drawn this frame: 320 x 240 bytes */
void plat_init(void);
void plat_exit(void);
void plat_frame(void);                /* shows g_fb, and g_fb becomes the other buffer */
void plat_begin_draw(void);           /* waits until the buffer to draw is no longer shown */
void plat_palette(const uint16_t *pal, int first, int n);   /* 1555 colours */
unsigned plat_keys(void);
unsigned plat_ticks(void);            /* a 32768 Hz clock (wraps at 16 bits) */
void plat_wait(unsigned ticks);
const uint8_t *plat_appvar(const char *name);   /* an appvar's data in place, NULL if missing */
bool plat_save(const char *name, const void *data, unsigned n);
bool plat_load(const char *name, void *data, unsigned n);
void plat_unpack(void *dst, const void *src);   /* ZX0 */

/* ---------------------------------------------------------------- reading data */
#ifdef __TICE__
#define R16(p) (*(const uint16_t *)(const void *)(p))
#define R24(p) (*(const unsigned int *)(const void *)(p))
#define S16(p) (*(const int16_t *)(const void *)(p))
#define S24(p) (*(const int *)(const void *)(p))
#else
static inline unsigned R16(const uint8_t *p) { return (unsigned)(p[0] | p[1] << 8); }
static inline unsigned R24(const uint8_t *p) { return (unsigned)(p[0] | p[1] << 8 | p[2] << 16); }
static inline int S16(const uint8_t *p) { return (int16_t)R16(p); }
static inline int S24(const uint8_t *p) { return (int)(R24(p) << 8) >> 8; }
#endif
#define MAX_PARTS 8
extern const uint8_t *g_common[MAX_PARTS], *g_chap[MAX_PARTS];
/* a 3-byte pointer of a blob: offset, then the appvar */
static inline const uint8_t *far_ptr(const uint8_t *const *parts, const uint8_t *p) {
  return parts[p[2]] + R16(p);
}
#define CPTR(p) far_ptr(g_common, p)
#define HPTR(p) far_ptr(g_chap, p)
bool data_open(void);                 /* the common appvars; false if missing */
bool chapter_open(int n);             /* chapter n's appvars and colours */
void palette_apply(int fade);         /* the chapter's colours (fade 0..256: to black) */
void palette_hair(uint32_t rgb);      /* Madeline's hair colour */
const uint8_t *sprite(int id);        /* a common sprite */
const uint8_t *csprite(int id);       /* a chapter sprite */
const uint8_t *anim(int id);          /* an animation: u8 frames, u8 flags, u16 delay, u8 goto, u8 bank, u16 sprites */
extern const uint8_t *g_tile_flags;
extern int g_nrooms;
const uint8_t *room_rec(int i);
const uint8_t *tile_data(int id);
const uint8_t *player_meta(int sprite_id);   /* s8 hair x, s8 hair y, u8 bangs | has hair << 7 */
const uint16_t *hair_mask(int which);        /* 0 hair, 1-3 bangs: 10 rows (mask), then 10 (dark) */
extern bool g_has_bg;                 /* the chapter has backgrounds */
extern int g_bg_scrollx, g_bg_scrolly;  /* how they move with the camera (1/256) */
extern uint8_t g_bg_map[16];          /* their 16 colours */
extern uint8_t g_bg4[320 * 180 / 2];  /* the room's: 4 bits a pixel, the left one low */
extern int g_bg_refx, g_bg_refy;      /* the camera it was made for */
extern int g_chapter_berries;      /* the chapter's strawberries */
extern const uint8_t *g_remap, *g_remap_red, *g_remap_half;   /* sprites drawn through g_remap if set */

/* ---------------------------------------------------------------- drawing (draw.c) */
enum { DF_FLIPX = 1, DF_FLIPY = 2, DF_SOLID = 4 };   /* DF_SOLID: every pixel in one colour (draw_color) */
extern int g_camx, g_camy;            /* the view's top left in the world */
extern uint8_t g_draw_color;
void draw_sprite(const uint8_t *spr, int x, int y, uint8_t flags);   /* (x, y): the frame's top left, world */
void draw_sprite_scaled(const uint8_t *spr, int x, int y, int sx, int sy, int ox, int oy, uint8_t flags);
void draw_tile(int tile, int x, int y);      /* world */
void draw_rect(int x, int y, int w, int h, uint8_t c);   /* world */
void draw_rect_screen(int x, int y, int w, int h, uint8_t c);   /* view coordinates */
void draw_line(int x0, int y0, int x1, int y1, uint8_t c);   /* world */
void draw_pixel(int x, int y, uint8_t c);
void draw_circle_fill(int cx, int cy, int r, uint8_t c);
void draw_text(const char *s, int x, int y, uint8_t c);   /* screen coordinates (0..320, 0..240) */
int text_width(const char *s);
void draw_layers_back(void);          /* background, bg tiles */
void draw_layer_fg(void);             /* fg tiles */
void draw_decals(int fg);
void draw_clear_screen(uint8_t c);

/* ---------------------------------------------------------------- floats (sf.c): IEEE single precision in whole numbers */
typedef uint32_t f32;
f32 f_add(f32 a, f32 b), f_sub(f32 a, f32 b), f_mul(f32 a, f32 b), f_div(f32 a, f32 b);
bool f_lt(f32 a, f32 b), f_le(f32 a, f32 b);
f32 f_int(int v);
int f_trunc(f32 a), f_floor(f32 a), f_round(f32 a), f_sign(f32 a);
f32 f_approach(f32 v, f32 t, f32 d), f_min(f32 a, f32 b), f_max(f32 a, f32 b), f_abs(f32 a), f_lerp(f32 a, f32 b, f32 t);
#define f_neg(a) ((f32)(a) ^ 0x80000000ul)
#define f_pos(a) (!((a) & 0x80000000ul) && ((a) & 0x7FFFFFFFul))   /* > 0 */
#define f_negative(a) (((a) & 0x80000000ul) && ((a) & 0x7FFFFFFFul))   /* < 0 */
#define f_zero(a) (!((a) & 0x7FFFFFFFul))

/* ---------------------------------------------------------------- math */
static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }
static inline int iabs(int a) { return a < 0 ? -a : a; }
static inline int isign(int a) { return (a > 0) - (a < 0); }
static inline int iclamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int approach(int v, int t, int d) { return v <= t ? imin(v + d, t) : imax(v - d, t); }
int fdiv(int a, int b);               /* floor(a / b), b > 0 */
int round_mu(int c, int dir);         /* c / MU rounded as Celeste's floats round it (util.c) */
int bias_add(int b, int amount);      /* the bias round_mu needs, kept within 24 bits */
int isqrt(int32_t v);
int sin15(int a);                     /* a: 1024 a turn, result * 32768 */
static inline int sin256(int a) { return sin15(a) >> 7; }
static inline int cos256(int a) { return sin15(a + 256) >> 7; }
uint32_t rnd(void);
static inline int rndn(int n) { return (int)((rnd() >> 8) % (uint32_t)n); }

/* ---------------------------------------------------------------- input */
typedef struct {
  bool check, edge, consumed;   /* held; went down this frame; ConsumePress */
  int8_t buffer;            /* time units the press is kept (Celeste's 0.08 s) */
} Button;
typedef struct {
  Button jump, dash, grab, pause, ok, back, talk;
  int move_x, move_y, aim_x, aim_y;
  unsigned keys, prev;
} Input;
extern Input g_in;
void input_update(unsigned keys);
static inline bool btn_pressed(const Button *b) { return !b->consumed && (b->buffer > 0 || b->edge); }
static inline void btn_consume(Button *b) { b->buffer = 0, b->consumed = true; }   /* ConsumePress */
static inline void btn_consume_buffer(Button *b) { b->buffer = 0; }

/* ---------------------------------------------------------------- entities (ents.c) */
enum {   /* Ent.flags */
  EF_ACTIVE = 1, EF_VISIBLE = 2, EF_COLLIDABLE = 4, EF_SOLID = 8, EF_JUMPTHRU = 16, EF_PCOLLIDE = 32, EF_SAFE = 64,
  EF_TRIGGER = 128
};
typedef struct Ent Ent;
struct Ent {
  uint8_t type;             /* ET_ / TT_ + 64 (triggers), 255: free */
  uint8_t flags, room;      /* room: its slot (0, 1), 2: none (the player) */
  uint8_t sub;              /* type's own little state */
  int16_t depth;
  int x, y;
  f32 remx, remy;           /* movementCounter (px) */
  int16_t hx, hy, hw, hh;   /* collider: left, top (from x, y), width, height */
  f32 liftx, lifty;         /* a platform's LiftSpeed this frame (px/s) */
  const uint8_t *rec;       /* its map record */
  int t0, t1, t2, t3;       /* the type's own numbers */
  int16_t a0, a1, a2, a3;
  void *ptr;
  f32 ptr2;
};
#define MAX_ENTS 96
extern Ent g_ents[MAX_ENTS];
extern int g_nents;
static inline int e_left(const Ent *e) { return e->x + e->hx; }
static inline int e_top(const Ent *e) { return e->y + e->hy; }
static inline int e_right(const Ent *e) { return e->x + e->hx + e->hw; }
static inline int e_bottom(const Ent *e) { return e->y + e->hy + e->hh; }
Ent *ent_new(int type, int x, int y);
void ent_free(Ent *e);
void ents_spawn_room(int slot);
void ents_update(void);
void ents_render(int dmax, int dmin);   /* depths in (dmin, dmax] */
void ents_remove_room(int slot);
void ents_clear(void);
bool rect_hits(int l, int t, int r, int b, const Ent *o);
bool ent_collide_at(const Ent *e, int x, int y, const Ent *o);

/* ---------------------------------------------------------------- the level (level.c) */
typedef struct {
  int index;                /* -1: none */
  const uint8_t *rec;
  int x, y, w, h;           /* pixels, world */
  int gx, gy, tw, th;       /* its grid: the top left cell's world pixel, cells (with MARGIN) */
  uint8_t *grid;            /* fg[tw * th], bg[tw * th], solid bits (rb a row) */
  int rb;
  const uint8_t *tiles[256];
  const uint8_t *decals, *ents, *trigs, *bg;
  int bgx, bgy;
  uint8_t ntiles;
  uint8_t flags;
  int8_t camx, camy;
} Room;
typedef struct {
  int chapter;
  Room rooms[2];
  int slot;                 /* the current room */
  Room *room;
  int camx, camy;           /* camera, 1/256 px */
  int cam_offx, cam_offy;   /* Level.CameraOffset, px */
  int shake_timer, shakex, shakey, shake_dirx, shake_diry;
  int8_t last_dir_shake;
  bool transitioning;
  int tr_t;                 /* 0..256*? the transition's progress (time units) */
  int tr_fromx, tr_fromy, tr_tox, tr_toy, tr_px, tr_py;
  int tr_dirx, tr_diry;
  int respawn_x, respawn_y;
  bool dead_reload;
  int freeze;               /* frames */
  int flash;
  uint32_t frame, time;     /* frames played, the chapter's clock (frames) */
  bool completed, paused;
  int wipe;                 /* the wipe: 0 none, >0 closing, <0 opening (frames) */
  uint8_t wipe_dir;
} Level;
extern Level g_level;
bool level_load(int chapter, int room, int intro);
void level_update(void);
void level_render(void);
bool solid_at(int l, int t, int r, int b, const Ent *self);   /* tiles or solid entities over [l, r) x [t, b) */
Ent *solid_first(int l, int t, int r, int b, const Ent *self);
Ent *jumpthru_first_outside(const Ent *e, int x, int y);
bool tiles_solid(int l, int t, int r, int b);
void level_shake(int frames);
void level_dir_shake(int dx, int dy, int frames);
void level_freeze(int frames);
int level_room_at(int x, int y);
int level_room_find(const char *name);
const char *level_room_name(void);

/* ---------------------------------------------------------------- Madeline (player.c) */
enum { ST_NORMAL, ST_CLIMB, ST_DASH, ST_INTRORESPAWN, ST_INTROJUMP, ST_INTROWALK, ST_DUMMY, ST_COUNT };
enum { INTRO_TRANSITION, INTRO_RESPAWN, INTRO_WALKINRIGHT, INTRO_WALKINLEFT, INTRO_JUMP, INTRO_NONE };
#define HAIR_NODES 5
typedef struct {
  Ent *e;
  f32 sx, sy;               /* Speed (px/s) */
  int facing;
  int state, prev_state;
  bool on_ground, was_on_ground, on_safe_ground, dead;
  int dashes, max_dashes, last_dashes;
  f32 stamina;
  int move_x, aim_x, aim_y;
  f32 jump_grace, var_jump, var_jump_speed, auto_jump_timer;
  bool auto_jump;
  int force_move_x;
  f32 force_move_x_timer;
  f32 dash_cooldown, dash_refill_cooldown, dash_attack;
  f32 dash_dx, dash_dy;
  f32 wall_slide_timer;
  int wall_slide_dir;
  f32 climb_no_move;
  f32 wall_speed_retention, wall_speed_retained;
  int wall_boost_dir;
  f32 wall_boost_timer;
  f32 max_fall;
  int hop_wait_x;
  f32 hop_wait_x_speed;
  Ent *climb_hop_solid;
  int climb_hop_x, climb_hop_y;
  int last_climb_move;
  bool dash_started_on_ground, fast_jump, was_dash_b, called_dash_events;
  f32 before_dash_sx, before_dash_sy;
  int co_step;
  f32 co_wait;              /* the state's coroutine */
  bool co_active;
  f32 lift_timer, last_liftx, last_lifty;
  f32 intro_fy;
  /* the sprite */
  int anim, frame, anim_timer;
  int scale_x, scale_y;     /* 1/1024 */
  int idle_timer;
  bool flash;
  int hair_flash;
  uint32_t hair_rgb;
  int hairx[HAIR_NODES], hairy[HAIR_NODES];   /* 1/256 px */
  int hair_count;
  /* intro, respawn, death */
  int intro_t, respawn_fromx, respawn_fromy;
  int body_step, body_t, body_dx, body_dy, death_t;
  int bodyx, bodyy;
  int strawb_index;
  f32 strawb_reset;
} Player;
extern Player g_player;
void player_spawn(int x, int y, int intro);
void player_update(void);
void player_render(void);
void player_die(int dx, int dy);
bool player_refill_dash(void);
bool player_use_refill(bool two);
void player_bounce(int from_y);
void player_super_bounce(int from_y);
void player_side_bounce(int dir, int from_x, int from_y);
bool player_dash_attacking(void);
int player_cam_targetx(void), player_cam_targety(void);
bool player_transition_to(int x, int y);
void player_on_transition(void);
void player_before_up_transition(void);
void player_before_down_transition(void);

/* ---------------------------------------------------------------- the game (game.c) */
extern bool g_running;
void game_frame(void);
void game_draw(void);
#endif
