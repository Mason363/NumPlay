/* Celeste for the NumWorks calculator: shared declarations.
 *
 * Inspired by Celeste, not affiliated with Maddy Makes Games.
 * Made by Mason Chen as part of NumPlay.
 *
 * The game runs Celeste's own levels, art and text (converted by
 * tools/pack.py into data.bin) on a small engine shaped like Monocle, the
 * game's engine: entities with depths, actors and solids that move a pixel at
 * a time, a fixed 60 updates a second. Gameplay is drawn at Celeste's 320x180,
 * pixel for pixel, in the middle of the 320x240 screen. */
#ifndef CELESTE_H
#define CELESTE_H
#include <math.h>
/* sinf, cosf and powf of src/util.c: smaller than the C library's (which reduces any angle exactly): the angles here
 * are small, the powers easings */
float c_sinf(float x);
float c_cosf(float x);
void c_sincosf(float x, float *s, float *c);
float c_powf(float a, float b);
#define sinf(x) c_sinf(x)
#define cosf(x) c_cosf(x)
#define sincosf(x, s, c) c_sincosf(x, s, c)
#define powf(a, b) c_powf(a, b)
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "data.h"

#define WEAK __attribute__((weak))

#define SCREEN_W 320
#define SCREEN_H 240
#define VIEW_W 320
#define VIEW_H 180
#define VIEW_Y 30
#define RAW_DT 0.0166667f   /* Engine.RawDeltaTime: XNA's 166667-tick frame */
extern float g_dt;           /* Engine.DeltaTime: RawDeltaTime * TimeRate */
extern float g_time_rate;    /* Engine.TimeRate (slow motion in cutscenes) */
#define DT g_dt

/* ---------------------------------------------------------------- data.bin */
extern const uint8_t *cel_bin;
static inline uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static inline int16_t rds16(const uint8_t *p) { return (int16_t)rd16(p); }
static inline uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static inline int8_t rds8(const uint8_t *p) { return (int8_t)p[0]; }
static inline int32_t rds32(const uint8_t *p) { return (int32_t)rd32(p); }
static inline float rdf(const uint8_t *p) { uint32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }
const uint8_t *section(int s);
const char *str(uint16_t id);
/* "a" + "b" + n as two digits (n < 0: none) into buf: names of textures */
char *path2(char *buf, const char *a, const char *b, int n);

/* ---------------------------------------------------------------- platform */
enum {
  K_LEFT = 1 << 0, K_RIGHT = 1 << 1, K_UP = 1 << 2, K_DOWN = 1 << 3,
  K_JUMP = 1 << 4, K_DASH = 1 << 5, K_GRAB = 1 << 6, K_TALK = 1 << 7,
  K_PAUSE = 1 << 8, K_HOME = 1 << 9, K_OK = 1 << 10, K_BACK = 1 << 11, K_JOURNAL = 1 << 12,
  K_ANY = 1 << 15
};
uint32_t plat_keys(void);
extern uint8_t g_bind[4];          /* jump, dash, grab, talk: EADK keys */
#define KEY_BACK 5                 /* (EADK keys) */
#define KEY_BACKSPACE 17           /* pause */
const char *key_label(int k);      /* an EADK key's name, as on the key sheet */
void plat_default_binds(void);
int plat_key_pressed(void);        /* the EADK key that went down this frame, -1 if none */
int uitoa(int v, char *out);       /* v in digits, 0-terminated; the length */
void time_text(uint32_t frames, char *out);   /* a time as the game shows it: 1:23.456 */
uint32_t plat_millis(void);
void plat_sleep(uint32_t ms);
void plat_push(int x, int y, int w, int h, const uint16_t *px);
void plat_fill(int x, int y, int w, int h, uint16_t c);
bool plat_save(const char *name, const void *data, uint32_t len);
const uint8_t *plat_load(const char *name, uint32_t *len);

