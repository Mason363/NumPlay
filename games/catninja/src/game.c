/* Cat Ninja's map, cat, camera, timers and level scripts: maps/Map, entities/Player, maps/MapCamera,
 * tools/Time, tools/text/GlitchInterpreter and Main.update, in C. The numbers and the order of things are
 * the original's; where its code would have thrown an error (reading past the map), this reads a wall. */
#include <math.h>
#include <string.h>
#include "cn.h"
#include "inflate.h"

uint8_t grid[CN_MAP_MAX];
uint8_t blood[(CN_MAP_MAX + 7) / 8];
int gw = 50, gh = 50, level = 0, room = 0;
int destrotate, currentrotation;
bool rotating, hallucinate;
static bool hoff, speedup;
cc_t map_tint;
int playerpoints[12];
static int ppx, ppy;           /* Map.playerpoint */
static int lastlevel = -1;
static bool savedlevel;
static int savedx, savedy, saveddestrotation;
static double savedworldrotation;
static int mapnode = -3;       /* a grid index, or: */
#define NODE_FREE (-1)         /* collidenode (walkable) */
#define NODE_FREE2 (-2)        /* collidefreenode (walkable) */
static struct {
  int x, y0, y1;
} cps[96];
static int n_cps;
static struct {
  int sx, sy, ex, ey;
} tps[16];
static int n_tps, lastteleport_x, lastteleport_y;
static bool hasmagnets;
static int16_t resetnodes[3 * 128];
static int n_reset;
/* what the levels keep when they are left and come back to (XMLMap.tiles changed by changeTile) */
#define MAX_MODS 640
static struct {
  int8_t lv;
  uint8_t type;
  uint16_t idx;
} mods[MAX_MODS];
static int n_mods;

double world_rot, world_sx = 1, world_sy = 1, screen_x, screen_y;
double fade_alpha = 1;
bool fade_on;
static bool fadingscreen, fadein, doublefade, faded;
static double fadespeed;
static int target_dir = -1; /* the targetfunction: -1 none, else a map change: 0 left 1 right 2 up 3 down 4 none */
static bool target_respawn;
bool pausegame;
bool exitedmap;

player_t P;
int pdeaths, collected;
uint8_t crystals_got[(CN_CRYSTALS + 7) / 8];
int hud_deaths, hud_collected;
static uint32_t hud_timer;
uint32_t game_ms;
static uint32_t game_frames;
uint32_t rng = 1;
bool key_left, key_right, key_up, key_down;
static bool key_jet, pressedkey = true;
int afk_text = -1;
int key_hint_frames;

double rnd01(void) {
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return (rng >> 8) * (1.0 / 16777216.0);
}
static double tw(double v) { return (double)(int)(v * 20) / 20; } /* a display object's x or y: twips */
static int iabs(int v) { return v < 0 ? -v : v; }

/* ---------------------------------------------------------------- ColourChanger */
void cc_init(cc_t *c, double speed, double max) {
  c->speed = speed, c->max = max, c->r = max, c->g = c->b = 1, c->ra = speed, c->ga = c->ba = 0, c->reversed = false;
}
static void cc_reverse(cc_t *c) {
  if (c->ra > 0) c->ga = c->speed, c->ra = -c->speed, c->ba = 0;
  else if (c->ba > 0) c->ra = c->speed, c->ba = -c->speed, c->ga = 0;
  else if (c->ga > 0) c->ra = 0, c->ba = c->speed, c->ga = -c->speed;
  c->reversed = true;
}
static void cc_revert(cc_t *c) {
  if (c->ra > 0) c->ga = 0, c->ra = -c->speed, c->ba = c->speed;
  else if (c->ba > 0) c->ra = 0, c->ba = -c->speed, c->ga = c->speed;
  else if (c->ga > 0) c->ra = c->speed, c->ba = 0, c->ga = -c->speed;
  c->reversed = false;
}
void cc_update(cc_t *c) {
  c->r += c->ra, c->g += c->ga, c->b += c->ba;
  if (c->r >= c->max) {
    c->r = c->max, c->g = 1;
    c->reversed ? cc_reverse(c) : cc_revert(c);
  } else if (c->b >= c->max) {
    c->r = 1, c->b = c->max;
    c->reversed ? cc_reverse(c) : cc_revert(c);
  } else if (c->g >= c->max) {
    c->g = c->max, c->b = 1;
    c->reversed ? cc_reverse(c) : cc_revert(c);
  }
}

/* ---------------------------------------------------------------- tools/Time */
typedef struct {
  uint32_t start;
  int runat, kind, arg;
  bool repeat;
} tmr_t;
#define MAX_TIMERS 48
static tmr_t tmr[MAX_TIMERS];
static int n_tmr;
int timer_add(int ms, int kind, int arg, bool repeat) {
  if (n_tmr == MAX_TIMERS) return -1;
  tmr[n_tmr] = (tmr_t){game_ms, ms, kind, arg, repeat};
  return n_tmr++;
}
static void timer_splice(int i) {
  memmove(&tmr[i], &tmr[i + 1], (size_t)(n_tmr - i - 1) * sizeof tmr[0]);
  n_tmr--;
}
void timer_remove_kind(int kind) {
  for (int i = 0; i < n_tmr; i++)
    if (tmr[i].kind == kind) {
      timer_splice(i);
      return;
    }
}
static void player_respawn(void);
static void timer_call(int kind, int arg) {
  switch (kind) {
  case T_PAGE: interp(arg, 0, true); break;
  case T_LOOP: interp(arg, 0, true); break;
  case T_LASER: laser_switch(arg); break;
  case T_RESPAWN: player_respawn(); break;
  case T_DIRT: dirt_break(arg); break;
  case T_AFK:
    afk_text = text_create(TX_IDLE0 + (int)(rnd01() * (TX_IDLES - 1)), 0, 40, 0.05, true, 540);
    break;
  }
}
static void timers_update(void) {
  for (int i = 0; i < n_tmr; i++) {
    if ((int)(game_ms - tmr[i].start) >= tmr[i].runat) {
      tmr_t t = tmr[i];
      timer_call(t.kind, t.arg);
      if (n_tmr <= 0) return;
      if (i >= n_tmr) return;
      if (!tmr[i].repeat) timer_splice(i);
      else tmr[i].start = game_ms;
    }
  }
}

/* ---------------------------------------------------------------- the map's tiles */
int walk_at(int x, int y) {
  if (x < 0 || y < 0 || x >= gw || y >= gh) return 0;
  return !(grid[y * gw + x] & SOLID);
}
static bool walkable(int node) { return node < 0 || !(grid[node] & SOLID); }
static int node_type(int node) { return node < 0 ? 0 : TYPE(node); }
static int idx_type(int i) { return i >= 0 && i < gw * gh ? TYPE(i) : 1; } /* (past the map: a wall) */

static void mod_set(int lv, int idx, int type) {
  for (int i = 0; i < n_mods; i++)
    if (mods[i].lv == lv && mods[i].idx == idx) {
      mods[i].type = (uint8_t)type;
      return;
    }
  if (n_mods < MAX_MODS) mods[n_mods++] = (typeof(mods[0])){(int8_t)lv, (uint8_t)type, (uint16_t)idx};
}

