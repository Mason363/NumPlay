#include <string.h>
/* The world on the screen (scene.h): the path, its jungle, the runner and the pursuers, coins and power-ups,
 * every one a mesh of data.c by number, placed and turned in the game's units, then drawn by r3d.c in metres
 * through a camera that follows the runner round the corners. */
#include "data.h"
#include "game.h"
#include "scene.h"

#define S 0.19f              /* metres a game unit (the runner: 9.6 units, 1.8 m) */
/* the game's: its fog (linear, from 100 to 400 units, in its teal), its camera's 50 degrees from top to bottom,
 * and the jungle below the path (its Water Plane: 30.5 below the temple's floor, its picture 30 times over its
 * 2000 units) */
#define FOG0 100.0f
#define FOG1 400.0f
#define FOG_RGB 0x325256
#define FOCAL 257.3f         /* 120 / tan(25 degrees) */
#define WATER_Y (-30.5f)
#define WATER_TILE (2000.0f / 30)
#define SMALL_FAR 350.0f     /* rocks, heads and ground patches seen this far */
#define TREE_FAR FOG1        /* the rest of the scenery (past it, all fog) */
#define SCENERY_POLYS 1080   /* (of the renderer's 1600 a frame) */
#define VIEW (FOG1 + 20)     /* units seen ahead */
#define PI 3.14159265f

static float cx_, cy_, cz_, cyaw, cc, cs;   /* the camera (game units) and its yaw's cosine and sine */
static float run_yaw, cam_h;                /* the run's way, smoothed; the camera's ground height */
static float camx, camy, camz;              /* the camera's place in a run (the game's GameCamera) */
static float intro;                         /* seconds the camera still waits where the title had it */
static float t_anim;
static uint32_t scenery_seed;   /* (the scenery of this run: draw_scenery) */
static bool title;

static float wrap(float a) {
  while (a > PI) a -= 2 * PI;
  while (a < -PI) a += 2 * PI;
  return a;
}

/* the angle of (y, x) from the x axis, -PI..PI (a short series: no C library) */
static float atan2_(float y, float x) {
  float ax = x < 0 ? -x : x, ay = y < 0 ? -y : y, mx = ax > ay ? ax : ay, mn = ax > ay ? ay : ax;
  if (mx <= 0) return 0;
  float a = mn / mx, s = a * a;
  float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
  if (ay > ax) r = PI / 2 - r;
  if (x < 0) r = PI - r;
  return y < 0 ? -r : r;
}

static float dir_yaw(int d) { return d == 1 ? PI / 2 : d == 2 ? PI : d == 3 ? -PI / 2 : 0; }

void scene_init(void) {
  r_fog0 = FOG0 * S, r_fog1 = FOG1 * S, r_fog_rgb = FOG_RGB;
  r_far = 40;
}

/* the game's camera: 50 behind the runner and 35 above the ground it runs on, looking at 20 above it (sliding under
 * a branch: 60 behind, 6 and 14 above; jumping: higher and farther by half the jump, the look by a quarter), going
 * after that place at 1.25 times the run's speed; the ground height it keeps follows the runner's at 50 a second */
static void cam_want(float *x, float *y, float *z, float *fy) {
  float h = 35, d = 50, f = 20, j = pl.y - cam_h, s, c;
  if (pl.sliding && tiles[path_root].obst == OB_SLIDE) h = 6, d = 60, f = 14;
  else if (pl.jumping && j > 0) h += 0.5f * j, d += 0.5f * j, f += 0.25f * j;
  r_sincos(run_yaw, &s, &c);
  *x = pl.x - s * d, *z = pl.z - c * d, *y = cam_h + h, *fy = cam_h + f;
}

