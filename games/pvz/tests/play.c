/* Plays Plants vs. Zombies on a computer, and saves frames.
 *
 *   play [--frames N] [--keys "10-40:right,50:ok,..."] [--shots 30,60|every:N] [--out DIR] [--saves DIR]
 *        [--progress L] [--money M]
 *   play --sim L [--frames N] [--shots ...] [--out DIR] [--quiet]
 *   play --bot L [--progress P] [--replay] [--stall-home] [--frames N] [--saves DIR] (see "the player" below)
 *   ... [--raw DIR --raw-from T --raw-to T] [--sun-from T]
 *
 * Keys: left right up down ok back shovel home almanac 0-9. "a-b:key" holds the key from tick a to b, "a:key" for
 * one tick (ticks are hundredths of a second; a frame is drawn every 3).
 * --progress L sets the adventure at level L with what it has found by then (then the menus play as scripted).
 * --sim L plays level L straight away, a robot planting (with all the sun it wants, so that every level's zombies
 * come and go): it prints how the level went and fails if it isn't won. Screenshots are DIR/shot_<tick>.ppm.
 * --raw saves every frame drawn from tick T to T as RGB565 (DIR/frame_<ms>.raw, for NumPlay's tools/record.py
 * --frames); --sun-from T: from then on the robot has the sun it collects (a level as it looks). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "../src/draw.h"
#include "../src/game.h"
#include "../src/gfx.h"
#include "../src/res.h"
#include "../src/save.h"
#include "../src/screens.h"

extern uint32_t host_keys, host_time;
extern uint16_t host_fb[SCREEN_W * SCREEN_H];
void host_shot(const char *path);
void host_save_dir(const char *d);
extern const uint8_t pvz_data[];
extern const uint8_t *pvz_bin;
const uint8_t *pvz_bin;

typedef struct { int a, b; uint32_t k; } Hold;
static Hold holds[4096];
static int nholds;

static uint32_t key_named(const char *s) {
  static const struct { const char *n; uint32_t k; } t[] = {
    {"left", K_LEFT}, {"right", K_RIGHT}, {"up", K_UP}, {"down", K_DOWN}, {"ok", K_OK}, {"back", K_BACK},
    {"shovel", K_SHOVEL}, {"home", K_HOME}, {"almanac", K_ALMANAC},
  };
  for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++)
    if (!strcmp(s, t[i].n)) return t[i].k;
  if (s[0] >= '0' && s[0] <= '9' && !s[1]) return K_DIGIT(s[0] == '0' ? 10 : s[0] - '0');
  fprintf(stderr, "unknown key %s\n", s);
  exit(2);
}

static void parse_keys(const char *spec) {
  char buf[65536];
  snprintf(buf, sizeof buf, "%s", spec);
  for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
    char *colon = strchr(tok, ':');
    if (!colon) continue;
    *colon = 0;
    int a, b;
    if (sscanf(tok, "%d-%d", &a, &b) != 2) b = a = atoi(tok);
    holds[nholds].a = a, holds[nholds].b = b, holds[nholds].k = key_named(colon + 1);
    if (nholds < 4095) nholds++;
  }
}

static int shots[1024], nshots_at, every;
static const char *raw_dir;
static int raw_from, raw_to = -1, sun_from = 1 << 30;

static void raw_frame(int tick) {
  char p[512];
  snprintf(p, sizeof p, "%s/frame_%06d.raw", raw_dir, tick * 10);
  FILE *f = fopen(p, "wb");
  if (!f) return;
  fwrite(host_fb, 2, SCREEN_W * SCREEN_H, f);
  fclose(f);
}
static bool raw_now(int tick) { return raw_dir && tick >= raw_from && tick <= raw_to && tick % 3 == 0; }
static void parse_shots(const char *s) {
  if (!strncmp(s, "every:", 6)) {
    every = atoi(s + 6);
    return;
  }
  char buf[8192];
  snprintf(buf, sizeof buf, "%s", s);
  for (char *t = strtok(buf, ","); t && nshots_at < 1024; t = strtok(NULL, ",")) shots[nshots_at++] = atoi(t);
}
static bool shot_now(int tick) {
  if (every && tick % every == 0) return true;
  for (int i = 0; i < nshots_at; i++)
    if (shots[i] == tick) return true;
  return false;
}

/* ---------------------------------------------------------------- the robot */

