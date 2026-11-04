/* The renderer on the TI-84 Plus CE: the NumWorks version's (src/gfx.c) list of what a frame shows, drawn at 8 bits a
 * pixel. The screen is cut in 15 strips of 16 rows; a strip is drawn again (in a buffer, then copied to the screen)
 * only when something in it changed: each strip keeps a hash of what covers it. The background's motion is its
 * colours turning (bg_ce.c), which needs no drawing at all.
 *
 * Colours: the interface's are fixed palette entries; a colour that has none (a boss's own, mixed shades of it) takes
 * one of 16 free entries while the screen shows it. Shading (panels, shadows, Options over the game) goes through
 * tables of every entry darkened. */
#include <string.h>
#include "art.h"
#include "assets.h"
#include "bg.h"
#include "ce.h"
#include "flame.h"

#define SH 16
#define NSTRIP (SCR_H / SH)
#define MAXI 360
#define MAXCLIP 16
#define NDYN 16
/* the fonts, read in the archive (BALLUT) */
#define font_small ((const glyph_t *)(ce_lut + LUT_FONT_SMALL))
#define font_med ((const glyph_t *)(ce_lut + LUT_FONT_MED))
#define font_large ((const glyph_t *)(ce_lut + LUT_FONT_LARGE))
#define font_large_digits ((const glyph_t *)(ce_lut + LUT_FONT_LARGE_DIGITS))
#define font_small_bits (ce_lut + LUT_FONT_SMALL_BITS)
#define font_med_bits (ce_lut + LUT_FONT_MED_BITS)
#define font_large_bits (ce_lut + LUT_FONT_LARGE_BITS)
#define font_large_digits_bits (ce_lut + LUT_FONT_LARGE_DIGITS_BITS)

enum { I_RECT, I_RRECT, I_SHADE, I_SPRITE, I_TEXT, I_FLAME, I_RSQ };

typedef struct {
  int16_t x, y, w, h;
  uint8_t type, clip, r, fx;
  uint16_t a, b;
  const char *s;
} item_t;

static item_t items[MAXI];
static char strbuf[2560]; /* (a frame takes under 1 KB: the How to play page) */
static int strpos, nitems;
static int16_t clips[MAXCLIP][4];
static int nclips, cur_clip;
static uint8_t strip[SCR_W * SH];
static int bg_swirl, bg_cover_x;
static uint8_t bg_solid;
static unsigned strip_hash[NSTRIP];
static uint8_t drawn_once;
uint32_t g_time;

void *g_scratch(int *size) {
  *size = (int)sizeof strip;
  return strip;
}
void g_bg_cover(int x) { bg_cover_x = x; }

#ifdef NP_HOST
/* (blit.S on the calculator) */
void ce_shade(uint8_t *d, const uint8_t *tab, unsigned n) {
  while (n--) *d = tab[*d], d++;
}
void ce_unpack(uint8_t *d, const uint8_t *s, unsigned n) {
  for (; n; n--, s++) *d++ = (uint8_t)(CE_SWIRL0 + (*s & 15)), *d++ = (uint8_t)(CE_SWIRL0 + (*s >> 4));
}
#endif

C darken(C a, int t) { return mix565(a, 0, t); }
C lighten(C a, int t) { return mix565(a, 0xFFFF, t); }

/* ------------------------------------------------------------------ colours */
static C dyn_col[NDYN];
static uint16_t strip_dyn[NSTRIP], frame_dyn; /* the taken entries each strip shows; those this frame uses */
static struct {
  C c;
  uint8_t v, ok;
} pcache[64];

static int dist(C a, uint16_t p) {
  int dr = (int)(a >> 11) - (int)(p >> 10 & 31), dg = (int)((a >> 6) & 31) - (int)(p >> 5 & 31),
      db = (int)(a & 31) - (int)(p & 31);
  return 2 * dr * dr + 4 * dg * dg + 3 * db * db;
}
static uint8_t nearest_fixed(C c, int *d) {
  int best = 0, bd = 1 << 20;
  for (int i = 0; i < CE_NSTATIC; i++) {
    int e = dist(c, ce_palette[i]);
    if (e < bd) bd = e, best = i;
  }
  if (d) *d = bd;
  return (uint8_t)best;
}

static void shade_tabs_reset(void);

