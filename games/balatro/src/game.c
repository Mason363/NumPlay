/* The run: helpers, creating cards, blinds, the shop, booster packs and
 * tags (game.lua, common_events.lua, button_callbacks.lua, tag.lua). */
#include "game.h"
#include <eadk.h>
#include <string.h>
#include "ev.h"
#include "jokers.h"
#include "util.h"

run_t R;

const char *const rank_names[15] = {"", "", "2", "3", "4", "5", "6", "7", "8", "9", "10", T("Jack"), T("Queen"), T("King"), T("Ace")};
const char *const suit_names[4] = {T("Spade"), T("Heart"), T("Club"), T("Diamond")};
const char *const suit_names_plural[4] = {T("Spades"), T("Hearts"), T("Clubs"), T("Diamonds")};

/* ------------------------------------------------------------------ rng */
static uint32_t rotl(uint32_t x, int k) { return (x << k) | (x >> (32 - k)); }

uint32_t rng_u32(void) {
  uint32_t *s = R.rng;
  uint32_t r = rotl(s[1] * 5, 7) * 9, t = s[1] << 9;
  s[2] ^= s[0];
  s[3] ^= s[1];
  s[1] ^= s[2];
  s[0] ^= s[3];
  s[2] ^= t;
  s[3] = rotl(s[3], 11);
  return r;
}
float rng_f(void) { return (rng_u32() >> 8) * (1.0f / 16777216.0f); }
int rng_int(int a, int b) {
  if (b <= a) return a;
  return a + (int)(rng_u32() % (uint32_t)(b - a + 1));
}

void rng_seed_str(const char *s) {
  uint32_t h = 2166136261u;
  for (int i = 0; s[i]; i++) h = (h ^ (uint8_t)s[i]) * 16777619u;
  for (int i = 0; i < 4; i++) {
    h += 0x9E3779B9u;
    uint32_t z = h;
    z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
    z = (z ^ (z >> 13)) * 0xC2B2AE35u;
    R.rng[i] = z ^ (z >> 16);
  }
  if (!(R.rng[0] | R.rng[1] | R.rng[2] | R.rng[3])) R.rng[0] = 1;
}

void rng_new_seed(void) {
  static const char alphabet[] = "123456789ABCDEFGHIJKLMNPQRSTUVWXYZ";
  uint32_t r = eadk_random() ^ (uint32_t)eadk_timing_millis();
  for (int i = 0; i < 8; i++) {
    r = (r * 1664525u + 1013904223u) ^ eadk_random();
    R.seed[i] = alphabet[(r >> 16) % 34];
  }
  R.seed[8] = 0;
}

float probs_normal(void) {
  float p = 1;
  for (int i = 0; i < R.njokers; i++)
    if (R.jokers[i].id == J_OOPS && !(R.jokers[i].flags & JF_DEBUFF)) p *= 2;
  return p;
}
int prob(float odds) { return rng_f() < probs_normal() / odds; }

/* ------------------------------------------------------------------ queries */
int has_joker(int id) {
  int n = 0;
  for (int i = 0; i < R.njokers; i++)
    if (R.jokers[i].id == id && !(R.jokers[i].flags & JF_DEBUFF)) n++;
  return n;
}
int has_voucher(int v) { return (R.vouchers >> v) & 1; }
int card_chip_nominal(int rank) { return rank == 14 ? 11 : rank > 10 ? 10 : rank; }

int card_get_id(int ci) {
  const card_t *c = &R.cards[ci];
  return c->enh == E_STONE ? -1 : c->rank;
}

int card_is_face(int ci, int from_boss) {
  const card_t *c = &R.cards[ci];
  if ((c->flags & CF_DEBUFF) && !from_boss) return 0;
  int id = card_get_id(ci);
  return (id == 11 || id == 12 || id == 13 || has_joker(J_PAREIDOLIA)) ? 1 : 0;
}

static int red(int s) { return s == S_HEARTS || s == S_DIAMONDS; }

int card_is_suit(int ci, int suit, int bypass_debuff, int flush_calc) {
  const card_t *c = &R.cards[ci];
  if (flush_calc) {
    if (c->enh == E_STONE) return 0;
    if (c->enh == E_WILD && !(c->flags & CF_DEBUFF)) return 1;
    if (has_joker(J_SMEARED) && red(c->suit) == red(suit)) return 1;
    return c->suit == suit;
  }
  if ((c->flags & CF_DEBUFF) && !bypass_debuff) return 0;
  if (c->enh == E_STONE) return 0;
  if (c->enh == E_WILD) return 1;
  if (has_joker(J_SMEARED) && red(c->suit) == red(suit)) return 1;
  return c->suit == suit;
}

int hand_size(void) {
  int n = 8 + R.hand_size_mod + R.temp_handsize;
  n += has_voucher(V_PAINT_BRUSH) + has_voucher(V_PALETTE);
  for (int i = 0; i < R.njokers; i++) {
    joker_t *j = &R.jokers[i];
    if (j->flags & JF_DEBUFF) continue;
    switch (j->id) {
      case J_JUGGLER: n += 1; break;
      case J_TROUBADOUR: n += 2; break;
      case J_TURTLE_BEAN: n += j->a; break;
      case J_MERRY_ANDY: n -= 1; break;
      case J_STUNTMAN: n -= 2; break;
    }
  }
  if (R.phase == PH_ROUND && R.blind == BL_MANACLE && !R.blind_disabled) n -= 1;
  return n < 0 ? 0 : n > MAXHAND ? MAXHAND : n;
}

/* round_resets.hands / discards: the base plus the jokers that change them */
int round_hands(void) {
  int n = R.base_hands;
  for (int i = 0; i < R.njokers; i++)
    if (R.jokers[i].id == J_TROUBADOUR && !(R.jokers[i].flags & JF_DEBUFF)) n -= 1;
  return n < 1 ? 1 : n;
}
int round_discards(void) {
  int n = R.base_discards;
  for (int i = 0; i < R.njokers; i++) {
    if (R.jokers[i].flags & JF_DEBUFF) continue;
    if (R.jokers[i].id == J_DRUNKARD) n += 1;
    if (R.jokers[i].id == J_MERRY_ANDY) n += 3;
  }
  return n < 0 ? 0 : n;
}

int joker_slots(void) {
  int n = R.joker_slots;
  for (int i = 0; i < R.njokers; i++) n += R.jokers[i].ed == ED_NEG;
  return n;
}
int cons_slots(void) {
  int n = R.cons_slots;
  for (int i = 0; i < R.ncons; i++) n += R.cons[i].ed == ED_NEG;
  return n;
}

static int edition_extra(int ed) {
  static const uint8_t e[5] = {0, 2, 3, 5, 5};
  return e[ed & 7];
}
static int cost_of(int base, int ed) {
  int c = (base + edition_extra(ed)) * 2 + 1; /* (base + extra + 0.5) * 2 */
  c = c * (100 - R.discount) / 200;
  return c < 1 ? 1 : c;
}
static int cons_base_cost(int id) { return id >= C_FAMILIAR ? 4 : 3; }
static int is_planet(int id) { return id >= C_MERCURY && id <= C_ERIS; }

int joker_sell_value(const joker_t *j) {
  int c = cost_of(joker_info[j->id].cost, j->ed) / 2;
  return (c < 1 ? 1 : c) + j->extra_value;
}
int cons_sell_value(const cons_t *k) {
  int cost = cost_of(cons_base_cost(k->id), k->ed);
  if (is_planet(k->id) && has_joker(J_ASTRONOMER)) cost = 0;
  int c = cost / 2;
  return (c < 1 ? 1 : c) + k->extra_value;
}

int item_cost(const sitem_t *it) {
  if (it->flags & 1) return 0;
  switch (it->kind) {
    case IT_JOKER: return cost_of(joker_info[it->id].cost, it->ed);
    case IT_CONS:
      if (is_planet(it->id) && has_joker(J_ASTRONOMER)) return 0;
      return cost_of(cons_base_cost(it->id), it->ed);
    case IT_CARD: return cost_of(1, it->card.ed);
    case IT_VOUCHER: return cost_of(10, 0);
    case IT_PACK:
      if (it->id / 3 == PK_CELESTIAL && has_joker(J_ASTRONOMER)) return 0;
      return cost_of(4 + 2 * (it->id % 3), 0);
  }
  return 0;
}