static const int prefer[] = {P_SUNFLOWER, P_REPEATER, P_THREEPEATER, P_SNOWPEA, P_PEASHOOTER, P_MELONPULT, P_KERNELPULT,
                             P_CABBAGEPULT, P_CHERRYBOMB, P_CACTUS, P_SPLITPEA, P_TALLNUT, P_WALLNUT, P_LILYPAD,
                             P_FLOWERPOT, P_PLANTERN, P_PUMPKIN, P_STARFRUIT, P_FUMESHROOM, P_PUFFSHROOM, P_TORCHWOOD,
                             P_SQUASH, P_SPIKEWEED, P_GRAVEBUSTER, P_UMBRELLA, P_MAGNETSHROOM, P_COFFEEBEAN, P_JALAPENO};

static bool level_has(int kind) {
  for (int w = 0; w < B.nwaves && w < 50; w++)
    for (int i = 0; i < 24 && B.waves[w][i] != 0xFF; i++)
      if (B.waves[w][i] == kind) return true;
  return false;
}

static void robot_seeds(int level) {
  if (B.nseeds || level_defs[level - 1].nseeds) return;
  uint8_t s[10];
  int n = 0;
  bool has_repeater = plant_unlocked(P_REPEATER), pult = plant_unlocked(P_CABBAGEPULT);
  for (unsigned i = 0; i < sizeof prefer / sizeof prefer[0] && n < 10; i++) {
    int k = prefer[i];
    if (!plant_unlocked(k)) continue;
    if (k == P_PEASHOOTER && has_repeater) continue;
    if ((k == P_REPEATER || k == P_THREEPEATER || k == P_SNOWPEA || k == P_PEASHOOTER || k == P_SPLITPEA ||
         k == P_TORCHWOOD) && B.env == ENV_ROOF)
      continue;
    if (k == P_CACTUS && !level_has(Z_BALLOON)) continue;
    if (k == P_SPLITPEA && !level_has(Z_DIGGER)) continue;
    if (k == P_UMBRELLA && !level_has(Z_CATAPULT) && !level_has(Z_BUNGEE)) continue;
    if ((k == P_FUMESHROOM || k == P_PUFFSHROOM) && (pult || !is_night())) continue;
    if ((k == P_LILYPAD) && !is_pool()) continue;
    if (k == P_FLOWERPOT && B.env != ENV_ROOF) continue;
    if (k == P_GRAVEBUSTER && !B.ngraves) continue;
    if ((k == P_SPIKEWEED || k == P_GRAVEBUSTER) && B.env == ENV_ROOF) continue;
    if (k == P_PLANTERN && B.env != ENV_FOG) continue;
    if (k == P_COFFEEBEAN && is_night()) continue;
    s[n++] = (uint8_t)k;
  }
  board_set_seeds(s, n);
}

/* what the robot would like in a cell, in order (the first it can plant) */
static int want(int col, int row, int *kinds) {
  int n = 0;
  bool shooter_ok = B.env != ENV_ROOF;
  if (water_row(row)) kinds[n++] = P_LILYPAD;
  if (B.env == ENV_ROOF) kinds[n++] = P_FLOWERPOT;
  if (col == 0) kinds[n++] = P_SUNFLOWER, kinds[n++] = P_SUNSHROOM;
  if (col == 1) kinds[n++] = P_SPLITPEA, kinds[n++] = P_CACTUS;
  if (col >= 1 && col <= 4) {
    if (col == 4) kinds[n++] = P_CACTUS;
    if (shooter_ok) kinds[n++] = P_REPEATER, kinds[n++] = P_THREEPEATER, kinds[n++] = P_SNOWPEA, kinds[n++] = P_PEASHOOTER;
    kinds[n++] = P_MELONPULT, kinds[n++] = P_KERNELPULT, kinds[n++] = P_CABBAGEPULT;
    kinds[n++] = P_FUMESHROOM, kinds[n++] = P_STARFRUIT, kinds[n++] = P_CACTUS, kinds[n++] = P_PUFFSHROOM;
    kinds[n++] = P_SCAREDYSHROOM, kinds[n++] = P_SEASHROOM;
  }
  if (col == 5) kinds[n++] = P_TORCHWOOD, kinds[n++] = P_PLANTERN, kinds[n++] = P_UMBRELLA;
  if (col == 6) kinds[n++] = P_TALLNUT, kinds[n++] = P_WALLNUT, kinds[n++] = P_PUMPKIN;
  if (col == 7) kinds[n++] = P_SPIKEWEED, kinds[n++] = P_GRAVEBUSTER;
  kinds[n++] = P_COFFEEBEAN, kinds[n++] = P_PUMPKIN;
  return n;
}