uint8_t ce_pal(C c) {
  unsigned h = (c ^ c >> 6 ^ c >> 11) & 63;
  if (pcache[h].ok && pcache[h].c == c) {
    uint8_t v = pcache[h].v;
    if (v < CE_DYN0 || v >= CE_SWIRL0 || dyn_col[v - CE_DYN0] == c) {
      if (v >= CE_DYN0 && v < CE_SWIRL0) frame_dyn |= (uint16_t)(1u << (v - CE_DYN0));
      return v;
    }
  }
  int d;
  uint8_t v = nearest_fixed(c, &d);
  if (d > 6) {
    /* a free entry, or one already showing it */
    uint16_t used = frame_dyn;
    for (int s = 0; s < NSTRIP; s++) used |= strip_dyn[s];
    int k = -1;
    for (int i = 0; i < NDYN; i++)
      if (dyn_col[i] == c && (used >> i & 1)) k = i;
    if (k < 0)
      for (int i = 0; i < NDYN && k < 0; i++)
        if (!(used >> i & 1)) k = i;
    if (k >= 0) {
      if (dyn_col[k] != c) {
        dyn_col[k] = c;
        uint16_t p = ce_1555(c);
        ce_set_palette(CE_DYN0 + k, 1, &p);
        shade_tabs_reset();
      }
      frame_dyn |= (uint16_t)(1u << k);
      v = (uint8_t)(CE_DYN0 + k);
    }
  }
  pcache[h].c = c, pcache[h].v = v, pcache[h].ok = 1;
  return v;
}

/* darkened tables: a few kept, made when asked for (the taken entries' only while they are shown) */
#define NSTAB 8
static const int8_t bank_dark[CE_BANKS] = CE_BANK_DARK;
static struct {
  uint8_t alpha, ok;
} stabs[NSTAB];
static uint8_t stab_t[NSTAB][256] __attribute__((aligned(256))); /* (ce_shade looks up by the low byte) */
static uint8_t stab_next;
static void shade_tabs_reset(void) {
  for (int i = 0; i < NSTAB; i++) stabs[i].ok = 0;
}
const uint8_t *ce_shade_tab(int alpha) {
  if (alpha < 0) alpha = 0;
  if (alpha > 32) alpha = 32;
  for (int i = 0; i < NSTAB; i++)
    if (stabs[i].ok && stabs[i].alpha == alpha) return stab_t[i];
  int i = stab_next;
  stab_next = (uint8_t)((stab_next + 1) % NSTAB);
  uint8_t *t = stab_t[i];
  memcpy(t, ce_lut + LUT_SHADE + alpha * CE_NSTATIC, CE_NSTATIC);
  uint16_t used = frame_dyn;
  for (int s = 0; s < NSTRIP; s++) used |= strip_dyn[s];
  for (int k = 0; k < NDYN; k++) t[CE_DYN0 + k] = used >> k & 1 ? nearest_fixed(mix565(dyn_col[k], 0, alpha), 0) : 0;
  for (int b = 0; b < CE_BANKS; b++) {
    /* the background's banks: the darkness after shading, to the nearest bank */
    int dark = 32 - (32 - bank_dark[b]) * (32 - alpha) / 32, nb = 0;
    for (int q = 1; q < CE_BANKS; q++)
      if ((dark - bank_dark[q]) * (dark - bank_dark[q]) < (dark - bank_dark[nb]) * (dark - bank_dark[nb])) nb = q;
    for (int l = 0; l < CE_LEVELS; l++) t[CE_SWIRL0 + b * CE_LEVELS + l] = (uint8_t)(CE_SWIRL0 + nb * CE_LEVELS + l);
  }
  stabs[i].alpha = (uint8_t)alpha, stabs[i].ok = 1;
  return t;
}

/* ------------------------------------------------------------------ the list */
#define MAXR 8 /* (the interface's corners are 7 at most) */
static uint8_t cov[MAXR + 1][MAXR][MAXR];

void g_init(void) {
  for (int r = 1; r <= MAXR; r++)
    for (int y = 0; y < r; y++)
      for (int x = 0; x < r; x++) {
        /* 4x4 samples of the quarter circle centred at (r, r) */
        int n = 0;
        for (int sy = 0; sy < 4; sy++)
          for (int sx = 0; sx < 4; sx++) {
            int dx = 8 * r - (8 * x + 2 * sx + 1), dy = 8 * r - (8 * y + 2 * sy + 1);
            if (dx * dx + dy * dy <= 64 * r * r) n++;
          }
        cov[r][y][x] = (uint8_t)(n * 2);
      }
  drawn_once = 0;
}

