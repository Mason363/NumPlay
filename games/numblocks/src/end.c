/* NumBlocks.nwa only: the End's fight, as Minecraft 1.8.8 has it. The Ender Dragon (EntityDragon) flies
 * about the island and at the player, heals from the ender crystals on the pillars, knocks the player back
 * with its wings and bites; killed, it rises turning for ten seconds, gives its experience and leaves the
 * exit portal with the egg on its pillar of bedrock. */
#include <math.h>
#include "nb.h"
#if NB_DIMS
#include "edits.h"

/* the dragon's own state (its place, health and turn are its entity's) */
static struct {
  float yaw;              /* rotationYaw (the entity's yaw is the model's: this one + 180) */
  float tx, ty, tz;       /* where it flies to */
  float yaw_v;            /* randomYawVelocity */
  float anim, prev_anim;  /* the wings' beat */
  int16_t death;          /* deathTicks */
  int8_t heal;            /* the pillar whose crystal heals it, -1 none */
  uint8_t to_player, slowed, force_new;
  uint8_t ring_i;
  int16_t ring[32][2];    /* its last turns (1/100 degree) and heights (1/16): the neck and tail follow them */
} dg;

static inline int ifl(float v) { return (int)floorf(v); }
static float wrap(float a) {
  while (a >= 180) a -= 360;
  while (a < -180) a += 360;
  return a;
}
static float gauss(void) {
  float u = rndf() + 1e-6f, v = rndf();
  return sqrtf(-2 * logf(u)) * cosf(6.2831853f * v);
}
static Entity *dragon(void) {
  for (int i = 0; i < N_ENT; i++)
    if (ents[i].type == E_DRAGON) return &ents[i];
  return NULL;
}
static bool crystal_alive(int i) { return i < 64 && !(dst.crystals_gone[i >> 5] >> (i & 31) & 1); }

/* a block set where it may be out of the cache (its change kept all the same) */
static void put(int x, int y, int z, int b) {
  if (y < 0 || y >= WORLD_H) return;
  if (world_loaded(x, y, z)) world_set(x, y, z, b);
  else if (b != gen_end_block(x, y, z) || edits_changed(x, y, z)) edits_put(x, y, z, b);
}

/* getMovementOffsets(i): its turn and height i ticks ago, eased by the frame's part of the tick */
void dragon_offsets(int i, float pt, float *yaw, float *y) {
  Entity *e = dragon();
  if (e && e->health <= 0) pt = 0;
  pt = 1 - pt;
  int j = (dg.ring_i - i) & 31, k = (dg.ring_i - i - 1) & 31;
  float y0 = dg.ring[j][0] / 100.0f, y1 = dg.ring[k][0] / 100.0f;
  *yaw = y0 + wrap(y1 - y0) * pt;
  float h0 = dg.ring[j][1] / 16.0f, h1 = dg.ring[k][1] / 16.0f;
  *y = h0 + (h1 - h0) * pt;
}
float dragon_anim(float pt) { return dg.prev_anim + (dg.anim - dg.prev_anim) * pt; }
float dragon_dying(void) { return dg.death; }

/* setNewTarget: half the time the player, else somewhere above the island more than 10 away */
static void new_target(Entity *e) {
  dg.force_new = 0;
  if (rnd(2) == 0 && !pl.dead && pl.mode != 3) {
    dg.to_player = 1;
    return;
  }
  float d;
  do {
    dg.tx = 0, dg.ty = 70 + rndf() * 50, dg.tz = 0;
    dg.tx += rndf() * 120 - 60;
    dg.tz += rndf() * 120 - 60;
    float a = e->x - dg.tx, b = e->y - dg.ty, c = e->z - dg.tz;
    d = a * a + b * b + c * c;
  } while (d <= 100);
  dg.to_player = 0;
}

/* destroyBlocksInAABB: what it flies through goes, but obsidian, end stone and bedrock; true if any of
 * those (it slows) */
static bool through(float x0, float y0, float z0, float x1, float y1, float z1) {
  bool hard = false;
  for (int y = ifl(y0); y <= ifl(y1); y++)
    for (int z = ifl(z0); z <= ifl(z1); z++)
      for (int x = ifl(x0); x <= ifl(x1); x++) {
        if (!world_loaded(x, y, z)) continue;
        int b = world_get(x, y, z);
        if (b == B_AIR) continue;
        if (b == B_OBSIDIAN || b == B_END_STONE || b == B_BEDROCK || !rule(GR_MOB_GRIEFING)) hard = true;
        else {
          if (b == B_CHEST || b == B_FURNACE || b == B_FURNACE_LIT) tiles_removed(x, y, z);
          world_set(x, y, z, B_AIR);
        }
      }
  return hard;
}

