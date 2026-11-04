/* The 3D view: the faces of the blocks drawn front to back, at 160 x 120, shown at 2x.
 *
 * The columns around the eye are visited from near to far (a quadrant at a time, each axis outward from the
 * eye: in that order nothing can hide what came before it), and in each column the cells with a face to draw
 * (wexp) outward from the eye's level. A face is projected, clipped to the screen and filled a row of pixels
 * at a time with its texture, mapped linearly across it (faces near the eye are cut into smaller ones, so the
 * mapping stays true). A pixel still 0 is not drawn yet: each is drawn once, and a cell whose square on screen is
 * all drawn is passed by. What is left at the end is sky. The loops that fill are in rs.s (eZ80 assembly).
 *
 * Like Minecraft 1.8 with Fast graphics and smooth lighting off: a face is lit by the light of the cell in
 * front of it, times its side's shade (top 1.0, north and south 0.8, east and west 0.6, bottom 0.5); grass and
 * leaves take their biome's colour; leaves are solid, their holes black; plants, glass and torches let what is
 * behind show through their see-through texels; fog fades the far blocks to the sky's horizon. The palette
 * does the shading: a texel's colour goes through a row of shade_cm (32 levels of light), then a fog row. */
#include "nb.h"
#include "draw.h"
#include "rs.h"

#ifndef NB_NO_RBUF
static uint8_t rbuf_mem[RW * RH];
uint8_t *rbuf = rbuf_mem;
#endif
uint32_t stat_faces, stat_spans, stat_px, stat_cells;
#ifdef HOST
extern unsigned long st_cols, st_list, st_frus, st_cov, st_culled;
#endif
int view_dist = 8;
uint8_t render_stale = 1;   /* rbuf no longer the last picture (its memory used for something else, the world changed) */
uint32_t world_time;
int crack_stage = -1;
const uint8_t *rs_recip, *rs_ratio;

/* ---------------------------------------------------------------- the camera */
#define Q 14
#define Q1 (1L << Q)
static int eye_bx, eye_by, eye_bz;      /* the eye's block */
static int rxx, rxz;                    /* camera axes in world units, 2.14: right (no y part) */
static int uxx, uxy, uxz, fxx, fxy, fxz; /* up, forward */
static int bx0, by0, bz0;               /* the camera-space position of the eye block's low corner, Q14 */
#define F16 1371L    /* 60 / tan(35 degrees), in 1/16 pixels: Minecraft's 70 degree view */
#define NEAR (Q1 / 20)

static uint8_t lvl_sky[16], lvl_blk[16], lvl_of[256];   /* (lvl_of: a light byte's, the brighter of its two) */   /* shade level of each light level */
static const uint8_t face_off[6] = {6, 0, 2, 2, 4, 4};   /* -Y +Y -Z +Z -X +X: 0.5 1.0 0.8 0.8 0.6 0.6 */
static uint8_t fog_cm[8][256] __attribute__((aligned(256)));   /* fog rows: 1/9 to 8/9 of the way to the fog */
#define FC_N 16
static uint8_t fc_key[FC_N];            /* the shade and fog rows made into one (shade_of): level x 8 + fog - 1 */
static uint8_t fc_slot[256];            /* and where each is (255: not made) */
static uint8_t fog_rgb[3], sky_rgb[3];
static int fog_q;                       /* the fog's colour as last made into fog_cm */
static int crack_x, crack_y, crack_z;

int16_t fsin(uint16_t a) {
  /* a quarter wave, 64 steps, linear between */
  static const int16_t q[65] = {
    0, 402, 804, 1205, 1606, 2006, 2404, 2801, 3196, 3590, 3981, 4370, 4756, 5139, 5520, 5897, 6270, 6639, 7005,
    7366, 7723, 8076, 8423, 8765, 9102, 9434, 9760, 10080, 10394, 10702, 11003, 11297, 11585, 11866, 12140, 12406,
    12665, 12916, 13160, 13395, 13623, 13842, 14053, 14256, 14449, 14635, 14811, 14978, 15137, 15286, 15426, 15557,
    15679, 15791, 15893, 15986, 16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379, 16384};
  int quad = a >> 14, k = a & 16383;
  if (quad & 1) k = 16384 - k;
  int i = k >> 8, f = k & 255;
  int v = i >= 64 ? 16384 : q[i] + (((q[i + 1] - q[i]) * f) >> 8);
  return (int16_t)(quad & 2 ? -v : v);
}

/* the camera's axes from the player's yaw and pitch (Minecraft: yaw 0 faces +Z, a quarter turn faces -X) */
static void camera(fx ex, fx ey, fx ez, uint16_t yaw, int16_t pitch) {
  int sy = fsin(yaw), cy = fcos(yaw), sp = fsin((uint16_t)pitch), cp = fcos((uint16_t)pitch);
  fxx = (int)(-(int32_t)sy * cp >> Q), fxy = -sp, fxz = (int)((int32_t)cy * cp >> Q);
  rxx = -cy, rxz = -sy;
  /* up = right x forward */
  uxx = (int)(((int32_t)0 * fxz - (int32_t)rxz * fxy) >> Q);
  uxy = (int)(((int32_t)rxz * fxx - (int32_t)rxx * fxz) >> Q);
  uxz = (int)(((int32_t)rxx * fxy - (int32_t)0 * fxx) >> Q);
  eye_bx = FXI(ex), eye_by = FXI(ey), eye_bz = FXI(ez);
  int32_t dx = -(int32_t)((ex & (FX1 - 1)) << (Q - FXS)), dy = -(int32_t)((ey & (FX1 - 1)) << (Q - FXS)),
          dz = -(int32_t)((ez & (FX1 - 1)) << (Q - FXS));
  bx0 = (int)((dx * rxx + dz * rxz) >> Q);
  by0 = (int)((dx * uxx + dy * uxy + dz * uxz) >> Q);
  bz0 = (int)((dx * fxx + dy * fxy + dz * fxz) >> Q);
}

/* ---------------------------------------------------------------- the light and the sky's colours */
static int sun_k;   /* 0..256: how much the sun lights (Minecraft's sun brightness) */
static int day_k;   /* 0..256: the sky's brightness */

/* the shade level (0..31, 0.89 apart) of a brightness 0..1 (x 4096) */
static int level_of(int32_t b) {
  int k = 31;
  int32_t v = 4096;
  while (k > 0 && v * 89 / 100 >= b) v = v * 89 / 100, k--;
  /* nearest of v and v * 0.89 */
  if (k > 0 && b < (v + v * 89 / 100) / 2) k--;
  return k;
}

