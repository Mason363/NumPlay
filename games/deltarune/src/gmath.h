/* Deltarune: GameMaker's own maths, in floats, for the code that follows the original's closely (angles in
 * its degrees, counterclockwise, y down). */
#ifndef DR_GMATH_H
#define DR_GMATH_H
#include <math.h>

#define GM_PI 3.14159265f
static inline float gm_dsin(float deg) { return sinf(deg * (GM_PI / 180.f)); }
static inline float gm_dcos(float deg) { return cosf(deg * (GM_PI / 180.f)); }
static inline float lengthdir_x(float len, float deg) { return len * gm_dcos(deg); }
static inline float lengthdir_y(float len, float deg) { return -len * gm_dsin(deg); }
static inline float point_direction(float x0, float y0, float x1, float y1) {
  if (x0 == x1 && y0 == y1) return 0;
  float d = atan2f(y0 - y1, x1 - x0) * (180.f / GM_PI);
  return d < 0 ? d + 360.f : d;
}
static inline float point_distance(float x0, float y0, float x1, float y1) {
  return sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
}
/* GameMaker's round (to even at .5 is close enough as nearest) */
static inline int gm_round(float v) { return (int)(v < 0 ? v - 0.5f : v + 0.5f); }
/* a speed and a direction, as GameMaker keeps them with hspeed and vspeed */
typedef struct {
  float hs, vs, dir;
} Motion;
static inline float mo_speed(const Motion *m) { return sqrtf(m->hs * m->hs + m->vs * m->vs); }
static inline void mo_set(Motion *m, float sp, float dir) {
  m->dir = dir, m->hs = lengthdir_x(sp, dir), m->vs = lengthdir_y(sp, dir);
}
static inline void mo_speed_set(Motion *m, float sp) { mo_set(m, sp, m->dir); }
static inline void mo_h(Motion *m, float hs) {
  m->hs = hs;
  if (m->hs || m->vs) m->dir = point_direction(0, 0, m->hs, m->vs);
}
static inline void mo_v(Motion *m, float vs) {
  m->vs = vs;
  if (m->hs || m->vs) m->dir = point_direction(0, 0, m->hs, m->vs);
}
#endif
