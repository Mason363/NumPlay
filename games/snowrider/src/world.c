/* Snow Rider 3D: the world, made as the game's GGen makes it, and asked as Unity's physics is asked.
 *
 * GGen makes the slope a structure at a time, 10 m long and four chunks of 5 m wide (the last one a mirror of a
 * side chunk). A structure belongs to a biome (the warm-up, the forest, the plains) for a random number of
 * structures, is chosen among the biome's by probability (never the same twice in a row, some only past a
 * score), lasts a random number of structures, and its chunks are random among its own. Modifiers change a
 * structure's chunks (a tunnel, a ramp before a crack) and choose the next one (the crack, its end, the floating
 * islands), or roll snowballs. A gift lies in one chunk column that wanders left and right. */
#include "sr.h"

Inst insts[MAXI];
int ninsts;
Inst camp[MAXC];
int ncamp;
bool camp_on;
Ball balls[MAXB];
Inst ball_inst[MAXB];
uint16_t ball_prefab;

/* the structures alive: the place of each one's instances in insts */
#define NSLOTS 9
static struct {
  int index, first, n;
  bool used;
  float spawner[3], end_lo[3], end_hi[3];   /* the snowballs' spawner, the box where they go (SnowballDestroy) */
  bool has_spawner, has_end;
  int balls_dir;
  float ball_timer;
  const Balls *bp;
} slots[NSLOTS];

/* GGen's state (giftChunk, currentStruct and currentStructLength stay from a run to the next, as there) */
static int gift_chunk = -1, cur_biome = -1, biome_len, cur_struct, struct_len;
int struct_index;
float generate_distance;
static int chunk_data[STRUCT_W];
static int slot_now;

static void bounds(Inst *in) {
  const Prefab *pf = &prefabs[in->prefab];
  for (int k = 0; k < 3; k++) in->lo[k] = 1e9f, in->hi[k] = -1e9f;
  float corner[3], w[3];
  for (int i = 0; i < pf->nparts; i++) {
    const Part *pt = &pf->parts[i];
    const RMesh *rm = &rmeshes[pt->near];
    float m[12];
    m_mul(m, in->m, pt->m);
    m_point(m, rm->ctr, w);
    float sc = fabsf(m_det(m));
    float r = rm->rad * cbrtf(sc > 1e-9f ? sc : 1) * 1.05f;
    for (int k = 0; k < 3; k++) in->lo[k] = fminf(in->lo[k], w[k] - r), in->hi[k] = fmaxf(in->hi[k], w[k] + r);
  }
  for (int i = 0; i < pf->ncolls; i++) {
    const Coll *c = &pf->colls[i];
    float m[12];
    m_mul(m, in->m, c->m);
    float lo[3] = {-1, -1, -1}, hi[3] = {1, 1, 1};
    if (c->kind == C_MESH) {
      const CMesh *cm = &cmeshes[COLL_MESH(c)];
      for (int k = 0; k < 3; k++) lo[k] = cm->lo[k], hi[k] = cm->hi[k];
    } else if (c->kind == C_CAPSULE) {
      float e = c->h / 2 > c->r ? c->h / 2 : c->r;
      for (int k = 0; k < 3; k++) lo[k] = -(k == c->dir_or_mesh ? e : c->r), hi[k] = -lo[k];
    }
    for (int n = 0; n < 8; n++) {
      corner[0] = n & 1 ? hi[0] : lo[0], corner[1] = n & 2 ? hi[1] : lo[1], corner[2] = n & 4 ? hi[2] : lo[2];
      m_point(m, corner, w);
      for (int k = 0; k < 3; k++) in->lo[k] = fminf(in->lo[k], w[k]), in->hi[k] = fmaxf(in->hi[k], w[k]);
    }
  }
}

static Inst *place(Inst *in, int prefab, const float m[12]) {
  for (int i = 0; i < 12; i++) in->m[i] = m[i];
  in->prefab = (uint16_t)prefab, in->alive = 1;
  bounds(in);
  return in;
}