/* a seed from the conveyor belt, where it does the most */
static bool robot_packet(int i) {
  int kind = B.packets[i].kind;
  if (B.mode == MODE_BOWLING) {
    for (int r = 0; r < B.nrows; r++) {
      int rr = (r + (int)(B.ticks / 25)) % B.nrows;
      if (!plant_issue(kind, 2, rr) && board_plant(i, 2, rr)) return true;
    }
    return false;
  }
  /* Dr. Zomboss: ice for his fire ball (and his head), fire for his ice ball */
  if (B.has_boss && (kind == P_ICESHROOM || kind == P_JALAPENO)) {
    bool fire = boss.ball.on && boss.ball.kind == 0, ice = boss.ball.on && boss.ball.kind == 1;
    if (kind == P_ICESHROOM && (fire || boss_vulnerable()))
      for (int r = 0; r < B.nrows; r++)
        for (int c = 0; c < 6; c++)
          if (!plant_issue(kind, c, r) && board_plant(i, c, r)) return true;
    if (kind == P_JALAPENO && ice)
      for (int c = 0; c < 6; c++)
        if (!plant_issue(kind, c, boss.ball.row) && board_plant(i, c, boss.ball.row)) return true;
    /* (or on the zombies, when they are close) */
    for (int k = 0; k < B.nzombies; k++) {
      const Zombie *z = &B.zombies[k];
      if (z->health > 0 && z->x < (kind == P_JALAPENO ? 400 : 250) && !plant_issue(kind, col_at(z->x), z->row))
        return board_plant(i, col_at(z->x), z->row);
    }
    return false;
  }
  /* the instant ones: on the zombie nearest the house */
  if (kind == P_CHERRYBOMB || kind == P_JALAPENO || kind == P_SQUASH || kind == P_TANGLEKELP || kind == P_ICESHROOM ||
      kind == P_DOOMSHROOM) {
    const Zombie *near = NULL;
    for (int k = 0; k < B.nzombies; k++) {
      const Zombie *z = &B.zombies[k];
      if (z->health > 0 && !(z->flags & ZF_HYPNO) && z->x < 700 && (!near || z->x < near->x) &&
          !plant_issue(kind, col_at(z->x), z->row))
        near = z;
    }
    return near && board_plant(i, col_at(near->x), near->row);
  }
  /* the others where they'd go, the rows in turn */
  for (int r = 0; r < B.nrows; r++) {
    int rr = (r + (int)(B.ticks / 25)) % B.nrows;
    for (int c = 0; c < COLS - 2; c++) {
      int kinds[24], n = want(c, rr, kinds);
      for (int k = 0; k < n; k++)
        if (kinds[k] == kind && !plant_issue(kind, c, rr) && board_plant(i, c, rr)) return true;
    }
  }
  return false;
}

