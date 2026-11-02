/* Drawing (draw.h). The screen is made 16 rows at a time in a strip and sent: the floor, then everything the
 * game sorts by depth (its getPosZ: the lower on the screen, the nearer), then the clouds and pads (the game's
 * DEEP_CURSOR layer), then the money, the alerts, the key line and any window. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "draw.h"
#include "art.h"

#define BAND 16
#define RGB(r, g, b) (uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))
#define INK RGB(89, 49, 102)            /* the game's dark purple (outlines, text) */
#define CREAM RGB(254, 239, 191)        /* its windows */
#define PALE RGB(255, 253, 226)
#define BUY RGB(255, 229, 105)          /* btn_buy */
#define BUY_EDGE RGB(238, 146, 37)
#define DULL RGB(246, 234, 214)         /* btn_insolvent */
#define DULL_EDGE RGB(189, 159, 105)
#define GREEN RGB(120, 239, 129)        /* btn_selected */
#define GREEN_EDGE RGB(78, 177, 61)
#define WHITE RGB(255, 255, 255)
#define FOCUS RGB(40, 200, 60)
#define GRASS RGB(0, 204, 102)

#ifdef HOST
int mm_clipped;
const char *mm_clipped_text;
static bool in_world;                   /* (labels in the world may go off the screen with it) */
#endif

static uint16_t strip[320 * BAND];
static int band;                        /* the strip's top row */

/* ------------------------------------------------------------------ pixels */
static uint16_t mix(uint16_t a, uint16_t b, unsigned n) {   /* a over b, n/15 */
  unsigned r = ((a >> 11) * n + (b >> 11) * (15 - n) + 7) / 15;
  unsigned g = (((a >> 5) & 63) * n + ((b >> 5) & 63) * (15 - n) + 7) / 15;
  unsigned bl = ((a & 31) * n + (b & 31) * (15 - n) + 7) / 15;
  return (uint16_t)(r << 11 | g << 5 | bl);
}
static void fill(int x, int y, int w, int h, uint16_t c) {
  int x0 = x < 0 ? 0 : x, x1 = x + w > 320 ? 320 : x + w;
  int y0 = y < band ? band : y, y1 = y + h > band + BAND ? band + BAND : y + h;
  for (int j = y0; j < y1; j++)
    for (int i = x0; i < x1; i++) strip[(j - band) * 320 + i] = c;
}
static void shade(int x, int y, int w, int h, uint16_t c, unsigned n) {
  int x0 = x < 0 ? 0 : x, x1 = x + w > 320 ? 320 : x + w;
  int y0 = y < band ? band : y, y1 = y + h > band + BAND ? band + BAND : y + h;
  for (int j = y0; j < y1; j++)
    for (int i = x0; i < x1; i++) strip[(j - band) * 320 + i] = mix(c, strip[(j - band) * 320 + i], n);
}
/* a box with rounded corners and an edge */
static void box(int x, int y, int w, int h, int r, uint16_t c, uint16_t edge, int t) {
  if (y + h <= band || y >= band + BAND) return;
  for (int j = 0; j < h; j++) {
    int yy = y + j;
    if (yy < band || yy >= band + BAND) continue;
    int in = 0;
    int dy = j < r ? r - j : j >= h - r ? j - (h - r - 1) : 0;
    if (dy) in = r - (int)sqrtf((float)(r * r - dy * dy));
    for (int i = in; i < w - in; i++) {
      int xx = x + i;
      if ((unsigned)xx >= 320) continue;
      bool e = j < t || j >= h - t || i < in + t || i >= w - in - t;
      if (dy && !e) {
        int dy2 = dy + t > r ? r : dy + t;
        int in2 = r - (int)sqrtf((float)(r * r - dy2 * dy2));
        e = i < in2 + (in2 == in ? t : 0) || i >= w - in2 - (in2 == in ? t : 0);
      }
      strip[(yy - band) * 320 + xx] = e ? edge : c;
    }
  }
}

/* a picture standing at (x, y), mirrored if flip */
static void pic(unsigned id, int x, int y, bool flip) {
  if (!id || id >= P_COUNT) return;
  const mm_pic *p = &mm_pics[id];
  int x0 = flip ? x - p->ox - p->w : x + p->ox, y0 = y + p->oy;
  if (y0 >= band + BAND || y0 + p->h <= band || x0 >= 320 || x0 + p->w <= 0) return;
  const uint16_t *pal = mm_pals[p->pal];
  const uint8_t *px = mm_pixels + p->px;
  int stride = (p->w + 1) >> 1;
  int ya = y0 < band ? band : y0, yb = y0 + p->h > band + BAND ? band + BAND : y0 + p->h;
  int xa = x0 < 0 ? 0 : x0, xb = x0 + p->w > 320 ? 320 : x0 + p->w;
  for (int yy = ya; yy < yb; yy++) {
    const uint8_t *row = px + (yy - y0) * stride;
    uint16_t *out = strip + (yy - band) * 320;
    for (int xx = xa; xx < xb; xx++) {
      int u = flip ? p->w - 1 - (xx - x0) : xx - x0;
      unsigned v = row[u >> 1];
      v = u & 1 ? v & 15 : v >> 4;
      if (v) out[xx] = pal[v];
    }
  }
}