void g_begin(void) {
  nitems = 0;
  bg_cover_x = 0;
  strpos = 0;
  nclips = 1;
  cur_clip = 0;
  frame_dyn = 0;
  clips[0][0] = 0, clips[0][1] = 0, clips[0][2] = SCR_W, clips[0][3] = SCR_H;
}

void g_clip(int x, int y, int w, int h) {
  if (nclips >= MAXCLIP) return;
  clips[nclips][0] = (int16_t)(x < 0 ? 0 : x);
  clips[nclips][1] = (int16_t)(y < 0 ? 0 : y);
  clips[nclips][2] = (int16_t)(x + w > SCR_W ? SCR_W : x + w);
  clips[nclips][3] = (int16_t)(y + h > SCR_H ? SCR_H : y + h);
  cur_clip = nclips++;
}
void g_noclip(void) { cur_clip = 0; }

static item_t *add(int type, int x, int y, int w, int h) {
  if (nitems >= MAXI || w <= 0 || h <= 0) return 0;
  const int16_t *c = clips[cur_clip];
  if (x >= c[2] || y >= c[3] || x + w <= c[0] || y + h <= c[1]) return 0;
  item_t *it = &items[nitems++];
  it->type = (uint8_t)type;
  it->x = (int16_t)x, it->y = (int16_t)y, it->w = (int16_t)w, it->h = (int16_t)h;
  it->clip = (uint8_t)cur_clip;
  it->fx = 0, it->r = 0, it->a = 0, it->b = 0, it->s = 0;
  return it;
}

void g_rect(int x, int y, int w, int h, C c) {
  item_t *it = add(I_RECT, x, y, w, h);
  if (it) it->a = ce_pal(c);
}
void g_line_h(int x, int y, int w, C c) { g_rect(x, y, w, 1, c); }
void g_rrect(int x, int y, int w, int h, int r, C c) {
  if (r > MAXR) r = MAXR;
  if (r > w / 2) r = w / 2;
  if (r > h / 2) r = h / 2;
  item_t *it = add(r > 0 ? I_RRECT : I_RECT, x, y, w, h);
  if (it) it->a = ce_pal(c), it->r = (uint8_t)r;
}
void g_shade(int x, int y, int w, int h, int r, int alpha) {
  if (r > MAXR) r = MAXR;
  if (r > w / 2) r = w / 2;
  if (r > h / 2) r = h / 2;
  item_t *it = add(I_SHADE, x, y, w, h);
  if (it) it->a = (uint16_t)alpha, it->r = (uint8_t)r;
}
void g_sprite(int id, int x, int y, int fx, int scale) {
  if (id < 0 || id >= ART_NSPRITES) return;
  const art_sprite_t *s = &art_sprites[id];
  int w = s->w, h = s->h;
  if (scale != 256) {
    int nw = (w * scale + 128) >> 8, nh = (h * scale + 128) >> 8;
    x += (w - nw) / 2, y += (h - nh) / 2;
    w = nw, h = nh;
  }
  item_t *it = add(I_SPRITE, x, y, w, h);
  if (it) it->a = (uint16_t)id, it->fx = (uint8_t)fx, it->b = (uint16_t)scale;
}
void g_sprite_wh(int id, int x, int y, int w, int h, int fx) {
  item_t *it = add(I_SPRITE, x, y, w, h);
  if (it) it->a = (uint16_t)id, it->fx = (uint8_t)fx, it->b = 0;
}
void g_rsquare(int cx, int cy, int hs, int angle, C c) {
  int R = hs * 3 / 2 + 1;
  item_t *it = add(I_RSQ, cx - R, cy - R, 2 * R + 1, 2 * R + 1);
  if (it) it->a = ce_pal(c), it->r = (uint8_t)hs, it->fx = (uint8_t)angle;
}
void g_flame(int k, int x, int y, int w, int h, int top) {
  if (!flame_on()) return;
  item_t *it = add(I_FLAME, x, y, w, h);
  if (it) it->a = (uint16_t)k, it->b = (uint16_t)top;
}
void g_bg(int swirl, C solid) {
  bg_swirl = swirl;
  bg_solid = ce_pal(solid);
}

/* ------------------------------------------------------------------ text */
static const C code_col[17] = {
    0, 0xFFFF, HEXC(0x009DFF), HEXC(0xFE5F55), HEXC(0xFF9A00), HEXC(0xF3B958), HEXC(0x4BC292), HEXC(0x88888A),
    HEXC(0xA782D1), HEXC(0x13AFCE), 0, HEXC(0x4584FA), HEXC(0x4F6367), 0, 0xFFFF, HEXC(0x4CA893), 0,
};

