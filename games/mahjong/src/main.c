/* Mahjong: Mahjong solitaire, the Shanghai kind.
 *
 * 144 tiles stacked in a layout: dots, bamboo and characters 1 to 9, the four winds and the three
 * dragons, four of each, and the four flowers and four seasons, one of each. A tile is free when no
 * tile lies on it and its left or right side is open; two free tiles that match go: the same tile,
 * or any flower with any flower, any season with any season. Take them all away to win.
 *
 * Every deal can be won: the layout is played through first on blank tiles, two free tiles taken at
 * a time, and the faces are laid on those pairs, matching pair by pair, so taking them in that order
 * wins. Shuffling, when no two free tiles match, deals the tiles left the same way.
 *
 * The arrows jump from a free tile to the nearest free tile that way, so the cursor never lands on
 * a blocked one. The faces are drawn in tools/art.py, the layouts come from tools/layouts.py. The
 * screen is drawn in strips by NumPlay's launcher/src/gfx.c, which this app brings along. */
#include <eadk.h>
#include "gfx.h"
#include "../../common/np_text.h"
#include "../../common/np_lz.h"

#ifdef HOST /* (tests/play.c: the files are the test's) */
const uint8_t *host_read(const char *name, uint32_t *len);
bool host_write(const char *name, const void *data, uint32_t len);
#define file_read host_read
#define file_write host_write
#define np_app_begin()
#define np_app_end() 0
#else
#include "../../common/epsilon_app.h"
#include "../../common/epsilon_files.h"
#define file_read ef_read
#define file_write ef_write
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Mahjong";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif
void np_push(int x, int y, int w, int h, const color_t *px) {
  eadk_display_push_rect((eadk_rect_t){(uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h}, px);
}

#include "art.h"
#include "layouts.h"

#define KEY(k) (1ull << (k))
#define NT 144
#define TW 20   /* a tile's face, outline included */
#define TH 26
#define DEP 4   /* its thickness: each layer is drawn this much up and left */
#define BAR 224 /* the line of keys at the bottom */
#define NONE 255
#define FLOWER 34
#define SEASON 38
#define IVORY HEX(0xF5EFDD)
#define FELT 0x1E6B45
#define DEEP HEX(0x0E3524)
#define GOLD HEX(0xFFC93C)
#define FOCUS HEX(0xFF4A1C)

/* mahjong.sav, and its copy in mahjong_saves.py */
#define SAVE_NAME "mahjong.sav"
#define COPY_NAME "mahjong_saves.py"
static struct {
  uint8_t magic, version, flags, layout; /* flags: 1 a game to go on with, 2 how to play was read */
  uint8_t cur, pick, pairs, pad;         /* the tile at the cursor, the tile picked (NONE), pairs taken */
  uint32_t ms;                           /* the game's time */
  uint32_t best[LAYOUTS];                /* each layout's best time, 0: none yet */
  uint16_t wins[LAYOUTS];
  uint8_t face[NT];                      /* each tile's kind (0 to 41), + 128 once taken */
  uint8_t hist[NT];                      /* the tiles taken, two by two */
  uint32_t sum;                          /* FNV-1a of everything before it */
} sv;

static color_t strip[SCREEN_W * STRIP_H];
static uint8_t art[ART_RAW];
static color_t pal[16];
static uint32_t seed, now;

/* the layout: each tile's place in half tiles and its layer, and the tiles around it */
static uint8_t tx[NT], ty[NT], tz[NT];
static uint8_t nup[NT], ndn[NT], nlf[NT], nrt[NT];
static uint8_t up[NT][16], dn[NT][16], lf[NT][2], rt[NT][2]; /* on it, under it, beside it */
static int ox, oy;                                         /* where the layout is drawn */

/* the game */
static uint8_t fr[NT];           /* free tiles */
static int left, moves;          /* tiles left; pairs of free tiles that match */
static int hint_a = -1, hint_b = -1;
static uint32_t hint_t;
static int ov, menu, quit_ok, best_new;

static uint32_t rnd(void) {
  seed ^= seed << 13;
  seed ^= seed >> 17;
  return seed ^= seed << 5;
}
static int iabs(int v) { return v < 0 ? -v : v; }
static bool gone(int i) { return sv.face[i] >> 7; }
static int kind(int i) { return sv.face[i] & 127; }
static int family(int k) { return k < FLOWER ? k : k < SEASON ? FLOWER : SEASON; }
static bool match(int a, int b) { return family(kind(a)) == family(kind(b)); }
static int sx(int i) { return ox + tx[i] * TW / 2 - tz[i] * DEP; }
static int sy(int i) { return oy + ty[i] * TH / 2 - tz[i] * DEP; }

static void load_layout(int l) {
  const uint8_t *p = layout_tiles[l];
  int x0 = 999, x1 = -999, y0 = 999, y1 = -999;
  for (int i = 0; i < NT; i++) tx[i] = p[2 * i], ty[i] = p[2 * i + 1] & 15, tz[i] = p[2 * i + 1] >> 4;
  for (int i = 0; i < NT; i++) {
    nup[i] = ndn[i] = nlf[i] = nrt[i] = 0;
    for (int j = 0; j < NT; j++) {
      int dx = tx[j] - tx[i], dy = iabs(ty[j] - ty[i]);
      if (j == i || dy > 1) continue;
      if (iabs(dx) < 2 && tz[j] > tz[i] && nup[i] < 16) up[i][nup[i]++] = (uint8_t)j;
      if (iabs(dx) < 2 && tz[j] < tz[i] && ndn[i] < 16) dn[i][ndn[i]++] = (uint8_t)j;
      if (tz[j] == tz[i] && dx == -2 && nlf[i] < 2) lf[i][nlf[i]++] = (uint8_t)j;
      if (tz[j] == tz[i] && dx == 2 && nrt[i] < 2) rt[i][nrt[i]++] = (uint8_t)j;
    }
    int x = tx[i] * TW / 2 - tz[i] * DEP, y = ty[i] * TH / 2 - tz[i] * DEP;
    x0 = x < x0 ? x : x0, x1 = x > x1 ? x : x1, y0 = y < y0 ? y : y0, y1 = y > y1 ? y : y1;
  }
  ox = (SCREEN_W - (x1 - x0 + TW + DEP)) / 2 - x0;
  oy = (BAR - (y1 - y0 + TH + DEP)) / 2 - y0;
}

/* whether tile a is free among the tiles marked in `in` */
static bool open_in(const uint8_t *in, int a) {
  for (int k = 0; k < nup[a]; k++)
    if (in[up[a][k]]) return false;
  bool l = false, r = false;
  for (int k = 0; k < nlf[a]; k++) l |= in[lf[a][k]] != 0;
  for (int k = 0; k < nrt[a]; k++) r |= in[rt[a][k]] != 0;
  return !(l && r);
}

/* ------------------------------------------------------------ deals that can be won */
/* The game played through first, on blank tiles: two free tiles taken at a time, at random, until none
   are left (or only one is free: then again). order: the tiles in the order they were taken. */
static uint8_t order[NT];
static bool play_through(const uint8_t *want, int n) {
  uint8_t on[NT], fr_[NT];
  for (int i = 0; i < NT; i++) on[i] = want[i];
  for (int done = 0; done < n; done += 2) {
    int nf = 0;
    for (int t = 0; t < NT; t++)
      if (on[t] && open_in(on, t)) fr_[nf++] = (uint8_t)t;
    if (nf < 2) return false;
    int a = (int)(rnd() % (uint32_t)nf), b = (int)(rnd() % (uint32_t)(nf - 1));
    b += b >= a;
    order[done] = fr_[a], order[done + 1] = fr_[b], on[fr_[a]] = on[fr_[b]] = 0;
  }
  return true;
}

/* the faces of the tiles of `want` dealt so the game can be won from there: paired as they match, the pairs
   given in a random order to the pairs play_through() took; false if those tiles can't be won at all */
static bool deal(const uint8_t *want, uint8_t *faces, int n) {
  uint8_t pairs[NT];
  for (int i = 1; i < n; i++) /* by family, so neighbours match */
    for (int j = i; j > 0 && family(faces[j - 1]) > family(faces[j]); j--) {
      uint8_t f = faces[j];
      faces[j] = faces[j - 1], faces[j - 1] = f;
    }
  for (int i = n / 2 - 1; i > 0; i--) { /* the pairs in a random order */
    int j = (int)(rnd() % (uint32_t)(i + 1));
    for (int k = 0; k < 2; k++) {
      uint8_t f = faces[2 * i + k];
      faces[2 * i + k] = faces[2 * j + k], faces[2 * j + k] = f;
    }
  }
  for (int i = 0; i < n / 2; i++) pairs[2 * i] = faces[2 * i + (rnd() & 1)], pairs[2 * i + 1] = faces[2 * i] ^ faces[2 * i + 1] ^ pairs[2 * i];
  for (int tries = 0; tries < 200; tries++)
    if (play_through(want, n)) {
      for (int i = 0; i < n; i++) sv.face[order[i]] = pairs[i];
      return true;
    }
  return false;
}

/* ------------------------------------------------------------ the game */
static int dist2(int a, int b) {
  int dx = sx(a) - sx(b), dy = sy(a) - sy(b);
  return dx * dx + dy * dy;
}
/* the free tiles and the moves left; the cursor kept on a free tile, the nearest one if its own went */
static void refresh(void) {
  uint8_t on[NT];
  left = moves = 0;
  for (int i = 0; i < NT; i++) on[i] = !gone(i), left += on[i];
  for (int i = 0; i < NT; i++) fr[i] = on[i] && open_in(on, i);
  for (int a = 0; a < NT; a++)
    for (int b = a + 1; b < NT && fr[a]; b++) moves += fr[b] && match(a, b);
  if (sv.cur >= NT || !fr[sv.cur]) {
    int best = -1, c = sv.cur < NT ? sv.cur : 0;
    for (int i = 0; i < NT; i++)
      if (fr[i] && (best < 0 || dist2(i, c) < dist2(best, c))) best = i;
    sv.cur = (uint8_t)(best < 0 ? 0 : best);
  }
  if (sv.pick != NONE && (sv.pick >= NT || !fr[sv.pick])) sv.pick = NONE;
}

static uint32_t fnv(const void *p, uint32_t n) {
  uint32_t h = 2166136261u;
  for (uint32_t i = 0; i < n; i++) h = (h ^ ((const uint8_t *)p)[i]) * 16777619u;
  return h;
}
#define SUMMED (uint32_t)((uint8_t *)&sv.sum - (uint8_t *)&sv)

/* the copy: the save in base 64, in a comment of a script (installing apps keeps only scripts) */
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char head[] = "# Mahjong keeps a copy of your game here,\n# so that installing it again doesn't erase it.\n#>" SAVE_NAME ":";
static uint8_t copy[1 + sizeof head + (sizeof sv + 2) / 3 * 4 + 1];
static void save(void) {
  sv.magic = 'M', sv.version = 1, sv.sum = fnv(&sv, SUMMED);
  file_write(SAVE_NAME, &sv, sizeof sv);
  const uint8_t *s = (const uint8_t *)&sv;
  uint8_t *p = copy;
  *p++ = 0; /* (a script's first byte: not imported) */
  for (const char *h = head; *h;) *p++ = (uint8_t)*h++;
  for (uint32_t i = 0; i < sizeof sv; i += 3, p += 4) {
    uint32_t v = (uint32_t)s[i] << 16 | (i + 1 < sizeof sv ? s[i + 1] << 8 : 0) | (i + 2 < sizeof sv ? s[i + 2] : 0);
    for (int k = 0; k < 4; k++) p[k] = (uint8_t)(i + k - 1 < sizeof sv || k < 2 ? b64[v >> (18 - 6 * k) & 63] : '=');
  }
  *p++ = '\n';
  file_write(COPY_NAME, copy, (uint32_t)(p - copy));
}
static bool valid(void) {
  if (sv.magic != 'M' || sv.version != 1 || sv.sum != fnv(&sv, SUMMED) || sv.layout >= LAYOUTS) return false;
  int count[ART_KINDS] = {0}, taken = 0;
  for (int i = 0; i < NT; i++) {
    if (kind(i) >= ART_KINDS) return false;
    count[kind(i)]++, taken += gone(i);
  }
  if (!(sv.flags & 1)) return true; /* no game: only the times and how to play matter */
  for (int k = 0; k < ART_KINDS; k++)
    if (count[k] != (k < FLOWER ? 4 : 1)) return false;
  if (sv.pairs * 2 != taken || taken >= NT) return false;
  for (int i = 0; i < taken; i++)
    if (sv.hist[i] >= NT || !gone(sv.hist[i])) return false;
  return true;
}
static void load(void) {
  uint32_t n = 0;
  const uint8_t *d = file_read(SAVE_NAME, &n);
  uint8_t *v = (uint8_t *)&sv;
  for (uint32_t i = 0; i < sizeof sv; i++) v[i] = d && n == sizeof sv ? d[i] : 0;
  if (valid()) return;
  d = file_read(COPY_NAME, &n); /* the copy, after an install */
  uint32_t o = 0, at = sizeof head; /* (the 0, then the head without its own 0) */
  for (uint32_t i = 0; d && n >= at + (sizeof sv + 2) / 3 * 4 && i < (sizeof sv + 2) / 3 * 4; i += 4) {
    uint32_t w = 0;
    for (int k = 0; k < 4; k++) {
      const char *c = b64;
      while (*c && *c != d[at + i + k]) c++;
      if (!*c && d[at + i + k] != '=') o = sizeof sv + 1; /* not base 64: not the copy */
      w = w << 6 | (uint32_t)(*c ? c - b64 : 0);
    }
    for (int k = 0; k < 3 && o < sizeof sv; k++) v[o++] = (uint8_t)(w >> (16 - 8 * k));
  }
  if (o == sizeof sv && valid()) return;
  for (uint32_t i = 0; i < sizeof sv; i++) v[i] = 0; /* neither: a fresh start */
}

static void new_game(int l) {
  uint8_t want[NT], faces[NT];
  sv.layout = (uint8_t)l;
  load_layout(l);
  for (int i = 0; i < NT; i++) want[i] = 1, faces[i] = (uint8_t)(i < 4 * FLOWER ? i / 4 : i - 3 * FLOWER);
  deal(want, faces, NT);
  sv.ms = 0, sv.pairs = 0, sv.pick = NONE, sv.flags |= 1, sv.cur = NT;
  hint_a = hint_b = -1;
  refresh();
  /* the cursor on the free tile nearest the middle */
  int c = -1, cd = 0;
  for (int i = 0; i < NT; i++) {
    int dx = sx(i) + TW / 2 - SCREEN_W / 2, dy = sy(i) + TH / 2 - BAR / 2, d = dx * dx + dy * dy;
    if (fr[i] && (c < 0 || d < cd)) c = i, cd = d;
  }
  sv.cur = (uint8_t)c;
  save();
}

enum { OV_NONE, OV_MENU, OV_NEW, OV_HELP, OV_WON, OV_STUCK };

static void after_move(void) {
  hint_a = hint_b = -1;
  refresh();
  if (!left) {
    sv.flags &= (uint8_t)~1;
    best_new = !sv.best[sv.layout] || sv.ms < sv.best[sv.layout];
    if (best_new) sv.best[sv.layout] = sv.ms;
    if (sv.wins[sv.layout] < 65535) sv.wins[sv.layout]++;
    ov = OV_WON, menu = 0;
  } else if (!moves) {
    ov = OV_STUCK, menu = 0;
  }
  save();
}

/* OK on tile t: picks it, or takes it with the tile picked if they match */
static void choose(int t) {
  if (t >= NT || !fr[t]) return;
  if (sv.pick == t) {
    sv.pick = NONE;
  } else if (sv.pick != NONE && match(sv.pick, t)) {
    sv.face[sv.pick] |= 128, sv.face[t] |= 128;
    sv.hist[2 * sv.pairs] = sv.pick, sv.hist[2 * sv.pairs + 1] = (uint8_t)t, sv.pairs++;
    sv.pick = NONE;
    after_move();
  } else {
    sv.pick = (uint8_t)t;
  }
}

static void undo(void) {
  if (!sv.pairs) return;
  sv.pairs--;
  int a = sv.hist[2 * sv.pairs], b = sv.hist[2 * sv.pairs + 1];
  sv.face[a] &= 127, sv.face[b] &= 127, sv.pick = NONE, sv.cur = (uint8_t)b;
  after_move();
}

/* a pair of free tiles that match, the next one each time */
static void hint(void) {
  int start = hint_a < 0 ? 0 : hint_a * NT + hint_b + 1;
  for (int n = 0; n < NT * NT; n++) {
    int p = (start + n) % (NT * NT), a = p / NT, b = p % NT;
    if (a < b && fr[a] && fr[b] && match(a, b)) {
      hint_a = a, hint_b = b, hint_t = now;
      return;
    }
  }
  ov = OV_STUCK, menu = 0;
}

static void shuffle(void) {
  uint8_t want[NT], faces[NT];
  int n = 0;
  for (int i = 0; i < NT; i++)
    if ((want[i] = !gone(i))) faces[n++] = (uint8_t)kind(i);
  if (!deal(want, faces, n)) { /* no order of these tiles can be won: mixed anyway */
    for (int i = n - 1; i > 0; i--) {
      int j = (int)(rnd() % (uint32_t)(i + 1));
      uint8_t f = faces[i];
      faces[i] = faces[j], faces[j] = f;
    }
    for (int i = 0, k = 0; i < NT; i++)
      if (want[i]) sv.face[i] = faces[k++];
  }
  sv.pick = NONE;
  after_move();
}

/* the arrows: the nearest free tile that way, first among those within 45 degrees of it */
static void step(int dx, int dy) {
  int best = -1, bs = 0, c = sv.cur;
  for (int j = 0; j < NT; j++) {
    if (!fr[j] || j == c) continue;
    int ex = sx(j) - sx(c), ey = sy(j) - sy(c), along = dx ? ex * dx : ey * dy, side = iabs(dx ? ey : ex);
    if (along <= 0) continue;
    int s = along + 2 * side + (side > along ? 10000 : 0);
    if (best < 0 || s < bs) best = j, bs = s;
  }
  if (best >= 0) sv.cur = (uint8_t)best;
}

/* ------------------------------------------------------------ drawing */
static char *num(char *o, uint32_t v) { /* v in o; the end of it */
  char t[10];
  int n = 0;
  do t[n++] = (char)('0' + v % 10); while (v /= 10);
  while (n) *o++ = t[--n];
  *o = 0;
  return o;
}
static char *clock_text(char *o, uint32_t ms) { /* 4:05, or 1:04:05 */
  uint32_t s = ms / 1000;
  char *e = o;
  if (s >= 3600) e = num(e, s / 3600), *e++ = ':', *e++ = (char)('0' + s / 600 % 6), *e++ = (char)('0' + s / 60 % 10);
  else e = num(e, s / 60);
  *e++ = ':', *e++ = (char)('0' + s % 60 / 10), *e++ = (char)('0' + s % 10), *e = 0;
  return o;
}
static char *cat(char *o, const char *s) {
  while (*s) *o++ = *s++;
  *o = 0;
  return o;
}

static void face_art(int k, int x, int y) {
  const uint8_t *a = art + k * (ART_W / 2 * ART_H);
  int r0 = y < gfx_y0 ? gfx_y0 - y : 0, r1 = y + ART_H > gfx_y1 ? gfx_y1 - y : ART_H;
  for (int r = r0; r < r1; r++) {
    color_t *o = gfx_buf + (y + r - gfx_y0) * SCREEN_W + x;
    const uint8_t *s = a + r * (ART_W / 2);
    for (int c = 0; c < ART_W; c += 2) {
      int v = s[c >> 1];
      if (v & 15) o[c] = pal[v & 15];
      if (v >> 4) o[c + 1] = pal[v >> 4];
    }
  }
}

static void box(int x, int y, int w, int h, int t, color_t c) {
  gfx_fill(x, y, w, t, c), gfx_fill(x, y + h - t, w, t, c);
  gfx_fill(x, y + t, t, h - 2 * t, c), gfx_fill(x + w - t, y + t, t, h - 2 * t, c);
}

/* a tile: its sides (ivory, then the green back), its face, lighter the higher it lies, and its picture */
static const uint32_t layer_face[6] = {0xD9CFB4, 0xE6DDC6, 0xF1EAD7, 0xF8F3E6, 0xFFFDF6, 0xFFFFFF};
static void tile(int i) {
  int x = sx(i), y = sy(i), z = tz[i];
  if (y >= gfx_y1 || y + TH + DEP + 3 <= gfx_y0) return; /* (its shadow included) */
  static const uint32_t sides[DEP] = {0xCDBF9C, 0xB9AA86, 0x2E8A57, 0x1D603B};
  gfx_fill_alpha(x + TW + DEP, y + DEP + 2, 3, TH - 1, 0, 70); /* its shadow, on what lies below and right */
  gfx_fill_alpha(x + DEP + 2, y + TH + DEP, TW + 1, 3, 0, 70);
  for (int k = DEP; k >= 1; k--) {
    color_t c = HEX(sides[k - 1]);
    gfx_fill(x + TW - 1 + k, y + k, 1, TH - 1, c);
    gfx_fill(x + k, y + TH - 1 + k, TW - 1, 1, c);
  }
  bool picked = sv.pick == i, hinted = (i == hint_a || i == hint_b) && (now - hint_t) % 700 < 450;
  color_t edge = HEX(0x8E8368);
  gfx_fill(x + 1, y, TW - 2, 1, edge), gfx_fill(x + 1, y + TH - 1, TW - 2, 1, edge);
  gfx_fill(x, y + 1, 1, TH - 2, edge), gfx_fill(x + TW - 1, y + 1, 1, TH - 2, edge);
  gfx_fill(x + 1, y + 1, TW - 2, TH - 2, picked ? GOLD : hinted ? HEX(0x8EE89A) : gfx_rgb(layer_face[z > 5 ? 5 : z]));
  face_art(kind(i), x + 1, y + 1);
}


static const char *const cards[6][6] = {
  {0},
  {T("Paused"), T("Resume"), T("New game"), T("How to play"), T("Quit game")},
  {T("New game"), 0, 0, 0, 0},
  {T("How to play"), T("Play")},
  {T("You win!"), T("New game"), T("Quit game")},
  {T("No moves left"), T("Shuffle"), T("Undo"), T("New game")},
};
static const uint8_t buttons[6] = {0, 4, LAYOUTS, 1, 2, 3};

static void overlay(void) {
  char s[24];
  int n = buttons[ov], extra = ov == OV_HELP ? 92 : ov == OV_WON ? 48 : ov == OV_STUCK ? 22 : 0;
  int h = 52 + extra + 30 * n, y = (SCREEN_H - h) / 2;
  gfx_fill_alpha(0, 0, SCREEN_W, SCREEN_H, 0, 130);
  gfx_rrect(50, y, 220, h, 12, DEEP, 256);
  gfx_rrect_outline(50, y, 220, h, 12, 2, HEX(0xCDBF9C), 256);
  gfx_text_center(&np_font_title, 160, y + 32, cards[ov][0], IVORY, 256);
  y += 44;
  if (ov == OV_HELP)
    gfx_paragraph(&np_font_body, 160, y + 10, 196, 15,
                  T("Take the tiles away in pairs that match. A tile is free when nothing lies on it and its left or "
                    "right side is open. Any flower goes with any flower, any season with any season."),
                  IVORY, 256);
  if (ov == OV_WON) {
    gfx_text_center(&np_font_title, 160, y + 20, clock_text(s, sv.ms), GOLD, 256);
    if (best_new) cat(s, T("New best!"));
    else clock_text(cat(s, T("Best ")), sv.best[sv.layout]);
    gfx_text_center(&np_font_body, 160, y + 40, s, IVORY, 256);
  }
  if (ov == OV_STUCK) gfx_text_center(&np_font_body, 160, y + 12, T("No two free tiles match."), IVORY, 256);
  y += extra;
  for (int i = 0; i < n; i++, y += 30) {
    bool on = menu == i;
    const char *t = ov == OV_NEW ? layout_name[i] : ov == OV_MENU && i == 3 && quit_ok ? T("OK again to quit") : cards[ov][i + 1];
    gfx_rrect(66, y, 188, 26, 7, on ? GOLD : HEX(0x2B6E4C), 256);
    if (ov == OV_NEW) {
      gfx_text(&np_font_body, 80, y + 18, t, on ? DEEP : IVORY, 256);
      if (sv.best[i]) gfx_text_right(&np_font_body, 240, y + 18, clock_text(s, sv.best[i]), on ? DEEP : GOLD, 256);
    } else {
      gfx_text_center(&np_font_body, 160, y + 18, t, on ? DEEP : IVORY, 256);
    }
  }
}

static void scene(void *ctx) {
  char s[24];
  gfx_vgrad(0, 0, SCREEN_W, BAR, 0x23774D, 0x14512F);
  gfx_fill(0, BAR, SCREEN_W, SCREEN_H - BAR, DEEP);
  for (int i = 0; i < NT && sv.flags & 1; i++)
    if (!gone(i)) tile(i);
  for (int h = 0; h < 2 && hint_a >= 0 && (now - hint_t) % 700 < 450; h++) { /* the hint's two tiles */
    int t = h ? hint_b : hint_a;
    box(sx(t) - 2, sy(t) - 2, TW + 4, TH + 4, 2, HEX(0x1FBF4A));
  }
  if (sv.flags & 1 && sv.cur < NT && !gone(sv.cur)) { /* the cursor: a frame no one can miss */
    int x = sx(sv.cur), y = sy(sv.cur);
    box(x - 3, y - 3, TW + 6, TH + 6, 3, FOCUS);
    box(x - 1, y - 1, TW + 2, TH + 2, 1, 0xFFFF);
  }
  /* the time, the tiles left, the keys */
  gfx_text(&np_font_body, 6, BAR + 12, clock_text(s, sv.ms), GOLD, 256);
  cat(num(s, (uint32_t)left), T(" tiles"));
  gfx_text(&np_font_small, 52, BAR + 12, s, HEX(0xA9C9B5), 256);
  gfx_text_right(&np_font_small, 315, BAR + 12, T("OK pick  shift hint  DEL undo  Back menu"), IVORY, 256);
  if (ov) overlay();
}

#ifdef PERF /* (make perf: how long a frame takes, in tools/emu.py) */
#define PERF_SYM __attribute__((used, externally_visible, visibility("default")))
PERF_SYM uint32_t perf_frames, perf_max, perf_slow;
#endif
static void draw(void) {
  eadk_display_wait_for_vblank();
#ifdef PERF
  uint32_t t0 = (uint32_t)eadk_timing_millis();
#endif
  gfx_render(scene, 0, 0, SCREEN_H);
#ifdef PERF
  uint32_t t = (uint32_t)eadk_timing_millis() - t0;
  perf_frames++, perf_max = t > perf_max ? t : perf_max, perf_slow += t > 33;
#endif
}

int main(void) {
  np_app_begin();
  color_t *their_strip = gfx_buf;
  gfx_buf = strip;
  np_lz_unpack(art_lz, sizeof art_lz, art, sizeof art);
  for (int i = 0; i < 16; i++) pal[i] = gfx_rgb(art_pal[i]);
  now = (uint32_t)eadk_timing_millis();
  seed = eadk_random() ^ now;
  if (!seed) seed = 1;
  ov = menu = quit_ok = best_new = 0, hint_a = hint_b = -1;
  load();
  load_layout(sv.layout);
  refresh();
  if (!(sv.flags & 1)) ov = OV_NEW, menu = sv.layout;
  if (!(sv.flags & 2)) ov = OV_HELP, sv.flags |= 2;
  if ((sv.flags & 1) && !moves) ov = OV_STUCK;
  uint64_t held = eadk_keyboard_scan(), stale = held; /* keys still down from the home screen wait to be let go */
  const uint64_t arrows = KEY(eadk_key_left) | KEY(eadk_key_right) | KEY(eadk_key_up) | KEY(eadk_key_down) |
                          KEY(eadk_key_four) | KEY(eadk_key_six) | KEY(eadk_key_eight) | KEY(eadk_key_two);
  uint32_t tick = now, repeat_at = 0, shown = ~0u;
  bool dirty = true, lit = false;
  for (;;) {
    now = (uint32_t)eadk_timing_millis();
    uint64_t k = eadk_keyboard_scan(), hit = k & ~held, rep = k & arrows & ~(stale &= k);
    if (hit & arrows) repeat_at = now + 250; /* an arrow held repeats */
    else if (rep && (int32_t)(now - repeat_at) >= 0) hit |= rep, repeat_at = now + 110;
    held = k;
    if (k & (KEY(eadk_key_home) | KEY(eadk_key_on_off))) break;
    int dx = !!(hit & (KEY(eadk_key_right) | KEY(eadk_key_six))) - !!(hit & (KEY(eadk_key_left) | KEY(eadk_key_four)));
    int dy = !!(hit & (KEY(eadk_key_down) | KEY(eadk_key_two))) - !!(hit & (KEY(eadk_key_up) | KEY(eadk_key_eight)));
    bool ok = hit & (KEY(eadk_key_ok) | KEY(eadk_key_exe)), back = hit & KEY(eadk_key_back);
    if (hit) dirty = true;
    if (ov) {
      int n = buttons[ov], was = ov;
      if (dy) menu = (menu + dy + n) % n, quit_ok = 0;
      if (back) {
        if (ov == OV_NEW && !(sv.flags & 1)) break; /* no game to go back to */
        if (ov == OV_HELP && !(sv.flags & 1)) ov = OV_NEW, menu = sv.layout;
        else if (ov != OV_WON) ov = OV_NONE;
      } else if (ok) {
        int i = menu;
        ov = OV_NONE, menu = 0;
        if (was == OV_MENU) {
          if (i == 1) ov = OV_NEW, menu = sv.layout;
          if (i == 2) ov = OV_HELP;
          if (i == 3 && quit_ok) break;
          if (i == 3) ov = OV_MENU, menu = 3, quit_ok = 1; /* Quit game asks first */
        } else if (was == OV_NEW) {
          new_game(i);
        } else if (was == OV_HELP) {
          if (!(sv.flags & 1)) ov = OV_NEW, menu = sv.layout;
        } else if (was == OV_WON) {
          if (i == 0) ov = OV_NEW, menu = sv.layout;
          else break;
        } else if (was == OV_STUCK) {
          if (i == 0) shuffle();
          if (i == 1) undo();
          if (i == 2) ov = OV_NEW, menu = sv.layout;
        }
      }
    } else if (back) {
      ov = OV_MENU, menu = quit_ok = 0;
      save();
    } else if (sv.flags & 1) {
      if (dx) step(dx, 0);
      if (dy) step(0, dy);
      if (ok) choose(sv.cur);
      if (hit & KEY(eadk_key_shift)) hint();
      if (hit & KEY(eadk_key_backspace)) undo();
    }
    /* the clock runs while a game is played */
    if (!ov && sv.flags & 1) sv.ms += now - tick;
    tick = now;
    if (hint_a >= 0 && now - hint_t > 3000) hint_a = hint_b = -1, dirty = true;
    if (hint_a >= 0 && lit != ((now - hint_t) % 700 < 450)) lit = !lit, dirty = true; /* the hint blinks */
#ifdef PERF
    dirty = true; /* every frame drawn */
#endif
    if (dirty || sv.ms / 1000 != shown) {
      shown = sv.ms / 1000, dirty = false;
      draw();
    }
    eadk_timing_msleep(20);
  }
  save();
  gfx_buf = their_strip;
  return np_app_end();
}