/* the cube's level (0..15) nearest each value 0..255 */
static uint8_t cube_of[256];
static uint8_t lerp_pal(int c, const uint8_t *to, uint8_t w) {
  /* (a colour w / 256 of the way to `to`, as the nearest of the cube) */
  const uint8_t *p = pal_rgb + c * 3;
  uint8_t k[3];
  for (int i = 0; i < 3; i++) {
    uint8_t a = p[i], b = to[i];
    k[i] = b >= a ? (uint8_t)(a + (uint8_t)((uint16_t)(uint8_t)(b - a) * w >> 8))
                  : (uint8_t)(a - (uint8_t)((uint16_t)(uint8_t)(a - b) * w >> 8));
  }
  return pal_cube[cube_of[k[0]] << 8 | cube_of[k[1]] << 4 | cube_of[k[2]]];
}

/* celestial angle (World.calculateCelestialAngle), 0..65535: 0 noon */
static uint16_t celestial(void) {
  int32_t t = (int32_t)((world_time + 18000) % 24000);   /* (Minecraft's time 0 is sunrise: +6000 is noon) */
  /* f = t / 24000 - 0.25, eased: f + ((1 - (cos(f * pi) + 1) / 2) - f) / 3 */
  int32_t f = t * 65536 / 24000;   /* 0..65535 from noon */
  int c = fcos((uint16_t)(f >> 1));   /* cos(f * pi) */
  int32_t e = (16384 - ((int32_t)c + 16384) / 2) * 4;   /* (1 - (cos+1)/2) x 65536 */
  return (uint16_t)(f + (e - f) / 3);
}

void render_sky_palette(void) {
  render_stale = 1;
  uint16_t ang = celestial();
  /* World.getSunBrightness and getCelestialLight-ish: the sky's brightness 0.2 at night to 1 by day */
  int c = fcos(ang);   /* 1 at noon, -1 at midnight */
  int32_t f = 16384 - (2 * (int32_t)c + 8192 * 2 / 5);   /* 1 - (cos * 2 + 0.2) */
  f = f < 0 ? 0 : f > 16384 ? 16384 : f;
  sun_k = (int)((16384 - f) * 256 / 16384 * 8 / 10 + 51);   /* x 0.8 + 0.2 */
  int32_t f2 = 16384 - (2 * (int32_t)c + 8192);   /* World.getSkyColor: cos * 2 + 0.5 */
  f2 = f2 < 0 ? 0 : f2 > 16384 ? 16384 : f2;
  day_k = (int)((16384 - f2) * 256 / 16384);
  /* the light table (gamma "Moody"): (1 - f) / (3f + 1), f = 1 - level / 15; the sky's scaled by the sun */
  for (int l = 0; l < 16; l++) {
    int32_t t = (int32_t)l * 4096 / (60 - 3 * l);   /* (l / 15) / ((1 - l / 15) * 3 + 1) */
    int32_t blk = t * 96 / 100 + 123;               /* x 0.96 + 0.03 */
    int32_t sky = (t * (sun_k * 95 / 100 + 13) / 256) * 96 / 100 + 123;
    lvl_blk[l] = (uint8_t)level_of(blk);
    lvl_sky[l] = (uint8_t)level_of(sky);
  }
  for (int i = 0; i < 256; i++) {
    uint8_t a = lvl_sky[i & 15], b = lvl_blk[i >> 4];
    lvl_of[i] = a > b ? a : b;
  }
  /* the sky (Minecraft's plains sky: HSB 0.6, 0.54, 1) and the fog (0.75, 0.85, 1), darker at night */
  int dk = day_k;
  sky_rgb[0] = (uint8_t)(120 * dk / 256), sky_rgb[1] = (uint8_t)(167 * dk / 256), sky_rgb[2] = (uint8_t)(255 * dk / 256);
  int fk = dk * 94 / 100 + 15;
  fog_rgb[0] = (uint8_t)(192 * fk / 256), fog_rgb[1] = (uint8_t)(216 * fk / 256);
  fog_rgb[2] = (uint8_t)(255 * (dk * 91 / 100 + 23) / 256);
  /* (the fog leans a quarter towards the sky) */
  for (int k = 0; k < 3; k++) fog_rgb[k] = (uint8_t)(fog_rgb[k] + (sky_rgb[k] - fog_rgb[k]) * 26 / 100);
  /* the sky's 15 colours: 1 the fog (the horizon) to 15 the sky above */
  uint8_t pal[15 * 3];
  for (int i = 0; i < 15; i++)
    for (int k = 0; k < 3; k++) pal[i * 3 + k] = (uint8_t)(fog_rgb[k] + (sky_rgb[k] - fog_rgb[k]) * i / 14);
  plat_palette(1, 15, pal);
  /* the fog rows, when its colour has changed enough */
  int qf = (fog_rgb[0] >> 3) << 10 | (fog_rgb[1] >> 3) << 5 | (fog_rgb[2] >> 3);
  if (qf != fog_q) {
    fog_q = qf;
    for (int f = 0; f < 8; f++) {
      fog_cm[f][0] = 0;
      uint8_t w = (uint8_t)((f + 1) * 256 / 9);
      for (int i = 1; i < 256; i++) fog_cm[f][i] = i < FIRST_STATIC ? (uint8_t)i : lerp_pal(i, fog_rgb, w);
    }
    memset(fc_key, 255, sizeof fc_key), memset(fc_slot, 255, sizeof fc_slot);
  }
}

/* ---------------------------------------------------------------- textures and shades in RAM, 256-aligned */
/* (rs_poly's loop reads a texel and its shade by setting a page's low byte: both tables 256-byte aligned) */
#define TC_N 28
static uint8_t tcache[TC_N][256] __attribute__((aligned(256)));
static uint8_t shade_ram[N_SHADE][256] __attribute__((aligned(256)));
static uint8_t fogc[FC_N][256] __attribute__((aligned(256)));
static uint8_t crack_tex[256] __attribute__((aligned(256)));
static uint8_t tc_slot[NTEX], tc_tex[TC_N], tc_used[TC_N], tc_next, frame_no;
static uint8_t fc_next;

static void caches_reset(void) {
  memset(tc_slot, 255, sizeof tc_slot);
  memset(tc_tex, 255, sizeof tc_tex);
  memset(fc_key, 255, sizeof fc_key), memset(fc_slot, 255, sizeof fc_slot);
}

