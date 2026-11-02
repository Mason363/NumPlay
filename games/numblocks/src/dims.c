/* NumBlocks.nwa only: the Nether and the End, as Minecraft 1.8.8 has them, next to the Overworld.
 *
 * The player is in one dimension at a time (dim); the block cache, the generator and the edit log
 * follow it. Each dimension's changes have their own records (edits.c). */
#include <math.h>
#include "nb.h"
#if NB_DIMS
#include "edits.h"
#include "gen.h"

int dim;
DimsState dst;
extern uint32_t game_time;

/* what the generators' codes stand for (dims.h) */
const uint16_t gen_code_block[GC_N] = {
    B_NETHERRACK, B_SOUL_SAND, B_QUARTZ_ORE, B_NETHER_BRICK, B_NETHER_BRICK_FENCE, B_NETHER_BRICK_STAIRS_E,
    B_NETHER_BRICK_STAIRS_W, B_NETHER_BRICK_STAIRS_S, B_NETHER_BRICK_STAIRS_N, B_NETHER_WART_0, B_END_STONE,
    B_END_PORTAL_FRAME, B_END_PORTAL_FRAME_EYE};

static inline int ifl(float v) { return (int)floorf(v); }
static inline bool is_portal(int b) { return b == B_PORTAL_X || b == B_PORTAL_Z; }

/* ---------------------------------------------------------------- Nether portals (BlockPortal) */
/* BlockPortal.Size: the frame around a spot, along an axis (0: x, its left east and its right west;
 * 1: z, north and south) */
typedef struct {
  int ax, lx, lz, rx, rz;   /* the axis; its left and right steps */
  int x, y, z;              /* the bottom left corner inside (none: w 0) */
  int w, h, n;              /* width, height, portal blocks inside */
} PSize;
static bool inside(int b) { return b == B_AIR || b == B_FIRE || is_portal(b); }   /* func_150857_a */
/* func_180120_a: how far inside goes that way on obsidian, to obsidian (0: not to obsidian) */
static int span(int x, int y, int z, int dx, int dz) {
  int i = 0;
  for (; i < 22; i++)
    if (!inside(world_get(x + dx * i, y, z + dz * i)) || world_get(x + dx * i, y - 1, z + dz * i) != B_OBSIDIAN) break;
  return world_get(x + dx * i, y, z + dz * i) == B_OBSIDIAN ? i : 0;
}
static void psize(PSize *s, int x, int y, int z, int ax) {
  s->ax = ax;
  s->lx = ax ? 0 : 1, s->lz = ax ? -1 : 0;
  s->rx = -s->lx, s->rz = -s->lz;
  s->w = s->h = s->n = 0;
  for (int y0 = y; y > y0 - 21 && y > 0 && inside(world_get(x, y - 1, z));) y--;
  int i = span(x, y, z, s->lx, s->lz) - 1;
  if (i < 0) return;
  s->x = x + s->lx * i, s->y = y, s->z = z + s->lz * i;
  s->w = span(s->x, s->y, s->z, s->rx, s->rz);
  if (s->w < 2 || s->w > 21) {
    s->w = 0;
    return;
  }
  /* func_150858_a: up while each row is inside, obsidian at both ends; then a row of obsidian on top */
  for (s->h = 0; s->h < 21; s->h++)
    for (int k = 0; k < s->w; k++) {
      int bx = s->x + s->rx * k, by = s->y + s->h, bz = s->z + s->rz * k, b = world_get(bx, by, bz);
      if (!inside(b)) goto top;
      if (is_portal(b)) s->n++;
      if (k == 0 && world_get(bx + s->lx, by, bz + s->lz) != B_OBSIDIAN) goto top;
      if (k == s->w - 1 && world_get(bx + s->rx, by, bz + s->rz) != B_OBSIDIAN) goto top;
    }
top:
  for (int k = 0; k < s->w; k++)
    if (world_get(s->x + s->rx * k, s->y + s->h, s->z + s->rz * k) != B_OBSIDIAN) {
      s->h = 0;
      break;
    }
  if (s->h < 3 || s->h > 21) s->w = s->h = 0;
}
static bool psize_ok(const PSize *s) { return s->w >= 2 && s->w <= 21 && s->h >= 3 && s->h <= 21; }

