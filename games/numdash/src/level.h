#ifndef NUMDASH_LEVEL_H
#define NUMDASH_LEVEL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "objdefs.h"

#define LEVEL_COUNT 18
#define THEME_COUNT 9          /* editor themes: the colours of the first nine levels */
#define WIN_CAP 2304           /* objects resident at once (leveldata.c checks it) */
#define WIN_CHUNKS 24
#define MAX_LEVEL_OBJS 20480   /* objects in the biggest level */
#define CUSTOM_MAX 800

/* Custom level object: GD units, sorted by x. xf: rotation (90 degree steps,
 * clockwise) in bits 0-1, flip x bit 2, flip y bit 3. */
typedef struct { uint16_t x; int16_t y; uint8_t type, xf; } LObj;

/* Resident object, 12 bytes: x and y in 1/32 units (biased), a scale index
 * in the top 11 bits of xs; rotation (1024 steps, clockwise) and flips in ys. */
typedef struct { uint32_t xs, ys; uint16_t type, paint; } RObj;
#define OBJ_XBIAS (2048 * 32)
#define OBJ_XMASK 0x1fffffu
#define OBJ_YBIAS (1 << 19)
static inline float obj_x(const RObj *o) { return (float)((int32_t)(o->xs & OBJ_XMASK) - OBJ_XBIAS) * (1.0f / 32); }
static inline float obj_y(const RObj *o) { return (float)((int32_t)(o->ys & 0xfffff) - OBJ_YBIAS) * (1.0f / 32); }
static inline unsigned obj_rot(const RObj *o) { return (o->ys >> 20) & 1023; }
static inline unsigned obj_flips(const RObj *o) { return o->ys >> 30; }
static inline unsigned obj_scale(const RObj *o) { return o->xs >> 21; }
/* Quarter turns in bits 0-1, flip x 4, flip y 8 (as LObj), or -1 when the
 * rotation is not a multiple of 90 degrees. */
static inline int obj_xf(const RObj *o) {
  unsigned r = obj_rot(o);
  return (r & 255) ? -1 : (int)((r >> 8) | obj_flips(o) << 2);
}
RObj obj_make(float x, float y, unsigned type, unsigned xf);

typedef struct { uint16_t x; int16_t y; uint8_t kind, arg, r, g, b, flags; uint16_t dur; } LevelEvent;
enum { EVF_TOUCH = 1, EVF_TINT_GROUND = 2, EVF_BLEND = 4 };
/* Colour channels, renumbered densely: the special GD channels first (BG
 * 1000, G1 1001, Line 1002, Obj 1004, 3DL 1003, P1 1005, P2 1006, LBG 1007,
 * G2 1009, black 1010, white 1011, lighter 1012), then the level's own. */
enum { CH_BG = 0, CH_G1 = 1, CH_LINE = 2, CH_OBJ = 3, CH_COUNT = 4, CH_3DL = 4, CH_P1 = 5, CH_P2 = 6, CH_LBG = 7,
       CH_G2 = 8, CH_BLACK = 9, CH_WHITE = 10, CH_LIGHTER = 11, CH_SPECIAL = 12 };

/* 2.0+ levels: initial channels, per-object styles (colours, z layer,
 * groups), group sets and a trigger stream. */
typedef struct { uint8_t rgb[3], flags, opacity; uint16_t copy; int16_t h, s, v; } LChan;   /* s, v: x64; copy 0xffff: none */
enum { CHF_BLEND = 1, CHF_P1 = 2, CHF_P2 = 4, CHF_SADD = 8, CHF_VADD = 16, CHF_COPY = 64 };
/* cz: main colour (bits 0-9, 1023 = the object's own), detail (10-19), z
 * bucket (20-23), flags (24-31) */
typedef struct { uint32_t cz; uint16_t groups; int16_t arg; } LStyle;
#define ST_NONE 1023u
static inline unsigned st_main(const LStyle *s) { return s->cz & 1023; }
static inline unsigned st_detail(const LStyle *s) { return s->cz >> 10 & 1023; }
static inline unsigned st_zlayer(const LStyle *s) { return s->cz >> 20 & 15; }
static inline unsigned st_flags(const LStyle *s) { return s->cz >> 24; }
enum { STF_DONT_FADE = 1, STF_DONT_ENTER = 2, STF_HIDE = 4, STF_NOTOUCH = 8, STF_FREE = 16 };
#define STF_TPGRAV(f) ((f) >> 5 & 3)   /* a teleport's gravity mode: 1 normal, 2 flipped, 3 toggle */
typedef struct {
  const LChan *chans;
  const LStyle *styles;
  const uint16_t *gsets;          /* at a set's offset: count, then dense group indices */
  const int32_t (*scales)[2];     /* x, y scale x1000 by scale index */
  const uint8_t *trig;            /* triggers: each channel's position list, then touch and spawned ones */
  const uint16_t *touch;          /* stream offsets of the touch triggers */
  const uint32_t *chan_off;       /* [16] each channel's list, 0xffffffff = none */
  const uint8_t *chan_frame;      /* [16] the frame (travel) each channel is sorted along */
  const uint16_t *spawn_first;    /* [ngroups + 1] into spawn_list, by dense group */
  const uint16_t *spawn_list;     /* stream offsets of spawned triggers */
  const int16_t (*anchors)[2];    /* [ngroups] where each group's first object is (x, y) */
  uint16_t nchans, ndyn, nstyles, ngroups, nscales;   /* channels below ndyn change (Game.ch) */
  uint32_t trig_len;
  uint16_t ntouch;
  uint8_t bg, ground;             /* GD background and ground styles */
  uint8_t lflags;                 /* LF_* (GD's level compatibility flags) */
  uint32_t end_dist;              /* turned levels: the distance travelled to the end (0: use x) */
} LevelExt;
enum { LF_FIX_RADIUS = 1 };      /* kA39: round hazards by centre distance */
enum { TR_COLOR = 1, TR_MOVE, TR_ALPHA, TR_TOGGLE, TR_PULSE, TR_FADE, TR_TRAIL, TR_SPAWN, TR_STOP, TR_ROTATE, TR_FOLLOW, TR_COUNT,
       TR_PICKUP, TR_COMPARE, TR_COLLISION, TR_TAP, TR_GROT, TR_END, TR_TIMEWARP, TR_SHAKE, TR_CAMERA };
