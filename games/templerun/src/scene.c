#include <string.h>
/* The world on the screen (scene.h): the path, its jungle, the runner and the pursuers, coins and power-ups,
 * every one a mesh of data.c by number, placed and turned in the game's units, then drawn by r3d.c in metres
 * through a camera that follows the runner round the corners. */
#include "data.h"
#include "game.h"
#include "scene.h"

#define S 0.19f              /* metres a game unit (the runner: 9.6 units, 1.8 m) */
#define SMALL_FAR 350.0f     /* rocks, heads and ground patches seen this far */
#define TREE_FAR 520.0f      /* the rest of the scenery (into the haze) */
#define SCENERY_POLYS 1080   /* (of the renderer's 1600 a frame) */
#define VIEW 1000.0f         /* units seen ahead (the renderer's far plane is 190 m) */
#define PI 3.14159265f

static float cx_, cy_, cz_, cyaw, cc, cs;   /* the camera (game units) and its yaw's cosine and sine */
static float run_yaw, cam_yaw, cam_h;       /* smoothed */
static float t_anim;
static uint32_t scenery_seed;   /* (the scenery of this run: draw_scenery) */
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
  r_far = 40;
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
  scenery_seed = plat_random();
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
  /* the corners in place once each; a mesh with more than MESH_CORNERS (the temple) has its faces' moved as they come */
  static V3 v[MESH_CORNERS];
  int nv = 0;
  for (int i = 0; i < me->nf; i++) {
    int e = me->f[i].first + me->f[i].n;
    for (int k = me->f[i].first; k < e; k++)
      if (me->idx[k] >= nv) nv = me->idx[k] + 1;
  }
  for (int k = 0; k < 3 * me->nt; k++)
    if (me->tidx[k] >= nv) nv = me->tidx[k] + 1;
  bool each = nv > MESH_CORNERS;
  for (int i = 0; i < nv && !each; i++) {
    float mx = me->v[i].x * 0.25f, my = me->v[i].y * 0.25f, mz = me->v[i].z * 0.25f;
    v[i] = (V3){(x0 + mx * c + mz * s) * S, (dy + my) * S, (z0 - mx * s + mz * c) * S};
  }
#define CORNER(j) (each ? (V3){(x0 + me->v[j].x * 0.25f * c + me->v[j].z * 0.25f * s) * S, (dy + me->v[j].y * 0.25f) * S, \
                              (z0 - me->v[j].x * 0.25f * s + me->v[j].z * 0.25f * c) * S} : v[j])
  int flags = alpha < 255 ? P_ALPHA : 0;
  for (int i = 0; i < me->nf; i++) {
    const MFace *f = &me->f[i];
    V3 p[12];
    int n = f->n > 12 ? 12 : f->n;
    for (int k = 0; k < n; k++) p[k] = CORNER(me->idx[f->first + k]);
    float nx = f->nx * (1.0f / 127), ny = f->ny * (1.0f / 127), nz = f->nz * (1.0f / 127);
    V3 nn = {nx * c + nz * s, ny, -nx * s + nz * c};
    r_poly(p, n, r_shade(f->rgb, nn), flags | (f->flags & 1 ? P_TWOSIDED : 0), alpha);
  }
  /* (a path piece's far version has its faces biggest first: farther off, only those; scenery stays whole, as its
   * leaves and roots would be left in the air) */
  int nt = me->nt;
  if (m < 2 * M_PIECES) {
    if (z0 > 300) nt /= 4;
    else if (z0 > 160) nt /= 2;
  }
  for (int i = 0; i < nt; i++) {
    const TFace *t = &me->t[i];
    const uint16_t *ix = me->tidx + 3 * i;
    const int16_t *q = me->uv + 6 * i;
    V3 p[3] = {CORNER(ix[0]), CORNER(ix[1]), CORNER(ix[2])};
    float uv[6] = {q[0] * 0.25f, q[1] * 0.25f, q[2] * 0.25f, q[3] * 0.25f, q[4] * 0.25f, q[5] * 0.25f};
    r_poly_tex_avg(p, uv, 3, t->tex, t->light + 1, flags, t->avg);
  }
#undef CORNER
}