/* texture t, in RAM */
/* page k of a 256-byte aligned table (on the eZ80 a shift is a call: the address's middle byte instead) */
static const uint8_t *page(const void *base, uint8_t k) {
#ifdef __ez80__
  static union { const void *p; uint8_t b[3]; } u;
  u.p = base;
  uint8_t m = (uint8_t)(u.b[1] + k);
  if (m < k) u.b[2]++;
  u.b[1] = m;
  return u.p;
#else
  return (const uint8_t *)base + k * 256;
#endif
}

static const uint8_t *tex_of(uint8_t t) {
  uint8_t s = tc_slot[t];
  if (s == 255) {
    /* a slot not used in this frame (round and round) */
    for (uint8_t n = 0; n < TC_N; n++) {
      s = tc_next, tc_next = tc_next + 1 == TC_N ? 0 : (uint8_t)(tc_next + 1);
      if (tc_used[s] != frame_no) break;
    }
    if (tc_tex[s] != 255) tc_slot[tc_tex[s]] = 255;
    tc_tex[s] = t, tc_slot[t] = s;
    memcpy((void *)page(tcache, s), tex_px[t], 256);
  }
  tc_used[s] = frame_no;
  return page(tcache, s);
}

/* the shade row of a level (0..31), through fog (1..8 eighths of the way, 0 none) */
static const uint8_t KEY8[32] = {0,   8,   16,  24,  32,  40,  48,  56,  64,  72,  80,  88,  96,  104, 112, 120,
                                  128, 136, 144, 152, 160, 168, 176, 184, 192, 200, 208, 216, 224, 232, 240, 248};
static const uint8_t *shade_of(uint8_t level, uint8_t fog) {
  if (!fog) return page(shade_ram, level);
  /* (through fog, light levels two by two: half as many rows to make) */
  level &= 0x1E;
  uint8_t key = (uint8_t)(KEY8[level] | (uint8_t)(fog - 1)), s = fc_slot[key];
  if (s != 255) return page(fogc, s);
  s = (uint8_t)(fc_next++ & (FC_N - 1));
  if (fc_slot[fc_key[s]] == s) fc_slot[fc_key[s]] = 255;
  fc_key[s] = key, fc_slot[key] = s;
  const uint8_t *a = page(shade_ram, level), *f = page(fog_cm, (uint8_t)(fog - 1));
  uint8_t *o = (uint8_t *)page(fogc, s);
  rs_compose(o, a, f);
  return o;
}

/* ---------------------------------------------------------------- faces */
static const uint8_t *s_tex, *s_cm;
static uint8_t s_alpha;

typedef struct { int x, y, z, u, v; } CV;   /* camera space, Q14; u, v the texel x 4096 */

static int lerp12(int a, int b, int t) { return a + (int)((int32_t)(b - a) * t >> 12); }

static void raster(SV *p, int n) {
  static SV buf_a[16], buf_b[16];
  int xmin = p[0].x, xmax = p[0].x, ymin = p[0].y, ymax = p[0].y;
  for (int i = 1; i < n; i++) {
    if (p[i].x < xmin) xmin = p[i].x;
    if (p[i].x > xmax) xmax = p[i].x;
    if (p[i].y < ymin) ymin = p[i].y;
    if (p[i].y > ymax) ymax = p[i].y;
  }
  if (xmax <= 0 || ymax <= 0 || xmin >= RW * 16 || ymin >= RH * 16) return;
  if (xmin < 0 || ymin < 0 || xmax > RW * 16 || ymax > RH * 16) {
    SV *a = p, *b = buf_a;
    if (xmin < 0) n = rs_clip2(a, (uint8_t)n, b, 0), a = b, b = a == buf_a ? buf_b : buf_a;
    if (xmax > RW * 16 && n) n = rs_clip2(a, (uint8_t)n, b, 1), a = b, b = a == buf_a ? buf_b : buf_a;
    if (ymin < 0 && n) n = rs_clip2(a, (uint8_t)n, b, 2), a = b, b = a == buf_a ? buf_b : buf_a;
    if (ymax > RH * 16 && n) n = rs_clip2(a, (uint8_t)n, b, 3), a = b, b = a == buf_a ? buf_b : buf_a;
    if (n < 3) return;
    p = a;
  }
  stat_spans++;
  rs_poly(p, (uint8_t)n);
}

/* clips a camera-space polygon to z >= NEAR and the guard planes |x|, |y| <= 3z */
static int clip3(CV *in, int n, CV *out, int k) {
  int m = 0;
  for (int i = 0; i < n; i++) {
    CV *a = &in[i], *b = &in[i + 1 == n ? 0 : i + 1];
    int za = a->z + a->z + a->z, zb = b->z + b->z + b->z, da, db;
    switch (k) {
      case 0: da = a->z - NEAR, db = b->z - NEAR; break;
      case 1: da = za - a->x, db = zb - b->x; break;
      case 2: da = za + a->x, db = zb + b->x; break;
      case 3: da = za - a->y, db = zb - b->y; break;
      default: da = za + a->y, db = zb + b->y;
    }
    if (da >= 0) out[m++] = *a;
    if ((da >= 0) != (db >= 0)) {
      int32_t dd = (int32_t)da - db, aa = da;
      while (dd > 262143 || dd < -262143) dd >>= 1, aa >>= 1;
      int t = (int)((aa << 12) / dd);
      CV *o = &out[m++];
      o->x = lerp12(a->x, b->x, t), o->y = lerp12(a->y, b->y, t), o->z = lerp12(a->z, b->z, t);
      o->u = lerp12(a->u, b->u, t), o->v = lerp12(a->v, b->v, t);
      if (k == 0 && o->z < NEAR) o->z = NEAR;
    }
  }
  return m;
}

/* (the general way: clipped to the near plane and the guard band, then projected; near faces, edges of the view) */
static const uint8_t BIT[8] = {1, 2, 4, 8, 16, 32, 64, 128};
static void draw_quad(CV *q, int n, uint8_t planes) {
  static CV ca[16], cb[16];
  CV *a = q, *b = ca;
  for (uint8_t k = 0; k < 5 && n >= 3; k++)
    if (planes & BIT[k]) {
      n = clip3(a, n, b, k);
      a = b, b = a == ca ? cb : ca;
    }
  if (n < 3) return;
  static SV s[16];
  int o[2];
  for (int i = 0; i < n; i++) {
    rs_proj(a[i].x, a[i].y, a[i].z, o);
    s[i].x = o[0], s[i].y = o[1], s[i].u = a[i].u, s[i].v = a[i].v;
  }
  raster(s, n);
}

/* The face being drawn: its corners in camera space (Q14), and on the screen. (Only constant indices below: on
 * the eZ80 an index into ints is a multiplication, which is a call. So are shifts, negations, masks.) */