/* ---------------------------------------------------------------- math */
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float approach(float v, float t, float d) { return v <= t ? fminf(v + d, t) : fmaxf(v - d, t); }
static inline float signf(float v) { return v > 0 ? 1.f : v < 0 ? -1.f : 0.f; }
static inline int signi(int v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline int sgn(float v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }
static inline float yoyo(float v) { return v <= .5f ? v * 2 : 1 - (v - .5f) * 2; }   /* Calc.YoYo */
static inline uint8_t a8(float a) { return (uint8_t)(clampf(a, 0, 1) * 255 + 0.5f); }   /* an alpha 0..1 as a byte */
static inline int roundi(float v) { return (int)rintf(v); }   /* Math.Round: to even */
typedef struct { float x, y; } V2;
static inline V2 v2(float x, float y) { return (V2){x, y}; }
static inline V2 v2add(V2 a, V2 b) { return (V2){a.x + b.x, a.y + b.y}; }
static inline V2 v2sub(V2 a, V2 b) { return (V2){a.x - b.x, a.y - b.y}; }
static inline V2 v2mul(V2 a, float k) { return (V2){a.x * k, a.y * k}; }
static inline float v2len(V2 a) { return sqrtf(a.x * a.x + a.y * a.y); }
static inline V2 v2norm(V2 a) { float l = v2len(a); return l > 0 ? (V2){a.x / l, a.y / l} : a; }
static inline V2 safe_norm(V2 v, float l) { float n = v2len(v); return n > 0 ? v2mul(v, l / n) : v2(0, 0); }   /* Calc.SafeNormalize */
static inline V2 approach_v(V2 v, V2 t, float m) {   /* Calc.Approach(Vector2) */
  if (m == 0 || (v.x == t.x && v.y == t.y)) return v;
  V2 d = v2sub(t, v);
  float l = v2len(d);
  return l < m ? t : v2add(v, v2mul(d, m / l));
}
static inline float vangle(V2 v) { return atan2f(v.y, v.x); }   /* Calc.Angle */
static inline V2 v2lerp(V2 a, V2 b, float t) { return (V2){a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}; }
static inline V2 angle_vec(float a, float len) { return (V2){cosf(a) * len, sinf(a) * len}; }   /* Calc.AngleToVector */
/* Calc.ClampedMap */
static inline float clamped_map(float v, float min, float max, float nmin, float nmax) {
  float t = (v - min) / (max - min);
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  return nmin + (nmax - nmin) * t;
}
static inline bool v2eq(V2 a, V2 b) { return a.x == b.x && a.y == b.y; }
#define PI_F 3.14159265f

/* Calc.Random: .NET's System.Random */
typedef struct { int32_t seed[56]; int inext, inextp; } NetRandom;
void rnd_seed(NetRandom *r, int32_t seed);
int32_t rnd_next(NetRandom *r);                 /* [0, int max) */
int32_t rnd_range(NetRandom *r, int32_t max);   /* [0, max) */
float rnd_float(NetRandom *r);                  /* [0, 1) */
extern NetRandom g_rnd;                          /* Calc.Random */
static inline float rndf(void) { return rnd_float(&g_rnd); }
static inline float rnd_rangef(float a, float b) { return a + rndf() * (b - a); }
static inline int rndi(int n) { return rnd_range(&g_rnd, n); }

/* eases (Monocle Ease) */
float ease_cube_out(float t);
float ease_cube_in(float t);
float ease_cube_inout(float t);
float ease_sine_in(float t);
float ease_sine_out(float t);
float ease_sine_inout(float t);
float ease_quad_out(float t);
float ease_quad_in(float t);
float ease_back_out(float t);
float ease_back_in(float t);
float ease_bounce_out(float t);
float ease_elastic_out(float t);
float ease_big_back_in(float t);
float ease_expo_out(float t);

/* ---------------------------------------------------------------- textures (res.c) */
typedef struct {
  uint16_t w, h;      /* stored pixels (trimmed) */
  int16_t ox, oy;     /* where they sit in the frame */
  uint16_t fw, fh;    /* the frame */
  uint16_t pal;
  uint8_t fmt;        /* TF_RLE or TF_RAW4, and for RLE: TF_ROW8, TF_NIB */
  uint8_t scale;      /* drawn this many times bigger (smooth gradients) */
  const uint8_t *px;  /* RLE: u16 row offsets (h + 1) (TF_ROW8: u8 row lengths (h)) then ops; RAW: w * h indices */
  const uint8_t *map; /* TF_NIB: its colors (count, then their palette indices) */
} Tex;
enum { TF_RLE = 0, TF_RAW4 = 2, TF_ROW8 = 4, TF_NIB = 8, TF_SHARED = 16 };   /* ROW8, NIB, SHARED: drawn from flash only */
#define ROW8_MAX 240   /* TF_ROW8 rows are shorter (lengths from it up: tools/pack.py direct_rows) */
void tex_row(const Tex *t, int r, uint8_t *out);   /* row r's palette indices (0: transparent) */
bool tex_get(uint16_t id, Tex *t);         /* false if not loaded (packed and not cached) */
const uint16_t *pal_colors(uint16_t pal, int *n);
const uint8_t *pal_alpha(uint16_t pal);
bool pal_opaque(uint16_t pal);   /* every color fully opaque */
void res_init(void);
/* the texture cache: what a room needs is loaded by res_need() */
void res_begin_need(void);
void res_need(uint16_t tex);
void res_need_list(const uint8_t *ids, int n);
void res_load_needed(void);
/* room packs: copies [off, off + size) of pack p's stream through sink */
typedef void (*Sink)(const uint8_t *p, uint32_t n, void *ctx);
void res_stream(uint16_t pack, uint32_t off, uint32_t size, Sink sink, void *ctx);
uint8_t *arena_scratch(uint32_t *size);
uint8_t *res_scratch(uint32_t need, uint32_t *size);
void res_load_frames(const uint16_t *ids, int n, uint16_t hard);   /* an animation's frames: `hard` surely, the others if they fit */   /* the cache's free top, `need` bytes made free if it can */
void res_load(const uint16_t *ids, int n);              /* loads those not loaded, in one pass */
bool res_cached(uint16_t id);
extern uint8_t *g_chapter_ram;           /* the chapter files' own tables, while it is played */
extern uint8_t *g_chram[16];             /* each chapter file's (src/chN.c) part of it */
void res_chapter_ram(uint32_t bytes);
uint16_t *res_picture_ram(uint32_t bytes);   /* the menus' big picture, at the same place */
extern uint8_t g_res_tops;                  /* changes when they take it (or a chapter does) */
void res_flush(void);
int tex_half_extent(uint16_t id);   /* half the bigger side of its frame, known without loading it */
extern uint16_t g_res_frame;
extern bool g_res_can_load;
extern bool g_res_drawing;
uint16_t res_tex_by_name(const char *path);   /* 0xFFFF: none */    /* free RAM while nothing else needs it */

/* ---------------------------------------------------------------- drawing (gfx.c) */
/* GF_ADD | GF_ADDALPHA: XNA's BlendState.Additive, the color also weighed by its alpha */
enum { GF_FLIPX = 1, GF_FLIPY = 2, GF_ADD = 4, GF_SCALE = 8, GF_HUD = 16, GF_SILHOUETTE = 32, GF_ADDALPHA = 64 };
void gfx_begin(void);
void gfx_end(void);                             /* draw the strips and push them */
void gfx_clear(uint16_t c);
/* a texture with its frame's (0,0) at world (x, y); tint RGB565 0xFFFF = none */
void gfx_tex(uint16_t tex, float x, float y, uint8_t flags, uint16_t tint, uint8_t alpha);
/* drawn like Monocle's MTexture.Draw(position, origin, color, scale, rotation, flip) */
void gfx_tex_ex(uint16_t tex, float x, float y, float ox, float oy, float sx, float sy, float rot, uint16_t tint, uint8_t alpha, uint8_t flags);
void gfx_banner(uint16_t tex, float x, float y, uint8_t flags, uint16_t ph, uint16_t prm);   /* Decal.Banner (gfx.c) */
void gfx_zoom(float zoom, float focus_x, float focus_y);   /* Level.Zoom around a screen point */
void gfx_screen(bool on);   /* screen coordinates for gameplay layers (zoomed with the world) */
/* part of a texture (tile sheets): sub-rect (sx, sy, w, h) of the frame */
void gfx_tex_part_ex(uint16_t tex, float x, float y, int sx, int sy, int w, int h, float ox, float oy, float scx, float scy,
                     float rot, uint16_t tint, uint8_t alpha, uint8_t flags);
void gfx_tex_part(uint16_t tex, float x, float y, int sx, int sy, int w, int h, uint8_t flags, uint16_t tint, uint8_t alpha);
#define TILE_POOL 1600
extern uint8_t g_tile_pool[TILE_POOL];   /* the blocks' tiles (entities.c), which gfx_tiles draws */
void gfx_tiles(uint16_t tex, const uint8_t *q, int w, int h, float x, float y, uint16_t tint, uint8_t alpha);
/* Celeste's nine-slice blocks (8x8 cells of a 24x24 texture over w x h, multiples of 8), one command; hollow: no middle */
void gfx_nine(uint16_t tex, float x, float y, int w, int h, bool hollow, uint16_t tint, uint8_t alpha);
/* the part (0, 0, tw, th) of a frame repeated over w x h, the last ones cut: one command */
void gfx_tiled(uint16_t tex, float x, float y, int w, int h, int tw, int th, uint16_t tint, uint8_t alpha);
void gfx_rect(float x, float y, float w, float h, uint16_t c, uint8_t alpha);
void gfx_hollow_rect(float x, float y, float w, float h, uint16_t c, uint8_t alpha);
void gfx_line(float x0, float y0, float x1, float y1, uint16_t c, uint8_t alpha);
void gfx_pixel(float x, float y, uint16_t c, uint8_t alpha);
void gfx_circle(float x, float y, float r, uint16_t c, uint8_t alpha, int resolution);
/* layers drawn by callbacks, once per strip: y0 and y1 are view rows */
typedef void (*StripFn)(uint16_t *strip, int y0, int y1, void *ctx);
void gfx_custom(StripFn fn, void *ctx, int y0, int y1);
/* the menus' big pictures (pic.c): decoded half size, drawn twice as big at view (x, y) by pic_strip (a gfx_custom) */
typedef struct { const uint16_t *px; int16_t w, h, x, y; uint8_t alpha; } PicLayer;
int pic_count(void);
bool pic_decode(int id, uint16_t *out, int *w, int *h);
void pic_strip(uint16_t *strip, int sy0, int sy1, void *ctx);
void gfx_keep(bool on);    /* the layers that must be drawn (tiles, stylegrounds in front): commands kept for them */
void blit_line(uint16_t *strip, int sy0, int sy1, float x0, float y0, float x1, float y1, uint16_t col, uint8_t alpha);
void blit_rect(uint16_t *strip, int sy0, int sy1, float x, float y, float w, float h, uint16_t col, uint8_t alpha);
/* gfx_tex_ex drawn into the strip by a StripFn layer (world coordinates; the texture got when the layer was recorded) */
void blit_tex_ex(uint16_t *strip, int sy0, int sy1, uint16_t tex, float x, float y, float ox, float oy, float sx, float sy,
                 float rot, uint16_t tint, uint8_t alpha, uint8_t flags);
void gfx_camera(float x, float y);              /* world position of the view's top left */
extern int g_camx, g_camy;                      /* floored */
void gfx_hud(bool on);                          /* following draws ignore the camera */
#define GFX_STRIP_BYTES 9600   /* the strip, on main's stack on the calculator (gfx_set_memory) */
#define GFX_CMD_BYTES 7200     /* a frame's draw commands, likewise */
void gfx_set_memory(void *strip, void *cmd);
void gfx_present_all(void);                     /* letterbox and full redraw next frame */
void gfx_top_label(const char *s);              /* a line of text in the top bar ("": none) */
static inline uint16_t rgb(uint32_t c) { return (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x7E0) | ((c >> 3) & 0x1F)); }
uint16_t blend565(uint16_t d, uint16_t s, int a);    /* a: 0..256 */
static inline uint16_t scale565(uint16_t c, int a) {   /* c * a / 256 */
  uint32_t r = ((c >> 11) * a) >> 8, g = (((c >> 5) & 63) * a) >> 8, b = ((c & 31) * a) >> 8;
  return (uint16_t)(r << 11 | g << 5 | b);
}
uint16_t mul565(uint16_t c, uint16_t tint);
/* blitting into a strip (for StripFn layers) */
void blit_tex(uint16_t *strip, int sy0, int sy1, const Tex *t, int x, int y, uint8_t flags, uint16_t tint, uint8_t alpha);
void blit_part_ex(uint16_t *strip, int sy0, int sy1, const Tex *t, int x, int y, int sx, int sy, int w, int h, uint8_t flags,
                  uint16_t tint, uint8_t alpha);   /* sub-rect (sx, sy, w, h) of the frame at strip-relative (x, y) */
