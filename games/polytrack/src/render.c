/* PolyTrack's picture (render.h). A frame is set up once: the visible pieces of the track (each part is cut into 20 m
 * chunks, near ones drawn in full, far ones simplified), the mountains, the car and its shadow are turned into the
 * camera's space, cut at the near plane, lit (Three.js' hemisphere light and sun, as the game) and kept as screen
 * triangles with their depth. rd_draw then fills a strip of rows at a time: the sky and the ground, the mountains
 * behind everything, then every triangle crossing the strip with a depth buffer, then the shadow. */
#include <string.h>
#if defined(__ARM_FEATURE_SIMD32) && defined(PLATFORM_DEVICE)
#include <arm_acle.h>
#define SIMD 1   /* the DSP: two pixels' depths compared and picked at once */
#endif
#include "../../../launcher/src/gfx.h"
#include "data.h"
#include "render.h"

#define CX 160.0f
#define CY 120.0f
#define NEAR 0.5f
#define MAX_TRIS RD_MAX_TRIS
#define MAX_VIS 400
#define GUARD 1500.0f
#define STRIP_ROWS 24

enum { RK_SOLID, RK_GHOST, RK_SHADOW, RK_BACK };

typedef struct {
  int16_t x[3], y[3];               /* 1/16 pixel, top to bottom */
  uint16_t w[3];                    /* NEAR/z, 65535 = at the near plane */
  uint16_t c;
  uint8_t kind, pad;
} tri;

static tri tris[MAX_TRIS];
static int ntris;
int rd_tris, rd_dropped, rd_mtris, rd_chunks;
int rd_detail = RD_LEVELS - 1, rd_ease;
static int qcur = RD_LEVELS - 1;    /* the quality of the frame being made */
bool rd_half, rd_halfx;
/* the picture's quality, lowest first: how far it draws, how near the parts keep their whole shape, half the rows,
   triangles at most */
static const float DRAW[RD_LEVELS] = {60.0f, 70.0f, 100.0f, 100.0f, 140.0f, 180.0f, 230.0f};
static const float NEAR_DETAIL[RD_LEVELS] = {10.0f, 12.0f, 22.0f, 22.0f, 30.0f, 40.0f, 52.0f};
static const uint8_t HALF[RD_LEVELS] = {3, 1, 1, 0, 0, 0, 0};   /* 1: half the rows, 2: and of the columns */
static const uint16_t CAP[RD_LEVELS] = {900, 1100, MAX_TRIS, MAX_TRIS, MAX_TRIS, MAX_TRIS, MAX_TRIS};
static int cap = MAX_TRIS;

/* the camera */
static camera_t C;
static float focal, far_d, fog0;
static float px_n[2], py_n[2];      /* the side planes */
static int env_now;
/* the light */
static v3 sun;
static float hemi_sky[3], hemi_ground[3];
static float matlin[3][PT_NMAT][3], custom_lin[9][3], ground_lin[3][3];
static uint8_t srgb[1025];
static uint16_t back_lut[256];
static float fog_rgb[3];

const uint32_t rd_paints[8] = {0x990000, 0x003399, 0x00802B, 0x5C0099, 0x994C00, 0x007A7A, 0x262626, 0x8C8C8C};

/* ------------------------------------------------------------------ colours */
static float lin_of(int c8) {
  float c = (float)c8 / 255.0f;
  return c <= 0.04045f ? c / 12.92f : pt_powf((c + 0.055f) / 1.055f, 2.4f);
}
static void hex_lin(uint32_t h, float *o) {
  o[0] = lin_of((int)(h >> 16 & 255)), o[1] = lin_of((int)(h >> 8 & 255)), o[2] = lin_of((int)(h & 255));
}
static void color_setup(void) {
  static int done;
  if (done) return;
  done = 1;
  for (int i = 0; i <= 1024; i++) {
    float l = (float)i / 1024.0f;
    float s = l <= 0.0031308f ? l * 12.92f : 1.055f * pt_powf(l, 1.0f / 2.4f) - 0.055f;
    int v = (int)(s * 255.0f + 0.5f);
    srgb[i] = (uint8_t)(v > 255 ? 255 : v < 0 ? 0 : v);
  }
  for (int e = 0; e < 3; e++)
    for (int m = 0; m < PT_NMAT; m++)
      for (int k = 0; k < 3; k++) matlin[e][m][k] = (float)pt_envmat[(e * PT_NMAT + m) * 3 + k] / 4096.0f;
  for (int i = 0; i < 9; i++)
    for (int k = 0; k < 3; k++) custom_lin[i][k] = (float)pt_custom[i * 3 + k] / 4096.0f;
  hex_lin(0x355638, ground_lin[0]);
  hex_lin(0xA8A8A8, ground_lin[1]);
  hex_lin(0xAA7942, ground_lin[2]);
  hex_lin(0x3B618D, hemi_sky);
  hex_lin(0xB2C0C3, hemi_ground);
}
static int enc(float l) {
  int i = (int)(l * 1024.0f);
  return srgb[i < 0 ? 0 : i > 1024 ? 1024 : i];
}
static uint16_t rgb565(int r, int g, int b) { return (uint16_t)((r & 0xF8) << 8 | (g & 0xFC) << 3 | b >> 3); }

/* Three.js' light on a flat face: base/pi (4.7 hemisphere(n) + 4.7 max(0, n.sun)), then the fade to the haze far
   away (t 0..256) */
static uint16_t lit(const float *base, v3 n, int t, float dim) {
  float h = 0.5f * n.y + 0.5f, d = vdot(n, sun);
  if (d < 0.0f) d = 0.0f;
  d *= dim;
  int c[3];
  for (int k = 0; k < 3; k++) {
    float e = (hemi_ground[k] + (hemi_sky[k] - hemi_ground[k]) * h + d) * (4.7f / PT_PI);
    c[k] = enc(base[k] * e);
    if (t) c[k] += (enc(fog_rgb[k]) - c[k]) * t / 256;
  }
  return rgb565(c[0], c[1], c[2]);
}