static void robot(void) {
  if (B.phase != PH_PLAY) return;
  if ((int)B.ticks < sun_from) B.sun = 5000;
  else if ((int)B.ticks == sun_from) B.sun = 325;
  /* sun and coins: picked up */
  board_collect(400, 300, 5000);
  if (B.mode == MODE_WHACK) {
    for (int i = 0; i < B.nzombies; i++)
      if (B.zombies[i].x < 700 && B.zombies[i].rise == 0 && (B.ticks + i) % 7 == 0)
        board_whack(B.zombies[i].x, row_y(B.zombies[i].row, B.zombies[i].x) - 40);
    for (int g = 0; g < B.ngraves; g++)
      for (int s = 0; s < B.nseeds; s++)
        if (B.seeds[s] == P_GRAVEBUSTER && !B.cooldown[s]) board_plant(s, B.graves[g].col, B.graves[g].row);
    return;
  }
  if (B.mode == MODE_VASE) {
    /* the seeds found: where a zombie is, or in the rows in turn */
    if (B.ndrops) {
      int kind = B.drops[0].kind, row = -1;
      for (int i = 0; i < B.nzombies; i++)
        if (B.zombies[i].health > 0 && (row < 0 || B.zombies[i].x < B.zombies[row].x)) row = i;
      row = row >= 0 ? B.zombies[row].row : (int)(B.ticks / 7) % B.nrows;
      if (kind == P_SQUASH || kind == P_CHERRYBOMB) {
        for (int i = 0; i < B.nzombies; i++)
          if (B.zombies[i].health > 0 && !plant_issue(kind, col_at(B.zombies[i].x), B.zombies[i].row)) {
            board_plant(0, col_at(B.zombies[i].x), B.zombies[i].row);
            break;
          }
      } else
        for (int c = 0; c < 5 && B.ndrops; c++)
          if (!plant_issue(kind, c, row) && board_plant(0, c, row)) break;
      if (B.ndrops && B.drops[0].left < 50) B.drops[0] = B.drops[--B.ndrops];
    } else if (B.nvases && B.ticks % 150 == 0) {
      /* a green one first, then from the far side, when the zombies out are few */
      int alive = 0, pick = 0;
      for (int i = 0; i < B.nzombies; i++) alive += B.zombies[i].health > 0;
      for (int i = 1; i < B.nvases; i++)
        if (B.vases[i].green > B.vases[pick].green ||
            (B.vases[i].green == B.vases[pick].green && B.vases[i].col > B.vases[pick].col))
          pick = i;
      if (alive < 2 || B.vases[pick].green) board_break_vase(B.vases[pick].col, B.vases[pick].row);
    }
    return;
  }
  if (has_conveyor()) {
    /* (with --sun-from, a few seeds on the belt first, as one plays) */
    if (!B.npackets || B.ticks % 25 || (sun_from < (1 << 30) && B.npackets < 4)) return;
    for (int i = 0; i < B.npackets; i++)
      if (robot_packet(i)) return;
    /* nowhere, the belt full: the first one dropped */
    if (B.npackets >= 8) {
      memmove(&B.packets[0], &B.packets[1], (size_t)(B.npackets - 1) * sizeof B.packets[0]);
      B.npackets--;
    }
    return;
  }
  /* plants: every cell, what it wants first (with --sun-from, a column at a time from the house, as one plays) */
  for (int i = 0; i < 8 * B.nrows; i++) {
    int c = sun_from < (1 << 30) ? i / B.nrows : i % 8, r = sun_from < (1 << 30) ? i % B.nrows : i / 8;
    {
      if (!(B.rows >> r & 1)) continue;
      int kinds[24], n = want(c, r, kinds);
      for (int k = 0; k < n; k++)
        for (int s = 0; s < B.nseeds; s++)
          if (B.seeds[s] == kinds[k] && !B.cooldown[s] && !plant_issue(kinds[k], c, r)) {
            board_plant(s, c, r);
            goto next;
          }
    next:;
    }
  }
  /* a cherry bomb where the zombies are thickest */
  for (int s = 0; s < B.nseeds; s++) {
    if ((B.seeds[s] != P_CHERRYBOMB && B.seeds[s] != P_JALAPENO) || B.cooldown[s]) continue;
    for (int i = 0; i < B.nzombies; i++) {
      const Zombie *z = &B.zombies[i];
      if (z->x < 500 && z->x > 100 && !(z->flags & ZF_HYPNO)) {
        int c = col_at(z->x);
        if (!plant_issue(B.seeds[s], c, z->row)) board_plant(s, c, z->row);
        break;
      }
    }
  }
}

/* ---------------------------------------------------------------- the player (--bot) */

/* --bot L: the Adventure through its screens, keys pressed as a player would, until level L is won or lost: OK through the
 * title, the menu, Crazy Dave and the rewards; on the lawn the robot above plants, but Wall-nut Bowling is played with
 * the keys (the shovel lesson, then a nut picked from the belt, the cursor to its row, OK). --replay: level L from
 * Choose a Level. --stall-home: in 1-5's bowling, the lawn left standing as NumPlay 1.17.0 and earlier left it after the
 * shovel lesson, and Home (the next start must bring the level back going). */
