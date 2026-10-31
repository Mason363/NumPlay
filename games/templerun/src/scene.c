/* The world on the screen (scene.h): the path, its jungle, the runner and the pursuers, coins and power-ups,
 * every one a mesh of data.c by number, placed and turned in the game's units, then drawn by r3d.c in metres
 * through a camera that follows the runner round the corners. */
#include "data.h"
#include "game.h"
#include "scene.h"

#define S 0.19f              /* metres a game unit (the runner: 9.6 units, 1.8 m) */
#define VIEW 1000.0f         /* units seen ahead (the renderer's far plane is 190 m) */
#define PI 3.14159265f

static float cx_, cy_, cz_, cyaw, cc, cs;   /* the camera (game units) and its yaw's cosine and sine */
static float run_yaw, cam_yaw, cam_h;       /* smoothed */
static float t_anim;
static bool title;
static uint16_t rowc[SCREEN_H];             /* the sky and the depths, row by row */
static int horizon = -1000;

static float wrap(float a) {
  while (a > PI) a -= 2 * PI;
  while (a < -PI) a += 2 * PI;
  return a;
}

static float dir_yaw(int d) { return d == 1 ? PI / 2 : d == 2 ? PI : d == 3 ? -PI / 2 : 0; }

void scene_init(void) {
  r_haze0 = 70, r_haze1 = 185;
  r_haze_rgb = 0xBFD3C6;
  r_far = 60;
  if (plat_slow()) r_haze0 = 60, r_haze1 = 170;
}

/* a 60th of a second: the camera turns after the runner */
void scene_follow(void) {
  float want = dir_yaw(pl.dir);
  run_yaw += wrap(want - run_yaw) * 0.3f;
  cam_yaw += wrap(want - cam_yaw) * (pl.boost ? 0.16f : 0.1f);
  if (!pl.falling && !(pl.dead && pl.death == DEATH_FALL)) cam_h += (pl.ground - cam_h) * 0.08f;
}

void scene_reset(void) {
  run_yaw = cam_yaw = dir_yaw(pl.dir);
  cam_h = pl.ground;
}

/* ---------------------------------------------------------------- meshes */
void scene_mesh(int m, float ox, float oy, float oz, float yaw, int alpha) {
  float dx = ox - cx_, dy = oy - cy_, dz = oz - cz_;
  float x0 = dx * cc - dz * cs, z0 = dx * cs + dz * cc;
  const Mesh *me = &meshes[m];
  float r = me->r;
  if (z0 + r < 0 || z0 - r > VIEW) return;
  if (x0 - r > z0 * 1.6f + 8 || -x0 - r > z0 * 1.6f + 8) return;
  if (me->far >= 0 && z0 > MESH_FAR) me = &meshes[me->far];
  float s, c;
  r_sincos(yaw - cyaw, &s, &c);
  static V3 v[256];
  int nv = 0;
  for (int i = 0; i < me->nf; i++) {
    int e = me->f[i].first + me->f[i].n;
    for (int k = me->f[i].first; k < e; k++)
      if (me->idx[k] >= nv) nv = me->idx[k] + 1;
  }
  if (nv > 256) nv = 256;
  for (int i = 0; i < nv; i++) {
    float mx = me->v[i].x * 0.25f, my = me->v[i].y * 0.25f, mz = me->v[i].z * 0.25f;
    v[i] = (V3){(x0 + mx * c + mz * s) * S, (dy + my) * S, (z0 - mx * s + mz * c) * S};
  }
  for (int i = 0; i < me->nf; i++) {
    const MFace *f = &me->f[i];
    V3 p[12];
    int n = f->n > 12 ? 12 : f->n;
    for (int k = 0; k < n; k++) p[k] = v[me->idx[f->first + k] < 256 ? me->idx[f->first + k] : 0];
    float nx = f->nx * (1.0f / 127), ny = f->ny * (1.0f / 127), nz = f->nz * (1.0f / 127);
    V3 nn = {nx * c + nz * s, ny, -nx * s + nz * c};
    int flags = (f->flags & 1 ? P_TWOSIDED : 0) | (alpha < 255 ? P_ALPHA : 0);
    r_poly(p, n, r_shade(f->rgb, nn), flags, alpha);
  }
}