void change_tile(int x, int y, int type, bool clear, bool persist, bool reset_node, bool set_walk) {
  (void)clear;
  if (x < 0 || x >= gw || y < 0 || y >= gh) return;
  int i = y * gw + x;
  bool walk = true;
  grid[i] = (uint8_t)((grid[i] & SOLID) | (type & 0x7F));
  if (persist) mod_set(level, i, type);
  blood[i >> 3] &= (uint8_t) ~(1 << (i & 7));
  switch (type) {
  case 1:
    walk = false;
    for (int k = 1; k < gh; k++) {
      if (y - k > 0 && TYPE((y - k) * gw + x) != 79) {
        if (k >= 2 && n_cps < 96) cps[n_cps].x = x, cps[n_cps].y0 = y - k, cps[n_cps].y1 = y - 1, n_cps++;
        break;
      }
    }
    break;
  case 8: case 9: case 45: case 46: {
    extern void mine_create(int x, int y, int g);
    mine_create(x * 20, y * 20, type == 8 ? 0 : type == 9 ? 1 : type == 45 ? 2 : 3);
    break;
  }
  case 16: case 17: case 18: case 19: {
    extern void button_create(int x, int y, int datanum);
    button_create(x * 20, y * 20, type);
    break;
  }
  case 24: case 25: case 26: case 27: walk = true; break;
  case 32:
    if (reset_node && n_reset < 128) resetnodes[n_reset * 3] = (int16_t)x, resetnodes[n_reset * 3 + 1] = (int16_t)y,
                                      resetnodes[n_reset * 3 + 2] = 32, n_reset++;
    walk = false;
    break;
  case 33: {
    extern void dirt_create(int x, int y);
    dirt_create(x * 20, y * 20);
    walk = false;
    break;
  }
  case 34:
    if (n_screens < MAX_SCREENS) screens[n_screens++] = (screen_t){x * 20, y * 20, -1, 0};
    walk = false;
    break;
  case 35: {
    extern void crate_create(int x, int y);
    walk = false;
    crate_create(x * 20, y * 20);
    break;
  }
  case 36: case 37: case 38: case 39: walk = false; break;
  case 44: case 82: case 83:
    if (reset_node && n_reset < 128) resetnodes[n_reset * 3] = (int16_t)x, resetnodes[n_reset * 3 + 1] = (int16_t)y,
                                      resetnodes[n_reset * 3 + 2] = (int16_t)type, n_reset++;
    break;
  case 67: case 68: case 69: case 70: case 71: case 72: case 73: case 74: {
    extern void shooter_create(int x, int y, int o, bool homing);
    shooter_create(x * 20, y * 20, (type - 67) & 3, type >= 71);
    break;
  }
  case 75: case 76: case 77: case 78:
    hasmagnets = true;
    walk = false;
    break;
  case 81: {
    for (int k = 0; k < CN_CRYSTALS; k++)
      if ((int8_t)cn_crystals[k][0] == level && cn_crystals[k][1] == x && cn_crystals[k][2] == y &&
          (crystals_got[k >> 3] >> (k & 7) & 1)) {
        grid[i] &= 0x7F; /* walkable */
        return;
      }
    sphere_create(x * 20, y * 20, true);
    break;
  }
  case 84: case 85: case 86: case 87: {
    extern void fall_create(int x, int y, int dir);
    fall_create(x * 20, y * 20, type - 84);
    break;
  }
  case 88: {
    extern void sun_create(int x, int y);
    sun_create(x * 20, y * 20);
    break;
  }
  case 90: walk = false; break;
  case 91:
    bigsphere_create(x * 20, y * 20);
    walk = false;
    break;
  }
  if (set_walk) grid[i] = (uint8_t)((grid[i] & 0x7F) | (walk ? 0 : SOLID));
}

static void hallucinate_off(bool now) {
  if (hallucinate) {
    if (now) hoff = false, world_sx = world_sy = 1, world_rot = 0;
    else hoff = true;
    hallucinate = false;
    map_tint.speed = 0.002;
  }
}
static void speedup_off(void) {
  if (speedup) P.ACCELERATION = 1, P.FRICTION = 0.9, P.trailfade = 0.02, speedup = false;
}

/* ---------------------------------------------------------------- maps/MapCamera */
static int cam_destx, cam_desty, cam_speed = 99;
static bool cam_moving;
bool cam_paused;
#define HALF_W 320 /* the calculator shows 640 x 480 of the original's pixels about the middle */
#define HALF_H 240
static void cam_move(int px, int py, bool now, int speed, bool locked) {
  if (locked) return;
  int hx = HALF_W, hy = HALF_H;
  if (world_rot == 90 || world_rot == -90) hx = HALF_H, hy = HALF_W;
  int mw = gw * 20, mh = gh * 20;
  if (px - hx <= 0) px = hx;
  else if (px + hx >= mw) px = mw - hx;
  if (py - hy <= 0) py = hy;
  else if (py + hy >= mh) py = mh - hy;
  if (now) screen_x = -px, screen_y = -py;
  else cam_destx = -px, cam_desty = -py;
  if (screen_x != cam_destx || screen_y != cam_desty) cam_moving = true;
  cam_speed = speed;
}
static void cam_update(void) {
  if (cam_moving && !cam_paused) {
    if (screen_x >= cam_destx - 1 && screen_x <= cam_destx + 1 && screen_y >= cam_desty - 1 && screen_y <= cam_desty + 1) {
      screen_x = cam_destx, screen_y = cam_desty, cam_moving = false;
      return;
    }
    double d = (screen_x >= cam_destx - 10 && screen_x <= cam_destx + 10 && screen_y >= cam_desty - 10 &&
                screen_y <= cam_desty + 10)
                   ? cam_speed / 2
                   : cam_speed;
    screen_x = tw(screen_x + (cam_destx - screen_x) / d);
    screen_y = tw(screen_y + (cam_desty - screen_y) / d);
  }
}

/* ---------------------------------------------------------------- tools/ShakeScreen */
static bool shaking;
static int shake_len, shake_pow, shake_t, shake_ox, shake_oy;
void shake(int power, int length) {
  shaking = true, shake_pow = power, shake_len = length, shake_t = 0, shake_ox = (int)P.x, shake_oy = (int)P.y;
}
static void shake_update(void) {
  if (shaking && !cam_paused) {
    int dx = (int)(rnd01() * shake_pow - rnd01() * shake_pow);
    int dy = (int)(rnd01() * shake_pow - rnd01() * shake_pow);
    if (++shake_t >= shake_len) shake_t = 0, shaking = false, cam_move(shake_ox, shake_oy, false, 15, false);
    screen_x = tw(screen_x + dx), screen_y = tw(screen_y + dy);
  }
}

/* ---------------------------------------------------------------- the rotations */
static void rotate_to(int r) { destrotate = r, rotating = true; }

