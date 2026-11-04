/* The heroes, as the later Flash build has them (SoldierHero.as and its seven: Gerald, Malik, Alleria, Bolin,
 * Magnus, Ignus, Denas; their wildcat and illusions; their bullets and fields). Numbers: GameSettings.heroes.
 *
 * The hero is a soldier (B.so) of kind SK_HERO: soldier.c walks it and fights with it, this file adds what
 * SoldierHero adds: levels 1 to 10 from experience (in each battle, as the Flash game), the two skills that come
 * with them, its shots, the walk around the battlefield (the level's 12.5 px grid) and its respawn. */
#include <math.h>
#include <string.h>
#include "hero.h"
#include "anim.h"
#include "gfx.h"
#include "res.h"

#define MAP_X 40
#define SX(x) (MAP_X + (int)lroundf((x) * 0.4f))
#define SY(y) ((int)lroundf((y) * 0.4f))

uint8_t hero_pick;

/* ------------------------------------------------------------------ the numbers (GameSettings.heroes) */
static const int16_t xp_table[10] = {0, 300, 900, 2000, 4000, 8000, 12000, 16000, 20000, 26000};   /* masterXp */
typedef struct {
  int16_t hp0, hp_step;
  uint8_t regen[10], armor[10], lo[10], hi[10], rlo[10], rhi[10];
  float speed, xpmul, mod1, mod2;
  uint8_t charge, charge_dmg, ao, lvlup, reload, range;
  int16_t respawn;
} HeroStat;
static const HeroStat HS[HERO_COUNT] = {
  {0},
  /* Gerald (heroGeneral) */
  {400, 20, {100, 105, 110, 115, 120, 125, 130, 135, 140, 145}, {30, 30, 40, 40, 50, 50, 60, 60, 70, 80},
   {11, 12, 14, 15, 17, 18, 20, 21, 23, 24}, {18, 20, 23, 25, 28, 30, 33, 35, 38, 40}, {0}, {0},
   1.65f, 3, 1, 2, 12, 6, 19, 27, 30, 130, 450},
  /* Malik (heroMalik) */
  {450, 30, {113, 120, 128, 135, 143, 150, 158, 165, 173, 180}, {0, 10, 10, 20, 20, 30, 30, 40, 40, 50},
   {14, 16, 18, 19, 21, 22, 24, 26, 27, 29}, {22, 24, 26, 29, 31, 34, 36, 38, 41, 43}, {0}, {0},
   1.45f, 1.9f, 1, 1, 12, 6, 22, 22, 30, 130, 450},
  /* Alleria (heroArcher) */
  {250, 20, {63, 68, 73, 78, 83, 88, 93, 98, 103, 108}, {0},
   {2, 4, 6, 7, 9, 10, 12, 14, 15, 17}, {4, 6, 8, 11, 13, 16, 18, 20, 23, 25},
   {7, 8, 9, 10, 11, 12, 13, 14, 14, 15}, {12, 14, 15, 17, 18, 20, 21, 23, 24, 26},
   2.2f, 2.5f, 0.5f, 1, 15, 6, 19, 19, 30, 70, 450},
  /* Bolin (heroDwarf) */
  {400, 30, {100, 108, 115, 123, 130, 138, 145, 153, 160, 168}, {0},
   {9, 11, 12, 14, 15, 17, 18, 20, 21, 23}, {15, 18, 20, 23, 25, 28, 30, 33, 35, 38},
   {9, 11, 12, 14, 15, 17, 18, 20, 21, 23}, {15, 18, 20, 23, 25, 28, 30, 33, 35, 38},
   1.5f, 3, 0.5f, 1, 17, 5, 18, 18, 30, 92, 450},
  /* Magnus (heroMage) */
  {170, 20, {43, 48, 53, 58, 63, 68, 73, 78, 83, 88}, {0},
   {1, 2, 2, 3, 4, 5, 6, 6, 7, 8}, {2, 4, 5, 6, 7, 8, 10, 11, 12, 13},
   {9, 11, 12, 14, 15, 17, 18, 20, 21, 23}, {27, 32, 36, 41, 45, 50, 54, 59, 63, 68},
   0.8f, 2.1f, 1, 1, 22, 12, 18, 29, 30, 70, 450},
  /* Ignus (heroFire) */
  {400, 30, {100, 108, 115, 123, 130, 138, 145, 153, 160, 168}, {0},
   {18, 20, 21, 23, 24, 26, 27, 29, 30, 32}, {30, 33, 35, 38, 40, 43, 45, 48, 50, 53}, {0}, {0},
   2.2f, 2, 1, 2, 19, 9, 18, 24, 30, 90, 360},
  /* Denas (heroKing) */
  {300, 20, {75, 80, 85, 90, 95, 100, 105, 110, 115, 120}, {0},
   {11, 14, 17, 20, 23, 25, 28, 31, 34, 37}, {19, 23, 28, 33, 38, 42, 47, 52, 56, 61}, {0}, {0},
   1.5f, 2.2f, 1, 2, 19, 8, 21, 27, 45, 70, 450},
};
static const uint8_t room_stats[HERO_COUNT][4] = {{0}, {8, 6, 0, 5}, {8, 7, 0, 4}, {3, 3, 6, 6}, {6, 5, 5, 4},
                                                  {2, 2, 8, 8}, {6, 8, 0, 6}, {5, 6, 6, 3}};
void hero_stats(int id, uint8_t out[4]) { memcpy(out, room_stats[id < HERO_COUNT ? id : 0], 4); }

static const char *const names[HERO_COUNT] = {"", "Sir Gerald", "Malik Hammerfury", "Alleria Swiftwind", "Bolin Farslayer",
                                              "Magnus Spellbane", "Ignus", "King Denas"};
static const char *const descs[HERO_COUNT] = {
  "",
  "Defender of the righteous, punisher of the dark, protector of the innocent, Gerald is the uncanny of Linirea's armed forces!",
  "A Hero with unmatched strength, untamed mind, and unbroken will. A destructive force, an unleashed fury, a bullet train attitude!",
  "Silent as the night, light as a feather and deadly beautiful. Too many have fallen by her charming gaze and many more by her fatal bow.",
  "Thunder and lightning have nothing on this crackshot! Let loose a hail of bullets, and make them fear where they step with an arsenal of mines!",
  "Unleash the arcane forces of Linirea, let loose a mystical storm, and spellbind your enemies! With his arcane arts and thirst for knowledge.",
  "Burn the kingdom's enemies with the blaze of a thousand stars! Forge victory in his raging flames, consuming evil in fire and brimstone!",
  "Your majesty itself brings an arsenal of power and courage to the battlefield. Long live the king!"};
static const char *const specials[HERO_COUNT] = {"", "Courage, Shield of Retribution", "Hammer Smash, Earthquake",
                                                 "Multishot, Call of the Wild", "Mine Layer, Tar Bomb", "Mirage, Arcane Storm",
                                                 "Surge of Flame, Flaming Frenzy", "Royal Edict, Catapult Barrage"};
const char *hero_name(int id) { return id > 0 && id < HERO_COUNT ? names[id] : ""; }
const char *hero_desc(int id) { return id > 0 && id < HERO_COUNT ? descs[id] : ""; }
const char *hero_special(int id) { return id > 0 && id < HERO_COUNT ? specials[id] : ""; }

/* each hero's animations: idle, run, fight, dead, respawn, level up; then its own */
static const int16_t AN[HERO_COUNT][6] = {
  {0},
  {AN_H_GERALD_IDLE, AN_H_GERALD_RUNNING, AN_H_GERALD_FIGHTING, AN_H_GERALD_DEAD, AN_H_GERALD_RESPAWNING, AN_H_GERALD_LVLUP},
  {AN_H_MALIK_IDLE, AN_H_MALIK_RUNNING, AN_H_MALIK_FIGHTING, AN_H_MALIK_DEAD, AN_H_MALIK_RESPAWNING, AN_H_MALIK_LVLUP},
  {AN_H_ALLERIA_IDLE, AN_H_ALLERIA_RUNNING, AN_H_ALLERIA_FIGHTING, AN_H_ALLERIA_DEAD, AN_H_ALLERIA_RESPAWNING, AN_H_ALLERIA_LVLUP},
  {AN_H_BOLIN_IDLE, AN_H_BOLIN_RUNNING, AN_H_BOLIN_FIGHTING, AN_H_BOLIN_DEAD, AN_H_BOLIN_RESPAWNING, AN_H_BOLIN_LVLUP},
  {AN_H_MAGNUS_IDLE, AN_H_MAGNUS_RUNNING, AN_H_MAGNUS_FIGHTING, AN_H_MAGNUS_DEAD, AN_H_MAGNUS_RESPAWNING, AN_H_MAGNUS_LVLUP},
  {AN_H_IGNUS_IDLE, AN_H_IGNUS_RUNNING, AN_H_IGNUS_FIGHTING, AN_H_IGNUS_DEAD, AN_H_IGNUS_RESPAWNING, AN_H_IGNUS_LVLUP},
  {AN_H_DENAS_IDLE, AN_H_DENAS_RUNNING, AN_H_DENAS_FIGHTING1, AN_H_DENAS_DEAD, AN_H_DENAS_RESPAWNING, AN_H_DENAS_LVLUP},
};
enum { A_HIDLE, A_HRUN, A_HFIGHT, A_HDEAD, A_HSPAWN, A_HLVLUP };