static void drop_balls(int s) {
  for (int b = 0; b < MAXB; b++)
    if (balls[b].slot == s) balls[b].alive = false, ball_inst[b].alive = 0;
  slots[s].balls_dir = 0;
}

static void drop_slot(int s) {
  slots[s].used = false;
  for (int i = slots[s].first; i < slots[s].first + slots[s].n; i++) insts[i % MAXI].alive = 0;
  drop_balls(s);
}

void world_clear(void) {
  for (int s = 0; s < NSLOTS; s++) slots[s].used = false;
  for (int i = 0; i < MAXI; i++) insts[i].alive = 0;
  for (int b = 0; b < MAXB; b++) balls[b].alive = false, ball_inst[b].alive = 0;
  ninsts = 0;
}

void world_camp(bool on) {
  camp_on = on;
  if (!on || ncamp) return;
  const Chunk *ch = &chunks[CAMP_CHUNK];
  for (int i = 0; i < ch->n && ncamp < MAXC; i++) {
    const Obj *o = &ch->obj[i];
    if (!prefabs[o->prefab].nparts && !prefabs[o->prefab].ncolls) continue;
    float m[12];
    m_trs(m, o->pos, (Quat){o->rot[0], o->rot[1], o->rot[2], o->rot[3]}, o->scale);
    place(&camp[ncamp++], o->prefab, m);
  }
}

/* ---------------------------------------------------------------- GGen */
static void choose_biome(void) {
  if (biome_len < 1) {
    int b;
    do b = 1 + rnd_int(0, NBIOMES - 1);
    while (b == cur_biome && NBIOMES - 1 > 1);
    cur_biome = b;
    biome_len = rnd_int(biomes[b].min_len, biomes[b].max_len);
    struct_len = 0;
  }
  biome_len--;
}

static void choose_struct(void) {
  if (struct_len < 1) {
    const Biome *b = &biomes[cur_biome];
    int eligible[16], n = 0;
    float total = 0;
    for (int i = 0; i < b->n; i++) {
      const Struct *s = &structs[b->structs[i]];
      if (s->min_score <= struct_index && struct_index < s->max_score) {
        eligible[n++] = b->structs[i];
        if (b->structs[i] != cur_struct) total += s->prob;
      }
    }
    float choice = rnd_range(0, total);
    for (int i = 0; i < n; i++) {
      if (eligible[i] != cur_struct) choice -= structs[eligible[i]].prob;
      if (choice <= 0) {
        cur_struct = eligible[i];
        struct_len = rnd_int(structs[cur_struct].min_len, structs[cur_struct].max_len);
        break;
      }
    }
  }
  struct_len--;
}

static void choose_chunk(int i) {
  const Struct *s = &structs[cur_struct];
  bool side = i == 0 || i >= STRUCT_W - 1;
  chunk_data[i] = side ? s->side[rnd_int(0, s->nside)] : s->mid[rnd_int(0, s->nmid)];
}

static int snow_dir;
static void before_apply(void) {
  const Struct *s = &structs[cur_struct];
  if (s->mod == MOD_TUNNEL) {
    extern int score;
    if (score < 31) {
      chunk_data[1] = s->list[rnd_int(0, s->nlist)];
      chunk_data[2] = s->list[rnd_int(0, s->nlist)];
    } else {
      int i = rnd_int(1, 3);   /* (the place drawn before the tunnel, as C# does) */
      chunk_data[i] = s->list[rnd_int(0, s->nlist)];
    }
  } else if (s->mod == MOD_CRACKSTART) {
    int i = rnd_int(0, STRUCT_W);
    chunk_data[i] = i < 1 || i >= STRUCT_W - 1 ? s->b : s->a;
  } else if (s->mod == MOD_SNOWBALLS) {
    if (rnd_range(0, 1) > 0.5f) chunk_data[0] = s->a, chunk_data[STRUCT_W - 1] = s->b, snow_dir = 1;
    else chunk_data[0] = s->b, chunk_data[STRUCT_W - 1] = s->a, snow_dir = -1;
  }
}

