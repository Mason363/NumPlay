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

#define VIEW 62.0f     /* metres ahead drawn */
#define LOD_FAR 22.0f  /* the far meshes past this */
#define SHADES 32

static uint16_t shade_lut[NCOLOURS][SHADES];
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
    } else
      for (int x = 0; x < SCREEN_W; x++) row[x] = sky_at(e0 + (e1 - e0) * (float)x / SCREEN_W);
  }
}

/* ---------------------------------------------------------------- the world into polygons */
#define MAXVB 640
static V3 vb[MAXVB];

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
    vb[i] = (V3){R[0] * wx + R[1] * wy + R[2] * dz, R[3] * wx + R[4] * wy + R[5] * dz, R[6] * wx + R[7] * wy + R[8] * dz};
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
    const uint16_t *t = rm->t + i * 3;
    if (t[0] >= nv || t[1] >= nv || t[2] >= nv) continue;
    V3 p[3] = {vb[t[0]], flip ? vb[t[2]] : vb[t[1]], flip ? vb[t[1]] : vb[t[2]]};
    if (p[0].z < 0.1f && p[1].z < 0.1f && p[2].z < 0.1f) continue;
    const int8_t *n = rm->n + i * 3;
    float d = n[0] * l0 + n[1] * l1 + n[2] * l2;
    int sh = d <= 0 ? 0 : d >= 1 ? SHADES - 1 : (int)(d * (SHADES - 1) + 0.5f);
    r_poly_cam(p, 3, shade_lut[rm->c[i]][sh], 0, 255);
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
    draw_mesh(&rmeshes[vz - r > LOD_FAR ? pt->far : pt->near], W);
  }
}

static V3 to_cam(float x, float y, float z) {
  const float *R = cam_rot;
  float dx = x - cam_pos[0], dz = z - cam_pos[2], dy = y - cam_pos[1] - dz * dz * CURVATURE;
  return (V3){R[0] * dx + R[1] * dy + R[2] * dz, R[3] * dx + R[4] * dy + R[5] * dz, R[6] * dx + R[7] * dy + R[8] * dz};
}