/* ---------------------------------------------------------------- loading a map (Map.create) */
static void scripts_load(void);
static void player_reset(void);
static void map_remove(void) {
  env_clear();
  n_particles = 0;
  n_texts = 0;
  afk_text = -1;
}
static void map_create(int lv) {
  static uint8_t tables[3400];
  if (lv >= 30) lv = 0;
  if (lv != level) n_reset = 0;
  const cn_map_t *m = &cn_maps[lv + 1];
  gw = m->w, gh = m->h;
  savedlevel = lastlevel == lv && lv != -1;
  level = lv;
  if (lv != -1) lastlevel = lv;
  if (hallucinate) hallucinate_off(true);
  if (speedup) speedup_off();
  n_cps = n_tps = n_reset = 0;
  env_clear();
  P.hasJetPack = P.usingjetpack = false; /* jetpackOff */
  hasmagnets = false;
  np_inflate(grid, (uint32_t)(gw * gh), cn_map_z + m->at, m->len, (np_inflate_tables_t *)tables);
  for (int i = 0; i < n_mods; i++)
    if (mods[i].lv == lv && mods[i].idx < gw * gh) grid[mods[i].idx] = mods[i].type;
  memset(blood, 0, sizeof blood);
  for (int k = 0; k < 12; k++) playerpoints[k] = m->marks[k];
  for (int y = 0; y < gh; y++)
    for (int x = 0; x < gw; x++) {
      int t = grid[y * gw + x];
      grid[y * gw + x] = 0;
      change_tile(x, y, t, false, false, true, true);
    }
  if (m->sx || m->sy) ppx = m->sx * 20, ppy = m->sy * 20;
  else ppx = playerpoints[0] * 20, ppy = playerpoints[1] * 20;
  if (savedlevel) ppx = savedx, ppy = savedy;
  cam_move(ppx, ppy, true, 15, false);
  room = 0;
  scripts_load();
  if (room != 1) rotate_to(0);
}

static void update_level(int dir, bool respawn) {
  fadingscreen = true, doublefade = true, fadein = true, fade_on = true, fadespeed = 0.08, pausegame = true, faded = false;
  target_dir = dir, target_respawn = respawn;
}
static void player_respawngame(void);
static void target_run(void) {
  int dir = target_dir;
  target_dir = -1;
  pausegame = false;
  map_remove();
  map_create(level);
  if (exitedmap && level == -1) {
    playerpoints[6] = 140, playerpoints[7] = 15, playerpoints[5] = lastlevel;
    exitedmap = false;
  } else if (!exitedmap && level == -1) {
    lastlevel = -1, savedlevel = false, playerpoints[5] = 0;
  }
  if (dir == 1) ppx = playerpoints[0] * 20, ppy = playerpoints[1] * 20;
  else if (dir == 0) ppx = playerpoints[3] * 20, ppy = playerpoints[4] * 20;
  if (dir == 2) ppx = playerpoints[9] * 20, ppy = playerpoints[10] * 20;
  else if (dir == 3) ppx = playerpoints[6] * 20, ppy = playerpoints[7] * 20;
  if (savedlevel) {
    ppx = savedx, ppy = savedy;
    destrotate = currentrotation = saveddestrotation;
    world_rot = savedworldrotation, P.rotation = savedworldrotation;
    rotating = false;
  }
  cam_move(ppx, ppy, true, 15, false);
  player_reset();
  if (target_respawn) player_respawngame();
  key_hint_frames = 300;
}
static void goto_level(int lv, bool respawn) {
  if (lv != level) level = lv, update_level(4, respawn);
}
void map_move(int dir) {
  static const int8_t at[4] = {2, 5, 11, 8};
  int d = playerpoints[at[dir]];
  if (d != 0) level = d;
  else level += dir == 0 || dir == 2 ? -1 : 1;
  update_level(dir, false);
}

/* ---------------------------------------------------------------- Map's collisions (the cat's top left tile) */
static int collision_right(int x, int y) {
  if (y >= 0 && y < gh - 1 && x >= 0 && x < gw - 1) {
    mapnode = y * gw + x + 1;
    if (!walkable(mapnode)) return mapnode;
  }
  return NODE_FREE;
}
static int collision_left(int x, int y) {
  if (y >= 0 && y < gh - 1 && x >= 0 && x < gw - 1) {
    mapnode = y * gw + x;
    if (!walkable(mapnode)) return mapnode;
  }
  return NODE_FREE;
}
static int collision_down(int x, int y) {
  if (y >= 0 && y < gh - 1 && x >= 0 && x < gw - 1) {
    mapnode = (y + 1) * gw + x;
    if (!walkable(mapnode)) return mapnode;
  }
  return NODE_FREE;
}
#define collision_up collision_left
int collide_down(int x, int y) {
  switch (currentrotation) {
  case 0:
    if (collision_left(x, y) == NODE_FREE && !walkable(collision_down(x, y))) return mapnode;
    if (collision_right(x, y) == NODE_FREE && !walkable(collision_down(x + 1, y))) return mapnode;
    break;
  case 90:
    if (collision_up(x, y) == NODE_FREE && !walkable(collision_right(x, y))) return mapnode;
    if (collision_down(x, y) == NODE_FREE && !walkable(collision_right(x, y + 1))) return mapnode;
    break;
  case 180:
    if (collision_left(x, y + 1) == NODE_FREE && !walkable(collision_down(x, y - 1))) return mapnode;
    if (collision_right(x, y + 1) == NODE_FREE && !walkable(collision_down(x + 1, y - 1))) return mapnode;
    break;
  case -90:
    if (collision_up(x + 1, y) == NODE_FREE && !walkable(collision_left(x, y))) return mapnode;
    if (collision_down(x + 1, y) == NODE_FREE && !walkable(collision_left(x, y + 1))) return mapnode;
    break;
  }
  return NODE_FREE2;
}
int collide_right(int x, int y) {
  switch (currentrotation) {
  case 0: if (!walkable(collision_right(x, y))) return mapnode; break;
  case 90: if (!walkable(collision_up(x, y))) return mapnode; break;
  case 180: if (!walkable(collision_left(x, y + 1))) return mapnode; break;
  case -90: if (!walkable(collision_down(x + 1, y))) return mapnode; break;
  }
  return NODE_FREE2;
}
int collide_left(int x, int y) {
  switch (currentrotation) {
  case 0: if (!walkable(collision_left(x, y))) return mapnode; break;
  case 90: if (!walkable(collision_down(x, y))) return mapnode; break;
  case 180: if (!walkable(collision_right(x, y + 1))) return mapnode; break;
  case -90: if (!walkable(collision_up(x + 1, y))) return mapnode; break;
  }
  return NODE_FREE2;
}
static bool grid_solid(int i) { return i >= 0 && i < gw * gh ? (grid[i] & SOLID) != 0 : true; }
int collide_up(int x, int y) {
  switch (currentrotation) {
  case 0:
    if ((walkable(collision_down(x, y)) && !walkable(collision_up(x, y))) ||
        (y >= 0 && y < gh - 1 && grid_solid(y * gw + x + 1) && !walkable(collision_up(x, y))))
      return mapnode;
    break;
  case 90:
    if ((collision_right(x, y) == NODE_FREE && !walkable(collision_left(x, y))) ||
        (y >= 0 && y < gh - 1 && grid_solid((y + 1) * gw + x) && !walkable(collision_left(x, y))))
      return mapnode;
    break;
  case 180:
    if ((walkable(collision_up(x, y)) && !walkable(collision_down(x, y))) ||
        (y >= 0 && y < gh - 1 && grid_solid((y + 1) * gw + x + 1) && !walkable(collision_down(x, y))))
      return mapnode;
    break;
  case -90:
    if ((collision_left(x, y) == NODE_FREE && !walkable(collision_right(x, y))) ||
        (y >= 0 && y < gh - 1 && grid_solid((y + 1) * gw + x + 1) && !walkable(collision_right(x, y))))
      return mapnode;
    break;
  }
  return NODE_FREE2;
}
bool node_walkable(int node) { return walkable(node); }