/* ------------------------------------------------------------------ cameras */
static void look_at(camera_t *cam, v3 eye, v3 target) {
  /* Three.js' lookAt with the world's up */
  v3 z = vsub(eye, target);
  if (vlen2(z) == 0.0f) z = V3(0.0f, 0.0f, 1.0f);
  z = vnorm(z);
  v3 up = V3(0.0f, 1.0f, 0.0f);
  v3 x = vcross(up, z);
  if (vlen2(x) == 0.0f) {
    z.z += pt_absf(up.z) == 1.0f ? 0.0001f : 0.0f;
    z.x += pt_absf(up.z) == 1.0f ? 0.0f : 0.0001f;
    z = vnorm(z);
    x = vcross(up, z);
  }
  x = vnorm(x);
  cam->pos = eye;
  cam->right = x;
  cam->up = vcross(z, x);
  cam->fwd = vneg(z);
}

void chase_reset(chase_t *ch, v3 pos, qt rot) {
  ch->anchor = vadd(qrot(rot, V3(1e-5f, 0.0f, -1.0f)), pos);
  ch->up = qrot(rot, V3(0.0f, 1.0f, 0.0f));
}

void chase_update(chase_t *ch, camera_t *cam, float dt, v3 pos, qt rot, float kmh) {
  v3 r = qrot(rot, V3(0.0f, 1.0f, 0.0f));
  float a = pt_minf(1.0f, 5.0f * dt);
  ch->up = vadd(vmul(r, a), vmul(ch->up, 1.0f - a));
  v3 s = vnorm(vsub(pos, ch->anchor));
  v3 eye = vadd(vsub(pos, vmul(s, 5.5f)), vmul(ch->up, 2.0f));
  if (eye.y < 0.25f) eye.y = 0.25f;
  look_at(cam, eye, vadd(pos, vmul(ch->up, 1.8f)));
  ch->anchor = vsub(pos, vmul(s, 5.5f));
  cam->fov = 70.0f + 30.0f * (1.0f - pt_expf(-pt_absf(kmh) / 200.0f));
}

void cockpit_camera(camera_t *cam, v3 pos, qt rot, float kmh) {
  m3 m;
  qmat(rot, &m);
  cam->pos = vadd(pos, mrow(&m, V3(0.0f, 1.1f, 0.4f)));
  /* the car's turn, then half a turn round y: the camera looks along the car's forward */
  cam->right = vneg(mcolumn(&m, 0));
  cam->up = mcolumn(&m, 1);
  cam->fwd = mcolumn(&m, 2);
  cam->fov = 70.0f + 10.0f * (1.0f - pt_expf(-pt_absf(kmh) / 200.0f));
}

/* ------------------------------------------------------------------ triangles */
typedef struct { float x, y, w; } sv;
static inline int iceil(float f) { return (int)__builtin_ceilf(f); }   /* (one instruction on the calculator) */

static void store(const sv *a, const sv *b, const sv *c, uint16_t col, int kind) {
  if (ntris >= cap) {
    rd_dropped = 1;
    return;
  }
  const sv *p[3] = {a, b, c};
  for (int i = 0; i < 2; i++)
    for (int j = 0; j < 2 - i; j++)
      if (p[j]->y > p[j + 1]->y) {
        const sv *t = p[j];
        p[j] = p[j + 1], p[j + 1] = t;
      }
  if (iceil(p[2]->y - 0.5f) <= iceil(p[0]->y - 0.5f)) return;
  if (p[2]->y < 0.0f || p[0]->y > 240.0f) return;
  float xmin = pt_minf(p[0]->x, pt_minf(p[1]->x, p[2]->x)), xmax = pt_maxf(p[0]->x, pt_maxf(p[1]->x, p[2]->x));
  if (xmax < 0.0f || xmin > 320.0f) return;
  tri *t = &tris[ntris++];
  for (int i = 0; i < 3; i++) {
    t->x[i] = (int16_t)(p[i]->x * 16.0f);
    t->y[i] = (int16_t)(p[i]->y * 16.0f);
    float w = pt_clampf(p[i]->w, 0.0f, 1.0f);
    t->w[i] = (uint16_t)(w * 65535.0f);
  }
  t->c = col, t->kind = (uint8_t)kind;
}

static void screen_poly(sv *v, int n, uint16_t col, int kind) {
  bool out = false;
  for (int i = 0; i < n; i++)
    if (v[i].x < -GUARD || v[i].x > 320.0f + GUARD || v[i].y < -GUARD || v[i].y > 240.0f + GUARD) out = true;
  if (out) {
    static const float LIM[4] = {-GUARD, 320.0f + GUARD, -GUARD, 240.0f + GUARD};
    sv buf[2][10];
    int m = n, cur = 0;
    memcpy(buf[0], v, sizeof(sv) * (size_t)n);
    for (int e = 0; e < 4 && m; e++) {
      sv *in = buf[cur], *o = buf[cur ^ 1];
      int k = 0;
      for (int i = 0; i < m; i++) {
        sv *A = &in[i], *B = &in[(i + 1) % m];
        float a = e < 2 ? A->x : A->y, b = e < 2 ? B->x : B->y;
        bool ain = (e & 1) ? a <= LIM[e] : a >= LIM[e], bin = (e & 1) ? b <= LIM[e] : b >= LIM[e];
        if (ain && k < 10) o[k++] = *A;
        if (ain != bin && k < 10) {
          float t = (LIM[e] - a) / (b - a);
          sv q = {A->x + (B->x - A->x) * t, A->y + (B->y - A->y) * t, A->w + (B->w - A->w) * t};
          o[k++] = q;
        }
      }
      m = k, cur ^= 1;
    }
    for (int i = 1; i + 1 < m; i++) store(&buf[cur][0], &buf[cur][i], &buf[cur][i + 1], col, kind);
    return;
  }
  for (int i = 1; i + 1 < n; i++) store(&v[0], &v[i], &v[i + 1], col, kind);
}

/* a triangle in the camera's space, cut at the near plane, projected */
static void cam_tri(v3 a, v3 b, v3 c, uint16_t col, int kind) {
  v3 p[3] = {a, b, c}, q[4];
  int m = 0;
  for (int i = 0; i < 3; i++) {
    const v3 *A = &p[i], *B = &p[(i + 1) % 3];
    bool ain = A->z >= NEAR, bin = B->z >= NEAR;
    if (ain) q[m++] = *A;
    if (ain != bin) {
      float t = (NEAR - A->z) / (B->z - A->z);
      q[m++] = V3(A->x + (B->x - A->x) * t, A->y + (B->y - A->y) * t, NEAR);
    }
  }
  if (m < 3) return;
  sv s[4];
  for (int i = 0; i < m; i++) {
    float inv = 1.0f / q[i].z;
    s[i].x = CX + q[i].x * focal * inv;
    s[i].y = CY - q[i].y * focal * inv;
    s[i].w = NEAR * inv;
  }
  screen_poly(s, m, col, kind);
}

