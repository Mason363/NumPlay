/* The lawn: a level's start, its waves, sun, planting and digging (game.h). */
#include <string.h>
#include "game.h"
#include "save.h"

Board B;

/* ---------------------------------------------------------------- places */

float cell_h(void) { return B.env == ENV_POOL || B.env == ENV_FOG || B.env == ENV_ROOF ? 85 : 100; }
static float slope(float x) { return B.env == ENV_ROOF ? (x < 440 ? (440 - x) * 0.25f : 0) : 0; }
float row_y(int row, float x) {
  return BOARD_TOP + row * cell_h() + (B.env == ENV_ROOF ? 58 + slope(x) : is_pool() ? 68 : 78);
}
float cell_x(int col) { return BOARD_LEFT + col * CELL_W + 40; }
int row_at(float y, float x) {
  float r = (y - BOARD_TOP - slope(x) + (B.env == ENV_ROOF ? 10 : 0)) / cell_h();
  return r < 0 ? -1 : (int)r;
}
int col_at(float x) { return x < BOARD_LEFT ? -1 : (int)((x - BOARD_LEFT) / CELL_W); }
bool is_pool(void) { return B.env == ENV_POOL || B.env == ENV_FOG; }
bool is_night(void) { return B.env == ENV_NIGHT || B.env == ENV_FOG; }
bool water_row(int row) { return is_pool() && (row == 2 || row == 3); }
bool has_conveyor(void) { return B.mode == MODE_BOWLING || B.mode == MODE_CONVEYOR; }

/* ---------------------------------------------------------------- chance (the game's own: a linear congruence) */

uint32_t rnd(void) { return B.rng = B.rng * 1664525u + 1013904223u; }
float frand(void) { return (float)(rnd() >> 8) / 16777216.0f; }
int irand(int n) { return n > 0 ? (int)(frand() * n) : 0; }
uint16_t new_id(void) {
  if (!++B.next_id) B.next_id = 1;
  return B.next_id;
}

/* ---------------------------------------------------------------- lookups */

Plant *plant_at(int col, int row, int kind) {
  for (int i = 0; i < B.nplants; i++) {
    Plant *p = &B.plants[i];
    if (p->col != col || p->row != row || p->health <= 0) continue;
    if (kind >= 0 ? p->kind == kind : p->kind != P_LILYPAD && p->kind != P_FLOWERPOT && p->kind != P_PUMPKIN) return p;
  }
  return NULL;
}
Zombie *zombie_by_id(uint16_t id) {
  for (int i = 0; i < B.nzombies; i++)
    if (B.zombies[i].id == id) return &B.zombies[i];
  return NULL;
}
bool sleeping(const Plant *p) {
  return !B.has_boss && !(p->flags & PF_AWAKE) && !is_night() && plant_defs[p->kind].mushroom;
}
int seed_kind(int slot) {
  if (has_conveyor()) return slot < B.npackets ? B.packets[slot].kind : -1;
  return slot < B.nseeds ? B.seeds[slot] : -1;
}
int plant_cost(int kind) { return plant_defs[kind].cost; }

/* ---------------------------------------------------------------- effects */

void fx_add(int kind, float x, float y, int life, int v) {
  if (B.nfx >= MAX_FX) {
    /* the oldest splat or puff gives its place */
    int k = -1;
    for (int i = 0; i < B.nfx; i++)
      if (B.fx[i].kind == FX_SPLAT || B.fx[i].kind == FX_DIRT || B.fx[i].kind == FX_SMOKE) k = i;
    if (k < 0) return;
    B.fx[k] = B.fx[--B.nfx];
  }
  Fx *f = &B.fx[B.nfx++];
  memset(f, 0, sizeof *f);
  f->kind = (uint8_t)kind, f->x = x, f->y = y, f->life = (int16_t)life, f->v = (int16_t)v;
}

/* ---------------------------------------------------------------- sun and coins */

void add_sun(float x, float y, float ty, int value, bool sky) {
  if (B.nsuns >= MAX_SUNS) return;
  Sun *s = &B.suns[B.nsuns++];
  s->x = x, s->y = y, s->ty = ty, s->left = 1200, s->value = (uint8_t)value, s->flags = sky;
}
void add_coin(float x, float y, int value) {
  if (B.ncoins >= MAX_COINS) return;
  Coin *c = &B.coins[B.ncoins++];
  c->x = x, c->y = y - 45, c->ty = y, c->left = 1500, c->value = (uint16_t)value;
}

int board_collect(float x, float y, float r) {
  int n = 0;
  for (int i = 0; i < B.nsuns; i++) {
    Sun *s = &B.suns[i];
    float dx = s->x - x, dy = s->y - y;
    if (dx * dx + dy * dy > r * r) continue;
    B.sun += s->value;
    fx_add(FX_SUNFLY, s->x, s->y, 50, s->value);
    B.suns[i--] = B.suns[--B.nsuns];
    n++;
  }
  for (int i = 0; i < B.ncoins; i++) {
    Coin *c = &B.coins[i];
    float dx = c->x - x, dy = c->y - y;
    if (dx * dx + dy * dy > r * r) continue;
    B.money += c->value;
    fx_add(FX_COINFLY, c->x, c->y, 60, c->value);
    B.coins[i--] = B.coins[--B.ncoins];
    n++;
  }
  return n;
}

/* ---------------------------------------------------------------- waves (Board::PickZombieWaves) */

int waves_flag_every(void) { return B.nwaves < 10 ? B.nwaves : 10; }
bool is_flag_wave(int wave) {
  int per = waves_flag_every();
  return B.level != 1 && per && wave % per == per - 1;
}

static void put(uint8_t *wave, int *n, int *points, int kind) {
  if (*n < 23) wave[(*n)++] = (uint8_t)kind;
  *points -= zombie_defs[kind].value;
}