static const glyph_t *font(int f) { return (f & T_LARGE) ? font_large : (f & T_MED) ? font_med : font_small; }
static const uint8_t *font_bits(int f) {
  return (f & T_LARGE) ? font_large_bits : (f & T_MED) ? font_med_bits : font_small_bits;
}
static int glyph_rows(int f) { return (f & T_LARGE) ? FONT_LARGE_H : (f & T_MED) ? FONT_MED_H : FONT_SMALL_H; }
int g_cap(int f) { return ((f & T_LARGE) ? 11 : (f & T_MED) ? 9 : 7) * ((f & T_X2) ? 2 : 1); }
int g_pitch(int f) { return ((f & T_LARGE) ? 15 : (f & T_MED) ? 13 : LINE_H) * ((f & T_X2) ? 2 : 1); }

static const glyph_t *glyph(int f, unsigned ch, const uint8_t **bits) {
  if ((f & (T_COND | T_LARGE)) == (T_COND | T_LARGE) && ch >= '0' && ch <= '9') {
    *bits = font_large_digits_bits;
    return &font_large_digits[ch - '0'];
  }
  *bits = font_bits(f);
  return &font(f)[ch - 32];
}
/* the condensed medium size: one column fewer (see src/gfx.c) */
static int drop_col(int f, unsigned ch, const glyph_t *g) {
  if ((f & (T_COND | T_MED)) != (T_COND | T_MED) || f & T_LARGE) return -1;
  if (ch == 'T' || ch == 'I' || ch == '1') return 0;
  if (ch == 'f' || ch == 't') return 4;
  switch (g->w) {
    case 5: return 3;
    case 6: return 2;
    case 7: return 3;
    case 8: return 4;
  }
  return ch == ' ' ? 99 : -1;
}
static int advance(int f, unsigned ch, const glyph_t *g) {
  int a = g->adv;
  if (drop_col(f, ch, g) >= 0) a--;
  if (f & T_TIGHT) a -= ch == ' ' ? 2 : 1;
  if ((f & T_COND) && (ch == ',' || ch == '.')) a--;
  return a;
}

static int textw(const char *s, int f, int one_line) {
  int w = 0, best = 0, badge = 0;
  const uint8_t *bits;
  for (; *s; s++) {
    unsigned char ch = (unsigned char)*s;
    if (ch == '\n') {
      if (badge) w += 2, badge = 0;
      if (w > best) best = w;
      if (one_line) break;
      w = 0;
      continue;
    }
    if (ch < 32) {
      if (ch == 14 && !badge) w += 2, badge = 1;
      else if (badge && ch != 14) w += 2, badge = 0;
      continue;
    }
    if (ch > (f & (T_LARGE | T_MED) ? 126 : 130)) ch = '?';
    w += advance(f, ch, glyph(f, ch, &bits));
  }
  if (badge) w += 2;
  if (w > best) best = w;
  if (best > 0) best--;
  return best * ((f & T_X2) ? 2 : 1);
}
int g_textw(const char *s, int f) { return textw(s, f, 0); }

int g_lines(const char *s) {
  int n = 1;
  for (; *s; s++) n += *s == '\n';
  return n;
}

static item_t *text_item(const char *s, int x, int y, int w, int h) {
  int n = (int)strlen(s);
  if (strpos + n + 1 > (int)sizeof strbuf) return 0;
  item_t *it = add(I_TEXT, x, y, w, h);
  if (!it) return 0;
  char *d = strbuf + strpos;
  memcpy(d, s, (size_t)n + 1);
  strpos += n + 1;
  it->s = d;
  return it;
}

void g_text(const char *s, int x, int y, int f, C c) {
  if (!s || !*s) return;
  item_t *it;
  if (f & T_ROT) {
    int len = g_textw(s, f & ~T_ROT);
    if (f & T_CENTER) y += len / 2;
    it = text_item(s, x - 2, y - len - 2, glyph_rows(f) + 4, len + 4);
  } else {
    int w = g_textw(s, f) + 2;
    if (f & T_CENTER) x -= (w - 2) / 2;
    if (f & T_RIGHT) x -= w - 2;
    int h = g_pitch(f) * (g_lines(s) - 1) + glyph_rows(f) * ((f & T_X2) ? 2 : 1) + 3;
    it = text_item(s, x - 2, y - 2, w + 4, h + 2);
  }
  if (it) it->a = ce_pal(c), it->b = (uint16_t)f;
}