static int fq[12];
static SV fs[4] = {{0, 0, 0, 0}, {0, 0, 65535, 0}, {0, 0, 65535, 65535}, {0, 0, 0, 65535}};

/* rs_code's two halves for a point: the planes to cut by, the view's sides it is past */
static uint8_t code_clip, code_side;
static void code(int x, int y, int z) {
  static union { int i; uint8_t b[3]; } u;
  u.i = rs_code(x, y, z);
  code_clip = u.b[0], code_side = u.b[1];
}

/* four projected corners: drawn, cut to the screen when they are not all on it */
static void poly4(SV *v) {
  if (v[0].x >= 0 && v[1].x >= 0 && v[2].x >= 0 && v[3].x >= 0 && v[0].x <= RW * 16 && v[1].x <= RW * 16 &&
      v[2].x <= RW * 16 && v[3].x <= RW * 16 && v[0].y >= 0 && v[1].y >= 0 && v[2].y >= 0 && v[3].y >= 0 &&
      v[0].y <= RH * 16 && v[1].y <= RH * 16 && v[2].y <= RH * 16 && v[3].y <= RH * 16) {
    stat_spans++;
    rs_poly(v, 4);
  } else
    raster(v, 4);
}

/* a near face's grid of (split + 1) x (split + 1) points: camera space, projected when no plane cuts them */
static CV gp[25];
static SV gq[25];
static uint8_t gclip[25], gside[25];
static const uint16_t GUV2[3] = {0, 32768, 65535}, GUV4[5] = {0, 16384, 32768, 49152, 65535};

/* the face in fq, split in split x split parts (1: whole; near faces 2 or 4, as the texture is not drawn in
 * perspective); passed by when all its corners are past one side of the view */
static void quad(uint8_t split) {
  stat_faces++;
  if (split == 1) {
    uint8_t cl, sd;
    code(fq[0], fq[1], fq[2]);
    cl = code_clip, sd = code_side;
    code(fq[3], fq[4], fq[5]);
    cl |= code_clip, sd &= code_side;
    code(fq[6], fq[7], fq[8]);
    cl |= code_clip, sd &= code_side;
    code(fq[9], fq[10], fq[11]);
    cl |= code_clip, sd &= code_side;
    if (sd) return;
    rs_setup(s_tex, s_cm, s_alpha);
    if (!cl) {
      rs_proj(fq[0], fq[1], fq[2], &fs[0].x);
      rs_proj(fq[3], fq[4], fq[5], &fs[1].x);
      rs_proj(fq[6], fq[7], fq[8], &fs[2].x);
      rs_proj(fq[9], fq[10], fq[11], &fs[3].x);
      poly4(fs);
      return;
    }
    static CV q[4] = {{0, 0, 0, 0, 0}, {0, 0, 0, 65535, 0}, {0, 0, 0, 65535, 65535}, {0, 0, 0, 0, 65535}};
    q[0].x = fq[0], q[0].y = fq[1], q[0].z = fq[2], q[1].x = fq[3], q[1].y = fq[4], q[1].z = fq[5];
    q[2].x = fq[6], q[2].y = fq[7], q[2].z = fq[8], q[3].x = fq[9], q[3].y = fq[10], q[3].z = fq[11];
    draw_quad(q, 4, cl);
    return;
  }
  rs_setup(s_tex, s_cm, s_alpha);
  /* the grid: rows from c0 + t e3 to c1 + t e3, the last from c3 to c2 (the face's edges exact: no cracks) */
  int e1x = fq[3] - fq[0], e1y = fq[4] - fq[1], e1z = fq[5] - fq[2];
  int e3x = fq[9] - fq[0], e3y = fq[10] - fq[1], e3z = fq[11] - fq[2];
  e1x >>= 1, e1y >>= 1, e1z >>= 1, e3x >>= 1, e3y >>= 1, e3z >>= 1;
  if (split == 4) e1x >>= 1, e1y >>= 1, e1z >>= 1, e3x >>= 1, e3y >>= 1, e3z >>= 1;
  const uint16_t *uvt = split == 2 ? GUV2 : GUV4;
  CV *p = gp;
  SV *q = gq;
  uint8_t *gc = gclip, *gs = gside;
  int rx = fq[0], ry = fq[1], rz = fq[2], ex = fq[3], ey = fq[4], ez = fq[5];
  for (uint8_t t = 0; t <= split; t++) {
    if (t == split) rx = fq[9], ry = fq[10], rz = fq[11], ex = fq[6], ey = fq[7], ez = fq[8];
    int x = rx, y = ry, z = rz;
    uint16_t v = uvt[t];
    for (uint8_t s = 0; s <= split; s++) {
      if (s == split) x = ex, y = ey, z = ez;
      p->x = x, p->y = y, p->z = z, p->u = uvt[s], p->v = v;
      code(x, y, z);
      *gc = code_clip, *gs = code_side;
      if (!code_clip) {
        rs_proj(x, y, z, &q->x);
        q->u = p->u, q->v = v;
      }
      p++, q++, gc++, gs++;
      x += e1x, y += e1y, z += e1z;
    }
    rx += e3x, ry += e3y, rz += e3z, ex += e3x, ey += e3y, ez += e3z;
  }
  /* its parts: corners (s, t) (s + 1, t) (s + 1, t + 1) (s, t + 1) */
  uint8_t w = (uint8_t)(split + 1);
  CV *a = gp, *b = gp + w;
  SV *qa = gq, *qb = gq + w;
  uint8_t *c0 = gclip, *c1 = gclip + w, *s0 = gside, *s1 = gside + w;
  for (uint8_t t = 0; t < split; t++) {
    for (uint8_t s = 0; s < split; s++) {
      if (!(s0[0] & s0[1] & s1[0] & s1[1])) {
        uint8_t cl = c0[0] | c0[1] | c1[0] | c1[1];
        if (!cl) {
          static SV v4[4];
          v4[0] = qa[0], v4[1] = qa[1], v4[2] = qb[1], v4[3] = qb[0];
          poly4(v4);
        } else {
          static CV c4[4];
          c4[0] = a[0], c4[1] = a[1], c4[2] = b[1], c4[3] = b[0];
          draw_quad(c4, 4, cl);
        }
      }
      a++, b++, qa++, qb++, c0++, c1++, s0++, s1++;
    }
    a++, b++, qa++, qb++, c0++, c1++, s0++, s1++;
  }
}