static v3 to_cam(v3 w) {
  v3 d = vsub(w, C.pos);
  return V3(vdot(d, C.right), vdot(d, C.up), vdot(d, C.fwd));
}

static bool sphere_visible(v3 c, float r) {
  if (c.z + r < NEAR || c.z - r > far_d) return false;
  if (c.x * px_n[0] - c.z * px_n[1] > r) return false;
  if (-c.x * px_n[0] - c.z * px_n[1] > r) return false;
  if (c.y * py_n[0] - c.z * py_n[1] > r) return false;
  if (-c.y * py_n[0] - c.z * py_n[1] > r) return false;
  return true;
}

/* ------------------------------------------------------------------ the frame */
void rd_begin(const camera_t *cam, int env, int sun_byte, bool sky_only) {
  color_setup();
  C = *cam;
  env_now = env;
  ntris = 0, rd_dropped = 0;
  float half = C.fov * 0.5f * (PT_PI / 180.0f);
  focal = CY * pt_cosf(half) / pt_sinf(half);
  int q = rd_detail - rd_ease;
  q = q < 0 ? 0 : q >= RD_LEVELS ? RD_LEVELS - 1 : q;
  qcur = q;
  far_d = DRAW[q];
  rd_half = HALF[q] & 1, rd_halfx = HALF[q] >> 1;
  cap = CAP[q];
  fog0 = far_d * 0.55f;
  float l = pt_sqrtf(focal * focal + CX * CX);
  px_n[0] = focal / l, px_n[1] = CX / l;
  l = pt_sqrtf(focal * focal + CY * CY);
  py_n[0] = focal / l, py_n[1] = CY / l;
  float a = 2.0f * (float)sun_byte * (PT_PI / 180.0f);
  sun = vnorm(V3(pt_cosf(a), 0.78f, pt_sinf(a)));
  /* the background: the sky dome (horizon white, zenith (5, 140, 255)) above, the lit ground below, fading into
     the haze (#9bd1ff) with distance */
  float g[3];
  for (int k = 0; k < 3; k++) {
    float e = (hemi_sky[k] + sun.y) * (4.7f / PT_PI);
    g[k] = ground_lin[env][k] * e;
  }
  hex_lin(0x9BD1FF, fog_rgb);
  /* (the far track fades to the ground's colour seen far off) */
  for (int k = 0; k < 3; k++) fog_rgb[k] = g[k] + (fog_rgb[k] - g[k]) * 0.18f;
  for (int i = 0; i < 256; i++) {
    float dy = ((float)i - 127.5f) / 127.5f;
    int r, gg, b;
    if (dy >= 0.0f) {
      float h = pt_powf(pt_clampf(dy, 0.01f, 1.0f), 0.2f);
      r = (int)((5.0f * h + 255.0f * (1.0f - h)) + 0.5f);
      gg = (int)((140.0f * h + 255.0f * (1.0f - h)) + 0.5f);
      b = 255;
    } else {
      float dist = pt_maxf(C.pos.y, 0.25f) / -dy;
      float t = pt_clampf(dist / 10000.0f, 0.0f, 1.0f);
      float hz[3];
      hex_lin(0x9BD1FF, hz);
      r = enc(g[0] + (hz[0] - g[0]) * t), gg = enc(g[1] + (hz[1] - g[1]) * t), b = enc(g[2] + (hz[2] - g[2]) * t);
    }
    back_lut[i] = rgb565(r, gg, b);
  }
  (void)sky_only;
}

/* ------------------------------------------------------------------ the track */
typedef struct {
  uint16_t inst;
  uint8_t chunk, lod;
  float dist;
} vis_t;
static vis_t vis[MAX_VIS];
static int nvis;

static void collect(int i, void *ctx) {
  (void)ctx;
  const pt_inst *in = world_inst(i);
  const pt_part *p = &pt_parts[in->part];
  const int8_t *o = world_orient(in);
  v3 o0 = world_origin(in);
  for (int k = 0; k < p->nchunk; k++) {
    const pt_chunk *ch = &pt_chunks[p->chunk + k];
    v3 lo = V3((float)ch->box[0] / PT_FIX, (float)ch->box[1] / PT_FIX, (float)ch->box[2] / PT_FIX);
    v3 hi = V3((float)ch->box[3] / PT_FIX, (float)ch->box[4] / PT_FIX, (float)ch->box[5] / PT_FIX);
    v3 lc = vmul(vadd(lo, hi), 0.5f);
    float r = vlen(vsub(hi, lc));
    v3 wc = vadd(V3((float)o[0] * lc.x + (float)o[1] * lc.y + (float)o[2] * lc.z,
                    (float)o[3] * lc.x + (float)o[4] * lc.y + (float)o[5] * lc.z,
                    (float)o[6] * lc.x + (float)o[7] * lc.y + (float)o[8] * lc.z), o0);
    v3 cc = to_cam(wc);
    if (!sphere_visible(cc, r)) continue;
    float d = vlen(vsub(wc, C.pos)) - r;
    vis_t *v;
    if (nvis < MAX_VIS) v = &vis[nvis++];
    else {
      /* (full: the nearest are kept) */
      int far = 0;
      for (int j = 1; j < MAX_VIS; j++)
        if (vis[j].dist > vis[far].dist) far = j;
      if (vis[far].dist <= d) continue;
      v = &vis[far];
    }
    v->inst = (uint16_t)i, v->chunk = (uint8_t)k, v->dist = d;
    v->lod = d < NEAR_DETAIL[qcur] ? 0 : 1;
  }
}

