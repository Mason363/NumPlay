/* The people, the dog, the power-ups, coins and boards (actors.h).
 *
 * The people are the game's own skinned models (data.c, from tools/apk.py): each corner follows one bone, or two
 * blended. A rig's bones play the game's animation clips: each frame of a clip turns every bone from its parent
 * (and moves the ones that move); between frames the turns are blended, and a new clip fades in over the one
 * before. Power-ups, keys and boards are the game's models too, on one bone. */
#include "actors.h"
#include "data.h"
#include "scene.h"

/* ---------------------------------------------------------------- 3x4 matrices */
typedef struct {
  float m[12];   /* x' = m0 x + m1 y + m2 z + m3, y' = m4 x ..., z' = m8 x ... */
} M34;

static M34 mul(const M34 *a, const M34 *b) {
  M34 r;
  for (int i = 0; i < 3; i++) {
    const float *A = a->m + 4 * i;
    for (int j = 0; j < 4; j++) r.m[4 * i + j] = A[0] * b->m[j] + A[1] * b->m[4 + j] + A[2] * b->m[8 + j] + (j == 3 ? A[3] : 0);
  }
  return r;
}
static V3 ap(const M34 *a, V3 v) {
  const float *m = a->m;
  return (V3){m[0] * v.x + m[1] * v.y + m[2] * v.z + m[3], m[4] * v.x + m[5] * v.y + m[6] * v.z + m[7],
              m[8] * v.x + m[9] * v.y + m[10] * v.z + m[11]};
}
static float fsin(float a) {
  float s, c;
  r_sincos(a, &s, &c);
  return s;
}
static M34 translate(float x, float y, float z) { return (M34){{1, 0, 0, x, 0, 1, 0, y, 0, 0, 1, z}}; }
static M34 rot_y(float a) {
  float s, c;
  r_sincos(a, &s, &c);
  return (M34){{c, 0, s, 0, 0, 1, 0, 0, -s, 0, c, 0}};
}
static M34 rot_x(float a) {
  float s, c;
  r_sincos(a, &s, &c);
  return (M34){{1, 0, 0, 0, 0, c, -s, 0, 0, s, c, 0}};
}
/* a turn (unit quaternion x, y, z, w) and a place */
static M34 quat_m(const float *q, float tx, float ty, float tz) {
  float x = q[0], y = q[1], z = q[2], w = q[3];
  return (M34){{1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), tx, 2 * (x * y + z * w), 1 - 2 * (x * x + z * z),
                2 * (y * z - x * w), ty, 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y), tz}};
}
/* the camera as a matrix: world to camera space (r_view) */
static M34 view(void) {
  return (M34){{1, 0, 0, -cam.x, 0, cam.cp, cam.sp, -cam.y * cam.cp - cam.z * cam.sp, 0, -cam.sp, cam.cp,
                cam.y * cam.sp - cam.z * cam.cp}};
}

/* ---------------------------------------------------------------- clips */
#define MAXB 24
typedef struct {
  float q[MAXB][4], t[MAXB][3];
} Bones;

static void sample(const Clip *c, float time, Bones *out) {
  int n = c->nframes, nb = c->nbones;
  float f = time * (float)c->fps;
  int i0, i1;
  if (c->loop) {
    f -= (float)(int)(f / (float)n) * (float)n;
    if (f < 0) f += (float)n;
    i0 = (int)f;
    if (i0 >= n) i0 = n - 1;
    i1 = i0 + 1 >= n ? 0 : i0 + 1;
  } else {
    if (f < 0) f = 0;
    if (f > (float)(n - 1)) f = (float)(n - 1);
    i0 = (int)f;
    i1 = i0 + 1 >= n ? n - 1 : i0 + 1;
  }
  float a = f - (float)i0, b = 1 - a;
  const int8_t(*q0)[4] = c->q + i0 * nb, (*q1)[4] = c->q + i1 * nb;
  for (int k = 0; k < nb; k++) {
    float x = q0[k][0] * b + q1[k][0] * a, y = q0[k][1] * b + q1[k][1] * a, z = q0[k][2] * b + q1[k][2] * a, w = q0[k][3] * b + q1[k][3] * a;
    float l = x * x + y * y + z * z + w * w;
    float r = l > 1e-6f ? 1.0f / __builtin_sqrtf(l) : 0;
    out->q[k][0] = x * r, out->q[k][1] = y * r, out->q[k][2] = z * r, out->q[k][3] = w * r;
    out->t[k][0] = c->t[k][0] * 0.001f, out->t[k][1] = c->t[k][1] * 0.001f, out->t[k][2] = c->t[k][2] * 0.001f;
  }
  for (int m = 0; m < c->nmove; m++) {
    int k = c->move[m];
    const int16_t *t0 = c->mt[i0 * c->nmove + m], *t1 = c->mt[i1 * c->nmove + m];
    for (int j = 0; j < 3; j++) out->t[k][j] = (t0[j] * b + t1[j] * a) * 0.001f;
  }
}