/* the cell's corners, camera space: corner k (bits x 1, y 2, z 4) at ck[3k..3k+2] */
#define ck rs_ck   /* (rs.s: rs_face reads them) */
static void corners(int x, int y, int z) {
  ck[0] = x, ck[1] = y, ck[2] = z;
  ck[3] = x + rxx, ck[4] = y + uxx, ck[5] = z + fxx;
  ck[6] = x, ck[7] = y + uxy, ck[8] = z + fxy;
  ck[9] = ck[3], ck[10] = ck[4] + uxy, ck[11] = ck[5] + fxy;
  ck[12] = x + rxz, ck[13] = y + uxz, ck[14] = z + fxz;
  ck[15] = ck[3] + rxz, ck[16] = ck[4] + uxz, ck[17] = ck[5] + fxz;
  ck[18] = ck[12], ck[19] = ck[13] + uxy, ck[20] = ck[14] + fxy;
  ck[21] = ck[15], ck[22] = ck[16] + uxy, ck[23] = ck[17] + fxy;
}
#define FC(i, k) (fq[3 * (i)] = ck[3 * (k)], fq[3 * (i) + 1] = ck[3 * (k) + 1], fq[3 * (i) + 2] = ck[3 * (k) + 2])
/* each face's corners (top-left, top-right, bottom-right, bottom-left, as seen from outside) into fq */
static void face_corners(int f) {
  switch (f) {
    case 0: FC(0, 4), FC(1, 5), FC(2, 1), FC(3, 0); break;   /* -Y */
    case 1: FC(0, 2), FC(1, 3), FC(2, 7), FC(3, 6); break;   /* +Y */
    case 2: FC(0, 3), FC(1, 2), FC(2, 0), FC(3, 1); break;   /* -Z */
    case 3: FC(0, 6), FC(1, 7), FC(2, 5), FC(3, 4); break;   /* +Z */
    case 4: FC(0, 2), FC(1, 6), FC(2, 4), FC(3, 0); break;   /* -X */
    case 5: FC(0, 7), FC(1, 3), FC(2, 1), FC(3, 5); break;   /* +X */
    case 6: FC(0, 2), FC(1, 7), FC(2, 5), FC(3, 0); break;   /* a plant's two crossing quads */
    default: FC(0, 6), FC(1, 3), FC(2, 1), FC(3, 4);
  }
}
/* the faces' corners, for the outline (corner numbers) */
static const uint8_t FACE_C[6][4] = {{4, 5, 1, 0}, {2, 3, 7, 6}, {3, 2, 0, 1}, {6, 7, 5, 4}, {2, 6, 4, 0}, {7, 3, 1, 5}};
static const uint8_t CROSS_C[2][4] = {{2, 7, 5, 0}, {6, 3, 1, 4}};

static uint8_t light_level(uint8_t lb, uint8_t side) {
  uint8_t l = lvl_of[lb], b = face_off[side];
  return l < b ? 0 : l - b;
}

static int crack_for = -1;
static const uint8_t *crack_of(int tex) {
  if (crack_for != tex * 16 + crack_stage) {
    crack_for = tex * 16 + crack_stage;
    const uint8_t *t = tex_px[tex], *c = tex_px[TX_DESTROY_STAGE_0 + crack_stage];
    for (int i = 0; i < 256; i++) crack_tex[i] = c[i] ? shade_ram[18][t[i]] : t[i];
  }
  return crack_tex;
}

static int tile_front(int x, int y, int z) {
  Tile *t = tile_at(x, y, z);
  return t ? t->facing : 3;
}

/* the cell being drawn: where it is, its index in the window, its column's, its slots there */
static int c_x, c_y, c_z, c_idx, c_ci;
static uint8_t c_sx, c_sy, c_sz;

/* the neighbour across face f (-Y +Y -Z +Z -X +X): its index, or -1 past the window (below it: solid, else air) */
static int nb_index(int f) {
  switch (f) {
    case 0: return c_y - 1 < win_y ? -1 : c_sy ? c_idx - 1024 : c_idx + 31 * 1024;
    case 1: return c_y + 1 >= win_y + WH ? -1 : c_sy != 31 ? c_idx + 1024 : c_idx - 31 * 1024;
    case 2: return c_z - 1 < win_z ? -1 : c_sz ? c_idx - 32 : c_idx + 31 * 32;
    case 3: return c_z + 1 >= win_z + WS ? -1 : c_sz != 31 ? c_idx + 32 : c_idx - 31 * 32;
    case 4: return c_x - 1 < win_x ? -1 : c_sx ? c_idx - 1 : c_idx + 31;
    default: return c_x + 1 >= win_x + WS ? -1 : c_sx != 31 ? c_idx + 1 : c_idx - 31;
  }
}

static uint16_t blk_tex_off[B_N];   /* b x 6 */

/* face f of the cell (its corners in ck): whole by rs_face (assembly) when far enough, else cut in parts (quad) */
static void face(uint8_t f, uint8_t split) {
  if (split > 1) {
    /* (cut only as much as its depth varies: a face seen straight on is not bent by the texture's straight lines) */
    face_corners(f);
    int z0 = fq[2], z1 = fq[2];
    if (fq[5] < z0) z0 = fq[5];
    if (fq[5] > z1) z1 = fq[5];
    if (fq[8] < z0) z0 = fq[8];
    if (fq[8] > z1) z1 = fq[8];
    if (fq[11] < z0) z0 = fq[11];
    if (fq[11] > z1) z1 = fq[11];
    int d = z1 - z0, d4 = d + d;
    d4 += d4;
    if (z0 > 0 && d4 < z0) split = 1;
    else if (split == 4 && z0 > 0 && d < z0) split = 2;
    if (split > 1) {
      quad(split);
      return;
    }
  }
  stat_faces++;
  rs_setup(s_tex, s_cm, s_alpha);
  uint8_t r = rs_face(f);

  if (r == 2) raster(rs_fs, 4);
  else if (r == 1) {
    face_corners(f);
    static CV q[4] = {{0, 0, 0, 0, 0}, {0, 0, 0, 65535, 0}, {0, 0, 0, 65535, 65535}, {0, 0, 0, 0, 65535}};
    q[0].x = fq[0], q[0].y = fq[1], q[0].z = fq[2], q[1].x = fq[3], q[1].y = fq[4], q[1].z = fq[5];
    q[2].x = fq[6], q[2].y = fq[7], q[2].z = fq[8], q[3].x = fq[9], q[3].y = fq[10], q[3].z = fq[11];
    draw_quad(q, 4, rs_fclip);
  }
}