/* ---------------------------------------------------------------- Map: what the cat touches */
static void pull(int dir, double p) {
  int r = currentrotation;
  if (dir == 0) {
    if (r == 0) P.speedx += p; else if (r == 90) P.gravity += p; else if (r == 180) P.speedx -= p; else if (r == -90) P.gravity -= p;
  } else if (dir == 1) {
    if (r == 0) P.speedx -= p; else if (r == 90) P.gravity -= p; else if (r == 180) P.speedx += p; else if (r == -90) P.gravity += p;
  } else if (dir == 2) {
    if (r == 0) P.gravity += p; else if (r == 90) P.speedx += p; else if (r == 180) P.gravity -= p; else if (r == -90) P.speedx += p;
  } else if (dir == 3) {
    P.maxgravity = 15, P.maxspeedx = 15;
    if (r == 0) P.gravity -= p; else if (r == 90) P.speedx += p; else if (r == 180) P.gravity += p; else if (r == -90) P.speedx -= p;
  }
}
static void magnet_collision(int x, int y, int dx, int dy, int dir) {
  for (;;) {
    int i = y * gw + x;
    int t = idx_type(i);
    bool w = i >= 0 && i < gw * gh ? !(grid[i] & SOLID) : false;
    x += dx, y += dy;
    if (x < 0 || x >= gw || y < 0 || y >= gh) break;
    if (t >= 75 && t <= 78) {
      if ((t == 75 && dy == -1) || (t == 76 && dx == 1) || (t == 77 && dx == -1) || (t == 78 && dy == 1)) {
        pull(dir, 0.8);
        return;
      }
    }
    if (!w) return;
  }
}
static void edit_checkpoint(int x, int y) {
  change_tile(x, y, 80, false, false, true, true);
  P.spawnx = P.coordx * 20, P.spawny = P.coordy * 20, P.spawnrotation = destrotate, P.spawnlevel = level;
  particles_create(x * 20, y * 20, 2, 20, 15, 2, false);
  mod_set(level, y * gw + x, 80);
  shake(6, 8);
}
static void teleport_check(int x, int y) {
  if (P.speedx >= -0.5 && P.speedx <= 0.5 && (x != lastteleport_x || y != lastteleport_y)) {
    for (int k = 0; k < n_tps; k++)
      if (x == tps[k].sx && y == tps[k].sy) {
        P.x = tps[k].ex * 20, P.y = tps[k].ey * 20, P.speedx = 0;
        P.coordx = (int)((P.x - OFFX) / 20), P.coordy = (int)((P.y - OFFY) / 20);
        lastteleport_x = tps[k].ex, lastteleport_y = tps[k].ey;
        return;
      }
  }
}
static void range_collision(int x, int y) {
  if (x < 0 || x >= gw || y < 0 || y >= gh) return;
  int t = TYPE(y * gw + x);
  if (t >= 63 && t <= 66) teleport_check(x, y);
}
static void handle_collision(int x, int y) {
  if (x < 0 || x >= gw || y < 0 || y >= gh) return;
  int i = y * gw + x, t = TYPE(i);
  if ((t < 63 || t > 66) && (x != lastteleport_x || y != lastteleport_y)) lastteleport_x = x, lastteleport_y = y;
  switch (t) {
  case 0:
    if (hasmagnets) {
      magnet_collision(x, y, 1, 0, 0);
      magnet_collision(x, y, -1, 0, 1);
      magnet_collision(x, y, 0, 1, 2);
      magnet_collision(x, y, 0, -1, 3);
    }
    break;
  case 2: case 5: case 6: case 7:
    blood[i >> 3] |= (uint8_t)(1 << (i & 7));
    player_die();
    break;
  case 3: player_die(); break;
  case 24: currentrotation = destrotate = 0; break;
  case 25: currentrotation = destrotate = -90; break;
  case 26: currentrotation = destrotate = 90; break;
  case 27: currentrotation = destrotate = 180; break;
  case 40: if (destrotate != 0) rotate_to(0); break;
  case 41: if (destrotate != -90) rotate_to(-90); break;
  case 42: if (destrotate != 90) rotate_to(90); break;
  case 43: if (destrotate != 180) rotate_to(180); break;
  case 44:
    P.hasJetPack = true; /* jetpackOn */
    change_tile(x, y, 0, true, false, true, true);
    key_hint_frames = 300;
    break;
  case 51: pull(3, 0.8); break;
  case 52: pull(0, 0.8); break;
  case 53: pull(1, 0.8); break;
  case 54: pull(2, 0.8); break;
  case 59: case 60: case 61: case 62: P.gravity = -20, P.doublejump = true; break;
  case 79:
    for (int k = 0; k < n_cps; k++)
      for (int a = cps[k].y0; a < cps[k].y1; a++)
        if (y >= cps[k].y0 && y <= cps[k].y1) edit_checkpoint(cps[k].x, a + 1);
    break;
  case 82:
    change_tile(x, y, 0, true, false, true, true);
    speedup = true, P.ACCELERATION = 5, P.FRICTION = 0.94, P.trailfade = 0.01;
    break;
  case 83:
    change_tile(x, y, 0, true, false, true, true);
    hallucinate = true, map_tint.speed = 0.02;
    break;
  }
  if (t >= 11 && t <= 15) player_die();
}

/* ---------------------------------------------------------------- entities/Player */
static void anim_frame(void) {
  static const uint8_t frames[2][2] = {{SPR_CAT_IDLE, SPR_CAT_IDLE2}, {SPR_CAT_WALK1, SPR_CAT_WALK2}};
  P.pic = frames[P.a_state][P.a_frame];
}
static void anim_update(void) {
  if (P.a_paused) return;
  if (++P.a_timer >= P.a_speed) {
    anim_frame();
    if (++P.a_frame >= 2) P.a_frame = 0;
    P.a_timer = 0;
  }
}
static void anim_play(void) { P.a_paused = false, anim_frame(); }
static void anim_pause(void) { P.a_paused = true; }
static void updatecoords(void) { P.coordx = (int)((P.x - OFFX) / 20), P.coordy = (int)((P.y - OFFY) / 20); }