void g_text_box(const char *s, int x, int y, int w, int h, int f, C c) {
  int th = g_cap(f) + g_pitch(f) * (g_lines(s) - 1);
  g_text(s, x + w / 2, y + (h - th + 1) / 2, (f & ~(T_RIGHT)) | T_CENTER, c);
}

/* one glyph's pixels (its shadow first), into the strip rows ra..rb, columns cx0..cx1 */
static void put_glyph(const glyph_t *g, const uint8_t *b, int f, int dc, int x, int y, uint8_t col, int y0, int ra,
                      int rb, int cx0, int cx1, const uint8_t *shadow) {
  int sc = (f & T_X2) ? 2 : 1, gh = glyph_rows(f);
  for (int pass = shadow ? 0 : 1; pass < 2; pass++) {
    unsigned bit = g->off;
    int dy = pass ? 0 : sc;
    for (int gx = 0; gx < g->w; gx++) {
      if (gx == dc) {
        bit += gh;
        continue;
      }
      int px = x + (gx - (dc >= 0 && gx > dc)) * sc;
      for (int gy = 0; gy < gh; gy++, bit++) {
        if (!((b[bit >> 3] >> (bit & 7)) & 1)) continue;
        int py = y + gy * sc + dy;
        for (int k = 0; k < sc * sc; k++) {
          int X = px + (k & (sc - 1)), Y = py + (k >> (sc - 1));
          if (Y >= ra && Y < rb && X >= cx0 && X < cx1) {
            uint8_t *d = strip + (Y - y0) * SCR_W + X;
            *d = pass ? col : shadow[*d];
          }
        }
      }
    }
  }
}

static void draw_text_rot(item_t *it, int y0, int ra, int rb, int cx0, int cx1) {
  int f = it->b, gh = glyph_rows(f);
  int x = it->x + 2, pen = it->y + it->h - 2;
  const uint8_t *sh = ce_shade_tab(12);
  for (const char *s = it->s; *s; s++) {
    unsigned char ch = (unsigned char)*s;
    if (ch < 32 || ch > 126) continue;
    const uint8_t *b;
    const glyph_t *g = glyph(f, ch, &b);
    int dc = drop_col(f, ch, g);
    for (int pass = (f & T_SHADOW) ? 0 : 1; pass < 2; pass++) {
      unsigned bit = g->off;
      for (int gx = 0; gx < g->w; gx++)
        for (int gy = 0; gy < gh; gy++, bit++) {
          if (gx == dc || !((b[bit >> 3] >> (bit & 7)) & 1)) continue;
          int X = x + gy + (pass ? 0 : 1), Y = pen - 1 - (gx - (dc >= 0 && gx > dc));
          if (Y < ra || Y >= rb || X < cx0 || X >= cx1) continue;
          uint8_t *d = strip + (Y - y0) * SCR_W + X;
          *d = pass ? (uint8_t)it->a : sh[*d];
        }
    }
    pen -= advance(f, ch, g);
  }
}

static void draw_text(item_t *it, int y0, int ra, int rb, int cx0, int cx1) {
  int f = it->b;
  if (f & T_ROT) {
    draw_text_rot(it, y0, ra, rb, cx0, cx1);
    return;
  }
  int sc = (f & T_X2) ? 2 : 1, cap = g_cap(f) / sc, pitch = g_pitch(f);
  int x0 = it->x + 2, x = x0, y = it->y + 2;
  int multi = strchr(it->s, '\n') != 0;
  uint8_t col = (uint8_t)it->a, base = col;
  const uint8_t *sh = (f & (T_SHADOW | T_OUTLINE)) ? ce_shade_tab(12) : 0;
  int badge = 0;
  int align = (f & (T_CENTER | T_RIGHT)) && multi, total = it->w - 4;
  if (align) {
    int lw = textw(it->s, f, 1);
    x = x0 + ((f & T_RIGHT) ? total - lw : (total - lw) / 2);
  }
  for (const char *s = it->s; *s; s++) {
    unsigned char ch = (unsigned char)*s;
    if (ch == '\n') {
      badge = 0;
      y += pitch;
      x = x0;
      if (align) {
        int lw = textw(s + 1, f, 1);
        x = x0 + ((f & T_RIGHT) ? total - lw : (total - lw) / 2);
      }
      continue;
    }
    if (ch < 32) {
      if (badge && ch != 14) x += 2 * sc, badge = 0;
      if (ch == 16) col = base;
      else if (ch == 14) {
        /* a red badge behind the following text, until the next code */
        const char *e = s + 1;
        int bw = 0;
        const uint8_t *bb;
        while (*e && (unsigned char)*e >= 32) bw += advance(f, (unsigned char)*e, glyph(f, (unsigned char)*e, &bb)), e++;
        bw = bw * sc + 3;
        int by0 = y - 2, by1 = y + cap * sc + 2;
        uint8_t red = ce_pal(HEXC(0xFE5F55));
        for (int yy = by0 > ra ? by0 : ra; yy < by1 && yy < rb; yy++) {
          uint8_t *d = strip + (yy - y0) * SCR_W;
          int a = x, b = x + bw;
          if (yy == by0 || yy == by1 - 1) a++, b--;
          for (int xx = a < cx0 ? cx0 : a; xx < b && xx < cx1; xx++) d[xx] = red;
        }
        x += 2 * sc;
        col = ce_pal(0xFFFF);
        badge = 1;
      } else if (code_col[ch])
        col = ce_pal(code_col[ch]);
      continue;
    }
    if (ch > (f & (T_LARGE | T_MED) ? 126 : 130)) ch = '?';
    const uint8_t *b;
    const glyph_t *g = glyph(f, ch, &b);
    int adv = advance(f, ch, g) * sc;
    if (x < cx1 && x + adv + sc > cx0 && y < rb && y + glyph_rows(f) * sc + sc > ra)
      put_glyph(g, b, f, drop_col(f, ch, g), x, y, col, y0, ra, rb, cx0, cx1, sh);
    x += adv;
  }
}

