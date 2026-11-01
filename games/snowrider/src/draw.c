/* Snow Rider 3D: a frame.
 *
 * The world is drawn the way its CurvedWorld shader draws it: every corner sinks by the square of how far ahead
 * of the camera it is (times 0.004), so the slope bends away over a near horizon. A face takes its palette
 * colour lit like Unity lights it (the scene's flat ambient plus 0.9 of the sun by the face's angle), then the
 * camera's colour grading (saturation +12). The sky is the procedural skybox's, from its horizon up. Over it all
 * the score, the gifts and the menus, in NumPlay's fonts. */
#include "sr.h"
#include "r3d.h"
#include "gfx.h"
#include "../../common/np_text.h"
#include "ui.h"

#define VIEW 62.0f     /* metres ahead drawn */
#define LOD_FAR 30.0f  /* the far meshes past this, at most */
#define SHADES 32

static uint16_t shade_lut[NCOLOURS][SHADES];
static float lod = LOD_FAR;   /* (nearer when the polygons did not all fit last frame) */
static uint16_t sky_lut[64];
static float focal;
static const float light[3] = LIGHT_DIR;

static uint16_t rgb565f(float r, float g, float b) {
  r = r < 0 ? 0 : r > 1 ? 1 : r, g = g < 0 ? 0 : g > 1 ? 1 : g, b = b < 0 ? 0 : b > 1 ? 1 : b;
  return (uint16_t)(((int)(r * 31 + 0.5f) << 11) | ((int)(g * 63 + 0.5f) << 5) | (int)(b * 31 + 0.5f));
}

static uint16_t grade(float r, float g, float b) {
  r = fminf(r, 1), g = fminf(g, 1), b = fminf(b, 1);
  float l = 0.2126f * r + 0.7152f * g + 0.0722f * b;
  return rgb565f(l + (r - l) * 1.12f, l + (g - l) * 1.12f, l + (b - l) * 1.12f);
}

void draw_init(void) {
  const float amb[3] = AMBIENT;
  for (int c = 0; c < NCOLOURS; c++) {
    const uint8_t *p = palette888[c];
    for (int s = 0; s < SHADES; s++) {
      float d = (float)s / (SHADES - 1) * LIGHT_INTENSITY;
      shade_lut[c][s] = grade(p[0] / 255.0f * (amb[0] + d), p[1] / 255.0f * (amb[1] + d), p[2] / 255.0f * (amb[2] + d));
    }
  }
  /* the sky by height: below the horizon the skybox's ground colour, above it from a pale horizon to blue */
  const float gnd[3] = SKY_GROUND;
  for (int i = 0; i < 64; i++) {
    float e = (float)(i - 16) / 64;
    if (e < 0) sky_lut[i] = grade(gnd[0], gnd[1], gnd[2]);
    else {
      float t = sqrtf(e / 0.75f);
      t = t > 1 ? 1 : t;
      sky_lut[i] = grade(0.86f + (0.38f - 0.86f) * t, 0.93f + (0.62f - 0.93f) * t, 0.97f + (0.90f - 0.97f) * t);
    }
  }
  focal = (SCREEN_H / 2) / tanf(CAM_FOV * DEG / 2);
}

static uint16_t sky_at(float e) {
  int i = (int)(e * 64) + 16;
  return sky_lut[i < 0 ? 0 : i > 63 ? 63 : i];
}

static void sky(void) {
  const float *R = cam_rot;
  for (int y = r_y0; y < r_y1; y++) {
    float dy = -((float)y + 0.5f - SCREEN_H / 2) / focal;
    float e0, e1;
    for (int k = 0; k < 2; k++) {
      float dx = ((k ? SCREEN_W : 0) - SCREEN_W / 2) / focal;
      float wy = dx * R[1] + dy * R[4] + R[7];
      float e = wy / sqrtf(dx * dx + dy * dy + 1);
      if (k) e1 = e;
      else e0 = e;
    }
    uint16_t *row = r_col + (y - r_y0) * SCREEN_W;
    if ((int)(e0 * 64) == (int)(e1 * 64)) {
      uint16_t c = sky_at(e0);
      for (int x = 0; x < SCREEN_W; x++) row[x] = c;
    } else {
      /* (a leaning camera: the height steps along the row, in 1/65536 of the table's) */
      int32_t i = (int32_t)((e0 * 64 + 16) * 65536), di = (int32_t)((e1 - e0) * 64 * 65536 / SCREEN_W);
      for (int x = 0; x < SCREEN_W; x++, i += di) {
        int k = i >> 16;
        row[x] = sky_lut[k < 0 ? 0 : k > 63 ? 63 : k];
      }
    }
  }
}

