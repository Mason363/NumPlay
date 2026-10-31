/* Temple Run: the mesh table (data.c, made by tools/models.py).
 *
 * Every model the game draws is a Mesh here, by number (data.h: M_RUNNER_RUN0, M_PATH_STRAIGHT...).
 * The game code only asks for a mesh by its number and where to put it, so the art can be made
 * again (tools/models.py, or another tool that writes the same table) without touching it.
 *
 * Model space: game units x 4 (x right, y up, z ahead along the run; a path tile is 60 units, its
 * centre at the origin, its floor at y = 0). Faces are convex polygons, their corners clockwise
 * seen from the front, each with one colour and its normal. */
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

typedef struct {
  const MVert *v;
  const uint16_t *idx;
  const MFace *f;
  uint16_t nf;
  int16_t far;        /* the mesh drawn instead beyond MESH_FAR units (-1: this one) */
  float r;            /* the radius round its origin everything lies in (units) */
} Mesh;

#define MESH_FAR 170.0f
#define MESH_CORNERS 512   /* the most corners a mesh has (tools/apk.py checks it) */
extern const Mesh meshes[];
#endif
