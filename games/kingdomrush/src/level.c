/* The battle (Level.as), its waves (Wave.as, WaveSpawn.as).
 *
 * A level's data (tools/pack.py): its paths, flags and each mode's build spots, lives, gold, unlocks and waves.
 * A wave: its interval (u16, the frames from the last wave's end), its path, its two notifications, whether its
 * flag shows a flier, how many spawns; a spawn: the enemy, the one it alternates with (0xFF none) every max_same,
 * how many, the frames between them, the frames before the next spawn, a fixed lane or a random one. */
#include <string.h>
#include "kr.h"
#include "anim.h"
#include "special.h"
#include "res.h"
#include "strings.h"

Battle B;
uint8_t upgrades[6];
uint32_t battle_seed;

static uint32_t rs = 0x9E3779B9u;
void rnd_seed(uint32_t s) { rs = s ? s : 0x9E3779B9u; }
float rnd(void) {
  rs ^= rs << 13, rs ^= rs >> 17, rs ^= rs << 5;
  return (float)(rs >> 8) * (1.0f / 16777216.0f);
}
int rnd_round(int lo, int hi) { return (int)__builtin_roundf(rnd() * (float)(hi - lo)) + lo; }

bool up_on(int line, int n) {
  if (upgrades[line] < n) return false;
  /* heroic and iron allow the upgrades up to a level: maxUpgradeLevel (0: all) */
  return B.max_upgrade == 0 || B.max_upgrade >= n;
}

/* ------------------------------------------------------------------ the level's data */
static const uint8_t *level_data(int i) {
  const uint8_t *s = section(SEC_LEVELS);
  return s + rd32(s + 4 + 4 * i);
}
static const uint8_t *level_part(int i, int part) {
  const uint8_t *l = level_data(i);
  return l + rd32(l + 4 * part);
}

Pt path_node(int path, int lane, int node) {
  const uint8_t *p = B.lane[path][lane] + 4 * node;
  return (Pt){(float)rds16(p) / 16.0f, (float)rds16(p + 2) / 16.0f};
}
int path_len(int path, int lane) { return B.lane_n[path][lane]; }

#define WAVE_HDR 7
#define SPAWN_SZ 11
static const uint8_t *wave_at(int w) {
  const uint8_t *p = B.waves;
  for (int i = 0; i < w; i++) p += WAVE_HDR + SPAWN_SZ * p[6];
  return p;
}
static int wave_interval(int w) { return (int)rd16(wave_at(w)); }

/* ------------------------------------------------------------------ start */
static void count_waves(void) {
  /* waves with an interval of 0 come with the one before: they count as one */
  int n = 0;
  for (int i = 0; i < B.nwaves; i++) {
    n++;
    while (i + 1 < B.nwaves && wave_interval(i + 1) == 0) i++;
  }
  B.max_waves = (uint8_t)n;
}