static int alloc_inst(void) {
  /* the ring is full: the structure holding its oldest instance goes */
  int idx = ninsts % MAXI, old = ninsts - MAXI;
  for (int k = 0; k < NSLOTS && old >= 0; k++)
    if (slots[k].used && k != slot_now && slots[k].first <= old && old < slots[k].first + slots[k].n) drop_slot(k);
  ninsts++;
  return idx;
}

static void generate_chunk(int j, int chunk_index, float zs) {
  bool mirrored = j >= STRUCT_W - 1;
  float cm[12];
  float ct[3] = {chunk_index * CHUNK_X + (mirrored ? 5 : 0), 0, zs}, cs[3] = {mirrored ? -1.0f : 1.0f, 1, 1};
  m_trs(cm, ct, (Quat){0, 0, 0, 1}, cs);
  const Chunk *ch = &chunks[chunk_data[j]];
  for (int i = 0; i < ch->n; i++) {
    const Obj *o = &ch->obj[i];
    int pre = o->prefab;
    if (prefabs[pre].flags & PF_GIFT && (chunk_index != gift_chunk || struct_index < 5)) continue;
    Quat rr = {0, 0, 0, 1};
    int r = prefabs[pre].rand;
    if (r >= 0) {
      const Rand *rd = &rands[r];
      float ex = rnd_range(rd->lo[0], rd->hi[0]), ey = rnd_range(rd->lo[1], rd->hi[1]), ez = rnd_range(rd->lo[2], rd->hi[2]);
      rr = q_euler(ex, ey, ez);
      (void)rnd_range(0.9f, 1.2f);
      if (rd->nalt) pre = rd->alt[rnd_int(0, rd->nalt)];
    }
    float om[12], m[12];
    m_trs(om, o->pos, q_mul((Quat){o->rot[0], o->rot[1], o->rot[2], o->rot[3]}, rr), o->scale);
    m_mul(m, cm, om);
    if (!prefabs[pre].nparts && !prefabs[pre].ncolls && !(prefabs[pre].flags & PF_SPAWNER)) continue;
    int idx = alloc_inst();
    place(&insts[idx], pre, m);
    insts[idx].slot = (uint8_t)slot_now;
    slots[slot_now].n++;
    if (prefabs[pre].flags & PF_SPAWNER) {
      m_point(m, prefabs[pre].spawner, slots[slot_now].spawner);
      slots[slot_now].has_spawner = true;
    }
    if (prefabs[pre].flags & PF_BALL_END) {
      for (int k = 0; k < 3; k++) slots[slot_now].end_lo[k] = insts[idx].lo[k], slots[slot_now].end_hi[k] = insts[idx].hi[k];
      slots[slot_now].has_end = true;
    }
  }
}

/* SnowBallSpawner.SpawnBall: at rest, dx along the way it rolls from the spawner, its size random (SnowBall.Start) */
static void spawn_ball(int s, float dx) {
  int b = 0;
  while (b < MAXB && balls[b].alive) b++;
  if (b == MAXB) return;
  Ball *bl = &balls[b];
  bl->alive = true, bl->ground = false, bl->slot = (uint8_t)s, bl->dir = (int8_t)slots[s].balls_dir;
  bl->x = slots[s].spawner[0] + dx * bl->dir, bl->y = slots[s].spawner[1], bl->z = slots[s].spawner[2];
  bl->r = rnd_range(slots[s].bp->min_size, slots[s].bp->max_size) * prefabs[slots[s].bp->prefab].colls[0].m[0];
  bl->vx = bl->vy = bl->k = bl->spin = 0;
}