/* ------------------------------------------------------------------ the hero's state */
enum {   /* what it is busy with (each its own counter, as the game's flags) */
  ACT_NONE, ACT_LVLUP, ACT_SHOOT, ACT_SKILL1, ACT_SKILL2, ACT_TELEOUT, ACT_TELEIN, ACT_SURGE_END
};
enum { HB_THROWN = BU_HERO, HB_BOLT, HB_LOB };   /* bullets: Denas' throws, Magnus' bolts, Bolin's mines and tar */

#define MAX_PATH 96
#define MAX_MINES 10
static struct {
  uint8_t id, spawned, level, sk1, sk2;
  int8_t so, pet;
  int32_t xp;
  uint8_t act;
  int16_t act_t, act_len, act_hit;
  int16_t cd0, cd1, cd2;           /* the shot's and the skills' counters (count up) */
  int8_t target;                   /* the shot's enemy */
  Pt aim;
  uint8_t shot, weapon;            /* Bolin's shots so far; Denas' weapon (1 to 4) */
  int8_t shot_done[4];
  uint8_t surge, surge_end;        /* Ignus: dashing; the end */
  int16_t surge_t;
  Pt tele;                         /* Magnus' teleport */
  int16_t cat_wait;                /* Alleria: callOfWildReloadTime (0 while a cat lives) */
  int16_t road_path, road_node;    /* the road node nearest the rally (§_-2x§, §_-Fs§) */
  Pt cat_at;                       /* §_-NV§ */
  uint8_t npath, ipath;
  uint16_t path[MAX_PATH];         /* the cells to walk through (x | y << 8) */
} H;

/* the fields: mines, tar, rains of rocks or arcane drops, quakes along the road, drops about to hit */
static struct { uint8_t on, lvl, hit; int16_t t; float x, y; } mines[MAX_MINES];
static struct { uint8_t on, lvl, end; int16_t t, tick, dur; float x, y; } tar[3];
static struct { uint8_t on, kind, lvl, n, max, out; int16_t t, deg; float x, y, ox; } volley[2];   /* kind 0 rocks, 1 drops */
static struct { uint8_t on, lvl, path, lane, sub, k; int16_t node, t; } quake;
static struct { uint8_t on, lvl; int16_t t; float x, y; } drops[8];

static const uint8_t *level_data(void) { return section(SEC_HEROES) + B.index * HERO_LEVEL_BYTES; }
Soldier *hero_soldier(void) { return H.id && H.spawned ? SO(H.so) : 0; }
static const HeroStat *hs(void) { return &HS[H.id]; }
static float dist(float x0, float y0, float x1, float y1) { return sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)); }
static bool in_ell(float cx, float cy, float w, float px, float py) { return ellipse_contains(cx, cy, w, w * RANGE_RATIO, px, py); }
static bool usable(const Enemy *e) { return e->alive && e->active && !e->dead; }
static int dmg_of(int lo, int hi) { return rnd_dmg(lo, hi); }

/* ------------------------------------------------------------------ experience and levels */
static void apply_level(Soldier *so) {   /* levelUpWithAnimation's stats */
  const HeroStat *s = hs();
  int i = H.level - 1;
  so->health = so->init_health = (int16_t)(s->hp0 + s->hp_step * i);
  so->regen = s->regen[i], so->armor = s->armor[i];
  so->min_dmg = s->lo[i], so->max_dmg = s->hi[i];
  if (so->courage_t) so->armor = (int16_t)(so->armor + 5 * so->courage_lvl), so->min_dmg += 2 * so->courage_lvl, so->max_dmg += 2 * so->courage_lvl;
  int l = H.level;
  if (l == 2 || l == 5 || l == 8) H.sk1 = (uint8_t)(l == 2 ? 1 : l == 5 ? 2 : 3);
  if (l == 4 || l == 7 || l == 10) H.sk2 = (uint8_t)(l == 4 ? 1 : l == 7 ? 2 : 3), H.cat_wait = 20;   /* (frames, as upgradeCallOfWild sets it) */
  Soldier *cat = SO(H.pet);
  if (H.id == HERO_ALLERIA && cat && cat->kind == SK_WILDCAT && (l == 4 || l == 7 || l == 10)) {   /* §_-U§ */
    cat->level = H.sk2;
    cat->health = cat->init_health = (int16_t)(250 * H.sk2);
    cat->min_dmg = (int16_t)(2 + 4 * H.sk2), cat->max_dmg = (int16_t)(4 + 4 * H.sk2);
  }
}

static void stop_special(void) {
  H.act = ACT_NONE, H.act_t = 0;
  H.surge = 0, H.surge_end = 0;
  H.shot = 0;
}

static void gain_xp(int n) {   /* §_-B5§ */
  Soldier *so = hero_soldier();
  if (!so) return;
  H.xp += n;
  if (H.level == 10 || H.xp < xp_table[H.level]) return;
  H.level++;
  apply_level(so);
  stop_special();   /* (the level up's animation: busy for its frames) */
  if (so->charging) so->charging = 0, so->charge_t = 0;
  H.act = ACT_LVLUP, H.act_t = 0, H.act_len = hs()->lvlup, H.act_hit = -1;
  soldier_play(so, AN[H.id][A_HLVLUP]);
  gain_xp(0);
}
static void skill_xp(int which, int lvl) { gain_xp((int)(50 * lvl * (which == 1 ? hs()->mod1 : hs()->mod2))); }   /* §_-R2§ */

/* ------------------------------------------------------------------ the walk grid */
static bool walkable(int cx, int cy) {
  if (cx < 0 || cy < 0 || cx >= 56 || cy >= 48) return false;
  return level_data()[cy * 7 + (cx >> 3)] >> (cx & 7) & 1;
}

/* §_-5w§: the way from where it is to its rally, cell by cell (8 ways). The game runs A*; here a breadth-first
 * flood from the rally (on the stack, while it runs) gives a shortest way as well. */
static void find_path(Soldier *so) {
  H.npath = H.ipath = 0;
  int sx = (int)lroundf(so->x / 12.5f), sy = (int)lroundf(so->y / 12.5f);
  int ex = (int)lroundf(so->rx / 12.5f), ey = (int)lroundf(so->ry / 12.5f);
  if (!walkable(sx, sy) || !walkable(ex, ey) || (sx == ex && sy == ey)) return;
  uint8_t d[48 * 56];
  memset(d, 255, sizeof d);
  d[ey * 56 + ex] = 0;
  bool found = false;
  for (int k = 0; k < 254 && !found; k++) {
    bool grew = false;
    for (int y = 0; y < 48; y++)
      for (int x = 0; x < 56; x++) {
        if (d[y * 56 + x] != k) continue;
        for (int dy = -1; dy <= 1; dy++)
          for (int dx = -1; dx <= 1; dx++) {
            int nx = x + dx, ny = y + dy;
            if ((dx || dy) && walkable(nx, ny) && d[ny * 56 + nx] == 255) d[ny * 56 + nx] = (uint8_t)(k + 1), grew = true;
          }
      }
    if (d[sy * 56 + sx] != 255) found = true;
    if (!grew) break;
  }
  if (!found) return;
  int x = sx, y = sy;
  while (d[y * 56 + x] > 0 && H.npath < MAX_PATH) {
    int bx = x, by = y, bd = d[y * 56 + x];
    for (int dy = -1; dy <= 1; dy++)
      for (int dx = -1; dx <= 1; dx++) {
        int nx = x + dx, ny = y + dy;
        if (!walkable(nx, ny) || d[ny * 56 + nx] >= bd) continue;
        if (d[ny * 56 + nx] < bd || (dx == 0 || dy == 0)) bx = nx, by = ny, bd = d[ny * 56 + nx];
      }
    if (bx == x && by == y) break;
    x = bx, y = by;
    H.path[H.npath++] = (uint16_t)(x | y << 8);
  }
  if (H.npath <= 2) H.npath = 0;   /* (the game walks straight when the way is that short) */
}

static bool path_walk(Soldier *so) {   /* SoldierHero.walk on its path; false when it is done */
  if (H.ipath >= H.npath) return false;
  float tx = (H.path[H.ipath] & 255) * 12.5f, ty = (H.path[H.ipath] >> 8) * 12.5f;
  if (dist(so->x, so->y, tx, ty) <= so->speed) {
    if (++H.ipath >= H.npath) {
      H.npath = H.ipath = 0;
      return false;
    }
    tx = (H.path[H.ipath] & 255) * 12.5f, ty = (H.path[H.ipath] >> 8) * 12.5f;
  }
  float a = atan2f(ty - so->y, tx - so->x);
  so->left = tx < so->x;
  so->x += cosf(a) * so->speed, so->y += sinf(a) * so->speed;
  return true;
}

/* Level7 and Level8's tunnels: a road's nodes underground (from, to), where no hero goes */
static bool tunnel(int r, int i) {
  if (B.index == 6) return (r == 1 && i >= 71 && i <= 203) || (r == 2 && i >= 71 && i <= 223);
  if (B.index == 7) return r == 1 && i >= 70 && i <= 174;
  return false;
}

/* the road node nearest a point, within 30 (§_-JL§: lane 0 of every road, not in a tunnel) */
static bool road_near(Pt p, int *path, int *node) {
  float best = 0;
  bool got = false;
  for (int r = 0; r < B.npaths; r++) {
    int n = path_len(r, 0);
    for (int i = 0; i < n; i++) {
      if (tunnel(r, i)) continue;
      Pt q = path_node(r, 0, i);
      float d = dist(q.x, q.y, p.x, p.y);
      if (d < 30 && (!got || best > d)) best = d, *path = r, *node = i, got = true;
    }
  }
  return got;
}
bool hero_rally_ok(Pt p) {
  int a, b;
  return road_near(p, &a, &b);
}