static void generate_waves(void) {
  int L = B.level, count = B.nwaves;
  uint8_t allowed[N_ZOMBIES];
  int nallowed = 0, introduced = -1;
  for (int k = 0; k < N_ZOMBIES; k++) {
    const ZombieDef *d = &zombie_defs[k];
    bool ok = L == 25 ? (k == Z_NORMAL || k == Z_CONE || k == Z_FOOTBALL || k == Z_SNORKEL)
                      : d->starting <= L && d->weight && (d->allowed >> (L - 1) & 1);
    if (ok) allowed[nallowed++] = (uint8_t)k;
    if (introduced < 0 && L > 1 && d->starting == L) introduced = k;
  }
  for (int w = 0; w < count && w < 50; w++) {
    uint8_t *wave = B.waves[w];
    int n = 0;
    memset(wave, 0xFF, 24);
    /* Bungee Blitz: every flag wave brings five bungee zombies */
    if (L == 45 && is_flag_wave(w)) {
      for (int i = 0; i < 5; i++) wave[n++] = Z_BUNGEE;
      if (w != count - 1) continue;
    }
    int points = w / 3 + 1;
    if (is_flag_wave(w)) {
      int normals = points < 8 ? points : 8;
      points = (int)(points * 2.5f);
      for (int i = 0; i < normals; i++) put(wave, &n, &points, Z_NORMAL);
      put(wave, &n, &points, Z_FLAG);
    }
    if (L == 5 || L == 25) points *= 4;
    if (L == 10 || L == 20 || L == 30 || L == 40) points *= 3;
    if (L == 45) points *= 2;
    if (introduced >= 0 &&
        (w == (introduced == Z_DIGGER ? 6 : count / 2) || w == count - 1))
      put(wave, &n, &points, introduced);
    if (w == count - 1)
      for (int i = 0; i < nallowed; i++) {
        bool there = false;
        for (int j = 0; j < n; j++) there |= wave[j] == allowed[i];
        if (!there) put(wave, &n, &points, allowed[i]);
      }
    while (points > 0 && n < 23) {
      int total = 0;
      for (int i = 0; i < nallowed; i++) {
        const ZombieDef *d = &zombie_defs[allowed[i]];
        if (d->value <= points && d->first_wave <= w + 1) total += d->weight;
      }
      if (!total) break;
      int pick = irand(total);
      for (int i = 0; i < nallowed; i++) {
        const ZombieDef *d = &zombie_defs[allowed[i]];
        if (d->value > points || d->first_wave > w + 1) continue;
        pick -= d->weight;
        if (pick < 0) {
          put(wave, &n, &points, allowed[i]);
          break;
        }
      }
    }
  }
}

/* Board::PickRowForNewZombie: rows weighed by how lately they had a zombie, and spared after a mower */
static int pick_row(uint8_t allowed) {
  float weight[MAX_ROWS], adj[MAX_ROWS], total = 0, sum = 0;
  for (int r = 0; r < B.nrows; r++) {
    weight[r] = 0;
    if (!(B.rows >> r & 1) || !(allowed >> r & 1)) continue;
    int since = B.wave - B.row_mowed[r];
    weight[r] = since <= 1 ? 0.01f : since <= 2 ? 0.5f : 1;
    total += weight[r];
  }
  if (total <= 0) {
    for (int r = 0; r < B.nrows; r++)
      if (allowed >> r & 1) return r;
    return 0;
  }
  for (int r = 0; r < B.nrows; r++) {
    float w = weight[r] / total;
    float first = 2 * w * (B.row_last[r] + 1) - 1, second = w * (B.row_second[r] + 1) - 1;
    float v = 0.75f * first + 0.25f * second;
    v = v < 0.01f ? 0.01f : v > 100 ? 100 : v;
    adj[r] = weight[r] > 0 ? w * v : 0;
    sum += adj[r];
  }
  float choice = frand() * sum;
  int pick = 0;
  while (pick < B.nrows - 1 && (adj[pick] == 0 || choice >= adj[pick])) choice -= adj[pick++];
  for (int r = 0; r < B.nrows; r++)
    if (B.row_last[r] < 250) B.row_last[r]++, B.row_second[r]++;
  B.row_second[pick] = B.row_last[pick], B.row_last[pick] = 0;
  return pick;
}

static bool ice_row(int row) { return B.ice[row].left > 0 && B.ice[row].x < 700; }

static void spawn_wave(void) {
  const uint8_t *wave = B.waves[B.wave];
  bool flag = is_flag_wave(B.wave);
  for (int i = 0; i < 24 && wave[i] != 0xFF; i++) {
    int kind = wave[i];
    int copies = 1;
    if (kind == Z_BOBSLED) {
      bool any = false;
      for (int r = 0; r < B.nrows; r++) any |= ice_row(r);
      if (!any) kind = Z_NORMAL, copies = 4;   /* (no ice to ride: four zombies instead, as the game does) */
    }
    for (int c = 0; c < copies; c++) {
      uint8_t allowed = 0;
      for (int r = 0; r < B.nrows; r++) {
        bool ok;
        if (kind == Z_BOBSLED) ok = ice_row(r);
        else if (kind == Z_DUCKY || kind == Z_SNORKEL || kind == Z_DOLPHIN) ok = water_row(r);
        else if (water_row(r))
          ok = kind == Z_BALLOON || (B.wave >= 5 && (kind == Z_NORMAL || kind == Z_CONE || kind == Z_BUCKET || kind == Z_FLAG));
        else ok = kind != Z_DANCER || is_pool() || (r > 0 && r < 4);
        if (ok) allowed |= (uint8_t)(1 << r);
      }
      int row = pick_row(allowed);
      float x = 810 + irand(40) + (flag ? 40 : 0);
      if (B.level == 45 && kind != Z_BUNGEE) {
        drop_from_sky(kind, row, B.wave);
        continue;
      }
      Zombie *z = spawn_zombie(kind, row, kind == Z_FLAG ? 830 : x, B.wave);
      if (z && kind == Z_BOBSLED) {
        uint16_t lead = z->id;
        float lx = z->x;
        z->armor = 300, z->u.sled.sled = lead, z->u.sled.leader = 0;
        for (int k = 1; k < 4; k++) {
          Zombie *m = spawn_zombie(Z_BOBSLED, row, lx + k * 45, B.wave);
          if (m) m->u.sled.sled = lead, m->u.sled.leader = lead;
        }
      }
    }
  }
  B.wave++;
  if (B.wave == B.nwaves && B.env != ENV_DAY) B.grave_wave_in = 200;
  B.wave_elapsed = 0;
  bool before_flag = B.mode != MODE_BOWLING && is_flag_wave(B.wave);
  int32_t sum = 0;
  for (int i = 0; i < B.nzombies; i++) {
    const Zombie *z = &B.zombies[i];
    if (!(z->flags & ZF_HYPNO) && z->wave == B.wave - 1 && !zombie_headless(z)) sum += zombie_total_health(z);
  }
  B.wave_threshold = before_flag ? 0 : (int32_t)(sum * (0.5f + frand() * 0.15f));
  B.wave_in = (int16_t)(before_flag ? 4500 : 2500 + irand(600));
  if (B.wave == B.nwaves && B.mode != MODE_WHACK) fx_add(FX_TEXT, 400, 300, 350, 2);   /* "Final Wave" */
}

