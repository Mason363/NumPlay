/* Worlds: up to 5, each an appvar NBCEW1..NBCEW5 (archived): a header (magic, version, the world's name, mode,
 * seed, time, the player, the inventory, chests and furnaces), then the edit log. An unknown or broken file is
 * left alone: it never crashes the game. */
#include "nb.h"

int cur_world;
char cur_name[17];
int new_mode;
extern int xp_level, xp_points;
static uint32_t plays;

#define MAGIC 0x3143424Eu   /* "NBC1" */
#define VERSION 1
typedef struct {
  uint32_t magic;
  uint8_t version, mode;
  char name[17];
  uint32_t seed, played, time;
  int32_t x, y, z;
  uint16_t yaw;
  int16_t pitch;
  uint8_t health, slot, flying, n_tiles;
  int16_t spawn_x, spawn_z;
  uint8_t spawn_y, xp_level;
  uint16_t xp_points;
  Stack inv[36];
  uint16_t n_edits;
} Header;

static void name_of(int slot, char *n) {
  memcpy(n, "NBCEW", 5);
  n[5] = (char)('0' + slot);
  n[6] = 0;
}

static const Header *header(int slot, unsigned *len) {
  char n[8];
  name_of(slot, n);
  const uint8_t *p = plat_load(n, len);
  if (!p || *len < sizeof(Header)) return 0;
  const Header *h = (const Header *)p;
  if (h->magic != MAGIC || h->version != VERSION) return 0;
  if (*len < sizeof(Header) + h->n_tiles * sizeof(Tile) + (unsigned)h->n_edits * sizeof(Edit)) return 0;
  return h;
}

bool world_info(int slot, WorldInfo *w) {
  unsigned len;
  const Header *h = header(slot, &len);
  if (!h) return false;
  memcpy(w->name, h->name, 17);
  w->name[16] = 0;
  w->mode = h->mode, w->seed = h->seed, w->played = h->played;
  if (h->played > plays) plays = h->played;
  return true;
}

int world_free_slot(void) {
  WorldInfo w;
  for (int s = 1; s <= MAX_WORLDS; s++)
    if (!world_info(s, &w)) return s;
  return 0;
}

void delete_world(int slot) {
  char n[8];
  name_of(slot, n);
  plat_delete(n);
}

bool save_world(void) {
  if (!cur_world) return false;
  Header h;
  memset(&h, 0, sizeof h);
  h.magic = MAGIC, h.version = VERSION, h.mode = pl.mode;
  memcpy(h.name, cur_name, 17);
  h.seed = world_seed, h.played = ++plays, h.time = world_time;
  h.x = pl.x, h.y = pl.y, h.z = pl.z, h.yaw = pl.yaw, h.pitch = pl.pitch;
  h.health = (uint8_t)pl.health, h.slot = (uint8_t)pl.slot, h.flying = pl.flying;
  h.spawn_x = (int16_t)pl.spawn_x, h.spawn_y = (uint8_t)pl.spawn_y, h.spawn_z = (int16_t)pl.spawn_z;
  h.xp_level = (uint8_t)xp_level, h.xp_points = (uint16_t)xp_points;
  memcpy(h.inv, pl.inv, sizeof h.inv);
  h.n_tiles = (uint8_t)n_tiles;
  h.n_edits = (uint16_t)n_edits;
  /* the header and the tiles, then the edits */
  uint8_t *buf = rbuf;   /* (the 3D view's picture: not drawn while saving) */
  render_stale = 1;
  memcpy(buf, &h, sizeof h);
  memcpy(buf + sizeof h, tiles, n_tiles * sizeof(Tile));
  char n[8];
  name_of(cur_world, n);
  return plat_save(n, buf, sizeof h + n_tiles * sizeof(Tile), edits, n_edits * sizeof(Edit));
}

void new_world(int slot, const char *name, uint32_t seed, int mode) {
  cur_world = slot;
  memcpy(cur_name, name, 17);
  cur_name[16] = 0;
  n_edits = 0;
  n_tiles = 0;
  world_time = 0;
  xp_level = xp_points = 0;
  memset(&pl, 0, sizeof pl);
  pl.mode = (uint8_t)mode;
  if (mode) {
    /* Creative starts with a hotbar of building blocks */
    static const uint8_t first[9] = {B_GRASS, B_STONE, B_COBBLESTONE, B_PLANKS_OAK, B_LOG_OAK, B_GLASS, B_BRICKS, B_TORCH,
                                     B_SAND};
    for (int i = 0; i < 9; i++) pl.inv[i] = (Stack){first[i], 64, 0};
  }
  world_new(seed);
  int x, z;
  gen_spawn(&x, &z);
  pl.spawn_x = x, pl.spawn_z = z;
  Column c;
  gen_column(x, z, &c);
  pl.spawn_y = c.h + 1;
  player_spawn();
  world_center(x, pl.spawn_y, z, true);
  pl.spawn_y = world_top(x, z);
  player_spawn();
}

bool load_world(int slot) {
  unsigned len;
  const Header *h = header(slot, &len);
  if (!h) return false;
  Header hh;
  memcpy(&hh, h, sizeof hh);
  cur_world = slot;
  memcpy(cur_name, hh.name, 17);
  memset(&pl, 0, sizeof pl);
  pl.mode = hh.mode;
  world_time = hh.time;
  pl.spawn_x = hh.spawn_x, pl.spawn_y = hh.spawn_y, pl.spawn_z = hh.spawn_z;
  player_spawn();
  pl.x = hh.x, pl.y = hh.y, pl.z = hh.z, pl.yaw = hh.yaw, pl.pitch = hh.pitch;
  pl.health = hh.health ? hh.health : 20;
  pl.slot = hh.slot < 9 ? hh.slot : 0;
  pl.flying = hh.flying && hh.mode;
  xp_level = hh.xp_level, xp_points = hh.xp_points;
  memcpy(pl.inv, hh.inv, sizeof pl.inv);
  n_tiles = hh.n_tiles > MAX_TILES ? MAX_TILES : hh.n_tiles;
  memcpy(tiles, (const uint8_t *)h + sizeof hh, n_tiles * sizeof(Tile));
  n_edits = 0;
  if (!edits_room(hh.n_edits)) return false;
  memcpy(edits, (const uint8_t *)h + sizeof hh + hh.n_tiles * sizeof(Tile), hh.n_edits * sizeof(Edit));
  n_edits = hh.n_edits;
  world_new(hh.seed);
  world_center(FXI(pl.x), FXI(pl.y), FXI(pl.z), true);
  return true;
}
