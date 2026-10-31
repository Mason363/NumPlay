/* Checks of the item rules: crafting, stacking, smelting, drops. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include "nb.h"
#include "edits.h"
#include "gen.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static int craft(const int *ids, int w) {
  Stack g[9] = {{0}};
  for (int i = 0; i < w * w; i++) g[i].id = (uint16_t)ids[i], g[i].aux = ids[i] ? 1 : 0;
  int r = craft_find(g, w);
  return r < 0 ? 0 : recipes[r].out * 100 + recipes[r].n;
}

int main(void) {
  /* planks from a log, anywhere in the grid */
  int a[4] = {0, B_LOG_BIRCH, 0, 0};
  CHECK(craft(a, 2) == B_PLANKS_BIRCH * 100 + 4);
  /* sticks from any planks */
  int b[4] = {B_PLANKS_JUNGLE, 0, B_PLANKS_OAK, 0};
  CHECK(craft(b, 2) == I_STICK * 100 + 4);
  /* crafting table */
  int c[4] = {B_PLANKS_OAK, B_PLANKS_SPRUCE, B_PLANKS_OAK, B_PLANKS_OAK};
  CHECK(craft(c, 2) == B_CRAFTING_TABLE * 100 + 1);
  /* a pickaxe needs the 3 x 3 grid */
  int p[9] = {B_COBBLESTONE, B_COBBLESTONE, B_COBBLESTONE, 0, I_STICK, 0, 0, I_STICK, 0};
  CHECK(craft(p, 3) == I_STONE_PICKAXE * 100 + 1);
  /* an axe, mirrored */
  int x1[9] = {I_IRON_INGOT, I_IRON_INGOT, 0, I_IRON_INGOT, I_STICK, 0, 0, I_STICK, 0};
  int x2[9] = {0, I_IRON_INGOT, I_IRON_INGOT, 0, I_STICK, I_IRON_INGOT, 0, I_STICK, 0};
  CHECK(craft(x1, 3) == I_IRON_AXE * 100 + 1);
  CHECK(craft(x2, 3) == I_IRON_AXE * 100 + 1);
  /* torches from coal or charcoal */
  int t1[4] = {I_COAL, 0, I_STICK, 0}, t2[4] = {0, I_CHARCOAL, 0, I_STICK};
  CHECK(craft(t1, 2) == B_TORCH * 100 + 4);
  CHECK(craft(t2, 2) == B_TORCH * 100 + 4);
  /* shapeless: stew, in any order */
  int s[9] = {I_BOWL, 0, 0, 0, B_RED_MUSHROOM, 0, 0, 0, B_BROWN_MUSHROOM};
  CHECK(craft(s, 3) == I_MUSHROOM_STEW * 100 + 1);
  /* nothing */
  int n[4] = {B_DIRT, 0, 0, 0};
  CHECK(craft(n, 2) == 0);
  /* a furnace and a chest */
  int f[9] = {B_COBBLESTONE, B_COBBLESTONE, B_COBBLESTONE, B_COBBLESTONE, 0, B_COBBLESTONE, B_COBBLESTONE,
              B_COBBLESTONE, B_COBBLESTONE};
  CHECK(craft(f, 3) == B_FURNACE * 100 + 1);
  int bed[9] = {B_WOOL_RED, B_WOOL_WHITE, B_WOOL_RED, B_PLANKS_OAK, B_PLANKS_BIRCH, B_PLANKS_OAK, 0, 0, 0};
  CHECK(craft(bed, 3) == I_BED * 100 + 1);
  /* stacking: 64 at most, the same items first */
  Stack inv[36] = {{0}};
  CHECK(inv_add(inv, 36, B_DIRT, 100, 0) == 0);
  CHECK(inv[0].id == B_DIRT && inv[0].aux == 64 && inv[1].aux == 36);
  CHECK(inv_add(inv, 36, I_IRON_PICKAXE, 1, 5) == 0 && inv[2].id == I_IRON_PICKAXE && inv[2].aux == 5);
  CHECK(inv_add(inv, 36, B_DIRT, 30, 0) == 0 && inv[1].aux == 64 && inv[3].aux == 2);
  CHECK(inv_add(inv, 36, I_EGG, 20, 0) == 0 && inv[4].aux == 16 && inv[5].aux == 4);
  /* smelting */
  CHECK(smelt_of(B_IRON_ORE) == I_IRON_INGOT);
  CHECK(smelt_of(B_LOG_ACACIA) == I_CHARCOAL);
  CHECK(smelt_of(B_DIRT) == 0);
  CHECK(item_fuel(I_COAL) == 1600 && item_fuel(B_PLANKS_OAK) == 300 && item_fuel(I_STICK) == 100);
  /* harvesting */
  CHECK(!can_harvest(B_STONE, 0) && can_harvest(B_STONE, I_WOODEN_PICKAXE));
  CHECK(!can_harvest(B_DIAMOND_ORE, I_STONE_PICKAXE) && can_harvest(B_DIAMOND_ORE, I_IRON_PICKAXE));
  CHECK(can_harvest(B_DIRT, 0) && dig_speed(B_DIRT, I_DIAMOND_SHOVEL) == 8 && dig_speed(B_DIRT, I_DIAMOND_PICKAXE) == 1);
  Stack out[2];
  CHECK(block_drops(B_STONE, I_WOODEN_PICKAXE, out) == 1 && out[0].id == B_COBBLESTONE);
  CHECK(block_drops(B_STONE, 0, out) == 0);
  CHECK(block_drops(B_GRASS, 0, out) == 1 && out[0].id == B_DIRT);
  CHECK(block_drops(B_LOG_OAK_X, 0, out) == 1 && out[0].id == B_LOG_OAK);
  CHECK(block_drops(B_COAL_ORE, I_GOLDEN_PICKAXE, out) == 1 && out[0].id == I_COAL);
  CHECK(block_drops(B_TORCH_E, 0, out) == 1 && out[0].id == B_TORCH);
  /* changes survive: through the journal, into storage, back after a reload */
  {
    extern void host_save_dir(const char *d);
    host_save_dir("build/unit-saves");
    remove("build/unit-saves/ut1r0_0.nbe");
    remove("build/unit-saves/ut1r-1_0.nbe");
    world_new(0, "ut1");
    world_follow(8, 70, 8);
    int base = world_get(5, 62, 5);
    world_set(5, 62, 5, B_GOLD_BLOCK);
    world_set(-3, 63, 7, B_GLASS);
    for (int i = 0; i < 300; i++) world_set(i % 20 - 5, 61 + i / 20, 12, B_DIAMOND_BLOCK);   /* more than the journal */
    CHECK(world_get(5, 62, 5) == B_GOLD_BLOCK);
    CHECK(edits_flush());
    world_new(0, "ut1");
    world_follow(8, 70, 8);
    CHECK(world_get(5, 62, 5) == B_GOLD_BLOCK);
    CHECK(world_get(-3, 63, 7) == B_GLASS);
    CHECK(world_get(14, 75, 12) == B_DIAMOND_BLOCK && world_get(-5, 61, 12) == B_DIAMOND_BLOCK);
    world_set(5, 62, 5, base);
    CHECK(edits_flush());
    world_new(0, "ut1");
    world_follow(8, 70, 8);
    CHECK(world_get(5, 62, 5) == base);
  }
  /* a region with thousands of changes (TNT craters) still saves: its record grows where it is */
  {
    remove("build/unit-saves/ut4r0_0.nbe");
    world_new(0, "ut4");
    world_follow(8, 70, 8);
    for (int y = 66; y < 74; y++)
      for (int z = 0; z < 20; z++)
        for (int x = 0; x < 20; x++) world_set(x, y, z, (x + z + y) & 1 ? B_GLASS : B_GOLD_BLOCK);
    CHECK(edits_flush());
    world_set(3, 70, 4, B_DIAMOND_BLOCK);
    CHECK(edits_flush());
    world_new(0, "ut4");
    world_follow(8, 70, 8);
    CHECK(world_get(0, 66, 0) == B_GOLD_BLOCK && world_get(19, 73, 19) == B_GLASS);
    CHECK(world_get(3, 70, 4) == B_DIAMOND_BLOCK && world_get(4, 70, 4) == B_GOLD_BLOCK);
    uint32_t len = 0;
    CHECK(plat_load("ut4r0_0.nbe", &len) && len == 20 * 20 * 8 * 4);
    plat_remove_prefix("ut4");
  }
  /* water: 7 blocks out on a flat floor, then dry when the source is gone; lava meets water */
  {
    world_new(0, "ut2");
    world_follow(8, 70, 8);
    int y = 70;
    for (int z = -12; z <= 12; z++)
      for (int x = -12; x <= 12; x++) {
        world_set(x + 8, y - 1, z + 8, B_STONE);
        for (int k = 0; k < 3; k++) world_set(x + 8, y + k, z + 8, B_AIR);
      }
    world_set(8, y, 8, B_WATER);
    neighbours_changed(8, y, 8);
    for (int t = 0; t < 400; t++) ticks_run++, world_tick();
    CHECK(world_get(9, y, 8) == B_FLOWING_WATER);
    CHECK(world_get(15, y, 8) == B_FLOWING_WATER_7);
    CHECK(world_get(16, y, 8) == B_AIR);
    CHECK(world_get(11, y, 10) == B_FLOWING_WATER_5);
    world_set(8, y, 8, B_AIR);
    neighbours_changed(8, y, 8);
    for (int t = 0; t < 600; t++) ticks_run++, world_tick();
    CHECK(world_get(9, y, 8) == B_AIR && world_get(12, y, 8) == B_AIR);
    /* lava source next to water: obsidian */
    world_set(4, y, 4, B_WATER);
    world_set(5, y, 4, B_LAVA);
    neighbours_changed(5, y, 4);
    for (int t = 0; t < 60; t++) ticks_run++, world_tick();
    CHECK(world_get(5, y, 4) == B_OBSIDIAN);
    plat_remove_prefix("ut2");
  }
  /* lava that has flowed sets fire by it, and the fire lights TNT (BlockStaticLiquid, BlockFire) */
  {
    world_new(0, "ut5");
    world_follow(8, 70, 8);
    int y = 70;
    for (int z = 4; z <= 12; z++)
      for (int x = 4; x <= 12; x++) {
        world_set(x, y - 1, z, B_STONE);
        for (int k = 0; k < 3; k++) world_set(x, y + k, z, B_AIR);
      }
    world_set(8, y, 8, B_FLOWING_LAVA);
    world_set(9, y, 8, B_TNT);
    int t = 0;
    while (t < 400000 && world_get(9, y, 8) == B_TNT) ticks_run++, world_tick(), t++;
    CHECK(world_get(9, y, 8) != B_TNT);
    plat_remove_prefix("ut5");
  }
  /* explosions as Explosion's rays: TNT walled in bedrock leaves what is past it, obsidian takes a blast that
   * takes stone, a wall keeps the player behind it from harm (getBlockDensity) */
  {
    world_new(0, "ut6");
    world_follow(8, 70, 8);
    int y = 70;
    for (int z = 0; z <= 16; z++)
      for (int x = 0; x <= 16; x++)
        for (int k = -3; k <= 5; k++) world_set(x, y + k, z, k < 0 ? B_STONE : B_AIR);
    for (int dz = -2; dz <= 2; dz++)
      for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++)
          world_set(8 + dx, y + 1 + dy, 8 + dz, abs(dx) == 2 || abs(dy) == 2 || abs(dz) == 2 ? B_STONE : B_BEDROCK);
    world_set(8, y + 1, 8, B_AIR);
    explode(8.5f, y + 1.06125f, 8.5f, 4, true);
    int left = 0;
    for (int dz = -2; dz <= 2; dz++)
      for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++) left += world_get(8 + dx, y + 1 + dy, 8 + dz) != B_AIR;
    CHECK(left == 5 * 5 * 5 - 1);
    /* in the open: stone next to it goes, obsidian stays, and a wall of obsidian shields stone behind it */
    for (int z = 0; z <= 16; z++)
      for (int x = 0; x <= 16; x++)
        for (int k = -3; k <= 5; k++) world_set(x, y + k, z, k < 0 ? B_STONE : B_AIR);
    world_set(9, y, 8, B_OBSIDIAN);
    world_set(7, y, 8, B_STONE);
    for (int k = 0; k <= 3; k++)
      for (int dz = -3; dz <= 3; dz++) world_set(5, y + k, 8 + dz, B_OBSIDIAN);
    world_set(4, y, 8, B_DIRT);
    explode(8.5f, y + 0.06125f, 8.5f, 4, true);
    CHECK(world_get(8, y - 1, 8) == B_AIR && world_get(7, y, 8) == B_AIR);
    CHECK(world_get(9, y, 8) == B_OBSIDIAN && world_get(5, y, 8) == B_OBSIDIAN && world_get(4, y, 8) == B_DIRT);
    CHECK(world_get(8, y - 3, 1) == B_STONE && world_get(15, y - 1, 15) == B_STONE);
    /* the player: behind the wall only the 1 every blast in reach does, in the open hurt and pushed away */
    pl.mode = 0, pl.dead = false, pl.invuln = 0, pl.health = 20, opt.difficulty = 2;
    pl.x = 3.5f, pl.y = (float)y, pl.z = 8.5f, pl.vx = pl.vy = pl.vz = 0;
    explode(8.5f, y + 0.06125f, 8.5f, 4, false);
    CHECK(pl.health == 19 && pl.vx == 0);
    pl.x = 8.5f, pl.z = 11.5f, pl.y = (float)y;
    explode(8.5f, y + 0.06125f, 8.5f, 4, false);
    CHECK(pl.health <= 0 && pl.vz > 0.5f);
    plat_remove_prefix("ut6");
  }
  /* two cows fed wheat make a calf (EntityAIMate) */
  {
    extern uint32_t game_time;
    world_new(0, "ut3");
    world_follow(8, 70, 8);
    int y = 70;
    for (int z = 0; z <= 16; z++)
      for (int x = 0; x <= 16; x++) {
        world_set(x, y - 1, z, B_STONE);
        for (int k = 0; k < 3; k++) world_set(x, y + k, z, B_AIR);
      }
    memset(ents, 0, sizeof ents);
    opt.difficulty = 0;   /* (no monsters) */
    Entity *a = ent_new(E_COW, 6.5f, (float)y, 8.5f), *b = ent_new(E_COW, 9.5f, (float)y, 8.5f);
    a->health = b->health = 10;
    pl.inv[0].id = I_WHEAT, pl.inv[0].aux = 5, pl.slot = 0, pl.mode = 0;
    CHECK(mob_use(a) && mob_use(b) && pl.inv[0].aux == 3);
    int calves = 0;
    for (int t = 0; t < 400; t++) ents_tick(), game_time++;
    for (int i = 0; i < N_ENT; i++)
      if (ents[i].type == E_COW && ents[i].growth < 0) calves++;
    CHECK(calves == 1);
    plat_remove_prefix("ut3");
  }
  /* a zombie after the player climbs a one-block step (the jump's first rise is the whole 0.42) */
  {
    extern uint32_t game_time;
    world_new(0, "ut3z");
    world_follow(8, 70, 8);
    int y = 70;
    for (int z = 0; z <= 16; z++)
      for (int x = 0; x <= 16; x++) {
        world_set(x, y - 1, z, B_STONE);
        world_set(x, y, z, x >= 10 ? B_STONE : B_AIR);
        for (int k = 1; k < 4; k++) world_set(x, y + k, z, B_AIR);
      }
    memset(ents, 0, sizeof ents);
    opt.difficulty = 2, pl.mode = 0, pl.dead = false, pl.health = 20;
    pl.x = 12.5f, pl.y = (float)y + 1, pl.z = 8.5f;
    game_time = 18000;   /* (midnight: no burning) */
    Entity *zb = ent_new(E_ZOMBIE, 5.5f, (float)y, 8.5f);
    zb->health = 20, zb->panic = 60;
    for (int t = 0; t < 100; t++) ticks_run++, ents_tick(), game_time++;
    CHECK(zb->type == E_ZOMBIE && zb->y >= y + 1 && zb->x > 10);
    plat_remove_prefix("ut3z");
  }
  {
    /* weather: the clock, the strength, the light, and where rain falls */
    extern uint32_t game_time;
    world_new(0, "ut4");
    world_follow(8, 110, 8);   /* (high above the ground: only the test's blocks under the sky) */
    for (int i = 0; i < VCX * VCZ; i++) vbiome[i] = 1;   /* plains */
    memset(&weather, 0, sizeof weather);
    rain_str = thunder_str = 0;
    game_time = 6000;   /* noon */
    CHECK(sky_sub() == 0);
    world_tick();
    CHECK(!weather.raining && weather.rain_time >= 12000 - 1 && weather.rain_time < 180000);
    weather.rain_time = 1;
    world_tick();
    CHECK(weather.raining && rain_str > 0 && rain_str < 0.05f);
    for (int t = 0; t < 120; t++) world_tick();
    CHECK(rain_str == 1 && sky_sub() == 3);   /* rain alone: still day, no sleeping */
    weather.thundering = 1, thunder_str = 1;
    CHECK(sky_sub() >= 4);                    /* a storm: dark enough to sleep */
    int y = 110;
    for (int z = 0; z <= 4; z++)
      for (int x = 0; x <= 4; x++) {
        world_set(x, y - 1, z, B_STONE);
        for (int k = 0; k < 6; k++) world_set(x, y + k, z, B_AIR);
      }
    CHECK(rain_at(2, y, 2));
    world_set(2, y + 3, 2, B_LEAVES_OAK);      /* leaves keep it off */
    CHECK(!rain_at(2, y, 2) && world_rain_top(2, 2) == y + 4);
    world_set(3, y, 3, B_TALL_GRASS);          /* plants do not */
    CHECK(world_rain_top(3, 3) == y);
    vbiome[(2 - vc_z0) * VCX + 1 - vc_x0] = 2;  /* deserts get none */
    CHECK(!rain_at(1, y, 2));
    vbiome[(2 - vc_z0) * VCX + 1 - vc_x0] = 12; /* snowy biomes get snow */
    CHECK(!rain_at(1, y, 2));
    /* a bolt on the player: 5 damage and fire, and the sky flashes */
    pl.x = 2.5f, pl.y = (float)y, pl.z = 2.5f, pl.mode = 0, pl.dead = false, pl.health = 20, pl.invuln = 0, pl.fire = 0;
    memset(pl.armor, 0, sizeof pl.armor);
    bolt = (Bolt){2.5f, (float)y, 2.5f, 2, 0, 1, 7};
    world_tick();
    CHECK(pl.health == 15 && pl.fire > 0 && last_bolt == 2 && bolt.on);
    for (int t = 0; t < 3; t++) world_tick();
    CHECK(!bolt.on);
    weather_clear();
    world_tick();
    CHECK(!weather.raining && !weather.thundering);
    plat_remove_prefix("ut4");
  }
  {
    /* fishing: cast into a pond, wait for a bite, reel it in */
    world_new(0, "ut5");
    world_follow(8, 70, 8);
    memset(&weather, 0, sizeof weather);
    rain_str = thunder_str = 0;
    int y = 70;
    for (int z = 0; z <= 16; z++)
      for (int x = 0; x <= 16; x++)
        for (int k = -3; k < 5; k++) world_set(x, y + k, z, k >= 0 ? B_AIR : z >= 6 && k >= -2 ? B_WATER : B_STONE);
    memset(ents, 0, sizeof ents);
    memset(pl.inv, 0, sizeof pl.inv);
    pl.x = 8.5f, pl.y = (float)y, pl.z = 2.5f, pl.yaw = 0, pl.pitch = 30, pl.dead = false, pl.mode = 0;
    pl.inv[0].id = I_FISHING_ROD, pl.slot = 0;
    fish_cast();
    Entity *b = bobber();
    CHECK(b != NULL);
    int t = 0;
    while (t < 3000 && (b = bobber()) && b->timer <= 0) ents_tick(), t++;
    CHECK(b && b->timer > 0 && is_water(world_get((int)b->x, (int)b->y, (int)b->z)));
    CHECK(fish_reel(b) == 1 && !bobber());
    for (int k = 0; k < 60; k++) ents_tick();
    int caught = 0;
    for (int i = 1; i < 36; i++) caught += pl.inv[i].id != 0;
    CHECK(caught == 1);
    plat_remove_prefix("ut5");
  }
  {
    /* the picture, straight up and straight down at every turn: no ray may get stuck
     * (an empty-region jump once handed a ray back and forth forever); the alarm ends a hang */
    world_new(0, "ut6");
    world_follow(8, 70, 8);
    alarm(60);
    for (int p = -90; p <= 90; p += 180)
      for (float yaw = -180; yaw < 180; yaw += 7.5f) {
        Camera c = {8.5f, 71.62f, 8.5f, yaw, (float)p};
        render_frame(&c, 6000);
      }
    alarm(0);
    plat_remove_prefix("ut6");
  }
  {
    /* out of a pool one block deep: swim into the bank holding jump (the 0.3 lift) */
    world_new(0, "ut7");
    world_follow(8, 110, 8);
    int g = 110;
    for (int z = 2; z <= 14; z++)
      for (int x = 2; x <= 14; x++)
        for (int y = g - 3; y < g + 4; y++)
          world_set(x, y, z, y >= g ? B_AIR : (x >= 7 && x <= 9 && z >= 7 && z <= 9 && y == g - 1) ? B_WATER : B_DIRT);
    memset(&pl, 0, sizeof pl);
    pl.x = 8.5f, pl.z = 8.5f, pl.y = (float)(g - 1), pl.health = 20, pl.food = 20, pl.mode = 0;
    for (int t = 0; t < 10; t++) player_tick(0, 0);
    CHECK(pl.in_water);
    int t = 0;
    for (; t < 60 && !(pl.on_ground && pl.y >= g); t++) player_tick(K_FWD | K_JUMP, t == 0 ? K_JUMP : 0);
    CHECK(pl.on_ground && pl.y >= g && pl.z > 10);
    plat_remove_prefix("ut7");
  }
  {
    /* sprinting (EntityPlayerSP): forward twice, or with the sprint key; it lasts while forward is held */
    world_new(0, "ut8");
    world_follow(8, 110, 8);
    int g = 110;
    for (int z = -8; z <= 60; z++)
      for (int x = 2; x <= 14; x++)
        for (int y = g - 2; y < g + 4; y++) world_set(x, y, z, y >= g ? B_AIR : B_STONE);
    memset(&pl, 0, sizeof pl);
    pl.x = 8.5f, pl.z = 0.5f, pl.y = (float)g, pl.health = 20, pl.food = 20, pl.mode = 0, pl.on_ground = true;
    for (int t = 0; t < 3; t++) player_tick(0, 0);
    player_tick(K_FWD, K_FWD), player_tick(0, 0), player_tick(0, 0);
    CHECK(!pl.sprinting);
    player_tick(K_FWD, K_FWD);
    CHECK(pl.sprinting);
    for (int t = 0; t < 5; t++) player_tick(K_FWD, 0);
    CHECK(pl.sprinting);
    player_tick(0, 0);
    CHECK(!pl.sprinting);
    /* too slow a second press: walking */
    for (int t = 0; t < 10; t++) player_tick(0, 0);
    player_tick(K_FWD, K_FWD);
    for (int t = 0; t < 10; t++) player_tick(0, 0);
    player_tick(K_FWD, K_FWD);
    CHECK(!pl.sprinting);
    /* the key, let go, still sprints; sneaking stops it; hungry, it doesn't start */
    player_tick(K_FWD | K_SPRINT, K_SPRINT), player_tick(K_FWD, 0);
    CHECK(pl.sprinting);
    player_tick(K_FWD | K_SNEAK, K_SNEAK);
    CHECK(!pl.sprinting);
    pl.food = 6;
    player_tick(K_FWD | K_SPRINT, K_SPRINT);
    CHECK(!pl.sprinting);
    plat_remove_prefix("ut8");
  }
  {
    /* the cache moving up a step at a time (new rows above the terrain are not generated, only
     * aired) holds the same blocks and light as one made there at once */
    static uint8_t a[VCY * VCZ * VCX], l[VCY * VCZ * VCX];
    int ay = 0;
    for (int k = 0; k < 2; k++) {
      world_new(77, "ut9");
      if (k == 0)
        for (int y = 64; y < 100; y += 3) {
          world_follow(8, (float)y, 8);
          while (world_pending()) world_follow(8, (float)y, 8);
        }
      world_follow(8, 100, 8);
      while (world_pending()) world_follow(8, 100, 8);
      if (k == 0) memcpy(a, vc, sizeof a), memcpy(l, vl, sizeof l), ay = vc_y0;
    }
    CHECK(ay == vc_y0);
    CHECK(!memcmp(a, vc, sizeof a));
    CHECK(!memcmp(l, vl, sizeof l));
    /* and coming down from high up (the new rows in the air made as air): as the generator makes them */
    world_new(77, "ut9");
    for (int y = 160; y >= 92; y -= 3)
      for (int r = 0; r < 3; r++) {
        world_follow(8, (float)y, 8);
        while (world_pending()) world_follow(8, (float)y, 8);
      }
    static uint8_t g[16 * 16 * VCY];
    int off = 0;
    for (int cz = -2; cz <= 1; cz++)
      for (int cx = -2; cx <= 1; cx++) {
        gen_slab(cx, cz, vc_y0, VCY, g);
        for (int y = 0; y < VCY; y++)
          for (int z = 0; z < 16; z++)
            for (int x = 0; x < 16; x++) {
              int lx = cx * 16 + x - vc_x0, lz = cz * 16 + z - vc_z0;
              if (lx >= 0 && lx < VCX && lz >= 0 && lz < VCZ) off += vc[VC_I(lx, y, lz)] != g[(y * 16 + z) * 16 + x];
            }
      }
    CHECK(off == 0);
    plat_remove_prefix("ut9");
  }
  {
    /* worlds in slots: names kept, made unique, renamed; one deleted leaves the others */
    for (int s = 1; s <= MAX_WORLDS; s++) delete_world(s);
    char n[WORLD_NAME + 1];
    WorldInfo w;
    world_unique_name("New World", n);
    CHECK(!strcmp(n, "New World"));
    new_world(world_free_slot(), 5, 0, false, WT_DEFAULT, n);
    save_world();
    world_unique_name("New World", n);
    CHECK(!strcmp(n, "New World (2)"));
    int s2 = world_free_slot();
    new_world(s2, 6, 1, true, WT_DEFAULT, n);
    save_world();
    CHECK(s2 == 2 && world_info(1, &w) && !strcmp(w.name, "New World") && w.mode == 0 && w.seed == 5);
    CHECK(world_info(2, &w) && !strcmp(w.name, "New World (2)") && w.mode == 1 && w.seed == 6);
    CHECK(rename_world(1, "Base") && world_info(1, &w) && !strcmp(w.name, "Base"));
    world_unique_name("New World", n);
    CHECK(!strcmp(n, "New World"));
    delete_world(1);
    CHECK(!world_info(1, &w) && world_info(2, &w) && world_free_slot() == 1);
    CHECK(load_world(2) && pl.mode == 1 && world_seed == 6);
    for (int s = 1; s <= MAX_WORLDS; s++) delete_world(s);
  }
  {
    /* the block looked at: a ray over a slab or beside a flower goes on to what is behind */
    extern float hit_t;
    world_new(5, "ut10");
    world_follow(8, 100, 8);
    while (world_pending()) world_follow(8, 100, 8);
    for (int z = 4; z < 16; z++)
      for (int x = 4; x < 12; x++)
        for (int y = 99; y < 104; y++) world_set(x, y, z, y == 99 ? B_STONE : B_AIR);
    world_set(8, 100, 12, B_STONE);
    world_set(8, 100, 10, B_STONE_SLAB);
    pl.mode = 0;
    player_look(8.5f, 100.75f, 8.5f, 0, 0);
    CHECK(pl.hit_face == 2 && pl.hit_x == 8 && pl.hit_y == 100 && pl.hit_z == 12);
    player_look(8.5f, 100.25f, 8.5f, 0, 0);
    CHECK(pl.hit_face == 2 && pl.hit_z == 10 && fabsf(hit_t - 1.5f) < 0.01f);
    player_look(8.5f, 102, 10.5f, 0, 90);
    CHECK(pl.hit_face == 1 && pl.hit_y == 100 && pl.hit_z == 10 && fabsf(hit_t - 1.5f) < 0.01f);
    world_set(8, 100, 10, B_POPPY);
    player_look(8.9f, 100.25f, 8.5f, 0, 0);
    CHECK(pl.hit_z == 12);
    player_look(8.5f, 100.25f, 8.5f, 0, 0);
    CHECK(pl.hit_face == 2 && pl.hit_z == 10);
    /* a mob in front of the block takes the crosshair (no outline); past 3 blocks nothing is in reach */
    world_set(8, 100, 10, B_AIR), world_set(8, 100, 12, B_AIR), world_set(8, 100, 13, B_STONE);
    memset(ents, 0, sizeof ents);
    player_look(8.5f, 100.75f, 8.6f, 0, 0);
    CHECK(pl.hit_face == 2 && pl.hit_z == 13);
    Entity *cow = ent_new(E_COW, 8.5f, 100, 10.5f);
    player_look(8.5f, 100.75f, 8.6f, 0, 0);
    CHECK(pl.hit_face < 0 && entity_looked_at(3, 1e9f) == cow);
    cow->z = 12.3f;
    player_look(8.5f, 100.75f, 8.6f, 0, 0);
    CHECK(pl.hit_face < 0 && !entity_looked_at(3, 1e9f));
    plat_remove_prefix("ut10");
  }
  {
    /* commands: what they do; most need cheats */
    extern uint32_t game_time;
    new_world(1, 9, 0, false, WT_DEFAULT, "Commands");
    world_follow(pl.x, pl.y, pl.z);
    while (world_pending()) world_follow(pl.x, pl.y, pl.z);
    command_run("/gamemode 1");
    CHECK(pl.mode == 0);
    world_cheats = true;
    command_run("/gamemode c");
    CHECK(pl.mode == 1);
    command_run("/gamemode survival");
    CHECK(pl.mode == 0);
    memset(pl.inv, 0, sizeof pl.inv);
    command_run("/give @p minecraft:diamond_sword");
    CHECK(pl.inv[0].id == I_DIAMOND_SWORD);
    command_run("/give Player wool 5 14");
    CHECK(pl.inv[1].id == B_WOOL_RED && item_count(&pl.inv[1]) == 5);
    command_run("/give @p 5 64 2");
    CHECK(pl.inv[2].id == B_PLANKS_BIRCH && item_count(&pl.inv[2]) == 64);
    command_run("/give @p dye 1 15");
    CHECK(pl.inv[3].id == I_DYE_WHITE);
    command_run("/give @p 276 1 100");
    CHECK(pl.inv[4].id == I_DIAMOND_SWORD && pl.inv[4].aux == 100);
    command_run("/give @p nothing");
    CHECK(!pl.inv[5].id);
    command_run("/clear @p wool");
    CHECK(!pl.inv[1].id && pl.inv[0].id);
    command_run("/clear");
    CHECK(!pl.inv[0].id && !pl.inv[2].id);
    command_run("/time set night");
    CHECK(game_time == 13000);
    command_run("/time add 100");
    CHECK(game_time == 13100);
    command_run("/weather thunder 60");
    CHECK(weather.thundering && weather.raining && weather.rain_time == 1200);
    command_run("/weather clear");
    CHECK(!weather.raining);
    command_run("/xp 5L");
    CHECK(pl.xp_level == 5);
    command_run("/xp -2L");
    CHECK(pl.xp_level == 3);
    int sx = (int)floorf(pl.x), sy = (int)floorf(pl.y + 0.5f), sz = (int)floorf(pl.z);
    command_run("/setblock ~ ~3 ~ wool 4");
    CHECK(world_get(sx, sy + 3, sz) == B_WOOL_YELLOW);
    command_run("/gamerule keepInventory true");
    CHECK(rule(GR_KEEP_INVENTORY) && rule(GR_DAYLIGHT_CYCLE));
    command_run("/gamerule doDaylightCycle false");
    CHECK(!rule(GR_DAYLIGHT_CYCLE));
    command_run("/effect @p poison 10 1");
    CHECK(pl.eff[EF_POISON] == 200 && pl.eff_amp[EF_POISON] == 1);
    command_run("/effect @p clear");
    CHECK(!pl.eff[EF_POISON]);
    command_run("/tp 10 80 -20");
    CHECK(pl.x == 10.5f && pl.y == 80 && pl.z == -19.5f);
    command_run("/tp ~1 ~ ~-0.5");
    CHECK(pl.x == 11.5f && pl.z == -20);
    /* the world keeps its cheats and rules */
    save_world();
    world_cheats = false, game_rules = GR_DEFAULT;
    CHECK(load_world(1) && world_cheats && rule(GR_KEEP_INVENTORY) && !rule(GR_DAYLIGHT_CYCLE));
    WorldInfo wi;
    CHECK(world_info(1, &wi) && wi.cheats);
    /* finishing words, as Tab does */
    char o[40];
    int at;
    CHECK(command_complete("/gam", 0, &at, o, sizeof o) && at == 1 && !strcmp(o, "gamemode"));
    CHECK(command_complete("/gam", 1, &at, o, sizeof o) && !strcmp(o, "gamerule"));
    CHECK(!command_complete("/gam", 2, &at, o, sizeof o));
    CHECK(command_complete("/give @p diamond_sw", 0, &at, o, sizeof o) && at == 9 && !strcmp(o, "minecraft:diamond_sword"));
    CHECK(command_complete("/gamemode cr", 0, &at, o, sizeof o) && !strcmp(o, "creative"));
    CHECK(command_complete("/time set ", 1, &at, o, sizeof o) && !strcmp(o, "night"));
    delete_world(1);
  }
  {
    /* fire: lit on stone it goes out; by planks it spreads and burns them; flint and steel lights it */
    world_new(0, "ut12");
    world_follow(8, 70, 8);
    while (world_pending()) world_follow(8, 70, 8);
    memset(&weather, 0, sizeof weather);
    weather.rain_time = 1000000, rain_str = 0;
    game_rules = GR_DEFAULT;
    for (int z = 0; z < 20; z++)
      for (int x = 0; x < 20; x++)
        for (int y = 69; y < 76; y++) world_set(x, y, z, y == 69 ? B_STONE : B_AIR);
    fire_set(8, 70, 4, 0);
    CHECK(world_get(8, 70, 4) == B_FIRE);
    for (int t = 0; t < 1200; t++) ticks_run++, world_tick();
    CHECK(world_get(8, 70, 4) == B_AIR);
    int planks = 0;
    for (int x = 4; x < 13; x++)
      for (int y = 70; y < 73; y++) world_set(x, y, 12, B_PLANKS_OAK);
    fire_set(8, 70, 11, 0);
    for (int t = 0; t < 4000; t++) ticks_run++, world_tick();
    for (int x = 4; x < 13; x++)
      for (int y = 70; y < 73; y++) planks += world_get(x, y, 12) == B_PLANKS_OAK;
    CHECK(planks < 27);
    /* with doFireTick off it stays as it is */
    game_rules &= (uint8_t)~GR_FIRE_TICK;
    fire_set(2, 70, 2, 0);
    for (int t = 0; t < 1200; t++) ticks_run++, world_tick();
    CHECK(world_get(2, 70, 2) == B_FIRE);
    game_rules = GR_DEFAULT;
    world_set(2, 70, 2, B_AIR);
    /* flint and steel on the floor: fire on it */
    memset(&pl, 0, sizeof pl);
    pl.x = 16.5f, pl.y = 70, pl.z = 16.5f, pl.health = 20, pl.food = 20, pl.mode = 0, pl.on_ground = true;
    pl.inv[0] = (Stack){I_FLINT_AND_STEEL, 0};
    player_look(pl.x, pl.y + 1.62f, pl.z, 0, 60);
    player_tick(K_USE, K_USE);
    CHECK(world_get(16, 70, 17) == B_FIRE && pl.inv[0].aux == 1);
    plat_remove_prefix("ut12");
  }
  {
    /* superflat: bedrock, two dirt, grass; the player on it; kept as superflat */
    new_world(2, 77, 0, false, WT_FLAT, "Flat");
    world_follow(pl.x, pl.y, pl.z);
    while (world_pending()) world_follow(pl.x, pl.y, pl.z);
    int x = (int)floorf(pl.x) + 7, z = (int)floorf(pl.z) - 5;
    CHECK(pl.y == 4 && world_get(x, 0, z) == B_BEDROCK && world_get(x, 1, z) == B_DIRT && world_get(x, 2, z) == B_DIRT &&
          world_get(x, 3, z) == B_GRASS && world_get(x, 4, z) == B_AIR && world_get(x, 40, z) == B_AIR);
    save_world();
    world_type = WT_DEFAULT;
    WorldInfo wi;
    CHECK(load_world(2) && world_type == WT_FLAT && world_info(2, &wi) && wi.type == WT_FLAT);
    world_follow(pl.x, pl.y, pl.z);
    while (world_pending()) world_follow(pl.x, pl.y, pl.z);
    CHECK(world_get(x + 30, 3, z) == B_GRASS || !world_loaded(x + 30, 3, z));
    delete_world(2);
    world_type = WT_DEFAULT;
  }
  {
    /* the copy in numblocks_saves.py: every save, and after an install (only scripts left) they come back */
    for (int s = 1; s <= MAX_WORLDS; s++) delete_world(s);
    new_world(3, 42, 0, false, WT_DEFAULT, "Kept");
    world_follow(pl.x, pl.y, pl.z);
    while (world_pending()) world_follow(pl.x, pl.y, pl.z);
    for (int k = 0; k < 40; k++) world_set((int)pl.x + k, 70, (int)pl.z + 200, B_GOLD_BLOCK);   /* (another region too) */
    for (int k = 0; k < 40; k++) world_set((int)pl.x + k, 70, (int)pl.z, B_DIAMOND_BLOCK);
    save_world();
    char name[40];
    static uint8_t before[8][4096];
    static char names[8][40];
    uint32_t lens[8];
    int n = 0;
    for (int i = 0; plat_record(i, name, sizeof name) && n < 8; i++) {
      uint32_t len;
      const uint8_t *d = plat_load(name, &len);
      if (!d || (strncmp(name, "nb3", 3) && strcmp(name, "numblocks.cfg"))) continue;
      if (len > sizeof before[0]) continue;
      memcpy(before[n], d, len), lens[n] = len, strcpy(names[n], name), n++;
    }
    CHECK(n >= 3);   /* options, the world, its regions */
    uint32_t cl;
    CHECK(plat_load("numblocks_saves.py", &cl) && cl > 100);
    /* the install: every save gone, the script left */
    for (int i = 0; i < n; i++) plat_remove_prefix(names[i]);
    copy_restore();
    for (int i = 0; i < n; i++) {
      uint32_t len;
      const uint8_t *d = plat_load(names[i], &len);
      CHECK(d && len == lens[i] && !memcmp(d, before[i], len));
    }
    /* inside NumPlay, NumPlay keeps the copy */
    extern bool host_launcher;
    plat_remove_prefix("numblocks_saves.py");
    host_launcher = true;
    save_world();
    CHECK(!plat_load("numblocks_saves.py", &cl));
    host_launcher = false;
    delete_world(3);
    plat_remove_prefix("numblocks_saves.py");
  }
  /* sneaking along a one-block bridge over the void: it walks to the end and stops there (bridging);
   * not sneaking, it walks off */
  for (int sneak = 1; sneak >= 0; sneak--) {
    world_new(0, "ut4");
    world_follow(8, 70, 8);
    int y = 70;
    for (int z = 0; z <= 20; z++)
      for (int x = 0; x <= 16; x++)
        for (int k = -6; k < 4; k++) world_set(x, y + k, z, B_AIR);
    for (int z = 8; z <= 12; z++) world_set(8, y - 1, z, B_COBBLESTONE);
    memset(ents, 0, sizeof ents);
    pl.mode = 0, pl.dead = false, pl.health = 20, pl.flying = false;
    pl.x = 8.5f, pl.y = (float)y, pl.z = 8.5f, pl.vx = pl.vy = pl.vz = 0, pl.yaw = 0, pl.pitch = 0;
    pl.on_ground = true;
    uint32_t k = K_FWD | (sneak ? K_SNEAK : 0);
    for (int t = 0; t < 100; t++) player_tick(k, t ? 0 : k);
    if (sneak) CHECK(pl.y == (float)y && pl.z > 12.9f && pl.z < 13.31f);
    else CHECK(pl.y < (float)y - 1);
    plat_remove_prefix("ut4");
  }
  {
    /* keys saved as they first were (Back mining, OK placing) become OK mining, Back placing; set ones stay */
    keys_reset();
    CHECK(opt.keys[A_ATTACK] == 4 && opt.keys[A_USE] == 5);
    opt.keys[A_ATTACK] = 5, opt.keys[A_USE] = 4;
    keys_update(3);
    CHECK(opt.keys[A_ATTACK] == 4 && opt.keys[A_USE] == 5);
    opt.keys[A_ATTACK] = 12, opt.keys[A_USE] = 4;
    keys_update(3);
    CHECK(opt.keys[A_ATTACK] == 12 && opt.keys[A_USE] == 4);
    opt.keys[A_ATTACK] = 5, opt.keys[A_USE] = 4;
    keys_update(KEY_SHEET);   /* (chosen with this version: kept) */
    CHECK(opt.keys[A_ATTACK] == 5 && opt.keys[A_USE] == 4);
    keys_reset();
  }