/* ---------------------------------------------------------------- the start of a level */

static void add_grave(int col, int row) {
  if (B.ngraves < 16) B.graves[B.ngraves].col = (uint8_t)col, B.graves[B.ngraves++].row = (uint8_t)row;
}

static void populate_vases(void);

void board_start(int level) {
  uint32_t seed = B.rng;
  memset(&B, 0, sizeof B);
  B.rng = seed ? seed : 12345;
  const LevelDef *d = &level_defs[level - 1];
  B.level = (uint8_t)level, B.env = d->env, B.mode = d->mode, B.rows = d->rows;
  B.nrows = is_pool() ? 6 : 5;
  B.sun = d->sun;
  B.nwaves = d->waves;
  B.has_boss = level == 50;
  B.sun_in = 400, B.wave_in = 1800;
  for (int r = 0; r < MAX_ROWS; r++) B.row_mowed[r] = -100;
  /* the seed packets: the level's own, else the ones chosen (screens.c fills them in) */
  if (B.mode == MODE_LAWN && !d->nseeds) B.nseeds = 0;
  else if (B.mode != MODE_CONVEYOR && B.mode != MODE_BOWLING) {
    B.nseeds = d->nseeds;
    for (int i = 0; i < d->nseeds; i++) B.seeds[i] = d->seeds[i];
  }
  generate_waves();
  /* (the zombies wait for the first plant, the first time through the Adventure) */
  if (progress.finished) B.started = 1;
  if (B.mode == MODE_BOWLING) {
    /* (Challenge::StartLevel: a wall-nut on the belt, the next in 400 ticks, the zombies in 200) */
    B.npackets = 1, B.packets[0].kind = P_WALLNUT, B.packets[0].x = 0;
    B.started = 1, B.wave_in = 200, B.conveyor_in = 400;
  }
  if (B.mode == MODE_CONVEYOR) B.started = 1, B.conveyor_in = 1;
  if (B.mode == MODE_WHACK) {
    B.started = 1, B.wave_in = 200;
    extern void whack_graves(int n);
    whack_graves(9);
  }
  if (B.env == ENV_NIGHT && B.mode != MODE_WHACK && B.mode != MODE_VASE)
    for (int c = 0; c < 6 && grave_columns[level - 1][c][1]; c++) {
      uint8_t rows[MAX_ROWS];
      int n = 0;
      for (int r = 0; r < B.nrows; r++)
        if (B.rows >> r & 1) rows[n++] = (uint8_t)r;
      for (int k = 0; k < grave_columns[level - 1][c][1] && n; k++) {
        int i = irand(n);
        add_grave(grave_columns[level - 1][c][0], rows[i]);
        rows[i] = rows[--n];
      }
    }
  if (B.mode == MODE_VASE) B.started = 1, populate_vases();
  if (B.env == ENV_ROOF)
    for (int c = 0; c < (level == 41 ? 5 : level == 42 ? 4 : 3); c++)
      for (int r = 0; r < B.nrows; r++)
        if (B.nplants < MAX_PLANTS) plant_new(&B.plants[B.nplants++], P_FLOWERPOT, c, r);
  /* the mowers: the pool's lanes and the roof only with what Crazy Dave sells */
  for (int r = 0; r < B.nrows; r++) {
    B.mowers[r].x = 20;
    bool has = B.mode != MODE_VASE && (B.rows >> r & 1);
    if (water_row(r)) has = has && (progress.items & ITEM_POOL_CLEANER);
    if (B.env == ENV_ROOF) has = has && (progress.items & ITEM_ROOF_CLEANER);
    B.mowers[r].state = has ? 0 : 2;
  }
  if (B.env == ENV_FOG && level == 40) B.storm_in = 400;
  else B.storm_in = 650;
  B.storm_flash = 1;
  /* the seeds recharging from the start: the slow ones (the game's SeedPacket) */
  for (int i = 0; i < B.nseeds; i++) {
    int cd = plant_defs[B.seeds[i]].cooldown;
    B.cooldown[i] = (int16_t)(cd >= 3000 ? cd : 0);
  }
  if (B.has_boss) boss_start();
  B.phase = PH_INTRO;
  B.tutorial = 0;
}

/* the chosen seeds start recharging like the level's own */
void board_set_seeds(const uint8_t *seeds, int n) {
  B.nseeds = (uint8_t)n;
  for (int i = 0; i < n; i++) {
    B.seeds[i] = seeds[i];
    int cd = plant_defs[seeds[i]].cooldown;
    B.cooldown[i] = (int16_t)(cd >= 3000 ? cd : 0);
  }
}

/* ---------------------------------------------------------------- planting */

const char *const issue_text[] = {
  "", "You can't plant there", "There's already a plant there", "Not enough sun!", "That seed is recharging",
  "That plant goes in the water", "Plant a lily pad there first", "Plant a flower pot there first",
  "You can't plant that on the roof", "Remove the grave first", "You can't plant on a crater",
  "Plant coffee beans on sleeping mushrooms", "Roll your wall-nuts from left of the red line",
  "You can't plant on the ice",
};

