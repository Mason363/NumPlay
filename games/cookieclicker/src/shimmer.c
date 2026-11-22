/* Shimmers (golden and wrath cookies, reindeer) and buffs: the game's Game.shimmerTypes, its golden
 * cookie effects, and Game.gainBuff and its buff types. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "game.h"

#define GC_SIZE 36            /* a golden cookie: 96 pixels in the game */
extern void gain_lumps(int n);
extern bool can_lumps(void);
extern void drop_egg(double failrate);
extern void lose_shimmering_veil(int why);
extern bool force_unslot_god(int god);
extern void use_swap(int32_t t);

static const char *const building_buffs[NB][2] = {
  {"High-five", "Slap to the face"}, {"Congregation", "Senility"}, {"Luxuriant harvest", "Locusts"},
  {"Ore vein", "Cave-in"}, {"Oiled-up", "Jammed machinery"}, {"Juicy profits", "Recession"},
  {"Fervent adoration", "Crisis of faith"}, {"Manabloom", "Magivores"}, {"Delicious lifeforms", "Black holes"},
  {"Breakthrough", "Lab disaster"}, {"Righteous cataclysm", "Dimensional calamity"}, {"Golden ages", "Time jam"},
  {"Extra cycles", "Predictable tragedy"}, {"Solar flare", "Eclipse"}, {"Winning streak", "Dry spell"},
  {"Macrocosm", "Microcosm"}, {"Refactoring", "Antipattern"}, {"Cosmic nursery", "Big crunch"},
  {"Brainstorm", "Brain freeze"}, {"Deduplication", "Clone strike"}};

/* ------------------------------------------------------------------ buffs */
typedef struct {
  const char *name;
  int16_t icon;
  uint8_t mode;      /* 0 set, 1 add, 2 max */
  uint8_t aura;      /* 1 good, 2 bad */
} BuffType;
static const BuffType buff_types[N_BUFF_TYPES] = {
  [BUFF_FRENZY] = {"Frenzy", ICON(10, 14), 1, 1}, [BUFF_ELDER_FRENZY] = {"Elder frenzy", ICON(29, 6), 1, 1},
  [BUFF_CLOT] = {"Clot", ICON(15, 5), 1, 2}, [BUFF_DRAGON_HARVEST] = {"Dragon Harvest", ICON(10, 25), 1, 1},
  [BUFF_EVERYTHING_MUST_GO] = {"Everything must go", ICON(17, 6), 1, 1},
  [BUFF_CURSED_FINGER] = {"Cursed finger", ICON(12, 17), 1, 1},
  [BUFF_CLICK_FRENZY] = {"Click frenzy", ICON(0, 14), 1, 1}, [BUFF_DRAGONFLIGHT] = {"Dragonflight", ICON(0, 25), 1, 1},
  [BUFF_COOKIE_STORM] = {"Cookie storm", ICON(22, 6), 1, 1}, [BUFF_BUILDING_BUFF] = {"", 0, 1, 1},
  [BUFF_BUILDING_DEBUFF] = {"", 0, 1, 2}, [BUFF_SUGAR_BLESSING] = {"Sugar blessing", ICON(29, 16), 0, 0},
  [BUFF_HAGGLER_LUCK] = {"Haggler's luck", ICON(25, 11), 2, 0},
  [BUFF_HAGGLER_MISERY] = {"Haggler's misery", ICON(25, 11), 2, 0},
  [BUFF_PIXIE_LUCK] = {"Crafty pixies", ICON(26, 11), 2, 0}, [BUFF_PIXIE_MISERY] = {"Nasty goblins", ICON(26, 11), 2, 0},
  [BUFF_MAGIC_ADEPT] = {"Magic adept", ICON(29, 11), 2, 0}, [BUFF_MAGIC_INEPT] = {"Magic inept", ICON(29, 11), 2, 0},
  [BUFF_DEVASTATION] = {"Devastation", ICON(23, 18), 2, 1}, [BUFF_SUGAR_FRENZY] = {"Sugar frenzy", ICON(29, 14), 1, 0},
  [BUFF_LOAN1] = {"Loan 1", ICON(1, 33), 2, 0}, [BUFF_LOAN1_INTEREST] = {"Loan 1 (interest)", ICON(1, 33), 2, 0},
  [BUFF_LOAN2] = {"Loan 2", ICON(1, 33), 2, 0}, [BUFF_LOAN2_INTEREST] = {"Loan 2 (interest)", ICON(1, 33), 2, 0},
  [BUFF_LOAN3] = {"Loan 3", ICON(1, 33), 2, 0}, [BUFF_LOAN3_INTEREST] = {"Loan 3 (interest)", ICON(1, 33), 2, 0},
};
const char *buff_name(const Buff *b) {
  if (b->type == BUFF_BUILDING_BUFF) return building_buffs[b->building][0];
  if (b->type == BUFF_BUILDING_DEBUFF) return building_buffs[b->building][1];
  return buff_types[b->type].name;
}
int buff_icon(const Buff *b) {
  if (b->type == BUFF_BUILDING_BUFF) return ICON(building_rec(b->building)->column, 14);
  if (b->type == BUFF_BUILDING_DEBUFF) return ICON(building_rec(b->building)->column, 15);
  return buff_types[b->type].icon;
}
int buff_aura(const Buff *b) { return buff_types[b->type].aura; }

