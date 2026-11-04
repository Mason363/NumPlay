/* Plays NumBlocks on a computer with scripted keys and saves screenshots.
 *
 *   play [--frames N] [--ms-per-frame M] [--keys "10-40:fwd,50:use,..."] [--shots 30,60] [--out DIR]
 *        [--showcase]   (the hotbar of the README's GIF: TNT, flint and steel, water and lava, blocks)
 *        [--animals]    (a sheep, a cow, a pig and a chicken ahead, on the ground where they stand)
 *        [--bow]        (a bow in the hand, arrows next to it)
 *
 * Keys: left right up down fwd back sleft sright jump use attack inv sneak sprint pause 1..9. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "../src/nb.h"

extern uint32_t host_keys, host_time;
void host_shot(const char *path);
void game_init(void);
bool game_frame(void);

typedef struct { int a, b; uint32_t k; } Hold;
static Hold holds[512];
static int nholds;

static uint32_t key_bit(const char *s) {
  static const struct { const char *n; uint32_t k; } t[] = {
    {"left", K_LEFT}, {"right", K_RIGHT}, {"up", K_UP}, {"down", K_DOWN}, {"fwd", K_FWD}, {"back", K_BACKW},
    {"sleft", K_STRAFE_L}, {"sright", K_STRAFE_R}, {"jump", K_JUMP}, {"use", K_USE}, {"attack", K_ATTACK},
    {"inv", K_INV}, {"sneak", K_SNEAK}, {"sprint", K_SPRINT}, {"pause", K_PAUSE}, {"home", K_HOME},
    {"ok", K_OK | K_USE}, {"exe", K_EXE | K_USE}, {"shift", K_SHIFT}, {"esc", K_BACK | K_ATTACK}, {"drop", K_DROP}};
  for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++)
    if (!strcmp(s, t[i].n)) return t[i].k;
  if (s[0] >= '1' && s[0] <= '9' && !s[1]) return K_SLOT1 << (s[0] - '1');
  return 0;
}

int main(int argc, char **argv) {
  const char *out = "build/play", *shots = "";
  int give = 0, bow = 0, showcase = 0, title_screen = 0, mobs = 0, animals = 0, start_time = -1, torches = 0, water = 0, creative = 0, build = 0, rain = 0;
  float hurt = 0;
  int frames = 60, mspf = 50, go_dim = 0, portal = 0, at = 0, fortress = 0, hold = -1, credits = 0;
  float at_v[5] = {0};
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--ms-per-frame") && i + 1 < argc) mspf = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
    else if (!strcmp(argv[i], "--shots") && i + 1 < argc) shots = argv[++i];
    else if (!strcmp(argv[i], "--give")) give = 1;
    else if (!strcmp(argv[i], "--showcase")) showcase = 1;
    else if (!strcmp(argv[i], "--bow")) bow = 1;
    else if (!strcmp(argv[i], "--title")) title_screen = 1;
    else if (!strcmp(argv[i], "--mobs")) mobs = 1;
    else if (!strcmp(argv[i], "--animals")) animals = 1;
    else if (!strcmp(argv[i], "--torches")) torches = 1;
    else if (!strcmp(argv[i], "--water")) water = 1;
    else if (!strcmp(argv[i], "--creative")) creative = 1;
    else if (!strcmp(argv[i], "--build")) build = 1;
    else if (!strcmp(argv[i], "--rain")) rain = 1;
    else if (!strcmp(argv[i], "--nether")) go_dim = 1;
    else if (!strcmp(argv[i], "--end")) go_dim = 2;
    else if (!strcmp(argv[i], "--portal")) portal = 1;
    else if (!strcmp(argv[i], "--fortress")) go_dim = 1, fortress = 1;
    else if (!strcmp(argv[i], "--stronghold") && i + 1 < argc) hold = atoi(argv[++i]);   /* 0-2: in its portal room */
    else if (!strcmp(argv[i], "--credits")) credits = 1;   /* the End poem and the credits, as after the End */
    else if (!strcmp(argv[i], "--at") && i + 1 < argc)   /* x,y,z,yaw,pitch: there, flying (Creative) */
      at = sscanf(argv[++i], "%f,%f,%f,%f,%f", &at_v[0], &at_v[1], &at_v[2], &at_v[3], &at_v[4]) >= 3;
    else if (!strcmp(argv[i], "--storm")) rain = 2;
    else if (!strcmp(argv[i], "--time") && i + 1 < argc) start_time = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--seed") && i + 1 < argc) {
      extern int64_t start_seed;
      start_seed = strtoll(argv[++i], NULL, 10);
    }
    else if (!strcmp(argv[i], "--hurt") && i + 1 < argc) hurt = (float)atof(argv[++i]);
    else if (!strcmp(argv[i], "--keys") && i + 1 < argc) {
      char *s = strdup(argv[++i]);
      for (char *t = strtok(s, ","); t && nholds < 512; t = strtok(NULL, ",")) {
        int a, b;
        char k[16];
        if (sscanf(t, "%d-%d:%15s", &a, &b, k) == 3) holds[nholds++] = (Hold){a, b, key_bit(k)};
        else if (sscanf(t, "%d:%15s", &a, k) == 2) holds[nholds++] = (Hold){a, a, key_bit(k)};
      }
    }
  }
  mkdir(out, 0755);
  {
    /* the saves (the edit records portals are looked for in) next to the pictures */
    extern void host_save_dir(const char *d);
    static char sd[600];
    snprintf(sd, sizeof sd, "%s/saves", out);
    mkdir(sd, 0755);
    host_save_dir(sd);
  }
  clock_t c0 = clock();
  extern bool start_in_world;
  start_in_world = !title_screen;
  game_init();
  if (give) {
    /* a test kit: tools, blocks, food, armour */
    static const int kit[][2] = {{I_IRON_PICKAXE, 1}, {I_DIAMOND_SWORD, 1}, {B_COBBLESTONE, 64}, {B_PLANKS_OAK, 23},
                                 {B_TORCH, 16}, {I_BREAD, 5}, {B_CRAFTING_TABLE, 1}, {B_FURNACE, 1}, {B_CHEST, 1},
                                 {B_LOG_OAK, 12}, {I_COAL, 9}, {B_IRON_ORE, 7}, {I_IRON_HELMET, 1},
                                 {I_IRON_CHESTPLATE, 1}, {I_STICK, 10}, {I_WHEAT_SEEDS, 3}, {I_WOODEN_HOE, 1},
                                 {I_BUCKET, 1}, {B_SAND, 20}, {I_APPLE, 2}};
    for (unsigned i = 0; i < sizeof kit / sizeof kit[0]; i++) inv_add(pl.inv, 36, kit[i][0], kit[i][1], 0);
    pl.inv[0].aux = 100;   /* a worn pickaxe */
  }
  if (bow) {
    pl.inv[0] = (Stack){I_BOW, 0}, pl.slot = 0;
    inv_add(pl.inv, 36, I_ARROW, 64, 0);
  }
  if (showcase) {
    /* the README's GIF: the hotbar first, then a full inventory to look at */
    static const int kit[][2] = {{B_TNT, 16}, {I_FLINT_AND_STEEL, 1}, {I_WATER_BUCKET, 1}, {I_LAVA_BUCKET, 1},
                                 {B_COBBLESTONE, 64}, {B_PLANKS_OAK, 48}, {B_GLASS, 32}, {I_DIAMOND_PICKAXE, 1},
                                 {B_TORCH, 32}, {I_DIAMOND_SWORD, 1}, {I_BOW, 1}, {I_ARROW, 48}, {I_DIAMOND, 12},
                                 {I_GOLD_INGOT, 21}, {I_IRON_INGOT, 30}, {I_COOKED_BEEF, 9}, {I_BREAD, 7},
                                 {I_APPLE, 4}, {B_OBSIDIAN, 10}, {B_DIAMOND_BLOCK, 2}, {B_LOG_OAK, 24},
                                 {B_CRAFTING_TABLE, 1}, {B_FURNACE, 2}, {B_CHEST, 3}, {I_IRON_HELMET, 1},
                                 {I_IRON_CHESTPLATE, 1}, {B_SAND, 40}};
    for (unsigned i = 0; i < sizeof kit / sizeof kit[0]; i++) inv_add(pl.inv, 36, kit[i][0], kit[i][1], 0);
  }
  if (start_time >= 0) {
    extern uint32_t game_time;
    game_time = (uint32_t)start_time;
  }
  if (creative) pl.mode = 1;
  if (portal) {
    /* an obsidian frame ahead (along x), lit with flint and steel; the player walks in with --keys */
    int fy = (int)pl.y, px = (int)floorf(pl.x), pz = (int)floorf(pl.z) + 3;
    for (int z = pz - 2; z <= pz + 2; z++)
      for (int x = px - 3; x <= px + 3; x++)
        for (int y = fy - 1; y < fy + 6; y++) world_set(x, y, z, y == fy - 1 ? B_STONE : B_AIR);
    for (int x = px - 1; x <= px + 2; x++)
      for (int y = fy - 1; y <= fy + 3; y++)
        if (x == px - 1 || x == px + 2 || y == fy - 1 || y == fy + 3) world_set(x, y, pz, B_OBSIDIAN);
    fire_set(px, fy, pz, 0);
  }
  if (go_dim) dims_go(go_dim);
  if (fortress) {
    /* to the nearest fortress, flying over its first crossing */
    int f[4];
    if (gen_fortress_find((int)pl.x, (int)pl.z, f)) {
      printf("fortress at %d %d %d, %d pieces\n", f[0], f[1], f[2], f[3]);
      at = 1, at_v[0] = f[0] + 0.5f, at_v[1] = (float)f[1], at_v[2] = f[2] + 0.5f, at_v[3] = pl.yaw, at_v[4] = 20;
    }
  }
  if (hold >= 0) {
    /* stronghold n: in its portal room, looking at the frame (and where an eye of ender would fly) */
    int f[6], e[3] = {0};
    if (gen_stronghold_find(hold, f)) {
      bool ok = gen_stronghold_near(f[0], f[1], e);
      printf("stronghold %d: start %d %d, portal %d %d %d, %d pieces; an eye flies to %d %d %d (%d)\n", hold, f[0], f[1],
             f[2], f[3], f[4], f[5], e[0], e[1], e[2], ok);
      if (!at) at = 1, at_v[0] = f[2] + 0.5f, at_v[1] = f[3] + 0.5f, at_v[2] = f[4] + 0.5f, at_v[3] = pl.yaw, at_v[4] = 50;
    }
  }
  if (at) {
    pl.x = at_v[0], pl.y = at_v[1], pl.z = at_v[2], pl.yaw = at_v[3], pl.pitch = at_v[4];
    pl.mode = 1, pl.flying = true;
    world_finish(pl.x, pl.y, pl.z);
    camera_reset();
  }
  if (hold >= 0) {
    printf("column under the player:");
    for (int y = (int)pl.y + 2; y > (int)pl.y - 12; y--) printf(" %d", world_get((int)floorf(pl.x), y, (int)floorf(pl.z)));
    printf("\n");
  }
  if (go_dim) printf("at %.2f %.2f %.2f: feet %d head %d below %d (cache y %d)\n", pl.x, pl.y, pl.z, world_get((int)floorf(pl.x), (int)floorf(pl.y), (int)floorf(pl.z)), world_get((int)floorf(pl.x), (int)floorf(pl.y) + 1, (int)floorf(pl.z)), world_get((int)floorf(pl.x), (int)floorf(pl.y) - 1, (int)floorf(pl.z)), vc_y0);
  if (credits) credits_open();
  if (rain) {
    /* raining (and storming) from the start, for long enough */
    weather.raining = 1, weather.rain_time = 24000, rain_str = 1;
    if (rain == 2) weather.thundering = 1, weather.thunder_time = 24000, thunder_str = 1;
  }
  if (build) {
    /* a cleared field with the shaped blocks in a row: stairs, door, bed, chest, furnace, fence, pane, ladder */
    int fy = (int)pl.y, px = (int)pl.x, pz = (int)pl.z;
    for (int z = pz - 3; z < pz + 12; z++)
      for (int x = px - 9; x < px + 10; x++)
        for (int y = fy - 1; y < fy + 8; y++) world_set(x, y, z, y == fy - 1 ? B_GRASS : B_AIR);
    int z = pz + 4;
    world_set(px + 5, fy, z, B_OAK_STAIRS);
    world_set(px + 4, fy, z, B_COBBLESTONE_STAIRS_S);
    world_set(px + 2, fy, z, B_DOOR_OAK_LOWER_N), world_set(px + 2, fy + 1, z, B_DOOR_OAK_UPPER);
    world_set(px + 1, fy, z, B_DOOR_OAK_LOWER_N_OPEN), world_set(px + 1, fy + 1, z, B_DOOR_OAK_UPPER);
    world_set(px - 1, fy, z, B_BED_FOOT), world_set(px - 1, fy, z + 1, B_BED_HEAD);
    world_set(px - 3, fy, z, B_CHEST);
    world_set(px - 4, fy, z, B_FURNACE);
    for (int k = 0; k < 3; k++) world_set(px - 6 - k, fy, z, B_FENCE_OAK);
    world_set(px - 6, fy, z + 1, B_FENCE_OAK);
    for (int k = 0; k < 2; k++) world_set(px + 7 + k, fy, z, B_GLASS_PANE), world_set(px + 7 + k, fy + 1, z, B_GLASS_PANE);
    world_set(px + 3, fy, z + 2, B_COBBLESTONE), world_set(px + 3, fy + 1, z + 2, B_COBBLESTONE);
    world_set(px + 3, fy, z + 1, B_LADDER), world_set(px + 3, fy + 1, z + 1, B_LADDER);
    world_set(px - 2, fy + 1, z + 3, B_TORCH_N);
    world_set(px - 2, fy + 1, z + 4, B_COBBLESTONE);
  }
  if (water) {
    /* a cleared field with a water source on a step */
    int fy = (int)pl.y;
    for (int z = (int)pl.z - 3; z < (int)pl.z + 12; z++)
      for (int x = (int)pl.x - 9; x < (int)pl.x + 10; x++)
        for (int y = fy - 1; y < fy + 8; y++) world_set(x, y, z, y == fy - 1 ? B_GRASS : B_AIR);
    world_set((int)pl.x, fy, (int)pl.z + 6, B_COBBLESTONE);
    world_set((int)pl.x, fy + 1, (int)pl.z + 6, B_WATER);
    neighbours_changed((int)pl.x, fy + 1, (int)pl.z + 6);
  }
  if (mobs) {
    /* a row of every mob, 4 blocks ahead, facing the player, on a cleared grass field */
    int fy = (int)pl.y;
    for (int z = (int)pl.z - 3; z < (int)pl.z + 9; z++)
      for (int x = (int)pl.x - 9; x < (int)pl.x + 10; x++)
        for (int y = fy - 1; y < fy + 8; y++) world_set(x, y, z, y == fy - 1 ? B_GRASS : B_AIR);
    for (int k = 0; k < 8; k++) {
      float x = pl.x - 5.6f + k * 1.6f, z = pl.z + 4.5f;
      Entity *e = ent_new(E_ZOMBIE + k, x, (float)fy, z);
      if (e) e->health = 20, e->yaw = 180, e->gx = e->x, e->gz = e->z;
    }
    if (dim) {
      /* the Nether's and the End's instead */
      memset(ents, 0, sizeof ents);
      static const int8_t t[6] = {E_PIGMAN, E_WSKELETON, E_BLAZE, E_MAGMA, E_ENDERMAN, E_CRYSTAL};
      for (int k = 0; k < 6; k++) {
        Entity *e = ent_new(t[k], pl.x - 5.0f + k * 2.0f, (float)fy, pl.z + 5.5f);
        if (e) e->health = 20, e->yaw = 180, e->gx = e->x, e->gz = e->z, e->sheared = 2;
      }
      Entity *g = ent_new(E_GHAST, pl.x, (float)fy + 2, pl.z + 14);
      if (g) g->health = 10, g->yaw = 180, g->delay = 1000;
    }
  }
  if (animals) {
    /* a few animals ahead of the player (forward f, right r blocks), each on the first solid ground under it, facing
     * the player */
    float a = pl.yaw * 3.14159265f / 180, fx = -sinf(a), fz = cosf(a), rx = -cosf(a), rz = -sinf(a);
    static const struct { int type; float f, r; } herd[] = {
      {E_SHEEP, 5.2f, -1.7f}, {E_COW, 7.5f, 1.2f}, {E_PIG, 4.6f, 1.9f}, {E_CHICKEN, 3.6f, -0.6f}};
    for (unsigned k = 0; k < sizeof herd / sizeof herd[0]; k++) {
      float x = pl.x + fx * herd[k].f + rx * herd[k].r, z = pl.z + fz * herd[k].f + rz * herd[k].r;
      for (int y = (int)pl.y + 8; y > (int)pl.y - 12; y--) {
        float box[6] = {x - 0.3f, y - 0.5f, z - 0.3f, x + 0.3f, y - 0.01f, z + 0.3f};
        if (phys_free(box)) continue;
        Entity *e = ent_new(herd[k].type, x, (float)y, z);
        if (e) e->health = 10, e->yaw = pl.yaw + 180 + (float)(k * 37 % 60) - 30, e->gx = e->x, e->gz = e->z;
        break;
      }
    }
  }
  if (torches) {
    /* torches around, for the night */
    int fy = (int)pl.y;
    for (int k = 0; k < 3; k++) world_set((int)pl.x - 3 + k * 3, fy, (int)pl.z + 3 + k, B_TORCH);
    world_set((int)pl.x + 2, fy, (int)pl.z + 6, B_GLOWSTONE);
  }
  if (hurt > 0) pl.health -= hurt, pl.food -= 7, pl.xp_level = 7, pl.xp = 0.4f;
  double init_ms = (clock() - c0) * 1000.0 / CLOCKS_PER_SEC;
  c0 = clock();
  double slowest = 0;
  int slowest_at = 0;
  for (int f = 0; f < frames; f++) {
    host_keys = 0;
    for (int i = 0; i < nholds; i++)
      if (f >= holds[i].a && f <= holds[i].b) host_keys |= holds[i].k;
    host_time += mspf;
    clock_t f0 = clock();
    if (!game_frame()) break;
    double fms = (clock() - f0) * 1000.0 / CLOCKS_PER_SEC;
    if (fms > slowest) slowest = fms, slowest_at = f;
    for (const char *s = shots; *s;) {
      if (atoi(s) == f) {
        char p[512];
        snprintf(p, sizeof p, "%s/shot_%d.ppm", out, f);
        host_shot(p);
      }
      const char *c = strchr(s, ',');
      if (!c) break;
      s = c + 1;
    }
  }
  for (int i = 0; i < N_ENT; i++)
    if (ents[i].type > E_BOBBER)
      printf("entity %d at %.1f %.1f %.1f health %d\n", ents[i].type, ents[i].x, ents[i].y, ents[i].z, ents[i].health);
  extern unsigned long st_steps, st_texels, st_jumps, st_pixels;
  printf("per frame: %.0f traces, %.0f steps\n", (double)st_pixels / frames, (double)st_steps / frames);
  extern unsigned long st_dis, st_k0, st_fpfail, st_fpok, st_sky;
  printf("between per frame: %.0f disagree, %.0f kind0, %.0f face fail, %.0f face ok, %.0f sky\n", (double)st_dis / frames, (double)st_k0 / frames, (double)st_fpfail / frames, (double)st_fpok / frames, (double)st_sky / frames);
  printf("per pixel: %.1f steps, %.2f texels, %.2f jumps\n", (double)st_steps / st_pixels, (double)st_texels / st_pixels, (double)st_jumps / st_pixels);
  printf("init %.1f ms, %.2f ms a frame (host), the slowest %.1f ms (frame %d)\n", init_ms,
         (clock() - c0) * 1000.0 / CLOCKS_PER_SEC / frames, slowest, slowest_at);
  return 0;
}