void blit_turned(uint16_t *strip, int sy0, int sy1, const Tex *t, int x, int y, int q, uint8_t flags, uint16_t tint);
void blit_cells(uint16_t *strip, int sy0, int sy1, const Tex *t, float x, float y, int ncx, int ncy, const uint8_t *cells,
                float gx, float gy, float scx, float scy, uint16_t tint, uint8_t alpha);
/* a 4-bit texture (TF_RAW4) centered on screen (cx, cy) (its frame's (ox, oy)), rotated (cos, sin), scaled 1 / inv */
typedef struct { const Tex *t; float cx, cy, ox, oy, cs, sn, inv; const uint16_t *lc, *la; } Layer;   /* lc, la: pal_lut's */
void blit_layers(uint16_t *strip, int sy0, int sy1, const Layer *l, int n, int x0, int y0, int x1, int y1,
                 uint16_t (*outline)(int x, int y, void *ctx), void *ctx, uint32_t *mask);
void pal_lut(uint16_t pal, uint16_t tint, uint16_t *col, uint16_t *alpha);
void blit_raw4(uint16_t *strip, int sy0, int sy1, const Tex *t, int x, int y, const uint16_t *lc, const uint16_t *la);
void blit_edges(uint16_t *strip, int sy0, int sy1, const uint32_t *mask, uint16_t (*outline)(int x, int y, void *ctx), void *ctx);