/* what a buff says it does */
void buff_desc(const Buff *b, char *out, int size) {
  char t[64], n[32];
  say_time(b->max_time, -1, t);
  switch (b->type) {
    case BUFF_FRENZY: case BUFF_ELDER_FRENZY: case BUFF_DRAGON_HARVEST: case BUFF_SUGAR_FRENZY:
    case BUFF_LOAN1: case BUFF_LOAN1_INTEREST: case BUFF_LOAN2: case BUFF_LOAN2_INTEREST: case BUFF_LOAN3:
    case BUFF_LOAN3_INTEREST:
      beautify(b->mult_cps, 2, n);
      snprintf(out, size, "Cookie production x%s for %s!", n, t);
      break;
    case BUFF_CLOT: snprintf(out, size, "Cookie production halved for %s!", t); break;
    case BUFF_EVERYTHING_MUST_GO: snprintf(out, size, "All buildings are %d%% cheaper for %s!", (int)b->power, t); break;
    case BUFF_CURSED_FINGER:
      snprintf(out, size, "Cookie production halted for %s,\nbut each click is worth %s of CpS.", t, t);
      break;
    case BUFF_CLICK_FRENZY: case BUFF_DRAGONFLIGHT:
      beautify(b->mult_click, 0, n);
      snprintf(out, size, "Clicking power x%s for %s!", n, t);
      break;
    case BUFF_COOKIE_STORM: snprintf(out, size, "Cookies everywhere!"); break;
    case BUFF_BUILDING_BUFF: case BUFF_BUILDING_DEBUFF: {
      const BuildingRec *r = building_rec(b->building);
      char amt[32], pct[32];
      beautify(S.buildings[b->building].amount, 0, amt);
      double m = b->type == BUFF_BUILDING_BUFF ? b->mult_cps : 1 / b->mult_cps;
      beautify(ceil(m * 100 - 100), 0, pct);
      snprintf(out, size, "Your %s %s are %s your CpS!\nCookie production %s%s%% %sfor %s!", amt, res_text(r->plural),
               b->type == BUFF_BUILDING_BUFF ? "boosting" : "rusting", b->type == BUFF_BUILDING_BUFF ? "+" : "", pct,
               b->type == BUFF_BUILDING_BUFF ? "" : "slower ", t);
      break;
    }
    case BUFF_SUGAR_BLESSING: snprintf(out, size, "You find 10%% more golden cookies for the next %s.", t); break;
    case BUFF_HAGGLER_LUCK: snprintf(out, size, "All upgrades are %d%% cheaper for %s!", (int)b->power, t); break;
    case BUFF_HAGGLER_MISERY: snprintf(out, size, "All upgrades are %d%% pricier for %s!", (int)b->power, t); break;
    case BUFF_PIXIE_LUCK: snprintf(out, size, "All buildings are %d%% cheaper for %s!", (int)b->power, t); break;
    case BUFF_PIXIE_MISERY: snprintf(out, size, "All buildings are %d%% pricier for %s!", (int)b->power, t); break;
    case BUFF_MAGIC_ADEPT: snprintf(out, size, "Spells backfire %d times less for %s.", (int)b->power, t); break;
    case BUFF_MAGIC_INEPT: snprintf(out, size, "Spells backfire %d times more for %s.", (int)b->power, t); break;
    case BUFF_DEVASTATION:
      snprintf(out, size, "Clicking power +%d%% for %s!", (int)floor(b->mult_click * 100 - 100), t);
      break;
    default: out[0] = 0;
  }
}