static int bot_level, bot_replay, bot_stall, bot_done;
static int bot_prev = -1, bot_t, bot_n;
static struct {
  int dave, line_dave, lesson, line_lesson, belt_lesson, play_t0, line_play, first_zombie, belt, explodenuts;
  int bounced, exploded, cones, huge, final, flag, stalled, came;
  uint16_t bounce_ids[64];
} bw;

static uint32_t step_to(int col, int row) {
  return hud.col < col ? K_RIGHT : hud.col > col ? K_LEFT : hud.row < row ? K_DOWN : hud.row > row ? K_UP : 0;
}

static uint32_t bot_bowl(int t) {
  if (B.tutorial == 4) {
    /* the shovel lesson: the shovel, then each peashooter */
    if (!hud.shovel) return K_SHOVEL;
    for (int i = 0; i < B.nplants; i++)
      if (B.plants[i].health > 0) {
        uint32_t k = step_to(B.plants[i].col, B.plants[i].row);
        return k ? k : K_OK;
      }
    return 0;
  }
  if (bot_stall && B.wave >= 1) {
    B.started = 0, bw.stalled = 1;
    return K_HOME;
  }
  /* the zombie nearest the house: a nut down its row (or, the belt full, down the rows in turn) */
  const Zombie *near = NULL;
  int alive = 0;
  for (int i = 0; i < B.nzombies; i++) {
    const Zombie *z = &B.zombies[i];
    if (z->health <= 0 || (z->flags & ZF_HYPNO) || z->x > 760) continue;
    alive++;
    if (!near || z->x < near->x) near = z;
  }
  int row = near ? near->row : B.npackets >= 8 ? t / 60 % B.nrows : -1;
  if (row < 0 || !B.npackets) return 0;
  if (hud.selected < 0) {
    int pick = -1;
    for (int i = 0; i < B.npackets && pick < 0; i++)
      if (B.packets[i].kind == (alive >= 3 ? P_EXPLODENUT : P_WALLNUT)) pick = i;
    return K_DIGIT((pick < 0 ? 0 : pick) + 1);
  }
  uint32_t k = step_to(2, row);
  return k ? k : K_OK;
}

static uint32_t bot_keys(int t) {
  if (g_state != bot_prev) bot_prev = g_state, bot_t = 0, bot_n = 0;
  else bot_t++;
  /* (the robot plants every tick, the keys are pressed one at a time) */
  if (g_state == ST_PLAY && B.mode != MODE_BOWLING) robot();
  if (t % 6) return 0;
  uint32_t k = 0;
  switch (g_state) {
  case ST_TITLE: case ST_HELP: case ST_DAVE: case ST_PAUSE: k = K_OK; break;
  case ST_MENU: k = bot_replay && !board_saved() && bot_n == 0 ? K_DOWN : K_OK; break;
  case ST_LEVELS: k = bot_n < progress.level - bot_level ? K_LEFT : K_OK; break;
  case ST_CHOOSE: bot_done = -2; break;   /* (the levels with a seed choice aren't the player's) */
  case ST_PLAY: k = B.mode == MODE_BOWLING ? bot_bowl(t) : 0; break;
  case ST_WON: k = bot_t > 70 ? K_OK : 0; break;
  case ST_AWARD:
    if (B.level >= bot_level) bot_done = 1;
    else k = bot_t > 50 ? K_OK : 0;
    break;
  case ST_LOST: bot_done = -1; break;
  }
  bot_n++;
  return k;
}

