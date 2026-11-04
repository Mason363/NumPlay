/* NumVisuals: moving backgrounds to look at, with a clock, a stopwatch, a
 * timer, a counter or your own words on top.
 *
 * Every background is a few formulas drawn in strips of 24 rows that go
 * straight to the screen, and all the text is one 5x7 font scaled up, so the
 * whole app is a few kilobytes. */
#include <eadk.h>
#include <stdbool.h>
#include <stdint.h>
#include "../../common/epsilon_app.h"
#include "../../common/epsilon_files.h"
#include "../../common/np_text.h"

#ifdef __ELF__ /* app name and API level, for the calculator's installer */
#ifndef NP_PORT /* (another target: platform/port.mk names the app) */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "NumVisuals";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif
#endif

#define W 320
#define H 240
#define SH 24 /* rows per strip */
typedef uint16_t color;
#define RGB(c) (color)((((c) >> 8) & 0xF800) | (((c) >> 5) & 0x07E0) | (((c) >> 3) & 0x1F))
#define WHITE 0xFFFF
#define KEY(k) (1ull << (k))

static color buf[W * SH];
static int top; /* the screen row buf starts at */
static uint32_t now;
static uint32_t seed = 0x2545F491;
static int16_t sn[256]; /* sine, -256..256 */
#define S(a) sn[(a) & 255]
static color pal[256];

static uint32_t rnd(void) {
  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;
  return seed;
}
static int clamp(int v, int a, int b) { return v < a ? a : v > b ? b : v; }
static int iabs(int v) { return v < 0 ? -v : v; }

/* f over b; a from 0 (all b) to 32 (all f) */
static color mix(color f, color b, int a) {
  uint32_t x = (f | (uint32_t)f << 16) & 0x07E0F81F, y = (b | (uint32_t)b << 16) & 0x07E0F81F;
  y = (y + ((x - y) * (uint32_t)a >> 5)) & 0x07E0F81F;
  return (color)(y | y >> 16);
}

/* pal: a gradient through n colours spaced evenly */
static void palette(const uint32_t *c, int n) {
  for (int i = 0; i < 256; i++) {
    int p = i * (n - 1), k = p >> 8, f = p & 255;
    uint32_t o = 0;
    for (int s = 0; s < 24; s += 8)
      o |= (uint32_t)((((c[k] >> s) & 255) * (256 - f) + ((c[k + 1] >> s) & 255) * f) >> 8) << s;
    pal[i] = RGB(o);
  }
}

static void rect(int x, int y, int w, int h, color c, int a) {
  int y0 = y < top ? top : y, y1 = y + h > top + SH ? top + SH : y + h;
  int x0 = x < 0 ? 0 : x, x1 = x + w > W ? W : x + w;
  for (int j = y0; j < y1; j++)
    for (color *p = buf + (j - top) * W + x0, *e = p + x1 - x0; p < e; p++) *p = a >= 32 ? c : mix(c, *p, a);
}
static void pixel(int x, int y, color c, int a) {
  if ((unsigned)x < W && (unsigned)(y - top) < SH) buf[(y - top) * W + x] = mix(c, buf[(y - top) * W + x], a);
}
static void gradient(int n) { /* the strip, from pal's first n entries, top to bottom */
  for (int j = 0; j < SH; j++) rect(0, top + j, W, 1, pal[(top + j) * n / H], 32);
}

