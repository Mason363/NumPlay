/* NumBlocks.nwa's Nether and End played through on a computer, as a player would go:
 *
 *   a Nether portal lit and walked into; the Nether's terrain; a fortress, its blaze spawner at work;
 *   blaze rods and ender pearls from what was killed, blaze powder and eyes of ender crafted; an ender
 *   pearl thrown; back through the portal; an eye of ender flying to a stronghold; its portal room, the
 *   frame filled, the End portal opening; the End: its crystals broken, the dragon killed, the exit portal
 *   and the egg; the exit portal: the End poem, then the Overworld's spawn; saved in the Nether and
 *   loaded again.
 *
 * Then it leaves the world saved in the Nether in build/dims-saves for np_load.c (NumPlay's NumBlocks). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "../src/nb.h"

extern uint32_t host_keys, host_time;
extern bool start_in_world;
extern int64_t start_seed;
void game_init(void);
bool game_frame(void);
void host_save_dir(const char *d);

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#define SAY(...) do { printf("  "); printf(__VA_ARGS__); printf("\n"); } while (0)

static void frames(int n) {
  for (int i = 0; i < n; i++) {
    host_time += 50;
    game_frame();
  }
}
static void hold(int id, int n) { pl.inv[pl.slot] = (Stack){(uint16_t)id, (uint16_t)n}; }
/* there, flying (Creative), the world around made */
static void fly_to(float x, float y, float z) {
  pl.mode = 1, pl.flying = true;
  pl.x = x, pl.y = y, pl.z = z, pl.vx = pl.vy = pl.vz = 0;
  world_finish(pl.x, pl.y, pl.z);
  camera_reset();
}
static int count_ents(int type, int item) {
  int n = 0;
  for (int i = 0; i < N_ENT; i++)
    if (ents[i].type == type && (type != E_ITEM || ents[i].item.id == item)) n++;
  return n;
}
static Entity *find_ent(int type) {
  for (int i = 0; i < N_ENT; i++)
    if (ents[i].type == type) return &ents[i];
  return NULL;
}
/* kills mobs of a type summoned by the player until one drops the item (or tries run out): drops seen */
static int kill_for(int type, int item, int tries) {
  int got = 0;
  for (int t = 0; t < tries && !got; t++) {
    for (int i = 0; i < N_ENT; i++)
      if (ents[i].type == E_ITEM) ents[i].type = E_NONE;
    Entity *e = mob_summon(type, pl.x + 2, pl.y, pl.z);
    if (!e) break;
    hold(I_DIAMOND_SWORD, 0);
    for (int k = 0; k < 40 && e->type == type && e->state != 255; k++) {
      mob_attack(e);
      frames(11);
    }
    frames(30);
    got = count_ents(E_ITEM, item);
  }
  return got;
}
static int craft2(int a, int b) {
  Stack g[9] = {{0}};
  g[0] = (Stack){(uint16_t)a, 1};
  if (b) g[1] = (Stack){(uint16_t)b, 1};
  int r = craft_find(g, 2);
  return r < 0 ? 0 : recipes[r].out * 100 + recipes[r].n;
}
static int nearest_block(int b, int r, int *at) {   /* the block within r of the player (cache): found */
  int px = (int)floorf(pl.x), py = (int)floorf(pl.y), pz = (int)floorf(pl.z);
  for (int d = 0; d <= r; d++)
    for (int y = py - d; y <= py + d; y++)
      for (int z = pz - d; z <= pz + d; z++)
        for (int x = px - d; x <= px + d; x++)
          if (world_get(x, y, z) == b) {
            at[0] = x, at[1] = y, at[2] = z;
            return 1;
          }
  return 0;
}