static void road_point(Pt p) {   /* Alleria's and Bolin's §_-JL§ */
  int r, n;
  if (!road_near(p, &r, &n)) return;
  H.road_path = (int16_t)r, H.road_node = (int16_t)n;
  if (n != 0) H.cat_at = path_node(r, 0, n - 5 < 0 ? 0 : n - 5);
}

static void pet_rally(Soldier *cat) {   /* SoldierWildCat.§_-9p§ */
  cat->rx = H.cat_at.x, cat->ry = H.cat_at.y;
  if (cat->dead || cat->respawning) return;
  cat->active = 0;
  soldier_stop_fight(cat);
  soldier_go_home(cat);
}

void hero_set_rally(Pt p) {   /* §_-J1§.freeRally, the hero's changeRallyPoint */
  Soldier *so = hero_soldier();
  if (!so) return;
  if (H.act == ACT_TELEOUT || H.act == ACT_TELEIN) return;
  so->rx = p.x, so->ry = p.y;
  fx_add(AN_T_RALLY, p.x, p.y, false, false);
  if (H.id == HERO_ALLERIA || H.id == HERO_BOLIN) road_point(p);
  if (so->dead || so->respawning) return;
  so->active = 0;
  soldier_stop_fight(so);
  soldier_go_home(so);
  stop_special();
  so->speed = hs()->speed;
  if (H.id == HERO_MAGNUS) {   /* far: he teleports */
    H.npath = 0;
    if (dist(so->x, so->y, p.x, p.y) > 80) {
      H.tele = p;
      H.act = ACT_TELEOUT, H.act_t = 0, H.act_len = 14, H.act_hit = -1;
      soldier_play(so, AN_H_MAGNUS_TELEPORTHOUT);
    }
    return;
  }
  find_path(so);
  Soldier *cat = SO(H.pet);
  if (H.id == HERO_ALLERIA && cat && cat->kind == SK_WILDCAT) pet_rally(cat);
}

int hero_level(void) { return H.level; }
int hero_skill(int k) { return k == 1 ? H.sk1 : H.sk2; }

int hero_respawn_left(void) {
  Soldier *so = hero_soldier();
  if (!so || !so->dead) return 0;
  return so->dead_time - so->dead_t;
}

/* ------------------------------------------------------------------ the battle */
void hero_battle_start(void) {
  memset(&H, 0, sizeof H);
  memset(mines, 0, sizeof mines), memset(tar, 0, sizeof tar), memset(volley, 0, sizeof volley);
  memset(&quake, 0, sizeof quake), memset(drops, 0, sizeof drops);
  H.so = H.pet = -1;
  /* only in the campaign, or in stages that bring it to every challenge (§_-1q§) */
  if (hero_pick && hero_pick < HERO_COUNT && (B.mode == MODE_CAMPAIGN || level_data()[48 * 7 + 4])) H.id = hero_pick;
}

static void spawn(void) {   /* Level.§_-04§: at the first wave, on the defense point */
  const uint8_t *d = level_data() + 48 * 7;
  Pt at = {(float)rds16(d), (float)rds16(d + 2)};
  int i = soldier_new(SK_HERO, -1, at, at);
  H.spawned = 1;
  if (i < 0) {
    H.id = 0;
    return;
  }
  H.so = (int8_t)i;
  if (H.id == HERO_ALLERIA || H.id == HERO_BOLIN) road_point(at);
}

static uint8_t illusion_level;
int hero_soldier_new(Soldier *so) {
  if (so->kind == SK_HERO) {
    const HeroStat *s = hs();
    H.level = 1;
    so->speed = s->speed;
    so->charge = s->charge, so->charge_dmg = s->charge_dmg;
    so->reload = (int16_t)(s->reload - s->charge);
    so->range_w = s->range, so->range_h = (int16_t)(s->range * RANGE_RATIO);
    so->regen_reload = FPS, so->dead_time = s->respawn;
    so->idle_time = 30;
    /* it starts dead, a frame from its respawn */
    so->hidden = 1, so->respawning = 0, so->dead = 1, so->dead_t = (int16_t)(s->respawn - 1), so->lifes = 1;
    apply_level(so);
    return 1;
  }
  if (so->kind == SK_WILDCAT) {
    so->level = H.sk2;
    so->health = so->init_health = (int16_t)(250 * so->level), so->armor = 0;
    so->min_dmg = (int16_t)(2 + 4 * so->level), so->max_dmg = (int16_t)(4 + 4 * so->level);
    so->regen = 75, so->regen_reload = FPS;
    so->charge = 16, so->charge_dmg = 9, so->reload = 30 - 16;
    so->range_w = 200, so->range_h = (int16_t)(200 * RANGE_RATIO);
    so->speed = 1.7f, so->dead_time = 10, so->door_wait = 19;
    so->hidden = 1, so->respawning = 0, so->dead = 1, so->dead_t = 9, so->lifes = 1;
    return 1;
  }
  /* SK_ILLUSION: the mirage's level (its stats from the hero's table at that index, as the game reads them) */
  so->level = illusion_level ? illusion_level : 1;
  int l = so->level - 1;
  const HeroStat *s = &HS[HERO_MAGNUS];
  so->health = so->init_health = (int16_t)((s->hp0 + s->hp_step * l) * 0.3f), so->armor = 0;
  so->min_dmg = (int16_t)(s->lo[l] * 0.2f), so->max_dmg = (int16_t)(s->hi[l] * 0.2f);
  so->regen = 0, so->regen_reload = FPS;
  so->charge = 22, so->charge_dmg = 12, so->reload = 30 - 22;
  so->range_w = 170, so->range_h = (int16_t)(170 * RANGE_RATIO);
  so->speed = 0.8f, so->dead_time = 14;
  so->life_time = 10 * FPS;
  so->hidden = 0, so->respawning = 1, so->dead = 0, so->lifes = 2, so->door_t = 0;
  return 1;
}

void hero_soldier_kind(int kind, int16_t v[10]) {
  /* charge, charge_dmg, speed10, respawn_wait, x_adj, idle, run, fight, dead, spawn */
  if (kind == SK_WILDCAT) {
    int16_t w[10] = {16, 9, 17, 19, 5, AN_H_WILDCAT_IDLE, AN_H_WILDCAT_RUNNING, AN_H_WILDCAT_FIGHTING, AN_H_WILDCAT_DEAD, AN_H_WILDCAT_RESPAWNING};
    memcpy(v, w, sizeof w);
  } else if (kind == SK_ILLUSION) {
    int16_t w[10] = {22, 12, 8, 6, 5, AN_H_ILLUSION_IDLE, AN_H_ILLUSION_RUNNING, AN_H_ILLUSION_FIGHTING, AN_H_ILLUSION_DEAD, AN_H_ILLUSION_RESPAWNING};
    memcpy(v, w, sizeof w);
  } else {
    int id = H.id ? H.id : 1;
    int16_t w[10] = {HS[id].charge, HS[id].charge_dmg, 15, HS[id].ao, id == HERO_DENAS ? 17 : 5,
                     AN[id][A_HIDLE], AN[id][A_HRUN], AN[id][A_HFIGHT], AN[id][A_HDEAD], AN[id][A_HSPAWN]};
    memcpy(v, w, sizeof w);
  }
}

static void respawn(Soldier *so) {   /* SoldierHero.respawn */
  so->active = 1, so->dead = 0, so->respawning = 0, so->walking = 1;
  so->charging = 0, so->blocking = 0, so->fighting = 0, so->idle = 0, so->hidden = 0;
  so->dx = so->rx, so->dy = so->ry;
  so->health = so->init_health;
  so->dead_t = 0;
  so->speed = hs()->speed;
  soldier_play(so, AN[H.id][A_HRUN]);
  find_path(so);
}

void hero_died(Soldier *so) {   /* stopOnDead */
  if (so->kind == SK_HERO) {
    stop_special();
    H.npath = 0;
    so->speed = hs()->speed;
  } else if (so->kind == SK_WILDCAT && so->lifes == 2) {   /* (it goes for good: Alleria waits again) */
  }
}

/* ------------------------------------------------------------------ shooting */
/* §_-Em§: an enemy in the shot's ellipse, far enough; the last one in the list (Alleria: the first) */
static int shot_target(const Soldier *so, float w, float min_d, bool first, bool ground) {
  int got = -1;
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy *e = &B.en[i];
    if (!usable(e) || (ground && e->flying)) continue;
    if (!in_ell(so->x, so->y, w, e->x, e->y) || dist(so->x, so->y, e->x, e->y) <= min_d) continue;
    got = i;
    if (first) break;
  }
  return got;
}

static void arrow(Soldier *so, int target, int dmg, bool multi) {   /* Alleria's arrows (§const§, ArrowAlleriaMultishoot) */
  Enemy *e = EN(target);
  Bullet *b = e ? bullet_new(BU_ARROW) : 0;
  if (!b) return;
  b->x0 = b->x = so->x, b->y0 = b->y = so->y - 12;
  b->target = (int8_t)target;
  b->level = 1, b->min_dmg = b->max_dmg = (int16_t)(dmg ? dmg : 1);
  b->t1 = multi ? (int16_t)rnd_round(12, 20) : 15;
  float tx = e->x + enemy_stats[e->type].x_adj, ty = e->y + enemy_stats[e->type].y_adj;
  if (!e->blocked && !e->held) tx += e->xs * b->t1 + rnd() * 10, ty += e->ys * b->t1 + rnd() * 10 - 10;   /* (where it will be) */
  b->g = 1;
  b->xs = (tx - b->x0) / b->t1, b->ys = (ty - b->y0 - b->g * b->t1 * b->t1 / 2) / b->t1;
}

