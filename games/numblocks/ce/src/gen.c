/* The terrain, made from the world's seed, a column at a time: Minecraft's biomes (oceans and beaches, plains,
 * forests, birch forests, taigas, snowy taigas, deserts, extreme hills), its layers (grass on dirt on stone,
 * sand on sandstone, bedrock at the bottom), caves (lava at the bottom of the deepest), ores at Minecraft's
 * depths, scaled to the CE's 64 levels, and its trees, cacti, tall grass and flowers.
 *
 * All of it is integer arithmetic on small numbers (the eZ80 has no floating point): value noise on lattices
 * of 8 to 128 blocks, hashed through a permutation of 0-255 shuffled by the seed. */
#include "nb.h"

static uint8_t perm[256];
static uint8_t salt[8];
static uint16_t smooth_t[256];   /* smoothstep, 0..256 for 0..255 */

/* the seed's own shuffle (Java's LCG would need 48-bit arithmetic: this one is 32-bit) */
static uint32_t lcg;
static unsigned lcg_next(void) {
  lcg = lcg * 1103515245u + 12345u;
  return (unsigned)(lcg >> 16) & 0x7FFF;
}

void gen_seed(uint32_t seed) {
  lcg = seed ^ 0x5DEECE66u;
  for (int i = 0; i < 256; i++) perm[i] = (uint8_t)i;
  for (int i = 255; i > 0; i--) {
    int j = (int)(lcg_next() % (unsigned)(i + 1));
    uint8_t t = perm[i];
    perm[i] = perm[j], perm[j] = t;
  }
  for (int i = 0; i < 8; i++) salt[i] = (uint8_t)lcg_next();
  for (int t = 0; t < 256; t++) smooth_t[t] = (uint16_t)(((int32_t)t * t * (768 - 2 * t)) >> 16);
}

static inline uint8_t h2(int x, int z, int s) {
  return perm[(uint8_t)(perm[(uint8_t)(perm[(uint8_t)(x + s)] + z)] + ((x >> 8) ^ (z >> 8) * 3) + s)];
}
static inline uint8_t h3(int x, int y, int z, int s) {
  return perm[(uint8_t)(perm[(uint8_t)(perm[(uint8_t)(perm[(uint8_t)(x + s)] + y)] + z)] + (x >> 8) + (z >> 8))];
}

#define smooth(t) smooth_t[t]

/* value noise in 2D: 0..255, on a lattice of 1 << sh blocks */
static int noise2(int x, int z, int sh, int s) {
  int xi = x >> sh, zi = z >> sh;
  int fxv = smooth((x - (xi << sh)) << (8 - sh)), fzv = smooth((z - (zi << sh)) << (8 - sh));
  int a = h2(xi, zi, s), b = h2(xi + 1, zi, s), c = h2(xi, zi + 1, s), d = h2(xi + 1, zi + 1, s);
  int ab = a + (((b - a) * fxv) >> 8), cd = c + (((d - c) * fxv) >> 8);
  return ab + (((cd - ab) * fzv) >> 8);
}

/* biomes from temperature, rain and height */
static int fbm2(int x, int z, int sh, int s) {
  return (noise2(x, z, sh, s) * 2 + noise2(x, z, sh - 1, s + 37) + 1) / 3;
}