enum { CAM_STATIC, CAM_ZOOM, CAM_OFFSET };

/* A chunk of a built-in level: one DEFLATE stream of objects whose x and y
 * (plus reach) span [x0, x1] x [y0, y1]. fmt 1 = legacy rows. */
typedef struct {
  uint32_t off, base;
  int32_t xq0;
  float x0, x1, y0, y1;
  uint16_t len, count, nrec, rmax;
  uint8_t fmt;
} LChunk;

typedef struct {
  const char *name;
  const uint8_t *data;
  const LChunk *chunks;
  const LevelEvent *events;
  const LevelExt *ext;      /* 2.0+ levels, else NULL */
  uint32_t count;
  uint16_t nchunks, event_count, end_x, wall_x;
  uint32_t coins[3];
  uint8_t bg[3], g1[3], line[3], obj[3];
  uint8_t difficulty, stars, bpm, start_mode, coin_count;
} LevelDef;
extern const LevelDef level_defs[LEVEL_COUNT];

typedef struct Level {
  const char *name;
  const LevelDef *def;      /* built-in level streamed from flash, or NULL */
  const LevelExt *ext;      /* channels, styles, groups and triggers of 2.0+ levels */
  const LObj *flat;         /* custom level: every object resident */
  uint16_t flat_count;
  const LevelEvent *events;
  uint16_t event_count, end_x, wall_x;
  uint32_t count;           /* objects in the whole level */
  uint8_t colors[CH_COUNT][3];
  uint8_t bpm, start_mode, difficulty, stars;
  uint32_t coin_obj[3];     /* object indices of the secret coins, in order */
  uint8_t coin_count;
} Level;

/* Style of an object of a 2.0+ level (the default style otherwise). */
static inline const LStyle *obj_style(const Level *L, const RObj *o) {
  static const LStyle none = {ST_NONE | ST_NONE << 10, 0, 0};
  return L->ext && o->paint < L->ext->nstyles ? &L->ext->styles[o->paint] : &none;
}

/* Scale of an object (x and y; 1 unless a 2.0+ level scales it). */
static inline void obj_scale_xy(const Level *L, const RObj *o, float *sx, float *sy) {
  unsigned k = obj_scale(o);
  if (!k || !L->ext || k >= L->ext->nscales) { *sx = *sy = 1; return; }
  *sx = L->ext->scales[k][0] * 0.001f;
  *sy = L->ext->scales[k][1] * 0.001f;
}
static inline float scale_of(const Level *L, const RObj *o) {
  float sx, sy;
  obj_scale_xy(L, o, &sx, &sy);
  sx = sx < 0 ? -sx : sx;
  sy = sy < 0 ? -sy : sy;
  return sx > sy ? sx : sy;
}

/* Objects near the camera live in one shared window. Iterating a range makes
 * it resident first, so the physics never depends on what was loaded
 * before. Objects come in level order (chunk by chunk, each sorted by x). */
typedef struct {
  const Level *L;
  uint8_t c;               /* current window chunk */
  unsigned i, end, first;
  float lo, hi, ylo, yhi;
  unsigned gi;             /* level-wide index of the object last returned */
} LIter;
void level_iter(const Level *L, float x0, float x1, LIter *it);
/* Only chunks that reach into [y0, y1] (objects come by x alone). */
void level_iter_y(const Level *L, float x0, float x1, float y0, float y1, LIter *it);
const RObj *level_next(LIter *it);
/* Makes objects whose reach overlaps [x0, x1] resident. */
void level_stream(const Level *L, float x0, float x1);
void level_stream_y(const Level *L, float x0, float x1, float y0, float y1);

int inflate_raw(uint8_t *out, size_t cap, const uint8_t *in, size_t len);
bool level_load_builtin(Level *L, unsigned index);
/* Custom levels: objs stays owned by the caller and is re-read on changes. */
void level_load_flat(Level *L, const LObj *objs, unsigned count);
void level_index_coins(Level *L);
bool level_valid(const Level *L);
/* Decodes every chunk of a built-in level and checks its objects (tests). */
bool level_check_all(const Level *L, unsigned *coins);
/* Rotated hitbox half extents in GD units. */
void obj_hitbox(const RObj *o, float *hw, float *hh);
/* Scratch memory shared by chunk decoding and custom level saving. */
#define LEVEL_SCRATCH 6400
/* Shared scratch memory: a chunk being decoded, or (between decodes) the
   glyph outline buffers of gfx.c (GFX_SCRATCH bytes). */
#define SCRATCH_BYTES 8064
extern uint8_t level_scratch[SCRATCH_BYTES];
/* The window's memory as LObj scratch (it is emptied), for old saves. */
LObj *level_big_scratch(unsigned *cap);
/* The editor's custom level. */
extern LObj *const level_objs;
#endif