Buff *gain_buff(int type, float time, double pow_, int building) {
  if ((type == BUFF_DRAGON_HARVEST || type == BUFF_DRAGONFLIGHT) && has(U_DRAGON_FANG)) pow_ = ceil(pow_ * 1.1);
  int32_t frames = (int32_t)(time * FPS);
  Buff *b = NULL;
  for (int i = 0; i < MAX_BUFFS; i++)
    if (S.buffs[i].used && S.buffs[i].type == type &&
        (type != BUFF_BUILDING_BUFF && type != BUFF_BUILDING_DEBUFF ? true : S.buffs[i].building == building))
      b = &S.buffs[i];
  const BuffType *t = &buff_types[type];
  if (b) {
    if (t->mode == 2) b->time = frames > b->time ? frames : b->time;
    else if (t->mode == 1) b->time += frames;
    else b->time = frames;
    b->max_time = b->time;
  } else {
    for (int i = 0; i < MAX_BUFFS && !b; i++)
      if (!S.buffs[i].used) b = &S.buffs[i];
    if (!b) b = &S.buffs[0];
    memset(b, 0, sizeof *b);
    b->used = 1, b->type = (uint8_t)type, b->building = (uint8_t)building;
    b->time = b->max_time = frames;
    b->mult_cps = 1, b->mult_click = 1, b->power = pow_, b->arg1 = (float)pow_;
    switch (type) {
      case BUFF_FRENZY: case BUFF_ELDER_FRENZY: case BUFF_CLOT: case BUFF_DRAGON_HARVEST: case BUFF_SUGAR_FRENZY:
      case BUFF_BUILDING_BUFF: case BUFF_LOAN1: case BUFF_LOAN1_INTEREST: case BUFF_LOAN2: case BUFF_LOAN2_INTEREST:
      case BUFF_LOAN3: case BUFF_LOAN3_INTEREST: b->mult_cps = pow_; break;
      case BUFF_BUILDING_DEBUFF: b->mult_cps = 1 / pow_; break;
      case BUFF_CURSED_FINGER: b->mult_cps = 0; break;
      case BUFF_CLICK_FRENZY: case BUFF_DRAGONFLIGHT: case BUFF_DEVASTATION: b->mult_click = pow_; break;
      default: break;
    }
  }
  G.recalculate = true, G.store_dirty = true;
  return b;
}
void kill_buff(int type) {
  Buff *b = find_buff(type);
  if (b) b->time = 0;
}
extern void buff_died(Buff *b);
void buffs_update(void) {
  for (int i = 0; i < MAX_BUFFS; i++) {
    Buff *b = &S.buffs[i];
    if (!b->used) continue;
    b->time--;
    if (b->time <= 0) {
      buff_died(b);
      b->used = 0;
      G.recalculate = true, G.store_dirty = true;
    }
  }
}

/* ------------------------------------------------------------------ the golden cookie's timer */
static int32_t time_mod(bool wrath, double m) {
  if (has(U_LUCKY_DAY)) m /= 2;
  if (has(U_SERENDIPITY)) m /= 2;
  if (has(U_GOLDEN_GOOSE_EGG)) m *= 0.95;
  if (has(U_HEAVENLY_LUCK)) m *= 0.95;
  if (has(U_GREEN_YEAST_DIGESTIVES)) m *= 0.99;
  m *= 1 - aura_mult(AURA_ARCANE_AURA) * 0.05;
  if (has_buff(BUFF_SUGAR_BLESSING)) m *= 0.9;
  if (S.season == SEASON_EASTER && has(U_STARSPAWN)) m *= 0.98;
  else if (S.season == SEASON_HALLOWEEN && has(U_STARTERROR)) m *= 0.98;
  else if (S.season == SEASON_VALENTINES && has(U_STARLOVE)) m *= 0.98;
  else if (S.season == SEASON_FOOLS && has(U_STARTRADE)) m *= 0.95;
  m *= 1 / eff(wrath ? EFF_WRATH_FREQ : EFF_GOLDEN_FREQ);
  static const float industry[4] = {1, 1.1f, 1.06f, 1.03f}, mother[4] = {1, 1.15f, 1.1f, 1.05f};
  m *= industry[hasgod(GOD_INDUSTRY)] * mother[hasgod(GOD_MOTHER)];
  if (S.season) {
    int god = hasgod(GOD_SEASONS);
    static const float s[4] = {1, 0.97f, 0.98f, 0.99f}, f[4] = {1, 0.955f, 0.97f, 0.985f};
    m *= S.season == SEASON_FOOLS ? f[god] : s[god];
  }
  if (S.golden_chain > 0) m = 0.05;
  if (has(U_GOLD_HOARD)) m = 0.01;
  return (int32_t)ceil(FPS * 60 * m);
}
int32_t golden_min_time(void) { return time_mod(false, 5); }
int32_t golden_max_time(void) { return time_mod(false, 15); }
static int32_t reindeer_mod(double m) {
  if (has(U_REINDEER_BAKING_GROUNDS)) m /= 2;
  if (has(U_STARSNOW)) m *= 0.95;
  static const float s[4] = {1, 0.9f, 0.95f, 0.97f};
  m *= s[hasgod(GOD_SEASONS)];
  if (has(U_REINDEER_SEASON)) m = 0.01;
  return (int32_t)ceil(FPS * 60 * m);
}