void gen_column(int x, int z, Column *c) {
  int cont = fbm2(x, z, 7, salt[0]);       /* land or sea (128-block lattice) */
  int hill = fbm2(x, z, 6, salt[1]);       /* where the extreme hills rise */
  int det = noise2(x, z, 4, salt[2]) * 2 + noise2(x, z, 3, salt[3]) + noise2(x, z, 2, salt[4]) / 2;   /* 0..893 */
  int temp = fbm2(x + 1000, z, 7, salt[5]);
  int rain = fbm2(x, z + 1000, 6, salt[6]);
  /* the land: up from the sea where cont is high; the hills: up to 22 blocks more */
  int land = cont - 118;                   /* -118..137: < 0 the ocean */
  int base = land < 0 ? SEA + land * 13 / 64 : SEA + 1 + land / 18;
  int amp = 3;
  if (hill > 150) amp += (hill - 150) * 20 / 105;
  if (land < 0) amp = 2;
  int h = base + ((det - 446) * amp) / 446;
  if (h < 6) h = 6;
  if (h > WORLD_H - 10) h = WORLD_H - 10;
  int b;
  if (h < SEA - 1) b = BIOME_OCEAN;
  else if (h <= SEA && land < 14) b = BIOME_BEACH;
  else if (amp >= 12) b = temp < 90 ? BIOME_COLD_TAIGA : BIOME_EXTREME_HILLS;
  else if (temp > 165 && rain < 120) b = BIOME_DESERT;
  else if (temp < 80) b = BIOME_COLD_TAIGA;
  else if (temp < 105) b = BIOME_TAIGA;
  else if (rain > 150) b = temp > 140 ? BIOME_FOREST : BIOME_BIRCH_FOREST;
  else if (rain > 128) b = BIOME_FOREST;
  else b = BIOME_PLAINS;
  c->h = (uint8_t)h;
  c->biome = (uint8_t)b;
  c->depth = (uint8_t)(3 + (h2(x, z, salt[7]) & 1));
  switch (b) {
    case BIOME_DESERT: case BIOME_BEACH: c->top = B_SAND, c->filler = B_SAND; break;
    case BIOME_OCEAN: {
      int k = h2(x >> 2, z >> 2, salt[3]) & 3;
      c->top = c->filler = (uint8_t)(k == 0 ? B_GRAVEL : k == 1 ? B_SAND : B_DIRT);
      if (h < SEA - 6 && k == 1) c->top = c->filler = B_GRAVEL;
      break;
    }
    case BIOME_COLD_TAIGA: c->top = B_GRASS_SNOWED, c->filler = B_DIRT; break;
    case BIOME_EXTREME_HILLS:
      c->top = h > SEA + 18 && (h2(x, z, salt[6]) & 3) == 0 ? B_STONE : B_GRASS, c->filler = B_DIRT;
      break;
    default: c->top = B_GRASS, c->filler = B_DIRT;
  }
}

/* caves: a cell is cut out where two smooth 3D noises are both near their middle (worm-like tunnels); the
 * lattice is 16 x 8 x 16 blocks, the noise read once a column at each lattice level and blended down it */
static int cave_x = -32768, cave_z = -32768;
static uint8_t cave_a[WORLD_H / 8 + 1], cave_b[WORLD_H / 8 + 1];
static int noise3_col(int x, int z, int ly, int s) {
  int xi = x >> 4, zi = z >> 4;
  int fxv = smooth((x & 15) << 4), fzv = smooth((z & 15) << 4);
  int a = h3(xi, ly, zi, s), b = h3(xi + 1, ly, zi, s), c = h3(xi, ly, zi + 1, s), d = h3(xi + 1, ly, zi + 1, s);
  int ab = a + (((b - a) * fxv) >> 8), cd = c + (((d - c) * fxv) >> 8);
  return ab + (((cd - ab) * fzv) >> 8);
}
static void cave_column(int x, int z) {
  if (x == cave_x && z == cave_z) return;
  cave_x = x, cave_z = z;
  for (int l = 0; l <= WORLD_H / 8; l++) {
    cave_a[l] = (uint8_t)noise3_col(x, z, l, salt[2] ^ 0x5A);
    cave_b[l] = (uint8_t)noise3_col(x + 7, z + 3, l + 9, salt[4] ^ 0xA5);
  }
}
static bool cave_at(int x, int y, int z) {
  cave_column(x, z);
  int l = y >> 3, f = (y & 7) << 5;
  int a = cave_a[l] + (((cave_a[l + 1] - cave_a[l]) * f) >> 8) - 128;
  int b = cave_b[l] + (((cave_b[l + 1] - cave_b[l]) * f) >> 8) - 128;
  return a * a + b * b < 150;
}

/* ores: a 2 x 2 x 2 group of cells may hold a vein; Minecraft's depths for 128 levels, halved */
static int ore_at(int x, int y, int z) {
  int g = h3(x >> 1, y >> 1, z >> 1, salt[5]);
  int k = h3(x, y, z, salt[1]);
  if (k < 110) return 0;
  if (g < 7) return y < 64 ? B_COAL_ORE : 0;
  if (g < 11) return y < 32 ? B_IRON_ORE : 0;
  if (g < 12) return y < 16 ? B_GOLD_ORE : 0;
  if (g < 14) return y < 8 ? B_REDSTONE_ORE : 0;
  if (g == 14) return y < 8 && (k & 1) ? B_DIAMOND_ORE : 0;
  if (g == 15) return y < 16 && y > 4 && (k & 3) == 0 ? B_LAPIS_ORE : 0;
  if (g == 16 && (k & 1)) return B_GRAVEL;
  if (g == 17 && (k & 1)) return B_DIRT;
  return 0;
}

