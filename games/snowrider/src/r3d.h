/* Snow Rider 3D: the 3D renderer (Subway Surfers', with a camera turned any way, kept to what the game draws).
 *
 * The world is flat-coloured polygons. Each frame they are moved in front of the camera, cut at the edges of the
 * view, projected and kept in a list; then the screen is drawn in bands of BAND_H rows, each one its background
 * (the sky), the polygons that cross it with a depth buffer, the see-through ones after them, and what is drawn
 * flat over the picture (the interface). The whole screen never has to fit in RAM: a band and its depths do. */
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
/* a camera space point in front of the camera on the screen: x and y in pixels, and z its depth R_ZK / z; and back */
#define R_ZK 6500.0f
static inline V3 r_to_screen(V3 v) {
  float iz = 1 / v.z, k = cam.f * iz;
  return (V3){cam.cx + v.x * k, cam.cy - v.y * k, R_ZK * iz};
}
static inline V3 r_from_screen(V3 s) {
  float z = R_ZK / s.z, k = z / cam.f;
  return (V3){(s.x - cam.cx) * k, (cam.cy - s.y) * k, z};
}

/* polygons: their corners in order (clockwise on the screen when seen from the front: the back is not
 * drawn unless P_TWOSIDED) */
enum {
  P_TWOSIDED = 1,
  P_ALPHA = 4,      /* see-through (alpha 0..255): drawn last, no depth written */
};
void r_begin(void);
/* a polygon in camera space, its colour RGB565 (alpha 0..255 with P_ALPHA) */
void r_poly_cam(const V3 *v, int n, uint16_t c565, int flags, int alpha);
/* a triangle of a mesh, its corners on the screen (r_to_screen), clockwise from the front: left out when it is
 * turned away or off the screen; false when it reaches too far off the screen to be drawn so (r_poly_cam then) */
bool r_tri_screen(const V3 *a, const V3 *b, const V3 *c, uint16_t c565);
int r_count(void);   /* polygons this frame */
bool r_full(void);   /* the list is (nearly) full: what comes next may be left out */

__attribute__((always_inline)) static inline uint16_t r_mix565(uint16_t a, uint16_t b, int t32) {   /* t32 = 0 (a) .. 32 (b) */
  uint32_t x = (a | (uint32_t)a << 16) & 0x07E0F81F, y = (b | (uint32_t)b << 16) & 0x07E0F81F;
  x = (x + ((y - x) * (uint32_t)t32 >> 5)) & 0x07E0F81F;
  return (uint16_t)(x | x >> 16);
}

/* draws the frame: bg fills each band's colours (the sky), the polygons go over it, then overlay draws flat things
 * over the band (both may be NULL) */
typedef void (*r_band_fn)(void);
void r_render(r_band_fn bg, r_band_fn overlay);
#endif