static void player_reset(void) {
  P.dead = false, P.forceSlam = false;
  P.x = ppx + OFFX, P.y = ppy + OFFY;
  cam_move((int)P.x, (int)P.y, false, 15, false);
  P.on_stage = true;
}
static void player_create(void) {
  memset(&P, 0, sizeof P);
  P.pic = SPR_CAT_IDLE, P.scaleX = 1;
  P.MAXSPEED = 6, P.ACCELERATION = 1, P.FRICTION = 0.9, P.trailfade = 0.02, P.onground = true;
  P.jet_visible = true;
  player_reset();
  P.spawnx = (int)P.x, P.spawny = (int)P.y, P.spawnlevel = level;
  cc_init(&P.tint, 0.01, 2);
  P.a_speed = 8;
}

static void slam(void) {
  bool broke = false;
  int t = 0;
  for (int k = 0; k < 4; k++) {
    if (currentrotation == 0) t = idx_type((P.coordy + 2) * gw + (P.coordx + (k - 2)));
    if (t == 32 || t == 35) {
      if (t == 32) {
        change_tile(P.coordx + (k - 2), P.coordy + 2, 0, false, false, true, true);
        particles_create(P.x, P.y, 6, 10, 10, 5, false);
        broke = true;
      }
    }
  }
  if (broke) {
    P.forceSlam = true, P.gravity = 15;
    P.coordy = (int)((P.y - OFFY) / 20);
    slam();
  }
  if (P.gravity <= 10) return;
  shake((int)(P.gravity - 8), 15);
  particles_create(P.x, P.y, (int)(P.gravity - 8), 6, 12, 3, false);
  env_slam();
}

static void walljump(bool jump) {
  if (P.coordy >= 0 && P.coordy < gh) {
    int r = currentrotation;
    if (P.speedx <= 0 && !P.onground) {
      if ((!walkable(collide_left(P.coordx, P.coordy)) && r == 0) ||
          (!walkable(collide_left(P.coordx - 1, P.coordy)) && r == 90) ||
          (!walkable(collide_left(P.coordx, P.coordy)) && r == -90) ||
          (r == 180 && !walkable(collide_left(P.coordx, P.coordy)))) {
        if (node_type(mapnode) != 1 && node_type(mapnode) != 90) return;
        if (jump) P.speedx = 12, P.gravity = -12;
        else if (P.gravity >= 0 && key_left) P.gravity = 2, anim_pause(), P.pic = SPR_CAT_WALLSLIDE, P.hitwall = true;
      }
    }
    if (P.speedx >= 0 && !P.onground) {
      if ((!walkable(collide_right(P.coordx, P.coordy)) && r == 0) ||
          (!walkable(collide_right(P.coordx, P.coordy)) && r == 90) ||
          (!walkable(collide_right(P.coordx, P.coordy)) && r == -90) ||
          (r == 180 && !walkable(collide_right(P.coordx, P.coordy)))) {
        if (node_type(mapnode) != 1 && node_type(mapnode) != 90) return;
        if (jump) P.speedx = -12, P.gravity = -12;
        else if (P.gravity >= 0 && key_right) P.gravity = 2, anim_pause(), P.pic = SPR_CAT_WALLSLIDE, P.hitwall = true;
      }
    }
  }
}

static void player_up(void) {
  if (P.usingjetpack) return;
  updatecoords();
  if (P.onground && !P.stopMovement) P.gravity = -8, P.onground = false;
  if (P.doublejump && P.gravity >= -6 && !P.stopMovement)
    P.gravity = -8, P.onground = false, P.doublejump = false, P.doublefalling = true;
  walljump(true);
}
static void togglejetpack(void) {
  if (P.hasJetPack) {
    if (P.usingjetpack) P.usingjetpack = false;
    else if (!P.rolling) P.usingjetpack = true;
  }
}

static void player_walk(void) {
  anim_update(), anim_play();
  P.a_speed = 12, P.a_state = 1;
  P.x = tw(P.x + (P.stepAmount > 0 ? 1 : -1));
  int cx = (int)((P.x - OFFX) / 20);
  if (P.lastX != cx) {
    P.lastX = cx;
    if (++P.steps >= iabs(P.stepAmount)) {
      P.walkSteps = false, P.steps = 0, P.stepAmount = 0;
      anim_pause();
      P.pic = SPR_CAT_IDLE;
    }
  }
}

static void move_by(double sx, double g) {
  switch (currentrotation) {
  case 0: P.x = tw(P.x + sx), P.y = tw(P.y + g); break;
  case 90: P.y = tw(P.y - sx), P.x = tw(P.x + g); break;
  case 180: P.x = tw(P.x - sx), P.y = tw(P.y - g); break;
  case -90: P.y = tw(P.y + sx), P.x = tw(P.x - g); break;
  }
}