int plant_issue(int kind, int col, int row) {
  if (row < 0 || row >= B.nrows || !(B.rows >> row & 1) || col < 0 || col >= (B.has_boss ? 8 : COLS)) return ISSUE_PLACE;
  for (int i = 0; i < B.nvases; i++)
    if (B.vases[i].col == col && B.vases[i].row == row) return ISSUE_PLACE;
  if (B.ice[row].left > 0 && cell_x(col) + 30 >= B.ice[row].x) return ISSUE_ICE;
  for (int i = 0; i < B.ncraters; i++)
    if (B.craters[i].col == col && B.craters[i].row == row) return ISSUE_CRATER;
  bool lily = false, pot = false, pumpkin = false, coffee = false, sleeper = false, other = false, any = false;
  for (int i = 0; i < B.nplants; i++) {
    const Plant *p = &B.plants[i];
    if (p->col != col || p->row != row || p->health <= 0) continue;
    any = true;
    if (p->kind == P_LILYPAD) lily = true;
    else if (p->kind == P_FLOWERPOT) pot = true;
    else if (p->kind == P_PUMPKIN) pumpkin = true;
    else if (p->kind == P_COFFEEBEAN) coffee = true;
    else {
      other = true;
      if (sleeping(p)) sleeper = true;
    }
  }
  bool aquatic = kind == P_LILYPAD || kind == P_TANGLEKELP || kind == P_SEASHROOM;
  if (aquatic && (!water_row(row) || any)) return ISSUE_WATER;
  if (water_row(row) && !aquatic && (kind == P_POTATOMINE || kind == P_SPIKEWEED || !lily))
    return kind == P_POTATOMINE || kind == P_SPIKEWEED ? ISSUE_WATER : ISSUE_LILY;
  if (B.env == ENV_ROOF) {
    if (kind == P_SPIKEWEED || kind == P_GRAVEBUSTER || aquatic) return ISSUE_ROOF;
    if (kind != P_FLOWERPOT && !pot) return ISSUE_POT;
  }
  bool grave = false;
  for (int i = 0; i < B.ngraves; i++) grave |= B.graves[i].col == col && B.graves[i].row == row;
  if (kind == P_GRAVEBUSTER ? !grave : grave) return kind == P_GRAVEBUSTER ? ISSUE_PLACE : ISSUE_GRAVE;
  if (B.mode == MODE_BOWLING) return col < 3 ? ISSUE_NONE : ISSUE_BOWLING;
  bool occupied;
  if (kind == P_COFFEEBEAN) {
    if (!sleeper || coffee) return ISSUE_COFFEE;
    occupied = false;
  } else if (kind == P_PUMPKIN) occupied = pumpkin;
  else if (kind == P_FLOWERPOT) occupied = any;
  else occupied = other || (kind == P_LILYPAD && any);
  /* the wall-nut first aid: a worn wall-nut, tall-nut or pumpkin made whole again */
  if (occupied && (progress.items & ITEM_FIRST_AID) && (kind == P_WALLNUT || kind == P_TALLNUT || kind == P_PUMPKIN)) {
    Plant *p = plant_at(col, row, kind);
    if (p && p->health < plant_defs[kind].health * 2 / 3) occupied = false;
  }
  if (occupied) return ISSUE_OCCUPIED;
  return ISSUE_NONE;
}

void plant_new(Plant *p, int kind, int col, int row) {
  memset(p, 0, sizeof *p);
  p->id = new_id(), p->kind = (uint8_t)kind, p->col = (uint8_t)col, p->row = (uint8_t)row;
  p->health = plant_defs[kind].health;
  p->born = B.ticks, p->shot = (uint32_t)-1000;
  switch (kind) {
  case P_SUNFLOWER: case P_SUNSHROOM: case P_MARIGOLD: p->action = (int16_t)(300 + irand(951)); break;
  case P_JALAPENO: case P_CHERRYBOMB: case P_ICESHROOM: case P_DOOMSHROOM: case P_COFFEEBEAN: p->action = 100; break;
  case P_POTATOMINE: p->action = 1500; break;
  case P_GRAVEBUSTER: p->action = 400; break;
  case P_PEASHOOTER: case P_SNOWPEA: case P_REPEATER: case P_PUFFSHROOM: case P_FUMESHROOM: case P_SCAREDYSHROOM:
  case P_THREEPEATER: case P_SEASHROOM: case P_CACTUS: case P_SPLITPEA: case P_STARFRUIT: case P_CABBAGEPULT:
  case P_KERNELPULT: case P_MELONPULT:
    p->action = (int16_t)irand(151);
    break;
  default: p->action = 50;
  }
}

bool board_plant(int slot, int col, int row) {
  int kind = seed_kind(slot);
  if (B.phase != PH_PLAY || kind < 0) return false;
  if (B.mode == MODE_VASE) kind = B.drops[slot].kind;
  if (plant_issue(kind, col, row)) return false;
  if (B.mode == MODE_BOWLING) {
    if (B.nrollers >= MAX_ROLLERS) return false;
    Roller *r = &B.rollers[B.nrollers++];
    memset(r, 0, sizeof *r);
    r->id = new_id(), r->kind = (uint8_t)kind, r->row = (uint8_t)row, r->x = cell_x(col), r->y = row_y(row, r->x);
    memmove(&B.packets[slot], &B.packets[slot + 1], (size_t)(B.npackets - slot - 1) * sizeof B.packets[0]);
    B.npackets--;
    B.planted++;
    return true;
  }
  if (B.mode == MODE_VASE) {
    B.drops[slot] = B.drops[--B.ndrops];
  } else if (B.mode == MODE_CONVEYOR) {
    memmove(&B.packets[slot], &B.packets[slot + 1], (size_t)(B.npackets - slot - 1) * sizeof B.packets[0]);
    B.npackets--;
  } else {
    if (B.sun < plant_cost(kind) || B.cooldown[slot] > 0) return false;
    B.sun -= plant_cost(kind);
    B.cooldown[slot] = (int16_t)plant_defs[kind].cooldown;
  }
  if ((kind == P_WALLNUT || kind == P_TALLNUT || kind == P_PUMPKIN) && (progress.items & ITEM_FIRST_AID)) {
    Plant *old = plant_at(col, row, kind);
    if (old) old->health = 0;
  }
  if (B.nplants >= MAX_PLANTS) {
    /* (room from the dead first) */
    for (int i = 0; i < B.nplants; i++)
      if (B.plants[i].health <= 0) B.plants[i--] = B.plants[--B.nplants];
    if (B.nplants >= MAX_PLANTS) return false;
  }
  plant_new(&B.plants[B.nplants++], kind, col, row);
  B.started = 1;
  B.planted++;
  fx_add(water_row(row) ? FX_SPLASH : FX_DIRT, cell_x(col), row_y(row, cell_x(col)), 40, 0);
  if (B.tutorial == 1) {
    int peas = 0;
    for (int i = 0; i < B.nplants; i++) peas += B.plants[i].kind == P_PEASHOOTER;
    if (peas >= 2) B.tutorial = 0, B.wave_in = 99;
  }
  if (B.tutorial == 2 || B.tutorial == 3) {
    int sf = 0;
    for (int i = 0; i < B.nplants; i++) sf += B.plants[i].kind == P_SUNFLOWER;
    if (sf >= 3) {
      if (B.tutorial == 2 && B.wave == 0) B.wave_in = 999;
      B.tutorial = 0;
    }
  }
  return true;
}

