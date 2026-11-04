/* The screens: the web game's scenes and HTML interface, drawn 8 rows at a time (a 5 KB strip) with its own
 * pictures (art.c) and its JUNEGULL font. A strip is pushed only when it changed. */
#include <math.h>
#include <string.h>
#include "../../common/np_lz.h"
#include "art.h"
#include "draw.h"

#define W 320
#define H 240
#define BAND 8
#define C(rgb) ((uint16_t)((((rgb) >> 8) & 0xF800) | (((rgb) >> 5) & 0x07E0) | (((rgb) >> 3) & 0x1F)))
#define WHITE 0xFFFF
#define INK C(0x00065F)         /* the interface's shadows: rgba(0, 6, 95, 0.5) */
#define BLUE C(0x0171A3)        /* buttons, the shop's text */
#define CYAN C(0x00B9DC)        /* button borders, the info bar, the sunburst's rays */
#define ORANGE C(0xFF6633)      /* the equipped skin's frame */
#define GREY C(0x666666)        /* the progress bar */
#define POPUP C(0x438DBE)
#define FOCUS C(0xFFD21F)       /* the keypad's focus: a thick gold frame */
#define TRAIL C(0x00FFFF)
#define KEYS_Y 224              /* the key line's band, drawn last */

static uint16_t band[W * BAND];
static int top;                 /* the strip's first row */
#if !NP_SOFT_FLOAT
static uint32_t sums[H / BAND];
#endif
static bool fresh;
static const sh_view *V;
static const sh_game *G;

void sh_redraw(void) { fresh = false; }

/* ---------------------------------------------------------------- pixels */

static uint16_t mix(uint16_t a, uint16_t b, unsigned k) {   /* b over a, k of 256 */
  unsigned r = ((a >> 11) * (256 - k) + (b >> 11) * k) >> 8;
  unsigned g = (((a >> 5) & 63) * (256 - k) + ((b >> 5) & 63) * k) >> 8;
  unsigned bl = ((a & 31) * (256 - k) + (b & 31) * k) >> 8;
  return (uint16_t)(r << 11 | g << 5 | bl);
}
static uint16_t half(uint16_t c) { return (c >> 1) & 0x7BEF; }   /* black at 50%: the shadows */

static void span(int y, float x0, float x1, uint16_t c, unsigned k) {   /* row y from x0 to x1, edges blended */
  if (y < top || y >= top + BAND || x1 <= 0 || x0 >= W || x1 <= x0) return;
  uint16_t *row = band + (y - top) * W;
  int a = (int)floorf(x0), b = (int)floorf(x1);
  if (a == b) {
    if (a >= 0 && a < W) row[a] = mix(row[a], c, (unsigned)((x1 - x0) * k));
    return;
  }
  if (a >= 0) row[a] = mix(row[a], c, (unsigned)((a + 1 - x0) * k));
  int l = a + 1 < 0 ? 0 : a + 1, r = b > W ? W : b;
  if (k >= 256) for (int x = l; x < r; x++) row[x] = c;
  else for (int x = l; x < r; x++) row[x] = mix(row[x], c, k);
  if (b < W && x1 > b) row[b] = mix(row[b], c, (unsigned)((x1 - b) * k));
}

static void rect(int x, int y, int w, int h, uint16_t c, unsigned k) {
  int a = y < top ? top : y, b = y + h > top + BAND ? top + BAND : y + h;
  for (int j = a; j < b; j++) span(j, (float)x, (float)(x + w), c, k);
}

/* a rounded rectangle, filled (thick = 0) or as a frame `thick` wide */
static float inset(float cy, int y, int h, int r) {
  float d = cy < y + r ? y + r - cy : cy > y + h - r ? cy - (y + h - r) : 0;
  return d > 0 && r > 0 ? r - sqrtf(fmaxf(0, (float)(r * r) - d * d)) : 0;
}
static void round_rect(int x, int y, int w, int h, int r, int thick, uint16_t c, unsigned k) {
  int a = y < top ? top : y, b = y + h > top + BAND ? top + BAND : y + h;
  for (int j = a; j < b; j++) {
    float cy = j + 0.5f, in = inset(cy, y, h, r);
    if (!thick || j < y + thick || j >= y + h - thick) {
      span(j, x + in, x + w - in, c, k);
    } else {
      float in2 = inset(cy, y + thick, h - 2 * thick, r - thick);
      span(j, x + in, x + thick + in2, c, k), span(j, x + w - thick - in2, x + w - in, c, k);
    }
  }
}