#define FALLSPEED 0.5
static void player_update(void) {
  int r;
  if (P.walkSteps) player_walk();
  if (P.gravity >= P.maxgravity) P.gravity = P.maxgravity;
  else if (P.gravity <= -P.maxgravity) P.gravity = -P.maxgravity;
  if (P.speedx >= P.maxspeedx) P.speedx = P.maxspeedx;
  else if (P.speedx <= -P.maxspeedx) P.speedx = -P.maxspeedx;
  P.maxgravity = 12, P.maxspeedx = 6;
  if (!P.usingjetpack) P.gravity += FALLSPEED;
  if (!P.usingjetpack && key_down && !P.stopMovement && !P.hitwall) {
    P.gravity += FALLSPEED * 2;
    P.maxspeedx = 10, P.FRICTION = 0.94, P.pic = SPR_CAT_ROLL, P.jet_visible = false, P.maxgravity = 15;
    if (P.onground && P.speedx == 0) anim_pause(), P.pic = SPR_CAT_DUCK, P.jet_half = true, P.jet_visible = true;
  } else {
    P.jet_half = false, P.jet_visible = true, P.MAXSPEED = 6, P.FRICTION = 0.9;
  }
  if (P.hasJetPack && P.usingjetpack) {
    if (key_up) P.gravity -= FALLSPEED * 2;
    if (key_down) P.gravity += FALLSPEED * 2;
    if (P.gravity >= P.MAXSPEED) P.gravity = P.MAXSPEED;
    else if (P.gravity <= -P.MAXSPEED) P.gravity = -P.MAXSPEED;
    P.gravity *= P.FRICTION;
    double rr = 2 + rnd01() * 4;
    trail_create(P.x + 5 * P.scaleoffset - 15 + rr + 0.5, P.y - OFFY + rr + 0.5, TR_CIRCLE_RED, 0, P.trailfade, 0.8, 0,
                 P.rotation, 0, rr);
  }
  anim_update();
  updatecoords();
  handle_collision((int)(P.x / 20), (int)(P.y / 20));
  if (n_tps) {
    range_collision((int)(P.x / 20) + 1, (int)(P.y / 20));
    range_collision((int)(P.x / 20) - 1, (int)(P.y / 20));
    range_collision((int)(P.x / 20), (int)(P.y / 20));
  }
  cc_update(&P.tint);
  if (key_right && !P.stopMovement) {
    P.speedx += P.ACCELERATION;
    anim_play(), P.a_speed = 8, P.a_state = 1, P.scaleX = 1;
    P.scaleoffset = currentrotation == 180 ? 23 - 20 : 0;
  }
  if (key_left && !P.stopMovement) {
    P.speedx -= P.ACCELERATION;
    anim_play(), P.a_speed = 8, P.a_state = 1, P.scaleX = -1;
    P.scaleoffset = currentrotation == 180 ? 0 : 23 - 20;
  }
  P.speedx *= P.FRICTION;
  bool idle = false;
  if (!key_right && !key_left && P.speedx <= 0.1 && P.speedx >= -0.1 && !P.stopMovement && P.gravity > 0) {
    P.speedx = 0;
    anim_play(), P.a_speed = 25, P.a_state = 0;
    idle = true;
  }
  if (P.gravity < 0 && !P.onground) anim_pause(), P.pic = SPR_CAT_JUMP;
  else if (P.gravity > 0 && !P.onground) anim_play(), P.a_speed = 12, P.a_state = 0;
  bool ceil = false;
  r = currentrotation;
  if (P.gravity < 0 && !walkable(collide_up(P.coordx, P.coordy))) {
    P.gravity = 0;
    if (r == 0) P.y = (P.coordy + 1) * 20 + OFFY;
    else if (r == 90) P.x = (P.coordx + 1) * 20 + OFFX;
    else if (r == 180) P.y = P.coordy * 20 + OFFY;
    else if (r == -90) P.x = P.coordx * 20 + OFFX;
    ceil = true;
  }
  P.hitwall = false;
  if (!ceil && P.speedx > 0 && P.coordx < gw - 1) {
    if (!walkable(collide_right(P.coordx, P.coordy))) {
      P.speedx = 0;
      if (r == 0) P.x = P.coordx * 20 + OFFX + 1;
      else if (r == 90) P.y = (P.coordy + 1) * 20 + OFFY - 1;
      else if (r == 180) P.x = (P.coordx + 1) * 20 + OFFX - 1;
      else if (r == -90) P.y = P.coordy * 20 + OFFY;
    }
  }
  if (!ceil && P.speedx < 0 && P.coordx >= 0) {
    if (!walkable(collide_left(P.coordx, P.coordy))) {
      P.speedx = 0;
      if (r == 0) P.x = (P.coordx + 1) * 20 + OFFX - 1;
      else if (r == 90) P.y = P.coordy * 20 + OFFY;
      else if (r == 180) P.x = P.coordx * 20 + OFFX;
      else if (r == -90) P.y = (P.coordy + 1) * 20 + OFFY - 1;
    }
  }
  walljump(false);
  if (P.coordx < 0) {
    map_move(0);
    return;
  }
  if (P.coordx > gw - 2) {
    map_move(1);
    return;
  }
  if (P.coordy < 0) {
    map_move(2);
    return;
  }
  if (P.coordy > gh - 1) {
    map_move(3);
    return;
  }
  move_by(P.speedx, P.gravity);
  P.onground = false;
  if (P.forceSlam) P.pic = SPR_CAT_ROLL, P.gravity = 10;
  if (P.gravity >= 0) {
    if (!P.onground && P.gravity > 4 && P.coordy > 1 && P.coordy < gh - 2) {
      if (!walkable(collide_down(P.coordx, P.coordy + 1))) {
        if (P.gravity >= 5) slam();
        P.gravity /= 2;
      }
    }
    if (!walkable(collide_down(P.coordx, P.coordy))) {
      if (P.gravity >= 5) slam();
      P.gravity = 0, P.forceSlam = false, P.doublejump = true, P.doublefalling = false;
      r = currentrotation;
      if (r == 0) P.y = P.coordy * 20 + OFFY;
      else if (r == 90) P.x = P.coordx * 20 + OFFX;
      else if (r == 180) P.y = (P.coordy + 1) * 20 + OFFY - 1;
      else if (r == -90) P.x = (P.coordx + 1) * 20 + OFFY;
      P.onground = true;
    }
  }
  if (!P.stopMovement) {
    if ((!P.onground && !P.hitwall && P.doublefalling) || (key_down && P.speedx != 0)) {
      if ((!P.usingjetpack && !P.doublefalling) || !P.hasJetPack) {
        P.pic = SPR_CAT_ROLL;
        P.rotation += P.scaleX == 1 ? 20 : -20;
        if (P.hasJetPack && !P.rolling) P.rotation = -currentrotation;
        if (key_down && P.speedx != 0) P.rolling = true;
      }
    } else {
      P.rotation = -currentrotation, P.rolling = false;
    }
  }
  if (!P.usingjetpack && key_down && !P.stopMovement && !P.hitwall) {
    P.pic = SPR_CAT_ROLL, P.jet_visible = false;
    if (P.onground && P.speedx == 0) anim_pause(), P.pic = SPR_CAT_DUCK, P.jet_half = true, P.jet_visible = true;
  }
  if (!idle && (!P.hasJetPack || !P.usingjetpack))
    trail_create(P.x - OFFX - P.scaleoffset + 11.5, P.y - OFFY + 10, TR_PIC, P.pic, P.trailfade, 0.25, 2, P.rotation, 0, 0);
}

void player_die(void) {
  if (!P.dead) {
    pdeaths++;
    info_update(false);
    text_screenmessage(TX_DIE0, TX_DIES, 90);
    particles_create(P.x, P.y - 20, 6, 15, 15, 0, true);
    particles_create(P.x, P.y - 20, 40, 25, 15, 1, false);
    shake(15, 15);
    P.on_stage = false;
    P.dead = true;
    timer_add(1500, T_RESPAWN, 0, false);
  }
}
static void player_respawn(void) {
  speedup_off();
  if (level == P.spawnlevel) {
    hallucinate_off(false);
    player_respawngame();
  } else {
    hallucinate_off(true);
    goto_level(P.spawnlevel, true);
  }
}
static void level_reset(void) {
  env_reset();
  for (int k = 0; k < n_reset; k++)
    change_tile(resetnodes[k * 3], resetnodes[k * 3 + 1], resetnodes[k * 3 + 2], true, false, false, true);
  P.hasJetPack = P.usingjetpack = false;
}
static void player_respawngame(void) {
  bool other = false;
  interp(0, LB_RESPAWN, false);
  n_particles = 0;
  P.on_stage = true;
  level_reset();
  if (level != P.spawnlevel) other = true;
  shake(15, 15);
  key_right = key_left = key_up = key_down = false;
  if (world_rot == currentrotation) rotate_to(P.spawnrotation);
  else currentrotation = destrotate = P.spawnrotation;
  P.x = P.spawnx + OFFX, P.y = P.spawny + OFFY;
  if (other) cam_move((int)P.x, (int)P.y, true, 15, false);
  else cam_move((int)P.x, (int)P.y, false, 8, true);
  particles_create(P.x, P.y - 20, 10, 20, 15, 2, false);
  P.gravity = 0, P.speedx = 0, P.onground = false, P.dead = false;
}