/* what 1-5 showed, after each tick */
static void bot_watch(int t) {
  /* (on every level: the zombies came) */
  if (g_state == ST_PLAY && B.level == bot_level && B.nzombies) bw.came = 1;
  if (B.mode != MODE_BOWLING) return;
  if (g_state == ST_DAVE && !bw.dave) bw.dave = 1, bw.line_dave = bowling_line();
  if (g_state != ST_PLAY) return;
  if (B.tutorial == 4) {
    bw.lesson = 1, bw.line_lesson |= bowling_line(), bw.belt_lesson |= B.nrollers > 0;
    return;
  }
  if (!bw.play_t0) bw.play_t0 = t, bw.line_play = 1;
  bw.line_play &= bowling_line();
  if (B.nzombies && !bw.first_zombie) bw.first_zombie = t - bw.play_t0;
  if (B.npackets > bw.belt) bw.belt = B.npackets;
  for (int i = 0; i < B.npackets; i++) bw.explodenuts |= B.packets[i].kind == P_EXPLODENUT;
  for (int i = 0; i < B.nrollers; i++) {
    if (!B.rollers[i].dir) continue;
    bool seen = false;
    for (int k = 0; k < 64; k++) seen |= bw.bounce_ids[k] == B.rollers[i].id;
    if (!seen) bw.bounce_ids[bw.bounced++ % 64] = B.rollers[i].id;
  }
  for (int i = 0; i < B.nfx; i++) {
    const Fx *f = &B.fx[i];
    if (f->t) continue;
    bw.exploded += f->kind == FX_BOOM, bw.huge += f->kind == FX_TEXT && f->v == 0, bw.final += f->kind == FX_TEXT && f->v == 2;
  }
  for (int i = 0; i < B.nzombies; i++) {
    const Zombie *z = &B.zombies[i];
    bw.flag |= z->kind == Z_FLAG;
    bw.cones |= z->kind == Z_CONE && z->armor == 0 && z->health > 0;   /* (a nut knocks the cone off, no more) */
  }
}

static int sim(int level, int frames, const char *out, bool quiet) {
  progress_load();
  progress.level = (uint8_t)level, progress.slots = 10, progress.items = 0x7F;
  board_start(level);
  robot_seeds(level);
  B.phase = PH_PLAY;
  cam_x = 0;
  show_lawn(-1);
  int t;
  int most = 0, planted = B.nplants;
  hud.col = -10;
  for (t = 0; t < frames && B.phase == PH_PLAY; t++) {
    robot();
    board_tick();
    vis_ticks++;
    if (B.nzombies > most) most = B.nzombies;
    if (getenv("PVZ_DEBUG") && t % 500 == 0) {
      int lob = 0, other = 0;
      for (int i = 0; i < B.nshots; i++) lob += B.shots[i].kind >= S_CABBAGE, other += B.shots[i].kind < S_CABBAGE;
      printf("t %d plants %d shots %d/%d:", t, B.nplants, lob, other);
      for (int i = 0; i < B.nzombies; i++)
        printf(" [k%d r%d x%.0f h%d a%d f%x]", B.zombies[i].kind, B.zombies[i].row, B.zombies[i].x, B.zombies[i].health,
               B.zombies[i].armor, B.zombies[i].flags);
      printf("\n");
    }
    if (B.nplants > planted) {
      /* (the cursor where the robot planted last) */
      hud.col = B.plants[B.nplants - 1].col, hud.row = B.plants[B.nplants - 1].row;
      hud.selected = -1, hud.shovel = false;
    }
    planted = B.nplants;
    if (shot_now(t) || raw_now(t)) {
      gfx_begin();
      draw_lawn(true);
      hud_draw(raw_dir != NULL);
      gfx_end();
      if (raw_now(t)) raw_frame(t);
      if (shot_now(t)) {
        char p[512];
        snprintf(p, sizeof p, "%s/shot_%06d.ppm", out, t);
        host_shot(p);
      }
    }
  }
  const char *how = B.phase == PH_WON ? "won" : B.phase == PH_LOST ? "LOST" : "UNFINISHED";
  if (!quiet || B.phase != PH_WON)
    printf("level %2d-%-2d %-10s ticks %6d waves %2d/%-2d kills %4d planted %3d most zombies %d\n", level_world(level),
           level_stage(level), how, t, B.wave, B.nwaves, B.kills, B.planted, most);
  if (B.has_boss && (!quiet || B.phase != PH_WON)) printf("Dr. Zomboss: %d of %d\n", (int)boss.health, BOSS_HEALTH);
  return B.phase == PH_WON ? 0 : 1;
}