/* the keypad's focus around a box: gold, with a dark edge outside so it shows on white and on blue */
static void focus(int x, int y, int w, int h, int r) {
  round_rect(x - 5, y - 5, w + 10, h + 10, r + 5, 1, INK, 200);
  round_rect(x - 4, y - 4, w + 8, h + 8, r + 4, 3, FOCUS, 256);
}

/* a convex quadrangle (the trail, the confetti), its rows filled */
static void quad(const float *px, const float *py, uint16_t c, unsigned k) {
  float lo = py[0], hi = py[0];
  for (int i = 1; i < 4; i++) lo = fminf(lo, py[i]), hi = fmaxf(hi, py[i]);
  int a = (int)floorf(lo), b = (int)ceilf(hi);
  if (a < top) a = top;
  if (b > top + BAND) b = top + BAND;
  for (int j = a; j < b; j++) {
    float y = j + 0.5f, l = 1e9f, r = -1e9f;
    for (int i = 0; i < 4; i++) {
      float x0 = px[i], y0 = py[i], x1 = px[(i + 1) & 3], y1 = py[(i + 1) & 3];
      if ((y0 <= y && y1 > y) || (y1 <= y && y0 > y)) {
        float x = x0 + (y - y0) * (x1 - x0) / (y1 - y0);
        l = fminf(l, x), r = fmaxf(r, x);
      }
    }
    if (r > l) span(j, l, r, c, k);
  }
}

static void segment(float x0, float y0, float x1, float y1, float width, uint16_t c, unsigned k) {
  float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
  if (len < 0.01f) return;
  if (width < 1) k = (unsigned)(k * width), width = 1;   /* thinner than a pixel: fainter */
  float nx = -dy / len * width / 2, ny = dx / len * width / 2;
  float px[4] = {x0 + nx, x1 + nx, x1 - nx, x0 - nx}, py[4] = {y0 + ny, y1 + ny, y1 - ny, y0 - ny};
  quad(px, py, c, k);
}

/* ---------------------------------------------------------------- text: JUNEGULL, with the interface's shadow */

static int text_width(const sh_font *f, const char *s) {
  int w = 0;
  for (; *s; s++) {
    unsigned c = (unsigned char)*s;
    if (c >= 'a' && c <= 'z') c -= 32;
    if (c >= 32 && c < 96) w += f->advance[c - 32] + 1;
  }
  return w ? w - 1 : 0;
}

#ifdef HOST
int sh_clipped;   /* text that didn't fit (the tests look) */
#endif
static void glyphs(const sh_font *f, int x, int y, const char *s, uint16_t c, unsigned k) {
#ifdef HOST
  if (x < 0 || x + text_width(f, s) > W || y < 0 || y + f->height > H) sh_clipped++;
#endif
  if (y >= top + BAND || y + f->height <= top) return;
  for (; *s; s++) {
    unsigned ch = (unsigned char)*s;
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (ch < 32 || ch >= 96) continue;
    ch -= 32;
    int w = f->width[ch], stride = (w + 3) / 4;
    const uint8_t *g = f->glyphs + f->offset[ch];
    for (int j = 0; j < f->height; j++) {
      int yy = y + j;
      if (yy < top || yy >= top + BAND) continue;
      uint16_t *row = band + (yy - top) * W;
      for (int i = 0; i < w; i++) {
        unsigned a = g[j * stride + i / 4] >> (2 * (i % 4)) & 3, xx = (unsigned)(x + i);
        if (a && xx < W) row[xx] = a == 3 && k >= 256 ? c : mix(row[xx], c, a * k / 3);
      }
    }
    x += f->advance[ch] + 1;
  }
}

static void text(const sh_font *f, int x, int y, const char *s, uint16_t c) {
  glyphs(f, x, y + (f == &sh_font_big ? 3 : 2), s, INK, 128);
  glyphs(f, x, y, s, c, 256);
}
static void center(const sh_font *f, int x, int y, const char *s, uint16_t c) { text(f, x - text_width(f, s) / 2, y, s, c); }
static void right(const sh_font *f, int x, int y, const char *s, uint16_t c) { text(f, x - text_width(f, s), y, s, c); }

static char *number(char *p, uint32_t v) {
  char d[10];
  int n = 0;
  do d[n++] = (char)('0' + v % 10), v /= 10; while (v);
  while (n) *p++ = d[--n];
  *p = 0;
  return p;
}
static char *append(char *p, const char *s) {
  while (*s) *p++ = *s++;
  *p = 0;
  return p;
}
/* "N / M": the level and the game's goal, the next multiple of 5 */
static void of_goal(char *out, const char *before, uint32_t level, uint32_t done) {
  char *p = append(out, before);
  p = number(p, level);
  p = append(p, " / ");
  number(p, sh_fake_max(done));
}