/* ------------------------------------------------------------------ text (Riffic, the game's font) */
static int text_w(const mm_font *f, const char *s) {
  int w = 0;
  for (; *s; s++) {
    unsigned c = (unsigned char)*s;
    if (c < 32 || c > 126) c = '?';
    w += f->g[c - 32].adv;
  }
  return w;
}
static void glyph(const mm_font *f, unsigned c, int x, int y, uint16_t col) {
  const mm_glyph *g = &f->g[c - 32];
  if (!g->w) return;
  int x0 = x + g->x, y0 = y + g->y;
  for (int j = 0; j < g->h; j++) {
    int yy = y0 + j;
    if (yy < band || yy >= band + BAND) continue;
    for (int i = 0; i < g->w; i++) {
      int xx = x0 + i;
      if ((unsigned)xx >= 320) continue;
      unsigned k = (unsigned)(j * g->w + i), v = f->px[g->off + (k >> 1)];
      v = k & 1 ? v & 15 : v >> 4;
      if (v) {
        uint16_t *o = &strip[(yy - band) * 320 + xx];
        *o = v == 15 ? col : mix(col, *o, v);
      }
    }
  }
}
/* y: the top of the capitals */
static int text(const mm_font *f, const char *s, int x, int y, uint16_t col) {
#ifdef HOST
  if (band == 0 && !in_world && (x < 0 || x + text_w(f, s) > 320 || y < 0 || y + f->cap > 240)) mm_clipped++, mm_clipped_text = s;
#endif
  for (; *s; s++) {
    unsigned c = (unsigned char)*s;
    if (c < 32 || c > 126) c = '?';
    glyph(f, c, x, y, col);
    x += f->g[c - 32].adv;
  }
  return x;
}
static void text_c(const mm_font *f, const char *s, int cx, int y, uint16_t col) { text(f, s, cx - text_w(f, s) / 2, y, col); }
/* outlined, as the game's labels */
static void text_o(const mm_font *f, const char *s, int x, int y, uint16_t col, uint16_t edge) {
  static const int8_t d[8][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
  for (int k = 0; k < 8; k++) {
    int xx = x + d[k][0];
    for (const char *p = s; *p; p++) {
      unsigned c = (unsigned char)*p;
      if (c < 32 || c > 126) c = '?';
      glyph(f, c, xx, y + d[k][1], edge);
      xx += f->g[c - 32].adv;
    }
  }
  text(f, s, x, y, col);
}
static void text_oc(const mm_font *f, const char *s, int cx, int y, uint16_t col, uint16_t edge) {
  text_o(f, s, cx - text_w(f, s) / 2, y, col, edge);
}
static char *num(char *b, uint32_t v) {     /* into b, returns its end */
  char t[12];
  int n = 0;
  do t[n++] = (char)('0' + v % 10), v /= 10; while (v);
  while (n) *b++ = t[--n];
  *b = 0;
  return b;
}
/* words wrapped to width w; returns the next line's top */
static int para(const mm_font *f, const char *s, int x, int y, int w, uint16_t col) {
  char line[96];
  while (*s) {
    int n = 0, last = -1, len = 0;
    while (s[n] && s[n] != '\n') {
      if (s[n] == ' ') last = n;
      len += f->g[(unsigned char)s[n] - 32].adv;
      if (len > w && last > 0) break;
      n++;
    }
    int cut = s[n] && s[n] != '\n' && last > 0 ? last : n;
    memcpy(line, s, (size_t)cut);
    line[cut] = 0;
    text(f, line, x, y, col);
    y += f->line;
    s += cut;
    while (*s == ' ' || *s == '\n') s++;
  }
  return y;
}

/* ------------------------------------------------------------------ the floor */
static void floor_band(const mm_game *g) {
  const mm_floor *fl = &mm_floors[g->m->bg];
  /* the floor is drawn at twice its size, its middle at (0, 0) (mart 2: (-10, 80)) */
  float bx = g->mi ? -10.0f : 0.0f, by = g->mi ? 80.0f : 0.0f;
  int u0 = (int)floorf((g->camx - bx) / 3.0f) - 160 + fl->w / 2;
  int v0 = (int)floorf((by - g->camy) / 3.0f) - 120 + fl->h / 2;
  for (int yy = band; yy < band + BAND; yy++) {
    uint16_t *out = strip + (yy - band) * 320;
    int v = v0 + yy;
    if (v < 0 || v >= fl->h) {
      for (int i = 0; i < 320; i++) out[i] = fl->pal[0];
      continue;
    }
    const uint8_t *p = fl->data + fl->rows[v];
    int u = 0, x = -u0;          /* x: the screen column of u */
    while (u < fl->w && x < 320) {
      unsigned n = *p++;
      if (n >= 128) {
        unsigned run = n - 127, c = fl->pal[*p++];
        int a = x < 0 ? 0 : x, b = x + (int)run > 320 ? 320 : x + (int)run;
        for (int i = a; i < b; i++) out[i] = (uint16_t)c;
        x += (int)run, u += (int)run;
      } else {
        unsigned run = n + 1;
        for (unsigned i = 0; i < run; i++) {
          if (x >= 0 && x < 320) {
            unsigned c = p[i >> 1];
            out[x] = fl->pal[i & 1 ? c & 15 : c >> 4];
          }
          x++;
        }
        p += (run + 1) / 2, u += (int)run;
      }
    }
    for (int i = x < 0 ? 0 : x; i < 320; i++) out[i] = fl->pal[0];
    for (int i = 0; i < -u0 && i < 320; i++) out[i] = fl->pal[0];
  }
}

/* ------------------------------------------------------------------ the world's list */
enum { IT_PIC, IT_TEXT, IT_TEXT_C };
typedef struct { int32_t key; int16_t x, y; uint16_t id; uint8_t kind, flip; } item;
#define ITEMS 420
static item items[ITEMS], tmp[ITEMS];
static unsigned nitems;
static char texts[40][12];
static unsigned ntexts;
static float camx, camy;
static int scr_x(int32_t x) { return (int)floorf(((float)x - camx) / 3.0f) + 160; }
static int scr_y(int32_t y) { return (int)floorf((camy - (float)y) / 3.0f) + 120; }

#define KEY_GROUND (-0x40000000)
#define KEY_TOP 0x40000000
static int32_t depth(int32_t y10) { return -y10; }   /* nearer (lower y) sorts later */

static void put_at(int32_t key, int x, int y, unsigned id, bool flip);
static void put(int32_t key, int32_t wx, int32_t wy, unsigned id, bool flip) { put_at(key, scr_x(wx), scr_y(wy), id, flip); }
static void put_at(int32_t key, int x, int y, unsigned id, bool flip) {
  if (!id || nitems >= ITEMS) return;
  const mm_pic *p = &mm_pics[id];
  int x0 = flip ? x - p->ox - p->w : x + p->ox, y0 = y + p->oy;
  if (x0 > 320 || x0 + p->w < 0 || y0 > 240 || y0 + p->h < 0) return;
  items[nitems++] = (item){key, (int16_t)x, (int16_t)y, (uint16_t)id, IT_PIC, flip};
}
static void put_text(int32_t key, int32_t wx, int32_t wy, const char *s, bool center) {
  if (nitems >= ITEMS || ntexts >= 40) return;
  int x = scr_x(wx), y = scr_y(wy);
  if (x < -60 || x > 380 || y < -20 || y > 260) return;
  strncpy(texts[ntexts], s, 11);
  texts[ntexts][11] = 0;
  items[nitems++] = (item){key, (int16_t)x, (int16_t)y, (uint16_t)ntexts++, center ? IT_TEXT_C : IT_TEXT, 0};
}
static void sort_items(void) {   /* stable: things at one depth keep the order they were put in */
  for (unsigned w = 1; w < nitems; w *= 2) {
    for (unsigned i = 0; i < nitems; i += 2 * w) {
      unsigned a = i, am = i + w < nitems ? i + w : nitems, b = am, bm = i + 2 * w < nitems ? i + 2 * w : nitems, k = i;
      while (a < am && b < bm) tmp[k++] = items[b].key < items[a].key ? items[b++] : items[a++];
      while (a < am) tmp[k++] = items[a++];
      while (b < bm) tmp[k++] = items[b++];
    }
    memcpy(items, tmp, nitems * sizeof(item));
  }
}

/* an actor's animation (dodik.script): its suffix (1 empty-handed, 2 carrying, 3 a basket, 4 leaving) */
static unsigned anim_of(const mm_actor *a, unsigned *frame) {
  unsigned suffix = 1;
  if (a->role == R_BUYER) {
    if (a->stage == S_EXIT) suffix = 4;
    else if (a->nfood) suffix = a->basket;
    else if (a->has_basket) suffix = 3;
  } else if (a->nfood) {
    suffix = 2;
  }
  int an = -1;
  unsigned sk = a->skin;
  /* the frames there are: monkey1 (you) and 9 (staff) carry but never take a basket or leave; 7 dances */
  if (sk == 1 || sk == 9) {
    if (suffix > 2) suffix = 2;
  }
  switch (a->anim) {
    case AM_SLEEP: case AM_WAKE: an = sk == 9 ? AN_MONKEY9_SLEEP1 : -1; break;
    case AM_DILER: an = sk == 9 ? AN_MONKEY9_DILER1 : -1; break;
    case AM_CRAZY: an = a->crazy == 2 ? AN_MONKEY7_CRAZY2 : AN_MONKEY7_CRAZY1; break;
    case AM_THROW: an = AN_MONKEY7_CRAZY3; break;
  }
  if (an < 0) {
    bool run = a->anim == AM_RUN;
    static const uint16_t base[10] = {0, AN_MONKEY1_IDLE1, AN_MONKEY2_IDLE1, AN_MONKEY3_IDLE1, AN_MONKEY4_IDLE1,
                                      AN_MONKEY5_IDLE1, AN_MONKEY6_IDLE1, AN_MONKEY7_IDLE1, 0, AN_MONKEY9_IDLE1};
    an = base[sk < 10 ? sk : 1] + (int)((suffix - 1) * 2 + (run ? 1 : 0));
  }
  const mm_anim *m = &mm_anims[an];
  unsigned f = (a->anim_t / m->ticks);
  if (a->anim == AM_DILER || a->anim == AM_THROW || a->anim == AM_WAKE) f = f < m->n ? f : m->n - 1u;
  *frame = f % m->n;
  return (unsigned)an;
}

static void put_actor(const mm_game *g, const mm_actor *a, unsigned id) {
  unsigned f, an = anim_of(a, &f);
  const mm_anim *m = &mm_anims[an];
  unsigned frame = m->first + f;
  const mm_anchor *k = &mm_anchors[frame - 1 - (mm_anims[0].first - 1)];
  int32_t key = depth(a->y * 10);
  bool flip = a->flip;
  int x = scr_x(a->x), y = scr_y(a->y), sx = flip ? -1 : 1;
  put_at(key, x, y, frame, flip);
  /* the hat and the carried food go on bones of the frame (in screen pixels from the actor) */
  if (a->hat && a->hat < 31 && mm_hat_pic[a->hat]) put_at(key, x + sx * k->hx, y + k->hy, mm_hat_pic[a->hat], flip);
  unsigned n = a->nfood < 12 ? a->nfood : 12;
  for (unsigned i = 0; i < n; i++) put_at(key, x + sx * k->fx[i], y + k->fy[i], mm_carry_pic[a->food[i]], flip);
  (void)g, (void)id;
}

/* the clouds over an actor (dodik.script updateCloud) */
static void put_cloud(const mm_game *g, const mm_actor *a) {
  int32_t key = KEY_TOP + (int32_t)(a->y < 0 ? -a->y : a->y) % 1000;
  char b[12];
  switch (a->cloud) {
    case C_WISH: {
      put(key, a->x, a->y + 125, P_UI_GAME_CLOUD1, false);
      put(key, a->x - 30, a->y + 131, mm_food_icon[a->wtype], false);
      char *e = num(b, a->have);
      *e++ = '/';
      num(e, a->need);
      put_text(key, a->x + 20, a->y + 128, b, true);
      break;
    }
    case C_PAY: put(key, a->x, a->y + 145, P_UI_GAME_CLOUD2, false), put(key, a->x + 3, a->y + 148, P_UI_GAME_TERMINAL_SIZE86, false); break;
    case C_HAPPY: put(key, a->x, a->y + 145, P_UI_GAME_CLOUD2, false), put(key, a->x + 3, a->y + 148, P_UI_GAME_SMILE_SIZE86, false); break;
    case C_SLEEP: {
      put(key, a->x, a->y + 145, P_UI_GAME_CLOUD2, false), put(key, a->x + 3, a->y + 148, P_UI_GAME_SMILE_SLEEPY_SIZE86, false);
      const mm_anim *e = &mm_effects[FX_UI_GAME_EFFECT_SLEEP];
      put(key, a->x - 60, a->y + 120, e->first + (g->tick / e->ticks) % e->n, false);
      break;
    }
    case C_CRAZY: put(key, a->x, a->y + 145, P_UI_GAME_CLOUD2, false), put(key, a->x + 3, a->y + 148, P_UI_GAME_SMILE_CRAZY_SIZE86, false); break;
    case C_AWAY: put(key, a->x, a->y + 145, P_UI_GAME_CLOUD2, false), put(key, a->x + 3, a->y + 148, P_UI_GAME_SMILE_EXIT_SIZE86, false); break;
  }
}

static const uint16_t staff_icon[7] = {P_UI_GAME_ICON_CASHIER2, P_UI_GAME_ICON_ASSISTANT2, P_UI_GAME_ICON_ASSISTANT2,
                                       P_UI_GAME_ICON_FARMER2, P_UI_GAME_ICON_CHEF2, P_UI_GAME_ICON_CHEF2,
                                       P_UI_GAME_ICON_MANAGER2};

static void world_list(const mm_game *g, const mm_ui *v) {
  const mm_mart *m = g->m;
  nitems = ntexts = 0;
  int32_t gseq = 0;
  /* the ground: decor under everything */
  for (unsigned i = 0; i < m->ndecor; i++) {
    const mm_decor *d = &m->decor[i];
    if (d->cond != MM_NONE && !g->box[d->cond].active) continue;
    if (d->zy == MM_GROUND) put(KEY_GROUND + gseq++, d->x, d->y, d->pic, false);
  }
  /* pads (the aim zone: on the ground; its cloud with the price on top) */
  for (unsigned i = 0; i < m->count; i++) {
    const mm_place *p = &m->places[i];
    if (g->box[i].active || !g->box[i].selling) continue;
    put(KEY_GROUND + 0x1000 + (int32_t)i, p->buy.x, p->buy.y, P_UI_GAME_AIM1, false);
    int32_t key = KEY_TOP + 2000 + (int32_t)i;
    put(key, p->buy.x - 3, p->buy.y + 74, P_UI_GAME_CLOUD2, false);
    put(key, p->buy.x, p->buy.y + 90, P_UI_GAME_CASH, false);
    char b[12];
    num(b, (uint32_t)(p->price - g->box[i].paid));
    put_text(key, p->buy.x, p->buy.y + 56, b, true);
    for (unsigned h = 0; h < m->nhook; h++)
      if (m->hooks[h].place == i && m->hooks[h].kind == HK_STAFF) put(key, p->buy.x, p->buy.y + 147, staff_icon[m->hooks[h].a], false);
    if (i == m->truck && g->mi == 0) put(key, p->buy.x, p->buy.y + 140, P_UI_GAME_ICON_NEXT_MART, false);
  }
  /* decor that stands */
  for (unsigned i = 0; i < m->ndecor; i++) {
    const mm_decor *d = &m->decor[i];
    if (d->zy == MM_GROUND || (d->cond != MM_NONE && !g->box[d->cond].active)) continue;
    put(depth(d->zy * 10), d->x, d->y, d->pic, false);
  }
  if (g->allow_next) put(depth(g->truck_y * 10), g->truck_x, g->truck_y - 50, P_GAME_AIM1, false);
  /* places, their food */
  for (unsigned i = 0; i < m->count; i++) {
    const mm_place *p = &m->places[i];
    const mm_box *b = &g->box[i];
    if (!b->active) continue;
    int32_t key = depth(p->zy * 10);
    bool show = !(p->flags & PL_HIDE) && !(p->food == 9 && (p->flags & PL_ZAVOD) && b->count == 0);
    if (show && p->pic2) put(depth((p->zy + 2) * 10), p->pos2.x, p->pos2.y, p->pic2, false);
    if (show) put(key, p->pos.x, p->pos.y, p->pic, false);
    if (p->nview) {
      if ((p->flags & PL_ZAVOD) && p->food == 1) {       /* a bunch: banana1 .. 3 */
        const mm_view *w = &mm_views[p->view];
        if (b->count) put(depth(p->pos.y * 10 + w->dz), p->pos.x + w->dx, p->pos.y + w->dy, w->pic + (b->count > 3 ? 2 : b->count - 1), false);
      } else {
        unsigned n = b->count < p->nview ? b->count : p->nview;
        for (unsigned k = 0; k < n; k++) {
          const mm_view *w = &mm_views[p->view + k];
          put(depth(p->pos.y * 10 + w->dz), p->pos.x + w->dx, p->pos.y + w->dy, w->pic, false);
        }
      }
    }
    /* the cash box: notes (up to ten), and stacks of ten (updateSkladCash); their z over the box's, as a y in tenths */
    if (i == m->cash) {
      int32_t c = g->cash > 100 ? 100 : g->cash;
      int stacks = c >= 100 ? 10 : c / 10, notes = c >= 100 ? 0 : c % 10;
      int32_t px = p->pos.x - 59, py = p->pos.y - 50;
      for (int s = 0; s < stacks; s++)
        put(depth(p->pos.y * 10 - 210 - 10 * s), px + 48, py + 47 + 16 * s, P_GAME_CASH_LIST, false);
      for (int k = 0; k < notes; k++) {
        int col = k & 1, row = k / 2;
        put(depth(p->pos.y * 10 - 16 * stacks * 10 - 250 + 10 * row), px + col * 47 + 12 * row, py + row * 25 - 7 * col + 16 * stacks, P_GAME_CASH, false);
      }
    }
  }
  /* animals */
  for (unsigned z = 0; z < m->nzavod; z++) {
    const mm_zavod *zv = &m->zavods[z];
    if (zv->kind == ZV_MACHINE || !g->box[zv->zavod].active) continue;
    int an = zv->kind == ZV_COW ? AN_COW1_IDLE_ANIMAL : AN_CHICKEN1_IDLE_ANIMAL;
    if (g->zs[z].sleeping) an += 2;
    else if (g->zs[z].eat) an += 1;
    const mm_anim *a = &mm_anims[an];
    put(depth(zv->pos.y * 10), zv->pos.x, zv->pos.y, a->first + (g->tick / a->ticks) % a->n, false);
  }
  /* dropped food */
  for (unsigned i = 0; i < g->ndropped; i++)
    if (g->dropped[i].live) put(depth(g->dropped[i].y * 10), g->dropped[i].x, g->dropped[i].y + 5, mm_food_pic[g->dropped[i].food], false);
  /* actors and their clouds */
  for (unsigned i = 0; i < MM_ACTORS; i++) {
    const mm_actor *a = &g->a[i];
    if (!a->alive) continue;
    put_actor(g, a, i);
    put_cloud(g, a);
    if (a->role != R_BUYER && a->nfood >= a->max_food) {   /* text_max */
      put_text(KEY_TOP + 1500, a->x, a->y + 44 + 27 * a->nfood, "MAX", true);
    }
  }
  /* sleeping animals and broken machines; the feeders' panels */
  for (unsigned z = 0; z < m->nzavod; z++) {
    const mm_zavod *zv = &m->zavods[z];
    if (!g->box[zv->zavod].active) continue;
    int32_t key = KEY_TOP + 1000 + (int32_t)z;
    if (g->zs[z].sleeping) {
      put(key, zv->pos.x, zv->pos.y + 125, P_UI_GAME_CLOUD2, false);
      bool mach = zv->kind == ZV_MACHINE;
      put(key, zv->pos.x + 3, zv->pos.y + 128, mach ? P_UI_GAME_SMILE_REPAIR_SIZE86 : P_UI_GAME_SMILE_SLEEPY_SIZE86, false);
      const mm_anim *e = &mm_effects[mach ? FX_UI_GAME_EFFECT_BROKEN : FX_UI_GAME_EFFECT_SLEEP];
      put(key, zv->pos.x + (mach ? -95 : -120) / 2, zv->pos.y + 125 + (mach ? -60 : -11), e->first + (g->tick / e->ticks) % e->n, false);
    }
    for (int f = 0; f < 2; f++) {
      unsigned feed = f ? zv->feed2 : zv->feed;
      if (feed == MM_NONE) continue;
      mm_pt pp = f ? zv->panel2 : zv->panel;
      put(key, pp.x, pp.y, P_UI_GAME_CLOUD5, false);
      put(key, pp.x - 30, pp.y - 1, mm_food_icon[m->places[feed].food], false);
      char b[12], *e = num(b, g->box[feed].count);
      *e++ = '/';
      num(e, g->box[feed].cap);
      put_text(key, pp.x + 20, pp.y - 2, b, true);
    }
  }
  /* food flying, effects, the hints */
  for (unsigned i = 0; i < MM_FLY; i++) {
    const mm_fly *f = &g->fly[i];
    if (f->t >= f->n || !f->n) continue;
    int32_t x = f->x0 + (f->x1 - f->x0) * f->t / f->n, y = f->y0 + (f->y1 - f->y0) * f->t / f->n;
    put(KEY_TOP + 3000, x, y, f->food ? mm_food_pic[f->food] : P_GAME_CASH, false);
  }
  for (unsigned i = 0; i < 8; i++) {
    if (!g->fx_kind[i]) continue;
    static const uint8_t fx[5] = {0, FX_UI_GAME_EFFECT_EXIT, FX_UI_GAME_EFFECT_ALARM, FX_UI_GAME_EFFECT_REPAIR, FX_UI_GAME_EFFECT_BROOM};
    const mm_anim *e = &mm_effects[fx[g->fx_kind[i]]];
    unsigned f = g->fx_t[i] / e->ticks;
    if (f < e->n) put(KEY_TOP + 3100, g->fx_x[i], g->fx_y[i], e->first + f, false);
  }
  int wave = abs((int)(g->tick % 60) - 30) / 3;   /* setWaveEffect: 10 up and down in half a second */
  if (g->hint == 1) put(KEY_TOP + 3200, g->hint_x, g->hint_y + 230 + wave * 3, P_UI_GAME_TUTORIAL_ARROW, false);
  if (g->force_marts && g->allow_next) put(KEY_TOP + 3200, g->truck_x, g->truck_y + 30 + wave * 3, P_UI_GAME_TUTORIAL_ARROW, false);
  if (g->tutorial && g->step <= 4 && v->screen != V_TITLE) {
    static const uint8_t place[5] = {6, 4, 0, 4, 6};
    static const int16_t off[5][2] = {{0, 160}, {0, 160}, {0, 160}, {20, 80}, {-210, 130}};
    const mm_place *p = &m->places[place[g->step]];
    put(KEY_TOP + 3200, p->buy.x + off[g->step][0], p->buy.y + off[g->step][1] + wave * 3, P_UI_GAME_TUTORIAL_ARROW, false);
  }
  (void)v;
  sort_items();
}

static void world_band(void) {
#ifdef HOST
  in_world = true;
#endif
  for (unsigned i = 0; i < nitems; i++) {
    const item *it = &items[i];
    if (it->kind == IT_PIC) {
      pic(it->id, it->x, it->y, it->flip);
    } else {
      const char *s = texts[it->id];
      int x = it->kind == IT_TEXT_C ? it->x - text_w(&mm_font_small, s) / 2 : it->x;
      int top = it->y - mm_font_small.cap / 2;
      if (top < band + BAND && top + mm_font_small.line > band) text(&mm_font_small, s, x, top, INK);
    }
  }
#ifdef HOST
  in_world = false;
#endif
}

/* ------------------------------------------------------------------ the heads-up display */
/* the keys, in words; "|" leaves room for the "!" (the game's icon_attention on its upgrades button) */
static void key_line(const char *s, bool badge) {
  shade(0, 226, 320, 14, INK, 12);
  char part[64];
  int x = 6;
  while (*s) {
    unsigned n = 0;
    while (s[n] && s[n] != '|' && n + 1 < sizeof part) part[n] = s[n], n++;
    part[n] = 0;
    x = text(&mm_font_small, part, x, 229, WHITE);
    s += n;
    if (*s == '|') {
      s++;
      if (badge) pic(P_UI_GAME_ICON_ATTENTION, x + 8, 232, false), x += 16;
    }
  }
}

static void hud(const mm_game *g) {
  /* the money */
  char b[16];
  num(b, g->coins_display >> 8);
  int w = text_w(&mm_font_big, b);
  box(4, 4, w + 34, 22, 10, PALE, INK, 2);
  pic(P_UI_GAME_UI_CASH, 17, 15, false);
  text(&mm_font_big, b, 29, 9, INK);
  /* the alerts (the game's cloud icons): what needs you */
  int x = 6;
  for (unsigned i = 0; i < g->nalerts && i < 9; i++) {
    uint8_t a = g->alerts[i];
    unsigned id;
    if (a >= 64) id = mm_food_icon[g->m->places[a - 64].food];
    else if (a >= 16) id = staff_icon[a - 16 - A_CASHIER];
    else id = mm_alert_icon[g->m->zavods[a - 1].name];
    box(x, 30, 24, 22, 8, PALE, INK, 1);
    pic(id, x + 12, 41, false);
    x += 27;
  }
}

/* ------------------------------------------------------------------ windows */
static void window(int x, int y, int w, int h, const char *title) {
  shade(0, 0, 320, 240, RGB(0, 0, 0), 7);
  box(x, y, w, h, 10, CREAM, INK, 2);
  if (title) text_oc(&mm_font_big, title, x + w / 2, y + 8, WHITE, INK);
}
static void focus(int x, int y, int w, int h) {   /* the selection: a bright frame */
  box(x - 3, y - 3, w + 6, h + 6, 8, FOCUS, FOCUS, 3);
}
static void button(int x, int y, int w, int h, uint16_t c, uint16_t edge, bool sel) {
  if (sel) focus(x, y, w, h);
  box(x, y, w, h, 6, c, edge, 2);
}

static const char *const help[MM_HELP_PAGES] = {
  "Walk with the arrows. Stand by a palm, a plant, an animal or a machine to pick up food, then by its shelf "
  "to put it there.\nCustomers take what they want and wait at the cash register: stand by it to serve them and "
  "take the money.",
  "Stand on a pad with a price to buy what it shows: shelves, plants, animals, machines and workers.\n"
  "Animals and machines tire: walk up to them. Tired workers sleep until you or the manager walk up.",
  "A troublemaker throws food off a shelf: walk up to send him away.\nOK opens the upgrades, shift the hats. "
  "The truck takes you to the other mart. Home saves and quits.",
};

unsigned mm_hat_count(void) {
  unsigned n = 0;
  for (unsigned i = 0; i < MM_HATS; i++) n += mm_hats[i].kind != HAT_ADS;
  return n;
}
unsigned mm_hat_at(unsigned k) {
  for (unsigned i = 0; i < MM_HATS; i++)
    if (mm_hats[i].kind != HAT_ADS && k-- == 0) return i;
  return 0;
}

static void upgrades_window(const mm_game *g, const mm_ui *v) {
  window(6, 4, 308, 220, "UPGRADES");
  static const char *const name[3] = {"Stamina", "Stack", "Speed"};
  const mm_mart *m = g->m;
  for (unsigned r = 0; r < 3; r++) {
    unsigned s = v->up_top + r;
    if (s >= m->nupshow) break;
    const mm_upshow *u = &m->upshow[s];
    int y = 34 + (int)r * 62;
    box(12, y, 296, 58, 8, PALE, DULL_EDGE, 1);
    bool open = mm_upshow_open(g, s);
    /* the game's row: its name, its picture and what it looks after; then stamina, stack and speed */
    text(&mm_font_small, open ? u->title : "?", 18, y + 5, INK);
    pic(open ? u->icon : (u->icon_off ? u->icon_off : u->icon), 34, y + 36, false);
    for (unsigned k = 0; k < 3; k++)
      if (u->small[k] && open) pic(u->small[k], 62, y + 22 + (int)k * 12, false);
    if (!open) continue;
    const mm_uprow *row = &(u->row == 0 ? &mm_marts[0] : m)->uprows[u->row];
    for (unsigned k = 0; k < 3; k++) {
      if (!row->max[k]) continue;
      int bx = 80 + (int)k * 76;
      char t[24], *e = t;
      strcpy(t, name[k]);
      e += strlen(t);
      strcpy(e, " ");
      num(e + 1, mm_upgrade_level(g, u->row, k));
      int price = mm_upgrade_price(g, u->row, k);
      bool sel = s == v->up_row && k == v->up_col;
      bool can = price >= 0 && (uint32_t)price <= g->p.coins;
      button(bx, y + 17, 70, 36, price < 0 ? DULL : can ? BUY : DULL, price < 0 ? DULL_EDGE : can ? BUY_EDGE : DULL_EDGE, sel);
      text_c(&mm_font_small, t, bx + 35, y + 21, INK);
      char p[12];
      if (price < 0) text_c(&mm_font_big, "MAX", bx + 35, y + 34, INK);
      else if (price == 0) text_c(&mm_font_big, "FREE", bx + 35, y + 34, INK);
      else {
        num(p, (uint32_t)price);
        int w2 = text_w(&mm_font_big, p) + 16;
        pic(P_UI_MAIN_CASH_SIZE60, bx + 35 - w2 / 2 + 6, y + 40, false);
        text(&mm_font_big, p, bx + 35 - w2 / 2 + 16, y + 34, INK);
      }
    }
  }
  /* where the list is */
  if (m->nupshow > 3) {
    int h = 186 * 3 / m->nupshow, top = 34 + (186 - h) * v->up_top / (m->nupshow - 3);
    fill(310, 34, 2, 186, DULL_EDGE);
    fill(309, top, 4, h, INK);
  }
}

static void hats_window(const mm_game *g, const mm_ui *v) {
  window(6, 4, 308, 220, "SHOP");
  unsigned n = mm_hat_count();
  for (unsigned r = 0; r < 3; r++)
    for (unsigned c = 0; c < 3; c++) {
      unsigned k = (v->hat_top + r) * 3 + c;
      if (k >= n) continue;
      unsigned h = mm_hat_at(k);
      int x = 16 + (int)c * 98, y = 34 + (int)r * 62;
      bool own = g->p.hats >> h & 1, on = g->p.hat == h, sel = k == v->hat_sel;
      if (sel) focus(x, y, 92, 58);
      box(x, y, 92, 58, 8, on ? GREEN : PALE, on ? GREEN_EDGE : DULL_EDGE, 2);
      static const uint16_t icon0 = P_UI_MAIN_HAT0;
      unsigned id = h ? P_UI_MAIN_HAT11 + (h - 1) : icon0;
      pic(id, x + 46, y + 22, false);
      char b[12];
      if (on) text_c(&mm_font_small, "Wearing", x + 46, y + 44, INK);
      else if (own) text_c(&mm_font_small, "Yours", x + 46, y + 44, INK);
      else if (mm_hats[h].kind == HAT_FREE) text_c(&mm_font_small, "FREE", x + 46, y + 44, INK);
      else {
        num(b, mm_hats[h].price);
        int w = text_w(&mm_font_small, b) + 12;
        pic(P_UI_MAIN_CASH_SIZE60, x + 46 - w / 2 + 5, y + 48, false);
        text(&mm_font_small, b, x + 46 - w / 2 + 12, y + 44, INK);
      }
    }
  unsigned rows = (n + 2) / 3;
  if (rows > 3) {
    int h = 186 * 3 / (int)rows, top = 34 + (186 - h) * v->hat_top / (int)(rows - 3);
    fill(310, 34, 2, 186, DULL_EDGE);
    fill(309, top, 4, h, INK);
  }
}

static void marts_window(const mm_game *g, const mm_ui *v) {
  window(30, 30, 260, 170, "MARTS");
  for (unsigned i = 0; i < MM_MARTS; i++) {
    int x = 44 + (int)i * 120, y = 66;
    bool here = g->mi == i, open = mm_mart_open(g, i), sel = v->mart_sel == i;
    if (sel && open && !here) focus(x, y, 112, 116);
    box(x, y, 112, 116, 8, here ? GREEN : PALE, here ? GREEN_EDGE : DULL_EDGE, 2);
    char t[8] = "MART ";
    t[5] = (char)('1' + i), t[6] = 0;
    text_oc(&mm_font_big, t, x + 56, y + 8, WHITE, INK);
    pic(P_UI_MAIN_ICON_MARTS_SIZE100, x + 56, y + 52, false);
    if (here) text_c(&mm_font_small, "You are here", x + 56, y + 92, INK);
    else if (open) {
      button(x + 16, y + 84, 80, 24, BUY, BUY_EDGE, false);
      text_c(&mm_font_big, "VISIT", x + 56, y + 90, INK);
    } else {
      text_c(&mm_font_small, "Buy the truck", x + 56, y + 92, INK);
    }
  }
}

static void pause_window(const mm_game *g, const mm_ui *v) {
  int h = 44 + 30 * v->nitems;
  window(70, 120 - h / 2, 180, h, "PAUSED");
  static const char *const label[4] = {"Resume", "Marts", "How to play", "Quit game"};
  for (unsigned i = 0; i < v->nitems; i++) {
    int y = 120 - h / 2 + 34 + (int)i * 30;
    button(90, y, 140, 24, v->sel == i ? BUY : PALE, v->sel == i ? BUY_EDGE : DULL_EDGE, v->sel == i);
    text_c(&mm_font_big, label[v->items[i]], 160, y + 6, INK);
  }
  (void)g;
}

static void quit_window(const mm_ui *v) {
  window(50, 77, 220, 86, 0);
  text_c(&mm_font_big, "Quit the game?", 160, 91, INK);
  static const char *const yn[2] = {"No", "Yes"};
  for (int i = 0; i < 2; i++) {
    int x = 80 + i * 90;
    button(x, 119, 70, 26, v->sel == i ? BUY : PALE, v->sel == i ? BUY_EDGE : DULL_EDGE, v->sel == i);
    text_c(&mm_font_big, yn[i], x + 35, 126, INK);
  }
}

static void help_window(const mm_ui *v) {
  window(10, 14, 300, 202, "HOW TO PLAY");
  para(&mm_font_small, help[v->page], 22, 44, 276, INK);
  char b[8], *e = num(b, v->page + 1u);
  *e++ = '/', *e++ = (char)('0' + MM_HELP_PAGES), *e = 0;
  text_c(&mm_font_small, b, 160, 198, INK);
}

/* the guide (the game's tutorial), in a box at the top */
static void guide(const mm_game *g, const mm_ui *v) {
  const char *s = 0;
  switch (g->step) {
    case 0: s = "Use the arrows to move"; break;
    case 4: s = "Go to the cash register"; break;
    case 5: s = "Upgrade your monkey: press OK"; break;
    case 6: s = "OK takes the free upgrade"; break;
    case 7: s = "Back closes the window"; break;
    case 8: s = "Let's get a new hat! Press shift"; break;
    case 9: s = "OK takes the free hat"; break;
    case 10: s = "Back closes the window"; break;
  }
  if (!s) return;
  if ((g->step == 6 || g->step == 7) && v->screen != V_UPGRADES) return;
  if ((g->step == 9 || g->step == 10) && v->screen != V_HATS) return;
  int w = text_w(&mm_font_big, s) + 20, y = v->screen == V_PLAY ? 30 : 200;
  box(160 - w / 2, y, w, 24, 10, PALE, INK, 2);
  text_c(&mm_font_big, s, 160, y + 6, INK);
  if (g->step == 0) pic(P_UI_GAME_TUTORIAL_KEYS_SIZE60, 160, y + 46, false);
}

/* ------------------------------------------------------------------ a frame */
static const char *keys_for(const mm_game *g, const mm_ui *v, bool *badge) {
  *badge = false;
  switch (v->screen) {
    case V_TITLE: return "OK play   back quit";
    case V_PAUSE: return "OK choose   back resume";
    case V_QUIT: return "arrows choose   OK confirm   back no";
    case V_HELP: return "right next page   back close";
    case V_UPGRADES: return "arrows choose   OK buy   back close";
    case V_HATS: return "arrows choose   OK buy or wear   back close";
    case V_MARTS: return g->force_marts ? "OK visit" : "arrows choose   OK visit   back close";
  }
  if (g->tutorial) {
    if (g->step >= 5 && g->step <= 7) return "arrows walk   OK upgrades   back menu";
    if (g->step >= 8) return "arrows walk   shift hats   back menu";
    return "arrows walk   back menu";
  }
  *badge = g->badge;
  return "OK upgrades|   shift hats   back menu";
}

void mm_draw(const mm_game *g, const mm_ui *v, mm_push_fn push) {
  camx = g->camx, camy = g->camy;
  bool world = v->screen == V_TITLE || v->screen == V_PLAY || v->screen == V_PAUSE || v->screen == V_QUIT ||
               v->screen == V_MARTS || v->screen == V_HELP;
  bool full = v->screen == V_UPGRADES || v->screen == V_HATS;   /* the world goes on, under a window */
  world_list(g, v);
#ifdef HOST
  mm_clipped = 0;
#endif
  bool badge;
  const char *keys = keys_for(g, v, &badge);
  for (band = 0; band < 240; band += BAND) {
    floor_band(g);
    world_band();
    if (v->screen == V_TITLE) {
      shade(0, 0, 320, 240, RGB(255, 240, 200), 4);
      pic(P_LOGO, 160, 78, false);
      box(84, 176, 152, 28, 10, PALE, INK, 2);
      text_c(&mm_font_big, "Press OK to play", 160, 184, INK);
    } else {
      hud(g);
    }
    if (g->tutorial && v->screen != V_TITLE) guide(g, v);
    switch (v->screen) {
      case V_PAUSE: pause_window(g, v); break;
      case V_QUIT: quit_window(v); break;
      case V_HELP: help_window(v); break;
      case V_UPGRADES: upgrades_window(g, v); break;
      case V_HATS: hats_window(g, v); break;
      case V_MARTS: marts_window(g, v); break;
    }
    if (g->tutorial && (v->screen == V_UPGRADES || v->screen == V_HATS)) guide(g, v);
    if (g->notice_t && v->screen != V_TITLE) {
      int w = text_w(&mm_font_big, g->notice) + 24;
      box(160 - w / 2, 92, w, 30, 10, PALE, INK, 2);
      text_c(&mm_font_big, g->notice, 160, 101, INK);
    }
    if (v->save_failed) {
      box(60, 200, 200, 22, 8, PALE, INK, 2);
      text_c(&mm_font_small, "Not saved: the storage is full", 160, 206, INK);
    }
    key_line(keys, badge);
    push(0, band, 320, BAND, strip);
  }
  (void)world, (void)full;
}
