/* Hollow Knight for the NumWorks calculator.
 *
 * Inspired by Hollow Knight, not affiliated with Team Cherry.
 * Made by Mason Chen as part of NumPlay. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#ifdef HK_XL
#include "data_xl.h"   /* (the bigger game: the rooms past Greenpath too) */
#else
#include "data.h"
#endif

#define SCREEN_W 320
#define SCREEN_H 240
/* the game keeps a 16:10 view (ForceCameraAspect): bars above and below */
#define VIEW_W 320
#define VIEW_H 200
#define VIEW_Y 20

/* the camera: perspective, 24 degrees high at 16:9, 38.1 units in front of the gameplay plane */
#define CAM_Z (-38.1f)
#define FOCAL 421.94f        /* (VIEW_H / 2) / tan(fov / 2), fov = 24 * (16/9) / 1.6 */

/* ---------------------------------------------------------------- platform */
enum {
  K_LEFT = 1, K_RIGHT = 2, K_UP = 4, K_DOWN = 8,
  K_JUMP = 16, K_ATTACK = 32, K_FOCUS = 64, K_SPELL = 128, K_DASH = 256,
  K_OK = 512, K_BACK = 1024, K_PAUSE = 2048, K_MAP = 4096, K_INV = 8192, K_HOME = 16384, K_ANY = 32768,
};
uint32_t plat_keys(void);
uint32_t plat_millis(void);
void plat_sleep(uint32_t ms);
void plat_push(int x, int y, int w, int h, const uint16_t *px);
void plat_fill(int x, int y, int w, int h, uint16_t c);
bool plat_save(const char *name, const void *data, uint32_t len);
const uint8_t *plat_load(const char *name, uint32_t *len);
bool plat_remove(const char *name);
uint8_t *plat_reserve(const char *name, uint32_t len);   /* a file len long to fill in place (NULL: no room) */
void plat_reserved(const char *name, uint32_t len);      /* (then: it is written) */

