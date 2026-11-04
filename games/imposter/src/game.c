/* Imposter: the players, the settings, the rounds and the save. */
#include "game.h"
#include <string.h>
#include "plat.h"

save_t sv;
const uint8_t timer_minutes[N_TIMERS] = {0, 1, 2, 3, 5};

#define SAVE_NAME "imposter.sav"
#define COPY_NAME "imposter_saves.py"
#define VERSION 1
#define ALL_CATS (((uint32_t)1 << N_CATS) - 1)

/* ---- chance */
static uint32_t seed = 1;
void rnd_seed(uint32_t s) { seed = s ? s : 0x9E3779B9u; }
/* the moment a key is pressed adds to the chance */
void rnd_stir(uint32_t v) {
  seed ^= v * 2654435761u;
  if (!seed) seed = 1;
  rnd();
}
uint32_t rnd(void) {
  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;
  return seed;
}
static int rnd_below(int n) { return (int)(rnd() % (uint32_t)n); }

/* ---- the rules */
int max_imposters(int players) { return players >= 7 ? 3 : players >= 5 ? 2 : 1; }
int imposters_now(void) {
  int m = max_imposters(sv.nplayers);
  return sv.imposters < m ? sv.imposters : m;
}
bool cat_on(int c) { return !sv.cats || (sv.cats >> c & 1); }
int cats_chosen(void) {
  int n = 0;
  for (int c = 0; c < N_CATS; c++) n += cat_on(c);
  return n;
}
bool is_imposter(int p) { return sv.imps >> p & 1; }

static bool used(int c, int i) { return sv.used[c * 8 + i / 8] >> (i % 8) & 1; }
static void use(int c, int i) { sv.used[c * 8 + i / 8] |= (uint8_t)(1 << (i % 8)); }
static int unused_count(int c) {
  int n = 0, k = word_count(c);
  for (int i = 0; i < k; i++) n += !used(c, i);
  return n;
}

int word_text(uint16_t w, char *out) {
  int c = w >> 6;
  if (w == NO_WORD || c >= N_CATS) return out[0] = 0, 0;
  word_get(c, w & 63, out, WORD_MAX + 1);
  return c;
}

/* a word of the chosen categories not played yet, every one as likely; a category played through starts again
 * (but not with the word just played) */
static uint16_t pick_word(void) {
  int total = 0;
  for (int c = 0; c < N_CATS; c++) {
    if (!cat_on(c)) continue;
    if (!unused_count(c)) memset(sv.used + c * 8, 0, 8);
    total += unused_count(c);
  }
  uint16_t skip = NO_WORD;
  if (sv.last != NO_WORD && cat_on(sv.last >> 6) && !used(sv.last >> 6, sv.last & 63) && total > 1) skip = sv.last, total--;
  int r = rnd_below(total);
  for (int c = 0; c < N_CATS; c++) {
    if (!cat_on(c)) continue;
    for (int i = 0, k = word_count(c); i < k; i++)
      if (!used(c, i) && (uint16_t)(c << 6 | i) != skip && r-- == 0) {
        use(c, i);
        return (uint16_t)(c << 6 | i);
      }
  }
  return 0; /* (never) */
}

void round_new(void) {
  int n = sv.nplayers, k = imposters_now();
  sv.word = pick_word();
  sv.last = sv.word;
  sv.imps = 0;
  while (k) {
    int p = rnd_below(n);
    if (!is_imposter(p)) sv.imps |= (uint16_t)(1 << p), k--;
  }
  sv.starter = (uint8_t)rnd_below(n);
  sv.turn = 0;
  sv.phase = PH_PASS;
  sv.time_left = (uint32_t)timer_minutes[sv.timer] * 60000;
  if (sv.rounds < 0xFFFF) sv.rounds++;
  game_save();
}

void round_end(void) {
  sv.phase = PH_NONE;
  game_save();
}

/* ---- the save, and its copy in a Python script: installing apps from the NumWorks website keeps only scripts */
void game_defaults(void) {
  memset(&sv, 0, sizeof sv);
  sv.imposters = 1, sv.hint = 1;
  sv.word = sv.last = NO_WORD;
}

static const char copy_head[] = "# Imposter's saves: keep this file\n#>" SAVE_NAME ":";
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
#define COPY_SIZE (1 + sizeof copy_head - 1 + (sizeof(save_t) + 2) / 3 * 4 + 2)
static uint8_t copy[COPY_SIZE];