void shimmers_reset(void) {
  memset(G.shimmers, 0, sizeof G.shimmers);
  G.nshimmers_golden = G.nshimmers_reindeer = 0;
  S.golden_time = 0, S.golden_spawned = 0, S.golden_chain = 0, S.golden_chain_total = 0, S.golden_last = 0;
  S.golden_min = golden_min_time(), S.golden_max = golden_max_time();
  S.reindeer_time = 0, S.reindeer_spawned = 0, S.reindeer_min = reindeer_mod(3), S.reindeer_max = reindeer_mod(6);
}

/* ------------------------------------------------------------------ a shimmer appears */
Shimmer *shimmer_new(int type, int force, bool no_count) {
  Shimmer *m = NULL;
  for (int i = 0; i < MAX_SHIMMERS && !m; i++)
    if (!G.shimmers[i].used) m = &G.shimmers[i];
  if (!m) return NULL;
  memset(m, 0, sizeof *m);
  m->used = 1, m->type = (uint8_t)type, m->force = (uint8_t)force, m->id = G.shimmers_id++;
  m->pad[1] = (uint8_t)rnd_int(8);   /* (the season's picture: which heart, egg or familiar) */
  m->spawn_lead = 0;
  if (!no_count) {
    if (type == SHIMMER_GOLDEN) G.nshimmers_golden++;
    else G.nshimmers_reindeer++;
    G.recalculate = true;
  } else
    m->pad[0] = 1;   /* (not counted) */
  if (type == SHIMMER_GOLDEN) {
    int wr = 0;
    if (S.elder_wrath == 1 && rnd() < 1.0 / 3) wr = 1;
    if (S.elder_wrath == 2 && rnd() < 2.0 / 3) wr = 1;
    if (S.elder_wrath == 3 || hasgod(GOD_SCORN)) wr = 1;
    if (force == GC_STORM_DROP) wr = 0;
    m->wrath = (uint8_t)wr;
    m->x = (float)(rnd() * (LEFT_W - GC_SIZE - 8) + 4);
    m->y = (float)(rnd() * (SCREEN_H - NEWS_H - GC_SIZE - 8) + NEWS_H + 4);
    double dur = 13;
    if (has(U_LUCKY_DAY)) dur *= 2;
    if (has(U_SERENDIPITY)) dur *= 2;
    if (has(U_DECISIVE_FATE)) dur *= 1.05;
    if (has(U_LUCKY_DIGIT)) dur *= 1.01;
    if (has(U_LUCKY_NUMBER)) dur *= 1.01;
    if (has(U_LUCKY_PAYOUT)) dur *= 1.01;
    dur *= eff(wr ? EFF_WRATH_DUR : EFF_GOLDEN_DUR);
    dur *= pow(0.95, G.nshimmers_golden - 1);
    if (S.golden_chain > 0) dur = 10.0 / S.golden_chain > 2 ? 10.0 / S.golden_chain : 2;
    m->dur = (float)dur;
    m->life = m->max_life = (int32_t)ceil(FPS * dur);
    m->size_mult = 1;
  } else {
    m->x = -40;
    m->y = (float)(rnd() * (SCREEN_H - NEWS_H - 90) + NEWS_H + 40);
    double dur = 4;
    if (has(U_WEIGHTED_SLEIGHS)) dur *= 2;
    m->dur = (float)dur;
    m->life = m->max_life = (int32_t)ceil(FPS * dur);
    m->size_mult = 1;
  }
  return m;
}

static void shimmer_die(Shimmer *m) {
  if (m->spawn_lead) {
    if (m->type == SHIMMER_GOLDEN) {
      S.golden_time = 0, S.golden_spawned = 0;
      S.golden_min = golden_min_time(), S.golden_max = golden_max_time();
    } else {
      S.reindeer_time = 0, S.reindeer_spawned = 0;
      S.reindeer_min = reindeer_mod(3), S.reindeer_max = reindeer_mod(6);
    }
  }
  if (!m->pad[0]) {
    if (m->type == SHIMMER_GOLDEN && G.nshimmers_golden > 0) G.nshimmers_golden--;
    if (m->type == SHIMMER_REINDEER && G.nshimmers_reindeer > 0) G.nshimmers_reindeer--;
    G.recalculate = true;
  }
  m->used = 0;
}