static void throw_at(Soldier *so, int target, Pt at, int t1, int dmg) {   /* Denas' throws (§_-EV§) */
  Bullet *b = bullet_new(HB_THROWN);
  if (!b) return;
  b->x0 = b->x = so->x + (so->left ? 6 : -6), b->y0 = b->y = so->y - 24;
  b->target = (int8_t)target, b->level = H.weapon;
  b->min_dmg = b->max_dmg = (int16_t)dmg;
  b->t1 = (int16_t)t1;
  Enemy *e = EN(target);
  float tx = at.x, ty = at.y;
  if (e && e->blocked) tx = e->x + enemy_stats[e->type].x_adj, ty = e->y + enemy_stats[e->type].y_adj;
  else if (e) tx = e->x + enemy_stats[e->type].x_adj + rnd() * 10, ty = e->y + enemy_stats[e->type].y_adj + rnd() * 10 - 10;
  b->tx = tx, b->ty = ty, b->g = 1;
  b->xs = (tx - b->x0) / t1, b->ys = (ty - b->y0 - b->g * t1 * t1 / 2.0f) / t1;
}

static void bolt(float x, float y, int target, int dmg) {   /* Magnus' (and his illusions') bolt: homing, magic */
  Bullet *b = bullet_new(HB_BOLT);
  if (!b) return;
  b->x = x, b->y = y - 19;
  b->target = (int8_t)target, b->acc = 1, b->acc_max = 20, b->first = 1;   /* (maxAceleration 20) */
  b->min_dmg = b->max_dmg = (int16_t)dmg;
  Enemy *e = EN(target);
  if (e) b->tx = e->x + enemy_stats[e->type].x_adj, b->ty = e->y + enemy_stats[e->type].y_adj;
}

static void lob(float x, float y, Pt to, int t1, int what, int lvl) {   /* Bolin's mines (what 0) and tar bombs (1) */
  Bullet *b = bullet_new(HB_LOB);
  if (!b) return;
  b->x0 = b->x = x, b->y0 = b->y = y;
  b->tx = to.x, b->ty = to.y, b->t1 = (int16_t)t1, b->g = 1;
  b->level = (uint8_t)lvl, b->bombs = (int16_t)what;
  b->xs = (to.x - x) / t1, b->ys = (to.y - y - b->g * t1 * (t1 / 2.0f)) / t1;
}

/* the range shot each hero has (Gerald, Malik and Ignus have none) */
static bool shoot(Soldier *so) {
  int id = H.id;
  if (id != HERO_ALLERIA && id != HERO_BOLIN && id != HERO_MAGNUS && id != HERO_DENAS) return false;
  if (H.act != ACT_NONE && H.act != ACT_SHOOT) return false;
  if (so->fighting || so->walking) return false;
  if (H.act == ACT_NONE) {
    int reload = id == HERO_ALLERIA ? 18 - 13 : id == HERO_BOLIN ? 60 : id == HERO_MAGNUS ? 30 : 45;
    if (H.cd0 < reload) return false;
    int t = id == HERO_ALLERIA ? shot_target(so, 290, 30, true, false)
          : id == HERO_BOLIN ? shot_target(so, 240, 36, false, false)
          : shot_target(so, 220, 36, false, false);
    if (t < 0) return false;
    Enemy *e = &B.en[t];
    H.target = (int8_t)t, H.aim = (Pt){e->x + enemy_stats[e->type].x_adj, e->y + enemy_stats[e->type].y_adj};
    so->left = e->x < so->x;
    H.act = ACT_SHOOT, H.act_t = 0;
    if (id == HERO_ALLERIA) H.act_len = 13, soldier_play(so, AN_H_ALLERIA_RANGE);
    else if (id == HERO_MAGNUS) H.act_len = 33, soldier_play(so, AN_H_MAGNUS_RANGE);
    else if (id == HERO_DENAS) {
      H.act_len = 19;
      hero_attack_anim(so);
      H.aim = enemy_road(e, 3);
    } else {   /* Bolin aims (up, across or down), then three shots */
      H.act_len = 28, H.shot = 0, H.cd0 = 0;
      memset(H.shot_done, -1, sizeof H.shot_done);
      float ang = atan2f(-(H.aim.y - so->y), H.aim.x - so->x) * 180 / (float)M_PI;
      if (ang < 0) ang += 360;
      soldier_play(so, ang > 45 && ang < 135 ? AN_H_BOLIN_AIMUP : ang > 240 && ang < 315 ? AN_H_BOLIN_AIMDOWN : AN_H_BOLIN_AIMLATERAL);
      H.weapon = ang > 45 && ang < 135 ? 1 : ang > 240 && ang < 315 ? 2 : 0;
    }
    return true;
  }
  H.act_t++;
  Enemy *e = EN(H.target);
  if (id == HERO_ALLERIA) {
    if (H.act_t == 7 && e && usable(e)) {
      int d = dmg_of(hs()->rlo[H.level - 1], hs()->rhi[H.level - 1]);
      arrow(so, H.target, d, false);
      gain_xp((int)(d * hs()->xpmul));
    }
    if (H.act_t < H.act_len) return true;
    H.cd0 = 0;
  } else if (id == HERO_MAGNUS) {
    if (H.act_t == 11 && e) {
      int d = dmg_of(hs()->rlo[H.level - 1], hs()->rhi[H.level - 1]);
      bolt(so->x, so->y, H.target, d);
      gain_xp((int)(d * hs()->xpmul));
    }
    if (H.act_t < H.act_len) return true;   /* (the counter is not reset: he shoots again at once) */
  } else if (id == HERO_DENAS) {
    if (H.act_t == 8) {
      int d = dmg_of(so->min_dmg, so->max_dmg);
      if (e && usable(e)) H.aim = enemy_road(e, 3);
      throw_at(so, H.target, H.aim, 17, d);
      gain_xp((int)(d * hs()->xpmul));
    }
    if (H.act_t < H.act_len) return true;
  } else {   /* Bolin: a shot at frame 2 (3 from level 5, 4 at 10) of each shooting, the next enemy at 14 */
    int max_shots = H.level >= 10 ? 4 : H.level >= 5 ? 3 : 2;
    if (H.act_t < H.act_len && H.shot <= 3) {
      if (H.act_t == max_shots && H.shot > 0 && e) {
        int d = dmg_of(hs()->rlo[H.level - 1], hs()->rhi[H.level - 1]);
        Bullet *b = bullet_new(BU_SHOT);
        if (b) {
          b->x = so->x, b->y = so->y - 16, b->target = H.target, b->acc = 20;
          b->tx = H.aim.x, b->ty = H.aim.y, b->min_dmg = b->max_dmg = (int16_t)d;
        }
        gain_xp((int)(d * hs()->xpmul));
      }
      if ((H.act_t == 10 && H.shot == 0) || (H.act_t == 14 && H.shot > 0)) {
        /* the next enemy: near the first one's spot, not one already shot */
        if (H.shot < 4) H.shot_done[H.shot] = H.target;
        int got = -1;
        for (int pass = 0; pass < 2 && got < 0; pass++) {
          if (pass) memset(H.shot_done, -1, sizeof H.shot_done);
          for (int i = 0; i < MAX_ENEMIES; i++) {
            Enemy *o = &B.en[i];
            if (!usable(o)) continue;
            if (H.shot == 0 ? !in_ell(so->x, so->y, 240, o->x, o->y) || dist(so->x, so->y, o->x, o->y) <= 36
                            : !in_ell(H.shot_done[0] >= 0 && EN(H.shot_done[0]) ? B.en[H.shot_done[0]].x : H.aim.x,
                                      H.shot_done[0] >= 0 && EN(H.shot_done[0]) ? B.en[H.shot_done[0]].y : H.aim.y, 60, o->x, o->y))
              continue;
            bool done = false;
            for (int k = 0; k < 4; k++) done |= H.shot_done[k] == i;
            if (!done) got = i;
          }
        }
        if (got < 0) {
          H.act = ACT_NONE, H.shot = 0, H.cd0 = 0;
          soldier_play(so, AN_H_BOLIN_RELOAD);
          return false;
        }
        H.target = (int8_t)got;
        H.aim = (Pt){B.en[got].x + enemy_stats[B.en[got].type].x_adj, B.en[got].y + enemy_stats[B.en[got].type].y_adj};
        H.shot++, H.act_t = 0;
        soldier_play(so, H.weapon == 1 ? AN_H_BOLIN_SHOOTUP : H.weapon == 2 ? AN_H_BOLIN_SHOOTDOWN : AN_H_BOLIN_SHOOTLATERAL);
        memset(H.shot_done + 1, -1, 3);
      }
      return true;
    }
    H.cd0 = 0, H.shot = 0;
    H.act = ACT_NONE;
    soldier_play(so, AN_H_BOLIN_RELOAD);
    return false;
  }
  H.act = ACT_NONE;
  soldier_play(so, AN[id][A_HIDLE]);
  return false;
}

/* ------------------------------------------------------------------ the skills */
static int count_ground(float cx, float cy, float w) {
  int n = 0;
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy *e = &B.en[i];
    if (usable(e) && !e->flying && in_ell(cx, cy, w, e->x, e->y)) n++;
  }
  return n;
}