float anim_length(const Clip *c) { return (float)(c->loop ? c->nframes : c->nframes - 1) / (float)c->fps; }

void anim_play(Anim *an, const Clip *c, float t, float fade) {
  if (an->a == c) return;
  if (an->a && fade > 0) {
    an->b = an->a, an->tb = an->ta;
    an->fade = 0, an->rate = 1 / fade;
  } else an->b = NULL, an->fade = 1;
  an->a = c, an->ta = t;
}

void anim_step(Anim *an, float dt) {
  an->ta += dt;
  an->tb += dt;
  if (an->fade < 1) {
    an->fade += dt * an->rate;
    if (an->fade >= 1) an->fade = 1, an->b = NULL;
  }
}

bool anim_done(const Anim *an) { return an->a && !an->a->loop && an->ta >= anim_length(an->a); }

static void pose(const Anim *an, Bones *out) {
  sample(an->a, an->ta, out);
  if (!an->b || an->fade >= 1) return;
  Bones o;
  sample(an->b, an->tb, &o);
  float a = an->fade, b = 1 - a;
  a = a * a * (3 - 2 * a), b = 1 - a;
  for (int k = 0; k < an->a->nbones; k++) {
    float *q = out->q[k], *p = o.q[k];
    float s = q[0] * p[0] + q[1] * p[1] + q[2] * p[2] + q[3] * p[3] < 0 ? -b : b;
    float x = q[0] * a + p[0] * s, y = q[1] * a + p[1] * s, z = q[2] * a + p[2] * s, w = q[3] * a + p[3] * s;
    float l = x * x + y * y + z * z + w * w, r = l > 1e-6f ? 1.0f / __builtin_sqrtf(l) : 0;
    q[0] = x * r, q[1] = y * r, q[2] = z * r, q[3] = w * r;
    for (int j = 0; j < 3; j++) out->t[k][j] = out->t[k][j] * a + o.t[k][j] * b;
  }
}

/* ---------------------------------------------------------------- drawing a model */
static M34 bone_w[MAXB];   /* the bones of the model just drawn, in the world */
static const Model *drawn;
static float drawn_yaw;   /* (its turn) */

/* where the light comes from (in the world): above, to the right, from the camera's side */
static const V3 sun = {0.35f, 0.8f, -0.45f};

/* each skin bone's matrix (world) given each rig bone's; then the corners */
static void draw_skin(const Model *m, const M34 *bw, float light) {
  M34 cm[MAXB], vw = view();
  V3 lb[MAXB];
  for (int i = 0; i < m->nbones; i++) {
    const float *ib = m->ibind[i];
    M34 inv = {{ib[0], ib[1], ib[2], ib[3], ib[4], ib[5], ib[6], ib[7], ib[8], ib[9], ib[10], ib[11]}};
    M34 s = mul(&bw[m->bone[i]], &inv);
    cm[i] = mul(&vw, &s);
    /* the light in the rest pose's frame (normals are kept at rest): the turn's transpose */
    const float *w = s.m;
    lb[i] = (V3){(w[0] * sun.x + w[4] * sun.y + w[8] * sun.z) * (1 / 127.0f), (w[1] * sun.x + w[5] * sun.y + w[9] * sun.z) * (1 / 127.0f),
                 (w[2] * sun.x + w[6] * sun.y + w[10] * sun.z) * (1 / 127.0f)};
  }
  /* (nothing to do far away or behind) */
  V3 c = ap(&cm[0], (V3){0, 0.8f, 0});
  if (c.z < -3.0f || c.z > 150) return;
  float amb = 0.62f * light * 255, dif = 0.45f * light * 255;
  RV rv[256];
  for (int pi = 0; pi < m->nparts; pi++) {
    const MPart *p = &m->parts[pi];
    const MV *q = m->v + p->v0;
    for (int k = 0; k < p->nv; k++, q++) {
      V3 r = {q->x * 0.001f, q->y * 0.001f, q->z * 0.001f};
      V3 v = ap(&cm[q->b0], r);
      V3 L = lb[q->b0];
      float d = q->nx * L.x + q->ny * L.y + q->nz * L.z;
      if (q->w) {
        float t = q->w * (1 / 256.0f);
        V3 v2 = ap(&cm[q->b1], r);
        V3 L2 = lb[q->b1];
        float d2 = q->nx * L2.x + q->ny * L2.y + q->nz * L2.z;
        v.x += (v2.x - v.x) * t, v.y += (v2.y - v.y) * t, v.z += (v2.z - v.z) * t;
        d += (d2 - d) * t;
      }
      float s = amb + dif * (d > 0 ? d : 0);
      rv[k] = (RV){v.x, v.y, v.z, q->u, q->v, (uint8_t)(s > 255 ? 255 : s), {0, 0, 0}};
    }
    r_mesh(rv, p->nv, m->t + p->t0, p->nt, m->pal, p->tex);
  }
}

