/* NumView: one file of yours (a picture, a PDF, a video, a book) as an app on the calculator.
 *
 * The file comes in a table in the app's flash, np_media (media.h: tools/mediapack.py, or NumPlay Studio), read in
 * place. The app shows it and nothing else: the screen is the file. A control appears only while it's used and goes
 * away on its own:
 *   pictures and documents: + and - (or OK and Back) zoom, the arrows move around, a small map of the page shows
 *     where you are; at full view the arrows turn the pages, whose number shows for a moment;
 *   video: it plays at once; OK pauses and shows where you are, Left and Right go back and forward 5 seconds;
 *   books: the text set for the screen in Nunito, the arrows and OK turn the pages, + and - change its size.
 * Back leaves (once zoomed all the way out), Home leaves from anywhere.
 *
 * Pictures are tiles of JPEG or packed palette pixels, two levels of them: one that fits the screen, one four times
 * as big to zoom into, decoded at full size or 1/2, 1/4, 1/8 (jpeg.c), a row of tiles at a time into a cache from
 * which the screen is put together in strips, with the controls drawn over them. */
#include <eadk.h>
#include <string.h>
#include "../../common/epsilon_app.h"
#include "gfx.h"
#include "inflate.h"
#include "jpeg.h"
#include "media.h"
#include "read_font.h"

const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
/* the kinds of file this build shows (1 pictures, 2 video, 4 books): NumPlay Studio takes the one for a file's kind,
   so a picture doesn't bring the books' fonts along */
#ifndef NV_KINDS
#define NV_KINDS 7
#endif
/* the file (eadk_app_name, the app's name, comes with it), up to the end of its block (media.ld) */
extern const uint8_t np_media[] __attribute__((weak));
extern const uint8_t np_numview_end[];