/* a 60th of a second: the camera goes after the runner (when it's caught or falls, the camera stays) */
void scene_follow(void) {
  float want = dir_yaw(pl.dir);
  run_yaw += wrap(want - run_yaw) * 0.3f;
  if (pl.dead) return;
  if (intro > 0) {
    intro -= 1.0f / 60;
    return;
  }
  if (!pl.falling) {
    float g = pl.ground;
    cam_h = cam_h < g ? (cam_h + 50.0f / 60 > g ? g : cam_h + 50.0f / 60) : (cam_h - 50.0f / 60 < g ? g : cam_h - 50.0f / 60);
  }
  float tx, ty, tz, fy;
  cam_want(&tx, &ty, &tz, &fy);
  float dx = tx - camx, dy = ty - camy, dz = tz - camz, m2 = dx * dx + dy * dy + dz * dz, step = pl.v * 1.25f / 60;
  if (step * step >= m2) camx = tx, camy = ty, camz = tz;
  else {
    float k = step / __builtin_sqrtf(m2);
    camx += dx * k, camy += dy * k, camz += dz * k;
  }
}

void scene_reset(void) {
  scenery_seed = plat_random();
  run_yaw = dir_yaw(pl.dir);
  cam_h = pl.ground;
  /* the game's menu camera: 100 before the temple and 10 up, looking at the runner in its door; a run starts with it
   * there for 3 seconds (the game's intro scene), the runner running out at it, and then it goes after him */
  const Tile *t = &tiles[path_root];
  camx = path_cx(t), camz = path_cz(t) + 100, camy = cam_h + 10;
  intro = 3;
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
  /* the corners in place once each, in the camera's space and on the screen; a mesh with more than MESH_CORNERS (the
   * temple) has its faces' moved as they come */
  static SV v[MESH_CORNERS];
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
    v[i] = r_corner((V3){(x0 + mx * c + mz * s) * S, (dy + my) * S, (z0 - mx * s + mz * c) * S});
  }
