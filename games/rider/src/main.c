/* Rider on the NumWorks calculator: Ketchapp's 2016 neon motorbike game, its tracks, bikes, numbers and colours
 * from the game's own files (tools/data.py, gamesrc/rider/spec).
 *
 * One button: OK (or EXE, or Up) held rides on, held in the air turns the bike backwards for a flip; let go to
 * land on both wheels. The endless run counts landed flips; the 48 levels give stars for their time, every flip
 * taking half a second off. Gems win bikes; runs, best scores, stars and challenges open the special ones.
 * Back pauses (Resume, How to play, Quit game, asked first), Home saves and quits.
 *
 * The ride runs at the game's 60 steps a second whatever the calculator manages to draw. */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "gfx.h"
#include "plat.h"
#include "rider.h"
#include "data.h"

#define SAVE_NAME "rider.sav"
#define COPY_NAME "rider_saves.py"
#define MAGIC 0x52445231u /* RDR1 */
#define VERSION 1

/* ---------------------------------------------------------------- what's kept */
#define NTOP 10
typedef struct {
  uint32_t magic;
  uint8_t version, bike, flags, gift_i; /* flags: 1 how to play was read */
  uint32_t gems, runs, play_ms;
  uint16_t best, challenges;
  uint32_t total_score, total_flips, total_perfect, total_gems, total_doubles;
  uint16_t top_score[NTOP], top_gems[NTOP], top_flips[NTOP], top_perfect[NTOP];
  uint8_t stars[NLEVELS];
  uint32_t best_ms[NLEVELS];
  uint8_t owned[8];       /* bikes, a bit each */
  uint16_t scapes_on;     /* the colours the endless run picks from */
  uint8_t draws;          /* bikes won with gems */
  uint8_t ch_tpl, ch_streak, ch_pad; /* the challenge on: its template, runs in a row */
  int32_t ch_progress;
  int16_t ch_x, ch_y, ch_z, ch_pad2;
  uint8_t ch_done[32];    /* times each challenge template was done */
  uint32_t sum;
} save_t;
static save_t sv;