#if NB_DIMS
  {
    /* (issue #219) each stronghold's portal room whole, caves or not: its 12 frames and the lava under them,
     * whichever rows of the chunks are made (the world's slabs of 24, from any height) */
    static uint8_t slab[256 * 24], hi[256 * 24 / 8];
    int rooms = 0, whole = 0;
    for (int64_t seed = 1; seed <= 12; seed++) {
      gen_init(seed);
      for (int s = 0; s < 3; s++) {
        int g[6] = {0};
        if (!gen_stronghold_find(s, g)) continue;
        for (int off = 0; off < 24; off += 11) {
          int fr = 0, lava = 0;
          for (int dz = -2; dz <= 2; dz++)
            for (int dx = -2; dx <= 3; dx++) {
              int x = g[2] + (dx < 3 ? dx : 0), y = dx < 3 ? g[3] : g[3] - 2, z = g[4] + (dx < 3 ? dz : 0);
              int y0 = y - off, h = 24;
              if (y0 < 0) h += y0, y0 = 0;
              memset(hi, 0, sizeof hi);
              gen_hi = hi;
              gen_slab(x >> 4, z >> 4, y0, h, slab);
              gen_hi = NULL;
              int c = (y - y0) * 256 + (z & 15) * 16 + (x & 15), b = slab[c] | (hi[c >> 3] >> (c & 7) & 1) << 8;
              if (dx < 3) fr += b == B_END_PORTAL_FRAME || b == B_END_PORTAL_FRAME_EYE;
              else lava += dz == 0 && b == B_LAVA;
            }
          rooms++, whole += fr == 12 && lava == 1;
        }
      }
    }
    printf("strongholds: %d of %d portal rooms whole\n", whole, rooms);
    CHECK(rooms == 12 * 3 * 3 && whole == rooms);
  }
#endif
  printf("%s (%d recipes)\n", fails ? "FAILED" : "all good", N_RECIPES);
  return fails != 0;
}