static const uint8_t hand_base[NHANDS][4] = {
    /* s_chips/5, s_mult, l_chips/5, l_mult */
    {32, 16, 10, 3}, {28, 14, 8, 4}, {24, 12, 7, 3}, {20, 8, 8, 4}, {12, 7, 6, 3}, {8, 4, 5, 2},
    {7, 4, 3, 2},    {6, 4, 6, 3},   {6, 3, 4, 2},   {4, 2, 4, 1},  {2, 2, 3, 1},  {1, 1, 2, 1},
};
double hand_chips(int h) {
  double v = hand_base[h][0] * 5.0 + hand_base[h][2] * 5.0 * (R.hands[h].level - 1);
  return v < 0 ? 0 : v;
}
double hand_mult(int h) {
  double v = hand_base[h][1] + (double)hand_base[h][3] * (R.hands[h].level - 1);
  return v < 1 ? 1 : v;
}

void level_up(int h, int amount, int src) {
  int old = R.hands[h].level;
  int lv = R.hands[h].level + amount;
  R.hands[h].level = (int16_t)(lv < 0 ? 0 : lv > 32000 ? 32000 : lv);
  ev_t *v = ev_push(EV_LEVELUP);
  if (v) {
    v->a = (uint16_t)h;
    v->d = R.hands[h].level;
    v->b = (uint8_t)(src >= 0 && src < R.njokers ? 1 : 0);
    v->c = (uint8_t)(src >= 0 && src < R.njokers ? src : 0);
    v->p.i = old;
  }
}

double blind_amount(int ante) {
  static const int amounts[8] = {300, 800, 2000, 5000, 11000, 20000, 35000, 50000};
  if (ante < 1) return 100;
  if (ante <= 8) return amounts[ante - 1];
  /* endless: a*(b+(k*c)^d)^c, then rounded down to 2 significant digits */
  double a = 50000, b = 1.6, k = 0.75, c = ante - 8, d = 1 + 0.2 * (ante - 8);
  /* pow with doubles, by exp/log done with float precision where fine */
  double kc = k * c, base;
  {
    /* (k*c)^d */
    double r = 1, x = kc;
    int di = (int)d;
    double frac = d - di;
    for (int i = 0; i < di; i++) r *= x;
    if (frac > 0) {
      /* x^frac = exp(frac*ln x) */
      double lx = 0, y = x;
      while (y > 2) y /= 2.718281828459045, lx += 1;
      while (y < 0.5) y *= 2.718281828459045, lx -= 1;
      double t = (y - 1) / (y + 1), t2 = t * t, s = 0, p = t;
      for (int i = 1; i < 30; i += 2) s += p / i, p *= t2;
      lx += 2 * s;
      double ex = frac * lx, e = 1, term = 1;
      for (int i = 1; i < 30; i++) term *= ex / i, e += term;
      r *= e;
    }
    base = b + r;
  }
  double amt = a;
  for (int i = 0; i < (int)c; i++) amt *= base;
  if (amt > 1e300) return amt;
  amt = dfloor(amt);
  /* amount - amount % 10^(floor(log10(amount)) - 1) */
  double p = 1;
  while (p * 10 <= amt) p *= 10;
  p /= 10;
  if (p >= 1) amt = amt - (amt - dfloor(amt / p) * p);
  return amt;
}

const char *blind_name(int b) { return text_get(TXT_BLIND + b, 0); }

/* ------------------------------------------------------------------ cards */
card_t random_card(void) {
  card_t c = {0};
  c.rank = (uint8_t)rng_int(2, 14);
  c.suit = (uint8_t)rng_int(0, 3);
  return c;
}

int random_enhancement(int no_stone) {
  for (;;) {
    int e = rng_int(E_BONUS, E_LUCKY);
    if (!(no_stone && e == E_STONE)) return e;
  }
}

void blind_debuff_card(int ci);

/* no free slot left for a new playing card */
static int deck_full(void) {
  if (R.ncards_alloc < MAXCARDS) return 0;
  for (int i = 0; i < R.ncards_alloc; i++)
    if (!(R.cards[i].flags & CF_USED)) return 0;
  return 1;
}

int add_card_to_deck(card_t c, int area) {
  int ci = -1;
  for (int i = 0; i < R.ncards_alloc; i++)
    if (!(R.cards[i].flags & CF_USED)) {
      ci = i;
      break;
    }
  if (ci < 0) {
    if (R.ncards_alloc >= MAXCARDS) return -1;
    ci = R.ncards_alloc++;
  }
  c.flags = (uint8_t)((c.flags & CF_DOWN) | CF_USED);
  R.cards[ci] = c;
  blind_debuff_card(ci);
  if (area == A_HAND && R.nhand < MAXHAND) {
    R.sel[R.nhand] = 0;
    R.hand[R.nhand++] = (uint8_t)ci;
  } else {
    /* into the deck at a random place */
    int p = rng_int(0, R.ndeck);
    for (int i = R.ndeck; i > p; i--) R.deck[i] = R.deck[i - 1];
    R.deck[p] = (uint8_t)ci;
    R.ndeck++;
    area = A_DECK;
  }
  ev_card_set(ci);
  ev_card(EV_CARD_MOVE, ci, area, 0);
  return ci;
}

static void remove_from(uint8_t *arr, uint8_t *n, int ci) {
  for (int i = 0; i < *n; i++)
    if (arr[i] == ci) {
      for (int k = i; k < *n - 1; k++) arr[k] = arr[k + 1];
      (*n)--;
      return;
    }
}

void destroy_card(int ci) {
  remove_from(R.deck, &R.ndeck, ci);
  remove_from(R.discard, &R.ndiscard, ci);
  remove_from(R.play, &R.nplay, ci);
  for (int i = 0; i < R.nhand; i++)
    if (R.hand[i] == ci) {
      for (int k = i; k < R.nhand - 1; k++) R.hand[k] = R.hand[k + 1], R.sel[k] = R.sel[k + 1];
      R.nhand--;
      break;
    }
  R.cards[ci].flags = 0;
}

/* ------------------------------------------------------------------ pools */
static int in_use_joker(int id) {
  /* used_jokers: owned, or shown in the shop or a pack */
  for (int i = 0; i < R.njokers; i++)
    if (R.jokers[i].id == id) return 1;
  for (int i = 0; i < R.nshop; i++)
    if (R.shop[i].kind == IT_JOKER && R.shop[i].id == id) return 1;
  if (R.phase == PH_PACK)
    for (int i = 0; i < R.npack; i++)
      if (R.pack[i].kind == IT_JOKER && R.pack[i].id == id) return 1;
  return 0;
}
static int in_use_cons(int id) {
  for (int i = 0; i < R.ncons; i++)
    if (R.cons[i].id == id) return 1;
  for (int i = 0; i < R.nshop; i++)
    if (R.shop[i].kind == IT_CONS && R.shop[i].id == id) return 1;
  if (R.phase == PH_PACK)
    for (int i = 0; i < R.npack; i++)
      if (R.pack[i].kind == IT_CONS && R.pack[i].id == id) return 1;
  return 0;
}

static int deck_has_enh(int enh) {
  for (int i = 0; i < R.ncards_alloc; i++)
    if ((R.cards[i].flags & CF_USED) && R.cards[i].enh == enh) return 1;
  return 0;
}

int create_joker_id(int rarity, int legendary) {
  if (legendary) rarity = 4;
  if (rarity < 0) {
    float r = rng_f();
    rarity = r > 0.95f ? 3 : r > 0.7f ? 2 : 1;
  }
  int pool[150], n = 0, showman = has_joker(J_RING_MASTER);
  for (int id = 0; id < N_JOKER; id++) {
    if (joker_info[id].rarity != rarity) continue;
    if (!showman && in_use_joker(id)) continue;
    if (id == J_GROS_MICHEL && R.gros_michel_extinct) continue;
    if (id == J_CAVENDISH && !R.gros_michel_extinct) continue;
    if (id == J_STEEL_JOKER && !deck_has_enh(E_STEEL)) continue;
    if (id == J_STONE && !deck_has_enh(E_STONE)) continue;
    if (id == J_LUCKY_CAT && !deck_has_enh(E_LUCKY)) continue;
    if (id == J_TICKET && !deck_has_enh(E_GOLD)) continue;
    if (id == J_GLASS && !deck_has_enh(E_GLASS)) continue;
    pool[n++] = id;
  }
  if (!n) return J_JOKER;
  return pool[rng_int(0, n - 1)];
}

int create_cons_id(int set) {
  int a = set == 0 ? C_FOOL : set == 1 ? C_MERCURY : C_FAMILIAR, b = set == 0 ? C_WORLD : set == 1 ? C_ERIS : C_CRYPTID;
  int pool[24], n = 0, showman = has_joker(J_RING_MASTER);
  static const uint8_t softlock_hand[3] = {H_FIVE_KIND, H_FLUSH_HOUSE, H_FLUSH_FIVE};
  for (int id = a; id <= b; id++) {
    if (!showman && in_use_cons(id)) continue;
    if (id >= C_PLANET_X && id <= C_ERIS && R.hands[softlock_hand[id - C_PLANET_X]].played == 0) continue;
    pool[n++] = id;
  }
  if (!n) return set == 0 ? C_STRENGTH : set == 1 ? C_PLUTO : C_INCANTATION;
  return pool[rng_int(0, n - 1)];
}