/* a rig's bones in the world: root placed, then each from its parent */
static void place_bones(const Anim *an, const M34 *root) {
  Bones b;
  pose(an, &b);
  const Clip *c = an->a;
  for (int k = 0; k < c->nbones; k++) {
    M34 local = quat_m(b.q[k], b.t[k][0], b.t[k][1], b.t[k][2]);
    bone_w[k] = c->parent[k] < 0 ? mul(root, &local) : mul(&bone_w[(int)c->parent[k]], &local);
  }
}

static const Model *const people[6] = {&model_jake, &model_tricky, &model_fresh, &model_spike, &model_inspector, &model_dog};

void actor_draw(int who, const Anim *an, V3 at, float yaw, float lean, float light) {
  if (!an->a) return;
  const Model *m = people[who];
  M34 root = translate(at.x, at.y, at.z), ry = rot_y(yaw);
  root = mul(&root, &ry);
  if (lean != 0) {
    /* forward about the hips */
    M34 up = translate(0, 0.6f, 0), rx = rot_x(lean), down = translate(0, -0.6f, 0);
    root = mul(&root, &up);
    root = mul(&root, &rx);
    root = mul(&root, &down);
  }
  place_bones(an, &root);
  drawn = m, drawn_yaw = yaw;
  draw_skin(m, bone_w, light);
}

/* a model on one bone (power-ups, the board, the jetpack) */
static void draw_rigid(const Model *m, const M34 *w, float light) {
  M34 b[1] = {*w};
  draw_skin(m, b, light);
}

/* ---------------------------------------------------------------- boards, the jetpack, shadows */
void board_draw(V3 p, int kind, float yaw, float t, float lt) {
  (void)kind;
  M34 w = translate(p.x, p.y + 0.12f + 0.03f * fsin(t * 6), p.z), ry = rot_y(yaw);
  w = mul(&w, &ry);
  draw_rigid(&model_p_board, &w, lt);
  /* the glow under it */
  V3 g[4] = {{p.x - 0.45f, p.y + 0.03f, p.z + 0.85f}, {p.x + 0.45f, p.y + 0.03f, p.z + 0.85f},
             {p.x + 0.45f, p.y + 0.03f, p.z - 0.85f}, {p.x - 0.45f, p.y + 0.03f, p.z - 0.85f}};
  r_poly(g, 4, 0x7FE8FF, P_ALPHA | P_NOHAZE, 80);
}

void jetpack_draw(float t, float lt) {
  if (drawn == &model_dog || drawn == &model_inspector) return;
  /* on the back: where the spine bone is, upright with the body (its bones' own axes point anywhere), behind it;
   * the pick-up's model a size smaller */
  const float *sp = bone_w[RB_SPINE].m;
  M34 w = translate(sp[3], sp[7] + 0.1f, sp[11] - 0.2f), ry = rot_y(drawn_yaw), tilt = rot_x(0.35f);
  w = mul(&w, &ry);
  w = mul(&w, &tilt);
  for (int i = 0; i < 3; i++) w.m[i] *= 0.6f, w.m[4 + i] *= 0.6f, w.m[8 + i] *= 0.6f;
  draw_rigid(&model_p_jetpack, &w, lt);
  /* the flames */
  for (int s = -1; s <= 1; s += 2) {
    V3 n = ap(&w, (V3){0.2f * (float)s, -0.36f, 0});
    float len = 0.3f + 0.1f * fsin(t * 37 + (float)s);
    V3 f[3] = {{n.x - 0.05f, n.y, n.z}, {n.x + 0.05f, n.y, n.z}, {n.x, n.y - len, n.z - 0.1f}};
    r_poly(f, 3, 0xFF9A1A, P_ALPHA | P_NOHAZE | P_TWOSIDED, 220);
    V3 f2[3] = {{n.x - 0.025f, n.y, n.z - 0.01f}, {n.x + 0.025f, n.y, n.z - 0.01f}, {n.x, n.y - len * 0.6f, n.z - 0.08f}};
    r_poly(f2, 3, 0xFFF4B0, P_ALPHA | P_NOHAZE | P_TWOSIDED, 230);
  }
}