static v3 cv[256];
static sv cs[256];                  /* the vertices on the screen (w < 0: before the near plane) */
static void draw_chunk(const vis_t *v) {
  const pt_inst *in = world_inst(v->inst);
  const pt_part *p = &pt_parts[in->part];
  const pt_chunk *ch = &pt_chunks[p->chunk + v->chunk];
  const pt_lod *lod = &ch->lod[v->lod];
  if (!lod->ntri) lod = &ch->lod[v->lod ^ 1];
  if (!lod->ntri) return;
  const int8_t *o = world_orient(in);
  /* part space -> camera space */
  float M[3][3];
  v3 ax[3] = {C.right, C.up, C.fwd};
  for (int i = 0; i < 3; i++) {
    float a[3] = {ax[i].x, ax[i].y, ax[i].z};
    for (int j = 0; j < 3; j++) M[i][j] = (a[0] * (float)o[j] + a[1] * (float)o[3 + j] + a[2] * (float)o[6 + j]);
  }
  v3 t = to_cam(world_origin(in));
  const float s = 1.0f / PT_FIX;
  const int16_t *pool = &pt_pool[p->vbase * 3];
  for (int k = 0; k < lod->nv; k++) {
    const int16_t *q = &pool[pt_cvidx[lod->vfirst + k] * 3];
    float x = (float)q[0] * s, y = (float)q[1] * s, z = (float)q[2] * s;
    cv[k] = V3(M[0][0] * x + M[0][1] * y + M[0][2] * z + t.x, M[1][0] * x + M[1][1] * y + M[1][2] * z + t.y,
               M[2][0] * x + M[2][1] * y + M[2][2] * z + t.z);
    /* (projected once here, for all the triangles that share it) */
    if (cv[k].z >= NEAR) {
      float inv = 1.0f / cv[k].z;
      cs[k].x = CX + cv[k].x * focal * inv, cs[k].y = CY - cv[k].y * focal * inv, cs[k].w = NEAR * inv;
    } else {
      cs[k].w = -1.0f;
    }
  }
  /* the colours: the track's world, or the one the part asks for */
  int env = env_now;
  int col = in->color;
  if (col >= 1 && col <= 3) env = col - 1;
  const float *custom = (p->custom && col >= 32 && col <= 40) ? custom_lin[col - 32] : NULL;
  uint32_t mask = p->simple ? pt_masks[W.t->mask + in->mask] : 0xFFFFFFFFu;
  int fade = v->dist > fog0 ? (int)((v->dist - fog0) * 256.0f / (far_d - fog0)) : 0;
  if (fade > 240) fade = 240;
  /* (a face's light depends here only on its material and its normal: kept for the triangles that share them) */
  uint32_t lkey[8];
  uint16_t lcol[8];
  for (int i = 0; i < 8; i++) lkey[i] = 0xFFFFFFFFu;   /* (no material is 255) */
  const uint8_t *tr = &pt_rtri[lod->tfirst * 7];
  for (int k = 0; k < lod->ntri; k++, tr += 7) {
    if (k < 32 && !(mask & (1u << k))) continue;
    v3 a = cv[tr[0]], b = cv[tr[1]], c = cv[tr[2]];
    float nx = (float)(int8_t)tr[4], ny = (float)(int8_t)tr[5], nz = (float)(int8_t)tr[6];
    /* back faces */
    float cnx = M[0][0] * nx + M[0][1] * ny + M[0][2] * nz, cny = M[1][0] * nx + M[1][1] * ny + M[1][2] * nz,
          cnz = M[2][0] * nx + M[2][1] * ny + M[2][2] * nz;
    if (cnx * a.x + cny * a.y + cnz * a.z >= 0.0f) continue;
    const sv *sa = &cs[tr[0]], *sb = &cs[tr[1]], *sc = &cs[tr[2]];
    bool past = sa->w > 0.0f && sb->w > 0.0f && sc->w > 0.0f;
    float x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    if (past) {
      /* (all past the near plane: off the screen, or as it is, or cut to the guard band) */
      x0 = pt_minf(sa->x, pt_minf(sb->x, sc->x)), x1 = pt_maxf(sa->x, pt_maxf(sb->x, sc->x));
      y0 = pt_minf(sa->y, pt_minf(sb->y, sc->y)), y1 = pt_maxf(sa->y, pt_maxf(sb->y, sc->y));
      if (x1 < 0.0f || x0 > 320.0f || y1 < 0.0f || y0 > 240.0f) continue;
    }
    uint32_t key = (uint32_t)tr[3] | (uint32_t)tr[4] << 8 | (uint32_t)tr[5] << 16 | (uint32_t)tr[6] << 24;
    int h = (int)((key * 2654435761u) >> 29);
    if (lkey[h] != key) {
      v3 wn = vmul(V3((float)o[0] * nx + (float)o[1] * ny + (float)o[2] * nz, (float)o[3] * nx + (float)o[4] * ny + (float)o[5] * nz,
                      (float)o[6] * nx + (float)o[7] * ny + (float)o[8] * nz), 1.0f / 127.0f);
      int m = tr[3];
      const float *base = (custom && m == PT_M_BLOCK_SURFACE) ? custom : matlin[env][m];
      lkey[h] = key, lcol[h] = lit(base, wn, fade, 1.0f);
    }
    uint16_t col = lcol[h];
    if (!past) cam_tri(a, b, c, col, RK_SOLID);
    else if (x0 >= -GUARD && x1 <= 320.0f + GUARD && y0 >= -GUARD && y1 <= 240.0f + GUARD) store(sa, sb, sc, col, RK_SOLID);
    else {
      sv q[3] = {*sa, *sb, *sc};
      screen_poly(q, 3, col, RK_SOLID);
    }
  }
}