/* ---------------------------------------------------------------- the world into polygons */
#define MAXVB 640
static V3 vb[MAXVB];        /* a mesh's corners: on the screen (r_to_screen) when in front of the camera, else in its space */
static uint8_t vin[MAXVB];  /* (1: in front) */

static void draw_mesh(const RMesh *rm, const float W[12]) {
  float s = rm->s;
  float cz = cam_pos[2];
  const float *R = cam_rot;
  int nv = rm->nv > MAXVB ? MAXVB : rm->nv;
  for (int i = 0; i < nv; i++) {
    const int16_t *v = rm->v + i * 3;
    float x = v[0] * s, y = v[1] * s, z = v[2] * s;
    float wx = W[0] * x + W[1] * y + W[2] * z + W[3] - cam_pos[0];
    float wy = W[4] * x + W[5] * y + W[6] * z + W[7] - cam_pos[1];
    float wz = W[8] * x + W[9] * y + W[10] * z + W[11];
    float dz = wz - cz;
    wy -= dz * dz * CURVATURE;
    V3 c = {R[0] * wx + R[1] * wy + R[2] * dz, R[3] * wx + R[4] * wy + R[5] * dz, R[6] * wx + R[7] * wy + R[8] * dz};
    /* (each corner projected once, for all its triangles) */
    vin[i] = c.z >= 0.1f;
    vb[i] = vin[i] ? r_to_screen(c) : c;
  }
  float det = m_det(W);
  bool flip = det < 0;
  float inv = 1 / (127 * cbrtf(fabsf(det) > 1e-9f ? fabsf(det) : 1));
  /* the sun in the mesh's own space, so a face's light is its normal against it */
  float l0 = (W[0] * light[0] + W[4] * light[1] + W[8] * light[2]) * inv;
  float l1 = (W[1] * light[0] + W[5] * light[1] + W[9] * light[2]) * inv;
  float l2 = (W[2] * light[0] + W[6] * light[1] + W[10] * light[2]) * inv;
  /* (a turn times a size s, maybe mirrored: the normal turns with W / s, and n . (W^T L) / s is its light) */
  for (int i = 0; i < rm->nt; i++) {
    int t0 = corner_of(rm->t, rm->nv, i * 3), t1 = corner_of(rm->t, rm->nv, i * 3 + 1), t2 = corner_of(rm->t, rm->nv, i * 3 + 2);
    if (t0 >= nv || t1 >= nv || t2 >= nv) continue;
    int a = t0, b = flip ? t2 : t1, c = flip ? t1 : t2;
    int in = vin[a] + vin[b] + vin[c];
    if (!in) continue;
    const int8_t *n = rm->n + i * 3;
    float d = n[0] * l0 + n[1] * l1 + n[2] * l2;
    int sh = d <= 0 ? 0 : d >= 1 ? SHADES - 1 : (int)(d * (SHADES - 1) + 0.5f);
    uint16_t col = shade_lut[rm->c[i]][sh];
    if (in == 3 && r_tri_screen(&vb[a], &vb[b], &vb[c], col)) continue;
    /* (crossing the near plane, or reaching far off the screen: cut in camera space) */
    V3 p[3] = {vin[a] ? r_from_screen(vb[a]) : vb[a], vin[b] ? r_from_screen(vb[b]) : vb[b], vin[c] ? r_from_screen(vb[c]) : vb[c]};
    r_poly_cam(p, 3, col, 0, 255);
  }
}

static void draw_prefab(int prefab, const float m[12]) {
  const Prefab *pf = &prefabs[prefab];
  const float *R = cam_rot;
  for (int i = 0; i < pf->nparts; i++) {
    const Part *pt = &pf->parts[i];
    float W[12], c[3];
    m_mul(W, m, pt->m);
    const RMesh *rm = &rmeshes[pt->near];
    m_point(W, rm->ctr, c);
    float r = rm->rad * cbrtf(fabsf(m_det(W)) + 1e-9f);
    float dx = c[0] - cam_pos[0], dz = c[2] - cam_pos[2], dy = c[1] - cam_pos[1] - dz * dz * CURVATURE;
    float vx = R[0] * dx + R[1] * dy + R[2] * dz, vy = R[3] * dx + R[4] * dy + R[5] * dz, vz = R[6] * dx + R[7] * dy + R[8] * dz;
    if (vz + r < 0.1f || vz - r > VIEW) continue;
    if (fabsf(vx) - r * 1.5f > (vz + r) * (SCREEN_W / 2 + 8) / focal) continue;
    if (fabsf(vy) - r * 1.5f > (vz + r) * (SCREEN_H / 2 + 8) / focal) continue;
    draw_mesh(&rmeshes[vz - r > lod ? pt->far : pt->near], W);
  }
}