static void stun(Enemy *e, int frames) {   /* the earthquake's stun (EnemyCommon.§_-2G§) */
  if (e->boss || e->flying || !usable(e)) return;
  e->stun_t = (int16_t)frames, e->charging = 0;
  e->anim = (uint16_t)ANIM(e->type, A_IDLE), e->anim_t = 0;
}

static void true_area(float cx, float cy, float w, int lo, int hi, bool ground, bool stun_them) {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy *e = &B.en[i];
    if (!usable(e) || (ground && e->flying) || !in_ell(cx, cy, w, e->x, e->y)) continue;
    enemy_damage(e, dmg_of(lo, hi), ARM_I, 0);
    if (stun_them) stun(e, 2 * FPS);
  }
}

static void burn_area(float cx, float cy, float w, int lo, int hi) {   /* burnDamage: explosive, not bosses or flying */
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy *e = &B.en[i];
    if (usable(e) && !e->boss && !e->flying && in_ell(cx, cy, w, e->x, e->y)) enemy_damage(e, dmg_of(lo, hi), ARM_E, 0);
  }
}

static void bomb_hit_area(float cx, float cy, float w, int lo, int hi) {   /* bombHit: a kill blows it up */
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy *e = &B.en[i];
    if (!usable(e) || e->flying || !in_ell(cx, cy, w, e->x, e->y)) continue;
    int dmg = dmg_of(lo, hi);
    int left = e->health - (int)((float)dmg - (float)e->armor / 2.0f * dmg / 100.0f);
    if (left <= 0 && e->sp_state != 9) enemy_kill(e, 1);
    else enemy_damage(e, dmg, ARM_E, 0);
  }
}

static void courage(Soldier *so) {   /* Gerald's Courage: every soldier around (not heroes or the wildcat) */
  for (int i = 0; i < MAX_SOLDIERS; i++) {
    Soldier *o = &B.so[i];
    if (!o->alive || !o->active || o->kind >= SK_HERO || !in_ell(so->x, so->y, 180, o->x, o->y)) continue;
    if (o->courage_t) o->armor = (int16_t)(o->armor - 5 * o->courage_lvl), o->min_dmg -= 2 * o->courage_lvl, o->max_dmg -= 2 * o->courage_lvl;
    o->courage_lvl = H.sk1, o->courage_t = 6 * FPS;
    o->health = (int16_t)(o->health + o->init_health * 15 / 100);
    if (o->health > o->init_health) o->health = o->init_health;
    o->armor = (int16_t)(o->armor + 5 * H.sk1), o->min_dmg += 2 * H.sk1, o->max_dmg += 2 * H.sk1;
    fx_add(AN_HB_COURAGE_COURAGE, o->x, o->y, false, false);
  }
}

static void start(Soldier *so, int act, int len, int hit, int an) {
  H.act = (uint8_t)act, H.act_t = 0, H.act_len = (int16_t)len, H.act_hit = (int16_t)hit;
  soldier_play(so, an);
}

static void skill_hit(Soldier *so) {
  int id = H.id, s1 = H.sk1, s2 = H.sk2;
  float fx = so->x + (so->left ? -17 : 17);
  if (H.act == ACT_SKILL1) switch (id) {
    case HERO_GERALD: courage(so); break;
    case HERO_MALIK:
      fx_add(AN_B_BOMBDECAL, fx, so->y, false, true);
      fx_add(AN_HB_SMASH, fx, so->y, false, false);
      true_area(so->x, so->y, 120, 20 * s1, 20 + 20 * s1, true, false);
      break;
    case HERO_ALLERIA: {   /* Multishot: the target, then others near it, the rest at the target again */
      int n = 1 + s1, k = 0;
      Enemy *t = EN(H.target);
      if (!t || !usable(t)) break;
      int lo = hs()->rlo[H.level - 1], hi = hs()->rhi[H.level - 1];
      arrow(so, H.target, dmg_of(lo, hi), true), k++;
      for (int i = 0; i < MAX_ENEMIES && k < n; i++) {
        Enemy *e = &B.en[i];
        if (i != H.target && usable(e) && in_ell(t->x, t->y, 150, e->x, e->y)) arrow(so, i, dmg_of(lo, hi), true), k++;
      }
      while (k < n) arrow(so, H.target, dmg_of(lo, hi), true), k++;
      break;
    }
    case HERO_BOLIN: {   /* a mine on his road, near his rally */
      int lane = rnd_round(0, 2), off = rnd_round(-12, 12);
      int r = H.road_path < B.npaths ? H.road_path : 0;
      if (lane >= B.nlanes[r]) lane = B.nlanes[r] - 1;
      if (H.road_node + off >= path_len(r, 0) || H.road_node + off < 0) off = 0;
      lob(so->x, so->y - 16, path_node(r, lane, H.road_node + off), 24, 0, s1);
      break;
    }
    case HERO_MAGNUS:   /* Mirage: below, above, right */
      for (int k = 0; k < s1; k++) {
        Pt to = {so->x + (k == 2 ? 22 : 0), so->y + (k == 0 ? 22 : k == 1 ? -22 : 0)};
        illusion_level = (uint8_t)s1;
        int i = soldier_new(SK_ILLUSION, -1, (Pt){so->x, so->y}, to);
        if (i >= 0) soldier_play(&B.so[i], AN_H_ILLUSION_RUNNING);
      }
      break;
    case HERO_DENAS:   /* Royal Edict: the towers around shoot faster and further */
      for (int s = 0; s < B.nspots; s++) {
        Spot *t = &B.spot[s];
        if (t->kind == TW_NONE || t->kind == 0xFF || t->kind == TW_BARRACKS || t->kind == TW_PALADIN || t->kind == TW_BARBARIAN ||
            site_kind(t->kind) || t->building || t->disabled || !in_ell(so->x, so->y, 260, t->x, t->y))
          continue;
        t->buff_t = (int16_t)(3 * H.level * FPS);   /* (resetToLevel: the hero's level, as the game passes it) */
        fx_add(AN_HB_INSPIRE, t->x, t->y - 20, false, false);
      }
      break;
    }
  else switch (id) {
    case HERO_MALIK: {   /* Earthquake: here, then along the enemy's road */
      fx_add(AN_HB_FISSURE, fx, so->y, false, false);
      true_area(fx, so->y, 80, 20 * s2, 20 + 20 * s2, false, true);
      Enemy *e = EN(H.target);
      if (e) quake = (typeof(quake)){1, (uint8_t)s2, e->path, e->lane, e->lane, 1, e->node, 0};
      break;
    }
    case HERO_ALLERIA: {   /* Call of the Wild */
      int i = soldier_new(SK_WILDCAT, -1, H.cat_at, H.cat_at);
      if (i >= 0) H.pet = (int8_t)i, H.cat_wait = 0;
      break;
    }
    case HERO_BOLIN:
      lob(so->x, so->y - 16, H.aim, 34, 1, s2);
      break;
    case HERO_MAGNUS: case HERO_DENAS: {   /* the arcane storm, the catapults */
      int k = volley[0].on ? 1 : 0;
      volley[k] = (typeof(volley[0])){1, (uint8_t)(id == HERO_MAGNUS), (uint8_t)s2, 0,
                                      (uint8_t)(id == HERO_MAGNUS ? 6 * s2 : 1 + 2 * s2), 0, 0, 0, H.aim.x, H.aim.y,
                                      H.aim.x > 350 ? 350 : -350};
      break;
    }
    }
}

/* a ground enemy to aim at, and the road spot 6 nodes ahead of it (§_-Ca§, §_-RQ§, §_-IX§) */
static bool ahead_target(Soldier *so, float w, float min_d) {
  int got = -1;
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy *e = &B.en[i];
    if (usable(e) && !e->flying && in_ell(so->x, so->y, w, e->x, e->y) && dist(so->x, so->y, e->x, e->y) > min_d) got = i;
  }
  if (got < 0) return false;
  Enemy *e = &B.en[got];
  int len = path_len(e->path, e->lane);
  H.aim = e->blocked || e->node + 6 > len ? enemy_road(e, 0) : enemy_road(e, 6);
  H.target = (int8_t)got;
  return true;
}