/* ---------------------------------------------------------------- sprites (sprite.c) */
typedef struct {
  uint8_t bank;
  uint8_t anim;           /* 255: none */
  uint8_t frame;
  uint8_t playing;
  float timer, rate;
  float ox, oy;           /* origin */
  float sx, sy;           /* scale */
  float rot;
  uint16_t tint;
  uint8_t alpha;
  uint8_t flipx, flipy;
  uint8_t visible;
  uint8_t justify;        /* origin follows the frame (Justify) */
  uint8_t raw;            /* UseRawDeltaTime: runs through slow motion */
  float jx, jy;
  int8_t last_goto;
} Sprite;
void spr_init(Sprite *s, int bank);
int bank_anim_textures(int bank, int anim, uint16_t *out, int max, uint16_t *gotos, int *ngotos);
void spr_play(Sprite *s, int anim, bool restart);
void spr_play_offset(Sprite *s, int anim, int frame);
void spr_update(Sprite *s);
uint16_t spr_tex(const Sprite *s);
int spr_frames(const Sprite *s);
void spr_draw(const Sprite *s, float x, float y);
bool spr_is(const Sprite *s, int anim);
int bank_anim_count(int bank);
float spr_anim_duration(int bank, int anim);
extern void (*spr_on_frame)(Sprite *s, int anim);   /* unused hook */

