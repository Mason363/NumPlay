/* NumBlocks.nwa's villages and desert temples, checked on a computer:
 *
 *   where they start: 1.8.8's grid of regions of 32 x 32 chunks, a start 0 to 23 chunks into each (so at
 *   least 8 apart), from World.setRandomSeed with the salt 10387312 (villages) or 14357617 (temples),
 *   worked out here again with a java.util.Random of this file's own, for a few seeds;
 *   how villages are laid out: no two pieces in each other, all within reach of the well, kept only with
 *   more than two pieces off the roads;
 *   how they are built: a village's well, its blacksmith's chest and a desert temple's trap where 1.8.8 puts
 *   them, and the same blocks whichever rows of a chunk are made; a dungeon's spawner at work;
 *   mineshafts: where they start (MapGenBase's random, here again), the same blocks whichever rows are made;
 *   old worlds: a world saved before NumBlocks.nwa made villages keeps its look where it had been (no
 *   village there), and gets them elsewhere; it remembers where that is. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "../src/nb.h"
#include "../src/gen.h"

extern uint32_t host_time;
extern bool start_in_world;
extern int64_t start_seed;
void game_init(void);
bool game_frame(void);
void host_save_dir(const char *d);

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#define SAY(...) do { printf("  "); printf(__VA_ARGS__); printf("\n"); } while (0)

/* java.util.Random */
static uint64_t jr;
static void jseed(int64_t s) { jr = ((uint64_t)s ^ 0x5DEECE66DULL) & ((1ULL << 48) - 1); }
static int jnext(int bits) {
  jr = (jr * 0x5DEECE66DULL + 0xB) & ((1ULL << 48) - 1);
  return (int)(int32_t)(uint32_t)(jr >> (48 - bits));
}
static int jint(int n) {
  if ((n & -n) == n) return (int)(((int64_t)n * jnext(31)) >> 31);
  int bits, val;
  do bits = jnext(31), val = bits % n;
  while (bits - val + (n - 1) < 0);
  return val;
}
static int64_t jlong(void) {
  int64_t hi = jnext(32);
  return (int64_t)((uint64_t)hi << 32) + jnext(32);
}
static double jdouble(void) {
  int64_t a = jnext(26), b = jnext(27);
  return (double)((a << 27) + b) * (1.0 / 9007199254740992.0);
}
/* MapGenMineshaft.canSpawnStructureAtCoords for chunk (x, z), with MapGenBase's random for it */
static int mineshaft_at(int64_t seed, int x, int z) {
  jseed(seed);
  int64_t j = jlong(), k = jlong();
  jseed((int64_t)((uint64_t)(int64_t)x * (uint64_t)j ^ (uint64_t)(int64_t)z * (uint64_t)k ^ (uint64_t)seed));
  jnext(32);
  int m = abs(x) > abs(z) ? abs(x) : abs(z);
  return jdouble() < 0.004 && jint(80) < m;
}
/* MapGenVillage / MapGenScatteredFeature.canSpawnStructureAtCoords: region (rx, rz)'s start */
static void start_of(int64_t seed, int rx, int rz, int salt, int *sx, int *sz) {
  jseed((int64_t)((uint64_t)(int64_t)rx * 341873128712ULL + (uint64_t)(int64_t)rz * 132897987541ULL + (uint64_t)seed +
                  (uint64_t)(int64_t)salt));
  *sx = rx * 32 + jint(32 - 8);
  *sz = rz * 32 + jint(32 - 8);
}

static uint8_t slab[128 * 256], hb[128 * 256 / 8], part[24 * 256], phb[24 * 256 / 8];
static int block_at(const uint8_t *s, const uint8_t *h, int x, int y, int z) {
  int c = (y * 16 + (z & 15)) * 16 + (x & 15);
  return s[c] | (h[c >> 3] >> (c & 7) & 1) << 8;
}
static void make(int cx, int cz, int y0, int h, uint8_t *s, uint8_t *hi) {
  memset(hi, 0, (size_t)h * 256 / 8);
  gen_hi = hi;
  gen_slab(cx, cz, y0, h, s);
  gen_hi = NULL;
}