int main(void) {
  mkdir("build/dims-saves", 0755);
  host_save_dir("build/dims-saves");
  remove("build/dims-saves/nb1.nbw");
  start_in_world = true, start_seed = 0;
  game_init();
  opt.difficulty = 2;
  CHECK(dim == DIM_OVERWORLD);

  printf("a Nether portal\n");
  pl.mode = 0, pl.flying = false;
  int fy = (int)floorf(pl.y), px = (int)floorf(pl.x), pz = (int)floorf(pl.z) + 3;
  for (int z = pz - 2; z <= pz + 2; z++)
    for (int x = px - 3; x <= px + 3; x++)
      for (int y = fy - 1; y < fy + 6; y++) world_set(x, y, z, y == fy - 1 ? B_STONE : B_AIR);
  for (int x = px - 1; x <= px + 2; x++)
    for (int y = fy - 1; y <= fy + 3; y++)
      if (x == px - 1 || x == px + 2 || y == fy - 1 || y == fy + 3) world_set(x, y, pz, B_OBSIDIAN);
  hold(I_FLINT_AND_STEEL, 0);
  player_use_on(px, fy - 1, pz, 1);   /* (flint and steel on the frame's bottom) */
  int portals = 0;
  for (int x = px; x <= px + 1; x++)
    for (int y = fy; y <= fy + 2; y++) portals += world_get(x, y, pz) == B_PORTAL_X || world_get(x, y, pz) == B_PORTAL_Z;
  CHECK(portals == 6);
  float ow_x = px + 1.0f, ow_z = pz + 0.5f;
  pl.x = ow_x, pl.y = (float)fy, pl.z = ow_z, pl.vx = pl.vy = pl.vz = 0;
  for (int t = 0; t < 200 && dim == DIM_OVERWORLD; t++) frames(1);
  CHECK(dim == DIM_NETHER);
  SAY("in the Nether at %.1f %.1f %.1f (from %.1f, %.1f)", pl.x, pl.y, pl.z, ow_x, ow_z);
  CHECK(fabsf(pl.x - ow_x / 8) < 130 && fabsf(pl.z - ow_z / 8) < 130);
  int at[3];
  CHECK(nearest_block(B_PORTAL_X, 3, at) || nearest_block(B_PORTAL_Z, 3, at));
  int npx = at[0], npy = at[1], npz = at[2];

  printf("the Nether's terrain\n");
  {
    static uint8_t slab[24 * 256];
    int counts[8] = {0};   /* netherrack, lava, soul sand, gravel, glowstone, quartz, bedrock, sea lava */
    for (int cz = -3; cz <= 3; cz++)
      for (int cx = -3; cx <= 3; cx++)
        for (int y0 = 0; y0 < 128; y0 += 24) {
          int h = y0 + 24 > 128 ? 128 - y0 : 24;
          gen_slab(cx, cz, y0, h, slab);
          for (int c = 0; c < h * 256; c++) {
            int b = slab[c], y = y0 + c / 256;
            unsigned k = (unsigned)(b - GEN_CODE(0));
            if (k < GC_N) b = gen_code_block[k];
            counts[0] += b == B_NETHERRACK;
            counts[1] += is_lava(b);
            counts[2] += b == B_SOUL_SAND;
            counts[3] += b == B_GRAVEL;
            counts[4] += b == B_GLOWSTONE;
            counts[5] += b == B_QUARTZ_ORE;
            counts[6] += b == B_BEDROCK && (y == 0 || y == 127);
            counts[7] += is_lava(b) && y == 31;
          }
        }
    SAY("netherrack %d, lava %d (%d at the sea's top), soul sand %d, gravel %d, glowstone %d, quartz %d, bedrock %d",
        counts[0], counts[1], counts[7], counts[2], counts[3], counts[4], counts[5], counts[6]);
    CHECK(counts[0] > 49 * 128 * 256 / 4);
    CHECK(counts[1] > 0 && counts[7] > 0 && counts[2] > 0 && counts[3] > 0 && counts[4] > 0 && counts[5] > 0);
    CHECK(counts[6] >= 49 * 256 * 2 * 9 / 10);
  }

  printf("a fortress\n");
  int f[4];
  CHECK(gen_fortress_find((int)pl.x, (int)pl.z, f));
  SAY("at %d %d %d, %d pieces", f[0], f[1], f[2], f[3]);
  CHECK(f[3] > 10);
  fly_to(f[0] + 0.5f, (float)f[1] + 4, f[2] + 0.5f);
  int bricks = 0;
  for (int z = -12; z <= 12; z++)
    for (int x = -12; x <= 12; x++)
      for (int y = -8; y <= 8; y++) bricks += world_get(f[0] + x, f[1] + y, f[2] + z) == B_NETHER_BRICK;
  CHECK(bricks > 200);
  int sp[6], nsp = gen_fortress_spawners(f[0], f[2], sp, 2);
  SAY("%d blaze spawner(s)%s", nsp, nsp ? "" : " in this one");
  if (nsp) {
    fly_to(sp[0] + 0.5f, sp[1] + 3.0f, sp[2] + 4.5f);
    CHECK(world_get(sp[0], sp[1], sp[2]) == B_MOB_SPAWNER);
    memset(ents, 0, sizeof ents);
    int blazes = 0;
    for (int t = 0; t < 1000 && !blazes; t++) frames(1), blazes = count_ents(E_BLAZE, 0);
    SAY("the spawner made %d blaze(s)", blazes);
    CHECK(blazes > 0);
  }

  printf("blaze rods and ender pearls\n");
  memset(ents, 0, sizeof ents);
  CHECK(kill_for(E_BLAZE, I_BLAZE_ROD, 12) > 0);
  CHECK(craft2(I_BLAZE_ROD, 0) == I_BLAZE_POWDER * 100 + 2);
  CHECK(kill_for(E_ENDERMAN, I_ENDER_PEARL, 12) > 0);
  CHECK(craft2(I_ENDER_PEARL, I_BLAZE_POWDER) == I_ENDER_EYE * 100 + 1);
  memset(ents, 0, sizeof ents);

  printf("back through the portal\n");
  pl.mode = 0, pl.flying = false, pl.health = 20;
  pl.x = npx + 0.5f, pl.y = (float)npy, pl.z = npz + 0.5f, pl.vx = pl.vy = pl.vz = 0;
  world_finish(pl.x, pl.y, pl.z);
  for (int t = 0; t < 400 && dim == DIM_NETHER; t++) frames(1);
  CHECK(dim == DIM_OVERWORLD);
  SAY("at %.1f %.1f %.1f, the portal it was lit at %.1f %.1f", pl.x, pl.y, pl.z, ow_x, ow_z);
  CHECK(fabsf(pl.x - ow_x) < 3 && fabsf(pl.z - ow_z) < 3);

  printf("an ender pearl\n");
  pl.x = ow_x, pl.y = (float)fy, pl.z = ow_z + 2, pl.vx = pl.vy = pl.vz = 0;   /* (out of the portal) */
  frames(20);
  {
    float x0 = pl.x, z0 = pl.z, h0 = pl.health;
    pl.yaw = 0, pl.pitch = 25;   /* (down the slope: it lands within the blocks kept) */
    hold(I_ENDER_PEARL, 1);
    throw_item(I_ENDER_PEARL, 1.5f, true);
    for (int t = 0; t < 100 && fabsf(pl.z - z0) < 1; t++) frames(1);
    SAY("from %.1f %.1f to %.1f %.1f, health %.0f to %.0f", x0, z0, pl.x, pl.z, h0, pl.health);
    CHECK(fabsf(pl.z - z0) > 4 && pl.health <= h0 - 5 + 0.01f);
    pl.health = 20;
  }

  printf("an eye of ender\n");
  int to[3];
  CHECK(gen_stronghold_near((int)pl.x, (int)pl.z, to));
  SAY("it flies towards %d %d %d", to[0], to[1], to[2]);
  memset(ents, 0, sizeof ents);
  throw_eye(to[0], to[1], to[2]);
  Entity *eye = find_ent(E_EYE);
  CHECK(eye != NULL);
  if (eye) {
    float d0 = hypotf(to[0] - eye->x, to[2] - eye->z), y0 = eye->y;
    frames(30);
    float d1 = hypotf(to[0] - eye->x, to[2] - eye->z);
    SAY("%.1f away, then %.1f, %.1f higher", d0, d1, eye->y - y0);
    CHECK(eye->type == E_EYE && d1 < d0 - 4 && eye->y > y0 + 2);
  }

  printf("the stronghold\n");
  int best = -1, hf[6] = {0};
  float bd = 0;
  for (int s = 0; s < 3; s++) {
    int g[6] = {0};
    if (!gen_stronghold_find(s, g)) continue;
    float d = hypotf(g[0] - pl.x, g[1] - pl.z);
    if (best < 0 || d < bd) best = s, bd = d, memcpy(hf, g, sizeof hf);
  }
  CHECK(best >= 0);
  SAY("stronghold %d: its portal room at %d %d %d, %d pieces", best, hf[2], hf[3], hf[4], hf[5]);
  fly_to(hf[2] + 0.5f, hf[3] + 1.0f, hf[4] + 0.5f);
  int frames_n = 0, eyes = 0;
  for (int dz = -2; dz <= 2; dz++)
    for (int dx = -2; dx <= 2; dx++) {
      int b = world_get(hf[2] + dx, hf[3], hf[4] + dz);
      frames_n += b == B_END_PORTAL_FRAME || b == B_END_PORTAL_FRAME_EYE;
      eyes += b == B_END_PORTAL_FRAME_EYE;
    }
  SAY("%d frames, %d with an eye", frames_n, eyes);
  CHECK(frames_n == 12);
  CHECK(world_get(hf[2], hf[3] - 2, hf[4]) == B_LAVA);
  CHECK(world_get(hf[2], hf[3] + 1, hf[4] + 4) == B_MOB_SPAWNER || world_get(hf[2], hf[3] + 1, hf[4] - 4) == B_MOB_SPAWNER ||
        world_get(hf[2] + 4, hf[3] + 1, hf[4]) == B_MOB_SPAWNER || world_get(hf[2] - 4, hf[3] + 1, hf[4]) == B_MOB_SPAWNER);
  /* standing on the frame's near side, the eyes in, one by one */
  pl.y = hf[3] + 1.0f;
  for (int dz = -2; dz <= 2; dz++)
    for (int dx = -2; dx <= 2; dx++)
      if (world_get(hf[2] + dx, hf[3], hf[4] + dz) == B_END_PORTAL_FRAME) {
        hold(I_ENDER_EYE, 12);
        player_use_on(hf[2] + dx, hf[3], hf[4] + dz, 1);
      }
  int open = 0;
  for (int dz = -1; dz <= 1; dz++)
    for (int dx = -1; dx <= 1; dx++) open += world_get(hf[2] + dx, hf[3], hf[4] + dz) == B_END_PORTAL;
  CHECK(open == 9);

  printf("into the End\n");
  pl.mode = 0, pl.flying = false;
  pl.x = hf[2] + 0.5f, pl.y = (float)hf[3], pl.z = hf[4] + 0.5f, pl.vx = pl.vy = pl.vz = 0;
  for (int t = 0; t < 20 && dim != DIM_END; t++) frames(1);
  CHECK(dim == DIM_END);
  SAY("at %.1f %.1f %.1f, on %d", pl.x, pl.y, pl.z, world_get((int)floorf(pl.x), (int)floorf(pl.y) - 1, (int)floorf(pl.z)));
  CHECK(world_get((int)floorf(pl.x), (int)floorf(pl.y) - 1, (int)floorf(pl.z)) == B_OBSIDIAN);
  CHECK(find_ent(E_DRAGON) != NULL);

  printf("the crystals\n");
  int nsk;
  const EndSpike *sk = gen_spikes(&nsk);
  CHECK(nsk >= 8);
  for (int i = 0; i < nsk; i++) {
    fly_to(sk[i].x + 0.5f, (float)(sk[i].y + sk[i].h + 4), sk[i].z + 3.5f);
    frames(2);
    for (int k = 0; k < N_ENT; k++)
      if (ents[k].type == E_CRYSTAL && ents[k].tx == i) mob_attack(&ents[k]);
    frames(2);
  }
  int broken = 0;
  for (int i = 0; i < nsk; i++) broken += (dst.crystals_gone[i >> 5] >> (i & 31)) & 1;
  SAY("%d of %d broken", broken, nsk);
  CHECK(broken == nsk);

  printf("the dragon at a player on the island\n");
  /* (it comes at the player, but no more through the end stone: its body never in it) */
  {
    int gy = 90;
    while (gy > 10 && gen_end_block(0, gy - 1, 20) != B_END_STONE) gy--;
    fly_to(0.5f, (float)gy, 20.5f);
    pl.flying = false;
    Entity *d = find_ent(E_DRAGON);
    int inside = 0;
    float nearest = 1e9f;
    for (int t = 0; t < 2000 && d; t++) {
      pl.x = 0.5f, pl.z = 20.5f, pl.y = (float)gy, pl.vx = pl.vy = pl.vz = 0, pl.health = 20;
      frames(1);
      d = find_ent(E_DRAGON);
      if (!d) break;
      nearest = fminf(nearest, hypotf(d->x - pl.x, d->z - pl.z) + fabsf(d->y - pl.y));
      bool in = false;
      for (int y = (int)floorf(d->y + 0.2f); y <= (int)floorf(d->y + 2.8f) && !in; y++)
        for (int z = (int)floorf(d->z - 1.5f); z <= (int)floorf(d->z + 1.5f) && !in; z++)
          for (int x = (int)floorf(d->x - 1.5f); x <= (int)floorf(d->x + 1.5f) && !in; x++)
            in = world_get(x, y, z) == B_END_STONE;
      inside += in;
    }
    SAY("standing at y %d: it came within %.1f, %d ticks in the end stone", gy, nearest, inside);
    CHECK(nearest < 16 && inside == 0);
  }

  printf("the dragon\n");
  fly_to(0.5f, 80, 20.5f);
  Entity *dr = find_ent(E_DRAGON);
  CHECK(dr != NULL);
  float dx = 0, dz = 0;
  hold(I_DIAMOND_SWORD, 0);
  int hits = 0;
  for (int t = 0; t < 4000 && !dst.dragon_dead; t++) {
    dr = find_ent(E_DRAGON);
    if (!dr) break;
    dx = dr->x, dz = dr->z;
    if (dr->health > 0 && t % 11 == 0) mob_attack(dr), hits++;
    pl.x = dr->x, pl.z = dr->z + 20, pl.y = 80, pl.vx = pl.vy = pl.vz = 0;
    frames(1);
  }
  SAY("%d hits; it died over %.0f %.0f; experience %d", hits, dx, dz, pl.xp_total);
  CHECK(dst.dragon_dead);
  CHECK(pl.xp_total >= 12000);
  fly_to(floorf(dx) + 0.5f, 70, floorf(dz) + 0.5f);
  int ex[3];
  CHECK(nearest_block(B_END_PORTAL, 8, ex));
  CHECK(nearest_block(B_DRAGON_EGG, 12, at));
  SAY("the exit portal at %d %d %d, the egg at %d %d %d", ex[0], ex[1], ex[2], at[0], at[1], at[2]);

  printf("the exit portal\n");
  pl.mode = 0, pl.flying = false;
  pl.x = ex[0] + 0.5f, pl.y = (float)ex[1], pl.z = ex[2] + 0.5f, pl.vx = pl.vy = pl.vz = 0;
  frames(2);
  CHECK(dim == DIM_OVERWORLD && gui == GUI_CREDITS);
  frames(100);
  CHECK(gui == GUI_CREDITS);   /* (the poem rolls on) */
  host_keys = K_BACK;
  frames(1);
  host_keys = 0;
  frames(1);
  CHECK(gui == GUI_NONE);
  SAY("back at %.1f %.1f %.1f", pl.x, pl.y, pl.z);

  printf("saved in the Nether, loaded again\n");
  dims_go(DIM_NETHER);
  CHECK(dim == DIM_NETHER);
  pl.mode = 0;
  hold(I_BLAZE_ROD, 3);
  pl.inv[1] = (Stack){B_COBBLESTONE, 20};
  float sx = pl.x, sy = pl.y, sz = pl.z;
  CHECK(save_world());
  dim = DIM_OVERWORLD;
  CHECK(load_world(1));
  CHECK(dim == DIM_NETHER && fabsf(pl.x - sx) < 0.01f && fabsf(pl.y - sy) < 0.01f && fabsf(pl.z - sz) < 0.01f);
  CHECK(pl.inv[pl.slot].id == I_BLAZE_ROD);
  FILE *o = fopen("build/dims-saves/expect.txt", "w");
  if (o) fprintf(o, "%f %f %f\n", dst.ow_x, dst.ow_y, dst.ow_z), fclose(o);
  CHECK(save_world());
  printf("%s\n", fails ? "FAILED" : "all good (the Nether and the End)");
  return fails != 0;
}