static void write_copy(void) {
  const uint8_t *d = (const uint8_t *)&sv;
  uint8_t *p = copy;
  *p++ = 0; /* (a script's first byte: not imported) */
  memcpy(p, copy_head, sizeof copy_head - 1), p += sizeof copy_head - 1;
  for (uint32_t i = 0; i < sizeof sv; i += 3, p += 4) {
    uint32_t left = (uint32_t)sizeof sv - i;
    uint32_t v = (uint32_t)d[i] << 16 | (uint32_t)(left > 1 ? d[i + 1] : 0) << 8 | (left > 2 ? d[i + 2] : 0);
    p[0] = (uint8_t)b64[v >> 18 & 63], p[1] = (uint8_t)b64[v >> 12 & 63];
    p[2] = left > 1 ? (uint8_t)b64[v >> 6 & 63] : '=';
    p[3] = left > 2 ? (uint8_t)b64[v & 63] : '=';
  }
  *p++ = '\n';
  *p++ = 0;
  plat_save(COPY_NAME, copy, (uint32_t)(p - copy));
}

static int b64_value(uint8_t c) {
  for (int i = 0; i < 64; i++)
    if ((uint8_t)b64[i] == c) return i;
  return -1;
}

/* the save in the copy, into out; false if it isn't one */
static bool read_copy(save_t *out) {
  uint32_t n;
  const uint8_t *c = plat_load(COPY_NAME, &n);
  uint32_t head = 1 + sizeof copy_head - 1, len = (sizeof(save_t) + 2) / 3 * 4;
  if (!c || n < head + len || memcmp(c + 1, copy_head, sizeof copy_head - 1)) return false;
  uint8_t *o = (uint8_t *)out;
  uint32_t k = 0;
  for (uint32_t i = 0; i < len; i += 4) {
    const uint8_t *q = c + head + i;
    int v[4];
    for (int j = 0; j < 4; j++) {
      v[j] = q[j] == '=' ? 0 : b64_value(q[j]);
      if (v[j] < 0) return false;
    }
    uint32_t w = (uint32_t)v[0] << 18 | (uint32_t)v[1] << 12 | (uint32_t)v[2] << 6 | (uint32_t)v[3];
    for (int j = 0; j < 3 && k < sizeof(save_t); j++) o[k++] = (uint8_t)(w >> (16 - 8 * j));
  }
  return true;
}

static bool name_ok(const char *s) {
  int n = 0;
  while (n <= NAME_MAX && s[n]) {
    char ch = s[n];
    if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == ' ')) return false;
    n++;
  }
  return n >= 1 && n <= NAME_MAX && s[0] != ' ' && s[n - 1] != ' ';
}

static bool word_ok(uint16_t w) { return w >> 6 < N_CATS && (int)(w & 63) < word_count(w >> 6); }

static bool usable(const save_t *s) {
  return !memcmp(s->magic, "IMPS", 4) && s->version == VERSION;
}

/* whatever the file held: a save this game could have written */
static void sanitize(void) {
  int n = 0;
  int count = sv.nplayers <= MAX_PLAYERS ? sv.nplayers : MAX_PLAYERS;
  bool same = true;
  for (int i = 0; i < count; i++) {
    sv.names[i][NAME_MAX] = 0;
    if (name_ok(sv.names[i])) {
      if (n != i) memcpy(sv.names[n], sv.names[i], NAME_MAX + 1);
      n++;
    } else {
      same = false;
    }
  }
  for (int i = n; i < MAX_PLAYERS; i++) memset(sv.names[i], 0, NAME_MAX + 1);
  sv.nplayers = (uint8_t)n;
  if (sv.imposters < 1 || sv.imposters > 3) sv.imposters = 1;
  if (sv.hint > 1) sv.hint = 1;
  if (sv.timer >= N_TIMERS) sv.timer = 0;
  sv.cats &= ALL_CATS;
  if (sv.cats == ALL_CATS) sv.cats = 0;
  if (sv.last != NO_WORD && !word_ok(sv.last)) sv.last = NO_WORD;
  sv.flags &= 1;
  /* a round in progress, if it still stands */
  int k = 0;
  for (int i = 0; i < MAX_PLAYERS; i++) k += sv.imps >> i & 1;
  if (sv.phase > PH_REVEAL || !same || n < MIN_PLAYERS || !word_ok(sv.word) || sv.imps >> n || k < 1 ||
      k > max_imposters(n) || sv.turn >= n || sv.starter >= n || sv.time_left > 5 * 60000u)
    sv.phase = PH_NONE;
  if (sv.phase == PH_NONE) sv.word = NO_WORD, sv.imps = 0, sv.turn = sv.starter = 0, sv.time_left = 0;
}

void game_load(void) {
  uint32_t n;
  const uint8_t *d = plat_load(SAVE_NAME, &n);
  save_t s;
  if (d && n == sizeof s) {
    memcpy(&s, d, sizeof s);
    if (usable(&s)) {
      sv = s;
      sanitize();
      return;
    }
  }
  if (read_copy(&s) && usable(&s)) {
    sv = s;
    sanitize();
    return;
  }
  game_defaults();
}

void game_save(void) {
  memcpy(sv.magic, "IMPS", 4);
  sv.version = VERSION;
  plat_save(SAVE_NAME, &sv, sizeof sv);
  write_copy();
}