static void keys(const char *s) {
  rect(0, KEYS_Y, W, H - KEYS_Y, INK, 150);
  center(&sh_font_small, W / 2, KEYS_Y + 3, s, WHITE);
}

/* in a level, the key line has no band (the stickman swings down there): outlined, it reads on anything */
static void level_keys(const char *s) {
  int x = W / 2 - text_width(&sh_font_small, s) / 2, y = KEYS_Y + 3;
  for (int d = 0; d < 8; d++) {
    static const int8_t dx[8] = {-1, 0, 1, -1, 1, -1, 0, 1}, dy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
    glyphs(&sh_font_small, x + dx[d], y + dy[d], s, INK, 256);
  }
  glyphs(&sh_font_small, x, y, s, WHITE, 256);
}

/* ---------------------------------------------------------------- pictures: turned, scaled, mirrored */

typedef struct {
  int16_t l, r, t, b;
  int32_t uc, us, vc, vs, u0, v0;   /* texture position (Q10) at (l, t), and its steps across and down */
  const uint8_t *px;                /* its pixels: in flash, or unpacked */
  uint16_t sprite;
  uint8_t mode;                     /* 0 its colours, 1 a shadow, 2 a glow, 3 darker (locked) */
  uint8_t layer;                    /* 0 under the screen's text and buttons, 1 over them */
} cmd;
static cmd cmds[256];   /* (a course has 240 pictures at most, a screen far fewer) */
static int ncmd, trail_at, layer;

/* the menus' big pictures are packed: unpacked here for the frame (the shop's 23 skins and its dancer at most) */
static uint8_t unpacked[16 * 1024];
static uint32_t unpacked_used;
static const uint8_t *pixels(const sh_sprite *s) {
  if (!(s->offset >> 31)) return sh_pixels + s->offset;
  const uint8_t *z = sh_pixels + (s->offset & 0x7FFFFFFF);
  uint32_t size = ((uint32_t)s->w * s->h + 1) / 2;
  if (unpacked_used + size > sizeof unpacked) return 0;
  uint8_t *out = unpacked + unpacked_used;
  if (np_lz_unpack(z + 2, (uint32_t)(z[0] | z[1] << 8), out, size) != (int32_t)size) return 0;
  unpacked_used += size;
  return out;
}

static void put(int sprite, float x, float y, float angle, float scale, bool flip, int mode) {
  const sh_sprite *s = &sh_sprites[sprite];
  float sn = sinf(angle), cs = cosf(angle), l = 1e9f, r = -1e9f, t = 1e9f, b = -1e9f;
  for (int i = 0; i < 4; i++) {
    float px = (float)(s->x + (i & 1 ? s->w : 0)), py = (float)(s->y + (i & 2 ? s->h : 0));
    if (flip) px = -px;
    float xx = (px * cs - py * sn) * scale + x, yy = (px * sn + py * cs) * scale + y;
    l = fminf(l, xx), r = fmaxf(r, xx), t = fminf(t, yy), b = fmaxf(b, yy);
  }
  if (r < 0 || l >= W || b < 0 || t >= H || ncmd == (int)(sizeof cmds / sizeof cmds[0])) return;
  const uint8_t *px = pixels(s);
  if (!px) return;
  cmd *c = &cmds[ncmd++];
  c->px = px;
  c->l = (int16_t)fmaxf(-1, floorf(l)), c->r = (int16_t)fminf(W, ceilf(r)), c->t = (int16_t)fmaxf(-1, floorf(t));
  c->b = (int16_t)fminf(H, ceilf(b));
  float k = 1024 / scale, f = flip ? -1.0f : 1.0f;
  c->uc = (int32_t)lroundf(cs * k * f), c->us = (int32_t)lroundf(sn * k * f);
  c->vc = (int32_t)lroundf(-sn * k), c->vs = (int32_t)lroundf(cs * k);
  float dx = c->l + 0.5f - x, dy = c->t + 0.5f - y;
  c->u0 = (int32_t)lroundf((dx * cs + dy * sn) * k * f), c->v0 = (int32_t)lroundf((-dx * sn + dy * cs) * k);
  c->sprite = (uint16_t)sprite, c->mode = (uint8_t)mode, c->layer = (uint8_t)layer;
}