int main(int argc, char **argv) {
  int frames = 2000, simlevel = 0, prog = 0;
  long money = -1;
  bool quiet = false;
  const char *out = "build/shots", *saves = "build/host-saves";
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--keys") && i + 1 < argc) parse_keys(argv[++i]);
    else if (!strcmp(argv[i], "--shots") && i + 1 < argc) parse_shots(argv[++i]);
    else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
    else if (!strcmp(argv[i], "--saves") && i + 1 < argc) saves = argv[++i];
    else if (!strcmp(argv[i], "--sim") && i + 1 < argc) simlevel = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--progress") && i + 1 < argc) prog = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--money") && i + 1 < argc) money = atol(argv[++i]);
    else if (!strcmp(argv[i], "--quiet")) quiet = true;
    else if (!strcmp(argv[i], "--raw") && i + 1 < argc) raw_dir = argv[++i];
    else if (!strcmp(argv[i], "--raw-from") && i + 1 < argc) raw_from = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--raw-to") && i + 1 < argc) raw_to = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--sun-from") && i + 1 < argc) sun_from = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--bot") && i + 1 < argc) bot_level = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--replay")) bot_replay = 1;
    else if (!strcmp(argv[i], "--stall-home")) bot_stall = 1;
  }
  mkdir(out, 0755);
  mkdir(saves, 0755);
  host_save_dir(saves);
  static uint16_t band[SCREEN_W * GFX_BAND];
  gfx_set_band_memory(band);
  pvz_bin = pvz_data;
  B.rng = 20251019;
  if (simlevel) {
    if (simlevel < 0) {
      int bad = 0;
      for (int l = 1; l <= N_LEVELS; l++) B.rng = (uint32_t)(l * 7919), bad += sim(l, frames, out, quiet);
      printf("%d of %d levels not won\n", bad, N_LEVELS);
      return bad ? 1 : 0;
    }
    return sim(simlevel, frames, out, quiet);
  }
  game_init();
  if (prog) {
    progress.level = (uint8_t)prog, progress.slots = 6, progress.keys_seen = 1;
    for (int l = 1; l < prog; l++) {
      int r = level_defs[l - 1].reward;
      if (r == REWARD_SHOVEL) progress.items |= ITEM_SHOVEL;
      if (r == REWARD_ALMANAC) progress.items |= ITEM_ALMANAC;
      if (r == REWARD_KEYS) progress.items |= ITEM_SHOP;
    }
  }
  if (money >= 0) progress.money = (uint32_t)money;
  int t;
  for (t = 0; t < frames && g_running && !bot_done; t++) {
    host_keys = bot_level ? bot_keys(t) : 0;
    for (int i = 0; i < nholds; i++)
      if (t >= holds[i].a && t <= holds[i].b) host_keys |= holds[i].k;
    if (host_keys) host_keys |= K_ANY;
    game_frame(host_keys);
    if (bot_level) bot_watch(t);
    host_time += 10;
    if (t % 3 == 0 || shot_now(t)) game_draw();
    if (raw_now(t)) raw_frame(t);
    if (shot_now(t)) {
      char p[512];
      snprintf(p, sizeof p, "%s/shot_%06d.ppm", out, t);
      host_shot(p);
    }
  }
  printf("state %d level %d phase %d sun %d zombies %d plants %d wave %d/%d progress %d money %u slots %d items %d "
         "running %d\n", g_state, B.level, B.phase, (int)B.sun, B.nzombies, B.nplants, B.wave, B.nwaves, progress.level,
         (unsigned)progress.money, progress.slots, progress.items, g_running);
  if (bot_level)
    printf("bot %s ticks %d dave %d line_dave %d lesson %d line_lesson %d belt_lesson %d line_play %d first_zombie %d "
           "belt %d explodenuts %d rolled %d bounced %d exploded %d cones %d huge %d final %d flag %d stalled %d came %d\n",
           bot_done == 1 ? "won" : bot_done == -1 ? "lost" : bot_done == -2 ? "choose" : "unfinished", t, bw.dave,
           bw.line_dave, bw.lesson, bw.line_lesson, bw.belt_lesson, bw.line_play, bw.first_zombie, bw.belt,
           bw.explodenuts, B.mode == MODE_BOWLING ? B.planted : 0, bw.bounced, bw.exploded, bw.cones, bw.huge, bw.final,
           bw.flag, bw.stalled, bw.came);
  return 0;
}
