/* The scenery and what stands on the tracks (scene.h).
 *
 * The ground is not polygons: it is flat, so each row of the screen below the horizon is one distance ahead, and
 * across the row the ground goes by evenly. Its colours across the yard are the game's own track seen from above
 * (data.c: the ballast, the sleepers, the rails), kept in tables, sharp ones near and blurred ones far (each half as
 * wide as the one before), and a row only reads its table at a steady step: no flicker on the far rails. The
 * sleepers come one row in so many: rows that see part of one get a table with them mixed in. Like the game's track,
 * the ground hides what is below it: the city's walls and floors go on down under the tracks (the game never shows
 * that part), so each row leaves its depth where the tracks are, and the polygons are drawn only above it.
 *
 * The city, the trains and the barriers are the game's models baked into a few flat faces with their detail
 * painted on (tools/bake.py): a stretch of city (SEG) at a time, its left and right sides, or a section (the
 * elevated line, a station, a tunnel) across. */
#include "actors.h"
#include "data.h"
#include "scene.h"

#define GROUND_W 384               /* entries of the sharpest table: 12 m at 32 a metre */
#define GROUND_X0 (-6.0f)
#define LEVELS 7
#define SLEEPER (15 * U)           /* from one sleeper to the next */
#define SLEEPER_Z0 (5.6f * U)      /* where one starts in that */
#define SLEEPER_W (4.4f * U)
#define TRACKS (30 * U)            /* the three tracks reach this far to each side (the table's ballast) */

enum { G_NONE, G_SLEEPER, G_MIX, G_KINDS };
static uint16_t ground[G_KINDS][GROUND_W * 2];   /* levels one after the other */
static const int level_at[LEVELS] = {0, 384, 576, 672, 720, 744, 756};
static uint16_t sky[SCREEN_H];
/* how far the details go: on the slower calculators (N0110, N0115) they stop sooner, and the haze comes nearer */
static float detail = 1;
static int horizon;                /* the first row of ground */

/* each row of ground: its table (its level), where it starts and its step (16.16), its haze; the tracks' pixels
 * and depth */
typedef struct {
  const uint16_t *lut;
  int32_t u, du;
  int16_t x0, x1;
  uint16_t depth;
  uint8_t level, haze;
} GroundRow;
static GroundRow rows[SCREEN_H];

/* ---------------------------------------------------------------- the ground's colours */
static void ground_tables(void) {
  uint32_t acc[GROUND_W];
  for (int kind = 0; kind < G_KINDS; kind++) {
    for (int i = 0; i < GROUND_W; i++) {
      uint32_t a = ground_rgb[i], b = ground_rgb[GROUND_W + i];
      acc[i] = kind == G_NONE ? a : kind == G_SLEEPER ? b : r_mix888(a, b, 75);
    }
    for (int l = 0; l < LEVELS; l++) {
      int n = GROUND_W >> l;
      for (int i = 0; i < n; i++) ground[kind][level_at[l] + i] = r_565(acc[i]);
      /* the next level: each two averaged */
      for (int i = 0; i < n / 2; i++) {
        uint32_t a = acc[2 * i], b = acc[2 * i + 1];
        acc[i] = ((((a >> 16) & 255) + ((b >> 16) & 255)) / 2) << 16 | ((((a >> 8) & 255) + ((b >> 8) & 255)) / 2) << 8 |
                 (((a & 255) + (b & 255)) / 2);
      }
    }
  }
}

void scene_init(void) {
  ground_tables();
  r_haze0 = 60, r_haze1 = 145;
  r_haze_rgb = 0xCFEFFF;
  if (plat_slow()) {
    detail = 0.7f;
    r_haze0 *= detail, r_haze1 *= detail;
  }
  r_far = 34 * detail;
}

/* (Subway City's tunnel is a hall with a glass roof: as light inside as out) */
float scene_light(float z) { (void)z; return 1.0f; }
uint32_t scene_lit(uint32_t rgb, float z) { (void)z; return rgb; }

/* ---------------------------------------------------------------- the camera, the sky, the ground's rows */
/* the part of [a, b] on sleepers */
static float on_sleepers(float a, float b) {
  float n = 0, s = (float)(int)(a / SLEEPER) * SLEEPER + SLEEPER_Z0 - SLEEPER;
  for (; s < b; s += SLEEPER) {
    float lo = s > a ? s : a, hi = s + SLEEPER_W < b ? s + SLEEPER_W : b;
    if (hi > lo) n += hi - lo;
  }
  return n / (b - a);
}