static bool skills(Soldier *so) {
  int id = H.id;
  bool busy = H.act != ACT_NONE;
  if (busy && H.act != ACT_SKILL1 && H.act != ACT_SKILL2) return false;
  if (busy) {
    if (H.act_t < H.act_len) {
      H.act_t++;
      if (H.act_t == H.act_hit) skill_hit(so);
      return true;
    }
    if (H.act == ACT_SKILL1) H.cd1 = 0;
    else H.cd2 = 0;
    so->charging = 0, so->charge_t = 0, so->reload_t = 0;
    H.act = ACT_NONE;
    soldier_play(so, so->fighting ? AN[id][A_HIDLE] : AN[id][A_HIDLE]);
    return false;
  }
  bool still = !so->walking && !so->charging;
  switch (id) {
  case HERO_GERALD:
    if (H.sk1 && still && H.cd1 >= 6 * FPS) {   /* 2 soldiers fighting near him */
      int n = 0;
      for (int i = 0; i < MAX_SOLDIERS && n < 2; i++) {
        Soldier *o = &B.so[i];
        if (o->alive && o->active && o->kind < SK_HERO && o->fighting && in_ell(so->x, so->y, 180, o->x, o->y)) n++;
      }
      if (n >= 2) {
        start(so, ACT_SKILL1, 53, 17, AN_H_GERALD_COURAGE), skill_xp(1, H.sk1);
        return true;
      }
    }
    break;
  case HERO_MALIK: {
    Enemy *e = EN(so->enemy);
    if (H.sk1 && still && H.cd1 >= 6 * FPS && e && usable(e) && e->fighting && count_ground(so->x, so->y, 120) >= 3) {
      H.cd1 = 0, start(so, ACT_SKILL1, 29, 14, AN_H_MALIK_SMASH), skill_xp(1, H.sk1);
      return true;
    }
    if (H.sk2 && still && H.cd2 >= 14 * FPS && so->fighting && e && usable(e) && e->fighting) {
      H.target = so->enemy;
      H.cd2 = 0, start(so, ACT_SKILL2, 35, 17, AN_H_MALIK_FISSURE), skill_xp(2, H.sk2);
      return true;
    }
    break;
  }
  case HERO_ALLERIA:
    if (H.sk1 && !so->fighting && !so->walking && H.cd1 >= 3 * FPS) {
      int t = shot_target(so, 290, 30, true, false);
      if (t >= 0) {
        H.target = (int8_t)t, so->left = B.en[t].x < so->x;
        H.cd1 = 0, start(so, ACT_SKILL1, 29, 16, AN_H_ALLERIA_MULTISHOOT), skill_xp(1, H.sk1);
        return true;
      }
    }
    if (H.sk2 && !so->fighting && !so->walking && H.pet < 0 && H.cat_wait && H.cd2 >= H.cat_wait) {
      H.cd2 = 0, start(so, ACT_SKILL2, 40, 17, AN_H_ALLERIA_CALLOFWILD), skill_xp(2, H.sk2);
      return true;
    }
    break;
  case HERO_BOLIN:
    if (H.sk2 && !so->fighting && !so->walking && H.cd2 >= 14 * FPS && ahead_target(so, 280, 15)) {
      so->left = H.aim.x < so->x;
      H.cd2 = 0, start(so, ACT_SKILL2, 27, 13, AN_H_BOLIN_TARBOMB), skill_xp(2, H.sk2);
      return true;
    }
    if (H.sk1 && !so->fighting && !so->walking && H.cd1 >= 6 * FPS) {
      H.cd1 = 0, start(so, ACT_SKILL1, 17, 7, AN_H_BOLIN_MINE), skill_xp(1, H.sk1);
      return true;
    }
    break;
  case HERO_MAGNUS:
    if (H.sk1 && !so->fighting && !so->walking && H.cd1 >= 10 * FPS) {
      H.cd1 = 0, start(so, ACT_SKILL1, 18, 12, AN_H_MAGNUS_MIRAGE), skill_xp(1, H.sk1);
      return true;
    }
    if (H.sk2 && !so->fighting && !so->walking && H.cd2 >= 14 * FPS && ahead_target(so, 280, 35)) {
      so->left = H.aim.x < so->x;
      H.cd2 = 0, start(so, ACT_SKILL2, 40, 15, AN_H_MAGNUS_THUNDER), skill_xp(2, H.sk2);
      return true;
    }
    break;
  case HERO_DENAS:
    if (H.sk1 && !so->walking && H.cd1 >= 12 * FPS) {
      bool any = false;
      for (int s = 0; s < B.nspots && !any; s++) {
        Spot *t = &B.spot[s];
        any = t->kind != TW_NONE && t->kind != 0xFF && t->kind != TW_BARRACKS && t->kind != TW_PALADIN && t->kind != TW_BARBARIAN &&
              !site_kind(t->kind) && !t->building && !t->disabled && in_ell(so->x, so->y, 260, t->x, t->y);
      }
      if (any) {
        so->charging = 0, so->charge_t = 0;
        H.cd1 = 0, start(so, ACT_SKILL1, 55, 13, AN_H_DENAS_BUFF), skill_xp(1, H.sk1);
        return true;
      }
    }
    if (H.sk2 && !so->walking && H.cd2 >= 10 * FPS && ahead_target(so, 260, 40)) {
      so->charging = 0, so->charge_t = 0;
      so->left = H.aim.x < so->x;
      H.cd2 = 0, start(so, ACT_SKILL2, 37, 7, AN_H_DENAS_CATAPULT), skill_xp(2, H.sk2);
      return true;
    }
    break;
  }
  return false;
}

/* Ignus: after each blow, the surge to a free enemy, or a chance of the frenzy */
void hero_blow_done(Soldier *so) {
  if (so->kind != SK_HERO || H.id != HERO_IGNUS) return;
  if (H.sk1 && H.cd1 >= 4 * FPS && so->active) {
    int got = -1;
    for (int i = 0; i < MAX_ENEMIES; i++) {
      Enemy *e = &B.en[i];
      int len = path_len(e->path, e->lane);
      if (usable(e) && !e->blocked && enemy_stats[e->type].blockable && !e->flying && in_ell(so->x, so->y, 200, e->x, e->y) &&
          dist(so->x, so->y, e->x, e->y) > 30 && e->node > 20 && e->node < len - 20)
        got = i;
    }
    if (got >= 0) {
      Enemy *cur = EN(so->enemy);
      if (so->blocking && cur && cur->soldier == so - B.so) enemy_unblock(cur);
      soldier_stop_fight(so);
      soldier_engage(so, got, true);
      so->rx = so->dx, so->ry = so->dy;
      H.surge = 1, H.surge_t = 0, H.surge_end = 0;
      so->speed = 7;
      soldier_play(so, AN_H_IGNUS_SURGEOFFLAMESTART);
      skill_xp(1, H.sk1);
      return;
    }
  }
  if (H.sk2 && H.act == ACT_NONE && rnd() < 0.25f) {
    stop_special();
    start(so, ACT_SKILL2, 24, 12, AN_H_IGNUS_FLAMING);
    skill_xp(2, H.sk2);
  }
}

static bool ignus(Soldier *so) {
  if (H.id != HERO_IGNUS) return false;
  if (H.act == ACT_SKILL2 && H.act_t + 1 == 12) {   /* the frenzy: burns all around, heals a fifth */
    fx_add(AN_HB_FRENZY_EFFECT, so->x, so->y, false, false);
    burn_area(so->x, so->y, 150, 10 * H.sk2, 10 + 10 * H.sk2);
    so->health = (int16_t)(so->health + so->init_health / 5);
    if (so->health > so->init_health) so->health = so->init_health;
    H.act_t++;
    return true;
  }
  if (H.surge_end) {   /* landed: its end, then the fight */
    if (++H.surge_t < 10) return true;
    H.surge_end = 0, H.surge = 0, H.cd1 = 0;
    so->speed = hs()->speed;
    Enemy *e = EN(so->enemy);
    if (so->blocking && e && e->active) enemy_start_fighting(e);
    soldier_play(so, AN[H.id][A_HIDLE]);
    return true;
  }
  if (H.surge) {
    if (H.surge_t < 3) {   /* its start */
      H.surge_t++;
      return true;
    }
    if (so->walking) {   /* burning everything on the way */
      burn_area(so->x, so->y, 40, 10 * H.sk1, 10 + 10 * H.sk1);   /* (surgeOfFlameMinDamage + itself x level: 0) */
      float dx = so->dx - so->x, dy = so->dy - so->y;
      if (sqrtf(dx * dx + dy * dy) <= so->speed) {
        so->x = roundf(so->dx), so->y = roundf(so->dy), so->walking = 0, so->active = 1;
        H.surge_end = 1, H.surge_t = 0;
        soldier_play(so, AN_H_IGNUS_SURGEOFFLAMEEND);
        return true;
      }
      float a = atan2f(dy, dx);
      so->left = so->dx < so->x;
      so->x += cosf(a) * so->speed, so->y += sinf(a) * so->speed;
      return true;
    }
    H.surge = 0, so->speed = hs()->speed;
  }
  return false;
}

/* ------------------------------------------------------------------ a frame of the hero */
static bool pet_step(Soldier *so);

bool hero_soldier_step(Soldier *so) {
  if (so->kind != SK_HERO) return pet_step(so);
  if (so->dead) {   /* readyToRespawn */
    if (so->dead_t == 0) hero_died(so);
    if (++so->dead_t < so->dead_time) return true;
    so->dead = 0, so->respawning = 1, so->hidden = 0, so->door_t = 0, so->lifes++;
    soldier_play(so, AN[H.id][A_HSPAWN]);
    return true;
  }
  if (so->respawning) {
    if (++so->door_t >= hs()->ao) respawn(so);
    return true;
  }
  H.cd0++, H.cd1++, H.cd2++;
  if (H.id == HERO_ALLERIA && H.pet >= 0 && !SO(H.pet)) H.pet = -1;
  if (H.act == ACT_LVLUP) {   /* §_-Hs§ */
    if (H.act_t++ < H.act_len) return true;
    H.act = ACT_NONE;
    soldier_play(so, so->walking ? AN[H.id][A_HRUN] : AN[H.id][A_HIDLE]);
  }
  if (H.act == ACT_TELEOUT) {   /* Magnus' teleport */
    if (++H.act_t < H.act_len) return true;
    so->x = H.tele.x, so->y = H.tele.y, so->dx = so->rx, so->dy = so->ry;
    H.act = ACT_TELEIN, H.act_t = 0, H.act_len = 11;
    soldier_play(so, AN_H_MAGNUS_TELEPORTHIN);
    return true;
  }
  if (H.act == ACT_TELEIN) {
    if (++H.act_t < H.act_len) return true;
    H.act = ACT_NONE;
    soldier_play(so, AN[H.id][A_HIDLE]);
  }
  if (ignus(so)) return true;
  if (shoot(so)) return true;
  if (skills(so)) return true;
  if (so->walking && H.npath && path_walk(so)) return true;
  return false;
}

