/* Drawing on the 320 x 240 screen (or the HUD's layer): Minecraft's font with its shadow, its GUI pictures (kept
 * as runs of a colour, sprite tables in gfx.c), 16 x 16 icons, and the 3D view doubled onto the screen with the
 * HUD over it. */
#include "nb.h"
#include "draw.h"

Canvas cv;

void canvas_screen(void) { cv = (Canvas){scr, SCREEN_W, 0, 0, SCREEN_W, SCREEN_H}; }

static inline void put(int x, int y, uint8_t c) {
  x -= cv.ox, y -= cv.oy;
  if ((unsigned)x < (unsigned)cv.w && (unsigned)y < (unsigned)cv.h) cv.p[y * cv.stride + x] = c;
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
  for (; *s; s++) {
    int k = *s - 32;
    if (k < 0 || k >= 96) continue;
    const uint8_t *g = font_bits + k * 8;
    for (int r = 0; r < 8; r++)
      for (int b = 0; b < 8; b++)
        if (g[r] >> b & 1) {
          if (scale == 1) put(x + b, y + r, c);
          else fill(x + b * scale, y + r * scale, scale, scale, c);
        }
    x += font_w[k] * scale;
  }
}

void text(int x, int y, const char *s, uint8_t c) {
  glyphs(s, x + 1, y + 1, dark(c, 19), 1);
  glyphs(s, x, y, c, 1);
}
void text_plain(int x, int y, const char *s, uint8_t c) { glyphs(s, x, y, c, 1); }
void text_center(int cx, int y, const char *s, uint8_t c) { text(cx - text_width(s) / 2, y, s, c); }
void text_big(int x, int y, const char *s, uint8_t c, int scale) { glyphs(s, x, y, c, scale); }

/* a GUI picture (gfx.c): its rows are runs; x, y its top left; sx, sy, w, h a part of it (w 0: all) */
void sprite_part(int id, int x, int y, int sx, int sy, int w, int h) {
  const uint8_t *base = spr_data + spr_off[id];
  int sw = spr_w[id], sh = spr_h[id];
  if (!w) w = sw, h = sh;
  for (int r = sy; r < sy + h && r < sh; r++) {
    const uint8_t *p = base + (base[r * 2] | base[r * 2 + 1] << 8) + sh * 2;
    int col = 0, dy = y + r - sy;
    while (col < sw) {
      int n = *p++;
      if (n < 128) {
        for (int k = 0; k <= n; k++, col++) {
          uint8_t c = *p++;
          if (c && col >= sx && col < sx + w) put(x + col - sx, dy, c);
        }
      } else {
        int len = n - 126;
        uint8_t c = *p++;
        if (c) {
          int a = col < sx ? sx : col, b = col + len > sx + w ? sx + w : col + len;
          for (int k = a; k < b; k++) put(x + k - sx, dy, c);
        }
        col += len;
      }
    }
  }
}
void sprite(int id, int x, int y) { sprite_part(id, x, y, 0, 0, 0, 0); }

void icon(int id, int x, int y) {
  if (id <= 0 || id >= B_N + IT_N + IT0 - B_N) return;
  int k = id >= IT0 ? B_N + id - IT0 : id;
  if (k >= B_N + IT_N || icon_of[k] == 255) return;
  const uint8_t *s = icon_px + icon_of[k] * 256;
  for (int r = 0; r < 16; r++)
    for (int c = 0; c < 16; c++)
      if (s[r * 16 + c]) put(x + c, y + r, s[r * 16 + c]);
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

/* the options background: dirt, 32 pixels a tile, darkened */
void dirt_background(int y0, int y1) {
  for (int y = y0; y < y1; y++)
    for (int x = 0; x < SCREEN_W; x += 32) {
      for (int k = 0; k < 32 && x + k < SCREEN_W; k += 2) {
        /* (the dirt texture at 2x, darkened to a quarter) */
        const uint8_t *row = tex_px[TX_DIRT] + ((y >> 1) & 15) * 16;
        uint8_t c = dark(row[(k >> 1) & 15], 19);
        put(x + k, y, c);
        put(x + k + 1, y, c);
      }
    }
}

/* ---------------------------------------------------------------- the 3D view on the screen */
uint8_t hud_layer[HUD_W * HUD_H];

void compose(const uint8_t *src, bool hud, bool crosshair) {
  plat_expand(src, scr, HUD_Y / 2);
  /* the rows under the HUD: doubled, then the HUD's own pixels over them */
  for (int y = HUD_Y; y < SCREEN_H; y += 2) {
    uint8_t *d = scr + y * SCREEN_W;
    const uint8_t *s = src + (y / 2) * RW;
    for (int x = 0; x < RW; x++) d[2 * x] = d[2 * x + 1] = s[x];
    if (hud) {
      const uint8_t *h = hud_layer + (y - HUD_Y) * HUD_W;
      for (int x = 0; x < HUD_W; x++)
        if (h[x]) d[HUD_X + x] = h[x];
    }
    uint8_t *d2 = d + SCREEN_W;
    for (int x = 0; x < RW; x++) d2[2 * x] = d2[2 * x + 1] = s[x];
    if (hud) {
      const uint8_t *h = hud_layer + (y + 1 - HUD_Y) * HUD_W;
      for (int x = 0; x < HUD_W; x++)
        if (h[x]) d2[HUD_X + x] = h[x];
    }
  }
  if (crosshair) {
    /* the crosshair (icons.png's, light) */
    uint8_t *m = scr + (SCREEN_H / 2) * SCREEN_W + SCREEN_W / 2;
    for (int k = -4; k <= 4; k++) {
      m[k] = C_LIGHT;
      m[k * SCREEN_W] = C_LIGHT;
    }
  }
}
