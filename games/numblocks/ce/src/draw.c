/* Drawing on the 320 x 240 screen (or the HUD's layer): Minecraft's font with its shadow, its GUI pictures (kept
 * as runs of a colour, sprite tables in gfx.c), 16 x 16 icons, and the 3D view doubled onto the screen with the
 * HUD over it. */
#include "nb.h"
#include "draw.h"

Canvas cv;
const uint8_t *gfx, *texdat;

void canvas_screen(void) { cv = (Canvas){scr, SCREEN_W, 0, 0, SCREEN_W, SCREEN_H}; }

/* (a row at a time: the row's pixel 0 in screen x, 0 off the canvas; x kept in [x0, x1)) */
static int x0, x1;
static uint8_t *row_at(int y) {
  y -= cv.oy;
  if ((unsigned)y >= (unsigned)cv.h) return 0;
  x0 = cv.ox, x1 = cv.ox + cv.w;
  return cv.p + y * cv.stride - cv.ox;
}

void fill(int x, int y, int w, int h, uint8_t c) {
  x -= cv.ox, y -= cv.oy;
  if (x < 0) w += x, x = 0;
  if (y < 0) h += y, y = 0;
  if (x + w > cv.w) w = cv.w - x;
  if (y + h > cv.h) h = cv.h - y;
  if (w <= 0 || h <= 0) return;
  for (int r = 0; r < h; r++) memset(cv.p + (y + r) * cv.stride + x, c, (size_t)w);
}

/* a darker colour: Minecraft's text shadow is a quarter as bright */
uint8_t dark(uint8_t c, int level) { return shade_cm[level][c]; }

int text_width(const char *s) {
  int w = 0;
  for (; *s; s++)
    if ((unsigned char)*s >= 32 && (unsigned char)*s < 128) w += font_w[*s - 32];
  return w ? w - 1 : 0;
}

static void glyphs(const char *s, int x, int y, uint8_t c, int scale) {
  if (scale > 1) {
    for (; *s; s++) {
      int k = *s - 32;
      if (k < 0 || k >= 96) continue;
      const uint8_t *g = font_bits + k * 8;
      for (int r = 0; r < 8; r++)
        for (int b = 0; b < 8; b++)
          if (g[r] >> b & 1) fill(x + b * scale, y + r * scale, scale, scale, c);
      x += font_w[k] * scale;
    }
    return;
  }
  for (int r = 0; r < 8; r++) {
    uint8_t *d = row_at(y + r);
    if (!d) continue;
    int gx = x;
    for (const char *p = s; *p; p++) {
      int k = *p - 32;
      if (k < 0 || k >= 96) continue;
      uint8_t bits = font_bits[k * 8 + r];
      for (int b = gx; bits; b++, bits >>= 1)
        if (bits & 1 && b >= x0 && b < x1) d[b] = c;
      gx += font_w[k];
    }
  }
}

void text(int x, int y, const char *s, uint8_t c) {
  glyphs(s, x + 1, y + 1, dark(c, 19), 1);
  glyphs(s, x, y, c, 1);
}
void text_plain(int x, int y, const char *s, uint8_t c) { glyphs(s, x, y, c, 1); }
void text_center(int cx, int y, const char *s, uint8_t c) { text(cx - text_width(s) / 2, y, s, c); }
void text_big(int x, int y, const char *s, uint8_t c, int scale) { glyphs(s, x, y, c, scale); }

/* a picture kept as runs (tools/pack.py rle): x, y its top left; sx, sy, w, h a part of it */
static void runs(const uint8_t *base, int sw, int sh, int x, int y, int sx, int sy, int w, int h) {
  for (int r = sy; r < sy + h && r < sh; r++) {
    uint8_t *d = row_at(y + r - sy);
    if (!d) continue;
    const uint8_t *p = base + (base[r * 2] | base[r * 2 + 1] << 8) + sh * 2;
    /* (screen x = col + dx, within [a, b): the part asked for, on the canvas) */
    int dx = x - sx, a = x > x0 ? x : x0, b = x + w < x1 ? x + w : x1;
    int col = 0;
    while (col < sw) {
      int n = *p++;
      if (n < 128) {
        for (int k = 0; k <= n; k++, col++) {
          uint8_t c = *p++;
          int px = col + dx;
          if (c && px >= a && px < b) d[px] = c;
        }
      } else {
        int len = n - 126;
        uint8_t c = *p++;
        if (c) {
          int s0 = col + dx, s1 = s0 + len;
          if (s0 < a) s0 = a;
          if (s1 > b) s1 = b;
          if (s1 > s0) memset(d + s0, c, (size_t)(s1 - s0));
        }
        col += len;
      }
    }
  }
}
/* a GUI picture (the appvar's sprites): sx, sy, w, h a part of it (w 0: all) */
void sprite_part(int id, int x, int y, int sx, int sy, int w, int h) {
  const uint8_t *t = gfx + GFX_SPR;
  int sw = gfx[GFX_SPR_W + id], sh = gfx[GFX_SPR_H + id];
  if (!w) w = sw, h = sh;
  runs(t + (t[id * 2] | t[id * 2 + 1] << 8), sw, sh, x, y, sx, sy, w, h);
}
void sprite(int id, int x, int y) { sprite_part(id, x, y, 0, 0, 0, 0); }

