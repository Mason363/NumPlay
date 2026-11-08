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
#define SCENERY_POLYS 1080   /* the polygons a frame the scenery's reach is kept to (of the renderer's 1800) */
#define INTRO_FAR 250.0f     /* the scenery's reach in the run's intro, the temple's front close by (half in the fog) */
#define LOD_NEAR 70.0f       /* path pieces nearer than this are whole */
#define FADE 50.0f           /* the scenery comes in from the fog over its last units within reach */
#define VIEW (FOG1 + 20)     /* units seen ahead */
#define PI 3.14159265f

static float cx_, cy_, cz_, cyaw, cc, cs;   /* the camera (game units) and its yaw's cosine and sine */
static float run_yaw, cam_h;                /* the run's way, smoothed; the camera's ground height */
static float camx, camy, camz;              /* the camera's place in a run (the game's GameCamera) */
static float intro_t;                       /* seconds into the run's intro (INTRO: over) */
static float start_x, start_y, start_z;     /* the temple's door, where the intro's places are measured from */
static float look_off;                      /* the title's turn of the view, gone in the intro's first quarter second */
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

static void reach_make(void);

void scene_init(void) {
  r_fog0 = FOG0 * S, r_fog1 = FOG1 * S, r_fog_rgb = FOG_RGB;
  r_far = 40;
  reach_make();
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

/* the game's intro (its GameStart): the camera goes in a quarter of a second from where the title had it to the
 * game's menu place, 86 before the temple and 10 up, then in 2.5 s, easing in and out, to 20 left, 45 up and 210
 * before it, looking at the runner all the while (he holds still the first second, then runs out under it) */
static void intro_camera(float t) {
  static const float at[3][3] = {{0, 10, 100}, {0, 10, 86}, {-20, 45, 210}};
  int k = t < 0.25f ? 0 : 1;
  float u = k ? (t - 0.25f) / 2.5f : t / 0.25f, e;
  if (u > 1) u = 1;
  if (k == 0) e = (u - 1) * (u - 1) * (u - 1) + 1;   /* (MovingObject's easing: out, then in and out) */
  else e = u < 0.5f ? 4 * u * u * u : 0.5f * ((2 * u - 2) * (2 * u - 2) * (2 * u - 2) + 2);
  camx = start_x + at[k][0] + (at[k + 1][0] - at[k][0]) * e;
  camy = start_y + at[k][1] + (at[k + 1][1] - at[k][1]) * e;
  camz = start_z + at[k][2] + (at[k + 1][2] - at[k][2]) * e;
  look_off = k ? 0 : -0.28f * (1 - e);
}

/* a 60th of a second: the camera goes after the runner (when it's caught or falls, the camera stays) */
void scene_follow(void) {
  float want = dir_yaw(pl.dir);
  run_yaw += wrap(want - run_yaw) * 0.3f;
  if (pl.dead) return;
  if (!pl.falling) {
    float g = pl.ground;
    cam_h = cam_h < g ? (cam_h + 50.0f / 60 > g ? g : cam_h + 50.0f / 60) : (cam_h - 50.0f / 60 < g ? g : cam_h - 50.0f / 60);
  }
  if (intro_t < INTRO) {
    intro_t += 1.0f / 60;
    intro_camera(intro_t);
    if (intro_t < INTRO) return;
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
  /* the title's camera: 100 before the temple and 10 up, looking at the runner in its door (turned a little); a run
   * starts from there (intro_camera) */
  const Tile *t = &tiles[path_root];
  start_x = path_cx(t), start_y = cam_h, start_z = path_cz(t);
  intro_t = 0;
  intro_camera(0);
}

bool scene_intro(void) { return intro_t < INTRO; }

void scene_tick(void) { t_anim += 1.0f / 60; }

/* ---------------------------------------------------------------- meshes */
/* each mesh's reach across (units, from its middle; its height left out): behind the camera, what lies farther than that
 * is not seen (it never looks down so far that what is behind it shows) */
static uint8_t reach[M_COUNT];

static void reach_make(void) {
  for (int m = 0; m < M_COUNT; m++) {
    const Mesh *me = &meshes[m];
    float r2 = 0;
    for (int k = 0; k < 3 * me->nt; k++) {
      const MVert *v = &me->v[me->tidx[k]];
      float x = v->x * 0.25f, z = v->z * 0.25f;
      if (x * x + z * z > r2) r2 = x * x + z * z;
    }
    for (int i = 0; i < me->nf; i++)
      for (int k = me->f[i].first; k < me->f[i].first + me->f[i].n; k++) {
        const MVert *v = &me->v[me->idx[k]];
        float x = v->x * 0.25f, z = v->z * 0.25f;
        if (x * x + z * z > r2) r2 = x * x + z * z;
      }
    float r = __builtin_sqrtf(r2) + 1;
    reach[m] = (uint8_t)(r > me->r ? me->r : r > 255 ? 255 : r);
  }
}

#if NP_SOFT_FLOAT
/* (calculators without a floating-point unit: the same, each corner moved, turned and projected in integers, the
 * mesh's turn and place read once into fixed point; the faces still go through r_poly_view in float) */
#include "np_fixed.h"
static inline void move_fx(const MVert *mv, const int64_t *q0, const int32_t *mx, const int32_t *my, const int32_t *mz,
                           SVq *o) {
  o->x = fx_sat32(q0[0] + (((int64_t)mx[0] * mv->x + (int64_t)mz[0] * mv->z) >> 14));
  o->y = fx_sat32(q0[1] + (((int64_t)mx[1] * mv->x + (int64_t)my[1] * mv->y + (int64_t)mz[1] * mv->z) >> 14));
  o->z = fx_sat32(q0[2] + (((int64_t)mx[2] * mv->x + (int64_t)my[2] * mv->y + (int64_t)mz[2] * mv->z) >> 14));
  if (o->z >= (int32_t)(R_NEAR * 65536)) r_screen_fx(o);
}
void scene_mesh(int m, float ox, float oy, float oz, float yaw, int alpha) {
  float dx = ox - cx_, dy = oy - cy_, dz = oz - cz_;
  float x0 = dx * cc - dz * cs, z0 = dx * cs + dz * cc;
  const Mesh *me = &meshes[m];
  float r = me->r, h = (float)reach[m];
  if (z0 + h < 0 || z0 - h > VIEW) return;
  if (x0 - r > z0 * 1.6f + 8 || -x0 - r > z0 * 1.6f + 8) return;
  if (me->far >= 0 && z0 > MESH_FAR) me = &meshes[me->far];
  float s, c;
  r_sincos(yaw - cyaw, &s, &c);
  /* a corner (a quarter unit) in camera space: q0 + vx mx + vy my + vz mz, the pitch (cam.sp, cam.cp) folded in; the
   * columns in 2^30ths of a metre, the place in 16.16 metres */
  float k = 0.25f * S, sp = cam.sp, cp = cam.cp;
  int32_t mx[3] = {(int32_t)fx_from_float(k * c, 30), (int32_t)fx_from_float(-k * s * sp, 30), (int32_t)fx_from_float(-k * s * cp, 30)};
  int32_t my[3] = {0, (int32_t)fx_from_float(k * cp, 30), (int32_t)fx_from_float(-k * sp, 30)};
  int32_t mz[3] = {(int32_t)fx_from_float(k * s, 30), (int32_t)fx_from_float(k * c * sp, 30), (int32_t)fx_from_float(k * c * cp, 30)};
  int64_t q0[3] = {fx_from_float(x0 * S, 16), fx_from_float((dy * cp + z0 * sp) * S, 16), fx_from_float((-dy * sp + z0 * cp) * S, 16)};
  const int32_t near = (int32_t)(R_NEAR * 65536);
  static SVq v[MESH_CORNERS];
  static uint16_t made[MESH_CORNERS], stamp;
  if (++stamp == 0) {
    memset(made, 0, sizeof made);
    stamp = 1;
  }
  SVq tmp[3];
#define CORNER_FX(j, k3)                                                                                         \
  ((j) >= MESH_CORNERS ? (move_fx(&me->v[j], q0, mx, my, mz, &tmp[k3]), &tmp[k3])                                    \
   : made[j] == stamp  ? &v[j]                                                                                        \
                       : (made[j] = stamp, move_fx(&me->v[j], q0, mx, my, mz, &v[j]), &v[j]))
  int flags = alpha < 255 ? P_ALPHA : 0;
  for (int i = 0; i < me->nf; i++) {
    const MFace *f = &me->f[i];
    V3 p[12];
    int n = f->n > 12 ? 12 : f->n;
    for (int kk = 0; kk < n; kk++) {
      const SVq *q = CORNER_FX(me->idx[f->first + kk], 0);
      p[kk] = (V3){fx_to_float(q->x, 16), fx_to_float(q->y, 16), fx_to_float(q->z, 16)};
    }
    float nx = f->nx * (1.0f / 127), ny = f->ny * (1.0f / 127), nz = f->nz * (1.0f / 127);
    V3 nn = {nx * c + nz * s, ny, -nx * s + nz * c};
    r_poly_view(p, n, r_shade(f->rgb, nn), flags | (f->flags & 1 ? P_TWOSIDED : 0), alpha);
  }
  int nt = me->nt;
  if (m < 2 * M_PIECES) {
    float d2 = dx * dx + dz * dz;
    if (d2 > LOD_NEAR * LOD_NEAR) nt = (int)((float)nt * (LOD_NEAR * LOD_NEAR) / d2);
  }
  for (int i = 0; i < nt; i++) {
    const TFace *t = &me->t[i];
    const uint16_t *ix = me->tidx + 3 * i;
    const int16_t *q = me->uv + 6 * i;
    const SVq *a = CORNER_FX(ix[0], 0), *b = CORNER_FX(ix[1], 1), *cc3 = CORNER_FX(ix[2], 2);
    if (a->z >= near && b->z >= near && cc3->z >= near && r_tri_tex_screen_fx(a, b, cc3, q, t->tex, t->light + 1, flags, t->avg))
      continue;
    float uv[6] = {q[0] * 0.25f, q[1] * 0.25f, q[2] * 0.25f, q[3] * 0.25f, q[4] * 0.25f, q[5] * 0.25f};
    V3 p[3] = {{fx_to_float(a->x, 16), fx_to_float(a->y, 16), fx_to_float(a->z, 16)},
               {fx_to_float(b->x, 16), fx_to_float(b->y, 16), fx_to_float(b->z, 16)},
               {fx_to_float(cc3->x, 16), fx_to_float(cc3->y, 16), fx_to_float(cc3->z, 16)}};
    r_poly_tex_view(p, uv, 3, t->tex, t->light + 1, flags, t->avg);
  }
#undef CORNER_FX
}
#else
void scene_mesh(int m, float ox, float oy, float oz, float yaw, int alpha) {
  float dx = ox - cx_, dy = oy - cy_, dz = oz - cz_;
  float x0 = dx * cc - dz * cs, z0 = dx * cs + dz * cc;
  const Mesh *me = &meshes[m];
  float r = me->r, h = (float)reach[m];
  if (z0 + h < 0 || z0 - h > VIEW) return;
  if (x0 - r > z0 * 1.6f + 8 || -x0 - r > z0 * 1.6f + 8) return;
  if (me->far >= 0 && z0 > MESH_FAR) me = &meshes[me->far];
  float s, c;
  r_sincos(yaw - cyaw, &s, &c);
  /* each corner in place once, the first time a face it is on is drawn, in the camera's space and on the screen (a
   * far piece draws only some of its faces: the others' corners are left alone); a mesh's corners past MESH_CORNERS
   * (the temple's) are moved for each face they are on */
  static SV v[MESH_CORNERS];
  static uint16_t made[MESH_CORNERS], stamp;   /* (v[j] is this mesh's when made[j] is stamp) */
  if (++stamp == 0) {
    memset(made, 0, sizeof made);
    stamp = 1;
  }
  /* (a quarter unit turned, in metres: one multiply a coordinate) */
  float kc = 0.25f * S * c, ks = 0.25f * S * s, ky = 0.25f * S, bx = x0 * S, by = dy * S, bz = z0 * S;
#define MOVE(j) r_corner((V3){bx + me->v[j].x * kc + me->v[j].z * ks, by + me->v[j].y * ky, bz - me->v[j].x * ks + me->v[j].z * kc})
#define CORNER(j) ((j) >= MESH_CORNERS ? MOVE(j) : made[j] == stamp ? v[j] : (made[j] = stamp, v[j] = MOVE(j)))
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
  /* (a path piece has its faces biggest first: past LOD_NEAR, only the first of them, fewer as it is farther, so that
   * coming nearer it gains them one by one, the biggest first; by its distance, not by the way the camera looks, so a
   * turn of the camera changes none. Scenery stays whole, as its leaves and roots would be left in the air) */
  int nt = me->nt;
  if (m < 2 * M_PIECES) {
    float d2 = dx * dx + dz * dz;
    if (d2 > LOD_NEAR * LOD_NEAR) nt = (int)((float)nt * (LOD_NEAR * LOD_NEAR) / d2);   /* (fewer as the square of the distance) */
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
#undef MOVE
}
#endif

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

static float scenery_far = TREE_FAR;   /* how far the scenery is drawn: it moves slowly, with how busy frames are */

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
  /* nearest first, ring by ring round the camera's square, as far as scenery_far: a piece of scenery is there or not by
   * where it is, never by how many polygons came before it, so none comes and goes from one frame to the next. Past
   * SCENERY_POLYS a frame, scenery_far comes a little nearer each frame, and goes back out when there is room (the
   * intro's reach too, when it is over); what comes within it comes in from the fog */
  if (intro_t < INTRO && !title && scenery_far > INTRO_FAR) scenery_far = INTRO_FAR;
  for (int ring = 0; ring <= SGRID / 2; ring++)
    for (int a = SGRID / 2 - ring; a <= SGRID / 2 + ring; a++)
      for (int b = SGRID / 2 - ring; b <= SGRID / 2 + ring; b += (a == SGRID / 2 - ring || a == SGRID / 2 + ring) ? 1 : 2 * ring) {
        int cx = x0 + a, cz = z0 + b;
        if (mask[a][b] || (cx >= -1 && cx <= 1 && cz >= 0 && cz <= 4)) continue;
        uint32_t h = mix((uint32_t)cx * 0x9E3779B1u ^ mix((uint32_t)cz + scenery_seed));
        if (h % 1000 >= 250) continue;   /* (ObjectDensity 0.25) */
        uint32_t k = (h >> 10) % 21, v = h >> 15;
        int m = k <= 10 ? trees[v % 4] : k <= 14 ? rocks[v % 3] : k <= 16 ? cliffs[v % 3] : k <= 18 ? M_overgrowthTrees : k == 19 ? M_templeHead : M_groundPatchTemple;
        float ox = (float)((int)((h >> 20) % 21) - 10), oz = (float)((int)((h >> 25) % 21) - 10);
        float wx = (float)cx * TILE + ox, wz = (float)cz * TILE + oz, dx = wx - cx_, dz = wz - cz_, d2 = dx * dx + dz * dz;
        /* (the small ones, a few pixels in the haze past SMALL_FAR, aren't drawn there) */
        float reach = ((k >= 11 && k <= 14) || k >= 19) && scenery_far > SMALL_FAR ? SMALL_FAR : scenery_far;
        if (d2 > reach * reach) continue;
        /* (coming within reach, it comes in from the fog: the last FADE units it is that much in the fog's colour) */
        float in = reach - FADE;
        r_fade = d2 > in * in ? (int)((__builtin_sqrtf(d2) - in) * (256 / FADE)) : 0;
        scene_mesh(m, wx, 0, wz, 0, 255);
        r_fade = 0;
      }
  int n = r_count();
  if (n > SCENERY_POLYS && scenery_far > 200) scenery_far -= 3;
  else if (n < SCENERY_POLYS - 80 && scenery_far < TREE_FAR) scenery_far += 1.5f;
}

/* ---------------------------------------------------------------- the sky and the depths */
/* the jungle far below (the game's Water Plane), where the row looks down at it: its picture fixed on the ground, its
 * depth there (r3d.c draws it where nothing lies nearer, fogged; the fog's colour above) */
static void ground(int y, RGround *g) {
  const Texture *t = r_tex[2];
  float hgt = (WATER_Y - cy_) * S, k = (float)(1 << t->wl) / (WATER_TILE * S);   /* (texels a metre) */
  float vs = ((float)y + 0.5f - cam.cy) / cam.f;
  float ry = -vs * cam.cp - cam.sp, rz = -vs * cam.sp + cam.cp;   /* (a ray down the row's middle, z ahead 1) */
  if (ry >= -1e-4f) return;
  float tz = hgt / ry;                                            /* its depth where it meets the jungle */
  if (tz > r_fog1) return;
  /* along the row: the ground's place at x, straight lines in the picture */
  float ax = (0.5f - cam.cx) / cam.f * tz, az = rz * tz, sx = tz / cam.f;
  float u0 = ((cx_ * S) + ax * cc + az * cs) * k, v0 = ((cz_ * S) - ax * cs + az * cc) * k;
  g->z = tz;
  g->u = (int32_t)(u0 * 65536.0f), g->v = (int32_t)(v0 * 65536.0f);
  g->du = (int32_t)(sx * cc * k * 65536.0f), g->dv = (int32_t)(-sx * cs * k * 65536.0f);
}

/* ---------------------------------------------------------------- the frame */
void scene_draw(r_band_fn overlay, bool on_title) {
  title = on_title;
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
    cyaw = atan2_(dx, dz) + look_off, pitch = atan2_(cy_ - fy, hd);
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
  r_render(r_tex[2], ground, overlay);
}
