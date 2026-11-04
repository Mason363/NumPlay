/* Moto X3M: the pictures, fonts and levels in data.bin (tools/pack.py). */
#ifndef MX_RES_H
#define MX_RES_H
#include <stdint.h>
#include "data.h"
#include "nape.h"

extern const uint8_t mx_data[];

/* a picture: its colours (RGB565, then alpha 0-255), then a byte a pixel, or two (IMG_4BIT, high bits first) */
typedef struct {
  uint16_t w, h;
  float px, py;            /* its pivot */
  uint16_t ncolors;
  uint8_t flags, pad;
} Pic;
enum { IMG_TILE = 1, IMG_OPAQUE = 2, IMG_4BIT = 4 };

const Pic *pic(int id);
static inline const uint16_t *pic_pal(const Pic *p) { return (const uint16_t *)(const void *)(p + 1); }
static inline const uint8_t *pic_alpha(const Pic *p) { return (const uint8_t *)(pic_pal(p) + p->ncolors); }
static inline const uint8_t *pic_px(const Pic *p) {
  uintptr_t a = (uintptr_t)(pic_alpha(p) + p->ncolors);
  return (const uint8_t *)((a + 3) & ~(uintptr_t)3);
}
static inline int pic_stride(const Pic *p) { return p->flags & IMG_4BIT ? (p->w + 1) >> 1 : p->w; }
static inline unsigned pic_at(const Pic *p, const uint8_t *px, int x, int y) {
  if (p->flags & IMG_4BIT) {
    uint8_t b = px[y * ((p->w + 1) >> 1) + (x >> 1)];
    return x & 1 ? b & 15 : b >> 4;
  }
  return px[y * p->w + x];
}

typedef struct {
  uint8_t w, h;
  int8_t x, y;             /* from the pen; y from the baseline */
  uint8_t adv, pad0;
  uint16_t pad1;
  uint32_t off;            /* 4-bit coverage, two pixels a byte */
} Glyph;
typedef struct {
  uint8_t first, count, height, ascent;
} Font;
const Font *font(int id);
static inline const Glyph *font_glyphs(const Font *f) { return (const Glyph *)(const void *)(f + 1); }
static inline const uint8_t *font_bits(const Font *f) { return (const uint8_t *)(font_glyphs(f) + f->count); }

/* ---- levels ---- */

typedef struct {
  uint8_t theme, nmats, pad0, pad1;
  uint16_t nobj, nsshapes, nsbodies, ndbodies, ndshapes, nbuckets, ndraws, ncams, naux, pad2;
  float gravity, prize[3], startx, starty, finishx, bucket_x0;
  uint32_t off[15];   /* mats, sshapes, sverts, sbodies, buckets, blist, dbodies, dshapes, ddata, objs, aux, draws,
                         dpts, params, cams */
} LevelHdr;
enum { L_MATS, L_SSHAPES, L_SVERTS, L_SBODIES, L_BUCKETS, L_BLIST, L_DBODIES, L_DSHAPES, L_DDATA, L_OBJS, L_AUX,
       L_DRAWS, L_DPTS, L_PARAMS, L_CAMS };

typedef struct {
  uint16_t cb;
  uint8_t flags, pad;
  float viscosity, density;
} SBodyRec;

typedef struct {
  float x, y, rot;
  uint16_t first_shape;
  uint8_t nshapes, type;   /* 1 dynamic, 2 kinematic */
  uint16_t cb;
  uint8_t flags, pad;      /* BF_NOROT */
} DBodyRec;

typedef struct {
  uint8_t type, n, filter, flags, mat, pad[3];   /* type 0 circle, 1 polygon */
  float radius, cx, cy, area, inertia;
  uint32_t off;            /* local vertices in ddata */
} DShapeRec;

enum { O_GROUND, O_SHAPER, O_PLANK, O_DYN, O_REMOVED, O_TNT, O_SPIKES, O_TOGGLE, O_SAFE, O_FINISH, O_MOVER, O_MOTOR,
       O_PIVOT, O_DECOR, O_WATER, O_GEYSER, O_BOOST, O_GLASS };

typedef struct {
  uint8_t type, flags;
  int16_t id, id2, body, sbody, body2;
  uint16_t img, naux, aux, params;
  float x, y, rot;
} ObjRec;

enum { D_POLY, D_STRIP, D_SPRITE, D_WATER };
enum { C_SHAPES, C_LAND, C_OBJ, C_FRONT };
typedef struct {
  uint8_t kind, cont, flags, back;
  int16_t owner;           /* the object whose body carries it, or -1 */
  uint16_t n, img, pts, params;
  int16_t minx, miny, maxx, maxy;
  uint16_t pad;
} DrawRec;

typedef struct {
  uint16_t pts, n;
  float dy;
} CamRec;

const LevelHdr *level(int n);   /* 0-based */
static inline const void *lvl_at(const LevelHdr *l, int part) { return (const uint8_t *)l + l->off[part]; }
#endif