static void mountains(void) {
  /* the ring, far to near: the outer rings first, then in */
  int s = W.mseg;
  if (!s) return;
  float rad[8];
  for (int k = 0; k < 8; k++) rad[k] = W.mr + 100.0f * (float)k;
  /* (at the lowest qualities: every other segment, the nearer rings) */
  int de = qcur <= 2 ? 2 : 1, k0 = qcur <= 2 ? 3 : 6;
  for (int k = k0; k >= 0; k--)
    for (int e = 0; e < s; e += de) {
      int e1 = (e + de) % s;
      float a0 = (float)e / (float)s * 2.0f * PT_PI, a1 = (float)(e + de) / (float)s * 2.0f * PT_PI;
      float c0 = pt_cosf(a0), s0 = pt_sinf(a0), c1 = pt_cosf(a1), s1 = pt_sinf(a1);
      float h00 = (float)W.mh[e][k] * (100.0f / 255.0f), h10 = (float)W.mh[e1][k] * (100.0f / 255.0f);
      float h01 = (float)W.mh[e][k + 1] * (100.0f / 255.0f), h11 = (float)W.mh[e1][k + 1] * (100.0f / 255.0f);
      if (h00 + h10 + h01 + h11 == 0.0f) continue;
      v3 A = V3(W.mx + c0 * rad[k], h00, W.mz + s0 * rad[k]), B = V3(W.mx + c1 * rad[k], h10, W.mz + s1 * rad[k]);
      v3 Cc = V3(W.mx + c1 * rad[k + 1], h11, W.mz + s1 * rad[k + 1]), D = V3(W.mx + c0 * rad[k + 1], h01, W.mz + s0 * rad[k + 1]);
      v3 ca = to_cam(A), cb = to_cam(B), cc = to_cam(Cc), cd = to_cam(D);
      if (ca.z < NEAR && cb.z < NEAR && cc.z < NEAR && cd.z < NEAR) continue;
      v3 n1 = vnorm(vcross(vsub(B, A), vsub(Cc, A))), n2 = vnorm(vcross(vsub(Cc, A), vsub(D, A)));
      float fade = 0.0f;
      int t = (int)(fade);
      cam_tri(ca, cb, cc, lit(ground_lin[env_now], n1, t, 1.0f), RK_BACK);
      cam_tri(ca, cc, cd, lit(ground_lin[env_now], n2, t, 1.0f), RK_BACK);
    }
}

void rd_track(void) {
  mountains();
  rd_mtris = ntris;
  nvis = 0;
  v3 r = V3(far_d, far_d, far_d);
  world_query(vsub(C.pos, r), vadd(C.pos, r), collect, NULL);
  /* near first, so that when there is no more room it is the far ones that go */
  for (int i = 1; i < nvis; i++) {
    vis_t t = vis[i];
    int j = i - 1;
    while (j >= 0 && vis[j].dist > t.dist) vis[j + 1] = vis[j], j--;
    vis[j + 1] = t;
  }
  /* (when the room runs short, the further ones simplified) */
  for (int i = 0; i < nvis; i++) {
    if (ntris > cap * 3 / 5) vis[i].lod = 1;
    draw_chunk(&vis[i]);
  }
  rd_chunks = nvis;
}

/* ------------------------------------------------------------------ the car */
car_look car_look_of(const car_t *c) {
  car_look l;
  memset(&l, 0, sizeof l);
  l.pos = c->pos, l.rot = c->rot;
  for (int i = 0; i < 4; i++) l.susp[i] = c->w[i].susp, l.spin[i] = c->w[i].rotation;
  l.steer = c->w[0].steering;
  l.brake = c->brake_light;
  return l;
}

car_look car_look_mix(const car_look *a, const car_look *b, float t) {
  car_look l = *b;
  l.pos = vadd(a->pos, vmul(vsub(b->pos, a->pos), t));
  qt q0 = a->rot, q1 = b->rot;
  if (q0.x * q1.x + q0.y * q1.y + q0.z * q1.z + q0.w * q1.w < 0.0f) q1.x = -q1.x, q1.y = -q1.y, q1.z = -q1.z, q1.w = -q1.w;
  qt q = {q0.x + (q1.x - q0.x) * t, q0.y + (q1.y - q0.y) * t, q0.z + (q1.z - q0.z) * t, q0.w + (q1.w - q0.w) * t};
  l.rot = qnorm(q);
  for (int i = 0; i < 4; i++) l.susp[i] = a->susp[i] + (b->susp[i] - a->susp[i]) * t;
  return l;
}

static void car_mesh(const int16_t *vtx, int nv, const uint8_t *tr, int nt, const float M[3][3], v3 t, const m3 *wrot,
                     const float mat[7][3], const car_look *c) {
  const float s = 1.0f / PT_FIX;
  for (int k = 0; k < nv; k++) {
    float x = (float)vtx[k * 3] * s, y = (float)vtx[k * 3 + 1] * s, z = (float)vtx[k * 3 + 2] * s;
    cv[k] = V3(M[0][0] * x + M[0][1] * y + M[0][2] * z + t.x, M[1][0] * x + M[1][1] * y + M[1][2] * z + t.y,
               M[2][0] * x + M[2][1] * y + M[2][2] * z + t.z);
    /* (projected once here, for all the triangles that share it) */
    if (cv[k].z >= NEAR) {
      float inv = 1.0f / cv[k].z;
      cs[k].x = CX + cv[k].x * focal * inv, cs[k].y = CY - cv[k].y * focal * inv, cs[k].w = NEAR * inv;
    } else {
      cs[k].w = -1.0f;
    }
  }
  for (int k = 0; k < nt; k++, tr += 4) {
    v3 a = cv[tr[0]], b = cv[tr[1]], cc = cv[tr[2]];
    v3 n = vcross(vsub(b, a), vsub(cc, a));
    if (vdot(n, a) >= 0.0f) continue;
    /* the world's normal for the light */
    v3 cn = vnorm(n);
    v3 wn = V3(cn.x * C.right.x + cn.y * C.up.x + cn.z * C.fwd.x, cn.x * C.right.y + cn.y * C.up.y + cn.z * C.fwd.y,
               cn.x * C.right.z + cn.y * C.up.z + cn.z * C.fwd.z);
    int m = tr[3];
    uint16_t col;
    if (m == PT_CM_BRAKELIGHT && c->brake) col = rgb565(255, 102, 77);
    else col = lit(mat[m], wn, 0, 1.0f);
    cam_tri(a, b, cc, col, c->ghost ? RK_GHOST : RK_SOLID);
  }
  (void)wrot;
}

static void mat_mul(float M[3][3], const m3 *rot) {
  v3 ax[3] = {C.right, C.up, C.fwd};
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) {
      v3 col = mcolumn(rot, j);
      M[i][j] = vdot(ax[i], col);
    }
}