/* the blocks it can't break in its body (5 wide) or head (3 wide), both 3 high, there (part: their places
 * from the dragon's): obsidian, end stone, bedrock; without mob griefing, anything solid */
static bool hard_in(float x0, float y0, float z0, float x1, float y1, float z1) {
  if (!rule(GR_MOB_GRIEFING)) {
    float a[6] = {x0, y0, z0, x1, y1, z1};
    return !phys_free(a);
  }
  for (int y = ifl(y0); y <= ifl(y1); y++)
    for (int z = ifl(z0); z <= ifl(z1); z++)
      for (int x = ifl(x0); x <= ifl(x1); x++) {
        if (!world_loaded(x, y, z)) continue;
        int b = world_get(x, y, z);
        if (b == B_OBSIDIAN || b == B_END_STONE || b == B_BEDROCK) return true;
      }
  return false;
}
static bool stopped(float x, float y, float z, const float (*part)[3]) {
  float bx = x + part[0][0], bz = z + part[0][2], hx = x + part[1][0], hy = y + part[1][1], hz = z + part[1][2];
  return hard_in(bx - 2.5f, y, bz - 2.5f, bx + 2.5f, y + 3, bz + 2.5f) ||
         hard_in(hx - 1.5f, hy, hz - 1.5f, hx + 1.5f, hy + 3, hz + 1.5f);
}

/* EntityDragon.generatePortal: the exit portal (bedrock round it and under it), the pillar with its
 * four torches and the egg */
static void exit_portal(int px, int py, int pz) {
  for (int j = -1; j <= 32; j++)
    for (int k = -4; k <= 4; k++)
      for (int l = -4; l <= 4; l++) {
        int d2 = k * k + l * l;
        if (d2 > 12) continue;   /* (12.25) */
        int x = px + k, y = py + j, z = pz + l;
        if (j < 0) {
          if (d2 <= 6) put(x, y, z, B_BEDROCK);
        } else if (j > 0)
          put(x, y, z, B_AIR);
        else
          put(x, y, z, d2 > 6 ? B_BEDROCK : B_END_PORTAL);
      }
  put(px, py, pz, B_BEDROCK);
  put(px, py + 1, pz, B_BEDROCK);
  put(px, py + 2, pz, B_BEDROCK);
  put(px - 1, py + 2, pz, B_TORCH_E);
  put(px + 1, py + 2, pz, B_TORCH_W);
  put(px, py + 2, pz - 1, B_TORCH_S);
  put(px, py + 2, pz + 1, B_TORCH_N);
  put(px, py + 3, pz, B_BEDROCK);
  put(px, py + 4, pz, B_DRAGON_EGG);
}

/* onDeathUpdate: ten seconds rising and turning, its light breaking out; experience from 7.5 s on, the
 * portal at the end */
static void dying(Entity *e) {
  dg.death++;
  if (dg.death >= 180 && dg.death <= 200)
    for (int k = 0; k < 3; k++)
      particle_add(e->x + (rndf() - 0.5f) * 8, e->y + 2 + (rndf() - 0.5f) * 4, e->z + (rndf() - 0.5f) * 8, 0, 0.05f, 0,
                   0xFFFF, 20);
  else
    for (int k = 0; k < 2; k++) {
      /* (RenderDragon's beams: light thrown out of it) */
      float a = rndf() * 6.2831853f, b = (rndf() - 0.5f) * 3.1415927f, s = 0.4f + rndf() * 0.6f;
      particle_add(e->x, e->y + 2, e->z, cosf(a) * cosf(b) * s, sinf(b) * s, sinf(a) * cosf(b) * s,
                   rnd(2) ? 0xFFFF : 0xF81F, 30);
    }
  if (dg.death > 150 && dg.death % 5 == 0 && rule(GR_MOB_LOOT)) player_add_xp(1000);
  e->y += 0.1f;
  dg.yaw += 20;
  e->yaw = dg.yaw + 180;
  if (dg.death == 200) {
    if (rule(GR_MOB_LOOT)) player_add_xp(2000);
    exit_portal(ifl(e->x), 64, ifl(e->z));
    dst.dragon_dead = 1, dst.dragon_hp = 0;
    e->type = E_NONE;
  }
}