void np_push(int x, int y, int w, int h, const color_t *px) {
  eadk_display_push_rect((eadk_rect_t){(uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h}, px);
}
static void fill_screen(int x, int y, int w, int h, color_t c) {
  if (w > 0 && h > 0) eadk_display_push_rect_uniform((eadk_rect_t){(uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h}, c);
}
void *np_alloc(uint32_t n) { return NULL; } /* (gfx_init's: not used here) */

#define KEY(k) (1ull << (k))
#define WHITE 0xFFFF
#define BLACK 0x0000
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

static const nv_header_t *hdr;
static jpeg_t jpg;
static color_t strip[SCREEN_W * STRIP_H];
static uint8_t lz[4096]; /* a palette tile's pixels, a book's chunk */
static uint32_t inflate_tables[1024]; /* (np_inflate's: 3.3 KB) */

/* ------------------------------------------------------------------ keys */
static uint64_t down, hit;
static uint32_t rep_at;
static bool rep;
static void keys(uint32_t now) {
  uint64_t k = eadk_keyboard_scan();
  hit = k & ~down;
  down = k;
  rep = false;
  if (hit) rep_at = now + 300;
  else if (down && (int32_t)(now - rep_at) >= 0) rep = true, rep_at = now + 40;
}
static bool pressed(int k) { return hit & KEY(k); }
static bool held(int k) { return (hit & KEY(k)) || (rep && (down & KEY(k))); }
static bool quit_key(void) { return down & (KEY(eadk_key_home) | KEY(eadk_key_on_off)); }

static int u16at(const uint8_t *p) { return p[0] | p[1] << 8; }

/* ------------------------------------------------------------------ small things drawn over the file */
static void chip(int cx, int y, const char *s) {
  int w = gfx_text_width(&np_font_small, s) + 16;
  gfx_rrect(cx - w / 2, y, w, 18, 6, BLACK, 150);
  gfx_text_center(&np_font_small, cx, y + 13, s, WHITE, 256);
}
static char *num(char *p, int v) {
  char t[12];
  int n = 0;
  do t[n++] = (char)('0' + v % 10), v /= 10;
  while (v);
  while (n) *p++ = t[--n];
  *p = 0;
  return p;
}
static void time_str(char *p, int s) {
  p = num(p, s / 60);
  *p++ = ':', *p++ = (char)('0' + s % 60 / 10), *p++ = (char)('0' + s % 10), *p = 0;
}

/* ------------------------------------------------------------------ tiles */
/* a palette tile at 1/d into out */
static void palette_tile(const uint8_t *p, uint32_t n, int tw, int th, int d, color_t *out, int stride) {
  int nc = p[0] + 1, bpp = nc <= 2 ? 1 : nc <= 4 ? 2 : nc <= 16 ? 4 : 8, rowb = (tw * bpp + 7) / 8;
  color_t pal[256];
  for (int i = 0; i < nc; i++) pal[i] = (color_t)u16at(p + 2 + 2 * i);
  uint32_t head = 2 + 2 * (uint32_t)nc;
  if (n < head || np_inflate(lz, sizeof lz, p + head, n - head, (np_inflate_tables_t *)inflate_tables) < rowb * th) return;
  int mask = (1 << bpp) - 1, ow = (tw + d - 1) / d, oh = (th + d - 1) / d;
#define IDX(x, y) ((lz[(y)*rowb + (x)*bpp / 8] >> (8 - bpp - (x)*bpp % 8)) & mask)
  for (int oy = 0; oy < oh; oy++) {
    color_t *o = out + oy * stride;
    if (d == 1) {
      for (int x = 0; x < tw; x++) o[x] = pal[IDX(x, oy)];
      continue;
    }
    for (int ox = 0; ox < ow; ox++) {
      int r = 0, g = 0, b = 0, k = 0;
      for (int y = oy * d; y < MIN(oy * d + d, th); y++)
        for (int x = ox * d; x < MIN(ox * d + d, tw); x++, k++) {
          color_t c = pal[IDX(x, y)];
          r += c >> 11, g += (c >> 5) & 63, b += c & 31;
        }
      o[ox] = (color_t)((r / k) << 11 | (g / k) << 5 | b / k);
    }
  }
#undef IDX
}
/* a tile (codec byte and data) of tw x th pixels, at 1/d into out */
static void tile(const uint8_t *t, uint32_t n, int tw, int th, int d, color_t *out, int stride) {
  int ow = (tw + d - 1) / d, oh = (th + d - 1) / d;
  if (n >= 3 && t[0] == NV_SOLID) {
    color_t c = (color_t)u16at(t + 1);
    for (int y = 0; y < oh; y++)
      for (int x = 0; x < ow; x++) out[y * stride + x] = c;
  } else if (n >= 1 && t[0] == NV_JPEG) {
    jpeg_scan(&jpg, t + 1, n - 1, tw, th, d, out, stride);
  } else if (n >= 1 && t[0] == NV_PALETTE) {
    palette_tile(t + 1, n - 1, tw, th, d, out, stride);
  }
}

/* ================================================================== pictures and documents */
typedef struct {
  uint8_t lv, d, m;
  uint16_t w, h; /* on the screen */
} step_t;
static struct {
  int pages, page, nlv, shift, nst, z;
  nv_level_t lv[4];
  step_t st[8];
  int ox, oy;         /* where the picture's top left is on the screen */
  uint32_t cx, cy;    /* the middle of the view, in 1/65536 of the picture */
  bool doc;
  uint32_t note_until; /* the page number and the map show until then */
  int mw, mh;          /* the map's size */
} pv;

#define CACHE_PX (6 * 64 * 64)
#define MAP_W 80 /* the map of a zoomed picture: its screen level at 1/4 or 1/8, this size at most */
#define MAP_H 64
static union {
  struct {
    color_t cache[CACHE_PX]; /* a row of tiles */
    color_t map[MAP_W * MAP_H];
  } pic;
  struct {
    color_t tile[64 * 64];
    color_t under[SCREEN_W * 32]; /* what the controls cover */
  } vid;
  struct {
    uint8_t chunk[2][NV_CHUNK];
    uint32_t page_at[4097];
  } book;
} ram;

static const uint8_t *page_rec(int i) {
  const uint32_t *idx = (const uint32_t *)(np_media + hdr->index);
  return np_media + idx[i];
}
static void level_tile(const nv_level_t *L, int c, int r, int d, color_t *out, int stride) {
  int T = 1 << pv.shift, cols = (L->w + T - 1) >> pv.shift;
  const uint32_t *offs = (const uint32_t *)(np_media + L->tiles);
  uint32_t a = offs[r * cols + c], b = offs[r * cols + c + 1];
  tile(np_media + a, b - a, MIN(T, L->w - c * T), MIN(T, L->h - r * T), d, out, stride);
}

static void page_open(int i) {
  const uint8_t *p = page_rec(i);
  pv.page = i, pv.nlv = MIN(p[0], 4), pv.shift = p[1];
  for (int k = 0; k < pv.nlv; k++) memcpy(&pv.lv[k], p + 4 + 8 * k, sizeof(nv_level_t));
  /* the zoom steps: the screen's level, then 1/8 .. 1/1 of the bigger ones, each a good deal bigger than the last */
  pv.nst = 0;
  pv.st[pv.nst++] = (step_t){0, 1, 1, pv.lv[0].w, pv.lv[0].h};
  for (int k = 1; k < pv.nlv; k++)
    for (int d = 8; d >= 1; d /= 2) {
      int w = (pv.lv[k].w + d - 1) / d, h = (pv.lv[k].h + d - 1) / d;
      const step_t *last = &pv.st[pv.nst - 1];
      if (w * 10 >= last->w * 15 && pv.nst < 6) pv.st[pv.nst++] = (step_t){(uint8_t)k, (uint8_t)d, 1, (uint16_t)w, (uint16_t)h};
    }
  /* (a small picture: its pixels made bigger) */
  while (pv.nst < 3) {
    step_t s = pv.st[pv.nst - 1];
    if (s.d > 1) s.d /= 2;
    else s.m *= 2;
    const nv_level_t *L = &pv.lv[s.lv];
    s.w = (uint16_t)((L->w + s.d - 1) / s.d * s.m), s.h = (uint16_t)((L->h + s.d - 1) / s.d * s.m);
    pv.st[pv.nst++] = s;
  }
  if (pv.z >= pv.nst) pv.z = pv.nst - 1;
  /* the map */
  const nv_level_t *L = &pv.lv[0];
  int d = (L->w + 3) / 4 <= MAP_W && (L->h + 3) / 4 <= MAP_H ? 4 : 8, T = 1 << pv.shift;
  pv.mw = (L->w + d - 1) / d, pv.mh = (L->h + d - 1) / d;
  for (int r = 0; r * T < L->h; r++)
    for (int c = 0; c * T < L->w; c++)
      level_tile(L, c, r, d, ram.pic.map + r * (T / d) * MAP_W + c * (T / d), MAP_W);
}

/* the view from its middle: where the picture goes (centered when smaller than the screen) */
static void place(void) {
  const step_t *s = &pv.st[pv.z];
  if (s->w <= SCREEN_W) pv.ox = (SCREEN_W - s->w) / 2;
  else pv.ox = MAX(SCREEN_W - s->w, MIN(0, SCREEN_W / 2 - (int)((uint64_t)pv.cx * s->w >> 16)));
  if (s->h <= SCREEN_H) pv.oy = (SCREEN_H - s->h) / 2;
  else pv.oy = MAX(SCREEN_H - s->h, MIN(0, SCREEN_H / 2 - (int)((uint64_t)pv.cy * s->h >> 16)));
  if (s->w > SCREEN_W) pv.cx = (uint32_t)(((uint64_t)(SCREEN_W / 2 - pv.ox) << 16) / s->w);
  if (s->h > SCREEN_H) pv.cy = (uint32_t)(((uint64_t)(SCREEN_H / 2 - pv.oy) << 16) / s->h);
}

static bool note_on(void) { return (int32_t)(pv.note_until - (uint32_t)eadk_timing_millis()) > 0; }

/* the controls over the picture: the page number, and the map while zoomed */
static void pic_overlay(void) {
  if (!note_on()) return;
  if (gfx_y1 <= SCREEN_H - 12 - MAP_H) return;
  if (pv.pages > 1) {
    char s[24], *p = num(s, pv.page + 1);
    *p++ = ' ', *p++ = '/', *p++ = ' ';
    num(p, pv.pages);
    chip(SCREEN_W / 2, SCREEN_H - 26, s);
  }
  if (pv.z > 0) {
    const step_t *s = &pv.st[pv.z];
    int x0 = SCREEN_W - 10 - pv.mw, y0 = SCREEN_H - 10 - pv.mh;
    /* the part on the screen, in the map's pixels */
    int vx0 = (-pv.ox) * pv.mw / s->w, vy0 = (-pv.oy) * pv.mh / s->h;
    int vx1 = (SCREEN_W - pv.ox) * pv.mw / s->w, vy1 = (SCREEN_H - pv.oy) * pv.mh / s->h;
    vx0 = MAX(vx0, 0), vy0 = MAX(vy0, 0), vx1 = MIN(vx1, pv.mw), vy1 = MIN(vy1, pv.mh);
    gfx_rrect(x0 - 2, y0 - 2, pv.mw + 4, pv.mh + 4, 4, BLACK, 110);
    for (int y = MAX(y0, gfx_y0); y < MIN(y0 + pv.mh, gfx_y1); y++) {
      color_t *o = strip + (y - gfx_y0) * SCREEN_W + x0;
      const color_t *m = ram.pic.map + (y - y0) * MAP_W;
      bool in_y = y - y0 >= vy0 && y - y0 < vy1;
      for (int x = 0; x < pv.mw; x++) o[x] = in_y && x >= vx0 && x < vx1 ? m[x] : gfx_mix(m[x], BLACK, 11);
    }
    gfx_rrect_outline(x0 + vx0 - 1, y0 + vy0 - 1, vx1 - vx0 + 2, vy1 - vy0 + 2, 0, 1, WHITE, 256);
  }
}

/* rows y0..y1 of the backdrop, with the controls */
static void backdrop_rows(int y0, int y1) {
  color_t bg = hdr->backdrop;
  for (int y = y0; y < y1; y += STRIP_H) {
    gfx_y0 = y, gfx_y1 = MIN(y + STRIP_H, y1);
    for (int i = 0; i < (gfx_y1 - y) * SCREEN_W; i++) strip[i] = bg;
    pic_overlay();
    np_push(0, y, SCREEN_W, gfx_y1 - y, strip);
  }
}

/* draws rows y0..y1 of the screen */
static void pic_render(int y0, int y1) {
  const step_t *s = &pv.st[pv.z];
  const nv_level_t *L = &pv.lv[s->lv];
  int T = 1 << pv.shift, d = s->d, m = s->m, ts = T / d, tm = ts * m;
  int cols = (L->w + T - 1) / T, ox = pv.ox, oy = pv.oy, W = s->w, H = s->h;
  int c0 = ox < 0 ? -ox / tm : 0, c1 = MIN(cols, (SCREEN_W - ox + tm - 1) / tm);
  int cs = (c1 - c0) * ts;
  color_t bg = hdr->backdrop;
  for (int y = y0; y < y1;) {
    int iy = y - oy;
    if (iy < 0 || iy >= H) {
      int e = iy < 0 ? MIN(y1, oy) : y1;
      backdrop_rows(y, e);
      y = e;
      continue;
    }
    int r = iy / tm, e = MIN(y1, oy + MIN((r + 1) * tm, H));
    for (int c = c0; c < c1; c++) level_tile(L, c, r, d, ram.pic.cache + (c - c0) * ts, cs);
    for (int sy = y; sy < e; sy += STRIP_H) {
      int se = MIN(sy + STRIP_H, e);
      int xa = MAX(0, ox), xb = MIN(SCREEN_W, ox + W);
      for (int yy = sy; yy < se; yy++) {
        color_t *o = strip + (yy - sy) * SCREEN_W;
        const color_t *src = ram.pic.cache + ((yy - oy - r * tm) / m) * cs;
        for (int x = 0; x < xa; x++) o[x] = bg;
        if (m == 1) memcpy(o + xa, src + (xa - ox - c0 * ts), (size_t)(xb - xa) * sizeof(color_t));
        else
          for (int x = xa; x < xb; x++) o[x] = src[(x - ox) / m - c0 * ts];
        for (int x = xb; x < SCREEN_W; x++) o[x] = bg;
      }
      gfx_y0 = sy, gfx_y1 = se;
      pic_overlay();
      np_push(0, sy, SCREEN_W, se - sy, strip);
    }
    y = e;
  }
}

static void zoom_to(int z) {
  if (z < 0 || z >= pv.nst || z == pv.z) return;
  pv.z = z;
  place();
}

static void pictures(void) {
  pv.pages = (int)hdr->count, pv.doc = hdr->flags & NV_DOCUMENT;
  pv.cx = pv.cy = 32768;
  page_open(0);
  place();
  bool shown = pv.pages > 1;
  if (shown) pv.note_until = (uint32_t)eadk_timing_millis() + 1600; /* (how many pages) */
  pic_render(0, SCREEN_H);
  for (;;) {
    uint32_t now = (uint32_t)eadk_timing_millis();
    keys(now);
    if (quit_key()) return;
    int z = pv.z, page = pv.page, ox = pv.ox, oy = pv.oy;
    if (pressed(eadk_key_plus) || pressed(eadk_key_ok) || pressed(eadk_key_exe)) zoom_to(pv.z + 1);
    if (pressed(eadk_key_minus)) zoom_to(pv.z - 1);
    if (pressed(eadk_key_back)) {
      if (pv.z == 0) return;
      zoom_to(0);
    }
    const step_t *s = &pv.st[pv.z];
    int dx = (held(eadk_key_right) ? 1 : 0) - (held(eadk_key_left) ? 1 : 0);
    int dy = (held(eadk_key_down) ? 1 : 0) - (held(eadk_key_up) ? 1 : 0);
    int step = rep ? 28 : 48;
    if (pv.z == 0 || (s->w <= SCREEN_W && s->h <= SCREEN_H)) {
      /* the whole page on the screen: the arrows turn the pages */
      int to = pv.page + ((hit & (KEY(eadk_key_right) | KEY(eadk_key_down))) ? 1 : 0) -
               ((hit & (KEY(eadk_key_left) | KEY(eadk_key_up))) ? 1 : 0);
      if (to != pv.page && to >= 0 && to < pv.pages) page_open(to), pv.cx = pv.cy = 32768, place();
    } else if (dx || dy) {
      /* pushed past the bottom of a page: the next one from its top; past its top, the one before from its bottom */
      if (dy > 0 && pv.oy + s->h <= SCREEN_H && (hit & KEY(eadk_key_down)) && pv.page + 1 < pv.pages) {
        page_open(pv.page + 1), pv.cy = 0;
      } else if (dy < 0 && pv.oy >= 0 && (hit & KEY(eadk_key_up)) && pv.page > 0) {
        page_open(pv.page - 1), pv.cy = 65535;
      } else {
        int nx = pv.ox - dx * step, ny = pv.oy - dy * step;
        if (s->w > SCREEN_W) pv.cx = (uint32_t)(((uint64_t)MAX(0, SCREEN_W / 2 - nx) << 16) / s->w);
        if (s->h > SCREEN_H) pv.cy = (uint32_t)(((uint64_t)MAX(0, SCREEN_H / 2 - ny) << 16) / s->h);
      }
      place();
    }
    bool moved = z != pv.z || page != pv.page || ox != pv.ox || oy != pv.oy;
    if (moved) {
      pv.note_until = now + 1600;
      pic_render(0, SCREEN_H);
      shown = true;
    } else if (shown && !note_on()) {
      shown = false;
      pic_render(SCREEN_H - 14 - MAP_H, SCREEN_H); /* (the controls away) */
    }
    eadk_timing_msleep(10);
  }
}

/* ================================================================== video */
static struct {
  const nv_video_t *v;
  int n, cols, rows, x0, y0, f; /* f: the next frame to show */
  bool playing, bar;
  uint32_t base, hold_until;
} vv;
#define BAR_Y (SCREEN_H - 32)

static uint32_t frame_ms(int f) { return (uint32_t)((uint64_t)f * 100000 / vv.v->fps100); }

static void frame(int f) {
  const nv_video_t *v = vv.v;
  const uint8_t *p = np_media + v->frame[f], *end = np_media + v->frame[f + 1];
  int nt = vv.cols * vv.rows;
  const uint8_t *bits = p;
  p += (nt + 7) / 8;
  for (int i = 0; i < nt && p + 2 <= end; i++) {
    if (!(bits[i >> 3] >> (i & 7) & 1)) continue;
    uint32_t n = (uint32_t)u16at(p);
    p += 2;
    int c = i % vv.cols, r = i / vv.cols;
    int tw = MIN(v->tw, v->w - c * v->tw), th = MIN(v->th, v->h - r * v->th);
    if (p[0] == NV_SOLID) fill_screen(vv.x0 + c * v->tw, vv.y0 + r * v->th, tw, th, (color_t)u16at(p + 1));
    else {
      tile(p, n, tw, th, 1, ram.vid.tile, tw);
      np_push(vv.x0 + c * v->tw, vv.y0 + r * v->th, tw, th, ram.vid.tile);
    }
    p += n;
  }
}

/* the bar: where you are, the time and the length, drawn over what's under it */
static void bar_draw(void) {
  int shown = vv.f > 0 ? vv.f - 1 : 0;
  int x0 = 16, x1 = SCREEN_W - 16, w = x1 - x0, at = x0 + (int)((int64_t)w * shown / MAX(1, vv.n - 1));
  char a[12], b[12];
  time_str(a, (int)(frame_ms(shown) / 1000));
  time_str(b, (int)(frame_ms(vv.n) / 1000));
  for (int y = BAR_Y; y < SCREEN_H; y += STRIP_H) {
    gfx_y0 = y, gfx_y1 = MIN(y + STRIP_H, SCREEN_H);
    memcpy(strip, ram.vid.under + (y - BAR_Y) * SCREEN_W, (size_t)(gfx_y1 - y) * SCREEN_W * sizeof(color_t));
    for (int r = BAR_Y; r < SCREEN_H; r++) gfx_fill_alpha(0, r, SCREEN_W, 1, BLACK, (r - BAR_Y) * 5);
    gfx_fill(x0, BAR_Y + 12, w, 2, gfx_mix(BLACK, WHITE, 10));
    gfx_fill(x0, BAR_Y + 12, at - x0, 2, WHITE);
    gfx_circle(at, BAR_Y + 13, 3, WHITE, 256);
    gfx_text(&np_font_small, x0, BAR_Y + 27, a, WHITE, 256);
    gfx_text_right(&np_font_small, x1, BAR_Y + 27, b, gfx_mix(BLACK, WHITE, 20), 256);
    np_push(0, y, SCREEN_W, gfx_y1 - y, strip);
  }
}
static void bar_show(void) {
  if (!vv.bar) eadk_display_pull_rect((eadk_rect_t){0, BAR_Y, SCREEN_W, SCREEN_H - BAR_Y}, ram.vid.under);
  vv.bar = true;
  bar_draw();
}
static void bar_hide(void) {
  if (vv.bar) np_push(0, BAR_Y, SCREEN_W, SCREEN_H - BAR_Y, ram.vid.under);
  vv.bar = false;
}

/* shows frame f, a whole one (a key frame): what follows plays from it */
static void seek(int f, uint32_t now) {
  bool bar = vv.bar;
  bar_hide();
  f = MAX(0, MIN(f, vv.n - 1)) / vv.v->key * vv.v->key;
  frame(f);
  vv.f = f + 1;
  vv.base = now - frame_ms(f);
  if (bar) bar_show();
}

static void video(void) {
  vv.v = (const nv_video_t *)(np_media + hdr->index);
  vv.n = (int)hdr->count;
  vv.cols = (vv.v->w + vv.v->tw - 1) / vv.v->tw, vv.rows = (vv.v->h + vv.v->th - 1) / vv.v->th;
  vv.x0 = (SCREEN_W - vv.v->w) / 2, vv.y0 = (SCREEN_H - vv.v->h) / 2;
  fill_screen(0, 0, SCREEN_W, SCREEN_H, BLACK);
  uint32_t now = (uint32_t)eadk_timing_millis();
  seek(0, now);
  vv.playing = true;
  for (;;) {
    now = (uint32_t)eadk_timing_millis();
    keys(now);
    if (quit_key() || pressed(eadk_key_back)) return;
    if (pressed(eadk_key_ok) || pressed(eadk_key_exe)) {
      if (vv.playing) vv.playing = false, bar_show();
      else {
        if (vv.f >= vv.n) seek(0, now);
        vv.playing = true, vv.base = now - frame_ms(vv.f - 1), vv.hold_until = now;
        bar_hide();
      }
    }
    int jump = (held(eadk_key_right) ? 1 : 0) - (held(eadk_key_left) ? 1 : 0);
    if (jump) {
      /* 5 seconds, from the key frame of the one on the screen */
      int per = MAX(1, (int)(500L * vv.v->fps100 / 100 / 100) / vv.v->key) * vv.v->key;
      int at = (vv.f - 1) / vv.v->key * vv.v->key;
      seek(at + jump * per, now);
      if (vv.playing) vv.hold_until = now + 900;
      bar_show();
    }
    if (vv.playing && (int32_t)(now - vv.hold_until) >= 0) {
      if (vv.bar) bar_hide(), vv.base = now - frame_ms(vv.f - 1);
      if (vv.f >= vv.n) {
        vv.playing = false;
        bar_show();
      } else if ((int32_t)(now - vv.base - frame_ms(vv.f)) >= 0) {
        frame(vv.f++);
        /* (behind by more than a fifth of a second: it slows down rather than rush) */
        if ((int32_t)(now - vv.base - frame_ms(vv.f)) > 200) vv.base = now - frame_ms(vv.f);
        continue;
      }
    }
    eadk_timing_msleep(2);
  }
}

/* ================================================================== books */
#define PAPER HEX(0xFAF8F3)
#define INK HEX(0x23211E)
#define FOLIO HEX(0xA8A49C)
#define BOOK_X 20
#define BOOK_W (SCREEN_W - 2 * BOOK_X)
#define BOOK_TOP 14
#define BOOK_BOTTOM 214
static const np_font_t *const read_fonts[3] = {&nv_read_s, &nv_read_m, &nv_read_l};
static const np_font_t *const head_fonts[3] = {&nv_head_s, &nv_head_m, &nv_head_l};
static const uint8_t line_h[3] = {15, 18, 22}, head_h[3] = {18, 21, 24};

static struct {
  const nv_book_t *b;
  const uint32_t *start, *at;
  uint32_t len;
  int size, pages, page;
  int slot[2], lru;
  const uint8_t *cur;
  uint32_t cur_a, cur_b;
} bk;

static void chunk_for(uint32_t pos) {
  int lo = 0, hi = bk.b->chunks - 1;
  while (lo < hi) {
    int mid = (lo + hi + 1) / 2;
    if (bk.start[mid] <= pos) lo = mid;
    else hi = mid - 1;
  }
  int s = bk.slot[0] == lo ? 0 : bk.slot[1] == lo ? 1 : -1;
  if (s < 0) {
    s = bk.lru;
    bk.lru ^= 1;
    np_inflate(ram.book.chunk[s], NV_CHUNK, np_media + bk.at[lo], bk.at[lo + 1] - bk.at[lo], (np_inflate_tables_t *)inflate_tables);
    bk.slot[s] = lo;
  } else bk.lru = s ^ 1;
  bk.cur = ram.book.chunk[s], bk.cur_a = bk.start[lo], bk.cur_b = bk.start[lo + 1];
}
static inline int byte_at(uint32_t pos) {
  if (pos >= bk.len) return 0;
  if (pos - bk.cur_a >= bk.cur_b - bk.cur_a) chunk_for(pos);
  return bk.cur[pos - bk.cur_a];
}
/* the letter at *pos, *pos moved past it */
static uint32_t letter(uint32_t *pos) {
  uint32_t c = (uint32_t)byte_at((*pos)++);
  if (c < 0x80) return c;
  int n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
  c &= 0x3F >> n;
  while (n--) c = c << 6 | (byte_at((*pos)++) & 0x3F);
  return c;
}
static const np_glyph_t *glyph(const np_font_t *f, uint32_t c) {
  if (c >= 32 && c < 127) return &f->glyphs[c - 32];
  int lo = 0, hi = nv_extra_count - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (nv_extra[mid] == c) return &f->glyphs[95 + mid];
    if (nv_extra[mid] < c) lo = mid + 1;
    else hi = mid - 1;
  }
  return &f->glyphs['?' - 32];
}

typedef struct {
  uint32_t a, b; /* its text */
  int16_t y;     /* its baseline */
  uint8_t head;
} line_t;
static line_t lines[24];
static int nlines;

/* the end of the word at pos (a space, a new line or the end) and its width */
static uint32_t word(uint32_t pos, const np_font_t *f, int *w) {
  *w = 0;
  for (;;) {
    if (pos >= bk.len) return pos;
    int c = byte_at(pos);
    if (c == ' ' || c == '\n') return pos;
    uint32_t q = pos;
    *w += glyph(f, letter(&q))->advance;
    pos = q;
  }
}

/* lays out the page that starts at pos into lines[]; returns where the next one starts */
static uint32_t layout(uint32_t pos, bool keep) {
  int y = BOOK_TOP, n = 0;
  bool first = true;
  while (pos < bk.len) {
    int c = byte_at(pos);
    if (c == 2) {
      if (!first) break;
      pos++;
      continue;
    }
    bool head = c == 1, said = false; /* (said: a line of it is on this page) */
    uint32_t para = pos;
    if (head) pos++;
    const np_font_t *f = head ? head_fonts[bk.size] : read_fonts[bk.size];
    int lh = head ? head_h[bk.size] : line_h[bk.size];
    if (!first) y += head ? lh / 2 + 4 : lh / 3; /* (the space between paragraphs) */
    int sp = glyph(f, ' ')->advance;
    /* the paragraph's lines */
    while (pos < bk.len && byte_at(pos) != '\n') {
      if (y + lh > BOOK_BOTTOM && !first) {
        if (head && !said) pos = para; /* (a heading moves to the next page whole) */
        goto full;
      }
      uint32_t a = pos, b = pos;
      int x = 0;
      for (;;) {
        int w;
        uint32_t e = word(pos, f, &w);
        if (x && x + sp + w > BOOK_W) break;
        if (!x && w > BOOK_W) { /* a word longer than a line: cut it */
          uint32_t q = pos;
          int ww = 0;
          while (q < e) {
            uint32_t r = q;
            int g = glyph(f, letter(&r))->advance;
            if (ww + g > BOOK_W && q > pos) break;
            ww += g, q = r;
          }
          e = q, w = ww;
        }
        x += (x ? sp : 0) + w;
        b = pos = e;
        if (byte_at(pos) == ' ') pos++;
        int c2 = byte_at(pos);
        if (c2 == '\n' || pos >= bk.len) break;
      }
      if (keep && n < (int)(sizeof lines / sizeof *lines))
        lines[n++] = (line_t){a, b, (int16_t)(y + f->ascent + (lh - f->ascent - f->descent) / 2), (uint8_t)head};
      y += lh;
      first = false, said = true;
    }
    if (pos < bk.len) pos++; /* (the new line) */
    first = false;
  }
full:
  if (keep) nlines = n;
  return pos;
}

static void paginate(void) {
  uint32_t pos = 0;
  bk.pages = 0;
  while (pos < bk.len && bk.pages < 4096) {
    ram.book.page_at[bk.pages++] = pos;
    uint32_t next = layout(pos, false);
    if (next <= pos) next = pos + 1;
    pos = next;
  }
  ram.book.page_at[bk.pages] = bk.len;
  if (!bk.pages) bk.pages = 1, ram.book.page_at[0] = 0;
}

static void book_scene(void *ctx) {
  for (int i = 0; i < (gfx_y1 - gfx_y0) * SCREEN_W; i++) strip[i] = PAPER;
  for (int i = 0; i < nlines; i++) {
    const line_t *l = &lines[i];
    const np_font_t *f = l->head ? head_fonts[bk.size] : read_fonts[bk.size];
    if (l->y + f->descent <= gfx_y0 || l->y - f->ascent >= gfx_y1) continue;
    int x = BOOK_X;
    for (uint32_t p = l->a; p < l->b;) {
      const np_glyph_t *g = glyph(f, letter(&p));
      if (g->w) gfx_mask(f->data + g->offset, g->w, g->h, x + g->x, l->y + g->y, INK, 256);
      x += g->advance;
    }
  }
  char s[24], *p = num(s, bk.page + 1);
  *p++ = ' ', *p++ = '/', *p++ = ' ';
  num(p, bk.pages);
  gfx_text_center(&np_font_small, SCREEN_W / 2, SCREEN_H - 9, s, FOLIO, 256);
}

static void book_show(void) {
  layout(ram.book.page_at[bk.page], true);
  gfx_render(book_scene, NULL, 0, SCREEN_H);
}

static void book(void) {
  bk.b = (const nv_book_t *)(np_media + hdr->index);
  bk.start = bk.b->start, bk.at = bk.b->start + bk.b->chunks + 1, bk.len = bk.b->bytes;
  bk.slot[0] = bk.slot[1] = -1, bk.size = 1;
  chunk_for(0);
  paginate();
  book_show();
  for (;;) {
    uint32_t now = (uint32_t)eadk_timing_millis();
    keys(now);
    if (quit_key() || pressed(eadk_key_back)) return;
    int page = bk.page, size = bk.size;
    if (held(eadk_key_right) || held(eadk_key_down) || pressed(eadk_key_ok) || pressed(eadk_key_exe))
      bk.page = MIN(bk.page + 1, bk.pages - 1);
    if (held(eadk_key_left) || held(eadk_key_up)) bk.page = MAX(bk.page - 1, 0);
    if (pressed(eadk_key_plus)) bk.size = MIN(bk.size + 1, 2);
    if (pressed(eadk_key_minus)) bk.size = MAX(bk.size - 1, 0);
    if (size != bk.size) { /* the same place, in the new size's pages */
      uint32_t at = ram.book.page_at[bk.page];
      paginate();
      bk.page = 0;
      while (bk.page + 1 < bk.pages && ram.book.page_at[bk.page + 1] <= at) bk.page++;
    }
    if (page != bk.page || size != bk.size) book_show();
    eadk_timing_msleep(10);
  }
}

/* ================================================================== */
static void nothing(void) {
  gfx_y0 = 0;
  for (int y = 0; y < SCREEN_H; y += STRIP_H) {
    gfx_y0 = y, gfx_y1 = y + STRIP_H;
    for (int i = 0; i < SCREEN_W * STRIP_H; i++) strip[i] = BLACK;
    gfx_text_center(&np_font_body, SCREEN_W / 2, 118, "No file in this app", WHITE, 256);
    gfx_text_center(&np_font_small, SCREEN_W / 2, 138, "Make one in NumPlay Studio", gfx_mix(BLACK, WHITE, 20), 256);
    np_push(0, y, SCREEN_W, STRIP_H, strip);
  }
  for (;;) {
    keys((uint32_t)eadk_timing_millis());
    if (quit_key() || pressed(eadk_key_back)) return;
    eadk_timing_msleep(20);
  }
}

int main(void) {
  np_app_begin();
  gfx_buf = strip;
  hdr = (const nv_header_t *)np_media;
  down = eadk_keyboard_scan(); /* (keys still down from the home screen wait to be let go) */
  bool ok = np_media && (uintptr_t)np_numview_end - (uintptr_t)np_media >= sizeof *hdr && hdr->magic == NV_MAGIC &&
            hdr->version == NV_VERSION;
  if (ok && hdr->jpeg_len) jpeg_setup(&jpg, np_media + hdr->jpeg, hdr->jpeg_len);
  if (!ok) nothing();
  else if (hdr->kind == NV_PICTURES && (NV_KINDS & 1)) pictures();
  else if (hdr->kind == NV_VIDEO && (NV_KINDS & 2)) video();
  else if (hdr->kind == NV_BOOK && (NV_KINDS & 4)) book();
  else nothing();
  return np_app_end();
}