/* soulable creation for packs: The Soul / Black Hole replace the card rarely */
static int create_cons_soulable(int set) {
  int showman = has_joker(J_RING_MASTER);
  if ((set == 0 || set == 2) && (showman || !in_use_cons(C_SOUL)) && rng_f() > 0.997f) return C_SOUL;
  if ((set == 1 || set == 2) && (showman || !in_use_cons(C_BLACK_HOLE)) && rng_f() > 0.997f) return C_BLACK_HOLE;
  return create_cons_id(set);
}

int poll_edition(float mod, int no_neg, int guaranteed) {
  float p = rng_f();
  if (guaranteed) {
    if (p > 1 - 0.003f * 25 && !no_neg) return ED_NEG;
    if (p > 1 - 0.006f * 25) return ED_POLY;
    if (p > 1 - 0.02f * 25) return ED_HOLO;
    if (p > 1 - 0.04f * 25) return ED_FOIL;
    return 0;
  }
  float er = R.edition_rate;
  if (p > 1 - 0.003f * mod && !no_neg) return ED_NEG;
  if (p > 1 - 0.006f * er * mod) return ED_POLY;
  if (p > 1 - 0.02f * er * mod) return ED_HOLO;
  if (p > 1 - 0.04f * er * mod) return ED_FOIL;
  return 0;
}

int joker_new_uid(void) { return ++R.next_uid ? R.next_uid : ++R.next_uid; }

void joker_on_added(joker_t *j) {
  (void)j;
  if (j->id == J_CHAOS) R.free_rerolls++;
  if (j->id == J_DRUNKARD) R.discards_left += 1;
  if (j->id == J_MERRY_ANDY) R.discards_left += 3;
  if (j->id == J_CHICOT && R.phase == PH_ROUND && R.blind >= BL_OX && !R.blind_disabled) R.blind_disabled = 2;
}

void joker_add(int id, int ed, int silent) {
  (void)silent;
  if (R.njokers >= MAXJ) return;
  joker_t *j = &R.jokers[R.njokers];
  memset(j, 0, sizeof *j);
  j->id = (uint8_t)id;
  j->uid = (uint16_t)joker_new_uid();
  joker_init(j);
  j->ed = (uint8_t)(ed < 0 ? poll_edition(1, 0, 0) : ed);
  R.njokers++;
  joker_on_added(j);
  ev_t *v = ev_push(EV_JOKER_ADD);
  if (v) v->a = j->uid, v->b = j->id, v->c = j->ed, v->d = (int16_t)(R.njokers - 1);
}

void joker_remove(int idx, int sold) {
  if (idx < 0 || idx >= R.njokers) return;
  joker_t *j = &R.jokers[idx];
  ev_t *v = ev_push(EV_JOKER_REMOVE);
  if (v) v->a = j->uid, v->b = (uint8_t)sold;
  if (!(j->flags & JF_DEBUFF)) {
    if (j->id == J_CHAOS && R.free_rerolls > 0) R.free_rerolls--;
    if (j->id == J_DRUNKARD) R.discards_left = R.discards_left > 1 ? R.discards_left - 1 : 0;
    if (j->id == J_MERRY_ANDY) R.discards_left = R.discards_left > 3 ? R.discards_left - 3 : 0;
  }
  for (int i = idx; i < R.njokers - 1; i++) R.jokers[i] = R.jokers[i + 1];
  R.njokers--;
}

void add_cons(int id, int ed) {
  if (R.ncons >= MAXCONS) return;
  cons_t *c = &R.cons[R.ncons++];
  c->id = (uint8_t)id;
  c->ed = (uint8_t)ed;
  c->uid = (uint16_t)joker_new_uid();
  c->extra_value = 0;
  ev_t *v = ev_push(EV_CONS_ADD);
  if (v) v->a = c->uid, v->b = c->id, v->c = c->ed;
}

static void remove_cons(int idx) {
  ev_t *v = ev_push(EV_CONS_REMOVE);
  if (v) v->a = R.cons[idx].uid;
  for (int i = idx; i < R.ncons - 1; i++) R.cons[i] = R.cons[i + 1];
  R.ncons--;
}

/* ------------------------------------------------------------------ tags */
int tag_for_skip(void) {
  int pool[N_TAG], n = 0;
  for (int t = 0; t < N_TAG; t++)
    if (tag_info[t].min_ante <= R.ante) pool[n++] = t;
  return pool[rng_int(0, n - 1)];
}

static int random_visible_hand(void) {
  int pick[NHANDS], n = 0;
  for (int h = 0; h < NHANDS; h++)
    if (R.hands[h].visible) pick[n++] = h;
  return pick[rng_int(0, n - 1)];
}

static void add_tag_orb(int t, int orb) {
  /* Double Tag copies the next tag (not another Double Tag) */
  int copies = 0;
  for (int i = 0; i < R.ntags; i++)
    if (R.tags[i] == TAG_DOUBLE && t != TAG_DOUBLE) {
      for (int k = i; k < R.ntags - 1; k++) R.tags[k] = R.tags[k + 1], R.tag_orbital[k] = R.tag_orbital[k + 1];
      R.ntags--;
      i--;
      copies++;
    }
  if (R.ntags < MAXTAGS) {
    R.tags[R.ntags] = (uint8_t)t;
    R.tag_orbital[R.ntags++] = (int8_t)orb;
  }
  for (int i = 0; i < copies; i++) {
    ev_popup(TG_CENTER, 0, T("Double Tag"), PC_ATTN);
    add_tag_orb(t, orb);
  }
}

void add_tag(int t) { add_tag_orb(t, t == TAG_ORBITAL ? random_visible_hand() : -1); }

static void tag_remove(int i) {
  for (int k = i; k < R.ntags - 1; k++) R.tags[k] = R.tags[k + 1], R.tag_orbital[k] = R.tag_orbital[k + 1];
  R.ntags--;
}

/* 'immediate' tags */
static void tags_immediate(void) {
  for (int i = 0; i < R.ntags; i++) {
    int t = R.tags[i], done = 1;
    switch (t) {
      case TAG_TOP_UP:
        for (int k = 0; k < 2; k++)
          if (R.njokers < joker_slots()) joker_add(create_joker_id(1, 0), -1, 0);
        break;
      case TAG_SKIP: R.money += R.skips * 5; break;
      case TAG_GARBAGE: R.money += R.unused_discards; break;
      case TAG_HANDY: R.money += R.hands_played; break;
      case TAG_ECONOMY: R.money += R.money > 40 ? 40 : R.money > 0 ? R.money : 0; break;
      case TAG_ORBITAL: level_up(R.tag_orbital[i] >= 0 ? R.tag_orbital[i] : random_visible_hand(), 3, -1); break;
      default: done = 0;
    }
    if (done) {
      ev_money();
      tag_remove(i);
      i--;
    }
  }
}

/* 'new_blind_choice' tags: returns 1 if one applied (it may open a pack) */
static int tags_new_blind_choice(void) {
  for (int i = 0; i < R.ntags; i++) {
    int t = R.tags[i];
    int pk = -1;
    switch (t) {
      case TAG_CHARM: pk = PK_ARCANA * 3 + 2; break;
      case TAG_METEOR: pk = PK_CELESTIAL * 3 + 2; break;
      case TAG_ETHEREAL: pk = PK_SPECTRAL * 3; break;
      case TAG_STANDARD: pk = PK_STANDARD * 3 + 2; break;
      case TAG_BUFFOON: pk = PK_BUFFOON * 3 + 2; break;
      case TAG_BOSS:
        tag_remove(i);
        R.boss_rerolled = 1;
        {
          int get_new_boss(void);
          R.boss = (uint8_t)get_new_boss();
        }
        return 1;
      default: continue;
    }
    tag_remove(i);
    pack_open(pk / 3, pk % 3, 1);
    return 1;
  }
  return 0;
}