/* the wildcat and the illusions: they come, live, fight and go as reinforcements do; the illusions shoot */
static bool pet_step(Soldier *so) {
  if (so->dead) {
    if (++so->dead_t < so->dead_time) return true;
    if (so->lifes != 1) {
      if (so->kind == SK_WILDCAT && H.pet == so - B.so) H.pet = -1, H.cat_wait = 20 * FPS, H.cd2 = 0;
      soldier_free(so);
      return true;
    }
    so->lifes = 2, so->dead = 0, so->respawning = 1, so->door_t = 0, so->hidden = 0;
    soldier_play(so, AN_H_WILDCAT_RESPAWNING);
    return true;
  }
  if (so->respawning) {
    if (so->kind == SK_ILLUSION && so->sp_state == 0) {   /* walking out of Magnus */
      float dx = so->rx - so->x, dy = so->ry - so->y;
      if (sqrtf(dx * dx + dy * dy) > 1.7f) {
        float a = atan2f(dy, dx);
        so->x += cosf(a) * 1.7f, so->y += sinf(a) * 1.7f;
        return true;
      }
      so->sp_state = 1, so->door_t = 0;
      soldier_play(so, AN_H_ILLUSION_RESPAWNING);
    }
    if (++so->door_t < (so->kind == SK_ILLUSION ? 9 : so->door_wait)) return true;
    so->active = 1, so->respawning = 0, so->walking = 1, so->hidden = 0, so->sp_state = 0;
    so->dx = so->rx, so->dy = so->ry;
    soldier_play(so, so->kind == SK_ILLUSION ? AN_H_ILLUSION_RUNNING : AN_H_WILDCAT_RUNNING);
    return true;
  }
  if (so->kind == SK_ILLUSION) {
    if (++so->life_t >= so->life_time) {   /* readyToHide */
      so->active = 0, so->dead = 1, so->dead_t = 0;
      soldier_stop_fight(so);
      soldier_play(so, AN_H_ILLUSION_DEAD);
      return true;
    }
    so->sp_t++;
    if (so->sp_state == 2) {   /* its shot */
      if (++so->sp_c == 11 && EN(so->sp_target)) {
        const HeroStat *s = &HS[HERO_MAGNUS];
        int l = so->level - 1;
        bolt(so->x, so->y, so->sp_target, dmg_of((int)(s->rlo[l] * 0.2f), (int)(s->rhi[l] * 0.2f)));
      }
      if (so->sp_c < 33) return true;
      so->sp_state = 0;
      soldier_play(so, AN_H_ILLUSION_IDLE);
    } else if (!so->fighting && !so->walking && so->sp_t >= 30) {
      int t = shot_target(so, 220, 36, false, false);
      if (t >= 0) {
        so->sp_target = (int8_t)t, so->sp_state = 2, so->sp_c = 0, so->left = B.en[t].x < so->x;
        soldier_play(so, AN_H_ILLUSION_RANGE);
        return true;
      }
    }
  }
  return false;
}

void hero_hit(Soldier *so) {   /* the blow: as Soldier's, with experience; Denas throws */
  Enemy *e = EN(so->enemy);
  if (!e) return;
  if (so->kind == SK_HERO && H.id == HERO_DENAS) {
    bool dodged = enemy_dodge(e);
    int d = dmg_of(so->min_dmg, so->max_dmg);
    throw_at(so, dodged ? -1 : so->enemy, (Pt){e->x, e->y}, 14, dodged ? 0 : d);
    if (!dodged) gain_xp((int)(d * hs()->xpmul));
    return;
  }
  if (enemy_dodge(e)) return;
  int d = dmg_of(so->min_dmg, so->max_dmg);
  if (so->kind == SK_HERO) {
    int real = (int)((float)d - (float)e->armor * d / 100.0f);   /* getArmorDamage */
    if (real < 0) real = 0;
    enemy_damage(e, d, ARM_P, 0);
    if (real) gain_xp((int)(real * hs()->xpmul));
    if (!e->active && e->dead) gain_xp(e->init_health * 20 / 100 > 1 ? e->init_health * 20 / 100 : 1);
  } else
    enemy_damage(e, d, ARM_P, 0);
}

void hero_attack_anim(Soldier *so) {
  if (so->kind != SK_HERO) return;
  if (H.id == HERO_GERALD && rnd() <= 0.5f) soldier_play(so, AN_H_GERALD_FIGHTING2);
  else if (H.id == HERO_MALIK && rnd() <= 0.5f) soldier_play(so, AN_H_MALIK_FIGHTING2);
  else if (H.id == HERO_DENAS) {
    H.weapon = (uint8_t)rnd_round(1, 4);
    static const int16_t an[4] = {AN_H_DENAS_FIGHTING1, AN_H_DENAS_FIGHTING2, AN_H_DENAS_FIGHTING3, AN_H_DENAS_FIGHTING4};
    soldier_play(so, an[H.weapon - 1]);
  }
}

bool hero_block(Soldier *so) {   /* Gerald's Shield of Retribution: dodgeHit */
  if (so->kind != SK_HERO || H.id != HERO_GERALD || !H.sk2 || H.act != ACT_NONE) return false;
  if (rnd_round(1, 100) > 20 * H.sk2) return false;
  if (so->charging) so->charging = 0, so->charge_t = 0;
  start(so, ACT_SKILL2, 24, 5, AN_H_GERALD_BLOCK);
  skill_xp(2, H.sk2);
  /* (the counter blow returns dodgeDamage x (50 + 50 x level) %; the game never sets dodgeDamage: it does 0) */
  return true;
}

/* ------------------------------------------------------------------ bullets and fields */
void hero_bullet(Bullet *b) {
  b->anim_t++;
  if (!b->active) {
    if (b->fade_t++ >= b->fade) b->alive = 0;
    return;
  }
  if (b->kind == HB_BOLT) {   /* homing, speeding up */
    Enemy *e = EN(b->target);
    if (e && !usable(e)) b->target = -1, e = 0;
    if (e) b->tx = e->x + enemy_stats[e->type].x_adj, b->ty = e->y + enemy_stats[e->type].y_adj;
    float a = atan2f(b->ty - b->y, b->tx - b->x);
    if (b->acc < b->acc_max) b->acc += ceilf(b->acc * 0.05f), b->acc = b->acc > b->acc_max ? b->acc_max : b->acc;
    if (b->first && b->ty <= b->y) b->y -= 11;
    b->first = 0;
    b->x += cosf(a) * b->acc, b->y += sinf(a) * b->acc;
    if (dist(b->x, b->y, b->tx, b->ty) < b->acc) {
      if (e) enemy_damage(e, b->min_dmg, ARM_M, 0);
      b->x = b->tx, b->y = b->ty, b->active = 0, b->fade = 9, b->anim_t = 0;
    }
    return;
  }
  /* the arcs: Denas' throws, the mines and tar bombs */
  float t = b->t0;
  b->x = b->x0 + t * b->xs, b->y = b->y0 + t * b->ys + b->g * t * t / 2;
  b->rot += 0.5f;
  if (b->kind == HB_THROWN) {
    Enemy *e = EN(b->target);
    if (e && usable(e) && b->t0 + 1 >= b->t1 && fabsf(b->x - e->x) < 12 && fabsf(b->y - (e->y - 10)) < 16) {
      enemy_damage(e, b->min_dmg, ARM_P, 0);
      b->alive = 0;
      return;
    }
    if (b->t0 >= b->t1) {
      fx_add(AN_B_BOMBDECAL, b->tx, b->ty, false, true);
      b->alive = 0;
      return;
    }
  } else if (b->t0 >= b->t1) {
    if (b->bombs == 0) {
      for (int i = 0; i < MAX_MINES; i++)
        if (!mines[i].on) {
          mines[i] = (typeof(mines[0])){1, b->level, 0, 0, b->tx, b->ty};
          break;
        }
    } else
      for (int i = 0; i < 3; i++)
        if (!tar[i].on) {
          tar[i] = (typeof(tar[0])){1, b->level, 0, 0, 0, (int16_t)((2 + 2 * b->level) * FPS), b->tx, b->ty};
          break;
        }
    b->alive = 0;
    return;
  }
  b->t0++;
}

void hero_draw_bullet(const Bullet *b, int x, int y) {
  if (b->kind == HB_BOLT) spr_anim(b->active ? AN_HB_BOLT_TRAVEL : AN_HB_BOLT_HIT, b->anim_t, b->active, x, y, false);
  else if (b->kind == HB_THROWN) {
    static const int16_t an[4] = {AN_HB_THROWN1, AN_HB_THROWN2, AN_HB_THROWN3, AN_HB_THROWN4};
    spr_anim(an[b->level >= 1 && b->level <= 4 ? b->level - 1 : 0], 0, false, x, y, b->xs < 0);
  } else
    spr_anim(b->bombs ? AN_HB_TARBOMB : AN_HB_MINEBOMB, 0, false, x, y, b->xs < 0);
}