static void golden_missed(Shimmer *m) {
  if (S.golden_chain > 0 && S.golden_chain_total > 0) {
    char t[64], d[96];
    cookies_str(S.golden_chain_total, 0, t);
    snprintf(d, sizeof d, "Cookie chain broken.\nYou made %s.", t);
    popup(d, m->x + GC_SIZE / 2, m->y);
    S.golden_chain = 0, S.golden_chain_total = 0;
  }
  if (m->spawn_lead) S.missed_golden_clicks++;
}

void shimmers_update(void) {
  for (int i = 0; i < MAX_SHIMMERS; i++) {
    Shimmer *m = &G.shimmers[i];
    if (!m->used) continue;
    m->life--;
    if (m->life <= 0) {
      if (m->type == SHIMMER_GOLDEN) golden_missed(m);
      shimmer_die(m);
    }
  }
  if (has_buff(BUFF_COOKIE_STORM) && rnd() < 0.5) {
    Shimmer *m = shimmer_new(SHIMMER_GOLDEN, GC_STORM_DROP, true);
    if (m) {
      m->dur = (float)ceil(rnd() * 4 + 1);
      m->life = m->max_life = (int32_t)ceil(FPS * m->dur);
      m->size_mult = (float)(rnd() * 0.75 + 0.25);
    }
  }
  /* the golden cookie: once its time has come, more likely each frame (the fifth power of how far between
   * the shortest and the longest wait) */
  if (!has(U_GOLDEN_SWITCH_OFF) && !S.golden_spawned) {
    S.golden_time++;
    double k = (double)(S.golden_time - S.golden_min) / (S.golden_max - S.golden_min);
    if (k < 0) k = 0;
    Shimmer *m;
    if (rnd() < pow(k, 5) && (m = shimmer_new(SHIMMER_GOLDEN, GC_NONE, false))) { /* (no room, as in a cookie
                                                                                     storm: it tries again) */
      m->spawn_lead = 1;
      if (has(U_DISTILLED_ESSENCE_OF_REDOUBLED_LUCK) && rnd() < 0.01) shimmer_new(SHIMMER_GOLDEN, GC_NONE, false);
      S.golden_spawned = 1;
    }
  }
  if (S.season == SEASON_CHRISTMAS && !S.reindeer_spawned) {
    S.reindeer_time++;
    double k = (double)(S.reindeer_time - S.reindeer_min) / (S.reindeer_max - S.reindeer_min);
    if (k < 0) k = 0;
    Shimmer *m;
    if (rnd() < pow(k, 5) && (m = shimmer_new(SHIMMER_REINDEER, GC_NONE, false))) m->spawn_lead = 1, S.reindeer_spawned = 1;
  }
}

int shimmer_next(void) {
  int best = -1;
  for (int i = 0; i < MAX_SHIMMERS; i++)
    if (G.shimmers[i].used && (best < 0 || G.shimmers[i].id < G.shimmers[best].id)) best = i;
  return best;
}