/* ------------------------------------------------------------------ blinds */
int get_new_boss(void) {
  int eligible[N_BLIND], n = 0, min_use = 100;
  int ante = R.ante < 1 ? 1 : R.ante;
  for (int b = BL_OX; b < N_BLIND; b++) {
    const blind_info_t *bi = &blind_info[b];
    int ok;
    if (!bi->showdown) ok = bi->min <= ante && (ante % WIN_ANTE != 0 || R.ante < 2);
    else ok = R.ante % WIN_ANTE == 0 && R.ante >= 2;
    if (ok && R.bosses_used[b] < min_use) min_use = R.bosses_used[b];
    eligible[b] = ok;
  }
  for (int b = BL_OX; b < N_BLIND; b++)
    if (eligible[b] && R.bosses_used[b] == min_use) eligible[n++] = b;
  int boss = n ? eligible[rng_int(0, n - 1)] : BL_HOOK;
  R.bosses_used[boss]++;
  return boss;
}

static void reset_round_cards(void) {
  /* reset_idol_card, reset_mail_rank, reset_ancient_card, reset_castle_card */
  int valid[MAXCARDS], n = 0;
  for (int i = 0; i < R.ncards_alloc; i++)
    if ((R.cards[i].flags & CF_USED) && R.cards[i].enh != E_STONE) valid[n++] = i;
  R.idol_rank = 14, R.idol_suit = S_SPADES, R.mail_rank = 14, R.castle_suit = S_SPADES;
  if (n) {
    int c = valid[rng_int(0, n - 1)];
    R.idol_rank = R.cards[c].rank, R.idol_suit = R.cards[c].suit;
    R.mail_rank = R.cards[valid[rng_int(0, n - 1)]].rank;
    R.castle_suit = R.cards[valid[rng_int(0, n - 1)]].suit;
  }
  int s;
  do s = rng_int(0, 3); while (s == R.ancient_suit);
  R.ancient_suit = (uint8_t)s;
}

static void next_voucher(void) {
  int pool[32], n = 0;
  for (int v = 0; v < 32; v++) {
    if (has_voucher(v)) continue;
    if (v >= 16 && !has_voucher(v - 16)) continue;
    pool[n++] = v;
  }
  R.shop_voucher = n ? (uint8_t)pool[rng_int(0, n - 1)] : (uint8_t)V_BLANK;
}

void blind_debuff_card(int ci) {
  card_t *c = &R.cards[ci];
  c->flags &= (uint8_t)~CF_DEBUFF;
  if (R.phase != PH_ROUND || R.blind_disabled) return;
  int s = -1;
  switch (R.blind) {
    case BL_CLUB: s = S_CLUBS; break;
    case BL_GOAD: s = S_SPADES; break;
    case BL_WINDOW: s = S_DIAMONDS; break;
    case BL_HEAD: s = S_HEARTS; break;
    case BL_PLANT:
      if (card_is_face(ci, 1)) c->flags |= CF_DEBUFF;
      return;
    case BL_PILLAR:
      if (c->flags & CF_PLAYED_ANTE) c->flags |= CF_DEBUFF;
      return;
    case BL_FINAL_LEAF: c->flags |= CF_DEBUFF; return;
  }
  if (s >= 0 && card_is_suit(ci, s, 1, 0)) c->flags |= CF_DEBUFF;
}

void blind_debuff_all(void) {
  for (int i = 0; i < R.ncards_alloc; i++)
    if (R.cards[i].flags & CF_USED) blind_debuff_card(i);
  for (int i = 0; i < R.ncards_alloc; i++)
    if (R.cards[i].flags & CF_USED) ev_card_set(i);
}

/* Blind:disable */
static void blind_disable(void) {
  R.blind_disabled = 1;
  for (int i = 0; i < R.njokers; i++)
    if (R.jokers[i].flags & JF_DOWN) {
      R.jokers[i].flags &= (uint8_t)~JF_DOWN;
      ev_t *v = ev_push(EV_JOKER_SET);
      if (v) v->a = R.jokers[i].uid, v->b = R.jokers[i].ed, v->c = R.jokers[i].flags;
    }
  if (R.blind == BL_WATER) R.discards_left += R.water_sub;
  if (R.blind == BL_WHEEL || R.blind == BL_HOUSE || R.blind == BL_MARK || R.blind == BL_FISH)
    for (int i = 0; i < R.nhand; i++) R.cards[R.hand[i]].flags &= (uint8_t)~CF_DOWN;
  if (R.blind == BL_NEEDLE) R.hands_left += R.needle_sub;
  if (R.blind == BL_WALL) R.blind_chips /= 2;
  if (R.blind == BL_FINAL_BELL)
    for (int i = 0; i < R.ncards_alloc; i++) R.cards[i].flags &= (uint8_t)~CF_FORCED;
  if (R.blind == BL_FINAL_VESSEL) R.blind_chips /= 3;
  if (R.blind == BL_FINAL_HEART)
    for (int i = 0; i < R.njokers; i++) R.jokers[i].flags &= (uint8_t)~JF_DEBUFF;
  blind_debuff_all();
  ev_push(EV_BLIND_FLASH);
  ev_sync();
  if (R.blind == BL_MANACLE) {
    extern void draw_phase(void);
    /* one more card fits in the hand */
    if (R.ndeck && R.nhand < hand_size()) {
      int ci = R.deck[--R.ndeck];
      R.sel[R.nhand] = 0;
      R.hand[R.nhand++] = (uint8_t)ci;
      ev_card(EV_CARD_MOVE, ci, A_HAND, 0);
      sort_hand(R.sort_suit);
    }
  }
}

void apply_disable_request(void) {
  if (R.blind_disabled == 2) blind_disable();
}

static void set_blind(int b) {
  R.blind = (uint8_t)b;
  R.blind_disabled = 0, R.blind_triggered = 0, R.blind_prepped = 1;
  R.blind_hands = 0, R.mouth_hand = -1, R.water_sub = 0, R.needle_sub = 0;
  R.blind_chips = blind_amount(R.ante) * blind_info[b].mult2 / 2.0;
  R.chips = 0;
  if (b == BL_FISH) R.blind_prepped = 0;
  if (b == BL_WATER) {
    R.water_sub = (int8_t)R.discards_left;
    R.discards_left = 0;
  }
  if (b == BL_NEEDLE) {
    R.needle_sub = (int8_t)(round_hands() - 1);
    R.hands_left -= R.needle_sub;
  }
  if (b == BL_FINAL_ACORN && R.njokers) {
    for (int i = 0; i < R.njokers; i++) R.jokers[i].flags |= JF_DOWN;
    for (int i = R.njokers - 1; i > 0; i--) {
      int k = rng_int(0, i);
      joker_t t = R.jokers[i];
      R.jokers[i] = R.jokers[k], R.jokers[k] = t;
    }
    ev_sync();
  }
  blind_debuff_all();
}

void run_start_blind_select(void) {
  R.phase = PH_BLIND_SELECT;
  tags_new_blind_choice();
}

static void shuffle_deck(void) {
  for (int i = R.ndeck - 1; i > 0; i--) {
    int k = rng_int(0, i);
    uint8_t t = R.deck[i];
    R.deck[i] = R.deck[k], R.deck[k] = t;
  }
}

void draw_phase(void);

void blind_select(void) {
  ev_reset();
  int on = R.blind_on;
  R.blind_state[on] = BS_CURRENT;
  R.round++;
  R.phase = PH_ROUND;
  /* new_round */
  R.discards_left = (int16_t)round_discards();
  R.hands_left = (int16_t)round_hands();
  R.hands_played_round = 0, R.discards_used_round = 0, R.reroll_inc = 0;
  for (int h = 0; h < NHANDS; h++) R.hands[h].played_round = 0;
  for (int i = 0; i < R.ncards_alloc; i++) R.cards[i].flags &= (uint8_t)~(CF_DOWN | CF_FORCED);
  R.free_rerolls = (int16_t)has_joker(J_CHAOS);
  set_blind(on == 0 ? BL_SMALL : on == 1 ? BL_BIG : R.boss);
  /* jokers when the blind is set */
  for (int i = 0; i < R.njokers; i++) R.jokers[i].flags &= (uint8_t)~JF_SLICED;
  for (int i = 0; i < R.njokers; i++) {
    if (R.jokers[i].flags & JF_SLICED) continue;
    cx_t cx = {0};
    cx.kind = CX_SETTING_BLIND, cx.bp = -1, cx.boss = R.blind >= BL_OX;
    eff_t e = {0};
    if (jcalc(i, &cx, &e) && (e.has & EF_MSG) && e.msg && e.msg[0]) ev_popup(TG_JOKER, e.card, e.msg, e.col);
    if (R.blind_disabled == 2) blind_disable();
  }
  for (int i = R.njokers - 1; i >= 0; i--)
    if (R.jokers[i].flags & JF_SLICED) joker_remove(i, 0);
  ev_t *v = ev_push(EV_HANDS);
  if (v) v->a = (uint16_t)R.hands_left;
  v = ev_push(EV_DISCARDS);
  if (v) v->a = (uint16_t)R.discards_left;
  shuffle_deck();
  draw_phase();
}