Plant *dig_target(int col, int row) {
  Plant *pumpkin = NULL, *main = NULL, *base = NULL;
  for (int i = 0; i < B.nplants; i++) {
    Plant *p = &B.plants[i];
    if (p->col != col || p->row != row || p->health <= 0) continue;
    if (p->kind == P_PUMPKIN) pumpkin = p;
    else if (p->kind == P_LILYPAD || p->kind == P_FLOWERPOT) base = p;
    else main = p;
  }
  return pumpkin ? pumpkin : main ? main : base;
}

bool board_dig(int col, int row) {
  Plant *p = dig_target(col, row);
  if (!p || B.phase != PH_PLAY) return false;
  for (int i = 0; i < B.nladders; i++)
    if (B.ladders[i].col == col && B.ladders[i].row == row) B.ladders[i--] = B.ladders[--B.nladders];
  if (p->kind == P_TANGLEKELP && p->state) {
    Zombie *z = zombie_by_id(p->target);
    if (z) z->health = 0;
  }
  p->health = 0;
  fx_add(FX_DIRT, cell_x(col), row_y(row, cell_x(col)), 30, 0);
  return true;
}

/* ---------------------------------------------------------------- the conveyor belt (Challenge::UpdateConveyor) */

static int next_conveyor_seed(void) {
  if (B.mode == MODE_BOWLING) return frand() < 0.85f ? P_WALLNUT : P_EXPLODENUT;
  const LevelDef *d = &level_defs[B.level - 1];
  int w[10], total = 0;
  for (int i = 0; i < d->nseeds; i++) {
    int kind = d->seeds[i], weight = d->weights[i];
    int count = 0, planted = 0;
    for (int k = 0; k < B.npackets; k++) count += B.packets[k].kind == kind;
    for (int k = 0; k < B.nplants; k++) planted += B.plants[k].kind == kind;
    int all = count + planted;
    if (kind == P_GRAVEBUSTER && B.ngraves <= all) weight = 0;
    if (kind == P_FLOWERPOT || kind == P_LILYPAD) {
      float f = all / (kind == P_FLOWERPOT ? 35.0f : 18.0f);
      if (f > 1) f = 1;
      weight = (int)(weight + (1 - weight) * f + 0.5f);
    }
    if (B.has_boss && (kind == P_MELONPULT || kind == P_KERNELPULT || kind == P_CABBAGEPULT)) {
      int empty = 0;
      for (int k = 0; k < B.nplants; k++) {
        const Plant *p = &B.plants[k];
        if (p->kind == P_FLOWERPOT && p->health > 0 && !plant_at(p->col, p->row, -1)) empty++;
      }
      if (empty <= 5) weight /= empty <= 2 ? 5 : 3;
    }
    if (B.has_boss && boss.phase == BS_RV && kind == P_FLOWERPOT) weight = 500;
    w[i] = count >= 4 ? 1 : count >= 3 ? 5 : kind == B.last_conveyor ? weight / 2 : weight;
    total += w[i];
  }
  int pick = irand(total), i = 0;
  while (i < d->nseeds - 1 && pick >= w[i]) pick -= w[i++];
  B.last_conveyor = d->seeds[i];
  return d->seeds[i];
}

static void conveyor_tick(void) {
  if (B.ticks % 4 == 0)
    for (int i = 0; i < B.npackets; i++) {
      float m = (float)(i * 50);
      if (B.packets[i].x - 1 > m) B.packets[i].x -= 1;
      else B.packets[i].x = m;
    }
  if (--B.conveyor_in <= 0) {
    int n = B.npackets;
    B.conveyor_in = (int16_t)((n > 8 ? 1000 : n > 6 ? 500 : n > 4 ? 425 : 400) * (B.has_boss ? 0.875f : 1));
    if (n < 10) {
      float x = n == 0 ? 500 : B.packets[n - 1].x + 50;
      B.packets[n].kind = (uint8_t)next_conveyor_seed();
      B.packets[n].x = x < 500 ? 500 : x;
      B.npackets++;
    }
  }
}

/* ---------------------------------------------------------------- Vasebreaker */

static const struct { uint8_t first, green, n; struct { uint8_t zombie, kind, count; } c[8]; } vase_stages[3] = {
  {6, 0, 4, {{0, P_PEASHOOTER, 5}, {0, P_SQUASH, 5}, {1, Z_NORMAL, 4}, {1, Z_BUCKET, 1}}},
  {5, 2, 6, {{0, P_PEASHOOTER, 4}, {0, P_SNOWPEA, 5}, {0, P_SQUASH, 4}, {1, Z_NORMAL, 5}, {1, Z_BUCKET, 1},
             {1, Z_FOOTBALL, 1}}},
  {4, 3, 7, {{0, P_PEASHOOTER, 5}, {0, P_SNOWPEA, 5}, {0, P_HYPNOSHROOM, 5}, {1, Z_NORMAL, 6}, {1, Z_BUCKET, 2},
             {1, Z_DANCER, 1}, {1, Z_JACK, 1}}},
};