void battle_start(int level, int mode) {
  memset(&B, 0, sizeof B);
  B.index = (uint8_t)level, B.mode = (uint8_t)mode;
  const uint8_t *p = level_part(level, 0);
  B.npaths = *p++;
  for (int i = 0; i < B.npaths && i < MAX_PATHS; i++) {
    B.nlanes[i] = *p++;
    for (int l = 0; l < B.nlanes[i]; l++) {
      B.lane_n[i][l] = (uint16_t)rd16(p);
      B.lane[i][l] = p + 2;
      p += 2 + 4 * B.lane_n[i][l];
    }
  }
  p = level_part(level, 1);
  B.nflags = *p++;
  for (int i = 0; i < B.nflags && i < MAX_PATHS; i++, p += 8) {
    B.flag[i] = (Pt){(float)rds16(p), (float)rds16(p + 2)};
    B.flag_dir[i] = (Pt){(float)rds16(p + 4), (float)rds16(p + 6)};
  }
  B.ndefend = *p++;
  for (int i = 0; i < B.ndefend; i++, p += 4)
    if (i < 2) B.defend[i] = (Pt){(float)rds16(p), (float)rds16(p + 2)};
  if (B.ndefend > 2) B.ndefend = 2;
  B.unlock_lvl[0] = p[0], B.unlock_lvl[1] = p[1], B.unlock_lvl[2] = p[2], B.unlock_lvl[3] = p[3];
  p = level_part(level, 2 + mode);
  B.nspots = *p++;
  int premade[MAX_SPOTS][2], npre = 0;
  for (int i = 0; i < B.nspots; i++, p += 10) {
    Spot *s = &B.spot[i < MAX_SPOTS ? i : MAX_SPOTS - 1];
    s->x = (int16_t)rds16(p + 2), s->y = (int16_t)rds16(p + 4);
    s->rally = (Pt){(float)rds16(p + 6), (float)rds16(p + 8)};
    s->target = -1, s->elemental = -1;
    for (int k = 0; k < 3; k++) s->soldiers[k] = -1;
    if (p[0] == 0xFF) s->kind = 0xFF;   /* (a level's special building: not here) */
    else if (p[0] >= 1 && p[0] <= 4 && npre < MAX_SPOTS) premade[npre][0] = i, premade[npre][1] = p[0] << 4 | p[1], npre++;
  }
  B.lives = B.init_lives = (int16_t)rd16(p);
  B.cash = (int16_t)rd16(p + 2);
  B.unlock[0] = p[4], B.unlock[1] = p[5], B.unlock[2] = p[6], B.unlock[3] = p[7];
  B.max_upgrade = p[8];
  bool power1 = p[9], power2 = p[10];
  B.nwaves = p[11];
  B.waves = p + 12;
  count_waves();
  /* power1: the reinforcements, power2: the rain of fire, unlocked at the first wave's call (Level.§_-H6§) */
  B.power_reinf = power1, B.power_fire = power2;
  B.fearless = 1;
  B.state = LS_STARTING;
  power_init();
  rnd_seed(0x5EED0000u ^ (uint32_t)(level * 977 + mode * 131) ^ battle_seed);
  for (int k = 0; k < npre; k++) {
    static const uint8_t kinds[5] = {0, TW_ARCHER, TW_BARRACKS, TW_MAGE, TW_ENGINEER};
    spot_make(premade[k][0], kinds[premade[k][1] >> 4], premade[k][1] & 15);
  }
  B.veznan = -1;
  if (level == NLEVELS - 1 && mode == MODE_CAMPAIGN) veznan_spawn();   /* (Level12: he waits in his tower) */
  B.grave = level == 9 ? 1 : level == 11 ? 2 : 0;   /* (Level10, Level12: a graveyard) */
  if (level == 5 && mode != MODE_IRON) {   /* Level6: the imperial guards, each with its group's ground */
    static const int16_t guards[7][4] = {{170, 300, 185, 300}, {208, 300, 185, 300}, {488, 300, 512, 300}, {522, 300, 512, 300},
                                         {320, 160, 345, 160}, {345, 160, 345, 160}, {370, 160, 345, 160}};
    for (int k = 0; k < 7; k++)
      soldier_new(SK_IMPERIAL, -1, (Pt){guards[k][0], guards[k][1]}, (Pt){guards[k][2], guards[k][3]});
  }
}

/* ------------------------------------------------------------------ cash, lives */
void battle_cash(int v) {
  if (v > 0) B.gold_won += v;
  B.cash = (int16_t)(B.cash + v);
}

void battle_lose_lives(int n) {
  B.lives = (int16_t)(B.lives - n);
  if (B.lives <= 0) {
    B.lives = 0;
    B.state = LS_OVER;
  }
}

int battle_stars(void) {
  if (B.mode != MODE_CAMPAIGN) return 3;
  if (B.lives >= 18) return 3;
  if (B.lives > 5) return 2;
  return 1;
}

bool tower_locked(int kind, int level) {
  /* kind: 0 archers, 1 barracks, 2 engineers, 3 mages; level 1 to 5 (4, 5: the specials) */
  int u = B.unlock[kind], ul = B.unlock_lvl[kind];
  if (u >= level || (u == 0 && level != 1 && ul >= level)) return false;
  return true;
}

/* ------------------------------------------------------------------ waves */
static void notify(int note);