static void paint(const cmd *c, int want) {
  if (c->layer != want || c->t >= top + BAND || c->b < top) return;
  const sh_sprite *s = &sh_sprites[c->sprite];
  const uint8_t *px = c->px;
  const uint16_t *pal = sh_palettes + s->palette;
  int l = c->l < 0 ? 0 : c->l, r = c->r > W - 1 ? W - 1 : c->r;
  int a = c->t < top ? top : c->t, b = c->b > top + BAND - 1 ? top + BAND - 1 : c->b;
  for (int y = a; y <= b; y++) {
    int dy = y - c->t, dx = l - c->l;
    int32_t u = c->u0 + dx * c->uc + dy * c->us, v = c->v0 + dx * c->vc + dy * c->vs;
    uint16_t *row = band + (y - top) * W;
    for (int x = l; x <= r; x++, u += c->uc, v += c->vc) {
      int tx = (u >> 10) - s->x, ty = (v >> 10) - s->y;
      if ((unsigned)tx >= s->w || (unsigned)ty >= s->h) continue;
      unsigned n = (unsigned)(ty * s->w + tx), p = px[n >> 1];
      p = n & 1 ? p & 15 : p >> 4;
      if (!p) continue;
      if (c->mode == 1) row[x] = half(row[x]);
      else if (c->mode == 2) row[x] = mix(row[x], pal[1], p * 17);
      else if (c->mode == 3) row[x] = mix(pal[p], INK, 140);
      else row[x] = pal[p];
    }
  }
}

/* ---------------------------------------------------------------- the level */

static float sx(float x) { return 240 * (x * G->zoom + G->cam_x); }
static float sy(float y) { return 240 * (y * G->zoom + G->cam_y); }

/* the pictures, in the scene's order: bumpers, hooks, walls, then the stickman and the spawn burst */
static void level_pictures(void) {
  float z = G->zoom;
  for (int j = 0, n = 0; j < G->sections; j++) {
    const sh_part *p = &sh_parts[G->course.part[j]];
    for (int i = 0; i < p->bumpers; i++, n++) {
      const sh_bumper *b = &sh_bumpers[p->bumper + i];
      int a = G->bumper_anim[n], frame = a ? (a - 1) / 2 : 4;
      float x = sx(b->x + G->offset[j]), y = sy(b->y), ang = sh_bumper_kinds[b->kind].angle;
      put(SPR_BUMPER0 + frame, x, y + 2.4f * z, ang, z, false, 1);
      put(SPR_BUMPER0 + frame, x, y, ang, z, false, 0);
    }
  }
  for (int i = 0, n = sh_hook_count(G); i < n; i++) {
    sh_vec h = sh_hook_at(G, i);
    put(SPR_HOOK, sx(h.x), sy(h.y), 0, z, false, 0);
    if (i == G->near) put(SPR_HOOK_NEAR, sx(h.x), sy(h.y), 0, z * G->near_scale, false, 0);
    if (G->hooked && h.x == G->hook_x && h.y == G->hook_y) put(SPR_HOOK_ACTIVE, sx(h.x), sy(h.y), 0, z, false, 0);
  }
  for (int j = 0; j < G->sections; j++) {
    const sh_part *p = &sh_parts[G->course.part[j]];
    for (int i = 0; i < p->walls; i++) {
      const sh_wall *o = &sh_walls[p->wall + i];
      put(SPR_WALL_A_WALL45 + o->shape, sx(o->x + G->offset[j]), sy(o->y), sh_turn_angles[o->turn], z, false, 0);
    }
  }
}

static void stickman_pictures(void) {
  float z = G->zoom, x = sx(G->x), y = sy(G->y);
  int first = sh_skin_first[V->skin];
  if (G->completed) {   /* zoomed in on the victory pose: the bigger picture, made for 3x */
    int big = first + 9 + sh_skin_dances[V->skin];
    put(big, x, y + 1.2f * z, G->rotation, z / 3, G->facing, 1);
    put(big, x, y, G->rotation, z / 3, G->facing, 0);
  } else {
    put(first + G->pose, x, y + 1.2f * z, G->rotation, z, G->facing, 1);
    put(first + G->pose, x, y, G->rotation, z, G->facing, 0);
  }
  if (G->spawn_fx) put(SPR_SPAWN0 + (G->spawn_fx - 1) / 2, sx(G->spawn_x), sy(G->spawn_y), 0, z, false, 0);
}

static void finish_line(void) {
  float x0 = sx(G->finish), q = 0.016f * 240 * G->zoom, y0 = sy(G->completed ? G->y - 1 : 0);
  if (x0 > W || x0 + 2 * q < 0) return;
  for (int j = top; j < top + BAND; j++) {
    float y = j + 0.5f - y0;
    if (y < 0) continue;
    int row = (int)(y / q);
    span(j, x0, x0 + q, row & 1 ? WHITE : 0, 256);
    span(j, x0 + q, x0 + 2 * q, row & 1 ? 0 : WHITE, 256);
  }
}