void hero_draw_decals(void) {
  for (int i = 0; i < 3; i++)
    if (tar[i].on) spr_anim(tar[i].end ? AN_HB_TAR_END : AN_HB_TAR_IDLE, tar[i].end ? tar[i].t : 0, false, SX(tar[i].x), SY(tar[i].y), false);
  for (int i = 0; i < MAX_MINES; i++)
    if (mines[i].on) spr_anim(mines[i].hit ? AN_HB_MINE_HIT : AN_HB_MINE_MINE, mines[i].hit ? mines[i].t : (int)B.frame, !mines[i].hit, SX(mines[i].x), SY(mines[i].y), false);
}

static void fields_step(void) {
  for (int i = 0; i < MAX_MINES; i++) {   /* the mines: 50 s, a blast when a ground enemy steps near */
    typeof(mines[0]) *m = &mines[i];
    if (!m->on) continue;
    m->t++;
    if (m->hit) {
      if (m->t >= 19) m->on = 0;
      continue;
    }
    if (m->t >= 50 * FPS) {
      m->on = 0;
      continue;
    }
    if (!count_ground(m->x, m->y, 15)) continue;
    bomb_hit_area(m->x, m->y, 15, 30 * m->lvl, 30 + 30 * m->lvl);
    fx_add(AN_B_BOMBDECAL, m->x, m->y, false, true);
    m->hit = 1, m->t = 0;
  }
  for (int i = 0; i < 3; i++) {   /* the tar: every 11 frames, half speed for a second */
    typeof(tar[0]) *p = &tar[i];
    if (!p->on) continue;
    if (p->end) {
      if (++p->t >= 9) p->on = 0;
      continue;
    }
    if (++p->t >= p->dur) {
      p->end = 1, p->t = 0;
      continue;
    }
    if (p->tick < 10) {
      p->tick++;
      continue;
    }
    p->tick = 0;
    for (int k = 0; k < MAX_ENEMIES; k++) {
      Enemy *e = &B.en[k];
      if (!usable(e) || !in_ell(p->x, p->y, 60, e->x, e->y)) continue;
      if (!e->slow_t && !e->net_t && !e->rage_t) {
        e->slow_speed = e->speed, e->speed /= 2, e->margin = e->speed + 0.1f;
        e->slow_t = FPS;
      } else if (e->slow_t && e->slow_t < FPS)
        e->slow_t = FPS;
    }
  }
  for (int i = 0; i < 2; i++) {   /* the rains: one every 7 frames around the spot, 70 degrees on each time */
    typeof(volley[0]) *v = &volley[i];
    if (!v->on) continue;
    if (v->out) {
      if (++v->t >= 10) v->on = 0;
      continue;
    }
    if (v->t < 6) {
      v->t++;
      continue;
    }
    v->t = 0;
    v->n++;
    int w = (int)(rnd() * 30 + 15);
    float h = (float)(int)(w * RANGE_RATIO), rad = v->deg * (float)M_PI / 180;
    Pt to = {v->x + cosf(rad) * w / 2, v->y + sinf(rad) * h / 2};
    v->deg = (int16_t)((v->deg + 70) % 360);
    if (v->kind == 1) {
      for (int k = 0; k < 8; k++)
        if (!drops[k].on) {
          drops[k] = (typeof(drops[0])){1, v->lvl, 0, to.x, to.y};
          fx_add(AN_HB_DROP, to.x, to.y, false, false);
          break;
        }
    } else {   /* a rock from beyond the battlefield's edge */
      Bullet *b = bullet_new(BU_BOMB);
      if (b) {
        b->x0 = b->x = v->x + v->ox, b->y0 = b->y = v->y - 180;
        b->tx = to.x, b->ty = to.y, b->t1 = 40, b->g = 0.8f;
        b->xs = (to.x - b->x0) / 40, b->ys = (to.y - b->y0 - 0.8f * 40 * 40 / 2) / 40;
        b->area = 60, b->bombs = 11, b->level = 1;
        b->min_dmg = (int16_t)(10 * v->lvl), b->max_dmg = (int16_t)(20 + 10 * v->lvl);
      }
    }
    if (v->n == v->max) v->out = 1, v->t = 0;
  }
  for (int k = 0; k < 8; k++)   /* the drops: true damage 17 frames after they show */
    if (drops[k].on && ++drops[k].t >= 17) {
      drops[k].on = 0;
      true_area(drops[k].x, drops[k].y, 60, 20, 20, false, false);
    }
  if (quake.on && ++quake.t >= 3) {   /* the quake runs both ways along the road, 4 nodes a step */
    quake.t = 0;
    int r = quake.path, step = quake.k * 4, len = path_len(r, 0);
    if (quake.k == 1) quake.lane = quake.sub;
    else if (quake.lane != quake.sub) quake.lane = quake.sub;
    else quake.lane = quake.lane == 0 ? 1 : 0;
    int lane = quake.lane < B.nlanes[r] ? quake.lane : 0;
    int lo = 20 * quake.lvl, hi = 20 + 20 * quake.lvl;
    if (quake.node + step < len) {
      Pt p = path_node(r, lane, quake.node + step);
      fx_add(AN_HB_FISSURE, p.x, p.y, false, false), true_area(p.x, p.y, 80, lo, hi, false, true);
    }
    if (quake.node - step > 0) {
      Pt p = path_node(r, lane, quake.node - step);
      fx_add(AN_HB_FISSURE, p.x, p.y, false, false), true_area(p.x, p.y, 80, lo, hi, false, true);
    }
    if (quake.lvl < ++quake.k) quake.on = 0;
  }
}

void hero_step(void) {
  if (H.id && !H.spawned && B.current_wave >= 1) spawn();
  for (int i = 0; i < MAX_SOLDIERS; i++) {   /* Courage wears off */
    Soldier *o = &B.so[i];
    if (o->alive && o->courage_t && --o->courage_t == 0)
      o->armor = (int16_t)(o->armor - 5 * o->courage_lvl), o->min_dmg -= 2 * o->courage_lvl, o->max_dmg -= 2 * o->courage_lvl;
  }
  for (int s = 0; s < B.nspots; s++)
    if (B.spot[s].buff_t) B.spot[s].buff_t--;
  fields_step();
}

/* ------------------------------------------------------------------ the portrait (§_-Du§) */
void hero_draw_hud(bool selected) {
  Soldier *so = hero_soldier();
  if (!so || !gfx_rows(2, 26)) return;
  int ox = MAP_X + 2, oy = 2, w = 20;   /* (the portrait's top left: its bars at 2, 50 and 2, 55 of 50 x 60) */
  spr_frame(anim_slot(AN_H_FACES, H.id - 1, false), ox, oy, false);
  int hp = so->dead ? 0 : so->health * w / (so->init_health ? so->init_health : 1);
  gfx_fill(ox + 1, oy + 20, hp, 2, RGB(0x8CC63F));
  int xp = w;
  if (H.level < 10) xp = (H.xp - xp_table[H.level - 1]) * w / (xp_table[H.level] - xp_table[H.level - 1]);
  if (xp > w) xp = w;
  if (xp < 0) xp = 0;
  gfx_fill(ox + 1, oy + 22, xp, 1, RGB(0xF6D04D));
  char s[4] = {(char)('0' + H.level % 10), 0, 0, 0};
  if (H.level == 10) s[0] = '1', s[1] = '0';
  gfx_text_o(FONT_SMALL, ox + 14, oy + 9, s, WHITE);
  if (so->dead) {   /* the respawn: the portrait greys as it fills back */
    int left = so->dead_time - so->dead_t, h = left * 20 / so->dead_time;
    gfx_blend(ox + 1, oy + 1, 20, h, BLACK, 20);
    char t[6];
    int sec = (left + FPS - 1) / FPS;
    t[0] = (char)('0' + sec / 10), t[1] = (char)('0' + sec % 10), t[2] = 0;
    gfx_text_oc(FONT_SMALL, ox + 10, oy + 5, sec >= 10 ? t : t + 1, RGB(0xFFD32A));
  }
  if (selected) gfx_frame_rect(ox - 1, oy - 1, 23, 26, RGB(0xFFE14D));
  gfx_text_o(FONT_SMALL, ox + 23, oy, "4", RGB(0xFFE14D));
}

/* the side bar's box while the hero is picked (the game's info panel: its name, life, level and respawn) */
void hero_draw_info(void) {
  Soldier *so = hero_soldier();
  if (!so || gfx_y0 + BAND_H < 166) return;
  gfx_fill(1, 166, 38, 74, RGB(0x1E140B));
  gfx_fill(1, 166, 38, 1, RGB(0x8A6A3A));
  int y = 168 + gfx_text_box(FONT_SMALL, 3, 168, 35, hero_name(H.id), RGB(0xF4E6C8));
  char s[8], t[8];
  int n = so->health > 0 ? so->health : 0, k = 0;
  do t[k++] = (char)('0' + n % 10), n /= 10;
  while (n && k < 7);
  for (int i = 0; i < k; i++) s[i] = t[k - 1 - i];
  s[k] = 0;
  spr_anim(AN_UI_HEART, 0, false, 3, y + 1, false);
  gfx_text(FONT_SMALL, 18, y, s, RGB(0xF4E6C8));
  char l[8] = "Lvl 1";
  if (H.level == 10) l[4] = '1', l[5] = '0', l[6] = 0;
  else l[4] = (char)('0' + H.level);
  gfx_text(FONT_SMALL, 3, y + 14, l, RGB(0x9A8A70));
  char r[8] = "R 15s";
  int sec = hs()->respawn / FPS;
  r[2] = (char)('0' + sec / 10), r[3] = (char)('0' + sec % 10);
  gfx_text(FONT_SMALL, 3, y + 25, r, RGB(0x9A8A70));
}