/* EntityDragon.onLivingUpdate */
void dragon_tick(Entity *e) {
  if (e->hurt > 0) e->hurt--;
  if (e->invuln > 0) e->invuln--;
  dg.prev_anim = dg.anim;
  if (dg.death) {
    dying(e);
    return;
  }
  /* updateDragonEnderCrystal: the nearest crystal within 32 heals it 1 every 10 ticks */
  int n;
  const EndSpike *s = gen_spikes(&n);
  if (dg.heal >= 0 && e->age % 10 == 0 && e->health < 200) e->health++;
  if (rnd(10) == 0) {
    float best = 1e30f;
    dg.heal = -1;
    for (int i = 0; i < n; i++) {
      if (!crystal_alive(i)) continue;
      float cx = s[i].x + 0.5f, cy = s[i].y + s[i].h + 1.0f, cz = s[i].z + 0.5f;
      if (fabsf(cx - e->x) > 40 || fabsf(cz - e->z) > 40 || cy < e->y - 32 || cy > e->y + 40) continue;
      float d = (cx - e->x) * (cx - e->x) + (cy - e->y) * (cy - e->y) + (cz - e->z) * (cz - e->z);
      if (d < best) best = d, dg.heal = (int8_t)i;
    }
  }
  float sp = sqrtf(e->vx * e->vx + e->vz * e->vz);
  float f10 = 0.2f / (sp * 10 + 1) * powf(2, e->vy);
  dg.anim += dg.slowed ? f10 * 0.5f : f10;
  dg.yaw = wrap(dg.yaw);
  if (++dg.ring_i == 32) dg.ring_i = 0;
  dg.ring[dg.ring_i][0] = (int16_t)(dg.yaw * 100), dg.ring[dg.ring_i][1] = (int16_t)(e->y * 16);
  float d10 = dg.tx - e->x, d0 = dg.ty - e->y, d1 = dg.tz - e->z, d2 = d10 * d10 + d0 * d0 + d1 * d1;
  if (dg.to_player) {
    dg.tx = pl.x, dg.tz = pl.z;
    float a = dg.tx - e->x, b = dg.tz - e->z, d8 = 0.4f + sqrtf(a * a + b * b) / 80 - 1;
    dg.ty = pl.y + (d8 > 10 ? 10 : d8);
    if (pl.dead || dim != DIM_END) dg.to_player = 0;
  } else
    dg.tx += gauss() * 2, dg.tz += gauss() * 2;
  if (dg.force_new || d2 < 100 || d2 > 22500) new_target(e);
  float hd = sqrtf(d10 * d10 + d1 * d1);
  d0 = hd > 0 ? d0 / hd : 0;
  d0 = d0 > 0.6f ? 0.6f : d0 < -0.6f ? -0.6f : d0;
  e->vy += d0 * 0.1f;
  float d4 = 180 - atan2f(d10, d1) * 57.29578f, d6 = wrap(d4 - dg.yaw);
  d6 = d6 > 50 ? 50 : d6 < -50 ? -50 : d6;
  float tx = dg.tx - e->x, ty = dg.ty - e->y, tz = dg.tz - e->z, tl = sqrtf(tx * tx + ty * ty + tz * tz);
  if (tl > 0) tx /= tl, ty /= tl, tz /= tl;
  float yr = dg.yaw * 0.017453292f, lx = sinf(yr), ly = e->vy, lz = -cosf(yr), ll = sqrtf(lx * lx + ly * ly + lz * lz);
  lx /= ll, ly /= ll, lz /= ll;
  float f5 = ((lx * tx + ly * ty + lz * tz) + 0.5f) / 1.5f;
  if (f5 < 0) f5 = 0;
  dg.yaw_v *= 0.8f;
  float f6 = sp + 1, d11 = sp + 1;
  if (d11 > 40) d11 = 40;
  dg.yaw_v += d6 * (0.7f / d11 / f6);
  dg.yaw += dg.yaw_v * 0.1f;
  float f7 = 2 / (d11 + 1), fr = 0.06f * (f5 * f7 + (1 - f7));
  /* moveFlying(0, -1): head first */
  yr = dg.yaw * 0.017453292f;
  e->vx += fr * sinf(yr), e->vz -= fr * cosf(yr);
  /* its parts: the body, the wings out at 4.5, the head 5.5 ahead (as the neck lifts it) */
  float o5y, o10y, o0y, oy;
  dragon_offsets(5, 1, &oy, &o5y);
  dragon_offsets(10, 1, &oy, &o10y);
  dragon_offsets(0, 1, &oy, &o0y);
  float f1 = (o5y - o10y) * 10 / 180 * 3.1415927f, f2 = cosf(f1), f15 = -sinf(f1);
  float f16 = sinf(yr), f4 = cosf(yr);
  float f17 = sinf(yr - dg.yaw_v * 0.01f), f18 = cosf(yr - dg.yaw_v * 0.01f);
  const float part[2][3] = {{f16 * 0.5f, 0, -f4 * 0.5f}, {f17 * 5.5f * f2, (o0y - o5y) + f15 * 5.5f, -f18 * 5.5f * f2}};
  /* the move: 1.8.8's dragon goes through everything (noClip); here what it can't break stops it, so it
   * doesn't come at the player through the island: it slides along it and climbs (free once inside) */
  float k = dg.slowed ? 0.8f : 1, dx = e->vx * k, dy = e->vy * k, dz = e->vz * k;
  if (stopped(e->x, e->y, e->z, part)) e->x += dx, e->y += dy, e->z += dz, e->vy += 0.05f;
  else {
    if (stopped(e->x, e->y + dy, e->z, part)) e->vy = dy > 0 ? 0 : 0.1f, dy = 0;
    e->y += dy;
    if (stopped(e->x + dx, e->y, e->z, part)) e->vx = 0, e->vy += 0.1f, dx = 0;
    e->x += dx;
    if (stopped(e->x, e->y, e->z + dz, part)) e->vz = 0, e->vy += 0.1f, dz = 0;
    e->z += dz;
  }
  float mx = e->vx, my = e->vy, mz = e->vz, ml = sqrtf(mx * mx + my * my + mz * mz);
  if (ml > 0) mx /= ml, my /= ml, mz /= ml;
  float f9 = 0.8f + 0.15f * (((mx * lx + my * ly + mz * lz) + 1) / 2);
  e->vx *= f9, e->vz *= f9, e->vy *= 0.91f;
  e->yaw = dg.yaw + 180;
  float bx = e->x + part[0][0], bz = e->z + part[0][2];
  float hx = e->x + part[1][0], hy = e->y + part[1][1], hz = e->z + part[1][2];
  if (e->hurt == 0 && !pl.dead && dim == DIM_END) {
    /* the wings knock the player away; the head bites (10) */
    for (int w = -1; w <= 1; w += 2) {
      float wx = e->x + w * f4 * 4.5f, wy = e->y + 2, wz = e->z + w * f16 * 4.5f;
      if (fabsf(pl.x - wx) < 2 + 4 + 0.3f && fabsf(pl.z - wz) < 2 + 4 + 0.3f && pl.y + 1.8f > wy - 2 && pl.y < wy) {
        float a = pl.x - bx, b = pl.z - bz, d = a * a + b * b;
        if (d < 0.01f) d = 0.01f;
        pl.vx += a / d * 4, pl.vy += 0.2f, pl.vz += b / d * 4;
      }
    }
    if (fabsf(pl.x - hx) < 1.5f + 1 + 0.3f && fabsf(pl.z - hz) < 1.5f + 1 + 0.3f && pl.y + 1.8f > hy - 1 && pl.y < hy + 4)
      player_hurt(opt.difficulty == 0 ? 0 : opt.difficulty == 1 ? 6 : opt.difficulty == 3 ? 15 : 10, DMG_MOB);
  }
  dg.slowed = through(hx - 1.5f, hy, hz - 1.5f, hx + 1.5f, hy + 3, hz + 1.5f) |
              through(bx - 2.5f, e->y, bz - 2.5f, bx + 2.5f, e->y + 3, bz + 2.5f);
  dst.dragon_hp = e->health;
}