/* BlockPortal.func_176548_d, from BlockFire.onBlockAdded: fire lit inside an empty frame makes the portal
 * (not in the End) */
bool dims_portal_light(int x, int y, int z) {
  if (dim == DIM_END) return false;
  PSize s;
  for (int ax = 0; ax < 2; ax++) {
    psize(&s, x, y, z, ax);
    if (!psize_ok(&s) || s.n) continue;
    for (int k = 0; k < s.w; k++)
      for (int j = 0; j < s.h; j++) world_set(s.x + s.rx * k, s.y + j, s.z + s.rz * k, ax ? B_PORTAL_Z : B_PORTAL_X);
    return true;
  }
  return false;
}

/* BlockPortal.onNeighborBlockChange: a portal block stays while its frame is whole and full; else it
 * goes, and the rest with it (in Minecraft each one's neighbours go in turn) */
static void portal_break(int x, int y, int z, int b) {
  uint16_t st[448];
  int n = 0, ax = b == B_PORTAL_Z;
  world_set(x, y, z, B_AIR);
  st[n++] = (uint16_t)(32 << 8 | 32);
  while (n) {
    int p = st[--n], u = (p >> 8) - 32, v = (p & 255) - 32;
    static const int8_t du[4] = {1, -1, 0, 0}, dv[4] = {0, 0, 1, -1};
    for (int k = 0; k < 4; k++) {
      int nu = u + du[k], nv = v + dv[k];
      int bx = x + (ax ? 0 : nu), by = y + nv, bz = z + (ax ? nu : 0);
      if (nu < -31 || nu > 31 || nv < -31 || nv > 31 || world_get(bx, by, bz) != b) continue;
      world_set(bx, by, bz, B_AIR);
      if (n < 448) st[n++] = (uint16_t)((nu + 32) << 8 | (nv + 32));
    }
  }
}

/* Block.canBlockStay for the dimensions' blocks: -1 for the others */
int dims_can_stay(int b, int x, int y, int z) {
  if (is_portal(b)) {
    PSize s;
    psize(&s, x, y, z, b == B_PORTAL_Z);
    if (!psize_ok(&s) || s.n < s.w * s.h) portal_break(x, y, z, b);
    return 1;   /* (gone already, or staying) */
  }
  if (b >= B_NETHER_WART_0 && b <= B_NETHER_WART_3) return world_get(x, y - 1, z) == B_SOUL_SAND;
  /* BlockDoor (iron): a half needs the other, the lower one ground under it */
  if (b == B_IRON_DOOR_UPPER) return is_iron_door(world_get(x, y - 1, z));
  if (is_iron_door(b)) {
    int u = world_get(x, y - 1, z);
    return world_get(x, y + 1, z) == B_IRON_DOOR_UPPER && ((blk_flags[u] & BF_OPAQUE) || u == B_GLASS);
  }
  /* BlockBasePressurePlate: on a solid top, or a fence (the villages' tables) */
  if (b == B_WOODEN_PRESSURE_PLATE || b == B_STONE_PRESSURE_PLATE) {
    int u = world_get(x, y - 1, z);
    return (blk_flags[u] & BF_OPAQUE) || u == B_FENCE_OAK || u == B_NETHER_BRICK_FENCE;
  }
  /* BlockButton: on a whole block behind it */
  if (b >= B_STONE_BUTTON_E && b <= B_STONE_BUTTON_N) {
    static const int8_t bx[4] = {-1, 1, 0, 0}, bz[4] = {0, 0, -1, 1};   /* (facing east, west, south, north) */
    return (blk_flags[world_get(x + bx[b - B_STONE_BUTTON_E], y, z + bz[b - B_STONE_BUTTON_E])] & BF_OPAQUE) != 0;
  }
  return -1;
}

/* ---------------------------------------------------------------- going through */
/* Teleporter.makePortal: a 2 x 3 portal in its frame, on the nearest spot within 16 that has room (ground
 * under 4 x 3, air above, 4 x 4 x 3 (or else 4 x 1) with the frame), else floating on an obsidian ledge at
 * y 70 or more. (The spots are looked for in the rows the cache holds.) */