static void generate_struct(void) {
  choose_biome();
  int s = 0;
  while (s < NSLOTS && slots[s].used) s++;
  if (s == NSLOTS) {   /* the oldest goes */
    int old = 0;
    for (int k = 1; k < NSLOTS; k++)
      if (slots[k].index < slots[old].index) old = k;
    drop_slot(old);
    s = old;
  }
  slot_now = s;
  slots[s].used = true, slots[s].index = struct_index, slots[s].first = ninsts, slots[s].n = 0, slots[s].has_spawner = false, slots[s].has_end = false, slots[s].balls_dir = 0;
  choose_struct();
  for (int i = 0; i < STRUCT_W; i++) choose_chunk(i);
  const Struct *st = &structs[cur_struct];
  if (st->mod) before_apply();
  float zs = struct_index * CHUNK_Z;
  for (int i = 0; i < STRUCT_W; i++) generate_chunk(i, i, zs);
  if (rnd_range(0, 1) < 0.5f) {
    if (gift_chunk < STRUCT_W - 1) gift_chunk++;
    else if (rnd_range(0, 1) > 0.75f) gift_chunk--;
  } else if (gift_chunk >= 1) gift_chunk--;
  else if (rnd_range(0, 1) > 0.75f) gift_chunk++;
  struct_index++;
  generate_distance += CHUNK_Z;
  if (st->mod && struct_len < 1) {
    /* AfterApply */
    if (st->mod == MOD_SNOWBALLS && slots[s].has_spawner) {
      const Balls *b = &st->balls;
      ball_prefab = b->prefab;
      slots[s].balls_dir = snow_dir, slots[s].bp = b;
      slots[s].ball_timer = b->interval * rnd_range(1 - b->randomness, 1 + b->randomness);
      for (float d = 0; d < STRUCT_W * CHUNK_X; d += b->speed * b->interval) spawn_ball(s, d);
    } else if (st->next >= 0)
      cur_struct = st->next;
    struct_len = rnd_int(structs[cur_struct].min_len, structs[cur_struct].max_len);
    biome_len += struct_len;
  }
}

void world_start(void) {
  world_clear();
  if (gift_chunk < 0) gift_chunk = rnd_int(0, STRUCT_W - 1);   /* (GGen.Awake: once) */
  struct_index = 0;
  generate_distance = 10;
  cur_biome = 0;
  cur_struct = biomes[0].structs[0];
  biome_len = WARMUP_LEN;
  for (int i = 0; i < VISIBLE_STRUCTS; i++) generate_struct();
  generate_distance -= VISIBLE_STRUCTS * CHUNK_Z;
}

bool world_update(float target_z) {
  if (target_z > generate_distance) {
    generate_struct();
    return true;
  }
  return false;
}

/* the snowballs: Snowball Big's rigidbody (its z held), falling with Unity's gravity, pushed sideways while slower
 * than 5 m/s (SnowBall.Update: a force for the frame, so force * dt * fixedDeltaTime a step), rolling on the ground
 * (a ball rolling gets 5/7 of a push or of the slope's pull, and keeps 5/7 of its speed when it lands), until it
 * meets its structure's SnowballDestroy */