/* ------------------------------------------------------------------ a golden cookie is clicked */
static void golden_pop(Shimmer *m) {
  if (m->spawn_lead) {
    S.golden_clicks++, S.golden_clicks_local++;
    static const struct { uint32_t at; uint16_t a; } wins[] = {
      {1, A_GOLDEN_COOKIE}, {7, A_LUCKY_COOKIE}, {27, A_A_STROKE_OF_LUCK}, {77, A_FORTUNE}, {777, A_LEPRECHAUN},
      {7777, A_BLACK_CATS_PAW}, {27777, A_SEVEN_HORSESHOES}};
    for (unsigned i = 0; i < sizeof wins / sizeof wins[0]; i++)
      if (S.golden_clicks >= wins[i].at) win(wins[i].a);
    if (S.golden_clicks >= 7) unlock(U_LUCKY_DAY);
    if (S.golden_clicks >= 27) unlock(U_SERENDIPITY);
    if (S.golden_clicks >= 77) unlock(U_GET_LUCKY);
    if (m->life / (float)FPS > m->dur - 1) win(A_EARLY_BIRD);
    if (m->life < FPS) win(A_FADING_LUCK);
    if (m->wrath) win(A_WRATH_COOKIE);
  }
  if (force_unslot_god(GOD_ASCETICISM)) use_swap(1000000);

  int list[24], n = 0;
#define PUSH(c) (list[n++] = (c))
  if (m->wrath) PUSH(GC_CLOT), PUSH(GC_LUCKY), PUSH(GC_RUIN);
  else PUSH(GC_FRENZY), PUSH(GC_LUCKY);
  if (m->wrath && hasgod(GOD_SCORN)) PUSH(GC_CLOT), PUSH(GC_RUIN), PUSH(GC_CLOT), PUSH(GC_RUIN);
  if (m->wrath && rnd() < 0.3) PUSH(GC_BLOOD_FRENZY), PUSH(GC_CHAIN), PUSH(GC_STORM);
  else if (rnd() < 0.03 && S.cookies_earned >= 100000) PUSH(GC_CHAIN), PUSH(GC_STORM);
  if (rnd() < 0.05 && S.season == SEASON_FOOLS) PUSH(GC_EVERYTHING_MUST_GO);
  if (rnd() < 0.1 && (rnd() < 0.05 || !has_buff(BUFF_DRAGONFLIGHT))) PUSH(GC_CLICK_FRENZY);
  if (m->wrath && rnd() < 0.1) PUSH(GC_CURSED_FINGER);
  if (G.buildings_owned >= 10 && rnd() < 0.25) PUSH(GC_BUILDING_SPECIAL);
  if (can_lumps() && rnd() < 0.0005) PUSH(GC_SUGAR_LUMP);
  if ((!m->wrath && rnd() < 0.15) || rnd() < 0.05) {
    if (rnd() < aura_mult(AURA_REAPER_OF_FIELDS)) PUSH(GC_DRAGON_HARVEST);
    if (rnd() < aura_mult(AURA_DRAGONFLIGHT)) PUSH(GC_DRAGONFLIGHT);
  }
  if (S.golden_last && rnd() < 0.8) {   /* 80%: another than last time */
    bool there = false;
    for (int i = 0; i < n; i++) there |= list[i] == S.golden_last;
    if (there) {
      int k = 0;
      for (int i = 0; i < n; i++)
        if (list[i] != S.golden_last) list[k++] = list[i];
      n = k;
    }
  }
  if (rnd() < 0.0001) PUSH(GC_BLAB);
#undef PUSH
  int choice = n ? list[rnd_int(n)] : GC_FRENZY;
  if (S.golden_chain > 0) choice = GC_CHAIN;
  if (m->force) S.golden_chain = 0, choice = m->force;
  if (choice != GC_CHAIN) S.golden_chain = 0;
  S.golden_last = (uint8_t)choice;

  double dur_mod = 1;
  if (has(U_GET_LUCKY)) dur_mod *= 2;
  if (has(U_LASTING_FORTUNE)) dur_mod *= 1.1;
  if (has(U_LUCKY_DIGIT)) dur_mod *= 1.01;
  if (has(U_LUCKY_NUMBER)) dur_mod *= 1.01;
  if (has(U_GREEN_YEAST_DIGESTIVES)) dur_mod *= 1.01;
  if (has(U_LUCKY_PAYOUT)) dur_mod *= 1.01;
  dur_mod *= 1 + aura_mult(AURA_EPOCH_MANIPULATOR) * 0.05;
  dur_mod *= eff(m->wrath ? EFF_WRATH_EFF_DUR : EFF_GOLDEN_EFF_DUR);
  static const float decadence[4] = {1, 1.07f, 1.05f, 1.02f};
  dur_mod *= decadence[hasgod(GOD_DECADENCE)];
  double mult = 1;
  if (m->wrath) mult *= 1 + aura_mult(AURA_UNHOLY_DOMINION) * 0.1;
  else mult *= 1 + aura_mult(AURA_ANCESTRAL_METAMORPHOSIS) * 0.1;
  if (has(U_GREEN_YEAST_DIGESTIVES)) mult *= 1.01;
  if (has(U_DRAGON_FANG)) mult *= 1.03;
  mult *= eff(m->wrath ? EFF_WRATH_GAIN : EFF_GOLDEN_GAIN);

  char pop[160] = "", t[48];
  Buff *buff = NULL;
  if (choice == GC_BUILDING_SPECIAL) {
    int cand[NB], k = 0;
    for (int b = 0; b < NB; b++)
      if (S.buildings[b].amount >= 10) cand[k++] = b;
    if (!k) choice = GC_FRENZY;
    else {
      int b = cand[rnd_int(k)];
      double p = S.buildings[b].amount / 10.0 + 1;
      float time = (float)ceil(30 * dur_mod);
      buff = gain_buff(m->wrath && rnd() < 0.3 ? BUFF_BUILDING_DEBUFF : BUFF_BUILDING_BUFF, time, p, b);
    }
  }
  switch (choice) {
    case GC_SUGAR_LUMP:
      gain_lumps(1);
      snprintf(pop, sizeof pop, "Sweet!\nFound 1 sugar lump!");
      break;
    case GC_FRENZY: buff = gain_buff(BUFF_FRENZY, (float)ceil(77 * dur_mod), 7, 0); break;
    case GC_DRAGON_HARVEST: buff = gain_buff(BUFF_DRAGON_HARVEST, (float)ceil(60 * dur_mod), 15, 0); break;
    case GC_EVERYTHING_MUST_GO: buff = gain_buff(BUFF_EVERYTHING_MUST_GO, (float)ceil(8 * dur_mod), 5, 0); break;
    case GC_LUCKY: {
      double a = S.cookies * 0.15, b = G.cookies_ps * 60 * 15;
      double moni = mult * (a < b ? a : b) + 13;
      earn(moni);
      cookies_str(moni, 0, t);
      snprintf(pop, sizeof pop, "Lucky!\n+%s!", t);
      break;
    }
    case GC_RUIN: {
      double a = S.cookies * 0.05, b = G.cookies_ps * 60 * 10;
      double moni = (a < b ? a : b) + 13;
      if (moni > S.cookies) moni = S.cookies;
      spend(moni);
      cookies_str(moni, 0, t);
      snprintf(pop, sizeof pop, "Ruin!\nLost %s!", t);
      break;
    }
    case GC_BLOOD_FRENZY: buff = gain_buff(BUFF_ELDER_FRENZY, (float)ceil(6 * dur_mod), 666, 0); break;
    case GC_CLOT: buff = gain_buff(BUFF_CLOT, (float)ceil(66 * dur_mod), 0.5, 0); break;
    case GC_CURSED_FINGER: {
      double time = ceil(10 * dur_mod);
      buff = gain_buff(BUFF_CURSED_FINGER, (float)time, G.cookies_ps * time, 0);
      break;
    }
    case GC_CLICK_FRENZY: buff = gain_buff(BUFF_CLICK_FRENZY, (float)ceil(13 * dur_mod), 777, 0); break;
    case GC_DRAGONFLIGHT:
      buff = gain_buff(BUFF_DRAGONFLIGHT, (float)ceil(10 * dur_mod), 1111, 0);
      if (rnd() < 0.8) kill_buff(BUFF_CLICK_FRENZY);
      break;
    case GC_CHAIN: {
      if (S.golden_chain == 0) S.golden_chain_total = 0;
      S.golden_chain++;
      int digit = m->wrath ? 6 : 7;
      if (S.golden_chain == 1) {
        int extra = (int)ceil(log10(S.cookies > 0 ? S.cookies : 1)) - 10;
        if (extra > 0) S.golden_chain += extra;
      }
      double a = G.cookies_ps * 60 * 60 * 6, b = S.cookies * 0.5;
      double max_payout = (a < b ? a : b) * mult;
      double v = floor(1.0 / 9 * pow(10, S.golden_chain) * digit * mult);
      double moni = v < max_payout ? v : max_payout;
      if (moni < digit) moni = digit;
      double v2 = floor(1.0 / 9 * pow(10, S.golden_chain + 1) * digit * mult);
      double next = v2 < max_payout ? v2 : max_payout;
      if (next < digit) next = digit;
      S.golden_chain_total += moni;
      cookies_str(moni, 0, t);
      if (rnd() < 0.01 || next >= max_payout) {
        char t2[48];
        S.golden_chain = 0;
        cookies_str(S.golden_chain_total, 0, t2);
        snprintf(pop, sizeof pop, "Cookie chain\n+%s!\nCookie chain over. You made %s.", t, t2);
      } else
        snprintf(pop, sizeof pop, "Cookie chain\n+%s!", t);
      earn(moni);
      break;
    }
    case GC_STORM: buff = gain_buff(BUFF_COOKIE_STORM, (float)ceil(7 * dur_mod), 7, 0); break;
    case GC_STORM_DROP: {
      double a = mult * (G.cookies_ps * 60 * floor(rnd() * 7 + 1)), b = floor(rnd() * 7 + 1);
      double moni = a > b ? a : b;
      earn(moni);
      cookies_str(moni, 0, t);
      snprintf(pop, sizeof pop, "+%s!", t);
      break;
    }
    case GC_BLAB: {
      static const char *const blab[] = {
        "Cookie crumbliness x3 for 60 seconds!", "Chocolatiness x7 for 77 seconds!",
        "Dough elasticity halved for 66 seconds!", "Golden cookie shininess doubled for 3 seconds!",
        "World economy halved for 30 seconds!", "Grandma kisses 23% stingier for 45 seconds!", "Thanks for clicking!",
        "Fooled you! This one was just a test.", "Golden cookies clicked +1!",
        "Your click has been registered. Thank you for your cooperation.", "Thanks! That hit the spot!",
        "Thank you. A team has been dispatched.", "They know.",
        "Oops. This was just a chocolate cookie with shiny aluminium foil.", "Eschaton immanentized!",
        "Oh, that tickled!", "Again.", "You've made a grave mistake.", "Chocolate chips reshuffled!",
        "Randomized chance card outcome!", "Mouse acceleration +0.03%!", "Ascension bonuses x5,000 for 0.1 seconds!",
        "Gained 1 extra!", "Sorry, better luck next time!", "I felt that.", "Nice try, but no.",
        "Wait, sorry, I wasn't ready yet.", "Yippee!", "Bones removed.", "Organs added.", "Did you just click that?",
        "Huh? Oh, there was nothing there.", "You saw nothing.", "It seems you hallucinated that golden cookie.",
        "This golden cookie was a complete fabrication.",
        "In theory there's no wrong way to click a golden cookie, but you just did that, somehow.",
        "All cookies multiplied by 999!\nAll cookies divided by 999!", "Why?"};
      snprintf(pop, sizeof pop, "%s", blab[rnd_int(sizeof blab / sizeof blab[0])]);
      break;
    }
    default: break;
  }
  if (!pop[0] && buff) snprintf(pop, sizeof pop, "%s", buff_name(buff));
  if (pop[0]) popup(pop, m->x + GC_SIZE / 2, m->y);
  drop_egg(0.9);
  shimmer_die(m);
}