static int made[3];   /* (where: the search for it reads the records, which a full storage may not have) */
static void make_portal(void) {
  int j = ifl(pl.x), k = ifl(pl.y), l = ifl(pl.z);
  float best = -1;
  int i1 = j, j1 = k, k1 = l, l1 = 0, i2 = rnd(4);
  int ylo = vc_y0 + 1, yhi = vc_y0 + VCY - 5;
  for (int pass = 0; pass < 2 && best < 0; pass++)
    for (int j2 = j - 16; j2 <= j + 16; j2++) {
      float d1 = j2 + 0.5f - pl.x;
      for (int l2 = l - 16; l2 <= l + 16; l2++) {
        float d2 = l2 + 0.5f - pl.z;
        for (int j3 = yhi; j3 >= ylo; j3--) {
          if (world_get(j2, j3, l2) != B_AIR) continue;
          while (j3 > ylo && world_get(j2, j3 - 1, l2) == B_AIR) j3--;
          for (int k3 = i2; k3 < i2 + (pass ? 2 : 4); k3++) {
            int l3 = k3 % 2, i4 = 1 - l3;
            if (!pass && k3 % 4 >= 2) l3 = -l3, i4 = -i4;
            bool ok = true;
            for (int j4 = 0; j4 < (pass ? 1 : 3) && ok; j4++)
              for (int k4 = 0; k4 < 4 && ok; k4++)
                for (int l4 = -1; l4 < 4 && ok; l4++) {
                  int bx = j2 + (k4 - 1) * l3 + j4 * i4, by = j3 + l4, bz = l2 + (k4 - 1) * i4 - j4 * l3;
                  int b = world_get(bx, by, bz);
                  if (l4 < 0 ? !(blk_flags[b] & BF_SOLID) : b != B_AIR) ok = false;
                }
            if (!ok) continue;
            float d5 = j3 + 0.5f - pl.y, d7 = d1 * d1 + d5 * d5 + d2 * d2;
            if (best < 0 || d7 < best) best = d7, i1 = j2, j1 = j3, k1 = l2, l1 = pass ? k3 % 2 : k3 % 4;
          }
        }
      }
    }
  int l6 = l1 % 2, i3 = 1 - l6;
  if (l1 % 4 >= 2) l6 = -l6, i3 = -i3;
  if (best < 0) {
    /* nowhere: a ledge of obsidian, air about it */
    j1 = j1 < 70 ? 70 : j1 > WORLD_H - 10 ? WORLD_H - 10 : j1;
    pl.y = (float)j1;
    world_finish(pl.x, pl.y, pl.z);
    for (int j7 = -1; j7 <= 1; j7++)
      for (int l7 = 1; l7 < 3; l7++)
        for (int k8 = -1; k8 < 3; k8++)
          world_set(i1 + (l7 - 1) * l6 + j7 * i3, j1 + k8, k1 + (l7 - 1) * i3 - j7 * l6, k8 < 0 ? B_OBSIDIAN : B_AIR);
  }
  for (int l8 = 0; l8 < 4; l8++)
    for (int l9 = -1; l9 < 4; l9++) {
      bool frame = l8 == 0 || l8 == 3 || l9 == -1 || l9 == 3;
      world_set(i1 + (l8 - 1) * l6, j1 + l9, k1 + (l8 - 1) * i3, frame ? B_OBSIDIAN : l6 ? B_PORTAL_X : B_PORTAL_Z);
    }
  made[0] = i1, made[1] = j1, made[2] = k1;
}

/* Teleporter.placeInExistingPortal: to the nearest portal within 128 (its lowest block), at the place
 * across it the player went in at; false: none */