void gfx_mask4(const uint8_t *bits, int w, int h, float x, float y, uint16_t col, uint8_t alpha);

/* ---------------------------------------------------------------- text (font.c): Renogare */
enum { FONT_S, FONT_M };   /* the game's 64 px face at 1/6, and its 192 px face at 1/9 (twice as big) */
float font_line_height(int size);
float font_measure(const char *s, int n, int size);        /* width in screen pixels of n bytes (UTF-8) */
float font_glyph_advance(int size, uint32_t code, uint32_t next);
/* draws at (x, y) = top-left of the line, screen coordinates (gfx_hud); returns the width */
float font_draw(const char *s, int n, float x, float y, int size, uint16_t col, uint8_t alpha);
void font_draw_glyph(int size, uint32_t code, float x, float y, uint16_t col, uint8_t alpha);
void font_draw_outline(const char *s, int n, float x, float y, int size, uint16_t col, uint16_t outline, uint8_t alpha);
/* ActiveFont.Draw with justify: (jx, jy) in 0..1 of the text's size */
void font_draw_justified(const char *s, float x, float y, float jx, float jy, int size, uint16_t col, uint8_t alpha);
uint32_t utf8_next(const char **s, const char *end);
/* a glyph: u16 code, s16 xo x16, s16 yo, u8 w, u8 h, u16 advance x16, u32 bitmap (NULL if none) */
const uint8_t *font_glyph_rec(int size, uint32_t code);
int font_kerning_x16(int size, uint32_t a, uint32_t b);
const uint8_t *font_glyph_bits(int size, const uint8_t *g);

/* ---------------------------------------------------------------- input (Celeste's Input) */
typedef struct {
  bool check, edge, released;   /* edge: pressed this frame (the key's Node.Pressed) */
  float buffer, buffer_time;
  bool consumed;
} Button;
typedef struct {
  Button jump, dash, grab, talk, pause, confirm, back;
  int move_x, move_y;          /* Input.MoveX/MoveY */
  int aim_x, aim_y;            /* Input.Aim, 8 directions */
  int feather_x, feather_y;
  uint32_t keys, prev;
} Input;
extern Input g_in;
void input_update(uint32_t keys);
/* VirtualButton.Pressed, ConsumePress, ConsumeBuffer */
static inline bool btn_pressed(const Button *b) { return !b->consumed && (b->buffer > 0 || b->edge); }
static inline void btn_consume(Button *b) { b->buffer = 0; b->consumed = true; }
static inline void btn_consume_buffer(Button *b) { b->buffer = 0; }

/* ---------------------------------------------------------------- the game */
extern uint32_t g_frame;
extern bool g_running;
void game_init(void);
void game_frame(void);   /* one update at 60 Hz (and drawing when due) */
void game_draw(void);
void game_save(void);
#endif