static void reindeer_pop(Shimmer *m) {
  if (m->spawn_lead) S.reindeer_clicked++;
  double val = G.cookies_ps * 60;
  if (has_buff(BUFF_ELDER_FRENZY)) val *= 0.5;
  if (has_buff(BUFF_FRENZY)) val *= 0.75;
  double moni = val > 25 ? val : 25;
  if (has(U_HO_HO_HO_FLAVORED_FROSTING)) moni *= 2;
  earn(moni);
  if (has_buff(BUFF_ELDER_FRENZY)) win(A_ELDEER);
  int cookie = -1;
  double fail = 0.8;
  if (has_achiev(A_LET_IT_SNOW)) fail *= 0.8;
  if (has(U_STARSNOW)) fail *= 0.95;
  static const float sg[4] = {1, 0.9f, 0.95f, 0.97f};
  fail *= sg[hasgod(GOD_SEASONS)];
  fail = pow(fail, drop_rate_mult());
  if (rnd() > fail) {
    static const uint16_t cookies[7] = {U_CHRISTMAS_TREE_BISCUITS, U_SNOWFLAKE_BISCUITS, U_SNOWMAN_BISCUITS,
                                        U_HOLLY_BISCUITS, U_CANDY_CANE_BISCUITS, U_BELL_BISCUITS,
                                        U_PRESENT_BISCUITS};
    int c = cookies[rnd_int(7)];
    if (!has_unlocked(c) && !has(c)) unlock(c), cookie = c;
  }
  static const char *const names[] = {"Dasher", "Dancer", "Prancer", "Vixen", "Comet", "Cupid", "Donner", "Blitzen",
                                      "Rudolph"};
  char title[48], t[48], d[200];
  snprintf(title, sizeof title, "You found %s!", names[rnd_int(9)]);
  cookies_str(moni, 0, t);
  if (cookie >= 0)
    snprintf(d, sizeof d, "The reindeer gives you %s.\nYou are also rewarded with %s!", t,
             res_text(upgrade_rec(cookie)->name));
  else
    snprintf(d, sizeof d, "The reindeer gives you %s.", t);
  notify(title, d, ICON(12, 9), 6);
  snprintf(d, sizeof d, "+%s!", t);
  popup(d, m->x + 30, m->y);
  shimmer_die(m);
}

void shimmer_pop(Shimmer *m) {
  if (!m->used) return;
  lose_shimmering_veil(1);
  if (m->type == SHIMMER_GOLDEN) golden_pop(m);
  else reindeer_pop(m);
}