int gen_cell(int x, int y, int z, const Column *c) {
  int h = c->h;
  if (y == 0) return B_BEDROCK;
  if (y < 4 && (h3(x, y, z, salt[0]) & 3) >= y) return B_BEDROCK;
  if (y > h) {
    if (y < SEA) return c->biome == BIOME_COLD_TAIGA && y == SEA - 1 ? B_ICE : B_WATER;
    return B_AIR;
  }
  /* caves: not right under the sea floor or a lake, and not through the beaches */
  if (y > 3 && (h >= SEA || y < h - 3) && cave_at(x, y, z)) return y < 8 ? B_LAVA : B_AIR;
  if (y == h) {
    if (h < SEA - 1 && c->top == B_GRASS) return B_DIRT;
    return c->top == B_GRASS_SNOWED && h < SEA ? B_DIRT : c->top;
  }
  if (y > h - c->depth) return c->filler;
  if (c->filler == B_SAND && y > h - c->depth - 3) return B_SANDSTONE;
  int o = ore_at(x, y, z);
  return o ? o : B_STONE;
}

void gen_spawn(int *x, int *z) {
  /* near 0, 0: plains or a forest (Minecraft's spawn biomes), else any land that is not a beach */
  Column c;
  for (int pass = 0; pass < 2; pass++)
    for (int r = 0; r < 2048; r += 8)
      for (int k = 0; k < 8; k++) {
        int px = (k & 1 ? r : -r) + (k >> 1) * 3, pz = (k & 2 ? r : -r) / 2 + k;
        gen_column(px, pz, &c);
        if (c.h < SEA || c.biome == BIOME_OCEAN || c.biome == BIOME_BEACH) continue;
        if (pass == 0 && c.biome != BIOME_PLAINS && c.biome != BIOME_FOREST && c.biome != BIOME_BIRCH_FOREST) continue;
        *x = px, *z = pz;
        return;
      }
  *x = 0, *z = 0;
}

/* ---------------------------------------------------------------- trees and plants */
/* a tree may stand in each 4 x 4 square of columns, at a place of its own; how likely, by biome */
static const uint8_t tree_chance[N_BIOMES] = {
  [BIOME_OCEAN] = 0, [BIOME_PLAINS] = 6, [BIOME_DESERT] = 30, [BIOME_EXTREME_HILLS] = 40, [BIOME_FOREST] = 200,
  [BIOME_TAIGA] = 150, [BIOME_BEACH] = 0, [BIOME_BIRCH_FOREST] = 190, [BIOME_COLD_TAIGA] = 90};

static void (*put_fn)(int, int, int, int);
static int rx0, rz0, rx1, rz1, ry0, ry1;
static void put(int x, int y, int z, int b) {
  if (x >= rx0 && x < rx1 && z >= rz0 && z < rz1 && y >= ry0 && y < ry1 && y < WORLD_H) put_fn(x, y, z, b);
}

static void leaves_layer(int x, int y, int z, int r, int b, int corners, int s) {
  for (int dz = -r; dz <= r; dz++)
    for (int dx = -r; dx <= r; dx++) {
      if (r && (dx == r || dx == -r) && (dz == r || dz == -r)) {
        if (corners == 0) continue;
        if (corners == 1 && (h3(x + dx, y, z + dz, s) & 1)) continue;
      }
      put(x + dx, y, z + dz, b | 0x100);   /* (0x100: only into air) */
    }
}