static V3 to_cam(float x, float y, float z) {
  const float *R = cam_rot;
  float dx = x - cam_pos[0], dz = z - cam_pos[2], dy = y - cam_pos[1] - dz * dz * CURVATURE;
  return (V3){R[0] * dx + R[1] * dy + R[2] * dz, R[3] * dx + R[4] * dy + R[5] * dz, R[6] * dx + R[7] * dy + R[8] * dz};
}

/* the sled's shadow (the sun's): the bottom of its box, each corner cast along the sun's light onto the snow under it */
static void sled_shadow(int k) {
  const Skin *sk = &skins[k];
  float m[12];
  sled_model_matrix(m);
  V3 p[4];
  for (int i = 0; i < 4; i++) {
    float c[3] = {sk->box_c[0] + (i == 1 || i == 2 ? sk->box_h[0] : -sk->box_h[0]) * 0.9f, sk->box_c[1] - sk->box_h[1],
                  sk->box_c[2] + (i >= 2 ? -sk->box_h[2] : sk->box_h[2]) * 0.9f}, w[3], dn[3] = {0, -1, 0};
    m_point(m, c, w);
    float o[3] = {w[0], w[1] + 0.3f, w[2]};
    Hit h;
    if (!ray_cast(o, dn, 6, L_SURFACE, &h)) return;   /* (over a hole: no snow to fall on) */
    float lift = w[1] - h.p[1];
    lift = lift < 0 ? 0 : lift;
    p[i] = to_cam(w[0] - light[0] / light[1] * lift, h.p[1] + 0.02f, w[2] - light[2] / light[1] * lift);
  }
  r_poly_cam(p, 4, 0x2945, P_ALPHA | P_TWOSIDED, 70);
}

static void draw_world(void) {
  r_begin();
  if (camp_on)
    for (int i = 0; i < ncamp; i++)
      if (camp[i].alive) draw_prefab(camp[i].prefab, camp[i].m);
  /* the sled first: the nearest polygons win the room left */
  bool sled_shown = mode == M_MAIN || (mode == M_PLAY && sled.alive);
  if (sled_shown) {
    float m[12];
    sled_model_matrix(m);
    int k = screen == S_SLEDS ? shop_skin : skin;
    draw_prefab(skins[k].prefab, m);
    sled_shadow(k);
  }
  for (int i = 0; i < MAXP_PIECES; i++)
    if (pieces[i].alive) {
      float m[12];
      m_trs(m, pieces[i].p, pieces[i].q, pieces[i].s);
      draw_prefab(pieces[i].prefab, m);
    }
  for (int i = 0; i < MAXB; i++)
    if (balls[i].alive) draw_prefab(ball_inst[i].prefab, ball_inst[i].m);
  /* the slope, nearest first 5 m at a time: when the polygons do not all fit, the farthest are left out */
  float z = cam_pos[2];
  for (int band = 0; band * 5 < VIEW + 20; band++)
    for (int i = 0; i < MAXI; i++) {
      const Inst *in = &insts[i];
      if (!in->alive) continue;
      float d = fmaxf(fmaxf(in->lo[2] - z, z - in->hi[2]), 0);
      if ((int)(d / 5) == band) draw_prefab(in->prefab, in->m);
    }
  /* (the far meshes come nearer while it is full, and go back out as room comes back) */
  if (r_full()) lod = fmaxf(lod - 1, 4);
  else if (r_count() < 1750) lod = fminf(lod + 0.25f, LOD_FAR);
}

/* ---------------------------------------------------------------- over the picture */
#define WHITE 0xFFFF
#define INK HEX(0x16324F)
#define PANEL HEX(0x1E4D7A)
#define GOLD HEX(0xFFD34E)
#define RED HEX(0xD8343A)