/* ------------------------------------------------------------------ sprites */
/* editions: a level a 2 x 2 cell of the card, a frame every ED_DT_MS, each card a little behind the next */
static const uint8_t *ed_pattern(int ed, int x) {
  int f = (int)((g_time / ED_DT_MS + (unsigned)(x / 12)) % ED_FRAMES);
  return ce_edpat + ((ed - 1) * ED_FRAMES + f) * (ED_CH * ED_CW / 2);
}

static uint8_t rowbuf[256];

static void draw_sprite(item_t *it, int y0, int ra, int rb, int cx0, int cx1) {
  int id = it->a;
  const uint8_t *spr = ce_sprite(id);
  if (!spr) return;
  const art_sprite_t *s = &art_sprites[id];
  int w = it->w, h = it->h, sw = s->w, sh = s->h, fx = it->fx;
  int ya = it->y > ra ? it->y : ra, yb = it->y + h < rb ? it->y + h : rb;
  int xa = it->x > cx0 ? it->x : cx0, xb = it->x + w < cx1 ? it->x + w : cx1;
  if (xa >= xb || ya >= yb) return;
  int same = w == sw && h == sh, ed = fx & FX_EDMASK;
  const uint8_t *tab = 0, *dim = (fx & FX_DIM) ? ce_lut + LUT_SHADE + 14 * CE_NSTATIC : 0;
  const uint8_t *grey = (fx & FX_GREY) ? ce_lut + LUT_GREY : 0, *pat = ed ? ed_pattern(ed, it->x) : 0;
  const uint8_t *edl = ed ? ce_lut + LUT_EDLUT + (ed - 1) * ED_LEVELS * CE_NSTATIC : 0;
  if (fx & FX_SHADOW) tab = ce_shade_tab(11);
  int plain = !(fx & ~FX_SHADOW);
  for (int y = ya; y < yb; y++) {
    int sy = same ? y - it->y : (int)((unsigned)(y - it->y) * sh / h);
    const uint8_t *row = spr + (spr[2 * sy] | spr[2 * sy + 1] << 8);
    uint8_t *d = strip + (y - y0) * SCR_W;
    if (same && plain) {
      /* the runs straight in (or their shadow) */
      int x = it->x, end = it->x + sw;
      while (x < end) {
        x += row[0];
        int n = row[1];
        const uint8_t *p = row + 2;
        row = p + n;
        int a = x < xa ? xa : x, b = x + n > xb ? xb : x + n;
        if (a < b) {
          if (tab)
            ce_shade(d + a, tab, (unsigned)(b - a));
          else
            memcpy(d + a, p + (a - x), (size_t)(b - a));
        }
        x += n;
      }
      continue;
    }
    /* the row unpacked, then each pixel with its effects */
    memset(rowbuf, 255, (size_t)sw);
    for (int x = 0; x < sw;) {
      x += row[0];
      int n = row[1];
      memcpy(rowbuf + x, row + 2, (size_t)n);
      row += 2 + n;
      x += n;
    }
    const uint8_t *prow = pat ? pat + (sy >> 1 < ED_CH ? sy >> 1 : ED_CH - 1) * (ED_CW / 2) : 0;
    unsigned step = ((unsigned)sw << 8) / (unsigned)w, sx8 = (unsigned)(xa - it->x) * step;
    for (int x = xa; x < xb; x++, sx8 += step) {
      int sx = same ? x - it->x : (int)(sx8 >> 8);
      int v = rowbuf[sx];
      if (v == 255) continue;
      if (fx & FX_SHADOW) {
        d[x] = tab[d[x]];
        continue;
      }
      if (fx & FX_WHITE) {
        d[x] = CE_WHITE;
        continue;
      }
      if (edl) {
        int cx = sx >> 1 < ED_CW ? sx >> 1 : ED_CW - 1;
        int lv = (prow[cx >> 1] >> ((cx & 1) * 4)) & 15;
        v = edl[lv * CE_NSTATIC + v];
      }
      if (grey) v = grey[v];
      if (dim) v = dim[v];
      d[x] = (uint8_t)v;
    }
  }
}

