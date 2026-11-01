/* Temple Run: the mesh table (data.c, made by tools/models.py).
 *
 * Every model the game draws is a Mesh here, by number (data.h: M_RUNNER_RUN0, M_PATH_STRAIGHT...).
 * The game code only asks for a mesh by its number and where to put it, so the art can be made
 * again (tools/models.py, or another tool that writes the same table) without touching it.
 *
 * Model space: game units x 4 (x right, y up, z ahead along the run; a path tile is 60 units, its
 * centre at the origin, its floor at y = 0). Faces are convex polygons, their corners clockwise
 * seen from the front, each with one colour and its normal; the game's own are laid with its pictures
 * (r_tex), each corner in its place in one. */
#ifndef TR_MODELS_H
#define TR_MODELS_H
#include <stdint.h>

typedef struct {
  int16_t x, y, z;
} MVert;

typedef struct {
  uint16_t first;     /* its corners: idx[first .. first + n) */
  uint8_t n;
  int8_t nx, ny, nz;  /* its normal x 127 */
  uint8_t flags;      /* 1: two-sided */
  uint8_t pad;
  uint32_t rgb;
} MFace;

/* a triangle laid with a picture: its corners tidx[3 i ..], their places in the picture uv[6 i ..] (u and v in 1/4
 * texels) */
typedef struct {
  uint8_t light;      /* 0..255 */
  uint8_t tex;        /* which picture: r_tex */
  uint16_t avg;       /* RGB565: what it shows there on average, drawn far off (0: nothing, it is mostly see-through) */
} TFace;

typedef struct {
  const MVert *v;
  const uint16_t *idx;
  const MFace *f;
  const uint16_t *tidx;
  const int16_t *uv;
  const TFace *t;
  uint16_t nf, nt;
  int16_t far;        /* the mesh drawn instead beyond MESH_FAR units (-1: this one) */
  float r;            /* the radius round its origin everything lies in (units) */
} Mesh;

#define MESH_FAR 95.0f
#define MESH_CORNERS 768   /* corners scene_mesh moves into place once each (a mesh with more: each face's) */
extern const Mesh meshes[];
#endif