static bool spawn_step(ActiveWave *a) {
  const uint8_t *w = wave_at(a->wave), *s = w + WAVE_HDR + SPAWN_SZ * a->spawn;
  int creep = s[0], aux = s[1], max_same = s[2], cant = (int)rd16(s + 3), ival = (int)rd16(s + 5);
  if (a->index_cant > cant) return false;
  if (ival - 1 > a->ival_t) {
    a->ival_t++;
    return true;
  }
  if (aux != 0xFF) {
    if (a->index_swap == max_same) {
      a->cur_creep = (uint8_t)(a->cur_creep == aux ? creep : aux);
      a->index_swap = 0;
    }
  } else
    a->cur_creep = (uint8_t)creep;
  int path = w[2], lane;
  if (path >= B.npaths) path = B.npaths - 1;
  if (!s[9]) lane = (int)__builtin_ceilf(rnd() * (float)B.nlanes[path]) - 1;
  else lane = s[10];
  if (lane < 0) lane = 0;
  if (lane >= B.nlanes[path]) lane = B.nlanes[path] - 1;
  enemy_spawn(a->cur_creep, path, lane, 0);
  /* WaveSpawn: the second notification (a new enemy, or a tip) with the first of its kind; the original puts
   * an icon at the side to open, here it opens itself, the first time only */
  if (w[4] && !a->note_sent) {
    int arg = -1, kind = note_pic(w[4], &arg);
    if (kind == 0 || (kind == 1 && arg == a->cur_creep)) notify(w[4]), a->note_sent = 1;
  }
  a->index_swap++, a->index_cant++, a->ival_t = 0;
  return true;
}

/* Wave.spawnEnemies: false when it is done */
static bool wave_step(ActiveWave *a) {
  const uint8_t *w = wave_at(a->wave);
  int n = w[6];
  if (a->spawn < n) {
    const uint8_t *s = w + WAVE_HDR + SPAWN_SZ * a->spawn;
    if (a->between) {
      if (a->between_t < (int)rd16(s + 7)) {
        a->between_t++;
        return true;
      }
      a->spawn++, a->between = 0;
      a->ival_t = 0, a->index_cant = 1, a->index_swap = 0, a->cur_creep = 0xFF;
      if (a->spawn < n) a->cur_creep = w[WAVE_HDR + SPAWN_SZ * a->spawn];
      return true;
    }
    if (!spawn_step(a)) {
      a->between = 1, a->between_t = 0;
      if (a->spawn + 1 == n) {
        a->spawn++;
        return false;
      }
    }
    return true;
  }
  return false;
}

static void activate(int wi) {
  if (B.nactive >= MAX_ACTIVE_WAVES) return;
  ActiveWave *a = &B.active[B.nactive++];
  memset(a, 0, sizeof *a);
  a->wave = (int16_t)wi, a->index_cant = 1;
  a->cur_creep = wave_at(wi)[WAVE_HDR];
}

static void show_flags(int wi) {
  memset(B.flag_path, 0, sizeof B.flag_path);
  for (int k = wi; k < B.nwaves; k++) {
    if (k > wi && wave_interval(k) != 0) break;
    int p = wave_at(k)[2];
    if (p < MAX_PATHS) B.flag_path[p] = 1 + wave_at(k)[5];   /* 2: a flier comes */
  }
  B.flags_up = 1;
}

static void notify(int note) {
  if (note == NOTE_POWERREINFORCEMENT) power_unlock_reinf();
  if (note == NOTE_POWERFIREBALL) power_unlock_fire();
  if (note == NOTE_NONE || note == B.pending_note) return;
  if (B.pending_note) B.pending_more = (uint8_t)note;
  else B.pending_note = (uint8_t)note;
}