/* ------------------------------------------------------------------ shapes */
static void draw_rsq(item_t *it, int y0, int ra, int rb, int cx0, int cx1) {
  static const int16_t q[17] = {256, 255, 251, 245, 237, 226, 213, 198, 181, 162, 142, 121, 98, 74, 50, 25, 0};
  int i = (it->fx & 63) >> 2, co = q[i], si = q[16 - i];
  int hs = it->r * 256 + 128, R = it->w / 2, cx = it->x + R, cy = it->y + R;
  int ya = it->y > ra ? it->y : ra, yb = it->y + it->h < rb ? it->y + it->h : rb;
  int xa = it->x > cx0 ? it->x : cx0, xb = it->x + it->w < cx1 ? it->x + it->w : cx1;
  for (int y = ya; y < yb; y++) {
    uint8_t *d = strip + (y - y0) * SCR_W;
    int dy = y - cy;
    for (int x = xa; x < xb; x++) {
      int dx = x - cx;
      int u = dx * co + dy * si, v = dy * co - dx * si;
      if (u < 0) u = -u;
      if (v < 0) v = -v;
      if (u <= hs && v <= hs) d[x] = (uint8_t)it->a;
    }
  }
}

static void draw_rrect(item_t *it, int y0, int ra, int rb, int cx0, int cx1, int shade) {
  int r = it->r, x = it->x, w = it->w, h = it->h;
  int ya = it->y > ra ? it->y : ra, yb = it->y + h < rb ? it->y + h : rb;
  int xa = x > cx0 ? x : cx0, xb = x + w < cx1 ? x + w : cx1;
  uint8_t c = (uint8_t)it->a;
  const uint8_t *tab = shade ? ce_shade_tab(it->a) : 0;
  for (int yy = ya; yy < yb; yy++) {
    int j = yy - it->y, cy = -1;
    if (j < r) cy = j;
    else if (j >= h - r) cy = h - 1 - j;
    uint8_t *d = strip + (yy - y0) * SCR_W;
    int ma = xa, mb = xb;
    if (cy >= 0) {
      if (ma < x + r) ma = x + r;
      if (mb > x + w - r) mb = x + w - r;
    }
    if (ma < mb) {
      if (shade)
        ce_shade(d + ma, tab, (unsigned)(mb - ma));
      else
        memset(d + ma, c, (size_t)(mb - ma));
    }
    if (cy < 0) continue;
    for (int side = 0; side < 2; side++) {
      int ea = side ? (mb > xa ? mb : xa) : xa, eb = side ? xb : (ma < xb ? ma : xb);
      for (int xx = ea; xx < eb; xx++) {
        int i = xx - x;
        int a = cov[r][cy][i < r ? i : w - 1 - i];
        if (!a) continue;
        if (a < 16) continue;
        d[xx] = shade ? tab[d[xx]] : c;
      }
    }
  }
}

static void draw_rect(item_t *it, int y0, int ra, int rb, int cx0, int cx1) {
  int ya = it->y > ra ? it->y : ra, yb = it->y + it->h < rb ? it->y + it->h : rb;
  int xa = it->x > cx0 ? it->x : cx0, xb = it->x + it->w < cx1 ? it->x + it->w : cx1;
  if (xa >= xb) return;
  for (int y = ya; y < yb; y++) memset(strip + (y - y0) * SCR_W + xa, it->a, (size_t)(xb - xa));
}

/* ------------------------------------------------------------------ the frame */
static unsigned mix(unsigned h, unsigned v) {
  h ^= v;
  return (h << 5) + h + (h >> 13);
}