#define CORNER(j) (each ? r_corner((V3){(x0 + me->v[j].x * 0.25f * c + me->v[j].z * 0.25f * s) * S, (dy + me->v[j].y * 0.25f) * S, \
                                       (z0 - me->v[j].x * 0.25f * s + me->v[j].z * 0.25f * c) * S}) : v[j])
  int flags = alpha < 255 ? P_ALPHA : 0;
  for (int i = 0; i < me->nf; i++) {
    const MFace *f = &me->f[i];
    V3 p[12];
    int n = f->n > 12 ? 12 : f->n;
    for (int k = 0; k < n; k++) {
      SV q = CORNER(me->idx[f->first + k]);
      p[k] = (V3){q.x, q.y, q.z};
    }
    float nx = f->nx * (1.0f / 127), ny = f->ny * (1.0f / 127), nz = f->nz * (1.0f / 127);
    V3 nn = {nx * c + nz * s, ny, -nx * s + nz * c};
    r_poly_view(p, n, r_shade(f->rgb, nn), flags | (f->flags & 1 ? P_TWOSIDED : 0), alpha);
  }
  /* (a path piece's far version has its faces biggest first: farther off, only those; scenery stays whole, as its
   * leaves and roots would be left in the air) */
  int nt = me->nt;
  if (m < 2 * M_PIECES) {
    if (z0 > 300) nt /= 8;
    else if (z0 > 160) nt /= 5;
    else if (z0 > MESH_FAR) nt /= 3;
  }
  for (int i = 0; i < nt; i++) {
    const TFace *t = &me->t[i];
    const uint16_t *ix = me->tidx + 3 * i;
    const int16_t *q = me->uv + 6 * i;
    SV a = CORNER(ix[0]), b = CORNER(ix[1]), cc3 = CORNER(ix[2]);
    float uv[6] = {q[0] * 0.25f, q[1] * 0.25f, q[2] * 0.25f, q[3] * 0.25f, q[4] * 0.25f, q[5] * 0.25f};
    /* (all in front of the camera: as it is on the screen, turned away ones left out at once) */
    if (a.z >= R_NEAR && b.z >= R_NEAR && cc3.z >= R_NEAR && r_tri_tex_screen(&a, &b, &cc3, uv, t->tex, t->light + 1, flags, t->avg)) continue;
    V3 p[3] = {{a.x, a.y, a.z}, {b.x, b.y, b.z}, {cc3.x, cc3.y, cc3.z}};
    r_poly_tex_view(p, uv, 3, t->tex, t->light + 1, flags, t->avg);
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
/* the background, row by row: the jungle far below (the game's Water Plane) where the row looks down at it, its
 * picture fixed on the ground and its depth there; the fog's colour above (r3d.c fogs both by depth) */
static void bg(void) {
  const Texture *t = r_tex[2];
  const uint8_t *px = t->px;
  const uint16_t *pal = t->pal;
  uint16_t fog = r_565(FOG_RGB);
  float hgt = (WATER_Y - cy_) * S, k = (float)(1 << t->wl) / (WATER_TILE * S);   /* (texels a metre) */
  uint32_t m = (1u << t->wl) - 1;
  for (int y = r_y0; y < r_y1; y++) {
    uint16_t *p = r_col + (y - r_y0) * SCREEN_W;
    float vs = ((float)y + 0.5f - cam.cy) / cam.f;
    float ry = -vs * cam.cp - cam.sp, rz = -vs * cam.sp + cam.cp;   /* (a ray down the row's middle, z ahead 1) */
    float tz = ry < -1e-4f ? hgt / ry : 1e9f;                       /* its depth where it meets the jungle */
    if (tz > r_fog1) {
      for (int x = 0; x < SCREEN_W; x++) p[x] = fog;
      continue;
    }
    /* along the row: the ground's place at x, straight lines in the picture */
    float ax = (0.5f - cam.cx) / cam.f * tz, az = rz * tz, sx = tz / cam.f;
    float u0 = ((cx_ * S) + ax * cc + az * cs) * k, v0 = ((cz_ * S) - ax * cs + az * cc) * k;
    int32_t u = (int32_t)(u0 * 65536.0f), v = (int32_t)(v0 * 65536.0f);
    int32_t du = (int32_t)(sx * cc * k * 65536.0f), dv = (int32_t)(-sx * cs * k * 65536.0f);
    for (int x = 0; x < SCREEN_W; x++, u += du, v += dv)
      p[x] = pal[px[(((uint32_t)v >> 16) & m) << t->wl | (((uint32_t)u >> 16) & m)]];
    r_band_depth(y, 0, SCREEN_W, r_floor_depth(tz));
  }
}

/* ---------------------------------------------------------------- the frame */
void scene_draw(r_band_fn overlay, bool on_title) {
  title = on_title;
  t_anim += 1.0f / 30;
  r_begin();
  /* the camera: the game's, behind the runner and above it, looking at it */
  float pitch;
  if (on_title) {
    /* the game's menu camera, looking at the runner in the temple's door; turned a little, so that he stands to the
     * right of the menu */
    cx_ = camx, cy_ = camy, cz_ = camz;
    float dx = pl.x - cx_, dz = pl.z - cz_;
    cyaw = atan2_(dx, dz) - 0.28f, pitch = atan2_(cy_ - (cam_h + 20), __builtin_sqrtf(dx * dx + dz * dz));
    r_sincos(cyaw, &cs, &cc);
  } else {
    static float fx, fy, fz;   /* (it looks where it looked when the run ended) */
    if (!pl.dead) {
      float tx, ty, tz;
      cam_want(&tx, &ty, &tz, &fy);
      fx = pl.x, fz = pl.z;
    }
    cx_ = camx, cy_ = camy, cz_ = camz;
    float dx = fx - cx_, dz = fz - cz_, hd = __builtin_sqrtf(dx * dx + dz * dz);
    cyaw = atan2_(dx, dz), pitch = atan2_(cy_ - fy, hd);
    r_sincos(cyaw, &cs, &cc);
  }
  r_camera(0, 0, 0, pitch, FOCAL, 160, 120);
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