void scene_camera(float x, float y, float z, float pitch, float f, float cy) {
  r_camera(x, y, z, pitch, f, 160, cy);
  /* the horizon: the row looking level */
  float kh = cam.sp / cam.cp;
  horizon = (int)(cam.cy - kh * cam.f) + 1;
  if (horizon < 0) horizon = 0;
  /* the game's sky: deep blue up high, pale at the horizon */
  for (int y = 0; y < SCREEN_H; y++) {
    int t = horizon > 0 ? y * 256 / (horizon + 4) : 256;
    t = t * t / 256;
    sky[y] = r_565(r_mix888(0x3E9BE0, 0xCDEEFC, t > 256 ? 256 : t));
  }
  for (int y = horizon; y < SCREEN_H; y++) {
    GroundRow *g = &rows[y];
    float k = (cam.cy - ((float)y + 0.5f)) / cam.f, den = k * cam.cp - cam.sp;
    if (den > -1e-4f) {
      g->lut = NULL;
      continue;
    }
    float zc = -cam.y / den;                     /* the distance ahead, from the camera */
    float wz = cam.z + zc * (k * cam.sp + cam.cp);
    float k2 = (cam.cy - ((float)y + 1.5f)) / cam.f, den2 = k2 * cam.cp - cam.sp;
    float wz2 = den2 < -1e-4f ? cam.z + -cam.y / den2 * (k2 * cam.sp + cam.cp) : wz + 100;
    float dz = wz2 - wz;
    if (zc > 400) {
      g->lut = NULL;
      continue;
    }
    int kind;
    if (dz < 0.1f) {
      float s = wz - (float)(int)(wz / SLEEPER) * SLEEPER;
      if (s < 0) s += SLEEPER;
      kind = s >= SLEEPER_Z0 && s < SLEEPER_Z0 + SLEEPER_W ? G_SLEEPER : G_NONE;
    } else if (dz >= SLEEPER) kind = G_MIX;
    else {
      float c = on_sleepers(wz, wz + dz);
      kind = c < 0.25f ? G_NONE : c > 0.75f ? G_SLEEPER : G_MIX;
    }
    float step = zc / cam.f * 32;   /* entries of the sharpest table a pixel */
    int l = 0;
    while (step > 1.4f && l < LEVELS - 1) step *= 0.5f, l++;
    float scale = (float)(32 >> l);
    g->lut = ground[kind] + level_at[l];
    g->level = (uint8_t)l;
    g->u = (int32_t)(((cam.x + (0.5f - cam.cx) * zc / cam.f) - GROUND_X0) * scale * 65536);
    g->du = (int32_t)(zc / cam.f * scale * 65536);
    float h = (zc - r_haze0) / (r_haze1 - r_haze0);
    g->haze = (uint8_t)(h <= 0 ? 0 : h >= 1 ? 240 : (int)(h * 240));
    /* the pixels whose middles see the tracks */
    float px = cam.f / zc, xa = cam.cx - 0.5f + (-TRACKS - cam.x) * px, xb = cam.cx - 0.5f + (TRACKS - cam.x) * px;
    g->x0 = (int16_t)(xa <= 0 ? 0 : xa >= SCREEN_W ? SCREEN_W : (int)xa + ((float)(int)xa < xa));
    g->x1 = (int16_t)(xb < 0 ? 0 : xb >= SCREEN_W - 1 ? SCREEN_W : (int)xb + 1);
    g->depth = r_floor_depth(zc);
  }
}

/* clouds: a few, far away, made of discs */
static void clouds(uint16_t *row, int y) {
  static const int8_t puff[5][3] = {{0, 0, 14}, {-16, 5, 10}, {16, 4, 11}, {-28, 9, 7}, {28, 9, 7}};
  for (int c = 0; c < 4; c++) {
    float cx = (float)((c * 97 + 30) % 340) - 10 - cam.x * 2, cy = (float)(horizon - 58 + (c * 37) % 30);
    for (int p = 0; p < 5; p++) {
      float px = cx + puff[p][0], py = cy + puff[p][1], r = puff[p][2];
      float dy = (float)y + 0.5f - py;
      if (dy * dy >= r * r) continue;
      float w = __builtin_sqrtf(r * r - dy * dy);
      int x0 = (int)(px - w), x1 = (int)(px + w);
      if (x0 < 0) x0 = 0;
      if (x1 > SCREEN_W) x1 = SCREEN_W;
      for (int x = x0; x < x1; x++) row[x] = r_mix565(row[x], 0xFFFF, 24);
    }
  }
}