static int path_mesh(const Tile *t) {
  static const uint8_t base[TH_COUNT] = {M_TEMPLE_STRAIGHT, M_PLANK_STRAIGHT, M_CLIFF_STRAIGHT};
  return base[t->theme] + 2 * t->kind;   /* (data.c: each kind, then its far version) */
}

static void draw_tile(const Tile *t, bool ahead) {
  float x = game_tile_cx(t), z = game_tile_cz(t), yaw = dir_yaw(t->dir);
  float dx = x - cx_, dz = z - cz_;
  if (dx * cs + dz * cc < -60 || dx * dx + dz * dz > (VIEW + 60) * (VIEW + 60)) return;
  scene_mesh(path_mesh(t), x, t->y, z, yaw, 255);
  /* the cliffs' rock wall: only along the way the runner goes (it would stand in the camera's way) */
  if (t->theme == TH_CLIFF && t->kind <= PK_HOLE_LONG && ahead && t->dir == pl.dir) scene_mesh(M_CLIFF_WALL, x, t->y, z, yaw, 255);
  int d = t->dir;
  float rx = (float)(d == 0 ? 1 : d == 2 ? -1 : 0), rz = (float)(d == 1 ? -1 : d == 3 ? 1 : 0);
  float fx = -rz, fz = rx;
  if (t->scenery & 1) scene_mesh(t->scenery & 4 ? M_JUNGLE_TALL : M_JUNGLE_TREE, x - rx * 27 + fx * 8, t->y, z - rz * 27 + fz * 8, yaw, 255);
  if (t->scenery & 2) scene_mesh(t->scenery & 4 ? M_JUNGLE_TREE : M_JUNGLE_TALL, x + rx * 27 - fx * 10, t->y, z + rz * 27 - fz * 10, yaw, 255);
  if (t->scenery & 8) scene_mesh(M_PILLAR, x - rx * 15 + fx * 18, t->y, z - rz * 15 + fz * 18, yaw, 255);
  if (t->ob) {
    static const uint8_t ob[5] = {0, M_ROOT_LOW, M_TREE_JUMP, M_BRANCH_SLIDE, M_FIRE0};
    int m = ob[t->ob];
    if (t->ob == OB_FIRE) m += ((int)(t_anim * 8)) & 3;
    scene_mesh(m, x, t->y, z, yaw, 255);
  }
  static const uint8_t coin[4] = {M_COIN_Y, M_COIN_Y, M_COIN_R, M_COIN_B};
  for (int k = 0; k < 5; k++) {
    float px, py, pz;
    if (game_coin_pos(t, k, &px, &py, &pz)) scene_mesh(coin[t->coin_kind & 3], px, py, pz, t_anim * 4.5f + (float)k, 255);
  }
  if (t->item) {
    static const uint8_t item[5] = {0, M_COIN_BONUS, M_INVISIBILITY, M_MAGNET, M_BOOST};
    scene_mesh(item[t->item], x, t->y + 0.6f * r_sin_(t_anim * 3), z, t_anim * 2.0f, 255);
  }
}

float r_sin_(float a) {
  float s, c;
  r_sincos(a, &s, &c);
  return s;
}

static int runner_mesh(void) {
  if (pl.dead) return pl.death == DEATH_FALL ? M_RUNNER_FALL : M_RUNNER_DOWN;
  if (pl.sliding) return M_RUNNER_SLIDE;
  if (pl.falling) return M_RUNNER_FALL;
  if (pl.jumping || !pl.on_ground) return M_RUNNER_JUMP;
  if (pl.stumbling) return M_RUNNER_STUMBLE;
  return M_RUNNER_RUN0 + (((int)(pl.anim * 4)) & 3);
}

/* ---------------------------------------------------------------- the sky and the depths */
static void bg(void) {
  for (int y = r_y0; y < r_y1; y++) {
    uint16_t c = rowc[y], *p = r_col + (y - r_y0) * SCREEN_W;
    for (int x = 0; x < SCREEN_W; x++) p[x] = c;
  }
}