static void populate_vases(void) {
  Cell cells[45];
  int n = 0;
  const typeof(vase_stages[0]) *st = &vase_stages[B.vase_stage];
  for (int r = 0; r < B.nrows; r++)
    for (int c = st->first; c < COLS; c++) cells[n].row = (uint8_t)r, cells[n++].col = (uint8_t)c;
  B.nvases = 0;
  for (int i = 0; i < st->n; i++)
    for (int k = 0; k < st->c[i].count && n; k++) {
      int j = irand(n);
      Vase *v = &B.vases[B.nvases++];
      v->row = cells[j].row, v->col = cells[j].col, v->zombie = st->c[i].zombie, v->kind = st->c[i].kind, v->green = 0;
      cells[j] = cells[--n];
    }
  /* green vases always hold a plant */
  for (int g = 0; g < st->green; g++) {
    int seeds[45], m = 0;
    for (int i = 0; i < B.nvases; i++)
      if (!B.vases[i].zombie && !B.vases[i].green) seeds[m++] = i;
    if (m) B.vases[seeds[irand(m)]].green = 1;
  }
  B.wave = (uint8_t)(B.vase_stage + 1);
}

void board_break_vase(int col, int row) {
  for (int i = 0; i < B.nvases; i++) {
    Vase *v = &B.vases[i];
    if (v->col != col || v->row != row) continue;
    if (v->zombie) spawn_zombie(v->kind, row, cell_x(col), B.vase_stage);
    else if (B.ndrops < 30) {
      Drop *d = &B.drops[B.ndrops++];
      d->kind = v->kind, d->row = (uint8_t)row, d->col = (uint8_t)col, d->left = 1500, d->times = 0;
    }
    fx_add(FX_SMOKE, cell_x(col), row_y(row, cell_x(col)) - 40, 40, 1);
    B.vases[i] = B.vases[--B.nvases];
    return;
  }
}

void board_next_vase_stage(void) {
  B.vase_stage++;
  B.nplants = B.nzombies = B.nshots = B.ndrops = B.nsuns = 0;
  populate_vases();
}

/* ---------------------------------------------------------------- Whack a Zombie */

void whack_graves(int count) {
  Cell places[45];
  int n = 0;
  for (int r = 0; r < B.nrows; r++)
    for (int c = 3; c < 9; c++) {
      bool there = false;
      for (int i = 0; i < B.ngraves; i++) there |= B.graves[i].row == r && B.graves[i].col == c;
      if (!there) places[n].row = (uint8_t)r, places[n++].col = (uint8_t)c;
    }
  for (int k = 0; k < count && n; k++) {
    float w[45], total = 0;
    for (int i = 0; i < n; i++) w[i] = plant_at(places[i].col, places[i].row, -1) ? 1 : 100000, total += w[i];
    float pick = frand() * total;
    int i = 0;
    while (i < n - 1 && pick >= w[i]) pick -= w[i++];
    for (int j = 0; j < B.nplants; j++)
      if (B.plants[j].row == places[i].row && B.plants[j].col == places[i].col) B.plants[j].health = 0;
    add_grave(places[i].col, places[i].row);
    places[i] = places[--n];
  }
}

static void whack_tick(void) {
  if (B.whack_final) return;
  B.wave_in--;
  if (B.wave_in == 100 && B.wave > 0) whack_graves(5 - B.ngraves > 1 ? 5 - B.ngraves : 1);
  if (B.wave_in == 5 && B.wave == B.nwaves - 1) fx_add(FX_TEXT, 400, 280, 400, 1);
  if (B.wave_in == 0) {
    B.wave_in = 2000, B.wave++;
    B.whack_spawn_in = B.wave == B.nwaves ? 300 : 1;
  } else if (B.wave_in < 300)
    return;
  if (--B.whack_spawn_in != 0) return;
  int phase = (B.wave - 1) * 6 / 12;
  phase = phase < 0 ? 0 : phase > 5 ? 5 : phase;
  static const uint8_t doubles[6] = {0, 30, 10, 10, 15, 18}, triples[6] = {0, 0, 0, 0, 10, 13};
  static const uint8_t pails[6] = {0, 0, 0, 10, 15, 15}, cones[6] = {0, 0, 30, 30, 30, 30};
  int group = irand(100), type = irand(100);
  bool final = B.wave == B.nwaves;
  int count = final ? 20 : group < triples[phase] ? 3 : group < triples[phase] + doubles[phase] ? 2 : 1;
  int kind = type < pails[phase] && count < 3 ? Z_BUCKET : type < pails[phase] + cones[phase] ? Z_CONE : Z_NORMAL;
  Cell graves[16];
  int ng = 0;
  for (int i = 0; i < B.ngraves; i++)
    if (!plant_at(B.graves[i].col, B.graves[i].row, P_GRAVEBUSTER)) graves[ng++] = B.graves[i];
  float wf = (B.wave - 1) / 11.0f;
  int max_speed = final ? 2 : (int)(1 + 2 * wf * wf + 0.5f);
  for (int i = 0; i < count && ng; i++) {
    int g = irand(ng);
    Zombie *z = spawn_zombie(final ? (frand() < 0.5f ? Z_CONE : Z_BUCKET) : kind, graves[g].row,
                             cell_x(graves[g].col) - 35, B.wave - 1);
    if (z) z->rise = 50, z->speed = 0.5f + frand() * (max_speed - 0.5f);
    graves[g] = graves[--ng];
  }
  int lo = (int)(100 - (B.wave - 1) * 70 / 11.0f + 0.5f), hi = (int)(200 - (B.wave - 1) * 140 / 11.0f + 0.5f);
  B.whack_spawn_in = (int16_t)(lo + irand(hi - lo + 1));
  if (final) B.whack_final = 1, B.wave_in = 0, B.whack_spawn_in = 0;
}