static void trail(void) {
  float z = G->zoom;
  for (int e = 1; e < G->trail_count; e++) {
    float k = (float)e / G->trail_max;
    segment(sx(G->trail[e - 1].x), sy(G->trail[e - 1].y), sx(G->trail[e].x), sy(G->trail[e].y),
            20 * 0.0005f * 240 * z * k, TRAIL, (unsigned)(256 * fminf(k, 1)));
  }
}

static void confetti(void) {
  if (!G->completed) return;
  for (int i = 0; i < SH_CONFETTI; i++) {
    const sh_confetto *c = &G->confetti[i];
    float x = sx(c->x), y = sy(c->y), w = c->w * 240 * G->zoom / 2, h = c->h * 240 * G->zoom / 2;
    if (x < -20 || x > W + 20 || y < top - 20 || y > top + BAND + 20) continue;
    float cs = cosf(c->angle), sn = sinf(c->angle);
    float px[4] = {x - w * cs + h * sn, x + w * cs + h * sn, x + w * cs - h * sn, x - w * cs - h * sn};
    float py[4] = {y - w * sn - h * cs, y + w * sn - h * cs, y + w * sn + h * cs, y - w * sn + h * cs};
    quad(px, py, c->color, 256);
  }
}

static void level_hud(void) {
  char s[24];
  number(s, G->level + 1);
  right(&sh_font_small, 110, 9, s, WHITE);
  number(s, G->level + 2);
  text(&sh_font_small, 228, 9, s, WHITE);
  rect(118, 14, 102, 9, INK, 128);
  rect(117, 11, 102, 9, GREY, 256);
  rect(117, 11, (int)lroundf(102 * G->progress), 9, WHITE, 256);
  if (G->completed) center(&sh_font_big, W / 2, 32, "LEVEL CLEARED!", WHITE);
}

/* ---------------------------------------------------------------- menus */

static void dim(void) { rect(0, top, W, BAND, INK, 128); }   /* the popups' veil */

/* a button as the game's "EQUIP NOW": blue with a cyan border; white when it has the focus */
static void button(int x, int y, int w, int h, const char *label, bool on) {
#ifdef HOST
  if (text_width(&sh_font_small, label) > w - 12) sh_clipped++;
#endif
  round_rect(x, y + 2, w, h, h / 2, 0, INK, 110);
  round_rect(x, y, w, h, h / 2, 0, on ? WHITE : CYAN, 256);
  round_rect(x + 3, y + 3, w - 6, h - 6, h / 2 - 3, 0, on ? WHITE : BLUE, 256);
  int ty = y + (h - sh_font_small.height) / 2 + 1;
  if (on) glyphs(&sh_font_small, x + w / 2 - text_width(&sh_font_small, label) / 2, ty, label, BLUE, 256);
  else center(&sh_font_small, x + w / 2, ty, label, WHITE);
  if (on) focus(x, y, w, h, h / 2);
}

/* the shop button of the title and the victory: a white tile, every skin's picture in turn, "15/23" */
#define SHOP 50
static void shop_button(int x, int y, bool on) {
  int open = 0;
  for (int i = 0; i < SH_SKINS; i++) open += sh_skin_open(V->done, i);
  round_rect(x, y + 3, SHOP, SHOP, 8, 0, INK, 110);
  round_rect(x, y, SHOP, SHOP, 8, 3, WHITE, 256);
  round_rect(x, y + 34, SHOP, 16, 7, 0, WHITE, 256);
  rect(x, y + 34, SHOP, 6, WHITE, 256);
  char s[8], *p = number(s, (uint32_t)open);
  *p++ = '/';
  number(p, SH_SKINS);
  glyphs(&sh_font_small, x + SHOP / 2 - text_width(&sh_font_small, s) / 2, y + 37, s, BLUE, 256);
  if (on) focus(x, y, SHOP, SHOP, 8);
}

static void sunburst(int cx, int cy, int y0, int y1, int x0, int x1, float turn) {
  int a = top > y0 ? top : y0, b = top + BAND < y1 ? top + BAND : y1;
  int base = (int)(turn * (256 / 6.2831853f)) & 255;
  for (int y = a; y < b; y++) {
    uint16_t *row = band + (y - top) * W;
    float dy = y + 0.5f - cy;
    for (int x = x0; x < x1; x++) {
      float dx = x + 0.5f - cx, ax = fabsf(dx), ay = fabsf(dy), m = fmaxf(ax, ay);
      float r = fminf(ax, ay) / m, t = r * (0.7853981f + 0.273f * (1 - r));   /* atan, to a few thousandths */
      if (ay > ax) t = 1.5707963f - t;
      if (dx < 0) t = 3.1415927f - t;
      if (dy < 0) t = -t;
      int i = ((int)floorf(t * (256 / 6.2831853f)) - base) & 255;
      if (sh_rays[i >> 3] >> (i & 7) & 1) row[x] = CYAN;
    }
  }
}