static void number(char *s, int v) {
  char t[12];
  int n = 0;
  if (v < 0) v = 0;
  do t[n++] = (char)('0' + v % 10), v /= 10;
  while (v);
  for (int i = 0; i < n; i++) s[i] = t[n - 1 - i];
  s[n] = 0;
}






/* the game's logo (logo.c), its top left at (x, y), every step-th pixel (1 or 2) */
extern const uint16_t logo_w, logo_h, logo_pal[64];
extern const uint8_t logo_px[];
static void logo(int x, int y, int step) {
  int h = logo_h / step, w = logo_w / step;
  int ya = y > r_y0 ? y : r_y0, yb = y + h < r_y1 ? y + h : r_y1;
  for (int yy = ya; yy < yb; yy++) {
    const uint8_t *src = logo_px + (yy - y) * step * logo_w;
    uint16_t *dst = r_col + (yy - r_y0) * SCREEN_W + x;
    for (int xx = 0; xx < w; xx++) {
      uint8_t c = src[xx * step];
      if (c && x + xx >= 0 && x + xx < SCREEN_W) dst[xx] = logo_pal[c];
    }
  }
}

/* the scene's falling snow (SnowParticles): flakes around the camera, drifting down */
#define NFLAKES 70
static float flake[NFLAKES][3];
static bool flakes_set;

static int nflakes;
static int16_t fx[NFLAKES], fy[NFLAKES];
static uint8_t fs[NFLAKES];
/* (their own random numbers, as a particle system's: how often frames are drawn never changes the slope) */
static uint32_t flake_seed = 0x2545F491;
static float flake_rnd(float a, float b) {
  flake_seed ^= flake_seed << 13, flake_seed ^= flake_seed >> 17, flake_seed ^= flake_seed << 5;
  return a + (b - a) * (float)(flake_seed >> 8) * (1.0f / 16777215);
}

static void snow_project(void) {
  if (!flakes_set) {
    for (int i = 0; i < NFLAKES; i++) flake[i][0] = flake_rnd(-6, 6), flake[i][1] = flake_rnd(-3, 4), flake[i][2] = flake_rnd(0.5f, 12);
    flakes_set = true;
  }
  static float last_t;
  float dt = game_time - last_t;
  last_t = game_time;
  if (dt < 0 || dt > 0.2f) dt = 0;
  nflakes = 0;
  for (int i = 0; i < NFLAKES; i++) {
    float *f = flake[i];
    f[1] -= 0.9f * dt, f[0] += 0.25f * sinf(game_time * 0.7f + i) * dt;
    /* the flakes keep their place in the world while the camera goes by */
    float w[3] = {f[0], f[1], f[2]};
    if (w[1] < -3) f[1] += 7;
    float z = w[2];
    if (z < 0.4f) continue;
    int x = (int)(SCREEN_W / 2 + w[0] / z * focal), y = (int)(SCREEN_H / 2 - w[1] / z * focal);
    if (x < 0 || x >= SCREEN_W - 2 || y < 0 || y >= SCREEN_H - 2) continue;
    fx[nflakes] = (int16_t)x, fy[nflakes] = (int16_t)y, fs[nflakes] = z < 3 ? 2 : 1, nflakes++;
  }
  /* (moving ahead brings them nearer) */
  float ahead = mode == M_PLAY ? sled.speed * dt : 0;
  for (int i = 0; i < NFLAKES; i++) {
    flake[i][2] -= ahead;
    if (flake[i][2] < 0.4f) flake[i][2] += 12, flake[i][0] = flake_rnd(-6, 6), flake[i][1] = flake_rnd(-3, 4);
  }
}