__attribute__((noinline)) static void cell(uint8_t b, int X, int Y, int Z, int fog) {
  stat_cells++;
  corners(X, Y, Z);
  int zmin = ck[2];
  if (ck[5] < zmin) zmin = ck[5];
  if (ck[8] < zmin) zmin = ck[8];
  if (ck[11] < zmin) zmin = ck[11];
  if (ck[14] < zmin) zmin = ck[14];
  if (ck[17] < zmin) zmin = ck[17];
  if (ck[20] < zmin) zmin = ck[20];
  if (ck[23] < zmin) zmin = ck[23];
#ifndef NB_SPLIT4
#define NB_SPLIT4 4
#define NB_SPLIT2 2
#endif
  int split = zmin < (int)(Q1 / 2) ? NB_SPLIT4 : zmin < (int)(2 * Q1) ? NB_SPLIT2 : 1;
  const uint8_t *bt = &blk_tex[0][0] + blk_tex_off[b];
  uint8_t m = blk_model[b], tint = blk_tint[b];
  if (m == M_CROSS || m == M_TORCH) {
    int tex = bt[2];
    if (tint) tex += tint_of_biome[wbiome(c_ci)];
    s_tex = tex_of(tex);
    s_cm = shade_of(light_level(wlight[c_idx], 1), fog);
    s_alpha = 1;
    face(6, split);
    face(7, split);
    return;
  }
  s_alpha = (blk_flags[b] & BF_ALPHA) != 0;
  bool mined = crack_stage >= 0 && c_x == crack_x && c_y == crack_y && c_z == crack_z;
  uint8_t fl = blk_flags[b];
  for (uint8_t f = 0; f < 6; f++) {
    /* only the faces turned to the eye */
    int e = f < 2 ? eye_by - c_y : f < 4 ? eye_bz - c_z : eye_bx - c_x;
    if (f & 1 ? e <= 0 : e >= 0) continue;
    int n = nb_index(f);
    uint8_t nb, lb;
    if (n < 0) nb = f ? B_AIR : B_STONE, lb = f == 1 ? 15 : 0;
    else nb = wblk[n], lb = wlight[n];
    uint8_t nf = blk_flags[nb];
    if (nf & BF_OPAQUE) continue;
    if (nb == b) continue;
    if ((fl & BF_LIQUID) && (nf & BF_LIQUID)) continue;
    int tex = bt[f];
    if (tint & BIT[f]) tex += tint_of_biome[wbiome(c_ci)];
    if ((b == B_FURNACE || b == B_FURNACE_LIT || b == B_CHEST) && f >= 2) {
      int fr = tile_front(c_x, c_y, c_z);
      tex = f == fr ? bt[3] : bt[2];
    } else if (b == B_CRAFTING_TABLE && f >= 2)
      tex = f == 3 || f == 4 ? bt[3] : bt[2];
    s_tex = mined ? crack_of(tex) : tex_of(tex);
    s_cm = shade_of(light_level(lb, f), fog);
    face(f, split);
  }
}

/* ---------------------------------------------------------------- the sky's colour, row by row */
static uint8_t sky_row[RH];
static void sky_rows(void) {
  /* the height in the sky of the ray through a row's middle (at the screen's middle column): 0 the horizon,
   * the sky's top colour from 0.4 up; x 75/2, Q14: linear in the row */
  int32_t t0 = (int32_t)(RH / 2 * 16 - 8) * Q1 / F16, dt = 16 * Q1 / F16;
  int32_t e = (fxy + (t0 * uxy >> Q)) * 75 / 2, de = (dt * uxy >> Q) * 75 / 2;
  for (int y = 0; y < RH; y++, e -= de)
    sky_row[y] = e < Q1 ? 1 : e >= 14 * Q1 ? 15 : (uint8_t)((e >> Q) + 1);
}

/* ---------------------------------------------------------------- a frame */
/* (no multiplying, shifting or masking in the loops below: on the eZ80 those are calls; tables and adds) */
static const uint16_t SQ[34] = {0,   1,   4,   9,   16,  25,  36,  49,  64,  81,  100, 121, 144, 169, 196, 225, 256,
                                289, 324, 361, 400, 441, 484, 529, 576, 625, 676, 729, 784, 841, 900, 961, 1024, 1089};
static uint16_t zrow[32], ylev[32];   /* z slot x 32, y slot x 1024 */
/* a cell rs_column found in the view: drawn */
void rs_cell(int Y, int Z, uint8_t fog, uint8_t o, uint8_t ys) {
  c_x = rs_x, c_y = eye_by + (int)o - 32, c_z = rs_z, c_ci = rs_ci, c_idx = ylev[ys] + rs_ci;
  c_sx = rs_xs, c_sy = ys, c_sz = rs_zs;
  cell(wblk[c_idx], rs_X0, Y, Z, fog);
}