static void draw_tile(const Tile *t) {
  float x = path_cx(t), z = path_cz(t);
  float dx = x - cx_, dz = z - cz_;
  if (dx * cs + dz * cc < -60 || dx * dx + dz * dz > (VIEW + 60) * (VIEW + 60)) return;
  /* the piece: the game's prefab, laid unturned (its far version: data.c's next block) */
  if (t->mesh >= 0) scene_mesh(t->mesh, x, 0, z, 0, 255);
  if (t->obst == OB_FLAME) {
    /* the fire across the flame tower (a vertical piece's runs along x) */
    bool v = t->type == PT_FLAME_V;
    scene_mesh(M_FIRE0 + (((int)(t_anim * 8)) & 3), x, t->y, z, v ? 0 : PI / 2, 255);
  }
  static const uint8_t coin[4] = {M_COIN_Y, M_COIN_Y, M_COIN_R, M_COIN_B};
  for (int k = 0; k < 5; k++) {
    float px, py, pz;
    if (path_coin_pos(t, k, &px, &py, &pz)) scene_mesh(coin[t->coin_kind & 3], px, py, pz, t_anim * 4.5f + (float)k, 255);
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

/* ---------------------------------------------------------------- the scenery (the game's SceneryManager) */
/* A grid of 19 x 19 squares of 60 around the camera's square: those the path is in are left out (FillMask), and
 * so is the start, in front of the temple; each other one has a piece of scenery, the game's odds of each kind,
 * seen one time in four (ObjectDensity), 10 or less off the square's middle, at y 0. What each square has is
 * worked out from where it is (and the run), so it stays the same while it is seen. */
#define SGRID 19
static uint32_t mix(uint32_t h) {
  h ^= h >> 16, h *= 0x7FEB352Du, h ^= h >> 15, h *= 0x846CA68Bu, h ^= h >> 16;
  return h;
}

static void draw_scenery(void) {
  static const uint8_t trees[4] = {M_treeAlone, M_treeAlone2, M_treeAlone3, M_treeAlone4};
  static const uint8_t rocks[3] = {M_rock, M_rock2, M_rock3}, cliffs[3] = {M_cliff, M_cliff2, M_cliff3};
  int gx = (int)(cx_ / TILE), gz = (int)(cz_ / TILE);   /* (the game's: truncated) */
  int x0 = gx - SGRID / 2, z0 = gz - SGRID / 2;
  static uint8_t mask[SGRID][SGRID];
  memset(mask, 0, sizeof mask);
  for (int i = 0; i < NTILES; i++)
    if (tiles[i].alive) {
      int a = tiles[i].gx - x0, b = tiles[i].gz - z0;
      if (a >= 0 && b >= 0 && a < SGRID && b < SGRID) mask[a][b] = 1;
    }
  /* nearest first, ring by ring round the camera's square, and no more once the frame has SCENERY_POLYS polygons: on a
   * busy frame what goes is a tree far off */
  for (int ring = 0; ring <= SGRID / 2; ring++)
    for (int a = SGRID / 2 - ring; a <= SGRID / 2 + ring; a++)
      for (int b = SGRID / 2 - ring; b <= SGRID / 2 + ring; b += (a == SGRID / 2 - ring || a == SGRID / 2 + ring) ? 1 : 2 * ring) {
        if (r_count() >= SCENERY_POLYS) return;
        int cx = x0 + a, cz = z0 + b;
        if (mask[a][b] || (cx >= -1 && cx <= 1 && cz >= 0 && cz <= 4)) continue;
        uint32_t h = mix((uint32_t)cx * 0x9E3779B1u ^ mix((uint32_t)cz + scenery_seed));
        if (h % 1000 >= 250) continue;   /* (ObjectDensity 0.25) */
        uint32_t k = (h >> 10) % 21, v = h >> 15;
        int m = k <= 10 ? trees[v % 4] : k <= 14 ? rocks[v % 3] : k <= 16 ? cliffs[v % 3] : k <= 18 ? M_overgrowthTrees : k == 19 ? M_templeHead : M_groundPatchTemple;
        float ox = (float)((int)((h >> 20) % 21) - 10), oz = (float)((int)((h >> 25) % 21) - 10);
        float wx = (float)cx * TILE + ox, wz = (float)cz * TILE + oz, dx = wx - cx_, dz = wz - cz_;
        /* (the small ones, a few pixels in the haze past SMALL_FAR, aren't drawn there) */
        if (k >= 11 && k <= 14 && dx * dx + dz * dz > SMALL_FAR * SMALL_FAR) continue;
        if (k >= 19 && dx * dx + dz * dz > SMALL_FAR * SMALL_FAR) continue;
        if (dx * dx + dz * dz > TREE_FAR * TREE_FAR) continue;
        scene_mesh(m, wx, 0, wz, 0, 255);
      }
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
  /* the pieces: the one under the runner and those ahead (the one behind it too, for the camera), nearest first:
   * the renderer lays pictures on so many faces a frame, and the near ones need them */
  int ord[NTILES], no = 0;
  float dist[NTILES];
  for (int i = 0; i < NTILES; i++)
    if (tiles[i].alive) {
      float dx = path_cx(&tiles[i]) - cx_, dz = path_cz(&tiles[i]) - cz_, d = dx * dx + dz * dz;
      int k = no++;
      for (; k > 0 && dist[k - 1] > d; k--) ord[k] = ord[k - 1], dist[k] = dist[k - 1];
      ord[k] = i, dist[k] = d;
    }
  for (int k = 0; k < no; k++) draw_tile(&tiles[ord[k]]);
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
  /* the scenery last: on a frame with more than the renderer keeps, what goes is a tree far off, not the runner */
  draw_scenery();
  r_render(bg, overlay);
}