/* ---------------------------------------------------------------- the game's own interface (ui.c) */
#define UI_GREY 0xDEFB      /* (223, 223, 223): its lettering */
#define UI_YELLOW 0xFE60    /* (255, 206, 0): BUY */
#define UI_GREEN 0x754B     /* (118, 171, 93): SELECTED */
#define UI_LIME 0xE7E0      /* (231, 255, 0): TAP TO RESTART */
static void ui_pic(int pic, int x, int y) {
  const UiPic *p = &ui_pics[pic];
  int ya = y > gfx_y0 ? y : gfx_y0, yb = y + p->h < gfx_y1 ? y + p->h : gfx_y1;
  for (int yy = ya; yy < yb; yy++) {
    const uint8_t *src = ui_px + p->off + (yy - y) * p->w;
    uint16_t *dst = gfx_buf + (yy - gfx_y0) * SCREEN_W;
    for (int xx = 0; xx < p->w; xx++) {
      int k = src[xx], X = x + xx;
      if (!k || X < 0 || X >= SCREEN_W) continue;
      int a = ui_alpha[k];
      dst[X] = a >= 248 ? ui_pal[k] : r_mix565(dst[X], ui_pal[k], a >> 3);
    }
  }
}
/* its lettering: light grey with a black outline, as the game's (align: -1 left, 0 centre, 1 right) */
static void ui_text(const np_font_t *f, int x, int y, const char *s, uint16_t c, bool outline, int align) {
  int w = gfx_text_width(f, s);
  x -= align < 0 ? 0 : align == 0 ? w / 2 : w;
  if (outline)
    for (int d = 0; d < 8; d++) {
      static const int8_t o[8][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, 1}, {-1, 1}, {1, -1}};
      gfx_text(f, x + o[d][0], y + o[d][1], s, 0, 256);
    }
  gfx_text(f, x, y, s, c, 256);
}
static void ui_gifts(int v) {   /* the game's GiftGUI: its gift and the count, at the top left */
  char s[12];
  number(s, v);
  ui_pic(UI_GIFT, 6, 4);
  ui_text(&sr_font_mid, 32, 21, s, 0xFFFF, true, -1);
}
static void ui_sign(int icon, const char *key) {   /* the wooden sign at the bottom left, its icon, the key that opens it */
  ui_pic(UI_SIGN, 2, SCREEN_H - 44);
  ui_pic(icon, 6, SCREEN_H - 35);
  ui_text(&sr_font_small, 46, SCREEN_H - 19, key, UI_GREY, true, -1);
}