bool board_whack(float x, float y) {
  B.hammer_t = 20;
  Zombie *best = NULL;
  for (int i = 0; i < B.nzombies; i++) {
    Zombie *z = &B.zombies[i];
    if (z->health <= 0 || zombie_headless(z)) continue;
    float dx = x - z->x, dy = y - 20 - (row_y(z->row, z->x) - 60);
    dx = dx < 0 ? -dx : dx, dy = dy < 0 ? -dy : dy;
    dx = dx > 24 ? dx - 24 : 0, dy = dy > 55 ? dy - 55 : 0;
    if (dx * dx + dy * dy > 45 * 45) continue;
    if (!best || z->row > best->row || (z->row == best->row && z->id > best->id)) best = z;
  }
  if (!best) return false;
  if (best->armor > 0) best->armor = (int16_t)(best->armor > 900 ? best->armor - 900 : 0);
  else kill_zombie(best, DIE_NORMAL);
  best->hit_at = B.ticks;
  fx_add(FX_POW, x, y, 25, 0);
  return true;
}

/* ---------------------------------------------------------------- a tick */

static void mowers_tick(void) {
  for (int r = 0; r < B.nrows; r++) {
    Mower *m = &B.mowers[r];
    if (m->state == 0) {
      for (int i = 0; i < B.nzombies; i++) {
        const Zombie *z = &B.zombies[i];
        if ((z->flags & (ZF_HYPNO | ZF_UNDERGROUND)) || zombie_flying(z) || z->kind == Z_BUNGEE || z->health <= 0) continue;
        if (z->row == r && z->x < 28) {
          m->state = 1;
          B.row_mowed[r] = (int8_t)(B.wave > 120 ? 120 : B.wave);
          break;
        }
      }
    }
    if (m->state == 1) {
      m->x += 3.5f;
      for (int i = 0; i < B.nzombies; i++) {
        Zombie *z = &B.zombies[i];
        if ((z->flags & (ZF_HYPNO | ZF_UNDERGROUND)) || zombie_flying(z) || z->kind == Z_BUNGEE || z->health <= 0) continue;
        if (z->row == r && z->x - m->x < 45 && m->x - z->x < 45) kill_zombie(z, DIE_MOWED);
      }
      if (m->x > 890) m->state = 2;
    }
  }
}

static void graves_wave(void) {
  if (B.env == ENV_ROOF)
    for (int i = 0; i < (B.level < 43 ? 2 : 3); i++) drop_from_sky(frand() < 0.5f ? Z_NORMAL : Z_CONE, irand(5), B.wave - 1);
  if (is_pool() && B.pool_frozen == 0) {
    Cell cells[8];
    int n = 0;
    for (int r = 2; r <= 3; r++)
      for (int c = 5; c <= 8; c++) cells[n].row = (uint8_t)r, cells[n++].col = (uint8_t)c;
    for (int i = 0; i < (B.level <= 22 ? 2 : 3); i++) {
      int k = irand(n);
      Zombie *z = spawn_zombie(frand() < 0.5f ? Z_NORMAL : Z_CONE, cells[k].row, cell_x(cells[k].col) - 35, B.wave - 1);
      if (z) z->rise = 50;
      cells[k] = cells[--n];
    }
  }
  for (int i = 0; i < B.ngraves; i++) {
    int kind = (zombie_defs[Z_CONE].allowed >> (B.level - 1) & 1) && frand() < 0.5f ? Z_CONE : Z_NORMAL;
    Zombie *z = spawn_zombie(kind, B.graves[i].row, cell_x(B.graves[i].col) - 35, B.wave - 1);
    if (z) z->rise = 150;
  }
}

bool board_won(void) {
  if (B.mode == MODE_VASE) return false;
  for (int i = 0; i < B.nzombies; i++)
    if (!(B.zombies[i].flags & ZF_HYPNO) && B.zombies[i].health > 0) return false;
  return true;
}