/* ---------------------------------------------------------------- data.bin */
extern const uint8_t *hk_bin;
const uint8_t *section(int id);
uint32_t section_size(int id);
static inline uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static inline uint32_t rd32(const uint8_t *p) { return (uint32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }
/* (the calculator's C library copies and clears a byte at a time: these go by words, for n a multiple of 4, d aligned) */
typedef uint32_t __attribute__((may_alias, aligned(1))) u32_any;
static inline void copy_words(void *d, const void *s, size_t n) {
  uint32_t *dw = (uint32_t *)d;
  const u32_any *sw = (const u32_any *)s;
  for (; n >= 16; n -= 16, dw += 4, sw += 4) dw[0] = sw[0], dw[1] = sw[1], dw[2] = sw[2], dw[3] = sw[3];
  for (; n; n -= 4) *dw++ = *sw++;
}
static inline void zero_words(void *d, size_t n) {
  uint32_t *dw = (uint32_t *)d;
  for (; n >= 16; n -= 16, dw += 4) dw[0] = dw[1] = dw[2] = dw[3] = 0;
  for (; n; n -= 4) *dw++ = 0;
}
void lz_decode(const uint8_t *src, uint32_t comp, uint8_t *dst, uint32_t raw);
/* decoders' working memory (LZMA probabilities or the tile coder's), one at a time; on the calculator, on main's
 * stack (main.c) */
#define SCRATCH_N 4096
extern uint16_t *g_scratch;
float f16(uint16_t h);
const char *str_at(int id);   /* the shared string table (SEC_STR) */

/* ---------------------------------------------------------------- textures (tex.c) */
typedef struct {
  uint16_t w, h;
  uint8_t tw, th;
  uint16_t pal;                            /* its colors (in SEC_PAL, at 2 x pal: gfx.c) */
  uint32_t off : 27, fmt : 2, flags : 3;   /* its data: SEC_TDAT (tiles: tex.c), SEC_SOFT (smooth: gfx.c) */
} TexRec;
#define TEX_SMOOTH 1   /* drawn interpolated: the blurred background */
#define TEX_FULL 2     /* every tile kept, none opaque: no tile map */
#define TEX_WIDE 4     /* tile ranks of 2 bytes */
enum { FMT_PAL4 = 0, FMT_ALPHA2 = 1, FMT_SOFT = 2, FMT_SOFTA = 3 };
#define TILE 16
#define TILE_SHIFT(fmt) ((fmt) == FMT_ALPHA2 ? 5 : 4)   /* a tile's width, log 2 */
#define TEX_NONE 0xFFFF
const TexRec *tex_rec(uint16_t t);
/* the tile's texels (128 bytes: 16x16 of 4 bits, or 32x16 of 2 bits for FMT_ALPHA2), NULL for an empty one; decodes
 * on demand */
const uint8_t *tex_tile(uint16_t t, int tx, int ty);
/* the same, as a cache slot (-1: an empty tile): it holds until the cache runs out of room in a frame */
int tex_slot(uint16_t t, int tx, int ty);
const uint8_t *tex_slot_px(int s);   /* (its tile's texels, the slot drawn in this strip) */
void tex_strip(int s);                /* the strip drawn from now on */
void gfx_slot_reused(int s);          /* (gfx.c: a slot drawn earlier this frame now holds another tile) */
/* where a cached tile's texels that are not clear are, 4 bits each: x0, x1 - 1, y0, y1 - 1 (at 2 bits, x in twos) */
uint32_t tex_slot_box(const uint8_t *px);
extern bool g_tex_overload;
/* a tile row's codes: 0 empty, 1 a tile, 3 an opaque tile (2 bits each) */
const uint8_t *tex_row_codes(uint16_t t, int ty);
void tex_frame(void);   /* a new frame: tiles used from now on are this frame's */
extern uint32_t g_tex_decodes, g_tex_misses, g_tex_calls;

/* ---------------------------------------------------------------- rooms (room.c) */
typedef struct {
  int16_t ax, ay;      /* the texture's top-left corner (1/64 unit) */
  int16_t z;           /* depth (1/128 unit) */
  uint16_t tex;
  uint8_t tint, flags;
  uint16_t a, b;       /* world units a texel along u and v (half floats, signed) */
  int16_t rot;         /* the u axis' angle (1/65536 turn) */
  uint8_t group;       /* its render group (F_DYN): faded or hidden as the game wants (g_group_alpha) */
} Inst;
#define MAX_GROUPS 256
extern uint8_t g_group_alpha[MAX_GROUPS];   /* 255 shown, 0 hidden */
enum { BL_ALPHA = 0, BL_ADD, BL_SCREEN, BL_LINEARLIGHT, BL_OVERLAY, BL_MULTIPLY };
#define F_BLEND 7
#define F_LIT 8
#define F_ROT 16
#define F_SOLID 32
#define F_DYN 64
#define F_GRASS 128
typedef struct {
  uint16_t version, nsec, ntint, axis;
  float w, h, blur_z, saturation;
  float ambient[4];
  float hero_light[4];
  float lo, hi;        /* the camera's range along the axis the room is split on */
  uint16_t ninst;
  uint16_t nseg, ncol, gw, gh;   /* the ground (phys.c) */
  uint16_t nent;                 /* game objects (game.h: Ent) */
  uint32_t ground, ents;         /* (in SEC_RBLOB) */
  uint8_t lut[3][64];
} RoomHdr;
typedef struct {
  uint32_t off, len;   /* its instances, in draw order (read in place) */
  uint16_t count, pad;
  float start, end;    /* camera positions from which its instances can show */
} SectorRec;
#define MAX_LOADED 16
typedef struct {
  int id;
  RoomHdr *h;
  const uint8_t *tints;     /* RGBA */
  const SectorRec *secs;
  int nnear;
  uint8_t near[MAX_LOADED];   /* the sectors near the camera */
  uint16_t hide_lo, hide_hi;  /* (a scene loaded with it, not this time: its sprites' groups, hidden) */
} Room;
bool room_flag(int flag);     /* (game.c: a PlayerData bool, the room's variant chosen by) */
extern Room g_room;
bool room_load(int id);
void room_near(float cx, float cy);   /* picks the sectors near the camera */
/* their instances, merged back to front: room_first, then room_next until it says no more; with each, its sorting
 * layer and order */
void room_first(void);
bool room_next(Inst *out, uint32_t *group);
#define SORT_KEY(layer, order) ((uint32_t)(layer) << 16 | (uint32_t)((order) + 32768))
const char *room_name(int id);

/* ---------------------------------------------------------------- sprites and animations (anim.c) */
typedef struct {
  uint16_t tex, col;   /* (col: its collider, sprite_collider) */
  int16_t lx, ty;      /* the texture's top-left corner, from the sprite's origin (1/64 unit) */
  uint16_t tu, tv;     /* units a texel (half floats) */
} SpriteRec;
const SpriteRec *sprite_rec(int id);
/* the collider 2D Toolkit sets as a frame shows: SC_UNSET (left as it was), SC_NONE (off), SC_BOX (*data: center x y,
 * half width height), SC_SHAPE (*n points, x y pairs); sprite units */
enum { SC_UNSET, SC_NONE, SC_BOX, SC_SHAPE };
int sprite_collider(int sprite, const float **data, int *n);
enum { ANIM_DONE = 1, ANIM_TRIGGER = 2 };
typedef struct {
  int16_t clip, frame, sprite;
  float time, fps;   /* time in frames */
  bool playing, paused;
  uint8_t events;    /* ANIM_*: what happened since the owner last cleared them */
} Anim;
void anim_play(Anim *a, int clip);   /* (a clip already playing goes on) */
void anim_play_from(Anim *a, int clip, float start);
void anim_play_from_frame(Anim *a, int clip, int frame);
bool anim_is_playing(const Anim *a, int clip);
void anim_update(Anim *a, float dt);
float clip_duration(int clip);
int clip_frames_count(int clip);
int clip_frame_sprite(int clip, int frame);
uint16_t to_f16(float f);
void sprite_inst(int sprite, float x, float y, float z, float sx, float sy, uint8_t tint, Inst *out);
void sprite_inst_rot(int sprite, float x, float y, float z, float sx, float sy, float degrees, uint8_t tint, Inst *out);

/* ---------------------------------------------------------------- the ground (phys.c) */
/* a collider's flags (tools/coll.py) */
enum { CF_TERRAIN = 1, CF_STEEP = 2, CF_NONSLIDER = 4, CF_NOHARDLAND = 8, CF_ROOF = 16, CF_SOLID = 32, CF_NONTHUNKER = 64 };
typedef struct {
  float dist, x, y, nx, ny;   /* where, and the ground's normal there */
  int col;
} PhysHit;
/* a ray from (x, y) along (dx, dy) (a unit vector) for dist: the nearest ground (of colliders with a flag in mask) */
bool phys_ray(float x, float y, float dx, float dy, float dist, uint8_t mask, PhysHit *hit);
uint8_t phys_col_flags(int col);
bool phys_tile_solid(int x, int y);
void phys_collider_enable(int col, bool on);   /* (a breakable's: off when it breaks) */
void phys_collider_shift(int col, float dx, float dy);   /* (moved with its object: by so much from its place) */
void phys_colliders_reset(void);               /* all on (a room starts) */
/* a box and a convex polygon (n points, x y pairs): do they overlap? */
bool box_meets_shape(float x0, float y0, float x1, float y1, const float *pts, int n);
bool shapes_meet(const float *a, int na, const float *b, int nb);   /* (two convex polygons) */
bool box_meets_polygon(float x0, float y0, float x1, float y1, const float *pts, int n);   /* (concave too) */

enum { EV_ENTER, EV_STAY, EV_EXIT };
typedef struct {
  uint8_t kind, col;
  float nx, ny;   /* the contact's normal, towards the body */
} BodyEvent;
#define MAX_CONTACTS 8
#define MAX_EVENTS 16
/* a rigid body with a box collider, as Unity moves it (Rigidbody2D, Box2D) */
typedef struct {
  float x, y, vx, vy;     /* its position (the transform's) and velocity */
  float ox, oy, hx, hy;   /* the box: its center from the position, half its size */
  float gravity_scale;
  float friction;         /* (with the ground's: the materials' combined) */
  uint8_t mask;           /* the colliders that stop it (CF_*) */
  int ncontacts;
  uint8_t ccol[MAX_CONTACTS];
  float cnx[MAX_CONTACTS], cny[MAX_CONTACTS];
  int nevents;            /* the last step's: OnCollisionEnter2D, Stay, Exit (into events, if it wants them) */
  BodyEvent *events;
} Body;
void body_step(Body *b, float dt);

/* ---------------------------------------------------------------- drawing (gfx.c) */
extern float g_cam_x, g_cam_y;   /* the view's center (world units) */
void gfx_frame(void);            /* draws the room at the camera into the screen */
/* a sprite of the Knight, an enemy, an effect... for the next frame, among the room's by its sorting layer and order
 * (SORT_KEY), then depth */
bool gfx_actor(const Inst *in, uint32_t group);
bool gfx_actor_minor(const Inst *in, uint32_t group);   /* (geo, debris, specks: they give way to the others) */
/* the HUD: a sprite in front of all, placed in HUD units from the screen's center (its camera's: 8.7107 units half
 * height), not graded nor faded with the room (tints from HUD_TINT); clip: 0, or 1 + a circle it is drawn in */
bool gfx_hud(const Inst *in, int clip);
void gfx_hud_rect(int clip, float x0, float y0, float x1, float y1);   /* (a clip that is a box, HUD units) */
void gfx_hud_fill(float x0, float y0, float x1, float y1, uint8_t tint);   /* (a box of one color) */
void gfx_hud_clip(int clip, float x, float y, float r);
uint8_t gfx_dyn_tint(int slot, uint8_t r, uint8_t g, uint8_t b, uint8_t a);   /* a tint (for Inst.tint) of that color */
/* the same, flashing: its colors towards (fr, fg, fb) by amount (SpriteFlash) */
uint8_t gfx_dyn_flash(int slot, uint8_t r, uint8_t g, uint8_t b, uint8_t a, uint8_t fr, uint8_t fg, uint8_t fb, uint8_t amount);
extern uint8_t g_screen_fade;   /* 0 .. 255: the screen faded to black */
extern bool g_gfx_no_room;      /* (the room not drawn: the menus') */
extern bool g_gfx_fast;         /* the room drawn at half the height where no HUD, text or menu is (main.c: when slow) */
/* an overlay over the HUD (the inventory): drawn by g_gfx_overlay as the frame is made, its instances (HUD units)
 * straight into the frame's; g_gfx_reserve of them kept from the room's */
extern void (*g_gfx_overlay)(void);
extern int g_gfx_reserve;
bool gfx_overlay(const Inst *in);
void gfx_overlay_fill(float x0, float y0, float x1, float y1, uint8_t tint);
/* a line of text (n glyphs of a style), its pen starting at (x, y) on the baseline (view pixels), of a color (0xAARRGGBB);
 * layer 0: the HUD's (in front of all), 1: the room's (in front of it, behind the HUD and the fade) */
bool gfx_text(int style, float x, float y, const uint8_t *s, int n, uint32_t argb, int layer);

/* ---------------------------------------------------------------- fonts (text.c) */
typedef struct {
  uint8_t w, h;
  int8_t left, top;   /* from the pen, on the baseline (pixels) */
  uint16_t adv;       /* 1/64 pixels; then rows of 4-bit alpha, low nibble first */
} Glyph;
const uint8_t *font_style(int style);
static inline float font_f(const uint8_t *st, int i) {
  float f;
  memcpy(&f, st + 4 * i, 4);
  return f;
}
#define font_em(st) font_f(st, 0)
#define font_line(st) font_f(st, 1)
#define font_asc(st) font_f(st, 2)
#define font_desc(st) font_f(st, 3)
/* a code's glyph, the pen phase (0 .. FONT_PHASES - 1) parts of a pixel right of a whole pixel */
static inline const Glyph *font_glyph(const uint8_t *st, uint8_t c, int phase) {
  /* (the codes it has: from st[16], st[17] of them) */
  if ((uint8_t)(c - st[16]) >= st[17]) return NULL;
  uint16_t o = rd16(st + 20 + 2 * ((c - st[16]) * FONT_PHASES + phase));
  return o ? (const Glyph *)(const void *)(st + o) : NULL;
}
extern uint32_t g_gfx_items, g_gfx_pixels;