static bool place_in_portal(float frac, int from_ax) {
  int at[3];
  if (!edits_find(ifl(pl.x), ifl(pl.y), ifl(pl.z), 128, B_PORTAL_X, B_PORTAL_Z, at)) return false;
  pl.x = at[0] + 0.5f, pl.y = (float)at[1], pl.z = at[2] + 0.5f;
  world_finish(pl.x, pl.y, pl.z);
  int b = world_get(at[0], at[1], at[2]), ax = b == B_PORTAL_Z;
  if (!is_portal(b)) return true;
  /* across: from its one side to the other */
  int dx = ax ? 0 : 1, dz = ax ? 1 : 0, lo = 0, hi = 0;
  while (lo > -21 && world_get(at[0] + dx * (lo - 1), at[1], at[2] + dz * (lo - 1)) == b) lo--;
  while (hi < 21 && world_get(at[0] + dx * (hi + 1), at[1], at[2] + dz * (hi + 1)) == b) hi++;
  float a = lo + 0.3f + frac * (hi - lo + 0.4f);
  if (ax) pl.z = at[2] + a;
  else pl.x = at[0] + a;
  if (ax != from_ax) pl.yaw += 90;
  return true;
}

/* the player's last place in the Overworld: what NumPlay's NumBlocks loads when the world is saved
 * elsewhere (save.c) */
static void left_overworld(void) {
  dst.ow_x = pl.x, dst.ow_y = pl.y, dst.ow_z = pl.z;
}

/* the loading screen while the dimension is made */
static void loading_screen(void) {
  int g = gui;
  gui = GUI_LOADING;
  render_frame(NULL, game_time);
  gui = g;
}

/* Entity.travelToDimension and ServerConfigurationManager.transferEntityToWorld */
static void travel(int to, float frac, int ax) {
  int from = dim;
  float x = pl.x, y = pl.y, z = pl.z;
  if (to == DIM_NETHER) x /= 8, z /= 8;
  else if (from == DIM_NETHER) x *= 8, z *= 8;
  /* (the world border, then whole blocks) */
  if (x < -29999872) x = -29999872;
  if (x > 29999872) x = 29999872;
  if (z < -29999872) z = -29999872;
  if (z > 29999872) z = 29999872;
  x = (float)(int)x, z = (float)(int)z;
  if (from == DIM_OVERWORLD) left_overworld();
  loading_screen();
  if (!world_dim(to)) {
    gui_message(T("Storage is full: the portal does nothing"));
    return;
  }
  pl.x = x, pl.y = y, pl.z = z;
  pl.vx = pl.vy = pl.vz = 0;
  pl.fall = 0;
  memset(ents, 0, sizeof ents);
  if (to == DIM_END) {
    /* WorldProviderEnd.getSpawnCoordinate, Teleporter.placeInPortal: on an obsidian ledge at 100, 49, 0 */
    pl.x = 100, pl.y = 49, pl.z = 0, pl.yaw = 90, pl.pitch = 0;
    world_finish(pl.x, pl.y, pl.z);
    for (int j1 = -2; j1 <= 2; j1++)
      for (int k1 = -2; k1 <= 2; k1++)
        for (int l1 = -1; l1 < 3; l1++) world_set(100 + k1, 49 + l1, 0 - j1, l1 < 0 ? B_OBSIDIAN : B_AIR);
    dragon_enter();
  } else if (from == DIM_END) {
    /* the End left: back where the player respawns (with all they have) */
    player_to_spawn();
  } else {
    world_finish(pl.x, pl.y, pl.z);
    if (!place_in_portal(frac, ax)) {
      make_portal();
      edits_flush();
      if (!place_in_portal(frac, ax)) pl.x = made[0] + 0.5f, pl.y = (float)made[1], pl.z = made[2] + 0.5f;
    }
  }
  world_finish(pl.x, pl.y, pl.z);
  camera_reset();
}

/* ---------------------------------------------------------------- the player in portals */
static int16_t portal_count, portal_cool;
float portal_time;   /* EntityPlayerSP.timeInPortal: the overlay */
int wither_ticks;
static bool in_portal;

/* Entity.onEntityUpdate's portal part, Entity.setPortal (a portal block in the player's cells: their
 * whole cells, as doBlockCollisions has it), BlockEndPortal.onEntityCollidedWithBlock */