/* the water in a village's well (the start chunk's corner + 2) as gen_slab makes it */
static int well_water(int wx, int wz) {
  make(wx >> 4, wz >> 4, 0, 128, slab, hb);
  int n = 0;
  for (int y = 40; y < 100; y++) n += block_at(slab, hb, wx + 2, y, wz + 2) == B_WATER;
  return n;
}
/* a block only villages make (the generator's own, rare, differences at a slab's edge are plants and springs) */
static int built(int b) {
  return b == B_COBBLESTONE || b == B_PLANKS_OAK || b == B_LOG_OAK || b == B_GLASS_PANE || b == B_FENCE_OAK ||
         b == B_FARMLAND || b == B_GRAVEL || b == B_WOOL_BLACK || b == B_LADDER || b == B_BOOKSHELF ||
         b == B_CRAFTING_TABLE || b == B_FURNACE || b == B_STONE_SLAB || b == B_STONE_SLAB_DOUBLE || b == B_CHEST ||
         (b >= B_TORCH && b <= B_TORCH_N) || (b >= B_WHEAT_0 && b <= B_WHEAT_7) || (b >= B_CARROTS_0 && b <= B_POTATOES_3) ||
         b == B_OAK_STAIRS || b == B_COBBLESTONE_STAIRS || (b >= B_OAK_STAIRS_W && b <= B_DOOR_OAK_LOWER_N_OPEN) ||
         b == B_DOOR_OAK_LOWER || b == B_DOOR_OAK_UPPER || b > 255;
}
static int boxes_meet(const int *a, const int *b) {
  return a[5] >= b[2] && a[2] <= b[5] && a[7] >= b[4] && a[4] <= b[7] && a[6] >= b[3] && a[3] <= b[6];
}

