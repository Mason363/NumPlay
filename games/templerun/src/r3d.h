/* Temple Run: the 3D renderer.
 *
 * The world is polygons: flat ones, shaded ones (a light level at each corner, blended across: round
 * things look round) and textured ones (a picture laid on, in perspective for big faces like a train's
 * side). Each frame they are moved in front of the camera, cut at the edges of the view, projected and
 * kept in a list; then the screen is drawn in bands of BAND_H rows, each one its background (sky and the
 * ground, scene.c, which leaves the ground's depth where it hides what is below it), the polygons that
 * cross it with a depth buffer, the see-through ones after them, and
 * what is drawn flat over the picture (ui). The whole screen never has to fit in RAM: a band and its
 * depths do. */
#ifndef SS_R3D_H
#define SS_R3D_H
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

/* the camera: where it is, how far it looks down (radians), its focal length and the
 * screen point straight ahead */
typedef struct {
  float x, y, z, pitch, f, cx, cy;
  float sp, cp;   /* sine and cosine of pitch */
} Camera;
extern Camera cam;
void r_camera(float x, float y, float z, float pitch, float f, float cx, float cy);
/* camera space (x right, y up, z ahead) of a world point */
static inline V3 r_view(V3 p) {
  float dy = p.y - cam.y, dz = p.z - cam.z;
  return (V3){p.x - cam.x, dy * cam.cp + dz * cam.sp, -dy * cam.sp + dz * cam.cp};
}
/* a mesh's corner moved once for all its triangles: camera space, and where it lies on the screen from its middle
 * (when it is in front of the near plane, R_NEAR) */
#define R_NEAR 0.95f   /* (the game's camera: 5 units) */
typedef struct {
  float x, y, z, sx, sy;
} SV;
static inline SV r_corner(V3 w) {
  V3 v = r_view(w);
  SV s = {v.x, v.y, v.z, 0, 0};
  if (v.z >= R_NEAR) {
    float k = cam.f / v.z;
    s.sx = v.x * k, s.sy = -v.y * k;
  }
  return s;
}
/* the screen point of a world point; false behind the camera */
bool r_project(V3 p, float *sx, float *sy);

/* fog, as the game's (linear, by depth): from r_fog0 to r_fog1 metres ahead every pixel goes to r_fog_rgb; what
 * lies wholly past r_fog1 is not drawn (it would be the fog's colour) */
extern float r_fog0, r_fog1;
/* beyond r_far (m) a baked face with an average colour is drawn in it, plain */
extern float r_far;
extern uint32_t r_fog_rgb;

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
  P_DECAL2 = 16,    /* lies on a decal */
  P_DECAL3 = 32,    /* lies on that */
};
void r_begin(void);
void r_poly(const V3 *v, int n, uint32_t rgb, int flags, int alpha);
void r_poly_view(const V3 *v, int n, uint32_t rgb, int flags, int alpha);   /* (corners already in camera space) */
/* a textured polygon, in perspective: uv gives each corner's place in the picture (in texels, two
 * floats a corner; the picture repeats), light 0..256 */
void r_poly_tex(const V3 *v, const float *uv, int n, int tex, int light, int flags);
/* the same, drawn plain in avg (RGB565, 0: never) beyond r_far */
/* a textured triangle of a mesh, as r_poly_tex_avg, its corners from r_corner and all in front of the near plane:
 * left out at once when it is turned away; false when it reaches too far off the screen (r_poly_tex_avg then) */
bool r_tri_tex_screen(const SV *a, const SV *b, const SV *c, const float *uv, int tex, int light, int flags, uint16_t avg);
#if NP_SOFT_FLOAT
/* (calculators without a floating-point unit) a corner as r_corner makes it, in 16.16: camera space, the screen from its
 * middle and its depth (as the polygons'), which r_screen_fx fills in from x, y, z in front of the near plane;
 * r_tri_tex_screen with such corners and the places in quarter texels */
typedef struct {
  int32_t x, y, z, sx, sy;
  int64_t w;
} SVq;
void r_screen_fx(SVq *s);
bool r_tri_tex_screen_fx(const SVq *a, const SVq *b, const SVq *c, const int16_t *uv, int tex, int light, int flags,
                         uint16_t avg);
#endif
void r_poly_tex_avg(const V3 *v, const float *uv, int n, int tex, int light, int flags, uint16_t avg);
void r_poly_tex_view(const V3 *v, const float *uv, int n, int tex, int light, int flags, uint16_t avg);   /* (camera space) */

/* a box: centre, its three half-axes (x, y and z sides), a colour each for the top, sides, front and
 * back, and flags for the faces to leave out (1 top, 2 bottom, 4 -x, 8 +x, 16 front -z, 32 back +z) */
void r_box(V3 c, V3 ax, V3 ay, V3 az, const uint32_t *rgb6, int skip);
/* an axis-aligned box from (x0,y0,z0) to (x1,y1,z1), one colour lit by face */
void r_cube(float x0, float y0, float z0, float x1, float y1, float z1, uint32_t rgb, int skip);
int r_count(void);   /* polygons this frame */

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

/* the ground far below, seen where no polygon lies nearer: for row y, where the row meets it (z metres ahead, 0: it
 * does not, the sky) and its picture's texel there at the row's first pixel and the step a pixel (16.16) */
typedef struct {
  float z;
  int32_t u, v, du, dv;
} RGround;
typedef void (*r_ground_fn)(int y, RGround *g);
/* draws the frame, band by band: the polygons, then the ground (tex, as ground finds it) and the sky where none lies
 * nearer, all fogged by depth, the see-through polygons over that, and overlay draws flat things over the band (ground
 * and overlay may be NULL) */
typedef void (*r_band_fn)(void);
void r_render(const Texture *tex, r_ground_fn ground, r_band_fn overlay);
#endif