void board_tick(void) {
  if (B.phase != PH_PLAY) return;
  B.ticks++;
  if (B.hammer_t > 0) B.hammer_t--;
  if (B.level == 40) {
    B.storm_in--;
    if (B.storm_in <= 0) {
      int max = frand() < 0.5f ? 750 : 400;
      B.storm_in = (int16_t)(450 + irand(max - 299));
      B.storm_flash = (uint8_t)(1 + irand(3));
    }
  }
  for (int r = 0; r < B.nrows; r++)
    if (B.ice[r].left > 0) B.ice[r].left--;
  if (B.fog_blown > 0) B.fog_blown--;
  for (int i = 0; i < B.ndrops; i++)
    if (--B.drops[i].left <= 0) B.drops[i--] = B.drops[--B.ndrops];
  for (int i = 0; i < B.ncraters; i++)
    if (--B.craters[i].left <= 0) B.craters[i--] = B.craters[--B.ncraters];
  if (B.pool_frozen > 0) B.pool_frozen--;
  for (int i = 0; i < B.nseeds; i++)
    if (B.cooldown[i] > 0) B.cooldown[i]--;
  /* the effects */
  for (int i = 0; i < B.nfx; i++)
    if (++B.fx[i].t >= B.fx[i].life) B.fx[i--] = B.fx[--B.nfx];
  /* (nothing moves before the first plant, the first time through, nor in 1-5's shovel lesson) */
  if (!B.started || B.tutorial == 4) return;
  if (has_conveyor()) conveyor_tick();

  plants_tick();
  zombies_tick();
  shots_tick();
  /* the bowling wall-nuts */
  for (int i = 0; i < B.nrollers; i++) {
    Roller *n = &B.rollers[i];
    n->x += 3.3f;
    if (n->dir) {
      n->y += n->dir * 2;
      if (n->y <= row_y(0, n->x)) n->dir = 1;
      if (n->y >= row_y(4, n->x)) n->dir = -1;
      n->row = (uint8_t)((n->y - row_y(0, n->x)) / 100 + 0.5f);
    }
    for (int k = 0; k < B.nzombies; k++) {
      Zombie *z = &B.zombies[k];
      if ((z->flags & ZF_HYPNO) || z->health <= 0 || z->row != n->row || z->x - n->x >= 40 || n->x - z->x >= 40) continue;
      float dy = n->y - row_y(z->row, z->x);
      if (dy > 2 || dy < -2) continue;
      bool seen = false;
      for (int h = 0; h < n->nhit; h++) seen |= n->hit[h] == z->id;
      if (seen) continue;
      if (n->nhit < 6) n->hit[n->nhit++] = z->id;
      if (n->kind == P_EXPLODENUT) {
        for (int j = 0; j < B.nzombies; j++) {
          Zombie *o = &B.zombies[j];
          int dr = o->row - n->row;
          if (dr >= -1 && dr <= 1 && o->x - n->x <= 90 && n->x - o->x <= 90) {
            damage_zombie(o, 1800, true);
            if (o->health <= 0) kill_zombie(o, DIE_BURN);
          }
        }
        fx_add(FX_BOOM, n->x, n->y - 35, 60, 0);
        n->x = 1000;
        break;
      }
      /* (Plant::UpdateBowling: a paper or a door takes 400, a cone or a bucket 900, and the blow stops there;
       * a door hit by a nut already bouncing, and a bare zombie, take 1800) */
      if (z->shield > 0 && !(z->kind == Z_DOOR && n->dir)) damage_zombie(z, z->shield < 400 ? z->shield : 400, false);
      else if (z->shield <= 0 && z->armor > 0) damage_zombie(z, z->armor < 900 ? z->armor : 900, false);
      else damage_zombie(z, 1800, false);
      z->hit_at = B.ticks;
      if (z->health <= 0) kill_zombie(z, DIE_NORMAL);
      n->dir = n->row == 0 ? 1 : n->row == 4 ? -1 : n->dir == 0 ? (frand() < 0.5f ? -1 : 1) : -n->dir;
      fx_add(FX_POW, n->x, n->y - 35, 20, 0);
    }
    if (n->x > 900) B.rollers[i--] = B.rollers[--B.nrollers];
  }
  /* sun lands and fades; coins too */
  for (int i = 0; i < B.nsuns; i++) {
    Sun *s = &B.suns[i];
    if (s->y < s->ty) s->y = s->y + 0.65f > s->ty ? s->ty : s->y + 0.65f;
    else if (--s->left <= 0) B.suns[i--] = B.suns[--B.nsuns];
  }
  for (int i = 0; i < B.ncoins; i++) {
    Coin *c = &B.coins[i];
    if (c->y < c->ty) c->y = c->y + 1.5f > c->ty ? c->ty : c->y + 1.5f;
    else if (--c->left <= 0) B.coins[i--] = B.coins[--B.ncoins];
  }
  mowers_tick();
  /* the dead go */
  for (int i = 0; i < B.nplants; i++)
    if (B.plants[i].health <= 0) {
      for (int k = 0; k < B.nladders; k++)
        if (B.ladders[k].col == B.plants[i].col && B.ladders[k].row == B.plants[i].row &&
            !dig_target(B.ladders[k].col, B.ladders[k].row))
          B.ladders[k--] = B.ladders[--B.nladders];
      B.plants[i--] = B.plants[--B.nplants];
    }
  for (int i = 0; i < B.nzombies; i++) {
    Zombie *z = &B.zombies[i];
    bool gone = (z->flags & (ZF_HYPNO | ZF_BACKWARDS)) && z->x > 900;
    if (z->health <= 0 || gone) {
      if (z->health <= 0 && !(z->flags & ZF_HYPNO)) B.kills++;
      B.zombies[i--] = B.zombies[--B.nzombies];
    }
  }
  /* a zombie in the house */
  for (int i = 0; i < B.nzombies; i++) {
    const Zombie *z = &B.zombies[i];
    if (!(z->flags & (ZF_HYPNO | ZF_UNDERGROUND | ZF_BACKWARDS)) && z->x < -35 && z->kind != Z_BUNGEE) {
      B.phase = PH_LOST;
      B.last_x = z->x, B.last_y = (float)z->row;
      return;
    }
  }
  /* sun from the sky */
  if (B.mode == MODE_LAWN && !is_night() && --B.sun_in <= 0) {
    int rows[MAX_ROWS], n = 0;
    for (int r = 0; r < B.nrows; r++)
      if (B.rows >> r & 1) rows[n++] = r;
    int row = rows[irand(n)];
    add_sun(100 + frand() * 549, 70, row_y(row, 440) - 30, 25, true);
    B.suns_fallen++;
    int base = 425 + B.suns_fallen * 10;
    B.sun_in = (int16_t)((base < 950 ? base : 950) + irand(275));
  }
  if (B.has_boss) {
    boss_tick();
    return;
  }
  if (B.mode == MODE_VASE) {
    bool zombies = false;
    for (int i = 0; i < B.nzombies; i++) zombies |= !(B.zombies[i].flags & ZF_HYPNO);
    if (B.nvases == 0 && !zombies) {
      if (B.vase_stage == 2) B.phase = PH_WON;
      else board_next_vase_stage();
    }
    return;
  }
  if (B.grave_wave_in > 0 && --B.grave_wave_in == 0) graves_wave();
  if (B.mode == MODE_WHACK) {
    whack_tick();
    if (B.whack_final && B.nzombies == 0) B.phase = PH_WON;
    return;
  }
  if (B.tutorial == 1) return;
  if (B.wave < B.nwaves) {
    B.wave_elapsed++;
    int32_t remaining = 0;
    for (int i = 0; i < B.nzombies; i++) {
      const Zombie *z = &B.zombies[i];
      if (!(z->flags & ZF_HYPNO) && z->wave == B.wave - 1 && !zombie_headless(z)) remaining += zombie_total_health(z);
    }
    if (B.flag_warning > 0) {
      if (--B.flag_warning == 0) spawn_wave();
      return;
    }
    if (B.wave > 0 && B.wave_elapsed > 400 && B.wave_in > 200 && remaining <= B.wave_threshold) B.wave_in = 200;
    B.wave_in--;
    if (B.wave_in == 5 && is_flag_wave(B.wave)) {
      B.flag_warning = 750;
      fx_add(FX_TEXT, 400, 280, 450, 0);   /* "A Huge Wave of Zombies is Approaching!" */
    } else if (B.wave_in <= 0)
      spawn_wave();
  } else if (board_won() && B.grave_wave_in == 0) {
    B.phase = PH_WON;
  }
}