static void sky_rows(int h) {
  if (h == horizon) return;
  horizon = h;
  for (int y = 0; y < SCREEN_H; y++) {
    uint32_t c;
    if (y < h) {
      int t = h <= 0 ? 256 : 256 - (h - y) * 256 / (h + 40);
      c = r_mix888(0x6FA6D2, 0xBFD3C6, t < 0 ? 0 : t);
    } else {
      int t = (y - h) * 256 / 130;
      c = r_mix888(0xBFD3C6, 0x2B4A30, t > 256 ? 256 : t);
    }
    rowc[y] = r_565(c);
  }
}

/* ---------------------------------------------------------------- the frame */
void scene_draw(r_band_fn overlay, bool on_title) {
  title = on_title;
  t_anim += 1.0f / 30;
  r_begin();
  /* the camera: behind the runner and above, turning after it round the corners */
  float back = 31, up = 19, pitch = 0.27f;
  if (on_title) {
    /* in front of the runner, who stands at the temple's door: the menu on the left, the runner on the right */
    float b = PI + 0.25f, sb, cb;
    r_sincos(b, &sb, &cb);
    cx_ = pl.x - sb * 25, cz_ = pl.z - cb * 25, cy_ = pl.y + 8;
    cyaw = b - 0.4f, pitch = 0.06f;
    r_sincos(cyaw, &cs, &cc);
  } else {
    cyaw = cam_yaw;
    r_sincos(cyaw, &cs, &cc);
    cx_ = pl.x - cs * back, cz_ = pl.z - cc * back, cy_ = cam_h + up;
    if (pl.y > pl.ground && !pl.falling) cy_ += 0.6f * (pl.y - pl.ground);   /* (a jump stays in sight) */
  }
  r_camera(0, 0, 0, pitch, 205, 160, 120);
  float sx, sy;
  sky_rows(r_project((V3){0, 0, 600}, &sx, &sy) ? (int)sy : 0);
  for (int i = 0; i < path_count; i++) draw_tile(tile_at(i), i >= pl.tile);
  if (stub_on)
    for (int s = 0; s < 2; s++)
      for (int i = 0; i < NSTUB; i++) draw_tile(&stub[s][i], false);
  scene_mesh(M_TEMPLE, 0, 0, 0, 0, 255);
  for (int f = 0; f < NFLY; f++)
    if (fly[f].on) scene_mesh(fly[f].kind == 3 ? M_COIN_B : fly[f].kind == 2 ? M_COIN_R : M_COIN_Y, fly[f].x, fly[f].y - 3, fly[f].z, t_anim * 6, 255);
  /* the runner's shadow on the floor */
  if (!on_title && pl.over && !(pl.dead && pl.death == DEATH_FALL)) {
    float gy = pl.ground, dx = pl.x - cx_, dz = pl.z - cz_;
    float x0 = dx * cc - dz * cs, z0 = dx * cs + dz * cc;
    V3 p[8];
    for (int i = 0; i < 8; i++) {
      float s, c;
      r_sincos((float)i * (PI / 4), &s, &c);
      p[i] = (V3){(x0 + 2.4f * c) * S, (gy - cy_ + 0.08f) * S, (z0 - 2.4f * s) * S};
    }
    r_poly(p, 8, 0x000000, P_ALPHA | P_TWOSIDED, 90);
  }
  int alpha = 255;
  if (pl.invis > 0 && !pl.dead) alpha = pl.invis < 2.5f && ((int)(pl.invis * 8) & 1) ? 200 : 110;
  if (!(pl.dead && pl.death == DEATH_FALL && pl.y < pl.ground - 30))
    scene_mesh(on_title ? M_RUNNER_RUN0 : runner_mesh(), pl.x, pl.y, pl.z, on_title ? 0 : run_yaw, alpha);
  if (pursuers_on && !on_title)
    for (int i = 0; i < NPURSUERS; i++) {
      const Pursuer *p = &pursuers[i];
      scene_mesh(M_PURSUER_RUN0 + (((int)(p->phase * 4) + i) & 3), p->x, p->y, p->z, p->yaw, 255);
    }
  r_render(bg, overlay);
}