void icon(int id, int x, int y) {
  if (id <= 0 || id >= IT0 + IT_N || (id >= B_N && id < IT0)) return;
  int k = id >= IT0 ? B_N + id - IT0 : id;
  if (icon_of[k] == 255) return;
  const uint8_t *t = gfx + GFX_ICON;
  runs(t + (t[icon_of[k] * 2] | t[icon_of[k] * 2 + 1] << 8), 16, 16, x, y, 0, 0, 16, 16);
}

/* a stack in a slot: its icon, its count (bottom right, white), a tool's wear bar */
void stack_draw(const Stack *s, int x, int y) {
  if (!s->id) return;
  icon(s->id, x, y);
  if (s->n > 1) {
    char t[4];
    int n = s->n, i = 0;
    if (n >= 10) t[i++] = (char)('0' + n / 10);
    t[i++] = (char)('0' + n % 10);
    t[i] = 0;
    text(x + 17 - text_width(t), y + 9, t, C_WHITE);
  }
  int dur = item_dur(s->id);
  if (dur && s->dmg) {
    /* RenderItem: 13 pixels, green to red */
    int left = dur - s->dmg, w = (13 * left + dur / 2) / dur;
    fill(x + 2, y + 13, 13, 2, C_BLACK);
    uint8_t c = left * 3 > dur * 2 ? C_GREEN : left * 3 > dur ? C_YELLOW : C_RED;
    fill(x + 2, y + 13, w, 1, c);
  }
}

/* Minecraft's button (widgets.png): its left part and its right end, the label centred */
void button(int x, int y, int w, int state, const char *label) {
  int id = state == 0 ? SP_BUTTON_OFF : state == 2 ? SP_BUTTON_HOVER : SP_BUTTON;
  sprite_part(id, x, y, 0, 0, w / 2, 20);
  sprite_part(id, x + w / 2, y, 200 - (w - w / 2), 0, w - w / 2, 20);
  uint8_t c = state == 0 ? C_GRAY : state == 2 ? C_HOVER : C_LIGHT;
  text_center(x + w / 2, y + 6, label, c);
}

/* the options background: dirt, 32 pixels a tile, darkened (the texture at 2x, a quarter as bright) */
void dirt_background(int y0, int y1) {
  static uint8_t line[SCREEN_W];
  int tr = -1;
  for (int y = y0; y < y1; y++) {
    uint8_t *d = row_at(y);
    if (!d) continue;
    int r = (y >> 1) & 15;
    if (r != tr) {
      const uint8_t *t = tex_px[TX_DIRT] + r * 16;
      for (int k = 0; k < 16; k++) line[2 * k] = line[2 * k + 1] = dark(t[k], 19);
      for (int k = 32; k < SCREEN_W; k += 32) memcpy(line + k, line, 32);
      tr = r;
    }
    int a = x0 > 0 ? x0 : 0, b = x1 < SCREEN_W ? x1 : SCREEN_W;
    if (b > a) memcpy(d + a, line + a, (size_t)(b - a));
  }
}

/* ---------------------------------------------------------------- the 3D view on the screen */
uint8_t *hud_layer;

void compose(bool hud, bool crosshair) {
  render_show(hud);
  if (crosshair) {
    /* the crosshair (icons.png's, light) */
    uint8_t *m = scr + (SCREEN_H / 2) * SCREEN_W + SCREEN_W / 2;
    for (int k = -4; k <= 4; k++) {
      m[k] = C_LIGHT;
      m[k * SCREEN_W] = C_LIGHT;
    }
  }
}