void scene_background(void) {
  /* (rows with the same table and haze share the hazed copy) */
  static uint16_t hazed[GROUND_W];
  static const uint16_t *hazed_lut;
  static int hazed_t = -1;
  uint16_t haze = r_565(r_haze_rgb);
  for (int y = r_y0; y < r_y1; y++) {
    uint16_t *p = r_col + (y - r_y0) * SCREEN_W;
    if (y < horizon || !rows[y].lut) {
      uint16_t c = y < horizon ? sky[y] : haze;
      for (int x = 0; x < SCREEN_W; x++) p[x] = c;
      if (y < horizon) clouds(p, y);
      continue;
    }
    const GroundRow *g = &rows[y];
    const uint16_t *lut = g->lut;
    uint32_t n = GROUND_W >> g->level;
    if (g->haze) {
      int t = (g->haze + 4) >> 3;
      if (lut != hazed_lut || t != hazed_t) {
        for (uint32_t i = 0; i < n; i++) hazed[i] = r_mix565(lut[i], haze, t);
        hazed_lut = lut, hazed_t = t;
      }
      lut = hazed;
    }
    uint16_t left = lut[0], right = lut[n - 1];
    int32_t u = g->u, du = g->du;
    /* the pixels left of the table's first entry, those on it, those right of its last */
    int x = 0, xa = 0, xb = SCREEN_W;
    if (u < 0) xa = (int)((-(int64_t)u + du - 1) / du);
    int64_t end = ((int64_t)n << 16) - u;
    if (end <= 0) xb = 0;
    else if ((int64_t)du * SCREEN_W > end) xb = (int)((end + du - 1) / du);
    if (xa > SCREEN_W) xa = SCREEN_W;
    if (xb < xa) xb = xa;
    for (; x < xa; x++) p[x] = left;
    u += du * xa;
    /* (two pixels a word, from a pixel on a word) */
    if (x < xb && (uintptr_t)(p + x) & 2) p[x++] = lut[(uint32_t)u >> 16], u += du;
    for (; x + 1 < xb; x += 2, u += 2 * du)
      *(uint32_t *)(void *)(p + x) = lut[(uint32_t)u >> 16] | (uint32_t)lut[(uint32_t)(u + du) >> 16] << 16;
    if (x < xb) p[x++] = lut[(uint32_t)u >> 16];
    for (; x < SCREEN_W; x++) p[x] = right;
    if (g->x0 < g->x1) r_band_depth(y, g->x0, g->x1, g->depth);
  }
}

/* ---------------------------------------------------------------- the game's baked models */
static int lit = 256;      /* the light on the baked faces (full) */

/* a baked model placed at (ox, oy, oz), its first maxq faces (the most worth seeing come first) */
static void bmodel(const BModel *m, float ox, float oy, float oz, int maxq) {
  float z0 = oz + m->box[2] * 0.01f, z1 = oz + m->box[5] * 0.01f;
  if (z1 < cam.z || z0 > cam.z + 190 * detail) return;
  /* (all of it to one side out of view: nothing to draw) */
  float x0 = ox + m->box[0] * 0.01f, x1 = ox + m->box[3] * 0.01f, zn = z0 > cam.z + 0.5f ? z0 : cam.z + 0.5f;
  float reach = (z1 - cam.z) * (cam.cx + 4) / cam.f;
  if (x0 > cam.x + reach || x1 < cam.x - reach) return;
  (void)zn;
  int n = m->n < maxq ? m->n : maxq;
  for (int i = 0; i < n; i++) {
    const BFace *f = &m->f[i];
    const BCorner *c = m->c + f->first;
    V3 v[8];
    float uv[16];
    for (int k = 0; k < f->n; k++, c++) {
      v[k] = (V3){ox + c->x * 0.01f, oy + c->y * 0.01f, oz + c->z * 0.01f};
      uv[2 * k] = c->u * 0.25f, uv[2 * k + 1] = c->v * 0.25f;
    }
    r_poly_tex_avg(v, uv, f->n, m->tex, lit, 0, f->avg);
  }
}

/* how many faces at this distance */
static int faces_at(float d) {
  d /= detail;
  return d < 26 ? 999 : d < 50 ? 16 : d < 90 ? 9 : 5;
}