static void overlay(void) {
  gfx_buf = r_col, gfx_y0 = r_y0, gfx_y1 = r_y1;
  for (int i = 0; i < nflakes; i++) {
    if (fy[i] + fs[i] <= r_y0 || fy[i] >= r_y1) continue;
    gfx_fill(fx[i], fy[i], fs[i], fs[i], 0xFFFF);
  }
  char s[24];
  if (mode == M_INTRO) {
    logo(60, 18, 1);
    ui_text(&sr_font_mid, SCREEN_W / 2, 220, T("PRESS OK TO START"), 0, false, 0);
  } else if (mode == M_MAIN && screen == S_NONE) {
    ui_text(&sr_font_mid, SCREEN_W / 2, 52, T("PRESS OK TO PLAY !"), 0, false, 0);
    ui_gifts(gifts);
    ui_sign(UI_SLED, T("LEFT  SLEDS"));
  } else if (screen == S_SLEDS) {
    ui_gifts(gifts);
    const Skin *sk = &skins[shop_skin];
    ui_text(&sr_font_big, SCREEN_W / 2, 176, T(sk->name), UI_GREY, true, 0);
    if (shop_skin == skin) ui_text(&sr_font_big, SCREEN_W / 2, 212, T("SELECTED"), UI_GREEN, true, 0);
    else if (owned >> shop_skin & 1) ui_text(&sr_font_big, SCREEN_W / 2, 212, T("SELECT"), UI_YELLOW, true, 0);
    else {
      number(s, sk->price);
      int w = gfx_text_width(&sr_font_mid, s), bw = gfx_text_width(&sr_font_big, T("BUY"));
      int x = SCREEN_W / 2 - (bw + 30 + w) / 2;
      ui_text(&sr_font_big, x, 212, T("BUY"), gifts >= sk->price ? UI_YELLOW : UI_GREY, true, -1);
      ui_pic(UI_GIFT, x + bw + 6, 192);
      ui_text(&sr_font_mid, x + bw + 30, 210, s, UI_GREY, true, -1);
    }
    ui_pic(UI_ARROW_BUTTON, 20, 108);
    ui_pic(UI_ARROW_BUTTON, SCREEN_W - 50, 108);
    ui_text(&sr_font_mid, 35, 130, "<", 0x2104, false, 0);
    ui_text(&sr_font_mid, SCREEN_W - 35, 130, ">", 0x2104, false, 0);
    ui_text(&sr_font_small, SCREEN_W / 2, 236, T("OK CHOOSE    BACK DONE"), UI_GREY, true, 0);
  } else if (mode == M_PLAY || mode == M_END) {
    if (mode == M_PLAY && sled.z > WARMUP_LEN * CHUNK_Z) {
      number(s, score);
      ui_text(&sr_font_big, SCREEN_W / 2, score_pop > 0 ? 40 : 36, s, 0xFFFF, true, 0);
    }
    if (mode == M_PLAY) {
      ui_gifts(gifts);
      /* the game's first run: its arrow keys and what they do */
      if (plays < 3 && sled.z < WARMUP_LEN * CHUNK_Z) {
        ui_pic(UI_KEYS, SCREEN_W / 2 - 32, SCREEN_H - 48);
        ui_text(&sr_font_small, SCREEN_W / 2, SCREEN_H - 54, T("UP ARROW TO JUMP"), 0xFFFF, true, 0);
        ui_text(&sr_font_small, SCREEN_W / 2 - 40, SCREEN_H - 20, T("STEER LEFT"), 0xFFFF, true, 1);
        ui_text(&sr_font_small, SCREEN_W / 2 + 40, SCREEN_H - 20, T("STEER RIGHT"), 0xFFFF, true, -1);
      }
    }
    if (mode == M_END && end_wait > 1.2f) {
      gfx_fill_alpha(0, 0, SCREEN_W, SCREEN_H, 0, 70);
      logo(110, 2, 2);
      number(s, score);
      ui_text(&sr_font_big, 70, 132, s, UI_GREY, true, 0);
      ui_text(&sr_font_mid, 70, 154, best_now ? T("NEW BEST") : T("SCORE"), UI_GREY, true, 0);
      number(s, best);
      ui_text(&sr_font_big, 160, 132, s, UI_GREY, true, 0);
      ui_text(&sr_font_mid, 160, 154, T("BEST"), UI_GREY, true, 0);
      s[0] = '+';
      number(s + 1, gifts_game);
      ui_text(&sr_font_big, 250, 132, s, UI_GREY, true, 0);
      ui_text(&sr_font_mid, 250, 154, T("GIFTS"), UI_GREY, true, 0);
      ui_text(&sr_font_mid, SCREEN_W / 2, 196, T("PRESS OK TO RESTART"), UI_LIME, true, 0);
      ui_text(&sr_font_small, SCREEN_W / 2, 220, T("BACK MENU"), UI_GREY, true, 0);
    }
  }
  if (screen == S_GUIDE) {
    gfx_fill_alpha(0, 0, SCREEN_W, SCREEN_H, 0, 140);
    ui_text(&sr_font_big, SCREEN_W / 2, 64, T("HOW TO PLAY"), UI_GREY, true, 0);
    gfx_paragraph(&np_font_body, 160, 92, 230, 18,
                  T("Left and Right steer the sled. Up or OK jumps. Dodge the trees, rocks, snowmen and walls, and pick up gifts to buy new sleds."),
                  WHITE, 256);
    ui_text(&sr_font_mid, SCREEN_W / 2, 206, T("PRESS OK TO RIDE !"), UI_LIME, true, 0);
  }
  if (screen == S_PAUSE || screen == S_ASK) {
    gfx_fill_alpha(0, 0, SCREEN_W, SCREEN_H, 0, 120);
    ui_text(&sr_font_big, SCREEN_W / 2, 84, screen == S_PAUSE ? T("PAUSED") : T("QUIT GAME?"), UI_GREY, true, 0);
    static const char *const pause_items[3] = {"RESUME", "RESTART", "QUIT GAME"}, *const ask_items[2] = {"NO", "YES"};
    int n = screen == S_PAUSE ? 3 : 2;
    for (int i = 0; i < n; i++) {
      const char *t = screen == S_PAUSE ? T(pause_items[i]) : T(ask_items[i]);
      ui_text(&sr_font_mid, SCREEN_W / 2, 122 + i * 28, t, sel == i ? UI_YELLOW : UI_GREY, true, 0);
      if (sel == i) ui_text(&sr_font_mid, SCREEN_W / 2 - gfx_text_width(&sr_font_mid, t) / 2 - 12, 122 + i * 28, ">", UI_YELLOW, true, 0);
    }
  }
  if (fade > 0) gfx_fill_alpha(0, 0, SCREEN_W, SCREEN_H, 0, (int)(fade * 256));   /* (over everything) */
}

void draw_frame(void) {
  r_camera(cam_pos[0], cam_pos[1], cam_pos[2], cam_rot, focal, SCREEN_W / 2, SCREEN_H / 2);
  draw_world();
  snow_project();
  r_render(sky, overlay);
}