void world_balls(float dt) {
  for (int s = 0; s < NSLOTS; s++)
    if (slots[s].used && slots[s].balls_dir) {
      /* (behind the structures kept in sight, before the camera's: its balls are not seen again) */
      if (slots[s].index < struct_index - VISIBLE_STRUCTS - 1) {
        drop_balls(s);
        continue;
      }
      const Balls *bp = slots[s].bp;
      slots[s].ball_timer -= dt;
      if (slots[s].ball_timer <= 0) {
        slots[s].ball_timer += bp->interval * rnd_range(1 - bp->randomness, 1 + bp->randomness);
        spawn_ball(s, 0);
      }
    }
  float fdt = 0.02f * dt / DT_REF;   /* (SlowMotion: Time.fixedDeltaTime is 0.02 s times the time scale) */
  for (int b = 0; b < MAXB; b++) {
    Ball *bl = &balls[b];
    if (!bl->alive) continue;
    const Balls *bp = slots[bl->slot].bp;
    float k = bl->k, sp = bl->ground ? fabsf(bl->vx) * sqrtf(1 + k * k) : sqrtf(bl->vx * bl->vx + bl->vy * bl->vy);
    float push = sp < 5 ? bp->force * dt * fdt * bl->dir : 0;
    if (bl->ground) bl->vx += (push - PHYS_GRAVITY * k / (1 + k * k) * dt) * 5 / 7;
    else bl->vx += push, bl->vy -= PHYS_GRAVITY * dt;
    float x0 = bl->x;
    bl->x += bl->vx * dt;
    /* the ground under it */
    float o[3] = {bl->x, bl->y + bl->r, bl->z}, d[3] = {0, -1, 0}, h = -1e9f;
    Hit hit;
    if (ray_cast(o, d, 10, L_SURFACE, &hit)) h = hit.p[1];
    if (bl->ground && h > bl->y - bl->r - 0.3f) {
      if (bl->x != x0) bl->k = (h + bl->r - bl->y) / (bl->x - x0);
      bl->y = h + bl->r;
    } else {
      if (bl->ground) bl->ground = false, bl->vy = bl->vx * k;
      bl->y += bl->vy * dt;
      if (bl->y - bl->r <= h) bl->y = h + bl->r, bl->vy = 0, bl->vx *= 5.0f / 7, bl->k = 0, bl->ground = true;
    }
    bl->spin += (bl->x - x0) / bl->r;
    /* SnowBall.OnCollisionEnter: SnowballDestroy (or fallen off the world) */
    const float *lo = slots[bl->slot].end_lo, *hi = slots[bl->slot].end_hi;
    float r = bl->r;
    if ((slots[bl->slot].has_end && bl->x > lo[0] - r && bl->x < hi[0] + r && bl->y > lo[1] - r && bl->y < hi[1] + r &&
         bl->z > lo[2] - r && bl->z < hi[2] + r) || bl->y < -20 || fabsf(bl->x - STRUCT_W * CHUNK_X / 2) > 60) {
      bl->alive = false, ball_inst[b].alive = 0;
      continue;
    }
    float sc[3] = {r / prefabs[ball_prefab].colls[0].m[0], 0, 0}, t[3] = {bl->x, bl->y, bl->z}, m[12];
    sc[1] = sc[2] = sc[0];
    m_trs(m, t, q_euler(0, 0, -bl->spin / DEG), sc);
    place(&ball_inst[b], ball_prefab, m);
  }
}

/* ---------------------------------------------------------------- Physics.Raycast */
static bool slab(const float o[3], const float d[3], const float lo[3], const float hi[3], float len, float *t0, float *t1) {
  float a = 0, b = len;
  for (int k = 0; k < 3; k++) {
    if (d[k] > -1e-9f && d[k] < 1e-9f) {
      if (o[k] < lo[k] || o[k] > hi[k]) return false;
      continue;
    }
    float inv = 1 / d[k], u = (lo[k] - o[k]) * inv, v = (hi[k] - o[k]) * inv;
    if (u > v) { float t = u; u = v, v = t; }
    if (u > a) a = u;
    if (v < b) b = v;
    if (a > b) return false;
  }
  *t0 = a, *t1 = b;
  return true;
}

static bool hit_sphere(const float o[3], const float d[3], const float c[3], float r, float len, float *t) {
  float oc[3] = {o[0] - c[0], o[1] - c[1], o[2] - c[2]};
  float a = d[0] * d[0] + d[1] * d[1] + d[2] * d[2], b = oc[0] * d[0] + oc[1] * d[1] + oc[2] * d[2];
  float cc = oc[0] * oc[0] + oc[1] * oc[1] + oc[2] * oc[2] - r * r;
  if (cc <= 0) return false;   /* inside */
  float disc = b * b - a * cc;
  if (disc < 0) return false;
  float s = (-b - sqrtf(disc)) / a;
  if (s < 0 || s > len) return false;
  *t = s;
  return true;
}