void blind_skip(void) {
  ev_reset();
  int on = R.blind_on;
  if (on > 1) return;
  R.skips++;
  add_tag(R.skip_tag[on]);
  R.blind_state[on] = BS_SKIPPED;
  R.blind_on = (uint8_t)(on + 1);
  R.blind_state[on + 1] = BS_SELECT;
  cx_t cx = {0};
  for (int i = 0; i < R.njokers; i++) {
    cx.kind = CX_SKIP_BLIND, cx.bp = -1;
    eff_t e = {0};
    if (jcalc(i, &cx, &e) && (e.has & EF_MSG)) ev_popup(TG_JOKER, e.card, e.msg, e.col);
  }
  tags_immediate();
  tags_new_blind_choice();
  ev_sync();
}

void boss_reroll(void) {
  ev_reset();
  R.boss_rerolled = 1;
  R.money -= 10;
  R.boss = (uint8_t)get_new_boss();
  ev_money();
  ev_sync();
}

void game_over(void) {
  R.phase = PH_GAMEOVER;
  ev_sync();
}

/* after a won round: blind state, then the cash out numbers */
void blind_defeat(void) {
  int boss = R.blind >= BL_OX;
  /* flip the jokers back */
  for (int i = 0; i < R.njokers; i++)
    if (R.jokers[i].flags & (JF_DOWN | JF_DEBUFF)) {
      R.jokers[i].flags &= (uint8_t)~(JF_DOWN | JF_DEBUFF);
      ev_t *v = ev_push(EV_JOKER_SET);
      if (v) v->a = R.jokers[i].uid, v->b = R.jokers[i].ed, v->c = R.jokers[i].flags;
    }
  /* back to the deck */
  for (int i = 0; i < R.ndiscard; i++) R.deck[R.ndeck++] = R.discard[i];
  for (int i = 0; i < R.nhand; i++) R.deck[R.ndeck++] = R.hand[i];
  for (int i = 0; i < R.nplay; i++) R.deck[R.ndeck++] = R.play[i];
  R.ndiscard = R.nhand = R.nplay = 0;
  for (int i = 0; i < R.ncards_alloc; i++) R.cards[i].flags &= (uint8_t)~(CF_DOWN | CF_FORCED | CF_DEBUFF);
  int won_now = 0;
  R.blind_state[R.blind_on] = BS_DEFEATED;
  if (boss) {
    if (R.ante == WIN_ANTE && !R.won) won_now = 1, R.won = 1;
    R.ante++;
    if (R.ante > R.max_ante_reached) R.max_ante_reached = R.ante;
    next_voucher();
    R.voucher_tag_extra = 0;
    for (int i = 0; i < R.ncards_alloc; i++) R.cards[i].flags &= (uint8_t)~CF_PLAYED_ANTE;
  } else {
    R.blind_on++;
    R.blind_state[R.blind_on] = BS_SELECT;
  }
  R.temp_handsize = 0;
  R.temp_reroll = -1;
  reset_round_cards();
  /* cash out */
  R.cash_blind = blind_info[R.blind].dollars;
  R.cash_hands = R.hands_left;
  int per[MAXJ];
  uint8_t ids[MAXJ];
  R.ncash_joker = (uint8_t)jokers_dollar_bonus(per, ids);
  for (int i = 0; i < R.ncash_joker; i++) R.cash_joker[i] = (int16_t)per[i], R.cash_joker_id[i] = ids[i];
  R.cash_tag = 0;
  if (boss)
    for (int i = 0; i < R.ntags; i++)
      if (R.tags[i] == TAG_INVESTMENT) {
        R.cash_tag = 25;
        tag_remove(i);
        break;
      }
  int interest = 0;
  if (R.money >= 5) {
    int k = R.money / 5, cap = R.interest_cap / 5, amount = 1 + has_joker(J_TO_THE_MOON);
    interest = amount * (k < cap ? k : cap);
  }
  R.cash_interest = (int16_t)interest;
  int total = R.cash_blind + R.cash_hands + R.cash_tag + R.cash_interest;
  for (int i = 0; i < R.ncash_joker; i++) total += R.cash_joker[i];
  R.cash_total = (int16_t)total;
  R.phase = won_now ? PH_WIN : PH_CASHOUT;
  ev_sync();
}

int jokers_dollar_bonus(int *per, uint8_t *ids) {
  int n = 0;
  for (int i = 0; i < R.njokers; i++) {
    joker_t *j = &R.jokers[i];
    if (j->flags & JF_DEBUFF) continue;
    int d = 0;
    switch (j->id) {
      case J_GOLDEN: d = 4; break;
      case J_CLOUD_9: {
        int nines = 0;
        for (int c = 0; c < R.ncards_alloc; c++)
          if ((R.cards[c].flags & CF_USED) && card_get_id(c) == 9) nines++;
        d = nines;
        break;
      }
      case J_ROCKET: d = j->a; break;
      case J_SATELLITE: {
        int p = 0;
        for (int k = 0; k < 12; k++) p += (R.planets_used_mask >> k) & 1;
        d = 1 * p;
        break;
      }
      case J_DELAYED_GRAT:
        if (R.discards_used_round == 0 && R.discards_left > 0) d = R.discards_left * 2;
        break;
    }
    if (d > 0) per[n] = d, ids[n++] = (uint8_t)i;
  }
  return n;
}

void cash_out(void) {
  ev_reset();
  R.chips = 0;
  R.money += R.cash_total;
  ev_money();
  R.hands_left = (int16_t)round_hands();
  R.discards_left = (int16_t)round_discards();
  if (R.blind_state[2] == BS_DEFEATED) {
    R.skip_tag[0] = (uint8_t)tag_for_skip();
    R.skip_tag[1] = (uint8_t)tag_for_skip();
    R.blind_state[0] = BS_SELECT, R.blind_state[1] = BS_UPCOMING, R.blind_state[2] = BS_UPCOMING;
    R.blind_on = 0;
    R.boss = (uint8_t)get_new_boss();
    R.boss_rerolled = 0;
  }
  shop_enter();
}

void continue_endless(void) {
  R.endless = 1;
  R.phase = PH_CASHOUT;
}

/* ------------------------------------------------------------------ shop */
static void calc_reroll(int skip_increment) {
  if (R.free_rerolls < 0) R.free_rerolls = 0;
  if (R.free_rerolls > 0) {
    R.reroll_cost = 0;
    return;
  }
  if (!skip_increment) R.reroll_inc++;
  R.reroll_cost = (int16_t)((R.temp_reroll >= 0 ? R.temp_reroll : R.reroll_base) + R.reroll_inc);
}

static void random_card_item(sitem_t *it, int enhanced) {
  it->kind = IT_CARD;
  it->card = random_card();
  if (enhanced) it->card.enh = (uint8_t)random_enhancement(0);
}

/* store_joker_modify tags on a new shop joker */
static void tags_modify(sitem_t *it) {
  if (it->kind != IT_JOKER || it->ed) return;
  for (int i = 0; i < R.ntags; i++) {
    int t = R.tags[i], ed = t == TAG_FOIL ? ED_FOIL : t == TAG_HOLO ? ED_HOLO : t == TAG_POLYCHROME ? ED_POLY
                                                     : t == TAG_NEGATIVE ? ED_NEG : 0;
    if (!ed) continue;
    it->ed = (uint8_t)ed;
    it->flags |= 1;
    tag_remove(i);
    return;
  }
}

static void shop_card(sitem_t *it) {
  memset(it, 0, sizeof *it);
  it->uid = (uint16_t)joker_new_uid();
  /* store_joker_create tags */
  for (int i = 0; i < R.ntags; i++) {
    int t = R.tags[i];
    if (t == TAG_UNCOMMON || t == TAG_RARE) {
      tag_remove(i);
      int rare_owned = 0, n_rare = 0;
      for (int id = 0; id < N_JOKER; id++) n_rare += joker_info[id].rarity == 3;
      for (int k = 0; k < R.njokers; k++) rare_owned += joker_info[R.jokers[k].id].rarity == 3;
      if (t == TAG_RARE && rare_owned >= n_rare) break;
      it->kind = IT_JOKER;
      it->id = (uint8_t)create_joker_id(t == TAG_RARE ? 3 : 2, 0);
      it->ed = (uint8_t)poll_edition(1, 0, 0);
      it->flags |= 1;
      tags_modify(it);
      return;
    }
  }
  float total = 20 + R.tarot_rate + R.planet_rate + R.playing_card_rate + R.spectral_rate;
  float p = rng_f() * total;
  if ((p -= 20) < 0) {
    it->kind = IT_JOKER;
    it->id = (uint8_t)create_joker_id(-1, 0);
    it->ed = (uint8_t)poll_edition(1, 0, 0);
    tags_modify(it);
  } else if ((p -= R.tarot_rate) < 0) {
    it->kind = IT_CONS, it->id = (uint8_t)create_cons_id(0);
  } else if ((p -= R.planet_rate) < 0) {
    it->kind = IT_CONS, it->id = (uint8_t)create_cons_id(1);
  } else if ((p -= R.playing_card_rate) < 0) {
    random_card_item(it, has_voucher(V_ILLUSION) && rng_f() > 0.6f);
    if (has_voucher(V_ILLUSION) && rng_f() > 0.8f) {
      float e = rng_f();
      it->card.ed = e > 0.85f ? ED_POLY : e > 0.5f ? ED_HOLO : ED_FOIL;
    }
  } else {
    it->kind = IT_CONS, it->id = (uint8_t)create_cons_id(2);
  }
  if (it->kind == IT_JOKER && it->id == J_TODO_LIST) {
    joker_t j = {0};
    j.id = J_TODO_LIST;
    joker_init(&j);
    it->a = j.c;
  }
}