static void frame_from(fx ex, fx ey, fx ez, uint16_t yaw, int16_t pitch) {
  camera(ex, ey, ez, yaw, pitch);
  frame_no++;
  memset(rbuf, 0, RW * RH);
  stat_faces = stat_spans = stat_px = stat_cells = 0;
#ifdef HOST
  st_cols = st_list = st_frus = st_cov = st_culled = 0;
#endif
  if (!ylev[1])
    for (int k = 0; k < 32; k++) zrow[k] = (uint16_t)(k * 32), ylev[k] = (uint16_t)(k * 1024);
  /* fog: from 3/4 of the view to all of it, in 8 steps (squared distances, blocks) */
  int vd = view_dist, f0 = vd * vd * 9 / 16, fstep = (vd * vd - f0 + 8) / 9, vlim = (vd + 1) * (vd + 1);
  for (int d = 0, f = 0; d <= vlim; d++) {
    while (f < 9 && d > f0 + fstep * f) f++;
    rs_fog[d] = (uint8_t)f;
  }
  rs_vlim = vlim;
  /* the frustum: |x| <= z (45 degrees, wider than the view's 43) + a cell's half diagonal, |y| <= 0.7 z + ... */
  const int R = (int)(Q1 * 87 / 100), KX = R * 141 / 100, KY = R * 122 / 100;
  int vzx = (int)((int32_t)fxx * 7 / 10), vzy = (int)((int32_t)fxy * 7 / 10), vzz = (int)((int32_t)fxz * 7 / 10);
  int cx0 = (rxx + rxz) / 2, cy0 = (uxx + uxy + uxz) / 2, cz0 = (fxx + fxy + fxz) / 2;
  int vz0 = (int)((int32_t)cz0 * 7 / 10);
  rs_R = R, rs_KX = KX, rs_cy0 = cy0, rs_cz0 = cz0, rs_vzk = vz0 + KY;
  for (int j = 0, a = 0, b = 0, c = 0; j <= 32; j++, a += uxy, b += fxy, c += vzy) {
    rs_jY[32 + j] = a, rs_jZ[32 + j] = b, rs_jV[32 + j] = c, rs_jY[32 - j] = -a, rs_jZ[32 - j] = -b, rs_jV[32 - j] = -c;
    rs_jS[32 + j] = rs_jS[32 - j] = SQ[j];
  }
  if (!rs_jS[127]) memset(rs_jS + 65, 255, sizeof rs_jS - 65 * 2);
  /* the eye's slots; the levels up and down from it in the window */
  int ey_s = eye_by & 31;
  int up = win_y + WH - eye_by, down = eye_by - win_y;
  if (up < 0) up = 0;
  if (up > WH) up = WH;
  if (down < 0) down = 0;
  if (down > WH) down = WH;
  if (eye_by < win_y) ey_s = win_y & 31, up = WH, down = 0;   /* (under the window: from its bottom up) */
  int jbase = eye_by < win_y ? win_y - eye_by : 0;
  if (jbase > 32) jbase = 32;   /* (all past the view) */
  rs_eys = (uint8_t)ey_s, rs_up = (uint8_t)up, rs_down = (uint8_t)down, rs_jbase = (uint8_t)jbase;
  /* a column is passed by when none of its levels can be in the view's sides: the forward distance of its
   * farthest level */
  int jlo = jbase - down, jhi = jbase + up - 1;
  if (jlo < -32) jlo = -32;
  if (jhi > 32) jhi = 32;
  if (jhi < jlo) jhi = jlo;
  int czm = (rs_jZ[32 + jlo] > rs_jZ[32 + jhi] ? rs_jZ[32 + jlo] : rs_jZ[32 + jhi]) + cz0 + KX;
  rs_czm = czm, rs_cx0 = cx0, rs_vd = (uint8_t)vd;
  rs_wx0 = win_x, rs_wx1 = win_x + WS, rs_wz0 = win_z, rs_wz1 = win_z + WS;
  /* the quadrants: the one looked into first */
  int qx = fxx >= 0 ? 0 : 1, qz = fxz >= 0 ? 0 : 2;
  for (int n = 0; n < 4; n++) {
    int q = n ^ qx ^ qz;
    bool px = !(q & 1), pz = !(q & 2);
    /* the first column's low corner at the eye's level, and the steps out along x and z */
    rs_qXi = bx0 + (px ? 0 : -rxx) + (pz ? 0 : -rxz), rs_qYi = by0 + (px ? 0 : -uxx) + (pz ? 0 : -uxz);
    rs_qZi = bz0 + (px ? 0 : -fxx) + (pz ? 0 : -fxz);
    rs_qVi = (int)((int32_t)rs_qZi * 7 / 10);
    rs_qsXx = px ? rxx : -rxx, rs_qsYx = px ? uxx : -uxx, rs_qsZx = px ? fxx : -fxx, rs_qsVx = px ? vzx : -vzx;
    rs_qsXz = pz ? rxz : -rxz, rs_qsYz = pz ? uxz : -uxz, rs_qsZz = pz ? fxz : -fxz, rs_qsVz = pz ? vzz : -vzz;
    rs_qsxi = px ? 1 : -1, rs_qszi = pz ? 1 : -1, rs_qsx = (uint8_t)rs_qsxi, rs_qsz = (uint8_t)rs_qszi;
    rs_qx0 = eye_bx + (px ? 0 : -1), rs_qz0 = eye_bz + (pz ? 0 : -1);
    rs_qxs0 = (uint8_t)(rs_qx0 & 31), rs_qzs0 = (uint8_t)(rs_qz0 & 31);
    /* (|i| and |k| from the loops' counts: 0, 1, 2... or 1, 2, 3...) */
    rs_qsqi = SQ + (px ? 0 : 1), rs_qsqk = SQ + (pz ? 0 : 1);
    rs_quad();
  }
  sky_rows();
}

/* ---------------------------------------------------------------- the block looked at, the hand */
/* a line darkening what it crosses (Minecraft's outline: black at 40%), a pixel at a time */
static void dark_line(int x0, int y0, int x1, int y1) {
  int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1, sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  int err = dx - dy;
  const uint8_t *dk = shade_cm[24];
  for (int n = 0; n < 400; n++) {
    if ((unsigned)x0 < RW && (unsigned)y0 < RH) rbuf[y0 * RW + x0] = dk[rbuf[y0 * RW + x0]];
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 > -dy) err -= dy, x0 += sx;
    if (e2 < dx) err += dx, y0 += sy;
  }
}

static void outline(void) {
  if (pl.hit_face < 0) return;
  int b = world_get(pl.hit_x, pl.hit_y, pl.hit_z);
  int i = pl.hit_x - eye_bx, j = pl.hit_y - eye_by, k = pl.hit_z - eye_bz;
  corners(bx0 + i * rxx + k * rxz, by0 + i * uxx + j * uxy + k * uxz, bz0 + i * fxx + j * fxy + k * fxz);
  bool cross = blk_model[b] == M_CROSS || blk_model[b] == M_TORCH;
  /* the edges of the faces turned to the eye (a plant's box: its two crossing quads' outlines) */
  for (int f = 0; f < (cross ? 2 : 6); f++) {
    const uint8_t *fc;
    if (cross) fc = CROSS_C[f];
    else {
      int d = f >> 1, pos = f & 1;
      int e = d == 0 ? eye_by - pl.hit_y : d == 1 ? eye_bz - pl.hit_z : eye_bx - pl.hit_x;
      if (pos ? e <= 0 : e >= 0) continue;
      fc = FACE_C[f];
    }
    for (int n = 0; n < 4; n++) {
      int ka = fc[n] * 3, kc = fc[(n + 1) & 3] * 3;
      CV a = {ck[ka], ck[ka + 1], ck[ka + 2], 0, 0};
      CV c = {ck[kc], ck[kc + 1], ck[kc + 2], 0, 0};
      /* (an end behind the eye: moved to the near plane) */
      if (a.z < NEAR && c.z < NEAR) continue;
      if (a.z < NEAR || c.z < NEAR) {
        CV *p = a.z < NEAR ? &a : &c, *q = a.z < NEAR ? &c : &a;
        int t = (int)(((int32_t)(NEAR - p->z) << 12) / (q->z - p->z));
        p->x = lerp12(p->x, q->x, t), p->y = lerp12(p->y, q->y, t), p->z = NEAR;
      }
      /* (an end far off the sides: left out) */
      if (a.x > 3 * a.z || -a.x > 3 * a.z || a.y > 3 * a.z || -a.y > 3 * a.z) continue;
      if (c.x > 3 * c.z || -c.x > 3 * c.z || c.y > 3 * c.z || -c.y > 3 * c.z) continue;
      int sa[2], sc[2];
      rs_proj(a.x, a.y, a.z, sa);
      rs_proj(c.x, c.y, c.z, sc);
      dark_line(sa[0] >> 4, sa[1] >> 4, sc[0] >> 4, sc[1] >> 4);
    }
  }
}