/* ---------------------------------------------------------------- keys (managers/KeyboardManager) */
void key_press(int k) {
  switch (k) {
  case GK_LEFT: key_left = true; break;
  case GK_RIGHT: key_right = true; break;
  case GK_UP:
    if (!key_up) player_up();
    key_up = true;
    break;
  case GK_DOWN: key_down = true; break;
  case GK_JET: key_jet = true; break;
  case GK_R:
    if (!P.stopMovement) player_die();
    break;
  }
  if (!pressedkey) {
    pressedkey = true;
    timer_remove_kind(T_AFK);
  }
}
void key_release(int k) {
  switch (k) {
  case GK_LEFT: key_left = false; break;
  case GK_RIGHT: key_right = false; break;
  case GK_UP: key_up = false; break;
  case GK_DOWN: key_down = false; break;
  case GK_JET:
    key_jet = false;
    togglejetpack();
    break;
  }
  if (pressedkey) {
    pressedkey = false;
    timer_add(10000, T_AFK, 0, true);
  } else if (afk_text >= 0) {
    if (afk_text < n_texts) {
      memmove(&texts[afk_text], &texts[afk_text + 1], (size_t)(n_texts - afk_text - 1) * sizeof texts[0]);
      n_texts--;
    }
    afk_text = -1;
  }
}

/* ---------------------------------------------------------------- tools/text/GlitchInterpreter */
#define MAX_PAGES 10
#define MAX_LINES 64
static struct {
  int len, line, loopAt;
  uint16_t at[MAX_LINES];
} pages[MAX_PAGES];
static int n_pages;

static void scripts_load(void) {
  int at = cn_maps[level + 1].script;
  n_pages = cn_scripts[at++];
  n_tmr = 0;
  for (int p = 0; p < n_pages && p < MAX_PAGES; p++) {
    int n = cn_scripts[at++];
    pages[p].len = n < MAX_LINES ? n : MAX_LINES, pages[p].line = 0, pages[p].loopAt = 0;
    for (int l = 0; l < n; l++) {
      if (l < MAX_LINES) pages[p].at[l] = (uint16_t)at;
      at += 2 + cn_scripts[at + 1];
    }
  }
  for (int p = 0; p < n_pages; p++) interp(p, 0, true);
}
static bool goto_line(int label, int page, int add) {
  for (int l = pages[page].line; l < pages[page].len; l++)
    if ((cn_scripts[pages[page].at[l]] >> 8) & label) {
      pages[page].line = l + add;
      interp(page, 0, true);
      return true;
    }
  return false;
}
static double value_of(int kind, int v) {
  switch (kind) {
  case 1: return P.x;
  case 2: return P.y;
  case 3: return P.x / 20;
  case 4: return P.y / 20;
  }
  return v;
}
static int as_int(double v) { return (int)v; }

void interp(int page, int go, bool next) {
  int op, line = 0;
  const int16_t *a = 0;
  if (!go) {
    if (!(page < n_pages && pages[page].line < pages[page].len)) return;
    line = pages[page].line;
    const int16_t *s = cn_scripts + pages[page].at[line];
    pages[page].line++;
    op = s[0] & 255;
    a = s + 2;
  } else {
    op = OP_GOTO;
  }
  switch (op) {
  case OP_PAUSECAM: cam_paused = true; break;
  case OP_STOPMOVE: P.stopMovement = true, P.rotation = 0; break;
  case OP_PAUSE: timer_add(a[0], T_PAGE, page, false); return;
  case OP_SETSPAWN: P.spawnx = a[0] * 20, P.spawny = a[1] * 20; break;
  case OP_DIE: player_die(); break;
  case OP_SETROOM: if (a[0] >= 0) room = a[0]; break;
  case OP_FUNCTION: goto_line(LB_ENDFUNCTION, page, 0); return;
  case OP_GOTO: {
    int label = go ? go : a[0] == 3 ? LB_UNLOCKLASER : LB_RESPAWN;
    for (int p = 0; p < 9 && p < n_pages; p++) {
      int keep = pages[p].line;
      pages[p].line = 0;
      if (!goto_line(label, p, 1)) pages[p].line = keep;
    }
    return;
  }
  case OP_SETTYPEPATH: {
    int x = a[0], y = a[1];
    for (int g = 0; g < a[2] - a[0] + (a[3] - a[1]) + 1; g++) {
      change_tile(x, y, a[4], true, a[5] != 0, true, true);
      if (x != a[2]) x++;
      else if (y != a[3]) y++;
    }
    break;
  }
  case OP_SETTYPE: change_tile(a[0], a[1], a[2], true, a[3] != 0, true, true); break;
  case OP_CREATELASER: laser_create(a[0], a[1], a[2], a[3], a[4]); break;
  case OP_MOVECAM: cam_move(a[0], a[1], true, 15, false); break;
  case OP_RESUMECAM: cam_paused = false; break;
  case OP_RESUMEMOVE: P.stopMovement = false; break;
  case OP_WALK:
    if (a[0] == -1) P.walkSteps = true, P.stepAmount = a[1] > 0 ? a[1] + 1 : a[1] - 1;
    break;
  case OP_JUMP: if (a[0] == -1) P.gravity = a[1]; break;
  case OP_SHAKE: shake(a[0], a[1]); break;
  case OP_UPDATESCREEN:
    if (a[0] == -1) {
      for (int k = 0; k < n_screens; k++) screens[k].text = a[1], screens[k].rot = a[2];
    } else if (a[0] >= 0 && a[0] < n_screens) {
      screens[a[0]].text = a[1], screens[a[0]].rot = a[2];
    } else {
      return; /* (the original: an error, and the script stops here) */
    }
    break;
  case OP_RESETLEVEL: {
    level_reset();
    map_remove();
    map_create(level);
    break;
  }
  case OP_TELEPORT:
    if (n_tps < 16) tps[n_tps++] = (typeof(tps[0])){a[0], a[1], a[2], a[3]};
    break;
  case OP_CREATETEXT: text_create(a[0], a[1], a[2], a[3] / 1000.0, a[5] != 0, a[6]); break;
  case OP_RUNSCRIPT: {
    int pg = a[0] - 1;
    if (pg >= 0 && pg < n_pages) pages[pg].line = 0, interp(pg, 0, true);
    break;
  }
  case OP_IF: {
    int n = cn_scripts[pages[page].at[line] + 1] / 6, correct = 0, required = 0;
    bool all = false;
    for (int k = 0; k < n; k++) {
      const int16_t *c = a + k * 6;
      double v1 = value_of(c[0], c[1]), v2 = value_of(c[3], c[4]);
      bool ok = false;
      switch (c[2]) {
      case 0: ok = v1 == v2; break;
      case 1: ok = v1 != v2; break;
      case 2: ok = as_int(v1) > as_int(v2); break;
      case 3: ok = as_int(v1) < as_int(v2); break;
      case 4: ok = as_int(v1) >= as_int(v2); break;
      case 5: ok = as_int(v1) <= as_int(v2); break;
      }
      correct += ok, required++;
      if (c[5] == 1) all = true;
      else if (c[5] == 2) all = false;
    }
    if ((correct >= required && all) || (correct >= 1 && !all)) {
      interp(page, 0, true);
      return;
    }
    goto_line(LB_ELSEIF, page, 0);
    goto_line(LB_ENDIF, page, 0);
    return;
  }
  case OP_ELSE: case OP_ENDIF: interp(page, 0, true); return;
  case OP_EXIT:
    pages[page].line = 999;
    n_tmr = 0;
    return;
  case OP_STARTBOSS: boss_create(); break;
  case OP_REMOVEBOSS: boss_remove(); break;
  case OP_LOOP:
    pages[page].loopAt = line;
    timer_add(a[0], T_LOOP, page, true);
    return;
  case OP_ENDLOOP: pages[page].line = pages[page].loopAt + 1; return;
  }
  if (next) interp(page, 0, true);
}