void shadow_draw(float x, float z, float ground, float h) {
  float k = 1 - h * 0.12f;
  if (k < 0.3f) k = 0.3f;
  static const float o[8][2] = {{0, 1}, {0.71f, 0.71f}, {1, 0}, {0.71f, -0.71f}, {0, -1}, {-0.71f, -0.71f}, {-1, 0}, {-0.71f, 0.71f}};
  V3 v[8];
  for (int i = 0; i < 8; i++) v[i] = (V3){x + o[i][0] * 0.36f * k, ground + 0.02f, z + o[i][1] * 0.34f * k};
  r_poly(v, 8, 0x000000, P_ALPHA | P_NOHAZE, (int)(110 * k));
}

/* ---------------------------------------------------------------- the things to pick up */
void actors_pickup(const Item *it, float t) {
  float lt = scene_light(it->z);
  float bob = 0.1f * fsin(t * 3 + it->z);
  const Model *m;
  float turn = t * 2.4f + it->z, size = 1;
  switch (it->kind) {
    case I_MAGNET: m = &model_p_magnet; break;
    case I_JETPACK: m = &model_p_jetpack; break;
    case I_SNEAKERS: m = &model_p_sneakers; break;
    case I_DOUBLE: m = &model_p_double; break;
    case I_MYSTERY: m = &model_p_mystery, turn *= 0.5f, size = 0.85f; break;
    default: m = &model_p_key; break;
  }
  M34 w = translate(it->x, it->y + bob, it->z), ry = rot_y(turn);
  w = mul(&w, &ry);
  if (size != 1) {
    for (int i = 0; i < 3; i++) w.m[i] *= size, w.m[4 + i] *= size, w.m[8 + i] *= size;
  }
  draw_rigid(m, &w, lt);
  /* a soft glow on the ground under it */
  float gy = world_ground(it->x, it->z, it->y) + 0.02f;
  V3 g[4] = {{it->x - 0.55f, gy, it->z + 0.55f}, {it->x + 0.55f, gy, it->z + 0.55f}, {it->x + 0.55f, gy, it->z - 0.55f}, {it->x - 0.55f, gy, it->z - 0.55f}};
  r_poly(g, 4, it->kind == I_KEY ? 0x7FD0FF : 0xFFE680, P_ALPHA, 60);
}

/* a coin: the game's gold coin, turning (as wide as the cosine of its turn); far away a gold disc */
void actors_coin(const Item *it, float t, float lt) {
  float s, c, r = 0.43f;
  r_sincos(t * 4 + it->z * 0.35f, &s, &c);
  float d = it->z - cam.z;
  if (d < 40) {
    float hw = r * (c < 0 ? -c : c);
    if (hw < 0.06f) hw = 0.06f;
    V3 q[4] = {{it->x - hw, it->y + r, it->z}, {it->x + hw, it->y + r, it->z}, {it->x + hw, it->y - r, it->z}, {it->x - hw, it->y - r, it->z}};
    float uv[8] = {0, 0, 64, 0, 64, 64, 0, 64};
    if (c < 0) uv[0] = uv[6] = 64, uv[2] = uv[4] = 0;
    r_poly_tex(q, uv, 4, TX_COIN, (int)(256 * lt), P_TWOSIDED | P_NOHAZE);
    return;
  }
  static const float k[8][2] = {{0, 1}, {0.707f, 0.707f}, {1, 0}, {0.707f, -0.707f}, {0, -1}, {-0.707f, -0.707f}, {-1, 0}, {-0.707f, 0.707f}};
  V3 v[8];
  uint32_t gold = scene_lit(0xF7B21E, it->z);
  for (int i = 0; i < 8; i++) v[i] = (V3){it->x + c * k[i][0] * r * 0.85f, it->y + k[i][1] * r * 0.85f, it->z + s * k[i][0] * r};
  r_poly(v, 8, gold, P_TWOSIDED, 0);
}