static uint32_t fnv(const void *d, uint32_t n) {
  const uint8_t *p = d;
  uint32_t h = 2166136261u;
  for (uint32_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}
static bool owned(int b) { return sv.owned[b >> 3] >> (b & 7) & 1; }
static void own(int b) { sv.owned[b >> 3] |= (uint8_t)(1 << (b & 7)); }

static bool save_ok(const save_t *s) {
  if (s->magic != MAGIC || s->version != VERSION || s->sum != fnv(s, offsetof(save_t, sum))) return false;
  if (s->bike >= NBIKES || s->ch_tpl >= 32) return false;
  for (int i = 0; i < NLEVELS; i++)
    if (s->stars[i] > 3) return false;
  return (s->owned[s->bike >> 3] >> (s->bike & 7)) & 1;
}

static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
#define TAG "#>" SAVE_NAME ":" /* the copy's line (found anywhere: an older copy has a longer head) */
static const char head[] = "# Rider's saves: keep this file\n" TAG;
static uint8_t copy[1 + sizeof head + (sizeof(save_t) + 2) / 3 * 4 + 2];

static void save(void) {
  sv.sum = fnv(&sv, offsetof(save_t, sum));
  plat_save(SAVE_NAME, &sv, sizeof sv);
  uint8_t *p = copy;
  const uint8_t *f = (const uint8_t *)&sv;
  *p++ = 0; /* (a script's first byte: not imported) */
  memcpy(p, head, sizeof head - 1), p += sizeof head - 1;
  for (uint32_t i = 0; i < sizeof sv; i += 3, p += 4) {
    uint32_t left = sizeof sv - i;
    uint32_t v = (uint32_t)f[i] << 16 | (left > 1 ? (uint32_t)f[i + 1] << 8 : 0) | (left > 2 ? f[i + 2] : 0);
    p[0] = (uint8_t)b64[v >> 18 & 63], p[1] = (uint8_t)b64[v >> 12 & 63];
    p[2] = left > 1 ? (uint8_t)b64[v >> 6 & 63] : '=', p[3] = left > 2 ? (uint8_t)b64[v & 63] : '=';
  }
  *p++ = '\n';
  plat_save(COPY_NAME, copy, (uint32_t)(p - copy));
}
static int b64v(uint8_t c) {
  for (int i = 0; i < 64; i++)
    if ((uint8_t)b64[i] == c) return i;
  return c == '=' ? 0 : -1;
}
static bool copy_read(save_t *out) {
  uint32_t n;
  const uint8_t *c = plat_load(COPY_NAME, &n);
  if (!c) return false;
  const uint32_t hl = sizeof TAG - 1, m = (sizeof(save_t) + 2) / 3 * 4;
  for (uint32_t i = 0; i + hl + m <= n; i++) {
    if (memcmp(c + i, head + sizeof head - sizeof TAG, hl)) continue;
    const uint8_t *l = c + i + hl;
    uint8_t buf[(sizeof(save_t) + 2) / 3 * 3];
    for (uint32_t k = 0; k < m; k += 4) {
      int v0 = b64v(l[k]), v1 = b64v(l[k + 1]), v2 = b64v(l[k + 2]), v3 = b64v(l[k + 3]);
      if (v0 < 0 || v1 < 0 || v2 < 0 || v3 < 0) return false;
      uint32_t w = (uint32_t)(v0 << 18 | v1 << 12 | v2 << 6 | v3);
      buf[k / 4 * 3] = (uint8_t)(w >> 16), buf[k / 4 * 3 + 1] = (uint8_t)(w >> 8), buf[k / 4 * 3 + 2] = (uint8_t)w;
    }
    memcpy(out, buf, sizeof *out);
    return save_ok(out);
  }
  return false;
}
static void fresh(void) {
  memset(&sv, 0, sizeof sv);
  sv.magic = MAGIC, sv.version = VERSION, sv.bike = 0, sv.scapes_on = 7;
  own(0);
}
static void load(void) {
  uint32_t n;
  const uint8_t *d = plat_load(SAVE_NAME, &n);
  static save_t t;
  if (d && n == sizeof t) {
    memcpy(&t, d, sizeof t);
    if (save_ok(&t)) { sv = t; return; }
  }
  if (copy_read(&t)) { sv = t; return; }
  fresh();
}

/* ---------------------------------------------------------------- bikes, colours, gifts */
static int total_stars(void) {
  int s = 0;
  for (int i = 0; i < NLEVELS; i++) s += sv.stars[i];
  return s;
}
static int levels_done(void) {
  int s = 0;
  for (int i = 0; i < NLEVELS; i++) s += sv.stars[i] > 0;
  return s;
}
static int price(void) { int p = 600 + 50 * sv.draws; return p > 1200 ? 1200 : p; }
static bool scape_open(int k) { return k < 3 || (scape_unlock[k] && sv.challenges >= scape_unlock[k]); }
static const char *new_bike_msg;
static int new_bike = -1;
/* the special bikes that are earned: true if one came */
static bool check_specials(void) {
  bool got = false;
  for (int b = 0; b < NBIKES; b++) {
    const rd_bike *k = &rd_bikes[b];
    if (owned(b) || !k->rule) continue;
    uint32_t have = k->rule == 1 ? sv.challenges : k->rule == 2 ? sv.runs : k->rule == 3 ? sv.best : (uint32_t)total_stars();
    if (have >= k->val) own(b), new_bike = b, new_bike_msg = "New bike!", got = true;
  }
  return got;
}
/* a bike for gems: the first four of the classes the game fixes, then any not won yet */
static int win_bike(uint32_t *seed) {
  static const uint8_t first[4] = {0, 1, 0, 2};
  int pool[NBIKES], n = 0;
  for (int pass = 0; pass < 2 && !n; pass++)
    for (int b = 0; b < NBIKES; b++)
      if (!owned(b) && rd_bikes[b].cls < 3 && (pass || sv.draws >= 4 || rd_bikes[b].cls == first[sv.draws])) pool[n++] = b;
  if (!n) return -1;
  int b = pool[rd_rand(seed) % (uint32_t)n];
  own(b), sv.draws++;
  return b;
}
static const uint16_t gift_min[6] = {2, 4, 8, 15, 30, 60};
static const uint16_t gift_gems[6] = {160, 180, 200, 160, 180, 200};
static uint32_t gift_wait(void) { return gift_min[sv.gift_i < 6 ? sv.gift_i : 5] * 60000u; }
static int secret_left(void) {
  for (int b = 0; b < NBIKES; b++)
    if (rd_bikes[b].cls == 4 && !owned(b)) return b;
  return -1;
}

/* ---------------------------------------------------------------- challenges (challenges.json) */
enum { O_RUN, O_SCORE, O_FLIPS, O_GEMS, O_PERFECT, O_DOUBLE, O_BUY };
enum { M_TOTAL, M_ONE, M_RUNS, M_ROW };
enum { D_NONE, D_SCORE, D_TOPSCORE, D_GEMS, D_TOPGEMS, D_FLIPS, D_TOPFLIPS, D_PERFECT, D_TOPPERFECT };
typedef struct {
  uint8_t obj, mode, dyn, n, round, min;
  uint8_t between, exact;
  float x[6], y[6], z[6];
  const char *text;
} challenge;
static const challenge chs[26] = {
    {O_RUN, M_TOTAL, 0, 1, 1, 0, 0, 0, {3}, {0}, {0}, "Play %X runs"},
    {O_SCORE, M_TOTAL, 0, 1, 1, 0, 0, 0, {25}, {0}, {0}, "Score %X points in total"},
    {O_FLIPS, M_TOTAL, 0, 1, 1, 0, 0, 0, {4}, {0}, {0}, "Make %X flips in total"},
    {O_BUY, M_TOTAL, 0, 1, 1, 0, 0, 0, {1}, {0}, {0}, "Win a new bike"},
    {O_SCORE, M_ROW, D_SCORE, 1, 2, 2, 0, 0, {-5}, {0}, {2}, "Score %X+ points %Zx in a row"},
    {O_GEMS, M_RUNS, 0, 1, 1, 0, 0, 0, {4}, {0}, {2}, "Collect %X+ gems %Zx"},
    {O_GEMS, M_ONE, D_TOPGEMS, 5, 2, 2, 1, 0, {-1, 0, 1, 3, 5}, {4, 4, 3, 3, 2}, {0}, "Collect between %X and %Y gems in 1 run"},
    {O_SCORE, M_ROW, D_SCORE, 5, 1, 0, 1, 0, {0.8f, 0.9f, 1.0f, 1.1f, 1.2f}, {3, 4, 5, 4, 3}, {2, 3, 4, 5, 6}, "Score between %X and %Y points %Zx in a row"},
    {O_PERFECT, M_RUNS, D_PERFECT, 5, 1, 2, 0, 0, {-2, -1, 0, 2, 3}, {0}, {2, 3, 4, 5, 6}, "Make %X+ perfect flips in %Z runs"},
    {O_SCORE, M_ONE, D_TOPSCORE, 6, 5, 0, 0, 0, {0.9f, 0.95f, 1.0f, 1.05f, 1.1f, 1.15f}, {0}, {0}, "Score %X+ points in 1 run"},
    {O_GEMS, M_ONE, D_TOPGEMS, 5, 1, 0, 0, 0, {-1, 0, 1, 2, 3}, {0}, {0}, "Collect %X gems in 1 run"},
    {O_FLIPS, M_RUNS, D_FLIPS, 5, 1, 2, 0, 0, {-1, 0, 1, 2, 3}, {0}, {2, 3, 4, 5, 6}, "Make %X+ flips in %Z runs"},
    {O_SCORE, M_ONE, D_TOPSCORE, 5, 5, 0, 0, 1, {0.5f, 0.6f, 0.7f, 0.8f, 0.9f}, {0}, {0}, "Score exactly %X points in 1 run"},
    {O_GEMS, M_ROW, D_GEMS, 5, 1, 2, 0, 0, {0.5f, 0.6f, 0.7f, 0.8f, 0.9f}, {0}, {2, 3, 4, 5, 6}, "Collect %X+ gems %Zx in a row"},
    {O_PERFECT, M_ONE, D_TOPPERFECT, 5, 1, 2, 0, 0, {-2, -1, 0, 1, 2}, {0}, {0}, "Make %X+ perfect flips in 1 run"},
    {O_SCORE, M_ONE, D_TOPSCORE, 5, 5, 15, 1, 0, {-10, -5, 0, 5, 10}, {-5, 0, 5, 10, 5}, {0}, "Score between %X and %Y points in 1 run"},
    {O_SCORE, M_ROW, D_SCORE, 5, 5, 0, 0, 0, {0.8f, 0.9f, 1.0f, 1.1f, 1.2f}, {0}, {3, 4, 5, 6, 7}, "Score %X+ points %Zx in a row"},
    {O_GEMS, M_RUNS, D_GEMS, 4, 2, 2, 0, 1, {1.0f, 1.1f, 1.2f, 1.3f}, {0}, {3, 4, 5, 6}, "Collect exactly %X gems %Zx"},
    {O_SCORE, M_RUNS, D_TOPSCORE, 4, 5, 20, 0, 0, {-10, -5, 0, 5}, {0}, {3, 3, 4, 4}, "Score %X+ points %Zx"},
    {O_GEMS, M_RUNS, D_TOPGEMS, 4, 1, 0, 0, 0, {-2, -1, 0, 0}, {0}, {1, 2, 3, 4}, "Collect %X+ gems %Zx"},
    {O_FLIPS, M_ONE, D_TOPFLIPS, 5, 1, 2, 0, 0, {-2, -1, 0, 1, 2}, {0}, {0}, "Make %X+ flips in 1 run"},
    {O_GEMS, M_ONE, D_TOPGEMS, 5, 2, 2, 1, 0, {0, 1, 2, 3, 4}, {4, 4, 4, 4, 4}, {0}, "Collect between %X and %Y gems in 1 run"},
    {O_DOUBLE, M_TOTAL, 0, 4, 1, 0, 0, 0, {2, 3, 4, 5}, {0}, {0}, "Make %X double flips in total"},
    {O_GEMS, M_ONE, D_TOPGEMS, 4, 2, 0, 0, 1, {0.5f, 0.6f, 0.7f, 0.8f}, {0}, {0}, "Collect exactly %X gems in 1 run"},
    {O_SCORE, M_ROW, D_SCORE, 4, 5, 10, 0, 0, {-10, -5, 0, 5}, {0}, {2, 3, 4, 5}, "Score %X+ points %Zx in a row"},
    {O_GEMS, M_RUNS, D_GEMS, 4, 2, 2, 0, 1, {0, 1, 2, 3}, {0}, {2, 2, 3, 3}, "Collect exactly %X gems %Zx"},
};
#define NFIXED 6
static float stat(int d) {
  const uint16_t *top = d == D_TOPSCORE ? sv.top_score : d == D_TOPGEMS ? sv.top_gems : d == D_TOPFLIPS ? sv.top_flips : sv.top_perfect;
  if (d == D_TOPSCORE || d == D_TOPGEMS || d == D_TOPFLIPS || d == D_TOPPERFECT) {
    float s = 0;
    int n = 0;
    for (int i = 0; i < NTOP; i++)
      if (top[i]) s += top[i], n++;
    return n ? s / n : 0;
  }
  if (!sv.runs) return 0;
  uint32_t t = d == D_SCORE ? sv.total_score : d == D_GEMS ? sv.total_gems : d == D_FLIPS ? sv.total_flips : sv.total_perfect;
  return (float)t / sv.runs;
}
/* the game's getValueMultiplied: whole numbers are added to the average, fractions multiply it */
static int dyn_value(const challenge *c, float v) {
  if (!c->dyn) return (int)v;
  float s = stat(c->dyn), r = v == floorf(v) ? s + v : s * v;
  if (r <= 0) r = 1;
  r = floorf(r / c->round + 0.5f) * c->round;
  return r <= 0 ? 1 : (int)r;
}
/* the next challenge: the six first, then the others in turn, each a step further each time */
static void next_challenge(void) {
  int t = sv.challenges < NFIXED ? sv.challenges : -1;
  if (t < 0) {
    int start = NFIXED + (sv.challenges - NFIXED) % (26 - NFIXED);
    for (int k = 0; k < 26 - NFIXED && t < 0; k++) {
      int c = NFIXED + (start - NFIXED + k) % (26 - NFIXED);
      if (sv.ch_done[c] < chs[c].n) t = c;
    }
    if (t < 0) { sv.ch_tpl = 31; return; } /* all done */
  }
  const challenge *c = &chs[t];
  int i = sv.ch_done[t] < c->n ? sv.ch_done[t] : c->n - 1;
  sv.ch_tpl = (uint8_t)t, sv.ch_progress = 0, sv.ch_streak = 0;
  int x = dyn_value(c, c->x[i]);
  bool clamped = false;
  if (c->min && x < c->min) x = c->min, clamped = true;
  int y = c->between ? dyn_value(c, c->y[i]) : 0;
  if (c->between) {
    if (c->x[i] == floorf(c->x[i]) && c->dyn) y = x + (int)(c->y[i] > 0 ? c->y[i] : 2);
    if (clamped) y = x + 2;
    if (y < x + 2) y = x + 2;
  }
  sv.ch_x = (int16_t)x, sv.ch_y = (int16_t)y, sv.ch_z = (int16_t)c->z[i];
}
static char ch_text[64];
static const char *challenge_text(void) {
  if (sv.ch_tpl >= 26) return "All challenges done";
  const char *s = chs[sv.ch_tpl].text;
  char *o = ch_text, *e = ch_text + sizeof ch_text - 8;
  for (; *s && o < e; s++) {
    if (*s == '%' && s[1]) {
      int v = s[1] == 'X' ? sv.ch_x : s[1] == 'Y' ? sv.ch_y : sv.ch_z;
      o += snprintf(o, (size_t)(e - o), "%d", v), s++;
    } else *o++ = *s;
  }
  *o = 0;
  return ch_text;
}
static int ch_goal(void) {
  const challenge *c = &chs[sv.ch_tpl];
  return c->mode == M_TOTAL ? sv.ch_x : c->mode == M_ONE ? 1 : sv.ch_z;
}
static const char *reward_text;
static char reward_buf[48];
static void challenge_done(uint32_t *seed) {
  (void)seed;
  if (sv.ch_tpl < 26 && sv.ch_done[sv.ch_tpl] < 255) sv.ch_done[sv.ch_tpl]++;
  sv.challenges++;
  int bike = -1;
  for (int b = 0; b < NBIKES; b++)
    if (rd_bikes[b].rule == 1 && rd_bikes[b].val == sv.challenges && !owned(b)) bike = b;
  int scape = -1;
  for (int k = 0; k < 10; k++)
    if (scape_unlock[k] == sv.challenges) scape = k;
  if (bike >= 0) own(bike), new_bike = bike, new_bike_msg = "Challenge bike!", snprintf(reward_buf, sizeof reward_buf, "A new bike: %s", rd_bikes[bike].name);
  else if (scape >= 0) sv.scapes_on |= (uint16_t)(1 << scape), snprintf(reward_buf, sizeof reward_buf, "New colours!");
  else sv.gems += 200, snprintf(reward_buf, sizeof reward_buf, "+200 gems");
  reward_text = reward_buf;
  next_challenge();
}
/* a run's numbers against the challenge on */
static void challenge_run(int score, int gems, int flips, int perfect, int doubles, uint32_t *seed) {
  if (sv.ch_tpl >= 26) return;
  const challenge *c = &chs[sv.ch_tpl];
  int v = c->obj == O_SCORE ? score : c->obj == O_GEMS ? gems : c->obj == O_FLIPS ? flips : c->obj == O_PERFECT ? perfect : c->obj == O_DOUBLE ? doubles : 1;
  if (c->obj == O_BUY) return;
  if (c->mode == M_TOTAL) sv.ch_progress += v;
  else {
    bool ok = c->between ? v >= sv.ch_x && v <= sv.ch_y : c->exact ? v == sv.ch_x : v >= sv.ch_x;
    if (ok) sv.ch_progress++;
    else if (c->mode == M_ROW) sv.ch_progress = 0;
  }
  if (sv.ch_progress >= ch_goal()) challenge_done(seed);
}

/* ---------------------------------------------------------------- the screens */
enum { S_MENU, S_RIDE, S_LEVELS, S_BIKES, S_COLORS, S_HELP };
enum { OV_NONE, OV_PAUSE, OV_QUIT, OV_OVER, OV_DONE, OV_WON };
static rd_run run;
static int screen = S_MENU, overlay, sel, help_back, page, bike_sel, color_sel, level_sel;
static uint32_t seed, now;
static bool used_ok;   /* the button was pressed in this run */
static bool run_fresh; /* a run started this frame */
static bool new_best;
static int earned_stars, earned_gems, won_bike;
static float done_time;
static float menu_t;

/* a shape drawn straight into the strip on its way to the screen (over the panels) */
static void strip_poly(const float *xy, int n, color_t c) {
  for (int y = gfx_y0; y < gfx_y1; y++) {
    float yc = y + 0.5f, xs[24];
    int m = 0;
    for (int i = 0; i < n && m < 24; i++) {
      float ax = xy[2 * i], ay = xy[2 * i + 1], bx = xy[2 * ((i + 1) % n)], by = xy[2 * ((i + 1) % n) + 1];
      if ((ay <= yc) != (by <= yc)) xs[m++] = ax + (yc - ay) * (bx - ax) / (by - ay);
    }
    for (int i = 1; i < m; i++)
      for (int j = i; j > 0 && xs[j - 1] > xs[j]; j--) { float t = xs[j]; xs[j] = xs[j - 1], xs[j - 1] = t; }
    for (int i = 0; i + 1 < m; i += 2) {
      int a = (int)ceilf(xs[i] - 0.5f), b = (int)floorf(xs[i + 1] - 0.5f);
      if (a < 0) a = 0;
      if (b > 319) b = 319;
      for (int x = a; x <= b; x++) gfx_buf[(y - gfx_y0) * SCREEN_W + x] = c;
    }
  }
}
static void hud_star(float x, float y, float rad, color_t c) {
  float p[20];
  for (int i = 0; i < 10; i++) {
    float a = -1.5708f + i * 0.6283f, rr = i & 1 ? rad * 0.45f : rad;
    p[2 * i] = x + cosf(a) * rr, p[2 * i + 1] = y + sinf(a) * rr;
  }
  strip_poly(p, 10, c);
}
static void hud_gem(float x, float y, float h) {
  float o[8] = {x, y - h - 1.5f, x + h * 0.75f + 1.5f, y, x, y + h + 1.5f, x - h * 0.75f - 1.5f, y};
  float d[8] = {x, y - h, x + h * 0.75f, y, x, y + h, x - h * 0.75f, y};
  strip_poly(o, 4, pal[C_GEM_HALO]), strip_poly(d, 4, pal[C_GEM]);
}
static color_t W(void) { return HEX(0xFFFFFF); }
static color_t DIM(void) { return HEX(0x9AA7AC); }
static color_t ACC(void) { return HEX(0xFFE14D); }

static void text_c(const np_font_t *f, int cx, int base, const char *s, color_t c) { gfx_text_center(f, cx, base, s, c, 256); }
static void panel(int x, int y, int w, int h) {
  gfx_rrect(x, y, w, h, 8, HEX(0x000000), 190);
  gfx_rrect_outline(x, y, w, h, 8, 1, HEX(0x58666B), 256);
}
static void keyline(const char *s) {
  gfx_fill_alpha(0, 222, 320, 18, HEX(0x000000), 170);
  text_c(&np_font_small, 160, 235, s, DIM());
}
static void button(int x, int y, int w, int h, const char *s, bool on) {
  if (on) gfx_rrect(x, y, w, h, 6, ACC(), 256);
  else gfx_rrect_outline(x, y, w, h, 6, 1, HEX(0x7C8A90), 256);
  text_c(&np_font_body, x + w / 2, y + h / 2 + 5, s, on ? HEX(0x101010) : W());
}

static void start_run(int mode, int level) {
  int theme;
  if (mode == RUN_LEVEL) theme = level / 8;
  else {
    int on[10], n = 0;
    for (int k = 0; k < 10; k++)
      if (scape_open(k) && (sv.scapes_on >> k & 1)) on[n++] = k;
    theme = 6 + (n ? on[rd_rand(&seed) % (uint32_t)n] : 0);
  }
  run_start(&run, mode, level, sv.bike, theme, rd_rand(&seed));
  draw_theme(theme);
  screen = S_RIDE, overlay = OV_NONE, used_ok = false, run_fresh = true, new_best = false, reward_text = 0, new_bike = -1;
}

/* the end of an endless run: the numbers kept, challenges, bikes */
static void top_add(uint16_t *t, int v) {
  int k = NTOP - 1;
  if (v <= t[k]) return;
  while (k > 0 && t[k - 1] < v) t[k] = t[k - 1], k--;
  t[k] = (uint16_t)(v > 65535 ? 65535 : v);
}
static void run_over(void) {
  rd_run *r = &run;
  sv.runs++;
  if (r->mode == RUN_ENDLESS) {
    sv.gems += (uint32_t)r->gems;
    sv.total_score += (uint32_t)r->score, sv.total_flips += (uint32_t)r->flips, sv.total_perfect += (uint32_t)r->perfect;
    sv.total_gems += (uint32_t)r->gems, sv.total_doubles += (uint32_t)r->doubles;
    top_add(sv.top_score, r->score), top_add(sv.top_gems, r->gems), top_add(sv.top_flips, r->flips), top_add(sv.top_perfect, r->perfect);
    if (r->score > sv.best) sv.best = (uint16_t)r->score, new_best = true;
    challenge_run(r->score, r->gems, r->flips, r->perfect, r->doubles, &seed);
  } else {
    sv.gems += (uint32_t)r->gems;
  }
  check_specials();
  save();
}
static void level_done(void) {
  rd_run *r = &run;
  float t = run_level_time(r);
  int l = r->level, st = 1 + (t < star2[l] * 10.0f) + (t < star3[l] * 10.0f);
  done_time = t;
  earned_stars = st > sv.stars[l] ? st - sv.stars[l] : 0;
  earned_gems = earned_stars * group_reward[l / 8];
  if (st > sv.stars[l]) sv.stars[l] = (uint8_t)st;
  new_best = !sv.best_ms[l] || t < sv.best_ms[l];
  if (new_best) sv.best_ms[l] = (uint32_t)t;
  sv.gems += (uint32_t)(earned_gems + r->gems);
  check_specials();
  save();
}

/* ---------------------------------------------------------------- drawing the screens */
static char buf[96];
static void fmt_time(char *o, size_t n, float ms) {
  int cs = (int)(ms / 10);
  snprintf(o, n, "%d.%02d", cs / 100, cs % 100);
}

static void hud_ride(void) {
  rd_run *r = &run;
  if (r->mode == RUN_ENDLESS) {
    snprintf(buf, sizeof buf, "%d", r->score);
    text_c(&np_font_title, 160, 26, buf, W());
    if (r->pending) {
      snprintf(buf, sizeof buf, "+%d", r->pending);
      gfx_text(&np_font_body, 160 + gfx_text_width(&np_font_title, buf) / 2 + 14, 20, buf, ACC(), 256);
    }
    snprintf(buf, sizeof buf, "best %d", sv.best);
    text_c(&np_font_small, 160, 40, buf, DIM());
  } else {
    fmt_time(buf, sizeof buf, run_level_time(r));
    text_c(&np_font_title, 160, 26, buf, W());
    snprintf(buf, sizeof buf, "Level %d", r->level % 8 + 1);
    gfx_text(&np_font_small, 6, 14, group_name[r->level / 8], DIM(), 256);
    gfx_text(&np_font_small, 6, 26, buf, DIM(), 256);
  }
  snprintf(buf, sizeof buf, "%u", (unsigned)(sv.gems + (uint32_t)r->gems));
  gfx_text_right(&np_font_body, 302, 18, buf, W(), 256);
  hud_gem(310, 13, 6);
  if (r->text && r->text_ms > 0 && !r->ended) {
    int a = r->text_ms > 1250 ? (int)((1500 - r->text_ms) * 1.0f) : r->text_ms < 250 ? (int)r->text_ms : 256;
    a = a > 256 ? 256 : a < 0 ? 0 : a;
    gfx_text_center(&np_font_title, 160, 78, r->text, ACC(), a);
    if (r->text2) gfx_text_center(&np_font_body, 160, 96, r->text2, W(), a);
  }
  if (overlay == OV_NONE && (!used_ok || (sv.runs < 2 && r->ms < 12000 && r->mode == RUN_ENDLESS)))
    keyline("Hold OK: ride, and flip in the air   Back: pause");
  if (overlay == OV_PAUSE || overlay == OV_QUIT) {
    gfx_fill_alpha(0, 0, 320, 240, HEX(0x000000), 120);
    panel(80, 50, 160, 140);
    text_c(&np_font_title, 160, 80, "Paused", W());
    const char *it[3] = {"Resume", "How to play", overlay == OV_QUIT ? "Sure? OK quits" : "Quit game"};
    for (int i = 0; i < 3; i++) button(96, 94 + i * 30, 128, 24, it[i], sel == i);
    keyline("OK choose   Back resume");
  }
  if (overlay == OV_OVER) {
    gfx_fill_alpha(0, 0, 320, 240, HEX(0x000000), 110);
    panel(50, 46, 220, 166);
    text_c(&np_font_body, 160, 68, new_best ? "New best!" : "Score", new_best ? ACC() : DIM());
    snprintf(buf, sizeof buf, "%d", r->score);
    text_c(&np_font_title, 160, 96, buf, W());
    snprintf(buf, sizeof buf, "best %d     gems +%d", sv.best, r->gems);
    text_c(&np_font_small, 160, 114, buf, DIM());
    if (reward_text) text_c(&np_font_body, 160, 134, reward_text, ACC());
    else if (sv.ch_tpl < 26) {
      snprintf(buf, sizeof buf, "%s (%d/%d)", challenge_text(), (int)sv.ch_progress, ch_goal());
      text_c(&np_font_small, 160, 134, buf, DIM());
    }
    if (new_bike >= 0) {
      snprintf(buf, sizeof buf, "%s %s", new_bike_msg, rd_bikes[new_bike].name);
      text_c(&np_font_small, 160, 150, buf, ACC());
    }
    button(66, 166, 90, 26, "Ride again", sel == 0);
    button(164, 166, 90, 26, "Menu", sel == 1);
    keyline("OK choose   Back menu");
  }
  if (overlay == OV_DONE) {
    gfx_fill_alpha(0, 0, 320, 240, HEX(0x000000), 110);
    panel(50, 40, 220, 172);
    snprintf(buf, sizeof buf, "Level %d done", r->level % 8 + 1);
    text_c(&np_font_body, 160, 62, buf, W());
    fmt_time(buf, sizeof buf, done_time);
    text_c(&np_font_title, 160, 118, buf, W());
    char b2[24];
    fmt_time(b2, sizeof b2, (float)sv.best_ms[r->level]);
    snprintf(buf, sizeof buf, new_best ? "Best time!" : "best %s", b2);
    text_c(&np_font_small, 160, 134, buf, new_best ? ACC() : DIM());
    if (earned_gems) snprintf(buf, sizeof buf, "+%d gems", earned_gems), text_c(&np_font_small, 160, 150, buf, ACC());
    int st = 1 + (done_time < star2[r->level] * 10.0f) + (done_time < star3[r->level] * 10.0f);
    for (int i = 0; i < 3; i++) hud_star(124 + i * 36, 84, 13, W()), hud_star(124 + i * 36, 84, 10, i < st ? ACC() : HEX(0x30383B));
    const char *it[3] = {"Next", "Again", "Levels"};
    for (int i = 0; i < 3; i++) button(60 + i * 68, 164, 64, 26, it[i], sel == i);
    keyline("OK choose   Back levels");
  }
}

static void draw_ride(void) {
  draw_run(&run);
  draw_flush(hud_ride);
}

/* the menu: the endless run's start, still, behind it */
static const char *menu_items[5] = {"Play", "Levels", "Bikes", "Colours", "How to play"};
static bool gift_ready(void) { return sv.play_ms >= gift_wait(); }
static void hud_menu(void) {
  gfx_fill_alpha(0, 0, 320, 240, HEX(0x000000), 70);
  text_c(&np_font_title, 160, 36, "RIDER", W());
  snprintf(buf, sizeof buf, "best %d", sv.best);
  text_c(&np_font_small, 160, 52, buf, DIM());
  snprintf(buf, sizeof buf, "%u", (unsigned)sv.gems);
  gfx_text_right(&np_font_body, 302, 18, buf, W(), 256);
  hud_gem(310, 13, 6);
  for (int i = 0; i < 6; i++) {
    if (i == 5) {
      if (gift_ready()) snprintf(buf, sizeof buf, sv.gift_i % 5 == 4 && secret_left() >= 0 ? "Gift: a secret bike" : "Gift: %d gems", gift_gems[sv.gift_i < 6 ? sv.gift_i : 5]);
      else snprintf(buf, sizeof buf, "Next gift in %u min", (unsigned)((gift_wait() - sv.play_ms) / 60000 + 1));
      if (gift_ready()) button(90, 60 + i * 23, 140, 20, buf, sel == i);
      else text_c(&np_font_small, 160, 60 + i * 23 + 13, buf, DIM());
      continue;
    }
    button(110, 60 + i * 23, 100, 20, menu_items[i], sel == i);
  }
  if (reward_text) text_c(&np_font_small, 160, 204, reward_text, W());
  if (sv.ch_tpl < 26) snprintf(buf, sizeof buf, "Challenge %d: %s (%d/%d)", sv.challenges + 1, challenge_text(), (int)sv.ch_progress, ch_goal());
  else snprintf(buf, sizeof buf, "All challenges done");
  text_c(&np_font_small, 160, 217, buf, ACC());
  keyline("Up Down choose   OK go   Back quit");
}
static void draw_menu(void) {
  draw_run(&run);
  draw_flush(hud_menu);
}

static void hud_levels(void) {
  gfx_fill_alpha(0, 0, 320, 240, HEX(0x000000), 80);
  text_c(&np_font_title, 160, 30, group_name[page], W());
  snprintf(buf, sizeof buf, "%d / %d stars", total_stars(), NLEVELS * 3);
  text_c(&np_font_small, 160, 46, buf, DIM());
  bool open = levels_done() >= group_open[page];
  for (int i = 0; i < 8; i++) {
    int x = 22 + (i % 4) * 72, y = 64 + (i / 4) * 70, l = page * 8 + i;
    bool on = level_sel == i;
    if (on) gfx_rrect(x, y, 64, 60, 6, ACC(), 256);
    else gfx_rrect_outline(x, y, 64, 60, 6, 1, HEX(0x7C8A90), 256);
    snprintf(buf, sizeof buf, "%d", i + 1);
    text_c(&np_font_title, x + 32, y + 30, buf, on ? HEX(0x101010) : open ? W() : DIM());
    if (sv.best_ms[l]) fmt_time(buf, sizeof buf, (float)sv.best_ms[l]), text_c(&np_font_small, x + 32, y + 56, buf, on ? HEX(0x101010) : DIM());
  }
  if (!open) {
    int n = group_open[page] - levels_done();
    snprintf(buf, sizeof buf, n == 1 ? "Finish %d more level to open" : "Finish %d more levels to open", n);
    gfx_fill_alpha(20, 100, 280, 30, HEX(0x000000), 200);
    text_c(&np_font_body, 160, 120, buf, W());
  }
  keyline("Arrows choose   Left Right group   OK ride   Back menu");
}
static void draw_levels(void) {
  memset(fb, C_BG, sizeof fb);
  for (int i = 0; i < 8; i++) {
    int x = 22 + (i % 4) * 72, y = 64 + (i / 4) * 70, l = page * 8 + i;
    for (int k = 0; k < 3; k++) draw_star(x + 16 + k * 16, y + 40, 6, k < sv.stars[l] ? C_GEM : C_GROUND);
  }
  draw_flush(hud_levels);
}

/* bikes: the one chosen big at the top, all of them in a grid below, the first cell wins one with gems */
#define COLS 7
static const char *cls_name[5] = {"Common", "Rare", "Epic", "Special", "Secret"};
static void unlock_text(int b) {
  const rd_bike *k = &rd_bikes[b];
  if (k->rule == 1) snprintf(buf, sizeof buf, "Finish %d challenges (%d)", k->val, sv.challenges);
  else if (k->rule == 2) snprintf(buf, sizeof buf, "Play %d runs (%u)", k->val, (unsigned)sv.runs);
  else if (k->rule == 3) snprintf(buf, sizeof buf, "Score %d in the endless run (best %d)", k->val, sv.best);
  else if (k->rule == 4) snprintf(buf, sizeof buf, "Get %d level stars (%d)", k->val, total_stars());
  else if (k->cls == 4) snprintf(buf, sizeof buf, "Comes in a gift");
  else snprintf(buf, sizeof buf, "Win it with gems");
}
static int bike_scroll;
static void hud_bikes(void) {
  snprintf(buf, sizeof buf, "%u", (unsigned)sv.gems);
  gfx_text_right(&np_font_body, 302, 18, buf, W(), 256);
  hud_gem(310, 13, 6);
  if (bike_sel < 0) {
    text_c(&np_font_title, 160, 28, "Win a bike", W());
    snprintf(buf, sizeof buf, "OK: a new bike for %d gems", price());
    text_c(&np_font_small, 160, 48, buf, sv.gems >= (uint32_t)price() ? ACC() : DIM());
  } else {
    const rd_bike *k = &rd_bikes[bike_sel];
    bool have = owned(bike_sel);
    text_c(&np_font_body, 230, 24, have || k->cls < 3 ? k->name : "?", W());
    text_c(&np_font_small, 230, 38, cls_name[k->cls], DIM());
    if (have) text_c(&np_font_small, 230, 54, sv.bike == bike_sel ? "Riding it" : "OK: ride it", ACC());
    else unlock_text(bike_sel), text_c(&np_font_small, 200, 54, buf, DIM());
  }
  for (int i = 0; i < 4 * COLS; i++) {
    int idx = bike_scroll * COLS + i - 1;
    if (idx >= NBIKES) break;
    int x = 6 + (i % COLS) * 44, y = 68 + (i / COLS) * 38;
    bool on = idx == bike_sel;
    if (on) gfx_rrect_outline(x, y, 42, 36, 5, 2, ACC(), 256);
    else if (idx >= 0 && idx == sv.bike) gfx_rrect_outline(x, y, 42, 36, 5, 1, W(), 256);
    if (idx < 0) text_c(&np_font_small, x + 21, y + 30, "WIN", on ? ACC() : W()), hud_gem(x + 21, y + 12, 6);
  }
  if (overlay == OV_WON) {
    gfx_fill_alpha(0, 0, 320, 240, HEX(0x000000), 150);
    panel(60, 70, 200, 100);
    text_c(&np_font_body, 160, 92, "You won", DIM());
    text_c(&np_font_title, 160, 152, rd_bikes[won_bike].name, ACC());
  }
  keyline(overlay == OV_WON ? "OK ride it   Back keep looking" : "Arrows choose   OK ride or win   Back menu");
}
static void draw_bikes(void) {
  memset(fb, C_BG, sizeof fb);
  if (bike_sel >= 0) {
    draw_bike_at(bike_sel, 80, 32, 0, 1.1f, owned(bike_sel));
    if (!owned(bike_sel))
      for (int yy = 0; yy < 64; yy++)
        for (int xx = 0; xx < 170; xx++)
          if (fb[yy][xx] != C_BG) fb[yy][xx] = C_GROUND;
  }
  for (int i = 0; i < 4 * COLS; i++) {
    int idx = bike_scroll * COLS + i - 1;
    if (idx >= NBIKES) break;
    int x = 6 + (i % COLS) * 44, y = 68 + (i / COLS) * 38;
    if (idx < 0) continue;
    if (owned(idx)) draw_bike_at(idx, x + 21, y + 15, 0, 0.42f, 1);
    else {
      draw_bike_at(idx, x + 21, y + 15, 0, 0.42f, 0);
      /* not won yet: a dark shape */
      for (int yy = y; yy < y + 34 && yy < 240; yy++)
        for (int xx = x; xx < x + 42; xx++)
          if (fb[yy][xx] != C_BG) fb[yy][xx] = C_GROUND;
    }
  }
  if (overlay == OV_WON) {
    memset(fb, C_BG, sizeof fb);
    draw_bike_at(won_bike, 160, 116, 0, 1.3f, 1);
  }
  draw_flush(hud_bikes);
}

static void hud_colors(void) {
  text_c(&np_font_title, 160, 28, "Colours", W());
  text_c(&np_font_small, 160, 44, "The endless run picks from the ones on", DIM());
  for (int k = 0; k < 10; k++) {
    int x = 14 + (k % 5) * 60, y = 56 + (k / 5) * 78;
    bool on = color_sel == k;
    if (on) gfx_rrect_outline(x - 2, y - 2, 56, 74, 6, 2, ACC(), 256);
    const uint32_t *t = themes[6 + k];
    for (int i = 0; i < 4; i++) gfx_fill(x + 4, y + 4 + i * 10, 44, 9, gfx_rgb(i == 3 ? t[3] : t[i == 0 ? 2 : i == 1 ? 1 : 0]));
    if (!scape_open(k)) {
      gfx_fill_alpha(x, y, 52, 70, HEX(0x000000), 190);
      snprintf(buf, sizeof buf, "%d", scape_unlock[k]);
      text_c(&np_font_small, x + 26, y + 60, buf, DIM());
    } else text_c(&np_font_small, x + 26, y + 60, sv.scapes_on >> k & 1 ? "on" : "off", sv.scapes_on >> k & 1 ? ACC() : DIM());
  }
  if (!scape_open(color_sel)) {
    snprintf(buf, sizeof buf, "Opens after %d challenges", scape_unlock[color_sel]);
    text_c(&np_font_small, 160, 216, buf, DIM());
  }
  keyline("Arrows choose   OK on/off   Back menu");
}
static void draw_colors(void) {
  memset(fb, C_BG, sizeof fb);
  draw_flush(hud_colors);
}

static const char *help_lines[] = {
    "Hold OK to ride. The bike speeds up.",
    "In the air, holding turns it backwards.",
    "That's a flip. Let go so that it lands",
    "on both wheels. On its head, it breaks.",
    "Red saws break it too.",
    "",
    "Endless run: each landed flip is a point,",
    "both wheels down together a perfect flip.",
    "Levels: reach the end fast for 3 stars.",
    "Each flip takes half a second off.",
    "",
    "Gems win bikes. Runs, scores, stars and",
    "challenges open the special ones.",
};
static void hud_help(void) {
  gfx_fill_alpha(0, 0, 320, 240, HEX(0x000000), 150);
  text_c(&np_font_title, 160, 28, "How to play", W());
  for (unsigned i = 0; i < sizeof help_lines / sizeof *help_lines; i++) text_c(&np_font_small, 160, 52 + (int)i * 14, help_lines[i], W());
  keyline("OK or Back: go on   Home: save and quit");
}
static void draw_help(void) {
  if (help_back == S_RIDE) draw_run(&run);
  else memset(fb, C_BG, sizeof fb);
  draw_flush(hud_help);
}

/* ---------------------------------------------------------------- keys */
static void go_menu(void) {
  screen = S_MENU, overlay = OV_NONE, sel = 0;
  run_start(&run, RUN_ENDLESS, 0, sv.bike, 6, 7);
  draw_theme(6 + 0);
}

static void keys_ride(uint32_t hit) {
  bool ok = hit & (PK_OK | PK_EXE);
  if (overlay == OV_PAUSE || overlay == OV_QUIT) {
    if (hit & PK_UP) sel = (sel + 2) % 3, overlay = OV_PAUSE;
    if (hit & PK_DOWN) sel = (sel + 1) % 3, overlay = OV_PAUSE;
    if (hit & PK_BACK) overlay = OV_NONE;
    else if (ok) {
      if (sel == 0) overlay = OV_NONE;
      else if (sel == 1) help_back = S_RIDE, screen = S_HELP;
      else if (overlay == OV_QUIT) {
        if (run.mode == RUN_LEVEL) screen = S_LEVELS, overlay = OV_NONE;
        else go_menu();
      } else overlay = OV_QUIT;
    }
    return;
  }
  if (overlay == OV_OVER) {
    if (hit & (PK_LEFT | PK_RIGHT | PK_UP | PK_DOWN)) sel ^= 1;
    if (hit & PK_BACK || (ok && sel == 1)) go_menu();
    else if (ok) start_run(RUN_ENDLESS, 0);
    return;
  }
  if (overlay == OV_DONE) {
    if (hit & (PK_LEFT | PK_UP)) sel = (sel + 2) % 3;
    if (hit & (PK_RIGHT | PK_DOWN)) sel = (sel + 1) % 3;
    if (hit & PK_BACK || (ok && sel == 2)) screen = S_LEVELS, overlay = OV_NONE, page = run.level / 8, level_sel = run.level % 8;
    else if (ok) {
      int l = run.level + (sel == 0);
      if (l >= NLEVELS || levels_done() < group_open[l / 8]) l = run.level;
      start_run(RUN_LEVEL, l);
    }
    return;
  }
  if (hit & PK_BACK) overlay = OV_PAUSE, sel = 0;
}

static void keys_menu(uint32_t hit) {
  int n = gift_ready() ? 6 : 5;
  if (sel >= n) sel = 0;
  if (hit & PK_UP) sel = (sel + n - 1) % n, reward_text = 0;
  if (hit & PK_DOWN) sel = (sel + 1) % n, reward_text = 0;
  if (!(hit & (PK_OK | PK_EXE))) return;
  if (sel == 0) start_run(RUN_ENDLESS, 0);
  else if (sel == 1) screen = S_LEVELS, page = 0, level_sel = 0;
  else if (sel == 2) screen = S_BIKES, bike_sel = sv.bike, bike_scroll = (sv.bike + 1) / COLS > 1 ? (sv.bike + 1) / COLS - 1 : 0, overlay = OV_NONE;
  else if (sel == 3) screen = S_COLORS, color_sel = 0;
  else if (sel == 4) help_back = S_MENU, screen = S_HELP;
  else if (sel == 5 && gift_ready()) {
    int s = sv.gift_i % 5 == 4 ? secret_left() : -1;
    if (s >= 0) own(s), snprintf(reward_buf, sizeof reward_buf, "A secret bike: %s", rd_bikes[s].name);
    else sv.gems += gift_gems[sv.gift_i < 6 ? sv.gift_i : 5], snprintf(reward_buf, sizeof reward_buf, "+%d gems", gift_gems[sv.gift_i < 6 ? sv.gift_i : 5]);
    reward_text = reward_buf;
    sv.gift_i = (uint8_t)(sv.gift_i < 200 ? sv.gift_i + 1 : sv.gift_i), sv.play_ms = 0, sel = 0;
    save();
  }
}

static void keys_levels(uint32_t hit) {
  if (hit & PK_BACK) { go_menu(); sel = 1; return; }
  if (hit & PK_LEFT) { if (level_sel % 4 == 0) page = (page + 5) % 6, level_sel += 3; else level_sel--; }
  if (hit & PK_RIGHT) { if (level_sel % 4 == 3) page = (page + 1) % 6, level_sel -= 3; else level_sel++; }
  if (hit & (PK_UP | PK_DOWN)) level_sel ^= 4;
  if ((hit & (PK_OK | PK_EXE)) && levels_done() >= group_open[page]) start_run(RUN_LEVEL, page * 8 + level_sel);
}

static void keys_bikes(uint32_t hit) {
  if (overlay == OV_WON) {
    if (hit & (PK_OK | PK_EXE)) sv.bike = (uint8_t)won_bike, save();
    if (hit & (PK_OK | PK_EXE | PK_BACK)) overlay = OV_NONE, bike_sel = won_bike;
    return;
  }
  if (hit & PK_BACK) { go_menu(); sel = 2; return; }
  int i = bike_sel + 1;
  if (hit & PK_LEFT) i = i > 0 ? i - 1 : NBIKES;
  if (hit & PK_RIGHT) i = i < NBIKES ? i + 1 : 0;
  if (hit & PK_UP) i = i >= COLS ? i - COLS : i;
  if (hit & PK_DOWN) i = i + COLS <= NBIKES ? i + COLS : i;
  bike_sel = i - 1;
  while (i / COLS < bike_scroll) bike_scroll--;
  while (i / COLS > bike_scroll + 3) bike_scroll++;
  if (!(hit & (PK_OK | PK_EXE))) return;
  if (bike_sel < 0) {
    if (sv.gems >= (uint32_t)price()) {
      uint32_t p = (uint32_t)price();
      int b = win_bike(&seed);
      if (b >= 0) {
        sv.gems -= p, won_bike = b, overlay = OV_WON;
        if (sv.ch_tpl < 26 && chs[sv.ch_tpl].obj == O_BUY) challenge_done(&seed);
        check_specials();
        save();
      }
    }
  } else if (owned(bike_sel)) sv.bike = (uint8_t)bike_sel, save();
}

static void keys_colors(uint32_t hit) {
  if (hit & PK_BACK) { go_menu(); sel = 3; save(); return; }
  if (hit & PK_LEFT) color_sel = (color_sel + 9) % 10;
  if (hit & PK_RIGHT) color_sel = (color_sel + 1) % 10;
  if (hit & (PK_UP | PK_DOWN)) color_sel = (color_sel + 5) % 10;
  if ((hit & (PK_OK | PK_EXE)) && scape_open(color_sel)) {
    uint16_t m = (uint16_t)(sv.scapes_on ^ (1 << color_sel));
    bool any = false;
    for (int k = 0; k < 10; k++) any |= scape_open(k) && (m >> k & 1);
    if (any) sv.scapes_on = m;
  }
}

/* ---------------------------------------------------------------- the loop */
#ifndef HOST
__attribute__((used, externally_visible))
#endif
uint32_t perf_frames, perf_max, perf_slow;
static color_t strip[SCREEN_W * STRIP_H];

int main(void) {
  plat_begin();
  gfx_buf = strip;
  now = plat_millis();
  seed = plat_random() ^ now;
  if (!seed) seed = 1;
  load();
  if (sv.ch_tpl == 0 && sv.challenges == 0 && sv.ch_x == 0) next_challenge();
  go_menu();
  if (!(sv.flags & 1)) help_back = S_MENU, screen = S_HELP, sv.flags |= 1;
  uint32_t held = plat_keys(), last = plat_millis(), saved = last;
  float acc = 0;
  for (;;) {
    uint32_t k = plat_keys(), hit = k & ~held;
    held = k;
    if (k & PK_HOME) break;
    now = plat_millis();
    uint32_t dt = now - last;
    last = now;
    if (dt > 200) dt = 200;
    sv.play_ms += dt;
    menu_t += dt;
    int was_screen = screen;
    if (screen == S_RIDE && run_fresh) was_screen = -1, run_fresh = false; /* (the key that started it rides from the next frame) */
    switch (screen) {
    case S_MENU: keys_menu(hit); if (hit & PK_BACK) goto out; break;
    case S_RIDE: keys_ride(hit); break;
    case S_LEVELS: keys_levels(hit); break;
    case S_BIKES: keys_bikes(hit); break;
    case S_COLORS: keys_colors(hit); break;
    case S_HELP:
      if (hit & (PK_OK | PK_EXE | PK_BACK)) screen = help_back, overlay = help_back == S_RIDE ? OV_PAUSE : overlay;
      break;
    }
    if (screen == S_RIDE && run_fresh) was_screen = -1, run_fresh = false;
    if (screen == S_RIDE && overlay == OV_NONE && was_screen == S_RIDE) {
#ifdef HOST
      acc = 1000.0f / 60; /* (tests: one step a frame) */
#else
      acc += dt;
#endif
      bool ride = k & (PK_OK | PK_EXE | PK_UP);
      if (ride) used_ok = true;
      int steps = 0;
      while (acc >= 1000.0f / 60 && steps < 4) {
        acc -= 1000.0f / 60, steps++;
        int was = run.ended;
        run_step(&run, ride && used_ok);
        if (!was && run.ended == END_FINISH) level_done();
        if (!was && run.ended == END_CRASH) run_over();
      }
      if (acc > 100) acc = 0;
      if (run.ended == END_CRASH && run.end_ms > 1200) {
        if (run.mode == RUN_LEVEL) start_run(RUN_LEVEL, run.level);
        else overlay = OV_OVER, sel = 0;
      }
      if (run.ended == END_FINISH && run.end_ms > 600) overlay = OV_DONE, sel = 0;
    } else acc = 0;
    if (screen != S_RIDE && now - saved > 60000) save(), saved = now;
    uint32_t t0 = plat_millis();
    switch (screen) {
    case S_MENU: draw_menu(); break;
    case S_RIDE: draw_ride(); break;
    case S_LEVELS: draw_levels(); break;
    case S_BIKES: draw_bikes(); break;
    case S_COLORS: draw_colors(); break;
    case S_HELP: draw_help(); break;
    }
    uint32_t t = plat_millis() - t0;
    perf_frames++, perf_max = t > perf_max ? t : perf_max, perf_slow += t > 33;
    uint32_t spent = plat_millis() - now;
    if (spent < 16) plat_sleep(16 - spent);
  }
out:
  save();
  return plat_end();
}