/* ---------------------------------------------------------------- the frame (Main.update, Map.update) */
void info_update(bool force) {
  if ((int)(game_ms - hud_timer) >= 1000 || force) hud_deaths = pdeaths, hud_collected = collected, hud_timer = game_ms;
}
static double norm_rot(double r) { /* (a display object's rotation: -180 to 180) */
  while (r > 180) r -= 360;
  while (r < -180) r += 360;
  return r;
}
static void map_update(void) {
  cc_update(&map_tint);
  if (rotating) {
    cam_move((int)(P.x + 10), (int)(P.y + 10), false, 6, false);
    world_rot = norm_rot(world_rot + (destrotate - world_rot) / 5);
    P.rotation = -world_rot;
    if (world_rot >= destrotate - 1 && world_rot <= destrotate + 1) {
      world_rot = destrotate, P.rotation = -world_rot, currentrotation = destrotate, rotating = false;
    }
  }
  if (hallucinate) {
    world_rot += rnd01() - rnd01();
    world_sx += (rnd01() - rnd01()) / 50;
    world_sy += (rnd01() - rnd01()) / 50;
    if (world_rot >= 15) world_rot = 15;
    else if (world_rot <= -15) world_rot = -15;
    world_sx = world_sx >= 1.4 ? 1.4 : world_sx <= 0.9 ? 0.9 : world_sx;
    world_sy = world_sy >= 1.4 ? 1.4 : world_sy <= 0.9 ? 0.9 : world_sy;
  } else if (hoff) {
    world_sx += (1 - world_sx) / 50, world_sy += (1 - world_sy) / 50, world_rot += (0.1 - world_rot) / 20;
    if (world_sx >= 0.99 && world_sx <= 1.01 && world_sy >= 0.99 && world_sy <= 1.01 && world_rot <= 0.2 &&
        world_rot >= -0.2)
      hoff = false, world_sx = world_sy = 1, world_rot = 0;
  }
}
extern void texts_update(void);
extern void trails_update(void);
extern void particles_update(void);

void game_step(void) {
  game_frames++;
  game_ms = (uint32_t)((uint64_t)game_frames * 50 / 3); /* (a 60th of a second) */
  if (!pausegame) {
    map_update();
    info_update(false);
    shake_update();
    cam_update();
    texts_update();
    if (boss.created) boss_update();
    if (!rotating) {
      timers_update();
      trails_update();
      env_update();
      particles_update();
      if (!P.dead) player_update();
    }
    cam_move((int)(P.x + 10), (int)(P.y + 10), false, 15, false);
  }
  if (fadingscreen) {
    if (fadein) {
      fade_alpha += fadespeed;
      if (fade_alpha >= 1) {
        fade_alpha = 1, fadein = false;
        if (doublefade && !faded) {
          faded = true;
          if (target_dir >= 0) target_run();
        } else {
          fadingscreen = false;
        }
      }
    } else {
      fade_alpha -= fadespeed;
      if (fade_alpha <= 0) {
        fade_alpha = 0, fade_on = false;
        if (doublefade && !faded) {
          fadein = true, faded = true;
          if (target_dir >= 0) target_run();
        } else {
          fadingscreen = false;
        }
      }
    }
  }
  if (key_hint_frames > 0) key_hint_frames--;
}

/* ---------------------------------------------------------------- starting */
void game_new(void) {
  static bool parts_made;
  (void)parts_made;
  cc_init(&map_tint, 0.002, 1.4);
  n_mods = 0, pdeaths = collected = 0;
  memset(crystals_got, 0, sizeof crystals_got);
  level = 0, lastlevel = -1, exitedmap = false, room = 0;
  world_rot = 0, world_sx = world_sy = 1, destrotate = currentrotation = 0, rotating = false;
  n_tmr = 0;
  fade_alpha = 1, fade_on = true, fadingscreen = true, doublefade = false, fadein = false, faded = false, fadespeed = 0.001;
  pausegame = false, target_dir = -1, pressedkey = true, key_left = key_right = key_up = key_down = key_jet = false;
  memset(&boss, 0, sizeof boss);
  boss.lineoffset = 5, boss.sx = boss.sy = 1, boss.alpha = 1;
  map_create(-1);
  player_create();
  key_hint_frames = 0;
  info_update(true);
}
bool game_can_exit(void) { return level != -1 && !P.dead && !rotating && level <= 27 && target_dir < 0; }
void game_exit_to_menu(void) {
  if (game_can_exit()) {
    exitedmap = true;
    playerpoints[8] = -1;
    savedx = (int)P.x, savedy = (int)P.y, saveddestrotation = destrotate, savedworldrotation = world_rot;
    map_move(3);
  }
}
/* a saved game: the menu, as after its exit button, its way right back to where the cat was */
void game_resume_at(int lvl, int x, int y, int rot) {
  if (lvl < 0 || lvl > 29) return;
  lastlevel = lvl;
  savedx = x, savedy = y, saveddestrotation = rot, savedworldrotation = rot;
  playerpoints[6] = 140, playerpoints[7] = 15, playerpoints[5] = lvl;
  ppx = 140 * 20, ppy = 15 * 20;
  cam_move(ppx, ppy, true, 15, false);
  player_reset();
}
/* the save's view of the game */
int game_mods(const void **m) {
  *m = mods;
  return n_mods;
}
void game_get_resume(int *lvl, int *x, int *y, int *rot) {
  if (game_can_exit() || (level >= 0 && !P.dead && !rotating && target_dir < 0)) {
    *lvl = level, *x = (int)P.x, *y = (int)P.y, *rot = destrotate;
  } else if (level == -1 && lastlevel >= 0 && playerpoints[5] == lastlevel) {
    *lvl = lastlevel, *x = savedx, *y = savedy, *rot = saveddestrotation;
  } else {
    *lvl = P.spawnlevel, *x = P.spawnx + OFFX, *y = P.spawny + OFFY, *rot = P.spawnrotation;
  }
}

/* a level straight away, its start the checkpoint (the tests, and pictures) */
/* (tests) the level's teleporters: start and end tiles */
int game_debug_teleports(int out[16][4]) {
  for (int k = 0; k < n_tps; k++) out[k][0] = tps[k].sx, out[k][1] = tps[k].sy, out[k][2] = tps[k].ex, out[k][3] = tps[k].ey;
  return n_tps;
}
void game_debug_goto(int lv) {
  level = lv;
  target_dir = 4, target_respawn = false;
  target_run();
  fade_alpha = 0, fade_on = false, fadingscreen = false;
  P.spawnx = ppx, P.spawny = ppy, P.spawnlevel = level, P.spawnrotation = destrotate;
}