int main(void) {
  static const int64_t seeds[] = {0, 12345, -4172144997902289642LL, 404};
  printf("where villages and temples start\n");
  int villages = 0, made = 0;
  for (unsigned s = 0; s < sizeof seeds / sizeof seeds[0]; s++) {
    gen_init(seeds[s]);
    for (int rz = -3; rz <= 3; rz++)
      for (int rx = -3; rx <= 3; rx++) {
        int out[10], sx, sz;
        start_of(seeds[s], rx, rz, 10387312, &sx, &sz);
        int found = gen_village_find(rx, rz, out);
        CHECK(out[0] == sx && out[1] == sz);
        CHECK(sx - rx * 32 >= 0 && sx - rx * 32 < 24 && sz - rz * 32 >= 0 && sz - rz * 32 < 24);
        int nx, nz;   /* the next region's start: at least 8 chunks further */
        start_of(seeds[s], rx + 1, rz, 10387312, &nx, &nz);
        CHECK(nx - sx >= 8);
        if (!found) continue;
        villages++;
        /* the layout: the well first, at the start chunk + 2; no piece in another; near the well; more than
         * two pieces off the roads to be made */
        int p[128][8], n = 0, houses = 0;
        while (n < 128 && gen_village_piece(n, p[n])) n++;
        CHECK(n == out[9] && n > 0);
        CHECK(p[0][0] == 0 && p[0][2] == out[0] * 16 + 2 && p[0][4] == out[1] * 16 + 2);
        for (int i = 0; i < n; i++) {
          houses += p[i][0] != 1;
          CHECK(abs(p[i][2] - p[0][2]) <= 112 + 35 && abs(p[i][4] - p[0][4]) <= 112 + 35);
          for (int j = i + 1; j < n; j++) CHECK(!boxes_meet(p[i], p[j]));
        }
        CHECK(out[2] == (houses > 2));
        made += out[2];
      }
    for (int rz = -3; rz <= 3; rz++)
      for (int rx = -3; rx <= 3; rx++) {
        int sx, sz, t[2];
        start_of(seeds[s], rx, rz, 14357617, &sx, &sz);
        gen_temple_find(rx, rz, t);
        CHECK(t[0] == sx && t[1] == sz);
      }
  }
  SAY("%d villages laid out, %d made, for %d seeds", villages, made, (int)(sizeof seeds / sizeof seeds[0]));
  CHECK(made > 10);

  printf("a village's well, the same whichever rows are made\n");
  gen_init(0);
  int out[10];
  CHECK(gen_village_find(-2, -1, out) && out[2]);   /* (seed 0: a village at chunk -53, -16) */
  int wx = out[0] * 16 + 2, wz = out[1] * 16 + 2, cx = wx >> 4, cz = wz >> 4;
  make(cx, cz, 0, 128, slab, hb);
  int water = 0, cobble = 0, wy = 0;
  for (int y = 40; y < 100; y++)
    for (int z = wz + 1; z <= wz + 4; z++)
      for (int x = wx + 1; x <= wx + 4; x++) {
        int b = block_at(slab, hb, x, y, z);
        water += b == B_WATER;
        cobble += b == B_COBBLESTONE;
        if (b == B_WATER && y > wy) wy = y;
      }
  SAY("the well at %d %d: %d water, %d cobblestone, its water up to y %d", wx, wz, water, cobble, wy);
  CHECK(water >= 4 * 10 && cobble >= 12 * 10);
  int differ = 0;
  for (int k = 0; k < 9; k++) {   /* (the well's chunk and the eight around it) */
    int qx = cx - 1 + k % 3, qz = cz - 1 + k / 3;
    make(qx, qz, 0, 128, slab, hb);
    for (int y0 = 0; y0 < 128; y0 += 24) {
      int h = 128 - y0 < 24 ? 128 - y0 : 24;
      make(qx, qz, y0, h, part, phb);
      for (int y = 0; y < h; y++)
        for (int i = 0; i < 256; i++)
          if (block_at(part, phb, i & 15, y, i >> 4) != block_at(slab, hb, i & 15, y0 + y, i >> 4) &&
              (built(block_at(part, phb, i & 15, y, i >> 4)) || built(block_at(slab, hb, i & 15, y0 + y, i >> 4)))) {
            if (differ++ < 8)
              SAY("at %d %d %d: %d (rows from %d), %d (all)", qx * 16 + (i & 15), y0 + y, qz * 16 + (i >> 4),
                  block_at(part, phb, i & 15, y, i >> 4), y0, block_at(slab, hb, i & 15, y0 + y, i >> 4));
          }
    }
  }
  SAY("its nine chunks made in rows of 24: %d blocks of the village differ", differ);
  CHECK(differ == 0);

  printf("the blacksmith's chest\n");
  int smith = -1, sp[8];
  gen_village_find(-2, -1, out);
  for (int i = 0; gen_village_piece(i, sp); i++)
    if (sp[0] == 10) smith = i;   /* (VT_HOUSE2) */
  CHECK(smith >= 0);
  if (smith >= 0) {
    gen_village_piece(smith, sp);
    /* its chest at (5, 1, 5) of the piece: turned its way, on the floor the ground sets */
    int f = sp[1], kx = f == 3 ? sp[5] - 5 : f == 1 ? sp[2] + 5 : sp[2] + 5, kz = f == 0 ? sp[7] - 5 : f == 2 ? sp[4] + 5 : sp[4] + 5;
    make(kx >> 4, kz >> 4, 0, 128, slab, hb);
    int ky = -1;
    for (int y = 40; y < 110; y++)
      if (block_at(slab, hb, kx, y, kz) == B_CHEST) ky = y;
    SAY("at %d %d %d: a chest %s", kx, ky, kz, ky >= 0 && gen_struct_chest(kx, ky, kz) == 1 ? "with its loot" : "missing");
    CHECK(ky >= 0 && gen_struct_chest(kx, ky, kz) == 1);
  }

  printf("a dungeon's spawner\n");
  gen_init(0);
  int dsp[8], nd = 0;
  for (int qz = -6; qz <= 6 && !nd; qz++)
    for (int qx = -6; qx <= 6 && !nd; qx++) {
      make(qx, qz, 0, 128, slab, hb);
      nd = gen_dungeon_spawners(qx * 16 + 8, qz * 16 + 8, dsp, 2);
      if (nd) CHECK(block_at(slab, hb, dsp[0], dsp[1], dsp[2]) == B_MOB_SPAWNER);
    }
  SAY("at %d %d %d, of %s", dsp[0], dsp[1], dsp[2], nd ? dsp[3] == 0 ? "skeletons" : dsp[3] == 1 ? "zombies" : "spiders" : "nothing");
  CHECK(nd > 0);

  printf("mineshafts\n");
  int nms = 0, mo[7], first[2] = {0, 0};
  for (int qz = -80; qz <= 80; qz++)
    for (int qx = -80; qx <= 80; qx++) {
      int m = gen_mineshaft_find(qx, qz, mo);
      CHECK(m == mineshaft_at(0, qx, qz));
      if (!m) continue;
      if (!nms++) first[0] = qx, first[1] = qz;
      CHECK(mo[6] > 0 && mo[1] >= 1 && mo[4] <= 63);
    }
  SAY("%d start in 161 x 161 chunks of seed 0", nms);
  CHECK(nms > 20);
  /* the first one's room: dug out, its floor dirt; its chunks the same whichever rows are made */
  gen_mineshaft_find(first[0], first[1], mo);
  int built = 0, msd = 0;
  for (int k = 0; k < 9; k++) {
    int qx = first[0] - 1 + k % 3, qz = first[1] - 1 + k / 3;
    make(qx, qz, 0, 128, slab, hb);
    for (int i = 0; i < 128 * 256; i++) built += slab[i] == B_FENCE_OAK || slab[i] == B_RAIL;
    for (int y0 = 0; y0 < 128; y0 += 24) {
      int h = 128 - y0 < 24 ? 128 - y0 : 24;
      make(qx, qz, y0, h, part, phb);
      for (int y = 0; y < h; y++)
        for (int i = 0; i < 256; i++) {
          int a = block_at(part, phb, i & 15, y, i >> 4), b = block_at(slab, hb, i & 15, y0 + y, i >> 4);
          msd += a != b && (a == B_FENCE_OAK || a == B_PLANKS_OAK || a == B_RAIL || a == B_COBWEB || b == B_FENCE_OAK ||
                            b == B_PLANKS_OAK || b == B_RAIL || b == B_COBWEB);
        }
    }
  }
  SAY("chunk %d %d's: %d pieces, %d fences and rails around it, %d blocks differ by rows", first[0], first[1], mo[6], built, msd);
  CHECK(built > 10 && msd == 0);

  printf("a desert temple's trap\n");
  gen_init(12345);
  int t[2];
  CHECK(gen_temple_find(2, -1, t));   /* (seed 12345: a desert temple at chunk 86, -14) */
  int tx = t[0] * 16 + 10, tz = t[1] * 16 + 10;
  make(tx >> 4, tz >> 4, 0, 128, slab, hb);
  int tnt = 0;
  for (int z = tz - 1; z <= tz + 1; z++)
    for (int x = tx - 1; x <= tx + 1; x++) tnt += block_at(slab, hb, x, 51, z) == B_TNT;
  SAY("the middle at %d %d: %d TNT at y 51, the plate %d", tx, tz, tnt, block_at(slab, hb, tx, 53, tz));
  CHECK(tnt == 9 && block_at(slab, hb, tx, 53, tz) == B_STONE_PRESSURE_PLATE);
  CHECK(gen_struct_chest(tx + 2, 53, tz) == 2 && gen_struct_chest(tx, 53, tz - 2) == 2 && !gen_struct_chest(tx, 53, tz));

  printf("an old world keeps its look where it had been\n");
  mkdir("build/structs-saves", 0755);
  host_save_dir("build/structs-saves");
  remove("build/structs-saves/nb1.nbw");
  remove("build/structs-saves/nb1rdim.nbx");
  start_in_world = true, start_seed = 0;
  game_init();
  pl.mode = 1, pl.flying = true;
  pl.x = wx + 3.5f, pl.y = 80, pl.z = wz + 3.5f;
  world_finish(pl.x, pl.y, pl.z);
  int new_well = well_water(wx, wz);
  SAY("a new world: the well's water %d deep", new_well);
  CHECK(new_well >= 10);
  CHECK(save_world());
  /* the dungeon's spawner at work, in the dark */
  if (nd) {
    pl.mode = 0, pl.flying = false;
    pl.x = dsp[0] + 2.5f, pl.y = (float)dsp[1], pl.z = dsp[2] + 0.5f;
    world_finish(pl.x, pl.y, pl.z);
    for (int x = dsp[0] - 3; x <= dsp[0] + 3; x++)   /* (room around it: the dungeon may be crossed by a cave) */
      for (int z = dsp[2] - 3; z <= dsp[2] + 3; z++) {
        world_set(x, dsp[1] - 1, z, B_COBBLESTONE);
        for (int y = dsp[1]; y <= dsp[1] + 2; y++)
          if (x != dsp[0] || z != dsp[2] || y != dsp[1]) world_set(x, y, z, B_AIR);
      }
    static const int kind[3] = {E_SKELETON, E_ZOMBIE, E_SPIDER};
    int made = 0;
    for (int k = 0; k < 900 && !made; k++) {
      host_time += 50;
      game_frame();
      pl.health = 20;
      for (int i = 0; i < N_ENT; i++) made += ents[i].type == kind[dsp[3]];
    }
    SAY("it made %d mob(s) of its kind", made);
    CHECK(made > 0);
    pl.mode = 1, pl.flying = true, pl.y = 80;
  }
  /* the world as NumBlocks.nwa saved it before: its side record without the boxes ("NBD1") */
  uint32_t len;
  const uint8_t *d = plat_load("nb1rdim.nbx", &len);
  CHECK(d && !memcmp(d, "NBD2", 4));
  static uint8_t old[8192];
  uint32_t side2 = 0;
  if (d) {
    /* (Side: magic, time, played, dim, the player, the dimensions' state; then (NBD2) 68 bytes of boxes) */
    side2 = 4 + 4 + 4 + 4 + (uint32_t)sizeof(Player) + (uint32_t)sizeof(DimsState);
    memcpy(old, d, side2);
    memcpy(old, "NBD1", 4);
    memcpy(old + side2, d + side2 + 68, len - side2 - 68);
    CHECK(plat_save("nb1rdim.nbx", old, len - 68));
  }
  CHECK(load_world(1));
  world_finish(pl.x, pl.y, pl.z);
  int old_well = well_water(wx, wz);
  SAY("the same world, saved before there were villages: water %d deep there", old_well);
  CHECK(old_well < 3 && gen_old_chunk(cx, cz));
  /* elsewhere: villages (seed 0: one at chunk 67, 40) */
  CHECK(gen_village_find(2, 1, out) && out[2]);
  int fx = out[0] * 16 + 2, fz = out[1] * 16 + 2;
  CHECK(!gen_old_chunk(fx >> 4, fz >> 4));
  pl.x = fx + 3.5f, pl.z = fz + 3.5f;
  world_finish(pl.x, pl.y, pl.z);
  int far_well = well_water(fx, fz);
  SAY("far away, a village's well: water %d deep", far_well);
  CHECK(far_well >= 10);
  /* saved there and loaded again: the old place is still the old one */
  CHECK(save_world() && load_world(1));
  CHECK(gen_old_chunk(cx, cz) && !gen_old_chunk(fx >> 4, fz >> 4));
  d = plat_load("nb1rdim.nbx", &len);
  CHECK(d && !memcmp(d, "NBD2", 4));

  printf("%s\n", fails ? "FAILED" : "all good (villages and temples)");
  return fails != 0;
}