/* ---------------------------------------------------------------- the city */
static const BModel *const street_l[8] = {&bm_low_01_l, &bm_low_02_l, &bm_med_01_l, &bm_med_02_l, &bm_med_03_l, &bm_high_01_l,
                                          &bm_high_02_l, &bm_high_03_l};
static const BModel *const street_r[8] = {&bm_low_01_r, &bm_low_02_r, &bm_med_01_r, &bm_med_02_r, &bm_med_03_r, &bm_high_01_r,
                                          &bm_high_02_r, &bm_high_03_r};
static const BModel *const sections[3][3] = {
  {&bm_pillars_start, &bm_pillars_mid, &bm_pillars_end},
  {&bm_station_start, &bm_station_mid, &bm_station_end},
  {&bm_tube_start, &bm_tube_mid, &bm_tube_end},
};

static void stretch(int seg) {
  float z0 = (float)seg * SEG, d = z0 - cam.z;
  Stretch s = world_stretch(seg);
  int nq = faces_at(d < 0 ? 0 : d);
  if (s.kind == SC_STREET) {
    bmodel(street_l[s.part], 0, 0, z0, nq);
    bmodel(street_r[s.right], 0, 0, z0, nq);
  } else bmodel(sections[s.kind - 1][s.part], 0, 0, z0, nq * 2);
  lit = 256;
}

/* ---------------------------------------------------------------- trains, barriers, coins */
static const BModel *car_model(int look, int c, bool head, bool moving) {
  if (look == TR_CARGO) {
    if (head && moving) return &bm_car_freight;
    return c & 1 ? &bm_car_cargo2 : &bm_car_cargo1;
  }
  if (look == TR_STANDARD) return head ? &bm_car_standard : &bm_car_standard_wagon;
  return head ? &bm_car_sub : &bm_car_sub_wagon;
}

static void train(const Obstacle *o, float zfar) {
  float lx = LANE_X(o->lane), z = o->z0;
  bool moving = o->type == O_MOVING;
  for (int c = 0; c < o->cars; c++, z += CAR_LEN) {
    if (z > zfar) break;
    if (z + CAR_LEN < cam.z) continue;
    bool head = c == 0 && o->v != 1;
    bmodel(car_model(o->look, c, head, moving), lx, 0, z, faces_at(z - cam.z));
    if (head && moving) {
      /* the headlights' glow */
      float x0 = lx - TRAIN_W, x1 = lx + TRAIN_W;
      V3 g[4] = {{x0 - 0.5f, 2.2f, z - 0.2f}, {x1 + 0.5f, 2.2f, z - 0.2f}, {x1 + 0.5f, 0.2f, z - 0.2f}, {x0 - 0.5f, 0.2f, z - 0.2f}};
      r_poly(g, 4, 0xFFF6C0, P_ALPHA | P_NOHAZE, 70);
    }
  }
}

static void barrier(const Obstacle *o) {
  float lx = LANE_X(o->lane);
  const BModel *m = o->type == O_HURDLE ? &bm_barrier_jump : o->type == O_HIGHBAR ? &bm_barrier_roll : &bm_barrier_standard;
  bmodel(m, lx, 0, o->z0, 99);
}

void scene_draw(float t) {
  /* (near things first: what they hide is then left out pixel by pixel, without being drawn) */
  float zfar = cam.z + 150 * detail;
  for (int i = 0; i < nobs; i++) {
    const Obstacle *o = &obs[i];
    if (o->z0 > zfar || o->z1 < cam.z) continue;
    if (o->type == O_TRAIN || o->type == O_MOVING) train(o, zfar);
    else if (o->type == O_RAMP) bmodel(&bm_car_ramp, LANE_X(o->lane), 0, o->z0 + 6.3f * U, faces_at(o->z0 - cam.z));
    else barrier(o);
  }
  for (int i = 0; i < nitems; i++) {
    const Item *it = &items[i];
    if (it->z > cam.z + 110 * detail || it->z < cam.z + 1) continue;
    if (it->kind == I_COIN) actors_coin(it, t, scene_light(it->z));
    else actors_pickup(it, t);
  }
  int b0 = (int)(cam.z / SEG) - (cam.z < 0), b1 = (int)(zfar / SEG);
  for (int b = b0; b <= b1; b++) stretch(b);
  lit = 256;
}