/* ------------------------------------------------------------------ text */
/* ASCII 32..126 and a Backspace key (127), columns of 7 bits, top bit first */
static const uint8_t font[96][5] = {
  {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
  {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
  {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
  {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
  {0x3E,0x41,0x41,0x41,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
  {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
  {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
  {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
  {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
  {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},
  {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
  {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
  {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
  {0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
  {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
  {0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},{0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},
  {0x38,0x44,0x44,0x48,0x7F},{0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x0C,0x52,0x52,0x52,0x3E},
  {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},{0x7F,0x10,0x28,0x44,0x00},
  {0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},{0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},
  {0x7C,0x14,0x14,0x14,0x08},{0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
  {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},{0x3C,0x40,0x30,0x40,0x3C},
  {0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},{0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},
  {0x00,0x00,0x7F,0x00,0x00},{0x00,0x41,0x36,0x08,0x00},{0x08,0x04,0x08,0x10,0x08},{0x08,0x1C,0x3E,0x3E,0x3E},
};

static void glyph(char ch, int x, int y, int k, color c, int a) {
  unsigned g = (uint8_t)ch - 32u;
  if (g > 95) g = '?' - 32;
  int d = k > 3 ? k - k / 4 : k; /* big letters are made of dots */
  for (int i = 0; i < 5; i++)
    for (int j = 0, b = font[g][i]; b; j++, b >>= 1) {
      if (!(b & 1)) continue;
      int px = x + i * k, py = y + j * k;
      if (d < 4) {
        rect(px, py, d, d, c, a);
      } else { /* a little rounded */
        rect(px + 1, py, d - 2, d, c, a);
        rect(px, py + 1, 1, d - 2, c, a);
        rect(px + d - 1, py + 1, 1, d - 2, c, a);
      }
    }
}
static int slen(const char *s) {
  int n = 0;
  while (s[n]) n++;
  return n;
}
#if NP_TEXT_EXTRA
/* Other languages: an accented letter is the font's own with np_accent() over it (a cedilla under it),
   a Chinese one comes from the 12-pixel font at a whole scale near the text's size; squeeze brings the
   letters of a text too long for the screen closer. */
static int pen_x, pen_y, pen_k, pen_a, squeeze;
static color pen_c;
static void pen_dot(int i, int j, void *ctx) {
  rect(pen_x + i * pen_k, pen_y + j * pen_k, pen_k, pen_k, pen_c, pen_a);
}
/* draws (or only measures, a < 0) the next letter of *s at x; returns its advance */
static int letter(const char **s, int x, int y, int k, color c, int a) {
  uint32_t cp = np_utf8(s);
  char b = (char)cp, b2 = 0;
  int acc = cp >= 0x80 ? np_latin(cp, &b, &b2) : 0, xs = (7 * k + 6) / 12;
  pen_x = x, pen_y = y, pen_k = k, pen_c = c, pen_a = a;
  if (!b) { /* in the middle of the capitals' height */
    if (a >= 0) pen_y += (7 * k - 12 * xs) / 2, pen_k = xs, np_xdraw(cp, 0, 0, 1, pen_dot, 0);
    return np_xadvance(cp, xs);
  }
  if (a >= 0) {
    glyph(b, x, y, k, c, a);
    if (b2) glyph(b2, x + 6 * k - squeeze, y, k, c, a);
    /* over a capital, over a small letter (its top is row 2), or the cedilla under it */
    np_accent(acc, 0, acc == NP_ACC_CEDIL ? 7 : b < 'a' ? -3 : -1, 1, pen_dot, 0);
  }
  return b2 ? 12 * k - 2 * squeeze : 6 * k - squeeze;
}
static int tw(const char *s, int k) {
  int w = *s ? -k : 0;
  while (*s) w += letter(&s, 0, 0, k, 0, -1);
  return w;
}
/* white, with a soft shadow; from x */
static void text(const char *s, int x, int y, int k, int a) {
  if (y - 3 * k >= top + SH || y + 8 * k + 2 <= top) return;
  for (int o = k / 3 + 1, pass = 0; pass < 2; pass++, o = 0) {
    const char *p = s;
    for (int x1 = x + o; *p;) x1 += letter(&p, x1, y + o, k, pass ? WHITE : 0, pass ? a : a * 3 / 8);
  }
}
static void label(const char *s, int cx, int y, int k, int a) {
  squeeze = tw(s, k) > 308 ? k / 2 : 0;
  text(s, cx - tw(s, k) / 2, y, k, a);
  squeeze = 0;
}
#else
static int tw(const char *s, int k) { return slen(s) ? slen(s) * 6 * k - k : 0; }
/* white, with a soft shadow; from x */
static void text(const char *s, int x, int y, int k, int a) {
  if (y >= top + SH || y + 8 * k <= top) return;
  for (int o = k / 3 + 1, pass = 0; pass < 2; pass++, o = 0)
    for (int i = 0; s[i]; i++) glyph(s[i], x + o + i * 6 * k, y + o, k, pass ? WHITE : 0, pass ? a : a * 3 / 8);
}
static void label(const char *s, int cx, int y, int k, int a) { text(s, cx - tw(s, k) / 2, y, k, a); }
#endif
/* a big line centred on (160, cy), with a smaller part after it, fitted to
   the screen */
static void big(const char *s, const char *small, int cy, int k, int a) {
  int ks;
  for (;; k--) {
    ks = k * 2 / 5 < 2 ? 2 : k * 2 / 5;
    if (k <= 2 || tw(s, k) + (*small ? k + tw(small, ks) : 0) <= 300) break;
  }
  int x = 160 - (tw(s, k) + (*small ? k + tw(small, ks) : 0)) / 2, y = cy - 7 * k / 2;
  text(s, x, y, k, a);
  if (*small) text(small, x + tw(s, k) + k, y + 7 * (k - ks), ks, a);
}

/* ------------------------------------------------------------------ backgrounds */
static int16_t ca[W], cb[W], cc[W];

static void aurora_frame(int dt) {
  int t = (int)now;
  for (int x = 0; x < W; x++) {
    ca[x] = (int16_t)(104 + (S(x + t / 29) * 22 + S(x * 2 - t / 41 + S(x / 2 + t / 67) / 6) * 12) / 256);
    cb[x] = (int16_t)(175 + S(x * 3 + t / 13) / 9 + S(x * 9 - t / 9) / 12 + S(x / 2 - t / 23) / 4);
    cc[x] = (int16_t)(220 + S(x * 2 + 50) / 26 + S(x * 7) / 50);
  }
}
static void aurora(void) {
  for (int j = 0; j < SH; j++) {
    int y = top + j, sky = 8 + y * 52 / H; /* the night sky, lighter near the horizon */
    color *p = buf + j * W;
    for (int x = 0; x < W; x++) {
      if (y >= cc[x]) { /* hills */
        p[x] = RGB(0x010308);
        continue;
      }
      int d = ca[x] - y, i = d >= 0 ? cb[x] - d * 3 / 2 : cb[x] * 2 / 3 + d * 3; /* rays above, a glow below */
      if (i < sky) {
        uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u;
        i = (h * 0x5BD1E995u) >> 22 ? sky : 190; /* a few stars */
      }
      p[x] = pal[i > 255 ? 255 : i];
    }
  }
}

static void synth(void) {
  int t = (int)now;
  for (int j = 0; j < SH; j++) {
    int y = top + j;
    color *p = buf + j * W;
    if (y < 150) {
      color sky = pal[y * 255 / 150];
      int dy = y - 102, r2 = 3600 - dy * dy; /* the sun: (160, 102), radius 60 */
      bool cut = dy > 4 && dy % 11 < (dy - 4) / 9 + 1;
      color sun = mix(RGB(0xFF3F81), RGB(0xFFE45E), clamp((y - 42) * 32 / 108, 0, 32));
      for (int x = 0; x < W; x++) {
        int dx = x - 160, d2 = dx * dx + dy * dy;
        p[x] = dx * dx < r2 && !cut ? sun : d2 < 8100 ? mix(RGB(0xFF4F9A), sky, (8100 - d2) / 700) : sky;
      }
    } else { /* the grid: a line wherever one falls between this pixel and the next */
      int d = y - 146, z0 = 98304 / d, z1 = 98304 / (d + 1), off = t / 2, a = clamp(4 + d / 3, 0, 28);
      bool row = ((z0 + off) >> 8) != ((z1 + off) >> 8) || d == 4;
      color base = mix(RGB(0x0A0018), RGB(0x2A0A4A), d * 32 / 94), line = mix(RGB(0xFF3DDB), base, d == 4 ? 32 : a);
      int q = (-160 * z0) >> 15;
      for (int x = 0; x < W; x++) {
        int nq = ((x - 159) * z0) >> 15;
        p[x] = row || nq != q ? line : base;
        q = nq;
      }
    }
  }
}

static int plasma_k, plasma_speed;
static void plasma_frame(int dt) {
  int t = (int)now * plasma_speed / 16, k = plasma_k;
  for (int x = 0; x < W; x++) ca[x] = (int16_t)(S(x * k / 8 + t / 7) + S(x * k / 13 - t / 11));
  for (int y = 0; y < H; y++) cb[y] = (int16_t)(S(y * k / 8 - t / 9) + S(y * k / 17 + t / 13));
}
static void plasma(void) {
  int t = (int)now * plasma_speed / 16, k = plasma_k;
  for (int j = 0; j < SH; j++) {
    int y = top + j, r = cb[y];
    color *p = buf + j * W;
    for (int x = 0; x < W; x++) p[x] = pal[(uint8_t)(((ca[x] + r + S((x + y) * k / 16 + t / 5)) >> 3) + (t >> 4))];
  }
}
static void plasma_init(void) { plasma_k = 10, plasma_speed = 16; }
static void pastel_init(void) { plasma_k = 6, plasma_speed = 8; }

static int16_t bx[6], by[6];
static void lava_frame(int dt) {
  int t = (int)now;
  for (int i = 0; i < 6; i++) {
    bx[i] = (int16_t)(160 + S(t / (41 + i * 7) + i * 43) * (110 - i * 9) / 256);
    by[i] = (int16_t)(120 + S(t / (53 + i * 11) + i * 97 + 64) * (100 - i * 6) / 256);
  }
}
static void lava(void) {
  for (int j = 0; j < SH; j++) {
    int y = top + j, dy2[6];
    color *p = buf + j * W;
    for (int i = 0; i < 6; i++) dy2[i] = (y - by[i]) * (y - by[i]) + 16;
    for (int x = 0; x < W; x += 2) {
      int f = 0;
      for (int i = 0; i < 6; i++) {
        int dx = x - bx[i], r = 26 + i * 3;
        f += (r * r << 7) / (dx * dx + dy2[i]);
      }
      p[x] = p[x + 1] = pal[f < 128 ? f * 5 / 8 : 150 + (f - 128 > 315 ? 105 : (f - 128) / 3)];
    }
  }
}

#define NS 240
static int16_t wx[NS], wy[NS], wz[NS], wpx[NS], wpy[NS], wqx[NS], wqy[NS];
static void star(int i, int z) {
  wx[i] = (int16_t)(rnd() % 2400) - 1200;
  wy[i] = (int16_t)(rnd() % 1800) - 900;
  wz[i] = (int16_t)z;
  wpx[i] = wqx[i] = (int16_t)(160 + wx[i] * 64 / z);
  wpy[i] = wqy[i] = (int16_t)(120 + wy[i] * 64 / z);
}
static void warp_init(void) {
  for (int i = 0; i < NS; i++) star(i, 64 + rnd() % 960);
}
static void warp_frame(int dt) {
  for (int i = 0; i < NS; i++) {
    wqx[i] = wpx[i], wqy[i] = wpy[i];
    wz[i] = (int16_t)(wz[i] - dt / 2 - 1);
    if (wz[i] < 20) {
      star(i, 1024);
      continue;
    }
    wpx[i] = (int16_t)(160 + wx[i] * 64 / wz[i]);
    wpy[i] = (int16_t)(120 + wy[i] * 64 / wz[i]);
    if (wpx[i] < -40 || wpx[i] > W + 40 || wpy[i] < -40 || wpy[i] > H + 40) star(i, 1024);
  }
}
static void warp(void) {
  gradient(255);
  for (int i = 0; i < NS; i++) {
    int x0 = wqx[i], y0 = wqy[i], x1 = wpx[i], y1 = wpy[i];
    if ((y0 < top && y1 < top) || (y0 >= top + SH && y1 >= top + SH)) continue;
    int a = clamp(40 - wz[i] / 26, 10, 32), n = iabs(x1 - x0) > iabs(y1 - y0) ? iabs(x1 - x0) : iabs(y1 - y0);
    n = clamp(n, 1, 40);
    for (int s = 0; s <= n; s++) pixel(x0 + (x1 - x0) * s / n, y0 + (y1 - y0) * s / n, RGB(0xDDE8FF), a * (s + 1) / (n + 1));
    if (wz[i] < 420) pixel(x1 + 1, y1, WHITE, a), pixel(x1, y1 + 1, WHITE, a), pixel(x1 + 1, y1 + 1, WHITE, a);
  }
}

/* angle and depth for a quarter of the screen, at half resolution */
static uint8_t tun_a[60][80], tun_d[60][80];
static float atn(float v) { /* atan, 0 <= v <= 1 */
  float s = v * v;
  return v * (0.9998660f + s * (-0.3302995f + s * (0.1801410f + s * (-0.0851330f + s * 0.0208351f))));
}
static void tunnel_init(void) {
  if (tun_d[0][0]) return;
  for (int y = 0; y < 60; y++)
    for (int x = 0; x < 80; x++) {
      float fx = x + 0.5f, fy = y + 0.5f, a = fx >= fy ? atn(fy / fx) : 1.5707964f - atn(fx / fy);
      tun_a[y][x] = (uint8_t)(a * 40.743665f); /* 64 a quarter turn */
      int d = (int)(1400 / __builtin_sqrtf(fx * fx + fy * fy));
      tun_d[y][x] = (uint8_t)(d > 255 ? 255 : d);
    }
}
static void tunnel(void) {
  int t = (int)now, spin = t / 24, move = t / 6;
  for (int j = 0; j < SH; j++) {
    int hy = (top + j) / 2 - 60, qy = hy < 0 ? -hy - 1 : hy;
    color *p = buf + j * W;
    for (int x = 0; x < W; x += 2) {
      int hx = x / 2 - 80, qx = hx < 0 ? -hx - 1 : hx, a = tun_a[qy][qx], d = tun_d[qy][qx];
      int u = hx >= 0 ? (hy >= 0 ? a : 256 - a) : (hy >= 0 ? 128 - a : 128 + a);
      color c = pal[(uint8_t)(d * 2 + move)];
      if ((((u * 2 + spin) >> 4) ^ ((d + move) >> 4)) & 1) c = mix(c, 0, 20);
      p[x] = p[x + 1] = mix(c, 0, 32 - (d >> 3));
    }
  }
}

#define FW 80
#define FH 60
static uint8_t fire[FH + 1][FW];
static int fire_acc;
static void fire_init(void) {
  for (int y = 0; y <= FH; y++)
    for (int x = 0; x < FW; x++) fire[y][x] = 0;
}
static void fire_frame(int dt) {
  for (fire_acc = clamp(fire_acc + dt, 0, 120); fire_acc >= 30; fire_acc -= 30) {
    for (int x = 0; x < FW; x++) fire[FH][x] = (uint8_t)(150 + rnd() % 106);
    for (int y = 0; y < FH; y++)
      for (int x = 0; x < FW; x++) {
        uint32_t r = rnd();
        int v = fire[y + 1][clamp(x + (int)(r & 3) - 1, 0, FW - 1)] - (int)((r >> 2) % 11);
        fire[y][x] = (uint8_t)(v < 0 ? 0 : v);
      }
  }
}
static void flames(void) {
  for (int j = 0; j < SH; j++) {
    int y = top + j, wy = y & 3;
    const uint8_t *r0 = fire[y >> 2], *r1 = fire[(y >> 2) + 1];
    color *p = buf + j * W;
    for (int x = 0; x < W; x++) {
      int fx = x >> 2, fx1 = fx + 1 < FW ? fx + 1 : fx, wx = x & 3;
      p[x] = pal[((r0[fx] * (4 - wx) + r0[fx1] * wx) * (4 - wy) + (r1[fx] * (4 - wx) + r1[fx1] * wx) * wy) >> 4];
    }
  }
}

static int16_t wave[6][W];
static void ocean_frame(int dt) {
  static const uint8_t base[6] = {121, 130, 142, 158, 180, 209}, amp[6] = {2, 3, 4, 6, 9, 13};
  int t = (int)now;
  for (int k = 0; k < 6; k++)
    for (int x = 0; x < W; x++)
      wave[k][x] = (int16_t)(base[k] + (S(x * (8 - k) / 4 + t / (70 + k * 25) + k * 50) * 2 +
                                        S(x * (11 - k) / 3 - t / (110 + k * 30) + k * 90)) * amp[k] / 768);
}
static void ocean(void) {
  static const color sea[6] = {RGB(0x7FA9CF), RGB(0x5A8CBE), RGB(0x3F72A8), RGB(0x2B5B91), RGB(0x1C4677), RGB(0x11335E)};
  int t = (int)now;
  for (int j = 0; j < SH; j++) {
    int y = top + j;
    color *p = buf + j * W;
    for (int x = 0; x < W; x++) {
      int k = 5;
      while (k >= 0 && y < wave[k][x]) k--;
      color c;
      if (k < 0) { /* the sky and a low sun */
        int dx = x - 236, dy = y - 92, d2 = dx * dx + dy * dy;
        c = d2 < 225 ? RGB(0xFFF1C9) : pal[clamp(y * 2, 0, 255)];
        if (d2 >= 225 && d2 < 2500) c = mix(RGB(0xFFD9A0), c, (2500 - d2) / 160);
      } else {
        int e = y - wave[k][x];
        c = e < 2 ? mix(WHITE, sea[k], 12 - e * 5) : sea[k];
        if (iabs(x - 236) < 4 + (y - 120) / 4 && S(y * 29 + x * 3 + t / 6) > 170) c = mix(RGB(0xFFE2B0), c, 14);
      }
      p[x] = c;
    }
  }
}

#define NB 22
static int16_t bkx[NB], bky[NB], bkr[NB]; /* bky in 1/16 pixels */
static uint8_t bkv[NB], bka[NB], bkc[NB];
static void bubble(int i, int y) {
  bkx[i] = (int16_t)(rnd() % W);
  bkr[i] = (int16_t)(8 + rnd() % 28);
  bky[i] = (int16_t)((y + bkr[i]) * 16);
  bkv[i] = (uint8_t)(4 + rnd() % 12);
  bka[i] = (uint8_t)(5 + rnd() % 8);
  bkc[i] = (uint8_t)(rnd() % 5);
}
static void bokeh_init(void) {
  for (int i = 0; i < NB; i++) bubble(i, (int)(rnd() % H) - 30);
}
static void bokeh_frame(int dt) {
  for (int i = 0; i < NB; i++) {
    bky[i] = (int16_t)(bky[i] - bkv[i] * dt / 64);
    if (bky[i] / 16 + bkr[i] < 0) bubble(i, H);
  }
}
static void bokeh(void) {
  static const color cols[5] = {RGB(0xFFB84D), RGB(0xFF6B9A), RGB(0x9B7BFF), RGB(0x5CE1E6), RGB(0xFFE08A)};
  gradient(255);
  for (int i = 0; i < NB; i++) {
    int r = bkr[i], cx = bkx[i] + S((int)now / 30 + i * 37) * 10 / 256, cy = bky[i] / 16;
    int y0 = clamp(cy - r, top, top + SH), y1 = clamp(cy + r + 1, top, top + SH);
    for (int y = y0; y < y1; y++)
      for (int x = clamp(cx - r, 0, W), x1 = clamp(cx + r + 1, 0, W); x < x1; x++) {
        int e = r * r - (x - cx) * (x - cx) - (y - cy) * (y - cy), a = bka[i];
        if (e <= 0) continue;
        if (e < 2 * r) a = a * e / (2 * r);
        else if (e < 5 * r) a += 3; /* a brighter rim, like a lens */
        color *q = buf + (y - top) * W + x;
        *q = mix(cols[bkc[i]], *q, a);
      }
  }
}

#define MC 40
#define MR 24
static uint8_t mg[MR][MC], mspd[MC], mlen[MC];
static int32_t mh[MC]; /* the head's row, in 1/256 */
static void drop(int c) {
  mh[c] = -(int32_t)(rnd() % (MR * 256));
  mspd[c] = (uint8_t)(2 + rnd() % 4);
  mlen[c] = (uint8_t)(5 + rnd() % 14);
}
static void rain_init(void) {
  for (int c = 0; c < MC; c++) {
    for (int r = 0; r < MR; r++) mg[r][c] = (uint8_t)(33 + rnd() % 94);
    drop(c);
    mh[c] += MR * 128;
  }
}
static void rain_frame(int dt) {
  for (int c = 0; c < MC; c++) {
    mh[c] += mspd[c] * dt;
    if ((mh[c] >> 8) - mlen[c] > MR) drop(c);
  }
  for (int i = 0; i < 6; i++) mg[rnd() % MR][rnd() % MC] = (uint8_t)(33 + rnd() % 94);
}
static void rain(void) {
  rect(0, top, W, SH, 0, 32);
  for (int r = top / 10; r <= (top + SH - 1) / 10 && r < MR; r++)
    for (int c = 0; c < MC; c++) {
      int d = (mh[c] >> 8) - r;
      if (d < 0 || d >= mlen[c]) continue;
      glyph((char)mg[r][c], c * 8 + 1, r * 10 + 1, 1, d ? mix(RGB(0x2BFF6A), 0, 32 - d * 28 / mlen[c]) : RGB(0xD8FFE0), 32);
    }
}

typedef struct {
  const char *name;
  void (*init)(void);
  void (*frame)(int dt);
  void (*draw)(void);
  uint8_t n; /* colours in pal */
  uint32_t pal[6];
} bg_t;
static const bg_t bgs[] = {
  {T("Aurora"), 0, aurora_frame, aurora, 5, {0x02030C, 0x071A33, 0x0E6070, 0x33E8A0, 0xD2FFEA}},
  {T("Sunset Drive"), 0, 0, synth, 4, {0x14002E, 0x55106E, 0xC72C79, 0xFF8A5B}},
  {T("Plasma"), plasma_init, plasma_frame, plasma, 6, {0x1B0B3A, 0x6B1FA8, 0xFF3E8A, 0xFFC857, 0x2EC4B6, 0x1B0B3A}},
  {T("Pastel"), pastel_init, plasma_frame, plasma, 6, {0xFFB3C7, 0xB9A2FF, 0x8FD8FF, 0x9EEDB6, 0xFFE08A, 0xFFB3C7}},
  {T("Lava Lamp"), 0, lava_frame, lava, 5, {0x12061F, 0x4A0F3F, 0xB8233B, 0xFF7A2E, 0xFFE7A6}},
  {T("Warp"), warp_init, warp_frame, warp, 3, {0x000004, 0x06061C, 0x140A2E}},
  {T("Tunnel"), tunnel_init, 0, tunnel, 5, {0x00E5FF, 0x7A2BFF, 0xFF2BB1, 0x7A2BFF, 0x00E5FF}},
  {T("Fire"), fire_init, fire_frame, flames, 5, {0x07050B, 0x4A0A0A, 0xC4260C, 0xFF8A1E, 0xFFF0B8}},
  {T("Ocean"), 0, ocean_frame, ocean, 4, {0x0E1B45, 0x5A3C8C, 0xE9867A, 0xFFD6A0}},
  {T("Bokeh"), bokeh_init, bokeh_frame, bokeh, 3, {0x0B0520, 0x250A33, 0x3A0F2E}},
  {T("Code Rain"), rain_init, rain_frame, rain, 0, {0}},
};
#define NBG (int)(sizeof bgs / sizeof bgs[0])

/* ------------------------------------------------------------------ add-ons */
enum { A_NONE, A_CLOCK, A_STOPWATCH, A_TIMER, A_COUNTER, A_TEXT, A_COUNT };
static const char *const addon_names[A_COUNT] = {T("None"), T("Clock"), T("Stopwatch"), T("Timer"), T("Counter"), T("Text")};

/* everything kept between visits (numvisuals.sav) */
static struct {
  uint8_t magic, version, bg, addon, h12, sw_on, tm_on, pad; /* tm_on: 0 paused, 1 running, 2 ringing */
  int32_t counter;
  uint32_t clock0; /* seconds of the day when the millisecond counter was 0 */
  uint32_t sw_acc, sw_t0, tm_len, tm_left, tm_t0;
  char text[24];
} V;
#define SAVE_NAME "numvisuals.sav"

static char *two(char *o, unsigned v) {
  *o++ = (char)('0' + v / 10 % 10);
  *o++ = (char)('0' + v % 10);
  return o;
}
static char *num(char *o, int32_t v) {
  char t[12];
  int n = 0;
  uint32_t u = v < 0 ? 0u - (uint32_t)v : (uint32_t)v;
  if (v < 0) *o++ = '-';
  do t[n++] = (char)('0' + u % 10); while (u /= 10);
  while (n) *o++ = t[--n];
  return o;
}
static uint32_t stopwatch(void) { return V.sw_on ? V.sw_acc + (now - V.sw_t0) : V.sw_acc; }
static uint32_t timer_left(void) {
  if (V.tm_on != 1) return V.tm_left;
  uint32_t gone = now - V.tm_t0;
  return gone >= V.tm_left ? 0 : V.tm_left - gone;
}

static void addon_draw(void) {
  char s[16] = {0}, m[8] = {0}, *o = s;
  int a = 32;
  switch (V.addon) {
    case A_CLOCK: {
      uint32_t t = (V.clock0 + now / 1000) % 86400, h = t / 3600;
      if (V.h12) o = num(o, (int32_t)(h % 12 ? h % 12 : 12));
      else o = two(o, h);
      *o++ = now % 1000 < 500 ? ':' : ' ';
      two(o, t / 60 % 60);
      if (V.h12) m[0] = h < 12 ? 'A' : 'P', m[1] = 'M';
      else m[0] = ':', two(m + 1, t % 60);
      break;
    }
    case A_STOPWATCH: {
      uint32_t ms = stopwatch(), sec = ms / 1000;
      if (sec >= 3600) o = num(o, (int32_t)(sec / 3600)), *o++ = ':';
      o = two(o, sec / 60 % 60);
      *o++ = ':';
      two(o, sec % 60);
      if (sec < 3600) m[0] = '.', two(m + 1, ms / 10 % 100);
      if (!V.sw_on) a = 22;
      break;
    }
    case A_TIMER: {
      uint32_t sec = (timer_left() + 999) / 1000;
      o = two(o, sec / 60);
      *o++ = ':';
      two(o, sec % 60);
      if (V.tm_on == 0) a = 22;
      if (V.tm_on == 2) { /* time's up: blink, and the screen pulses */
        a = now % 800 < 400 ? 32 : 6;
        rect(0, top, W, SH, WHITE, (int)(S((int)now / 3) + 256) / 64);
      }
      break;
    }
    case A_COUNTER: num(o, V.counter); big(s, "", 116, 16, 32); return;
    case A_TEXT: big(V.text, "", 116, 12, 32); return;
    default: return;
  }
  big(s, m, 116, 10, a);
}

/* ------------------------------------------------------------------ the app */
enum { M_BG, M_ADDON, M_SET, M_SHOW };
static int mode, field, typed, set_v[3];
static uint32_t hint_t, toast_t;
static bool text_wait;
static const bg_t *cur;

static void choose_bg(int i) {
  V.bg = (uint8_t)((i + NBG) % NBG);
  cur = &bgs[V.bg];
  if (cur->n) palette(cur->pal, cur->n);
  if (cur->init) cur->init();
}

/* OK on an add-on: set it up if it needs it, else show it */
static void open_addon(void) {
  mode = M_SET, field = 0, typed = 0;
  if (V.addon == A_CLOCK) {
    uint32_t t = (V.clock0 + now / 1000) % 86400;
    set_v[0] = (int)(t / 3600), set_v[1] = (int)(t / 60 % 60), set_v[2] = V.h12;
  } else if (V.addon == A_TIMER) {
    set_v[0] = (int)(V.tm_len / 60000), set_v[1] = (int)(V.tm_len / 1000 % 60);
  } else if (V.addon == A_TEXT) {
    text_wait = true;
  } else {
    mode = M_SHOW, hint_t = now;
  }
}
static void apply_set(void) {
  if (V.addon == A_CLOCK) {
    V.clock0 = ((uint32_t)(set_v[0] * 3600 + set_v[1] * 60) + 86400 - now / 1000 % 86400) % 86400;
    V.h12 = (uint8_t)set_v[2];
  } else if (V.addon == A_TIMER) {
    V.tm_len = (uint32_t)(set_v[0] * 60 + set_v[1]) * 1000;
    if (!V.tm_len) V.tm_len = 60000;
    V.tm_left = V.tm_len, V.tm_on = 1, V.tm_t0 = now;
  }
  mode = M_SHOW, hint_t = now;
}

/* a typed character (Alpha gives letters), or 0 */
static char event_char(int e) {
  static const char letters[] = "abcdefghijklmnopq\1rstuv\1wxyz \1?!";
  static const uint8_t keys[] = {30, 31, 32, 36, 37, 38, 42, 43, 44, 48, 49, 22, 45, 46, 39, 40, 33, 34, 122, 123, 124, 125, 81, 82, 83, 76};
  static const char chars[] = "7894561230.,+-*/():;\"%=<>_";
  if (e >= 126 && e < 158) return letters[e - 126] == 1 ? 0 : letters[e - 126];
  if (e >= 180 && e < 208) return letters[e - 180] >= 'a' ? (char)(letters[e - 180] - 32) : 0;
  for (unsigned i = 0; i < sizeof keys; i++)
    if (keys[i] == e) return chars[i];
  return 0;
}

static const char *hint(void) {
  switch (V.addon) {
    case A_CLOCK: case A_TEXT: return T("OK: change  BACK: menu");
    case A_STOPWATCH: case A_TIMER: return T("OK: go/stop  \x7f: reset");
    case A_COUNTER: return T("OK: +1  -: -1  \x7f: 0");
    default: return T("BACK: menu");
  }
}

static void ui_draw(void) {
  int fade = mode == M_SHOW ? clamp(32 - ((int)(now - hint_t) - 2500) / 16, 0, 32) : 32;
  int toast = mode == M_SHOW ? clamp(32 - ((int)(now - toast_t) - 1500) / 16, 0, 32) : 0;
  if (mode == M_BG || mode == M_ADDON) {
    bool b = mode == M_BG;
    rect(0, 0, W, 28, 0, 10);
    rect(0, 180, W, 60, 0, 12);
    char n[8], *o = num(n, b ? V.bg + 1 : V.addon + 1);
    *o++ = '/';
    *num(o, b ? NBG : A_COUNT) = 0;
    label(b ? T("BACKGROUND") : T("ADD-ON"), 160, 8, 2, 26);
    text(n, 8, 8, 2, 16);
    label(b ? cur->name : addon_names[V.addon], 160, 188, 3, 32);
    label("<", 14, 188, 3, 32);
    label(">", 306, 188, 3, 32);
    label(b ? T("OK: add-ons  BACK: quit") : T("OK: choose  BACK: back"), 160, 218, 2, 22);
  } else if (mode == M_SET) {
    static const char *const titles[3] = {T("SET THE TIME"), T("SET THE TIMER"), T("TYPE YOUR TEXT")};
    bool blink = now % 1000 < 600;
    rect(0, 0, W, 28, 0, 10);
    rect(0, 206, W, 34, 0, 12);
    label(titles[V.addon == A_CLOCK ? 0 : V.addon == A_TIMER ? 1 : 2], 160, 8, 2, 26);
    if (V.addon == A_TEXT) {
      char s[26], *o = s;
      for (int i = 0; V.text[i]; i++) *o++ = V.text[i];
      *o++ = blink ? '_' : ' ';
      *o = 0;
      big(s, "", 120, 9, 32);
      label(T("ALPHA: letters  OK: done"), 160, 216, 2, 22);
    } else {
      char s[8], *o = two(s, (unsigned)set_v[0]);
      *o++ = ':';
      *two(o, (unsigned)set_v[1]) = 0;
      int x = 160 - tw(s, 10) / 2;
      text(s, x, 81, 10, 32);
      if (field < 2) rect(x + field * 180, 158, 110, 4, WHITE, blink ? 32 : 10);
      if (V.addon == A_CLOCK) {
        const char *f = set_v[2] ? T("12-hour") : T("24-hour");
        if (field == 2) rect(160 - tw(f, 2) / 2 - 10, 170, tw(f, 2) + 20, 22, WHITE, blink ? 10 : 6);
        label(f, 160, 174, 2, 32);
      }
      label(T("ARROWS: set  OK: done"), 160, 216, 2, 22);
    }
  } else if (fade || toast) {
    rect(0, 196, W, 44, 0, 12 * (fade > toast ? fade : toast) / 32);
    if (toast) label(cur->name, 160, 200, 2, toast);
    label(hint(), 160, 220, 2, fade * 22 / 32);
  }
}

static void save(void) {
  V.magic = 'V', V.version = 1;
  ef_write(SAVE_NAME, &V, sizeof V);
}

int main(void) {
  np_app_begin();
  for (int i = 0; i < 128; i++) { /* Bhaskara's sine */
    float x = i * (3.14159265f / 128), q = x * (3.14159265f - x);
    sn[i] = (int16_t)(16 * q / (49.348022f - 4 * q) * 256 + 0.5f);
    sn[i + 128] = (int16_t)-sn[i];
  }
  now = eadk_timing_millis();
  seed ^= now;
  uint32_t n = 0;
  const uint8_t *d = ef_read(SAVE_NAME, &n);
  if (d && n == sizeof V && d[0] == 'V' && d[1] == 1) {
    for (uint32_t i = 0; i < n; i++) ((uint8_t *)&V)[i] = d[i];
    V.text[23] = 0;
    V.addon %= A_COUNT;
    if (now < V.sw_t0) V.sw_t0 = now; /* the calculator restarted */
    if (now < V.tm_t0) V.tm_t0 = now;
    mode = M_SHOW, hint_t = now;
  } else {
    V.tm_len = V.tm_left = 300000;
    V.clock0 = 12 * 3600;
  }
  choose_bg(V.bg);
  uint64_t held = eadk_keyboard_scan();
  uint32_t last = now, repeat_at = 0;
  for (;;) {
    now = eadk_timing_millis();
    int dt = clamp((int)(now - last), 0, 100);
    last = now;
    uint64_t k = eadk_keyboard_scan(), hit = k & ~held;
    const uint64_t rep = KEY(eadk_key_left) | KEY(eadk_key_right) | KEY(eadk_key_up) | KEY(eadk_key_down) |
                         KEY(eadk_key_plus) | KEY(eadk_key_minus);
    if (hit & rep) repeat_at = now + 380;
    else if ((k & rep) && (int32_t)(now - repeat_at) >= 0) hit |= k & rep, repeat_at = now + 70;
    held = k;
    if (k & (KEY(eadk_key_home) | KEY(eadk_key_on_off))) break;
    bool ok = hit & (KEY(eadk_key_ok) | KEY(eadk_key_exe)), back = hit & KEY(eadk_key_back);
    int lr = (hit & KEY(eadk_key_right) ? 1 : 0) - (hit & KEY(eadk_key_left) ? 1 : 0);
    int ud = (hit & KEY(eadk_key_up) ? 1 : 0) - (hit & KEY(eadk_key_down) ? 1 : 0);
    if (V.tm_on == 1 && !timer_left()) V.tm_on = 2, V.tm_left = 0; /* time's up */
    if (mode == M_SET && V.addon == A_TEXT) { /* typing goes through the calculator's events, for Alpha */
      if (text_wait) {
        text_wait = k != 0;
      } else {
        int32_t timeout = 0;
        int e = eadk_event_get(&timeout), len = slen(V.text);
        if (e == 6 || e == 8) break;
        char c = event_char(e);
        if (e == eadk_event_ok || e == eadk_event_exe || e == eadk_event_back) mode = M_SHOW, hint_t = now;
        else if (e == eadk_event_backspace && len) V.text[len - 1] = 0;
        else if (c && len < 23) V.text[len] = c, V.text[len + 1] = 0;
      }
    } else if (mode == M_BG) {
      if (lr) choose_bg(V.bg + lr);
      if (ok) mode = M_ADDON;
      if (back) break;
    } else if (mode == M_ADDON) {
      if (lr) V.addon = (uint8_t)((V.addon + A_COUNT + lr) % A_COUNT);
      if (ok) open_addon();
      if (back) mode = M_BG;
    } else if (mode == M_SET) {
      int n = V.addon == A_CLOCK ? 3 : 2, max = field == 2 ? 2 : field == 1 ? 60 : V.addon == A_CLOCK ? 24 : 100;
      if (lr) field = clamp(field + lr, 0, n - 1), typed = 0;
      if (ud) set_v[field] = (set_v[field] + max + ud) % max, typed = 0;
      static const int8_t digit_keys[10] = {48, 42, 43, 44, 36, 37, 38, 30, 31, 32};
      for (int i = 0; i < 10; i++)
        if ((hit & KEY(digit_keys[i])) && field < 2) {
          set_v[field] = clamp(typed ? set_v[field] * 10 + i : i, 0, max - 1);
          if (++typed == 2) typed = 0, field = field + 1 < n ? field + 1 : field;
        }
      if (ok) apply_set();
      if (back) mode = M_ADDON;
    } else {
      if (back) mode = M_ADDON;
      if (lr) choose_bg(V.bg + lr), toast_t = now;
      if (V.addon == A_STOPWATCH) {
        if (ok) {
          if (V.sw_on) V.sw_acc = stopwatch(), V.sw_on = 0;
          else V.sw_t0 = now, V.sw_on = 1;
        }
        if (hit & KEY(eadk_key_backspace)) V.sw_acc = 0, V.sw_t0 = now;
      } else if (V.addon == A_TIMER) {
        if (ok) {
          if (V.tm_on == 1) V.tm_left = timer_left(), V.tm_on = 0;
          else if (V.tm_on == 2) V.tm_on = 0, V.tm_left = V.tm_len;
          else V.tm_left = V.tm_left ? V.tm_left : V.tm_len, V.tm_on = 1, V.tm_t0 = now;
        }
        if (hit & KEY(eadk_key_backspace)) V.tm_on = 0, V.tm_left = V.tm_len;
      } else if (V.addon == A_COUNTER) {
        if (ok || (hit & (KEY(eadk_key_up) | KEY(eadk_key_plus)))) V.counter += V.counter < 999999999;
        if (hit & (KEY(eadk_key_down) | KEY(eadk_key_minus))) V.counter -= V.counter > -999999999;
        if (hit & KEY(eadk_key_backspace)) V.counter = 0;
      } else if (ok && (V.addon == A_CLOCK || V.addon == A_TEXT)) {
        open_addon();
      }
    }
    if (cur->frame) cur->frame(dt);
    eadk_display_wait_for_vblank();
    for (top = 0; top < H; top += SH) {
      cur->draw();
      if (mode == M_ADDON || mode == M_SHOW) addon_draw();
      ui_draw();
      eadk_display_push_rect((eadk_rect_t){0, (uint16_t)top, W, SH}, buf);
    }
    uint32_t spent = eadk_timing_millis() - now;
    if (spent < 33) eadk_timing_msleep(33 - spent);
  }
  save();
  return np_app_end();
}