static void tree(int x, int z, const Column *c, int kind, int r) {
  int y = c->h + 1;
  if (kind == B_CACTUS) {
    int n = 1 + (r & 1) + (r >> 7 & 1);
    for (int k = 0; k < n; k++) put(x, y + k, z, B_CACTUS);
    return;
  }
  if (kind == B_LOG_SPRUCE) {
    /* WorldGenTaiga2, simply: a cone of leaves, wider every other layer, on a tall trunk */
    int hgt = 6 + (r & 3), bare = 1 + (r >> 2 & 1);
    for (int k = 0; k < hgt; k++) put(x, y + k, z, B_LOG_SPRUCE);
    put(x, y + hgt, z, B_LEAVES_SPRUCE | 0x100);
    int rad = 0;
    for (int k = hgt - 1; k >= bare; k--) {
      leaves_layer(x, y + k, z, rad, B_LEAVES_SPRUCE, rad == 2 ? 1 : 2, r);
      rad = rad >= 2 || (rad == 1 && k < hgt - 4 && (k & 1)) ? (rad == 2 ? 1 : 2) : rad + 1;
      if (k > hgt - 3) rad = k == hgt - 1 ? 1 : 0;
    }
    return;
  }
  /* WorldGenTrees (oak) and birch: a trunk of 4 to 6, two wide layers and two narrow ones on top */
  int birch = kind == B_LOG_BIRCH;
  int hgt = 4 + (r % 3) + birch;
  int leaf = birch ? B_LEAVES_BIRCH : B_LEAVES_OAK;
  for (int k = 0; k < hgt; k++) put(x, y + k, z, kind);
  for (int k = -3; k <= 0; k++) {
    int rad = k < -1 ? 2 : 1;
    leaves_layer(x, y + hgt + k, z, rad, leaf, k == 0 ? 0 : 1, r + k);
  }
}

void gen_decorate(int x0, int z0, int x1, int z1, int y0, int y1, void (*fn)(int, int, int, int)) {
  put_fn = fn;
  rx0 = x0, rz0 = z0, rx1 = x1, rz1 = z1, ry0 = y0, ry1 = y1;
  Column c;
  /* trees: the 4 x 4 squares whose tree could reach in (two blocks of leaves around a trunk) */
  for (int gz = (z0 - 2) >> 2; gz <= (z1 + 1) >> 2; gz++)
    for (int gx = (x0 - 2) >> 2; gx <= (x1 + 1) >> 2; gx++) {
      int r = h2(gx, gz, salt[3] ^ 0x33);
      int tx = (gx << 2) + (r & 3), tz = (gz << 2) + (r >> 2 & 3);
      if (tx < x0 - 2 || tx >= x1 + 2 || tz < z0 - 2 || tz >= z1 + 2) continue;
      gen_column(tx, tz, &c);
      if (r >= tree_chance[c.biome] || c.h < SEA || c.h > WORLD_H - 12) continue;
      if (c.top != B_GRASS && c.top != B_GRASS_SNOWED && c.top != B_SAND) continue;
      if (cave_at(tx, c.h, tz)) continue;
      int r2 = h2(tx, tz, salt[4]);
      int kind;
      switch (c.biome) {
        case BIOME_DESERT: kind = B_CACTUS; break;
        case BIOME_TAIGA: case BIOME_COLD_TAIGA: kind = B_LOG_SPRUCE; break;
        case BIOME_BIRCH_FOREST: kind = r2 < 200 ? B_LOG_BIRCH : B_LOG_OAK; break;
        case BIOME_FOREST: kind = r2 < 50 ? B_LOG_BIRCH : B_LOG_OAK; break;
        case BIOME_EXTREME_HILLS: kind = r2 < 128 ? B_LOG_SPRUCE : B_LOG_OAK; break;
        default: kind = B_LOG_OAK;
      }
      if ((kind == B_CACTUS) != (c.top == B_SAND)) continue;
      if (c.top == B_GRASS || c.top == B_GRASS_SNOWED) put(tx, c.h, tz, B_DIRT);
      tree(tx, tz, &c, kind, r2);
    }
}

/* the plant on top of a column (0: none) */
int gen_plant(int x, int z, const Column *c) {
  int r = h2(x, z, salt[6] ^ 0x77);
  if (r > 90 || c->h < SEA) return 0;
  if (c->top == B_GRASS) {
    if (c->biome == BIOME_PLAINS) return r < 4 ? B_DANDELION : r < 7 ? B_POPPY : r < 75 ? B_TALL_GRASS : 0;
    if (c->biome == BIOME_FOREST || c->biome == BIOME_BIRCH_FOREST)
      return r < 2 ? B_DANDELION : r < 4 ? B_POPPY : r < 26 ? B_TALL_GRASS : 0;
    return r < 20 ? B_TALL_GRASS : 0;
  }
  if (c->top == B_SAND && c->biome == BIOME_DESERT) return r < 4 ? B_DEAD_BUSH : 0;
  return 0;
}
