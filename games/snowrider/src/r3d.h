/* Snow Rider 3D: the 3D renderer (Subway Surfers', with a camera turned any way).
 *
 * The world is polygons: flat ones, shaded ones (a light level at each corner, blended across: round
 * things look round) and textured ones (a picture laid on, in perspective for big faces like a train's
 * side). Each frame they are moved in front of the camera, cut at the edges of the view, projected and
 * kept in a list; then the screen is drawn in bands of BAND_H rows, each one its background (sky and the
 * ground, scene.c, which leaves the ground's depth where it hides what is below it), the polygons that
 * cross it with a depth buffer, the see-through ones after them, and
 * what is drawn flat over the picture (ui). The whole screen never has to fit in RAM: a band and its
 * depths do. */
#ifndef SR_R3D_H
#define SR_R3D_H
#include <stdbool.h>
#include <stdint.h>
#include "plat.h"

#define BAND_H 20
#define NBANDS (SCREEN_H / BAND_H)

typedef struct {
  float x, y, z;
} V3;

/* the band being drawn: its colours (SCREEN_W x BAND_H) and rows [r_y0, r_y1) */
extern uint16_t *r_col;
extern int r_y0, r_y1;
void r_set_band_memory(uint16_t *band);
/* the frame's lists (polygons, corners, depths): r_work_size bytes, given before anything is drawn */
uint32_t r_work_size(void);
void r_init(void *mem);

/* the camera: where it is, its turn (rows: its right, up and ahead in the world), its focal length and the
 * screen point straight ahead */
typedef struct {
  float x, y, z, r[9], f, cx, cy;
} Camera;
extern Camera cam;
void r_camera(float x, float y, float z, const float r[9], float f, float cx, float cy);
/* camera space (x right, y up, z ahead) of a world point */
static inline V3 r_view(V3 p) {
  float dx = p.x - cam.x, dy = p.y - cam.y, dz = p.z - cam.z;
  return (V3){cam.r[0] * dx + cam.r[1] * dy + cam.r[2] * dz, cam.r[3] * dx + cam.r[4] * dy + cam.r[5] * dz,
              cam.r[6] * dx + cam.r[7] * dy + cam.r[8] * dz};
}
/* the screen point of a world point; false behind the camera */
bool r_project(V3 p, float *sx, float *sy);

/* distance haze: from HAZE0 to HAZE1 metres ahead colours go to the horizon's */
extern float r_haze0, r_haze1;
/* beyond r_far (m) a baked face with an average colour is drawn in it, plain */
extern float r_far;
extern uint32_t r_haze_rgb;

/* pictures laid on polygons: width and height are powers of two; index 0 can be see-through */
typedef struct {
  uint8_t wl, hl;          /* log2 of the width and height */
  uint8_t clear;           /* 1: index 0 is not drawn */
  uint8_t pad;
  const uint8_t *px;       /* palette indices, row by row */
  const uint16_t *pal;     /* RGB565 */
} Texture;
extern const Texture *const r_tex[];   /* the game's textures, by number (data.c) */

/* polygons: their corners in order (clockwise on the screen when seen from the front: the back is not
 * drawn unless P_TWOSIDED), a colour already lit (r_shade) */
enum {
  P_TWOSIDED = 1,
  P_DECAL = 2,      /* lies on another polygon: drawn over it */
  P_ALPHA = 4,      /* see-through (alpha 0..255): drawn last, no depth written */
  P_NOHAZE = 8,
  P_DECAL2 = 16,    /* lies on a decal */
  P_DECAL3 = 32,    /* lies on that */
};
void r_begin(void);
/* a polygon already in camera space, its colour RGB565 (alpha 0..255 with P_ALPHA) */
void r_poly_cam(const V3 *v, int n, uint16_t c565, int flags, int alpha);
void r_poly(const V3 *v, int n, uint32_t rgb, int flags, int alpha);
/* a textured polygon, in perspective: uv gives each corner's place in the picture (in texels, two
 * floats a corner; the picture repeats), light 0..256 */
void r_poly_tex(const V3 *v, const float *uv, int n, int tex, int light, int flags);
/* the same, drawn plain in avg (RGB565, 0: never) beyond r_far */
void r_poly_tex_avg(const V3 *v, const float *uv, int n, int tex, int light, int flags, uint16_t avg);

/* meshes (people, the dog): corners in camera space with a light level (0..255) and a place in a
 * picture (1/4 texels, below 4096), triangles of three corners and a colour (an index into pal; TEX_COL:
 * the picture tex) */
typedef struct {
  float x, y, z;
  uint16_t u, v;
  uint8_t s, pad[3];
} RV;
typedef struct {
  uint8_t a, b, c, col;
} Tri;
#define TEX_COL 255
void r_mesh(const RV *v, int nv, const Tri *t, int nt, const uint16_t *pal, int tex);

/* a box: centre, its three half-axes (x, y and z sides), a colour each for the top, sides, front and
 * back, and flags for the faces to leave out (1 top, 2 bottom, 4 -x, 8 +x, 16 front -z, 32 back +z) */
void r_box(V3 c, V3 ax, V3 ay, V3 az, const uint32_t *rgb6, int skip);
/* an axis-aligned box from (x0,y0,z0) to (x1,y1,z1), one colour lit by face */
void r_cube(float x0, float y0, float z0, float x1, float y1, float z1, uint32_t rgb, int skip);
int r_count(void);   /* polygons this frame */
bool r_full(void);   /* the list is (nearly) full: what comes next may be left out */

/* sine and cosine (short series: no C library) */
void r_sincos(float a, float *s, float *c);
/* lighting: the colour of a face whose normal is n */
uint32_t r_shade(uint32_t rgb, V3 n);
uint32_t r_mix888(uint32_t a, uint32_t b, int t256);
static inline uint16_t r_565(uint32_t c) {
  return (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x1F));
}
__attribute__((always_inline)) static inline uint16_t r_mix565(uint16_t a, uint16_t b, int t32) {   /* t32 = 0 (a) .. 32 (b) */
  uint32_t x = (a | (uint32_t)a << 16) & 0x07E0F81F, y = (b | (uint32_t)b << 16) & 0x07E0F81F;
  x = (x + ((y - x) * (uint32_t)t32 >> 5)) & 0x07E0F81F;
  return (uint16_t)(x | x >> 16);
}

/* draws the frame: bg fills each band's colours (sky, ground), the polygons go over it, then
 * overlay draws flat things over the band (both may be NULL) */
typedef void (*r_band_fn)(void);
void r_render(r_band_fn bg, r_band_fn overlay);
/* for bg: a surface it draws hides what is behind it. Row y of the band, pixels [x0, x1), lies at depth d:
 * r_floor_depth(z) for a floor seen at z metres ahead of the camera (a hair farther, so that what lies on it,
 * a shadow, is still drawn) */
uint16_t r_floor_depth(float z);
void r_band_depth(int y, int x0, int x1, uint16_t d);
#endif