static void dancer(int skin, float x, float y) {   /* the dance, at 0.125 frame a tick, at 3x */
  int first = sh_skin_first[skin], n = sh_skin_dances[skin];
  put(first + 9 + (int)(V->clock * 60 / 1000 / 8 % (uint32_t)n), x, y, 0, 1, false, 0);
}

static const char *const skin_names[SH_SKINS] = {
    "CLASSIC", "DRAGON", "PLANT", "MUMMY", "ANGEL", "EVIL", "RAINBOW", "THUG", "KING", "SUPERSTICK", "BURGER", "CACTUS",
    "CORGI", "UNICORN", "BANANA", "NINJA", "SHEEP", "PANDA", "ROBOT", "TIKI", "HOT DOG", "LEMONMAN", "PLANTMONSTER"};

#define TILE 34
#define TILE_X(i) (13 + (i) % 8 * (TILE + 3))
#define TILE_Y(i) (114 + (i) / 8 * (TILE + 3))
#define PLAY_X 75
#define PLAY_Y 158
#define TITLE_HOOK_X 168.27f   /* the logo's red dot: (x + w / 13, y + h / 6) */
#define TITLE_HOOK_Y 60.92f
#define HELP_HOOK_Y 46

/* ---------------------------------------------------------------- a frame */

static bool in_level(void) { return V->screen == S_PLAY || V->screen == S_PAUSE || V->screen == S_QUIT; }
static bool at_victory(void) { return V->screen == S_VICTORY || V->screen == S_UNLOCKED; }

/* the pictures of the frame, in the order they're painted */
static void pictures(void) {
  ncmd = 0, layer = 0, trail_at = 0, unpacked_used = 0;
  if (in_level()) {
    level_pictures();
    trail_at = ncmd;
    stickman_pictures();
    if (!G->completed) put(SPR_BTN_PAUSE, 16, 15, 0, 1, false, 0);   /* the pause button, over the level */
  } else if (V->screen == S_TITLE) {
    const sh_swing *w = V->swing;
    put(SPR_TITLE, 160, 43 + 1.92f, 0, 1, false, 1);
    put(SPR_TITLE, 160, 43, 0, 1, false, 0);
    put(sh_skin_first[V->skin] + 6 + (w->pose - SH_HOOKED0), w->x * 240, w->y * 240, -w->theta, 1, w->facing, 0);
    const sh_sprite *b = &sh_sprites[SPR_BTN_PLAY];
    put(SPR_BTN_PLAY, PLAY_X + b->w / 2.0f, PLAY_Y + b->h / 2.0f, 0, 1, false, 0);
    put(sh_skin_mini[V->clock / 500 % SH_SKINS], 195 + SHOP / 2, PLAY_Y + 17, 0, 0.75f, false, 0);
  } else if (at_victory()) {
    put(SPR_FINISHLIGHT, 160, 72, 0, 1, false, 2);
    put(SPR_PLATFORM0 + (int)(V->clock * 60 / 1000 / 2 % 11), 160, 72, 0, 1, false, 0);   /* 0.5 frame a tick */
    dancer(V->skin, 160, 72);
    put(SPR_BTN_NEXT, 160, 172, 0, 1, false, 0);
    put(sh_skin_mini[V->clock / 500 % SH_SKINS], 258 + SHOP / 2, 160 + 17, 0, 0.75f, false, 0);
    if (V->screen == S_UNLOCKED) {
      layer = 1;
      dancer(V->new_skin, 160, 104);
    }
  } else if (V->screen == S_SKINS) {
    dancer(V->skin, 160, 48);
    for (int i = 0; i < SH_SKINS; i++) {
      bool open = sh_skin_open(V->done, i);
      put(sh_skin_mini[i], TILE_X(i) + TILE / 2.0f, TILE_Y(i) + TILE / 2.0f, 0, 0.85f, false, open ? 0 : 3);
      if (!open) put(SPR_LOCK, TILE_X(i) + 8.0f, TILE_Y(i) + 8.0f, 0, 1, false, 0);
    }
  } else if (V->screen == S_HELP) {   /* the title's stickman, on a hook */
    const sh_swing *w = V->swing;
    put(SPR_HOOK, 160, HELP_HOOK_Y, 0, 2, false, 0);

    put(SPR_HOOK_ACTIVE, 160, HELP_HOOK_Y, 0, 2, false, 0);
    put(sh_skin_first[V->skin] + 6 + (w->pose - SH_HOOKED0), 160 + w->x * 240 - TITLE_HOOK_X, HELP_HOOK_Y + w->y * 240 - TITLE_HOOK_Y,
        -w->theta, 1, w->facing, 0);
  }
}