/* attackEntityFromPart: the head takes it all, the rest a quarter and 1; it turns away; dead, it dies */
void dragon_hurt(Entity *e, float dmg) {
  if (dg.death || e->invuln > 10) return;
  /* the head: if the player's look passes near it */
  float yr = dg.yaw * 0.017453292f, hx = e->x + sinf(yr) * 5.5f, hy = e->y + 1.5f, hz = e->z - cosf(yr) * 5.5f;
  float vx = hx - look_ray[0], vy = hy - look_ray[1], vz = hz - look_ray[2], *d = look_ray + 3;
  float dl = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), t = (vx * d[0] + vy * d[1] + vz * d[2]) / (dl * dl);
  float cx = vx - d[0] * t, cy = vy - d[1] * t, cz = vz - d[2] * t;
  bool head = cx * cx + cy * cy + cz * cz < 2.5f * 2.5f;
  if (!head) dmg = dmg / 4 + 1;
  dg.tx = e->x + sinf(yr) * 5 + (rndf() - 0.5f) * 2;
  dg.ty = e->y + rndf() * 3 + 1;
  dg.tz = e->z - cosf(yr) * 5 + (rndf() - 0.5f) * 2;
  dg.to_player = 0;
  e->health = (int16_t)(e->health - (int)ceilf(dmg));
  e->invuln = 20, e->hurt = 10;
  if (e->health <= 0) e->health = 0, dg.death = 1;
}