static int pack_pick(void) {
  /* get_pack: the first shop has a Buffoon Pack */
  if (!R.first_shop_buffoon) {
    R.first_shop_buffoon = 1;
    return PK_BUFFOON * 3;
  }
  /* weights by kind and size, as in P_CENTERS */
  static const float w[5][3] = {{4, 2, 0.5f}, {4, 2, 0.5f}, {4, 2, 0.5f}, {1.2f, 0.6f, 0.15f}, {0.6f, 0.3f, 0.07f}};
  float tot = 0;
  for (int k = 0; k < 5; k++)
    for (int s = 0; s < 3; s++) tot += w[k][s];
  float p = rng_f() * tot;
  for (int k = 0; k < 5; k++)
    for (int s = 0; s < 3; s++) {
      p -= w[k][s];
      if (p < 0) return k * 3 + s;
    }
  return 0;
}

void shop_enter(void) {
  ev_reset();
  R.phase = PH_SHOP;
  R.d6 = 0, R.coupon = 0;
  R.temp_reroll = -1;
  /* shop_start: D6 Tag */
  for (int i = 0; i < R.ntags; i++)
    if (R.tags[i] == TAG_D_SIX) {
      tag_remove(i);
      R.temp_reroll = 0;
      R.d6 = 1;
      break;
    }
  R.reroll_inc = 0;
  calc_reroll(1);
  R.nshop = 0;
  for (int i = 0; i < R.shop_jokers_max && i < MAXSHOP; i++) shop_card(&R.shop[R.nshop++]);
  R.nshop_v = 0;
  if (R.shop_voucher != 0xFF) {
    sitem_t *v = &R.shop_v[R.nshop_v++];
    memset(v, 0, sizeof *v);
    v->kind = IT_VOUCHER, v->id = R.shop_voucher, v->uid = (uint16_t)joker_new_uid();
  }
  R.nshop_p = 0;
  for (int i = 0; i < 2; i++) {
    sitem_t *p = &R.shop_p[R.nshop_p++];
    memset(p, 0, sizeof *p);
    p->kind = IT_PACK, p->id = (uint8_t)pack_pick(), p->uid = (uint16_t)joker_new_uid();
  }
  /* voucher_add: Voucher Tag */
  for (int i = 0; i < R.ntags && R.nshop_v < 3; i++)
    if (R.tags[i] == TAG_VOUCHER) {
      tag_remove(i);
      i--;
      int pool[32], n = 0;
      for (int v = 0; v < 32; v++) {
        int in = has_voucher(v) || (v >= 16 && !has_voucher(v - 16));
        for (int k = 0; k < R.nshop_v; k++) in |= R.shop_v[k].id == v;
        if (!in) pool[n++] = v;
      }
      if (!n) continue;
      sitem_t *v = &R.shop_v[R.nshop_v++];
      memset(v, 0, sizeof *v);
      v->kind = IT_VOUCHER, v->id = (uint8_t)pool[rng_int(0, n - 1)], v->uid = (uint16_t)joker_new_uid();
      v->flags = 2; /* not the ante's voucher */
    }
  /* shop_final_pass: Coupon Tag */
  for (int i = 0; i < R.ntags; i++)
    if (R.tags[i] == TAG_COUPON) {
      tag_remove(i);
      R.coupon = 1;
      for (int k = 0; k < R.nshop; k++) R.shop[k].flags |= 1;
      for (int k = 0; k < R.nshop_p; k++) R.shop_p[k].flags |= 1;
      break;
    }
  ev_sync();
}

void shop_leave(void) {
  ev_reset();
  cx_t cx = {0};
  for (int i = 0; i < R.njokers; i++) {
    cx.kind = CX_ENDING_SHOP, cx.bp = -1;
    eff_t e = {0};
    if (jcalc(i, &cx, &e) && (e.has & EF_MSG)) ev_popup(TG_JOKER, e.card, e.msg, e.col);
  }
  R.nshop = R.nshop_p = 0;
  run_start_blind_select();
  ev_sync();
}

static int can_afford(int cost) {
  int bankrupt = -20 * has_joker(J_CREDIT_CARD);
  return cost == 0 || R.money - bankrupt - cost >= 0;
}

int shop_reroll(void) {
  if (!can_afford(R.reroll_cost)) return 0;
  ev_reset();
  if (R.reroll_cost > 0) {
    R.money -= R.reroll_cost;
    ev_money();
  }
  int final_free = R.free_rerolls > 0;
  R.free_rerolls = (int16_t)(R.free_rerolls > 0 ? R.free_rerolls - 1 : 0);
  R.rerolls++;
  calc_reroll(final_free);
  R.nshop = 0;
  for (int i = 0; i < R.shop_jokers_max && i < MAXSHOP; i++) shop_card(&R.shop[R.nshop++]);
  cx_t cx = {0};
  for (int i = 0; i < R.njokers; i++) {
    cx.kind = CX_REROLL, cx.bp = -1;
    eff_t e = {0};
    if (jcalc(i, &cx, &e) && (e.has & EF_MSG)) ev_popup(TG_JOKER, e.card, e.msg, e.col);
  }
  ev_sync();
  return 1;
}

static void redeem(int v) {
  R.vouchers |= (uint32_t)1 << v;
  switch (v) {
    case V_OVERSTOCK_NORM: case V_OVERSTOCK_PLUS:
      R.shop_jokers_max++;
      if (R.phase == PH_SHOP && R.nshop < MAXSHOP) shop_card(&R.shop[R.nshop++]);
      break;
    case V_TAROT_MERCHANT: R.tarot_rate = 4 * 2.4f; break;
    case V_TAROT_TYCOON: R.tarot_rate = 4 * 8; break;
    case V_PLANET_MERCHANT: R.planet_rate = 4 * 2.4f; break;
    case V_PLANET_TYCOON: R.planet_rate = 4 * 8; break;
    case V_HONE: R.edition_rate = 2; break;
    case V_GLOW_UP: R.edition_rate = 4; break;
    case V_MAGIC_TRICK: case V_ILLUSION: R.playing_card_rate = 4; break;
    case V_CRYSTAL_BALL: R.cons_slots++; break;
    case V_CLEARANCE_SALE: R.discount = 25; break;
    case V_LIQUIDATION: R.discount = 50; break;
    case V_REROLL_SURPLUS: case V_REROLL_GLUT:
      R.reroll_base -= 2;
      R.reroll_cost = (int16_t)(R.reroll_cost > 2 ? R.reroll_cost - 2 : 0);
      break;
    case V_SEED_MONEY: R.interest_cap = 50; break;
    case V_MONEY_TREE: R.interest_cap = 100; break;
    case V_GRABBER: case V_NACHO_TONG:
      R.base_hands++;
      R.hands_left++;
      break;
    case V_WASTEFUL: case V_RECYCLOMANCY:
      R.base_discards++;
      R.discards_left++;
      break;
    case V_ANTIMATTER: R.joker_slots++; break;
    case V_HIEROGLYPH:
      R.ante--;
      R.base_hands--;
      R.hands_left--;
      break;
    case V_PETROGLYPH:
      R.ante--;
      R.base_discards--;
      R.discards_left--;
      break;
  }
}