void rd_car(const car_look *c) {
  float mat[7][3];
  hex_lin(rd_paints[c->paint & 7], mat[PT_CM_MAIN]);
  hex_lin(0xFFFFFF, mat[PT_CM_STRIPE]);
  for (int i = 2; i < 7; i++)
    for (int k = 0; k < 3; k++) mat[i][k] = (float)pt_carmat[(i - 2) * 3 + k] / 4096.0f;
  hex_lin(0x131313, mat[PT_CM_METAL]);
  hex_lin(0x666666, mat[PT_CM_RIM]);
  if (c->ghost) {
    for (int i = 0; i < 7; i++)
      for (int k = 0; k < 3; k++) mat[i][k] = mat[i][k] * 0.4f + 0.35f;
  }
  m3 R;
  qmat(c->rot, &R);
  float M[3][3];
  mat_mul(M, &R);
  /* the body and the suspension: the model drawn at the body's transform, moved up by the mass offset */
  v3 t = to_cam(vadd(c->pos, mrow(&R, V3(0.0f, CAR_MASS_OFFSET, 0.0f))));
  if (t.z < -6.0f || t.z > far_d) return;
  car_mesh(pt_body_v, PT_BODY_NV, pt_body_t, PT_BODY_NT, M, t, &R, mat, c);
  if (!c->ghost && qcur > 2) car_mesh(pt_extra_v, PT_EXTRA_NV, pt_extra_t, PT_EXTRA_NT, M, t, &R, mat, c);
  /* the wheels: at their suspension's end, turned by the steering (front) and rolling */
  for (int i = 0; i < 4; i++) {
    v3 wp = vadd(c->pos, mrow(&R, vadd(car_wheel_pos[i], V3(0.0f, -c->susp[i], 0.0f))));
    float st = i < 2 ? c->steer : 0.0f;
    qt qs = {0.0f, pt_sinf(st * 0.5f), 0.0f, pt_cosf(st * 0.5f)};
    qt qr = {pt_sinf(c->spin[i] * 0.5f), 0.0f, 0.0f, pt_cosf(c->spin[i] * 0.5f)};
    m3 WR;
    qmat(qmul(qmul(c->rot, qs), qr), &WR);
    float WM[3][3];
    mat_mul(WM, &WR);
    car_mesh(pt_wheel_v, PT_WHEEL_NV, pt_wheel_t, PT_WHEEL_NT, WM, to_cam(wp), &WR, mat, c);
  }
}

/* the car's shadow: its outline cast along the sun onto the ground under its wheels */
void rd_shadow(const car_look *c, const car_t *phys) {
  v3 n = V3(0.0f, 0.0f, 0.0f), p = n;
  int k = 0;
  for (int i = 0; i < 4; i++)
    if (phys->w[i].contact) n = vadd(n, phys->w[i].normal), p = vadd(p, phys->w[i].point), k++;
  if (!k) {
    if (!phys->ground_on) return;
    n = V3(0.0f, 1.0f, 0.0f);
    p = V3(c->pos.x, 0.0f, c->pos.z);
  } else {
    n = vnorm(n), p = vmul(p, 1.0f / (float)k);
  }
  m3 R;
  qmat(c->rot, &R);
  static const float FOOT[6][2] = {{0.75f, 1.6f}, {0.55f, 1.9f}, {-0.55f, 1.9f}, {-0.75f, 1.6f}, {-0.75f, -1.9f}, {0.75f, -1.9f}};
  v3 q[6];
  float dn = vdot(sun, n);
  if (dn < 0.2f) return;
  for (int i = 0; i < 6; i++) {
    v3 w = vadd(c->pos, mrow(&R, V3(FOOT[i][0], CAR_MASS_OFFSET - 0.2f, FOOT[i][1])));
    /* along the sun onto the plane through p */
    float d = vdot(vsub(w, p), n);
    if (d < -0.5f) return;
    v3 s = vsub(w, vmul(sun, d / dn));
    q[i] = to_cam(vadd(s, vmul(n, 0.02f)));
  }
  for (int i = 1; i + 1 < 6; i++) cam_tri(q[0], q[i], q[i + 1], 0, RK_SHADOW);
}

/* the picture's quality follows the time a frame's work takes (the physics and the picture): down at once when
   slow (or out of room for triangles), up slowly. rd_ease_for: a frame whose physics took long is drawn simpler */
void rd_ease_for(uint32_t physics_ms) { rd_ease = physics_ms > 10 ? 2 : physics_ms > 6 ? 1 : 0; }
#ifndef UP_LO
#define UP_LO 20
#endif
#ifndef UP_HI
#define UP_HI 16
#endif
void rd_adapt(uint32_t spent_ms) {
  static uint32_t slow, quick;
  int q = rd_detail;
  /* (up from the lower resolutions on time alone; above them, only with triangles to spare) */
  if (spent_ms > 26 || rd_dropped) slow++, quick = 0;
  else if (q < 3 ? spent_ms < UP_LO : spent_ms < UP_HI && rd_tris < cap * 3 / 4) quick++, slow = 0;
  else slow = quick = 0;
  /* (a frame near 33 ms: down at once, two steps when past it) */
  if (spent_ms > 30) q -= spent_ms > 36 ? 2 : 1, slow = 0;
  /* (out of triangles but in time, at the nearest distance: not a reason for half the rows) */
  if (slow >= 2 && q > 0 && (spent_ms > 26 || q > 3)) q--, slow = 0;
  if (q < 0) q = 0;
  if (quick >= 30 && q < RD_LEVELS - 1) q++, quick = 0;
  rd_detail = q;
}

/* ------------------------------------------------------------------ drawing a strip */
static uint16_t zbuf[SCREEN_W * STRIP_ROWS] __attribute__((aligned(4)));