static int background(void) { return in_level() || at_victory() ? G->course.background : 1; }

/* what's under the pictures */
static void under(void) {
  char s[32];
  if (in_level()) {
    finish_line();
    confetti();
    if (G->hooked) segment(sx(G->hook_x), sy(G->hook_y), sx(G->x), sy(G->y), fmaxf(1, 0.48f * G->zoom), WHITE, 256);
    for (int i = 0; i < trail_at; i++) paint(&cmds[i], 0);
    trail();
  } else if (V->screen == S_TITLE) {
    const sh_swing *w = V->swing;
    segment(TITLE_HOOK_X, TITLE_HOOK_Y, w->x * 240, w->y * 240, 1, WHITE, 256);
    shop_button(195, PLAY_Y, V->sel == 1);
  } else if (at_victory()) {
    shop_button(258, 160, V->screen == S_VICTORY && V->sel == 1);
  } else if (V->screen == S_HELP) {
    const sh_swing *w = V->swing;
    segment(160, HELP_HOOK_Y, 160 + w->x * 240 - TITLE_HOOK_X, HELP_HOOK_Y + w->y * 240 - TITLE_HOOK_Y, 1, WHITE, 256);
  } else if (V->screen == S_SKINS) {
    sunburst(160, 48, 0, 80, 0, W, (float)(V->clock % 6283) / 1000);   /* a turn a second... of a radian */
    of_goal(s, "LEVEL ", V->current + 1, V->current);
    right(&sh_font_small, W - 6, 5, s, WHITE);
    int k = V->sel;
    rect(0, 80, W, 17, WHITE, 256);
    glyphs(&sh_font_small, W / 2 - text_width(&sh_font_small, skin_names[k]) / 2, 84, skin_names[k], CYAN, 256);
    rect(0, 97, W, 15, CYAN, 256);
    rect(0, 112, W, 2, INK, 90);
    const char *about = k == 0 ? "THE ORIGINAL" : !sh_unlock_level[k] ? "FREE" : 0;
    if (!about) {
      number(append(s, "FINISH LEVEL "), sh_unlock_level[k]);
      about = s;
    }
    center(&sh_font_small, W / 2, 99, about, WHITE);
    for (int i = 0; i < SH_SKINS; i++) {
      int x = TILE_X(i), y = TILE_Y(i);
      bool open = sh_skin_open(V->done, i);
      round_rect(x, y + 2, TILE, TILE, 6, 0, INK, 90);
      round_rect(x, y, TILE, TILE, 6, 0, open ? WHITE : BLUE, 256);
      round_rect(x + 3, y + 3, TILE - 6, TILE - 6, 4, 0, open ? C(0xD6F1FA) : C(0x0B5F8A), 256);
      if (i == V->skin) round_rect(x, y, TILE, TILE, 6, 3, ORANGE, 256);
    }
  }
}