static bool has_enemies(void) {
  if (B.ready_win) {
    if (B.ready_win_t < 30) {
      B.ready_win_t++;
      if (B.ready_win_t % 10 == 0)
        for (int i = 0; i < MAX_ENEMIES; i++)
          if (B.en[i].alive && (!B.en[i].boss || !B.en[i].dead)) B.ready_win = 0;
      return true;
    }
    return false;
  }
  for (int i = 0; i < MAX_ENEMIES; i++)
    if (B.en[i].alive && (!B.en[i].boss || !B.en[i].dead) && !(i == B.veznan && B.en[i].froze == 0)) return true;
  /* Level12: the others gone, Vez'nan comes down himself */
  Enemy *v = EN(B.veznan);
  if (v && !v->dead) {
    if (v->froze == 0) veznan_descend();
    return true;
  }
  B.ready_win = 1, B.ready_win_t = 0;
  return true;
}

/* Level.§_-6x§: the waves, a frame */
static void waves_step(void) {
  if (B.current_wave == 0 && B.waiting_first) return;
  if (B.nactive == 0) {
    if (B.index_waves < B.nwaves) {
      const uint8_t *w = wave_at(B.index_waves);
      if ((int)rd16(w) == B.interval_t) {
        if (B.index_waves != 0) notify(w[3]);
        B.current_wave++;
        activate(B.index_waves);
        B.interval_t = 0;
        B.index_waves++;
        while (B.index_waves < B.nwaves && wave_interval(B.index_waves) == 0) activate(B.index_waves++);
        B.flags_up = 0;
      } else {
        if (B.index_waves == 0 && w[3] && !B.waiting_first) notify(w[3]);
        if ((B.interval_t == 50 && rd16(w) != 0) || B.index_waves == 0) show_flags(B.index_waves);
        if (B.index_waves == 0) B.waiting_first = 1;
        else B.interval_t++;
      }
    } else if (!has_enemies()) {
      B.state = LS_PRE_WIN;
      B.prewin_t = 0;
    }
  }
  for (int i = 0; i < B.nactive;) {
    if (!wave_step(&B.active[i])) {
      B.active[i] = B.active[--B.nactive];
      continue;
    }
    i++;
  }
}

int battle_wave_seconds_left(void) {
  if (!B.flags_up || B.index_waves >= B.nwaves) return -1;
  if (B.waiting_first) return 0;
  return (wave_interval(B.index_waves) - B.interval_t + FPS - 1) / FPS;
}

/* Level.§_-H6§: a wave flag clicked */
bool battle_call_wave(void) {
  if (!B.flags_up || B.index_waves >= B.nwaves || B.state != LS_NORMAL) return false;
  if (B.waiting_first) {
    if (B.power_fire) power_unlock_fire();
    if (B.power_reinf) power_unlock_reinf();
    B.waiting_first = 0;
  }
  if (B.index_waves != 0) {
    int left = wave_interval(B.index_waves) - B.interval_t;
    power_shorten(left);
    int secs = (int)__builtin_roundf((float)left / FPS);
    if (secs >= 1) {
      battle_cash(secs);
      int p = 0;
      while (p < MAX_PATHS - 1 && !B.flag_path[p]) p++;
      fx_value(B.flag[p].x, B.flag[p].y, secs);
    }
    B.fearless = 0;
  }
  B.interval_t = (int16_t)wave_interval(B.index_waves);
  B.flags_up = 0;
  return true;
}

/* ------------------------------------------------------------------ a frame */
void battle_step(void) {
  B.frame++;
  if (B.state == LS_STARTING) {
    if (++B.start_t >= 30) B.state = LS_NORMAL;
    return;
  }
  if (B.state != LS_NORMAL && B.state != LS_PRE_WIN) return;
  if (B.state == LS_NORMAL) waves_step();
  for (int s = 0; s < B.nspots; s++) tower_step(s);
  for (int i = 0; i < MAX_ENEMIES; i++)
    if (B.en[i].alive) enemy_step(&B.en[i]);
  for (int i = 0; i < MAX_SOLDIERS; i++)
    if (B.so[i].alive) soldier_step(&B.so[i]);
  for (int i = 0; i < MAX_BULLETS; i++)
    if (B.bu[i].alive) bullet_step(&B.bu[i]);
  fx_step();
  power_step();
  eggs_step();
  portals_step();
  vportals_step();
  if (B.state == LS_PRE_WIN && ++B.prewin_t > 30) B.state = LS_WIN;
}