/* a ray in the collider's own space (d not a unit: t is the world's) */
static bool hit_coll(const Coll *c, const float m[12], const float wo[3], const float wd[3], float len, float *t) {
  float inv[12], o[3], d[3];
  if (!m_inv(inv, m)) return false;
  m_point(inv, wo, o);
  m_dir(inv, wd, d);
  if (c->kind == C_BOX) {
    float lo[3] = {-1, -1, -1}, hi[3] = {1, 1, 1}, a, b;
    if (o[0] > -1 && o[0] < 1 && o[1] > -1 && o[1] < 1 && o[2] > -1 && o[2] < 1) return false;
    if (!slab(o, d, lo, hi, len, &a, &b)) return false;
    *t = a;
    return true;
  }
  if (c->kind == C_SPHERE) {
    float z[3] = {0, 0, 0};
    return hit_sphere(o, d, z, 1, len, t);
  }
  if (c->kind == C_CAPSULE) {
    int ax = c->dir_or_mesh;
    float e = c->h / 2 - c->r;
    if (e < 0) e = 0;
    float best = 1e9f, s;
    float p0[3] = {0, 0, 0}, p1[3] = {0, 0, 0};
    p0[ax] = -e, p1[ax] = e;
    /* inside? */
    float q = o[ax] < -e ? -e : o[ax] > e ? e : o[ax], dd = 0;
    for (int k = 0; k < 3; k++) dd += (o[k] - (k == ax ? q : 0)) * (o[k] - (k == ax ? q : 0));
    if (dd <= c->r * c->r) return false;
    if (hit_sphere(o, d, p0, c->r, len, &s) && s < best) best = s;
    if (hit_sphere(o, d, p1, c->r, len, &s) && s < best) best = s;
    /* the side: the ray's distance to the axis */
    int u = (ax + 1) % 3, v = (ax + 2) % 3;
    float a = d[u] * d[u] + d[v] * d[v], b = o[u] * d[u] + o[v] * d[v], cc = o[u] * o[u] + o[v] * o[v] - c->r * c->r;
    if (a > 1e-12f) {
      float disc = b * b - a * cc;
      if (disc >= 0) {
        s = (-b - sqrtf(disc)) / a;
        float y = o[ax] + d[ax] * s;
        if (s >= 0 && s <= len && y >= -e && y <= e && s < best) best = s;
      }
    }
    if (best > len) return false;
    *t = best;
    return true;
  }
  const CMesh *cm = &cmeshes[COLL_MESH(c)];
  float a, b;
  float lo[3] = {cm->lo[0] - 1e-3f, cm->lo[1] - 1e-3f, cm->lo[2] - 1e-3f}, hi[3] = {cm->hi[0] + 1e-3f, cm->hi[1] + 1e-3f, cm->hi[2] + 1e-3f};
  if (!slab(o, d, lo, hi, len, &a, &b)) return false;
  float best = len + 1, sc = cm->s;
  const int16_t *v = cm->v;
  for (int i = 0; i < cm->nt; i++) {
    const int16_t *A = v + corner_of(cm->t, cm->nv, i * 3) * 3, *B = v + corner_of(cm->t, cm->nv, i * 3 + 1) * 3,
                  *C = v + corner_of(cm->t, cm->nv, i * 3 + 2) * 3;
    float ax = A[0] * sc, ay = A[1] * sc, az = A[2] * sc;
    float e1x = B[0] * sc - ax, e1y = B[1] * sc - ay, e1z = B[2] * sc - az;
    float e2x = C[0] * sc - ax, e2y = C[1] * sc - ay, e2z = C[2] * sc - az;
    float px = d[1] * e2z - d[2] * e2y, py = d[2] * e2x - d[0] * e2z, pz = d[0] * e2y - d[1] * e2x;
    float det = e1x * px + e1y * py + e1z * pz;
    /* its front only: det > 0 is a ray against the normal e1 x e2 (a mirrored mesh keeps its outside out) */
    if (det <= 1e-12f) continue;
    float id = 1 / det, tx = o[0] - ax, ty = o[1] - ay, tz = o[2] - az;
    float uu = (tx * px + ty * py + tz * pz) * id;
    if (uu < 0 || uu > 1) continue;
    float qx = ty * e1z - tz * e1y, qy = tz * e1x - tx * e1z, qz = tx * e1y - ty * e1x;
    float vv = (d[0] * qx + d[1] * qy + d[2] * qz) * id;
    if (vv < 0 || uu + vv > 1) continue;
    float s = (e2x * qx + e2y * qy + e2z * qz) * id;
    if (s >= 0 && s < best) best = s;
  }
  if (best > len) return false;
  *t = best;
  return true;
}