static unsigned item_hash(const item_t *it) {
  unsigned h = mix(mix(mix((unsigned)(uint16_t)it->x, (unsigned)(uint16_t)it->y), (unsigned)it->w),
                   (unsigned)it->h);
  h = mix(h, (unsigned)it->type | (unsigned)it->r << 8 | (unsigned)it->fx << 16);
  h = mix(h, (unsigned)it->a ^ (unsigned)it->b << 9);
  const int16_t *c = clips[it->clip];
  h = mix(h, (unsigned)(c[0] ^ c[1] << 9) ^ (unsigned)(c[2] << 3 ^ c[3] << 12));
  if (it->s)
    for (const char *p = it->s; *p; p++) h = mix(h, (unsigned char)*p);
  /* what moves on its own: the editions' sheen, the fire */
  if (it->type == I_SPRITE && (it->fx & FX_EDMASK)) h = mix(h, g_time / ED_DT_MS);
  if (it->type == I_FLAME) h = mix(h, (unsigned)ce_flame_frame());
  return h;
}

#ifdef NP_HOST
/* how full the frame's lists get (tools/host.c prints it) */
int ce_max_items, ce_max_str;
#endif

void g_end(void) {
#ifdef NP_HOST
  if (nitems > ce_max_items) ce_max_items = nitems;
  if (strpos > ce_max_str) ce_max_str = strpos;
#endif
  unsigned hs[NSTRIP];
  unsigned base = mix(mix((unsigned)bg_swirl, (unsigned)bg_cover_x), (unsigned)bg_solid ^ (unsigned)ce_bg_ver << 8);
  for (int s = 0; s < NSTRIP; s++) hs[s] = base + (unsigned)s;
  for (int i = 0; i < nitems; i++) {
    const item_t *it = &items[i];
    int s0 = it->y < 0 ? 0 : it->y / SH, s1 = (it->y + it->h - 1) / SH;
    if (s1 >= NSTRIP) s1 = NSTRIP - 1;
    if (s0 > s1) continue;
    unsigned h = item_hash(it) + (unsigned)i;
    for (int s = s0; s <= s1; s++) hs[s] = mix(hs[s], h);
  }
  for (int s = 0; s < NSTRIP; s++) {
    if (drawn_once && hs[s] == strip_hash[s]) continue;
    strip_hash[s] = hs[s];
    int y0 = s * SH, y1 = y0 + SH;
    uint16_t dyn = 0;
    if (bg_swirl) {
      for (int y = y0; y < y1; y++) {
        uint8_t *d = strip + (y - y0) * SCR_W;
        if (bg_cover_x > 0) memset(d, bg_solid, (size_t)(bg_cover_x < SCR_W ? bg_cover_x : SCR_W));
        if (bg_cover_x < SCR_W) ce_bg_row(d, y, bg_cover_x);
      }
    } else
      memset(strip, bg_solid, sizeof strip);
    for (int i = 0; i < nitems; i++) {
      item_t *it = &items[i];
      if (it->y >= y1 || it->y + it->h <= y0) continue;
      const int16_t *c = clips[it->clip];
      int cy0 = y0 > c[1] ? y0 : c[1], cy1 = y1 < c[3] ? y1 : c[3];
      if (cy0 >= cy1) continue;
      if (it->type != I_SPRITE && it->type != I_SHADE && it->type != I_FLAME && it->a >= CE_DYN0 && it->a < CE_SWIRL0)
        dyn |= (uint16_t)(1u << (it->a - CE_DYN0));
      switch (it->type) {
        case I_RECT: draw_rect(it, y0, cy0, cy1, c[0], c[2]); break;
        case I_RRECT: draw_rrect(it, y0, cy0, cy1, c[0], c[2], 0); break;
        case I_SHADE: draw_rrect(it, y0, cy0, cy1, c[0], c[2], 1); break;
        case I_SPRITE: draw_sprite(it, y0, cy0, cy1, c[0], c[2]); break;
        case I_TEXT: draw_text(it, y0, cy0, cy1, c[0], c[2]); break;
        case I_RSQ: draw_rsq(it, y0, cy0, cy1, c[0], c[2]); break;
        case I_FLAME: ce_flame_rows(it->a & 1, it->x, it->y, it->w, it->h, it->b, strip, y0, cy0, cy1, c[0], c[2]); break;
      }
    }
    strip_dyn[s] = dyn;
    memcpy(ce_vram + y0 * SCR_W, strip, sizeof strip);
  }
  drawn_once = 1;
}