__attribute__((noinline)) static void background(int ys, int ye) {
  /* the view ray's height, every 32 pixels (in between, a straight line): dir = right (x - CX)/f + up (CY - y)/f + fwd;
     and the depth buffer cleared */
  float rx = C.right.y / focal, uy = C.up.y / focal, fy = C.fwd.y, ff = 1.0f / (focal * focal);
  uint32_t *zall = (uint32_t *)zbuf;
  for (int x = 0; x < (ye - ys) * SCREEN_W / 2; x += 4) zall[x] = zall[x + 1] = zall[x + 2] = zall[x + 3] = 0;
  int step = rd_half ? 2 : 1;
  for (int y = ys; y < ye; y += step) {
    uint16_t *row = gfx_buf + (y - ys) * SCREEN_W;
    float yy = CY - ((float)y + (float)step * 0.5f);   /* (half the rows: each one drawn twice) */
    float base = uy * yy + fy, yy2 = yy * yy * ff + 1.0f;
    int32_t prev = 0;
    for (int x = 0; x <= SCREEN_W; x += 32) {
      float xx = (float)x - CX;
      float n = (rx * xx + base) / pt_sqrtf(xx * xx * ff + yy2);
      int32_t idx = (int32_t)((n * 127.5f + 127.5f) * 65536.0f);
      if (x > 0) {
        int32_t d = (idx - prev) / 32, f = prev;
        uint16_t *o = row + x - 32;
        if ((prev >> 16) == (idx >> 16) && prev >= 0 && prev < (256 << 16)) {
          uint32_t c = back_lut[prev >> 16] * 0x10001u, *w = (uint32_t *)(void *)o;   /* (two pixels a word) */
          for (int k = 0; k < 16; k += 4) w[k] = w[k + 1] = w[k + 2] = w[k + 3] = c;
        } else if (prev >= 0 && prev < (256 << 16) && idx >= 0 && idx < (256 << 16)) {
          for (int k = 0; k < 32; k++, f += d) o[k] = back_lut[f >> 16];   /* (all of it within the table) */
        } else {
          for (int k = 0; k < 32; k++, f += d) {
            int i = f >> 16;
            o[k] = back_lut[i < 0 ? 0 : i > 255 ? 255 : i];
          }
        }
      }
      prev = idx;
    }
    if (step == 2 && y + 1 < ye) {
      /* (a word at a time: the C library's copies a byte at a time) */
      const uint32_t *a = (const uint32_t *)(void *)row;
      uint32_t *b = (uint32_t *)(void *)(row + SCREEN_W);
      for (int k = 0; k < SCREEN_W / 2; k += 4) b[k] = a[k], b[k + 1] = a[k + 1], b[k + 2] = a[k + 2], b[k + 3] = a[k + 3];
    }
  }
}

__attribute__((noinline)) static void raster(const tri *t, int ys, int ye) {
  float x0 = t->x[0] * (1.0f / 16), y0 = t->y[0] * (1.0f / 16), x1 = t->x[1] * (1.0f / 16), y1 = t->y[1] * (1.0f / 16),
        x2 = t->x[2] * (1.0f / 16), y2 = t->y[2] * (1.0f / 16);
  int ta = iceil(y0 - 0.5f), tb = iceil(y2 - 0.5f);
  int ra = ta > ys ? ta : ys, rb = tb < ye ? tb : ye;
  if (ra >= rb) return;
  float w0 = t->w[0] * (1.0f / 65536), w1 = t->w[1] * (1.0f / 65536), w2 = t->w[2] * (1.0f / 65536);
  float dx1 = x1 - x0, dy1 = y1 - y0, dx2 = x2 - x0, dy2 = y2 - y0;
  float det = dx1 * dy2 - dx2 * dy1;
  if (det > -0.01f && det < 0.01f) return;
  float idet = 1.0f / det;
  float B = ((w1 - w0) * dy2 - (w2 - w0) * dy1) * idet, Cc = (dx1 * (w2 - w0) - dx2 * (w1 - w0)) * idet;
  float s02 = (x2 - x0) / (y2 - y0);
  float s01 = y1 > y0 ? (x1 - x0) / (y1 - y0) : 0, s12 = y2 > y1 ? (x2 - x1) / (y2 - y1) : 0;
  int32_t dq = (int32_t)(pt_clampf(B, -1.9f, 1.9f) * 1073741824.0f);   /* (a sliver's slope is huge: kept in range) */
  uint16_t col = t->c;
  int kind = t->kind;
  /* half the rows: every other one, sampled between the two, written twice */
  int step = 1;
  if (rd_half) {
    step = 2, ra &= ~1;
    if (ra < ys) ra = ys;
  }
  for (int r = ra; r < rb; r += step) {
    float yc = (float)r + (float)step * 0.5f;
    if (step == 2) yc = pt_clampf(yc, y0, y2);
    float xl = x0 + (yc - y0) * s02;
    float xs = yc < y1 ? x0 + (yc - y0) * s01 : x1 + (yc - y1) * s12;
    float xa = xl < xs ? xl : xs, xb = xl < xs ? xs : xl;
    if (xa < -1.0f) xa = -1.0f;
    if (xb > 321.0f) xb = 321.0f;
    uint16_t *p = gfx_buf + (r - ys) * SCREEN_W;
    bool two = step == 2 && r + 1 < ye;
    if (rd_halfx && kind == RK_SOLID) {
      /* half the columns too: blocks of two pixels, each covered when its middle is, depth at its middle */
      int ba = iceil((xa - 1.0f) * 0.5f), bb = iceil((xb - 1.0f) * 0.5f);
      if (ba < 0) ba = 0;
      if (bb > SCREEN_W / 2) bb = SCREEN_W / 2;
      if (bb <= ba) continue;
      float wb = w0 + B * ((float)(2 * ba + 1) - x0) + Cc * (yc - y0);
      wb = pt_clampf(wb, 0.0f, 0.99998f);
      int32_t qb = (int32_t)(wb * 1073741824.0f), d2 = (int32_t)(pt_clampf(2.0f * B, -1.9f, 1.9f) * 1073741824.0f);
      int nb = bb - ba;
      int32_t qe = qb + d2 * (nb - 1);
      if (nb > 1 && (qe < 0 || qe > 1073725440)) d2 = ((qe < 0 ? 0 : 1073725440) - qb) / (nb - 1);
      /* (a block's two pixels always share their depth: a word each, for its depths and its colours) */
      uint32_t *z = (uint32_t *)(void *)(zbuf + (r - ys) * SCREEN_W + 2 * ba), *pp = (uint32_t *)(void *)(p + 2 * ba);
      uint32_t cc = col * 0x10001u;
      int dn = two ? SCREEN_W / 2 : 0;
      for (int k = 0; k < nb; k++, qb += d2, z++, pp++) {
        uint32_t q = (uint32_t)qb >> 14;
        if (q > (*z & 0xFFFF)) z[0] = z[dn] = q * 0x10001u, pp[0] = pp[dn] = cc;
      }
      continue;
    }
    int pa = iceil(xa - 0.5f), pb = iceil(xb - 0.5f);
    if (pa < 0) pa = 0;
    if (pb > SCREEN_W) pb = SCREEN_W;
    if (pb <= pa) continue;
    if (kind == RK_BACK) {
      for (int x = pa; x < pb; x++) p[x] = col;
      if (two)
        for (int x = pa; x < pb; x++) p[SCREEN_W + x] = col;
      continue;
    }
    float wa = w0 + B * ((float)pa + 0.5f - x0) + Cc * (yc - y0);
    if (wa < 0.0f) wa = 0.0f;
    if (wa > 0.99998f) wa = 0.99998f;
    int32_t qa = (int32_t)(wa * 1073741824.0f), d = dq;
    int n = pb - pa;
    int32_t qe = qa + d * (n - 1);
    if (n > 1 && (qe < 0 || qe > 1073725440)) d = ((qe < 0 ? 0 : 1073725440) - qa) / (n - 1);
    uint16_t *z = zbuf + (r - ys) * SCREEN_W + pa;
    p += pa;
    if (kind == RK_SOLID) {
      int x = 0;
#ifdef SIMD
      /* two pixels a word: each half nearer than what is there takes the depth and the colour (in both rows when
         half the rows are drawn, as below) */
      if ((pa & 1) && n) {
        uint32_t q = (uint32_t)qa >> 14;
        if (q > z[0]) {
          z[0] = (uint16_t)q, p[0] = col;
          if (two) z[SCREEN_W] = (uint16_t)q, p[SCREEN_W] = col;
        }
        x = 1, qa += d;
      }
      uint32_t cc = col * 0x10001u;
      uint32_t *zw = (uint32_t *)(void *)(z + x), *pw = (uint32_t *)(void *)(p + x);
      int nw = (n - x) / 2;
      if (two)
        for (int k = 0; k < nw; k++, qa += 2 * d) {
          uint32_t nz = ((uint32_t)qa >> 14) | (((uint32_t)(qa + d) << 2) & 0xFFFF0000u), oz = zw[k];
          __usub16(oz, nz);
          zw[k] = __sel(oz, nz), zw[k + SCREEN_W / 2] = __sel(zw[k + SCREEN_W / 2], nz);
          pw[k] = __sel(pw[k], cc), pw[k + SCREEN_W / 2] = __sel(pw[k + SCREEN_W / 2], cc);
        }
      else
        for (int k = 0; k < nw; k++, qa += 2 * d) {
          uint32_t nz = ((uint32_t)qa >> 14) | (((uint32_t)(qa + d) << 2) & 0xFFFF0000u), oz = zw[k];
          __usub16(oz, nz);
          zw[k] = __sel(oz, nz), pw[k] = __sel(pw[k], cc);
        }
      x += 2 * nw;
#endif
      if (two) {
        for (; x < n; x++, qa += d) {
          uint32_t q = (uint32_t)qa >> 14;
          if (q > z[x]) z[x] = z[SCREEN_W + x] = (uint16_t)q, p[x] = p[SCREEN_W + x] = col;
        }
      } else {
        for (; x < n; x++, qa += d) {
          uint32_t q = (uint32_t)qa >> 14;
          if (q > z[x]) z[x] = (uint16_t)q, p[x] = col;
        }
      }
    } else if (kind == RK_GHOST) {
      int odd = (pa + r) & 1;
      for (int x = odd; x < n; x += 2) {
        uint32_t q = (uint32_t)(qa + d * x) >> 14;
        if (q > z[x]) {
          p[x] = col;
          if (two) p[SCREEN_W + x + (x + 1 < n ? 1 : -1)] = col;
        }
      }
    } else {
      for (int x = 0; x < n; x++, qa += d) {
        uint32_t q = (uint32_t)qa >> 14;
        for (int k = 0; k <= (int)two; k++) {
          if (q + 24 >= z[k * SCREEN_W + x]) {
            uint16_t c = p[k * SCREEN_W + x];
            /* 60 % of the sun taken away: about five eighths of the colour */
            p[k * SCREEN_W + x] = (uint16_t)(((c >> 1) & 0x7BEF) + ((c >> 3) & 0x18E3));
          }
        }
      }
    }
  }
}