static void ray_list(Inst *list, int n, const float o[3], const float d[3], float len, int layers, Hit *hit,
                     const float lo[3], const float hi[3]) {
  for (int i = 0; i < n; i++) {
    Inst *in = &list[i];
    if (!in->alive) continue;
    if (in->hi[0] < lo[0] || in->lo[0] > hi[0] || in->hi[1] < lo[1] || in->lo[1] > hi[1] || in->hi[2] < lo[2] || in->lo[2] > hi[2]) continue;
    const Prefab *pf = &prefabs[in->prefab];
    for (int k = 0; k < pf->ncolls; k++) {
      const Coll *c = &pf->colls[k];
      if (!(c->layer & layers)) continue;
      float m[12], t;
      m_mul(m, in->m, c->m);
      if (hit_coll(c, m, o, d, hit->t, &t) && t < hit->t) {
        hit->t = t, hit->inst = in, hit->coll = c;
      }
    }
  }
}

bool ray_cast(const float o[3], const float d[3], float len, int layers, Hit *hit) {
  float lo[3], hi[3];
  for (int k = 0; k < 3; k++) {
    float e = o[k] + d[k] * len;
    lo[k] = fminf(o[k], e), hi[k] = fmaxf(o[k], e);
  }
  hit->t = len, hit->inst = 0, hit->coll = 0;
  ray_list(insts, MAXI, o, d, len, layers, hit, lo, hi);
  if (camp_on) ray_list(camp, ncamp, o, d, len, layers, hit, lo, hi);
  if (layers & L_OBSTACLE) ray_list(ball_inst, MAXB, o, d, len, layers, hit, lo, hi);
  if (!hit->inst) return false;
  for (int k = 0; k < 3; k++) hit->p[k] = o[k] + d[k] * hit->t;
  return true;
}

/* ---------------------------------------------------------------- touching (the sled's box) */
static bool inside_unit(const float inv[12], const float p[3]) {
  float q[3];
  m_point(inv, p, q);
  return q[0] >= -1 && q[0] <= 1 && q[1] >= -1 && q[1] <= 1 && q[2] >= -1 && q[2] <= 1;
}

static bool boxes_touch(const float a[12], const float b[12]) {
  float ia[12], ib[12], p[3], w[3];
  if (!m_inv(ia, a) || !m_inv(ib, b)) return false;
  for (int n = 0; n < 15; n++) {
    /* corners, the middle and the middles of the faces of each */
    if (n < 8) p[0] = n & 1 ? 1 : -1, p[1] = n & 2 ? 1 : -1, p[2] = n & 4 ? 1 : -1;
    else if (n == 8) p[0] = p[1] = p[2] = 0;
    else p[0] = p[1] = p[2] = 0, p[(n - 9) / 2] = (n & 1) ? 1 : -1;
    m_point(a, p, w);
    if (inside_unit(ib, w)) return true;
    m_point(b, p, w);
    if (inside_unit(ia, w)) return true;
  }
  return false;
}

void box_touch(const float box[12], int layers, void (*f)(Inst *inst, const Coll *c)) {
  float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f}, p[3], w[3];
  for (int n = 0; n < 8; n++) {
    p[0] = n & 1 ? 1 : -1, p[1] = n & 2 ? 1 : -1, p[2] = n & 4 ? 1 : -1;
    m_point(box, p, w);
    for (int k = 0; k < 3; k++) lo[k] = fminf(lo[k], w[k]), hi[k] = fmaxf(hi[k], w[k]);
  }
  for (int i = 0; i < MAXI; i++) {
    Inst *in = &insts[i];
    if (!in->alive) continue;
    if (in->hi[0] < lo[0] || in->lo[0] > hi[0] || in->hi[1] < lo[1] || in->lo[1] > hi[1] || in->hi[2] < lo[2] || in->lo[2] > hi[2]) continue;
    const Prefab *pf = &prefabs[in->prefab];
    for (int k = 0; k < pf->ncolls; k++) {
      const Coll *c = &pf->colls[k];
      if (!(c->layer & layers) || c->kind != C_BOX) continue;
      float m[12];
      m_mul(m, in->m, c->m);
      if (boxes_touch(box, m)) {
        f(in, c);
        break;
      }
    }
  }
}