/* an ender crystal hit: it blows up (power 6); the dragon it was healing takes 10 */
void crystal_gone(Entity *e) {
  int i = e->tx;
  if (i >= 0 && i < 64) dst.crystals_gone[i >> 5] |= 1u << (i & 31);
  float x = e->x, y = e->y, z = e->z;
  e->type = E_NONE;
  explode(x, y, z, 6, true);
  Entity *d = dragon();
  if (d && dg.heal == i) {
    dg.heal = -1;
    if (!dg.death && d->invuln <= 10) {
      d->health = (int16_t)(d->health - 10), d->invuln = 20, d->hurt = 10;
      if (d->health <= 0) d->health = 0, dg.death = 1;
    }
  }
}

/* entering the End: the dragon, if it lives (it is met at (0, 128, 0), as it was made) */
void dragon_enter(void) {
  if (dst.dragon_dead) return;
  Entity *e = ent_new(E_DRAGON, 0.5f, 128, 0.5f);
  if (!e) return;
  memset(&dg, 0, sizeof dg);
  dg.heal = -1;
  dg.yaw = rndf() * 360;
  e->yaw = dg.yaw + 180;
  e->health = (int16_t)(dst.dragon_hp > 0 ? dst.dragon_hp : 200);
  for (int i = 0; i < 32; i++) dg.ring[i][0] = (int16_t)(wrap(dg.yaw) * 100), dg.ring[i][1] = 128 * 16;
  new_target(e);
}

/* each tick in the End: the crystals of the pillars near, the dragon back if the entities were lost */
void end_tick(void) {
  if (dim != DIM_END) return;
  int n;
  const EndSpike *s = gen_spikes(&n);
  for (int i = 0; i < n; i++) {
    int x = s[i].x, y = s[i].y + s[i].h + 1, z = s[i].z;
    if (!crystal_alive(i) || !world_loaded(x, vc_y0, z)) continue;
    bool there = false;
    for (int k = 0; k < N_ENT && !there; k++) there = ents[k].type == E_CRYSTAL && ents[k].tx == i;
    if (there) continue;
    Entity *e = ent_new(E_CRYSTAL, x + 0.5f, (float)y, z + 0.5f);
    if (e) e->tx = (int16_t)i, e->tz = 0, e->age = (int16_t)rnd(10000), e->health = 5;
  }
  if (!dst.dragon_dead && !dragon()) dragon_enter();
  /* the healing crystal's beam to it (RenderDragon): its light along the way, a few bits a tick */
  float a[3], b[3];
  if (dragon_beam(a, b))
    for (int k = 0; k < 3; k++) {
      float t = rndf();
      particle_add(a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t, 0, 0, 0,
                   rnd(2) ? 0xFFFF : 0xC81F, 6);
    }
}

/* the boss bar: its health (0 to 1), or -1 with no dragon near (BossStatus, set as RenderDragon draws it) */
float dragon_bar(void) {
  Entity *e = dragon();
  if (!e || dim != DIM_END) return -1;
  float dx = e->x - pl.x, dz = e->z - pl.z;
  if (dx * dx + dz * dz > 160 * 160) return -1;
  return e->health / 200.0f;
}
/* what the HUD points to in the End while the dragon lives: the dragon (kind 0) and the tops of the pillars
 * whose crystals are whole (kind 1) */
int end_marks(float (*at)[3], uint8_t *kind, int max) {
  Entity *e = dragon();
  if (!e || dim != DIM_END || dg.death || dst.dragon_dead) return 0;
  int n = 0, ns;
  at[n][0] = e->x, at[n][1] = e->y + 2, at[n][2] = e->z, kind[n++] = 0;
  const EndSpike *s = gen_spikes(&ns);
  for (int i = 0; i < ns && n < max; i++)
    if (crystal_alive(i)) at[n][0] = s[i].x + 0.5f, at[n][1] = s[i].y + s[i].h + 1.0f, at[n][2] = s[i].z + 0.5f, kind[n++] = 1;
  return n;
}
/* the beam to the dragon from the crystal healing it: the crystal's place, false if none */
bool dragon_beam(float *from, float *to) {
  Entity *e = dragon();
  if (!e || dg.heal < 0 || dg.death) return false;
  int n;
  const EndSpike *s = gen_spikes(&n);
  from[0] = s[dg.heal].x + 0.5f, from[1] = s[dg.heal].y + s[dg.heal].h + 1.6f, from[2] = s[dg.heal].z + 0.5f;
  to[0] = e->x, to[1] = e->y + 2, to[2] = e->z;
  return true;
}
#endif