int shop_buy(int area, int idx, int use_now) {
  sitem_t *it = area == 0 ? &R.shop[idx] : area == 1 ? &R.shop_v[idx] : &R.shop_p[idx];
  int cnt = area == 0 ? R.nshop : area == 1 ? R.nshop_v : R.nshop_p;
  if (idx < 0 || idx >= cnt) return 0;
  int cost = item_cost(it);
  if (!can_afford(cost)) return 0;
  if (!use_now) {
    if (it->kind == IT_JOKER && (R.njokers >= joker_slots() + (it->ed == ED_NEG) || R.njokers >= MAXJ)) return 0;
    if (it->kind == IT_CONS && (R.ncons >= cons_slots() + (it->ed == ED_NEG) || R.ncons >= MAXCONS)) return 0;
  }
  if (it->kind == IT_CARD && deck_full()) return 0;
  ev_reset();
  sitem_t copy = *it;
  /* take it out of the shop */
  if (area == 0) {
    for (int i = idx; i < R.nshop - 1; i++) R.shop[i] = R.shop[i + 1];
    R.nshop--;
  } else if (area == 1) {
    for (int i = idx; i < R.nshop_v - 1; i++) R.shop_v[i] = R.shop_v[i + 1];
    R.nshop_v--;
  } else {
    for (int i = idx; i < R.nshop_p - 1; i++) R.shop_p[i] = R.shop_p[i + 1];
    R.nshop_p--;
  }
  if (cost) {
    R.money -= cost;
    ev_money();
  }
  if (copy.kind != IT_PACK) R.cards_bought++;
  switch (copy.kind) {
    case IT_JOKER: {
      joker_t *j = &R.jokers[R.njokers];
      memset(j, 0, sizeof *j);
      j->id = copy.id;
      j->uid = copy.uid;
      joker_init(j);
      if (copy.id == J_TODO_LIST) j->c = (uint8_t)copy.a;
      j->ed = copy.ed;
      R.njokers++;
      joker_on_added(j);
      ev_t *v = ev_push(EV_JOKER_ADD);
      if (v) v->a = j->uid, v->b = j->id, v->c = j->ed, v->d = (int16_t)(R.njokers - 1);
      break;
    }
    case IT_CONS:
      if (use_now) {
        extern int use_cons_id(int id, int ed, int from);
        use_cons_id(copy.id, copy.ed, 2);
      } else {
        cons_t *c = &R.cons[R.ncons++];
        c->id = copy.id, c->ed = copy.ed, c->uid = copy.uid, c->extra_value = 0;
        ev_t *v = ev_push(EV_CONS_ADD);
        if (v) v->a = c->uid, v->b = c->id, v->c = c->ed;
      }
      break;
    case IT_CARD: {
      int ci = add_card_to_deck(copy.card, A_DECK);
      (void)ci;
      cx_t cx = {0};
      cx.kind = CX_CARDS_ADDED, cx.bp = -1, cx.n = 1;
      for (int i = 0; i < R.njokers; i++) {
        eff_t e = {0};
        jcalc(i, &cx, &e);
      }
      break;
    }
    case IT_VOUCHER:
      if (!(copy.flags & 2)) R.shop_voucher = 0xFF;
      redeem(copy.id);
      break;
    case IT_PACK: pack_open(copy.id / 3, copy.id % 3, 0); break;
  }
  ev_sync();
  return 1;
}

/* ------------------------------------------------------------------ packs */
static const uint8_t pack_n[5][3] = {{3, 5, 5}, {3, 5, 5}, {3, 5, 5}, {2, 4, 4}, {2, 4, 4}};

void pack_open(int kind, int size, int from_tag) {
  R.pack_return = R.phase == PH_PACK ? R.pack_return : R.phase;
  R.phase = PH_PACK;
  R.pack_kind = (uint8_t)kind, R.pack_size = (uint8_t)size, R.pack_from_tag = (uint8_t)from_tag;
  R.pack_picks = size == 2 ? 2 : 1;
  R.npack = 0;
  int n = pack_n[kind][size];
  for (int i = 0; i < n; i++) {
    sitem_t *it = &R.pack[R.npack++];
    memset(it, 0, sizeof *it);
    it->uid = (uint16_t)joker_new_uid();
    switch (kind) {
      case PK_ARCANA:
        it->kind = IT_CONS;
        if (has_voucher(V_OMEN_GLOBE) && rng_f() > 0.8f) it->id = (uint8_t)create_cons_soulable(2);
        else it->id = (uint8_t)create_cons_soulable(0);
        break;
      case PK_CELESTIAL:
        it->kind = IT_CONS;
        if (has_voucher(V_TELESCOPE) && i == 0) {
          int best = -1, tally = 0;
          static const uint8_t order[12] = {H_FLUSH_FIVE, H_FLUSH_HOUSE, H_FIVE_KIND, H_STRAIGHT_FLUSH, H_FOUR_KIND,
                                            H_FULL_HOUSE, H_FLUSH, H_STRAIGHT, H_THREE_KIND, H_TWO_PAIR, H_PAIR,
                                            H_HIGH_CARD};
          for (int k = 0; k < 12; k++)
            if (R.hands[order[k]].visible && R.hands[order[k]].played > tally) tally = R.hands[order[k]].played, best = order[k];
          static const uint8_t planet_of[12] = {C_ERIS, C_CERES, C_PLANET_X, C_NEPTUNE, C_MARS, C_EARTH, C_JUPITER,
                                                C_SATURN, C_VENUS, C_URANUS, C_MERCURY, C_PLUTO};
          it->id = (uint8_t)(best >= 0 ? planet_of[best] : create_cons_soulable(1));
        } else
          it->id = (uint8_t)create_cons_soulable(1);
        break;
      case PK_SPECTRAL:
        it->kind = IT_CONS;
        it->id = (uint8_t)create_cons_soulable(2);
        break;
      case PK_STANDARD: {
        random_card_item(it, rng_f() > 0.6f);
        it->card.ed = (uint8_t)poll_edition(2, 1, 0);
        if (rng_f() > 1 - 0.02f * 10) {
          float s = rng_f();
          it->card.seal = s > 0.75f ? SEAL_RED : s > 0.5f ? SEAL_BLUE : s > 0.25f ? SEAL_GOLD : SEAL_PURPLE;
        }
        break;
      }
      case PK_BUFFOON:
        it->kind = IT_JOKER;
        it->id = (uint8_t)create_joker_id(-1, 0);
        it->ed = (uint8_t)poll_edition(1, 0, 0);
        break;
    }
  }
  cx_t cx = {0};
  for (int i = 0; i < R.njokers; i++) {
    cx.kind = CX_OPEN_BOOSTER, cx.bp = -1;
    eff_t e = {0};
    jcalc(i, &cx, &e);
  }
  /* Arcana and Spectral packs let you use cards on your hand */
  if (kind == PK_ARCANA || kind == PK_SPECTRAL) {
    for (int i = R.ndeck - 1; i > 0; i--) {
      int k = rng_int(0, i);
      uint8_t t = R.deck[i];
      R.deck[i] = R.deck[k], R.deck[k] = t;
    }
    int hs = hand_size();
    while (R.nhand < hs && R.ndeck) {
      int ci = R.deck[--R.ndeck];
      R.sel[R.nhand] = 0;
      R.hand[R.nhand++] = (uint8_t)ci;
      ev_card(EV_CARD_MOVE, ci, A_HAND, 0);
    }
    sort_hand(R.sort_suit);
  }
  ev_sync();
}

void pack_close(void) {
  /* the hand goes back into the deck */
  for (int i = 0; i < R.nhand; i++) {
    R.deck[R.ndeck++] = R.hand[i];
    ev_card(EV_CARD_MOVE, R.hand[i], A_DECK, 0);
  }
  R.nhand = 0;
  for (int i = 0; i < MAXHAND; i++) R.sel[i] = 0;
  R.npack = 0;
  R.phase = R.pack_return;
  if (R.phase == PH_BLIND_SELECT) tags_new_blind_choice();
  ev_sync();
}

void pack_skip(void) {
  ev_reset();
  cx_t cx = {0};
  for (int i = 0; i < R.njokers; i++) {
    cx.kind = CX_SKIPPING_BOOSTER, cx.bp = -1;
    eff_t e = {0};
    if (jcalc(i, &cx, &e) && (e.has & EF_MSG)) ev_popup(TG_JOKER, e.card, e.msg, e.col);
  }
  pack_close();
}