/* what's over the pictures: the interface */
static void over(void) {
  char s[32];
  switch (V->screen) {
    case S_PLAY:
      level_hud();
      if (!G->completed) level_keys("HOLD OK TO HOOK   BACK PAUSE");
      break;
    case S_PAUSE: {
      static const char *const items[SH_PAUSE_ITEMS] = {"RESUME", "RESTART", "LEVELS", "HOW TO PLAY", "QUIT GAME"};
      level_hud();
      dim();
      center(&sh_font_big, W / 2, 24, "PAUSED", WHITE);
      for (int i = 0; i < SH_PAUSE_ITEMS; i++) button(85, 60 + i * 31, 150, 24, items[i], V->sel == i);
      keys("OK SELECT   BACK RESUME");
      break;
    }
    case S_QUIT:
      level_hud();
      dim();
      center(&sh_font_big, W / 2, 72, "QUIT GAME?", WHITE);
      button(70, 118, 80, 26, "NO", V->sel == 0);
      button(170, 118, 80, 26, "YES", V->sel == 1);
      keys("OK SELECT   BACK NO");
      break;
    case S_TITLE:
      center(&sh_font_small, PLAY_X + 71, PLAY_Y + 9, "PLAY", WHITE);
      of_goal(s, "", V->current + 1, V->current);
      center(&sh_font_small, PLAY_X + 71, PLAY_Y + 26, s, WHITE);
      if (V->sel == 0) focus(PLAY_X, PLAY_Y, sh_sprites[SPR_BTN_PLAY].w, sh_sprites[SPR_BTN_PLAY].h, 12);
      keys(V->sel == 0 ? "OK PLAY   RIGHT SKINS   BACK QUIT" : "OK SKINS   LEFT PLAY   BACK QUIT");
      break;
    case S_VICTORY:
    case S_UNLOCKED:
      of_goal(s, "LEVEL ", V->current, V->current);   /* (current: the level after the one finished) */
      center(&sh_font_big, W / 2, 10, s, WHITE);
      if (V->screen == S_VICTORY) {
        const sh_sprite *b = &sh_sprites[SPR_BTN_NEXT];
        if (V->sel == 0) focus(160 - b->w / 2, 172 - b->h / 2, b->w, b->h, 9);
        keys(V->sel == 0 ? "OK NEXT LEVEL   RIGHT SKINS   BACK MENU" : "OK SKINS   LEFT NEXT LEVEL   BACK MENU");
      } else {
        int x = 70, y = 34, w = 180, h = 168;
        dim();
        round_rect(x, y, w, h, 6, 0, WHITE, 256);
        rect(x + 6, y + 6, w - 12, h - 12, POPUP, 256);
        sunburst(x + w / 2, y + h / 2 - 8, y + 6, y + h - 6, x + 6, x + w - 6, (float)(V->clock % 5000) * (6.2831853f / 5000));
        center(&sh_font_small, x + w / 2, y + 10, "NEW SKIN UNLOCKED!", WHITE);
        button(x + 30, y + h - 36, w - 60, 24, "EQUIP NOW", true);
        keys("OK EQUIP NOW   BACK CLOSE");
      }
      break;
    case S_SKINS: {
      int k = V->sel;
      focus(TILE_X(k), TILE_Y(k), TILE, TILE, 6);
      keys(!sh_skin_open(V->done, k) ? "ARROWS CHOOSE   BACK"
           : k == V->skin            ? "ARROWS CHOOSE   OK PLAY   BACK"
                                     : "ARROWS CHOOSE   OK SELECT   BACK");
      break;
    }
    case S_LEVELS:
      center(&sh_font_big, W / 2, 8, "LEVELS", WHITE);
      for (uint32_t row = V->level_top; row < V->level_top + SH_LEVEL_ROWS; row++)
        for (uint32_t col = 0; col < SH_LEVEL_COLS; col++) {
          uint32_t lv = row * SH_LEVEL_COLS + col;
          if (lv > V->done) break;
          int x = 14 + (int)col * 37, y = 44 + (int)(row - V->level_top) * 35;
          bool next = lv == V->done;
          round_rect(x, y + 2, 32, 28, 6, 0, INK, 90);
          round_rect(x, y, 32, 28, 6, 0, next ? CYAN : WHITE, 256);
          number(s, lv + 1);
          glyphs(&sh_font_small, x + 16 - text_width(&sh_font_small, s) / 2, y + 8, s, next ? WHITE : BLUE, 256);
          if (lv == V->level_sel) focus(x, y, 32, 28, 6);
        }
      keys("ARROWS CHOOSE   OK PLAY   BACK");
      break;
    case S_HELP: {
      static const char *const lines[] = {"HOLD OK: GRAB THE NEAREST HOOK", "LET GO: FLY", "BUMPERS BOUNCE YOU",
                                          "CROSS THE CHECKERED LINE", "BACK PAUSES   HOME SAVES AND QUITS"};
      center(&sh_font_big, W / 2, 8, "HOW TO PLAY", WHITE);
      for (int i = 0; i < 5; i++) center(&sh_font_small, W / 2, 112 + i * 21, lines[i], i == 4 ? C(0xD6F1FA) : WHITE);
      keys(V->first_run ? "OK PLAY" : "OK CONTINUE");
      break;
    }
  }
}

void sh_draw(const sh_view *v, sh_push push) {
  V = v, G = v->game;
  pictures();
  const uint16_t *grad = sh_gradients[background()];
  for (top = 0; top < H; top += BAND) {
    for (int j = 0; j < BAND; j++)
      for (int x = 0; x < W; x++) band[j * W + x] = grad[top + j];
    under();
    for (int i = trail_at; i < ncmd; i++) paint(&cmds[i], 0);
    over();
    for (int i = 0; i < ncmd; i++) paint(&cmds[i], 1);
#if NP_SOFT_FLOAT
    push(0, top, W, BAND, band);   /* (a band is cheap to show there, dearer to compare) */
#else
    uint32_t h = 2166136261u;
    for (int i = 0; i < W * BAND; i++) h = (h ^ band[i]) * 16777619u;
    if (!fresh || h != sums[top / BAND]) push(0, top, W, BAND, band);
    sums[top / BAND] = h;
#endif
  }
  fresh = true;
}