/* the triangles by the strip they start in, and those still going on */
static uint16_t order[MAX_TRIS], active[MAX_TRIS];
static uint16_t strip_start[240 / STRIP_ROWS + 2];
static int nactive, next_order;

static int strip_of(int16_t y16) {
  int r = iceil((float)y16 * (1.0f / 16) - 0.5f);
  if (r < 0) r = 0;
  if (r > 239) r = 239;
  return r / STRIP_ROWS;
}

void rd_draw(void) {
  int ys = gfx_y0, ye = gfx_y1;
  rd_tris = ntris;
  background(ys, ye);
  int s = ys / STRIP_ROWS;
  if (ys == 0) {
    int cnt[240 / STRIP_ROWS + 2] = {0};
    for (int i = 0; i < ntris; i++) cnt[strip_of(tris[i].y[0])]++;
    int acc = 0;
    for (int k = 0; k < 240 / STRIP_ROWS + 1; k++) strip_start[k] = (uint16_t)acc, acc += cnt[k];
    strip_start[240 / STRIP_ROWS + 1] = (uint16_t)acc;
    int pos[240 / STRIP_ROWS + 2];
    for (int k = 0; k < 240 / STRIP_ROWS + 2; k++) pos[k] = strip_start[k];
    for (int i = 0; i < ntris; i++) order[pos[strip_of(tris[i].y[0])]++] = (uint16_t)i;
    nactive = 0, next_order = 0;
  }
  /* the ones that ended above this strip go; the ones starting in it come */
  int m = 0;
  for (int k = 0; k < nactive; k++)
    if (strip_of(tris[active[k]].y[2]) >= s) active[m++] = active[k];
  nactive = m;
  for (; next_order < strip_start[s + 1 > 240 / STRIP_ROWS + 1 ? 240 / STRIP_ROWS + 1 : s + 1]; next_order++)
    active[nactive++] = order[next_order];
  for (int pass = 0; pass < 3; pass++)
    for (int k = 0; k < nactive; k++) {
      const tri *t = &tris[active[k]];
      int kd = t->kind == RK_BACK ? 0 : t->kind == RK_SHADOW ? 2 : 1;
      if (kd == pass) raster(t, ys, ye);
    }
}