int pack_choose(int idx) {
  if (idx < 0 || idx >= R.npack || R.pack_picks <= 0) return 0;
  sitem_t *it = &R.pack[idx];
  if (it->kind == IT_JOKER && ((R.njokers >= joker_slots() && it->ed != ED_NEG) || R.njokers >= MAXJ)) return 0;
  if (it->kind == IT_CARD && deck_full()) return 0;
  if (it->kind == IT_CONS) {
    extern int cons_usable(int id, int in_pack);
    if (!cons_usable(it->id, 1)) return 0;
  }
  ev_reset();
  sitem_t copy = *it;
  for (int i = idx; i < R.npack - 1; i++) R.pack[i] = R.pack[i + 1];
  R.npack--;
  switch (copy.kind) {
    case IT_JOKER: {
      joker_t *j = &R.jokers[R.njokers];
      memset(j, 0, sizeof *j);
      j->id = copy.id, j->uid = copy.uid;
      joker_init(j);
      j->ed = copy.ed;
      R.njokers++;
      joker_on_added(j);
      ev_t *v = ev_push(EV_JOKER_ADD);
      if (v) v->a = j->uid, v->b = j->id, v->c = j->ed, v->d = (int16_t)(R.njokers - 1);
      break;
    }
    case IT_CARD: {
      add_card_to_deck(copy.card, A_DECK);
      cx_t cx = {0};
      cx.kind = CX_CARDS_ADDED, cx.bp = -1, cx.n = 1;
      for (int i = 0; i < R.njokers; i++) {
        eff_t e = {0};
        jcalc(i, &cx, &e);
      }
      break;
    }
    case IT_CONS: {
      extern int use_cons_id(int id, int ed, int from);
      use_cons_id(copy.id, copy.ed, 1);
      break;
    }
  }
  R.pack_picks--;
  if (R.pack_picks <= 0 || R.npack == 0) pack_close();
  else ev_sync();
  return 1;
}

/* ------------------------------------------------------------------ selling, moving */
int sell_joker(int idx) {
  if (idx < 0 || idx >= R.njokers) return 0;
  ev_reset();
  int v = joker_sell_value(&R.jokers[idx]);
  uint16_t uid = R.jokers[idx].uid;
  /* selling_self (Luchador, Diet Cola, Invisible Joker) */
  cx_t cx = {0};
  cx.kind = CX_SELLING_SELF, cx.bp = -1;
  eff_t e = {0};
  int had = jcalc(idx, &cx, &e);
  R.money += v;
  ev_money();
  char t[12];
  t[0] = '$';
  fmt_int(t + 1, v);
  ev_popup(TG_JOKER, idx, t, PC_MONEY);
  if (had && (e.has & EF_MSG)) ev_popup(TG_JOKER, idx, e.msg, e.col);
  /* the index may have moved if a joker was added: find it again */
  for (int i = 0; i < R.njokers; i++)
    if (R.jokers[i].uid == uid) idx = i;
  joker_remove(idx, 2);
  apply_disable_request();
  if (R.phase == PH_ROUND && R.blind == BL_FINAL_LEAF && !R.blind_disabled) blind_disable();
  for (int i = 0; i < R.njokers; i++) {
    cx_t c2 = {0};
    c2.kind = CX_SELLING_CARD, c2.bp = -1;
    eff_t e2 = {0};
    if (jcalc(i, &c2, &e2) && (e2.has & EF_MSG)) ev_popup(TG_JOKER, e2.card, e2.msg, e2.col);
  }
  ev_sync();
  return 1;
}

int sell_cons(int idx) {
  if (idx < 0 || idx >= R.ncons) return 0;
  ev_reset();
  int v = cons_sell_value(&R.cons[idx]);
  R.money += v;
  ev_money();
  remove_cons(idx);
  for (int i = 0; i < R.njokers; i++) {
    cx_t c2 = {0};
    c2.kind = CX_SELLING_CARD, c2.bp = -1;
    eff_t e2 = {0};
    if (jcalc(i, &c2, &e2) && (e2.has & EF_MSG)) ev_popup(TG_JOKER, e2.card, e2.msg, e2.col);
  }
  ev_sync();
  return 1;
}

void move_joker(int from, int to) {
  if (from < 0 || to < 0 || from >= R.njokers || to >= R.njokers || from == to) return;
  joker_t t = R.jokers[from];
  if (from < to)
    for (int i = from; i < to; i++) R.jokers[i] = R.jokers[i + 1];
  else
    for (int i = from; i > to; i--) R.jokers[i] = R.jokers[i - 1];
  R.jokers[to] = t;
}

void move_cons(int from, int to) {
  if (from < 0 || to < 0 || from >= R.ncons || to >= R.ncons || from == to) return;
  cons_t t = R.cons[from];
  if (from < to)
    for (int i = from; i < to; i++) R.cons[i] = R.cons[i + 1];
  else
    for (int i = from; i > to; i--) R.cons[i] = R.cons[i - 1];
  R.cons[to] = t;
}

void move_hand_card(int from, int to) {
  if (from < 0 || to < 0 || from >= R.nhand || to >= R.nhand || from == to) return;
  uint8_t c = R.hand[from], s = R.sel[from];
  if (from < to)
    for (int i = from; i < to; i++) R.hand[i] = R.hand[i + 1], R.sel[i] = R.sel[i + 1];
  else
    for (int i = from; i > to; i--) R.hand[i] = R.hand[i - 1], R.sel[i] = R.sel[i - 1];
  R.hand[to] = c, R.sel[to] = s;
}

int nselected(void) {
  int n = 0;
  for (int i = 0; i < R.nhand; i++) n += R.sel[i] != 0;
  return n;
}

void toggle_select(int pos) {
  if (pos < 0 || pos >= R.nhand) return;
  if (R.cards[R.hand[pos]].flags & CF_FORCED) return; /* Cerulean Bell */
  if (R.sel[pos]) R.sel[pos] = 0;
  else if (nselected() < 5) R.sel[pos] = 1;
}

void sort_hand(int by_suit) {
  /* Balatro: descending by rank (then suit), or by suit (then rank) */
  static const uint8_t suitn[4] = {4, 3, 2, 1};
  for (int a = 0; a < R.nhand; a++)
    for (int b = a + 1; b < R.nhand; b++) {
      const card_t *x = &R.cards[R.hand[a]], *y = &R.cards[R.hand[b]];
      int kx, ky;
      int nx = card_chip_nominal(x->rank) * 10 + (x->rank > 10 ? x->rank - 10 : 0);
      int ny = card_chip_nominal(y->rank) * 10 + (y->rank > 10 ? y->rank - 10 : 0);
      if (by_suit) kx = suitn[x->suit] * 1000 + nx, ky = suitn[y->suit] * 1000 + ny;
      else kx = nx * 10 + suitn[x->suit], ky = ny * 10 + suitn[y->suit];
      if (x->enh == E_STONE) kx = -100000 + kx;
      if (y->enh == E_STONE) ky = -100000 + ky;
      if (ky > kx) {
        uint8_t t = R.hand[a];
        R.hand[a] = R.hand[b], R.hand[b] = t;
        t = R.sel[a], R.sel[a] = R.sel[b], R.sel[b] = t;
      }
    }
  ev_push(EV_SORT);
}

/* ------------------------------------------------------------------ new run */
void run_new(const char *seed) {
  memset(&R, 0, sizeof R);
  if (seed && seed[0]) {
    for (int i = 0; i < 8 && seed[i]; i++) R.seed[i] = seed[i];
  } else
    rng_new_seed();
  rng_seed_str(R.seed);
  R.money = 4;
  R.ante = 1;
  R.round = 0;
  R.base_hands = 4;
  R.base_discards = 3 + 1; /* Red Deck: +1 discard every round */
  R.joker_slots = 5;
  R.cons_slots = 2;
  R.interest_cap = 25;
  R.reroll_base = 5;
  R.shop_jokers_max = 2;
  R.tarot_rate = 4, R.planet_rate = 4, R.spectral_rate = 0, R.playing_card_rate = 0, R.edition_rate = 1;
  R.last_tarot_planet = -1;
  R.temp_reroll = -1;
  R.max_ante_reached = 1;
  for (int h = 0; h < NHANDS; h++) {
    R.hands[h].level = 1;
    R.hands[h].visible = h >= H_STRAIGHT_FLUSH;
  }
  for (int s = 0; s < 4; s++)
    for (int r = 2; r <= 14; r++) {
      card_t c = {0};
      c.rank = (uint8_t)r, c.suit = (uint8_t)s, c.flags = CF_USED;
      R.cards[R.ncards_alloc] = c;
      R.deck[R.ndeck++] = R.ncards_alloc++;
    }
  R.starting_deck = 52;
  R.blind_state[0] = BS_SELECT, R.blind_state[1] = BS_UPCOMING, R.blind_state[2] = BS_UPCOMING;
  R.boss = (uint8_t)get_new_boss();
  R.skip_tag[0] = (uint8_t)tag_for_skip();
  R.skip_tag[1] = (uint8_t)tag_for_skip();
  next_voucher();
  reset_round_cards();
  R.phase = PH_BLIND_SELECT;
  R.hands_left = (int16_t)round_hands();
  R.discards_left = (int16_t)round_discards();
  ev_reset();
  ev_sync();
}