/* the sled's shadow: its box laid on the snow under it, pushed away from the sun by its height over the snow */
static void sled_shadow(int k) {
  const Skin *sk = &skins[k];
  float m[12];
  sled_model_matrix(m);
  float ground = sled.py + 0.02f, lift = (sled.y + sled.model_y + sk->box_c[1]) - ground;
  if (lift < 0) lift = 0;
  float sx = -light[0] / light[1] * lift, sz = -light[2] / light[1] * lift;
  V3 p[4];
  for (int i = 0; i < 4; i++) {
    float c[3] = {sk->box_c[0] + (i == 1 || i == 2 ? sk->box_h[0] : -sk->box_h[0]) * 0.9f, sk->box_c[1],
                  sk->box_c[2] + (i >= 2 ? -sk->box_h[2] : sk->box_h[2]) * 0.9f}, w[3];
    m_point(m, c, w);
    p[i] = to_cam(w[0] + sx, ground, w[2] + sz);
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
    if (sled.pgrounded || sled.py > sled.y - 3) sled_shadow(k);
  }
  for (int i = 0; i < MAXP_PIECES; i++)
    if (pieces[i].alive) {
      float m[12];
      m_trs(m, pieces[i].p, pieces[i].q, pieces[i].s);
      draw_prefab(pieces[i].prefab, m);
    }
  for (int i = 0; i < MAXB; i++)
    if (balls[i].alive) draw_prefab(ball_inst[i].prefab, ball_inst[i].m);
  /* the slope, nearest structures first */
  float z = cam_pos[2];
  for (int pass = 0; pass < 2; pass++)
    for (int i = 0; i < MAXI; i++) {
      const Inst *in = &insts[i];
      if (!in->alive) continue;
      bool near = in->lo[2] < z + 25 && in->hi[2] > z - 10;
      if (near == (pass == 0)) draw_prefab(in->prefab, in->m);
    }
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

static void outlined(const np_font_t *f, int cx, int y, const char *s, color_t c) {
  for (int k = 0; k < 4; k++) gfx_text_center(f, cx + (k & 1 ? 1 : -1), y + (k & 2 ? 1 : -1), s, INK, 200);
  gfx_text_center(f, cx, y, s, c, 256);
}

static void gift_icon(int x, int y, int s) {
  gfx_fill(x, y + s / 4, s, s - s / 4, RED);
  gfx_fill(x - 1, y + s / 4 - 2, s + 2, s / 4, RED);
  gfx_fill(x + s / 2 - 1, y + s / 4 - 2, 3, s - s / 4 + 2, GOLD);
  gfx_fill(x - 1, y + s / 2, s + 2, 2, GOLD);
  gfx_circle(x + s / 2 - 3, y + s / 4 - 4, 3, GOLD, 256);
  gfx_circle(x + s / 2 + 3, y + s / 4 - 4, 3, GOLD, 256);
}

static void gifts_counter(int v) {
  char s[12];
  number(s, v);
  int w = gfx_text_width(&np_font_body, s);
  gfx_rrect(SCREEN_W - w - 40, 6, w + 34, 24, 12, INK, 150);
  gift_icon(SCREEN_W - w - 33, 9, 16);
  gfx_text(&np_font_body, SCREEN_W - w - 12, 24, s, WHITE, 256);
}

static void choice(int cx, int y, int w, const char *s, bool on) {
  if (on) gfx_rrect(cx - w / 2, y - 18, w, 26, 13, GOLD, 256);
  gfx_text_center(&np_font_body, cx, y, s, on ? INK : WHITE, 256);
}

static void panel(int x, int y, int w, int h) {
  gfx_shadow(x, y, w, h, 14, 6, 90);
  gfx_rrect(x, y, w, h, 14, PANEL, 235);
  gfx_rrect_outline(x, y, w, h, 14, 2, WHITE, 120);
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

static void snow_project(void) {
  if (!flakes_set) {
    for (int i = 0; i < NFLAKES; i++) flake[i][0] = rnd_range(-6, 6), flake[i][1] = rnd_range(-3, 4), flake[i][2] = rnd_range(0.5f, 12);
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
    if (flake[i][2] < 0.4f) flake[i][2] += 12, flake[i][0] = rnd_range(-6, 6), flake[i][1] = rnd_range(-3, 4);
  }
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
    outlined(&np_font_body, SCREEN_W / 2, 206, T("Press OK"), WHITE);
  } else if (mode == M_MAIN && screen == S_NONE) {
    logo(34, 4, 2);
    panel(20, 92, 128, 106);
    choice(84, 122, 108, T("Play"), sel == 0);
    choice(84, 152, 108, T("Sleds"), sel == 1);
    choice(84, 182, 108, T("Quit"), sel == 2);
    number(s, best);
    gfx_rrect(36, 204, 96, 24, 12, INK, 150);
    gfx_text(&np_font_body, 46, 222, T("Best"), WHITE, 200);
    gfx_text_right(&np_font_body, 124, 222, s, GOLD, 256);
    gifts_counter(gifts);
    outlined(&np_font_small, 236, 222, T("Arrows: steer, Up: jump"), WHITE);
  } else if (screen == S_SLEDS) {
    gifts_counter(gifts);
    const Skin *sk = &skins[shop_skin];
    panel(60, 150, 200, 76);
    gfx_text_center(&np_font_body, SCREEN_W / 2, 174, T(sk->name), WHITE, 256);
    if (shop_skin == skin) gfx_text_center(&np_font_body, SCREEN_W / 2, 204, T("Selected"), GOLD, 256);
    else if (owned >> shop_skin & 1) choice(SCREEN_W / 2, 206, 120, T("Select"), true);
    else {
      number(s, sk->price);
      int w = gfx_text_width(&np_font_body, s);
      bool can = gifts >= sk->price;
      gfx_rrect(SCREEN_W / 2 - w / 2 - 22, 188, w + 44, 26, 13, can ? GOLD : INK, 256);
      gift_icon(SCREEN_W / 2 - w / 2 - 14, 192, 14);
      gfx_text(&np_font_body, SCREEN_W / 2 - w / 2 + 6, 206, s, can ? INK : WHITE, 256);
    }
    gfx_text_center(&np_font_title, 40, 196, "<", WHITE, 256);
    gfx_text_center(&np_font_title, 280, 196, ">", WHITE, 256);
    gfx_text_center(&np_font_small, SCREEN_W / 2, 236, T("OK: choose   Back: done"), WHITE, 220);
  } else if (mode == M_PLAY || mode == M_END) {
    if (mode == M_PLAY && sled.z > WARMUP_LEN * CHUNK_Z) {
      number(s, score);
      outlined(&np_font_title, SCREEN_W / 2, score_pop > 0 ? 44 : 40, s, WHITE);
    }
    gifts_counter(mode == M_PLAY ? gifts_game : gifts);
    if (mode == M_END && end_wait > 1.2f) {
      gfx_fill_alpha(0, 0, SCREEN_W, SCREEN_H, 0, 80);
      panel(70, 40, 180, 172);
      gfx_text_center(&np_font_body, 160, 66, best_now ? T("New best!") : T("Score"), best_now ? GOLD : WHITE, 256);
      number(s, score);
      gfx_text_center(&np_font_title, 160, 102, s, WHITE, 256);
      number(s, best);
      gfx_text(&np_font_body, 92, 130, T("Best"), WHITE, 200);
      gfx_text_right(&np_font_body, 228, 130, s, GOLD, 256);
      choice(160, 166, 140, T("Play again"), sel == 0);
      choice(160, 196, 140, T("Menu"), sel == 1);
    }
  }
  if (screen == S_GUIDE) {
    gfx_fill_alpha(0, 0, SCREEN_W, SCREEN_H, 0, 110);
    panel(40, 30, 240, 180);
    gfx_text_center(&np_font_title, 160, 60, T("How to play"), WHITE, 256);
    gfx_paragraph(&np_font_body, 160, 88, 210, 18,
                  T("Left and Right steer the sled. Up or OK jumps. Dodge the trees, rocks, snowmen and walls, and pick up gifts to buy new sleds."),
                  WHITE, 256);
    choice(160, 196, 120, T("Ride!"), true);
  }
  if (screen == S_PAUSE || screen == S_ASK) {
    gfx_fill_alpha(0, 0, SCREEN_W, SCREEN_H, 0, 110);
    panel(80, 56, 160, 132);
    gfx_text_center(&np_font_title, 160, 86, screen == S_PAUSE ? T("Paused") : T("Quit game?"), WHITE, 256);
    if (screen == S_PAUSE)
      choice(160, 118, 130, T("Resume"), sel == 0), choice(160, 146, 130, T("Restart"), sel == 1), choice(160, 174, 130, T("Quit game"), sel == 2);
    else
      choice(160, 132, 130, T("No"), sel == 0), choice(160, 162, 130, T("Yes"), sel == 1);
  }
}

void draw_frame(void) {
  r_camera(cam_pos[0], cam_pos[1], cam_pos[2], cam_rot, focal, SCREEN_W / 2, SCREEN_H / 2);
  draw_world();
  snow_project();
  r_render(sky, overlay);
}