/* the hand: what it holds as the inventory shows it (a block, an item), 3 times as big at the bottom right,
 * swinging down and in while it mines or places (EntityRenderer's arm swing, simply) */
static void hand(void) {
  const Stack *h = &pl.inv[pl.slot];
  if (!h->id) return;
  int k = h->id >= IT0 ? B_N + h->id - IT0 : h->id;
  if (icon_of[k] == 255) return;
  int sw = pl.swing;   /* 6 down to 0 */
  int ph = sw > 3 ? 6 - sw : sw;   /* 0 1 2 3 2 1 */
  int x0 = RW - 52 - ph * 5, y0 = RH - 40 + ph * 4;
  const uint8_t *t = gfx + GFX_ICON;
  const uint8_t *img = t + (t[icon_of[k] * 2] | t[icon_of[k] * 2 + 1] << 8);
  /* the icon's runs, 3 x 3 pixels each */
  for (int r = 0; r < 16; r++) {
    const uint8_t *p = img + (img[r * 2] | img[r * 2 + 1] << 8) + 32;
    int col = 0, y = y0 + r * 3;
    while (col < 16) {
      int n = *p++, len = n < 128 ? n + 1 : n - 126;
      for (int q = 0; q < len; q++, col++) {
        uint8_t c = n < 128 ? p[q] : p[0];
        int x = x0 + col * 3;
        if (!c || x < 0 || x + 3 > RW) continue;
        for (int dy = 0; dy < 3 && y + dy < RH; dy++) memset(rbuf + (y + dy) * RW + x, c, 3);
        if (n >= 128) {
          /* (a run: the rest of it at once) */
          int m = len - q - 1, w = m * 3;
          if (x + 3 + w > RW) w = RW - x - 3;
          if (w > 0)
            for (int dy = 0; dy < 3 && y + dy < RH; dy++) memset(rbuf + (y + dy) * RW + x + 3, c, (size_t)w);
          col += m;
          break;
        }
      }
      p += n < 128 ? n + 1 : 1;
    }
  }
}

bool render_frame(void) {
  fx ey = pl.y + (pl.sneaking ? FX1 * 154 / 100 : FX1 * 162 / 100);
  if (pl.hit_face >= 0) crack_x = pl.hit_x, crack_y = pl.hit_y, crack_z = pl.hit_z;
  crack_stage = pl.break_need > 0 && pl.break_ticks > 0 ? pl.break_ticks * 10 / pl.break_need : -1;
  if (crack_stage > 9) crack_stage = 9;
  /* the same view as the last picture, nothing changed since: that picture again */
  static struct { fx x, y, z; int hx, hy, hz; uint16_t yaw; int16_t pitch; int8_t hf, crack; uint8_t swing, held; } v, last;
  v.x = pl.x, v.y = ey, v.z = pl.z, v.hx = pl.hit_x, v.hy = pl.hit_y, v.hz = pl.hit_z, v.yaw = pl.yaw, v.pitch = pl.pitch;
  v.hf = (int8_t)pl.hit_face, v.crack = (int8_t)crack_stage, v.swing = (uint8_t)pl.swing, v.held = pl.inv[pl.slot].id;
  if (!render_stale && !memcmp(&v, &last, sizeof v)) return false;
  last = v, render_stale = 0;
  frame_from(pl.x, ey, pl.z, pl.yaw, pl.pitch);
  outline();
  hand();
  return true;
}

/* shade row level, the sky's colours (1..15) darkened too: a 256-byte aligned row (the screens' veil) */
const uint8_t *render_veil_row(uint8_t level) {
  uint8_t *row = crack_tex;   /* (free while no block is being mined) */
  crack_for = -1;
  memcpy(row, page(shade_ram, level), 256);
  for (int i = 1; i < FIRST_STATIC; i++) {
    uint8_t k[3];
    for (int j = 0; j < 3; j++) k[j] = (uint8_t)(fog_rgb[j] + (sky_rgb[j] - fog_rgb[j]) * (i - 1) / 14);
    row[i] = row[pal_cube[cube_of[k[0]] << 8 | cube_of[k[1]] << 4 | cube_of[k[2]]]];
  }
  return row;
}

/* a picture kept (render_title's, rbuf then the sky's rows) shown again; the sky's rows */
void render_picture(const uint8_t *p) {
  render_stale = 1;
  memcpy(rbuf, p, RW * RH);
  memcpy(sky_row, p + RW * RH, RH);
}
const uint8_t *render_sky(void) { return sky_row; }

void render_title(uint16_t yaw) {
  render_stale = 1;
  crack_stage = -1;
  frame_from(pl.x, pl.y + FX1 * 162 / 100, pl.z, yaw, 1200);
}

void render_init(void) {
  fog_q = -1;
  rs_recip = texdat + TEXDAT_RECIP, rs_ratio = texdat + TEXDAT_RATIO;
  memcpy(shade_ram, shade_cm, sizeof shade_ram);
  for (int b = 0; b < B_N; b++) blk_tex_off[b] = (uint16_t)(b * 6);
  caches_reset();
  for (int v = 0; v < 256; v++) {
    int k = 0;
    while (k < 15 && (cube_levels[k] + cube_levels[k + 1]) / 2 < v) k++;
    cube_of[v] = (uint8_t)k;
  }
}

/* the picture to the screen: the sky in what is not drawn, the HUD over it */
/* the HUD layer's rows as runs, for rs_expand: pairs (see-through, drawn), each row's adding up to HUD_W (none
 * when they do not fit: rs_expand then goes over each pixel) */
uint8_t rs_hruns[1600], rs_hruns_ok, hud_changed = 1;
static void hud_runs(void) {
  uint8_t *o = rs_hruns;
  rs_hruns_ok = 0;
  for (int r = 0; r < HUD_H; r++) {
    if (o + HUD_W + 1 > rs_hruns + sizeof rs_hruns) return;
    o = rs_runs_of(hud_layer + r * HUD_W, o, 1);   /* (in assembly) */
  }
  rs_hruns_ok = 1;
}
void render_show(bool hud) {
  if (hud && hud_changed) hud_runs(), hud_changed = 0;
  rs_expand(rbuf, scr, sky_row, hud ? hud_layer : 0);
}
