/* The fire behind chips and mult when a hand beats the blind, on the TI-84 Plus CE: the NumWorks version's flame
 * (src/flame.c: Balatro's shader, intensity and easing) with its smoke worked out ahead (tools/mkart.py: BALFIRE, a
 * few intensities, 24 moments) and its colours a table per row (BALLUT). */
#include <string.h>
#include "ce.h"
#include "flame.h"

typedef struct {
  float intensity, real, vel, timer;
} flame_t;
static flame_t fl[2];

/* log base 5 of v >= 1, from the double's bits */
static float log5(double v) {
  uint64_t b;
  memcpy(&b, &v, 8);
  int e = (int)((b >> 52) & 0x7FF) - 1023;
  uint32_t mb = 0x3F800000u | (uint32_t)((b >> 29) & 0x7FFFFF);
  float m;
  memcpy(&m, &mb, 4);
  float z = (m - 1) / (m + 1), z2 = z * z;
  float ln = 2.f * z * (1.f + z2 * (1.f / 3 + z2 * (1.f / 5 + z2 * (1.f / 7))));
  return (e * 0.69314718f + ln) * 0.62133493f;
}

void flame_update(int dt_ms, double earned, double required, int allowed, int calm) {
  float target = 0;
  if (allowed && required > 0 && earned >= required && earned > 1) {
    target = log5(earned) - 2.f;
    if (target < 0) target = 0;
  }
  for (int k = 0; k < 2; k++) {
    flame_t *f = &fl[k];
    f->intensity = target;
    int left = dt_ms;
    while (left > 0) {
      int step = left > 17 ? 17 : left;
      left -= step;
      float dt = step / 1000.f;
      float ex = 1.f - 0.4f * dt;
      f->timer += dt * (1 + f->intensity * 0.2f);
      if (f->vel < 0) f->vel *= 1 - 10 * dt;
      f->vel = (1 - ex) * (f->intensity - f->real) * dt * 25 + ex * f->vel;
      f->real += f->vel;
      if (calm) f->real -= f->real * 8 * dt + 0.02f;
      if (f->real < 0) f->real = 0;
    }
    if (f->timer > 2000) f->timer -= 2000;
  }
}

int flame_on(void) { return fl[0].real >= 0.1f || fl[1].real >= 0.1f; }

static const float levels[FL_LEVELS] = FL_I;
static int level_of(float I) {
  int best = 0;
  for (int i = 1; i < FL_LEVELS; i++)
    if ((I - levels[i]) * (I - levels[i]) < (I - levels[best]) * (I - levels[best])) best = i;
  return best;
}
static int frame_of(int k) { return ((int)(fl[k].timer / 0.11f) + (k ? 0 : 7)) % FL_FRAMES; }

int ce_flame_frame(void) {
  return frame_of(0) | frame_of(1) << 5 | level_of(fl[0].real) << 10 | level_of(fl[1].real) << 13 |
         (fl[0].real >= 0.1f) << 16 | (fl[1].real >= 0.1f) << 17;
}

/* rows ya..yb of flame k drawn at (x, y), w x h (the box under it ends at the bottom) */
void ce_flame_rows(int k, int x, int y, int w, int h, int top, uint8_t *strip, int y0, int ya, int yb, int cx0, int cx1) {
  const flame_t *f = &fl[k];
  if (f->real < 0.1f || w != FL_W || h != FL_H) return;
  int lv = level_of(f->real > 10 ? 10 : f->real);
  const uint8_t *g = ce_fire + (lv * FL_FRAMES + frame_of(k)) * (FL_GR * FL_GC / 2);
  const uint8_t *cols = ce_lut + LUT_FCOL + ((k * FL_LEVELS + lv) * FL_ROWS) * 17;
  int r0 = ya - y, r1 = yb - y;
  if (r0 < 0) r0 = 0;
  if (r1 > FL_ROWS) r1 = FL_ROWS;
  int c0 = cx0 - x, c1 = cx1 - x;
  if (c0 < 0) c0 = 0;
  if (c1 > (FL_GC - 1) * 2) c1 = (FL_GC - 1) * 2;
  for (int r = r0; r < r1; r++) {
    int j = r >> 1;
    if (j + 1 >= FL_GR) break;
    const uint8_t *ra = g + j * (FL_GC / 2), *rb = ra + FL_GC / 2, *lut = cols + r * 17;
    int wy = r & 1;
    /* the tips thin out in the last 3 pixels under the top limit */
    int fade = y + r - top < 3 ? (3 - (y + r - top)) * 3 : 0;
    uint8_t *d = strip + (y + r - y0) * SCR_W + x;
#define FV(row, i) (((row)[(i) >> 1] >> (((i) & 1) * 4)) & 15)
    for (int cx = c0; cx < c1; cx++) {
      int i = cx >> 1, a = FV(ra, i), b = FV(rb, i);
      if (cx & 1) a = (a + FV(ra, i + 1)) >> 1, b = (b + FV(rb, i + 1)) >> 1;
      int sm = (wy ? (a + b) >> 1 : a) + fade;
      if (sm < 15) d[cx] = lut[sm];
    }
#undef FV
  }
}