void dims_player_tick(void) {
  /* Potion.wither: 1 every 40 ticks (it kills) */
  if (wither_ticks > 0) {
    if (wither_ticks % 40 == 0) player_hurt(1, DMG_MAGIC);
    wither_ticks--;
  }
  end_tick();
  dims_spawners_tick();
  bool nether = false, end = false;
  int px = 0, pz = 0, pax = 0;
  for (int y = ifl(pl.y + 0.001f); y <= ifl(pl.y + 1.799f); y++)
    for (int z = ifl(pl.z - 0.299f); z <= ifl(pl.z + 0.299f); z++)
      for (int x = ifl(pl.x - 0.299f); x <= ifl(pl.x + 0.299f); x++) {
        int b = world_get(x, y, z);
        if (is_portal(b)) nether = true, px = x, pz = z, pax = b == B_PORTAL_Z;
        if (b == B_END_PORTAL) end = true;
      }
  if (end && !pl.dead) {
    if (dim == DIM_END) dims_credits();
    else if (dim == DIM_OVERWORLD) travel(DIM_END, 0, 0);
    return;
  }
  if (nether) {
    if (portal_cool > 0) portal_cool = 10;
    else in_portal = true;
  }
  /* EntityPlayerSP.onLivingUpdate */
  if (in_portal) {
    portal_time += 0.0125f;
    if (portal_time > 1) portal_time = 1;
  } else if (portal_time > 0) {
    portal_time -= 0.05f;
    if (portal_time < 0) portal_time = 0;
  }
  int wait = pl.mode == 1 ? 0 : 80;   /* EntityPlayer.getMaxInPortalTime */
  if (in_portal) {
    if (portal_count++ >= wait) {
      portal_count = (int16_t)wait;
      portal_cool = 10;   /* getPortalCooldown */
      /* where across the portal the player went in */
      int lo = 0, hi = 0, b = pax ? B_PORTAL_Z : B_PORTAL_X, dx = pax ? 0 : 1, dz = pax ? 1 : 0, y = ifl(pl.y + 0.5f);
      while (lo > -21 && world_get(px + dx * (lo - 1), y, pz + dz * (lo - 1)) == b) lo--;
      while (hi < 21 && world_get(px + dx * (hi + 1), y, pz + dz * (hi + 1)) == b) hi++;
      float at = pax ? pl.z - pz : pl.x - px, frac = (at - lo) / (hi - lo + 1);
      frac = frac < 0 ? 0 : frac > 1 ? 1 : frac;
      travel(dim == DIM_NETHER ? DIM_OVERWORLD : DIM_NETHER, frac, pax);
    }
    in_portal = false;
  } else {
    if (portal_count > 0) portal_count -= 4;
    if (portal_count < 0) portal_count = 0;
  }
  if (portal_cool > 0) portal_cool--;
}

/* ServerConfigurationManager.recreatePlayerEntity's place: the spawn point, up out of blocks */
void player_to_spawn(void) {
  pl.x = pl.spawn_x + 0.5f, pl.y = (float)pl.spawn_y, pl.z = pl.spawn_z + 0.5f;
  pl.vx = pl.vy = pl.vz = 0, pl.fall = 0;
  world_finish(pl.x, pl.y, pl.z);
  for (int k = 0; k < WORLD_H; k++) {
    int x = ifl(pl.x), y = ifl(pl.y), z = ifl(pl.z);
    if (!(blk_flags[world_get(x, y, z)] & BF_SOLID) && !(blk_flags[world_get(x, y + 1, z)] & BF_SOLID)) break;
    pl.y = (float)(y + 1);
    world_finish(pl.x, pl.y, pl.z);
  }
}

/* (tests) straight there, as through a portal */
void dims_go(int to) { travel(to, 0.5f, 0); }

/* death: the respawn is in the Overworld (WorldProvider.canRespawnHere) */
void dims_respawn(void) {
  if (dim == DIM_OVERWORLD) return;
  loading_screen();
  world_dim(DIM_OVERWORLD);
  memset(ents, 0, sizeof ents);
}

/* the End's portal: the End poem and the credits, then the player at the Overworld's spawn
 * (EntityPlayerMP.travelToDimension from the End to it, GuiWinGame) */
void dims_credits(void) {
  loading_screen();
  if (!world_dim(DIM_OVERWORLD)) return;
  memset(ents, 0, sizeof ents);
  player_to_spawn();
  camera_reset();
  credits_open();
}
#endif
