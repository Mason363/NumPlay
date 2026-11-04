/* Core Ball: shoot every ball into the spinning core without touching another.
 *
 * Random's (Yang Hao's) HTML5 game of 2015, its 60-level release: the same levels, the core's turns,
 * speeds and timers, the ball's flight, the collision, the green clear and red miss, the sliding panels
 * and "to be continued..." after level 60. The numbers come from the author's own build (MIT), checked
 * frame by frame (gamesrc/coreball/spec): angles move in tenths of a degree a frame at 60 frames a second,
 * exactly as there, and a ball hits another one when it lands within 8.4 degrees of it.
 *
 * Inside NumPlay it draws with the launcher's strips, shapes and fonts (launcher/src/gfx.c) and saves
 * through its file functions; on its own (CoreBall.nwa) it brings them along.
 *
 * Made by Bobydeluxe and Mason Chen as part of NumPlay. */
#include <eadk.h>
#include "../../../launcher/src/gfx.h"
#include "../../common/np_text.h"

#if NP_MODULE /* (np_file_read and np_file_write: NumPlay's, launcher/src/np.h) */
#define np_app_begin()
#define np_app_end() 0
#else
#include "../../common/epsilon_app.h"
#include "../../common/epsilon_files.h"
#define np_file_read ef_read
#define np_file_write ef_write
#ifndef HOST /* (a computer build of the tests has no app header) */
#ifndef NP_PORT /* (another target: platform/port.mk names the app) */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Core Ball";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif
#endif
void np_push(int x, int y, int w, int h, const color_t *px) {
  eadk_display_push_rect((eadk_rect_t){(uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h}, px);
}
#endif

#define KEY(k) (1ull << (k))

/* ------------------------------------------------------------ the original's numbers
   Its world is 560 pixels high: the core at (middle, 200), radius 50; pins of radius 12 at 150 from its
   centre; the queue from y = 400, 36 apart; a shot flies to 364 in 50 ms (3 frames). On screen: 3/7. */
#define S (3.0f / 7)
#define CY (200 * S)
#define HIT 84    /* a pin within 8.4 degrees of straight down when a ball lands there: a miss */
#define SPAN 61   /* frames of the green or red screen (more than 1000 ms) */
#define SLIDE 30  /* the gray and black panels: 30 pixels a frame */
#define LAST 60

/* the levels: preset pins (n of them, at ceil(i * 360 / n) degrees), shots, rotation pattern */
static const uint16_t levels[LAST] = {
  0x0104, 0x0146, 0x0282, 0x0588, 0x010C, 0x054A, 0x01AC, 0x0470, 0x0740, 0x0150,
  0x090A, 0x0D86, 0x088C, 0x0DC8, 0x08C8, 0x0D45, 0x0986, 0x0DC8, 0x0AE0, 0x0DAA,
  0x1184, 0x1146, 0x1188, 0x11C7, 0x1242, 0x1244, 0x1300, 0x1544, 0x15A6, 0x1284,
  0x1906, 0x1D82, 0x1DC3, 0x1A43, 0x1988, 0x1DE7, 0x1D10, 0x1AE0, 0x198C, 0x1DEC,
  0x2945, 0x2986, 0x29E3, 0x2A63, 0x2B00, 0x2DE2, 0x2E04, 0x2D0C, 0x2E83, 0x2DD0,
  0x20C4, 0x2584, 0x21A6, 0x2700, 0x22A4, 0x0210, 0x1304, 0x1B44, 0x2F24, 0x2E6D,
};
#define L_PINS(l) ((l) & 31)
#define L_SHOTS(l) ((l) >> 5 & 31)
#define L_PAT(l) ((l) >> 10)

/* the patterns A1 A2 B1 B2 C1 C2 D1 D2 D3 D4 E1 E2: tenths of a degree a frame (and the other speed),
   after how many frames (more than) the timer acts, and how: turn back (C), swap speeds (D, E2), turn
   back on each shot (E), the other way round (A2, B2, D2, D4) */
enum { TURN = 1, SWAP = 2, TAP = 4, NEG = 8 };
static const uint8_t pats[12][4] = {
  {15, 15, 0, 0},    {15, 15, 0, NEG},    {25, 25, 0, 0},     {25, 25, 0, NEG},
  {22, 22, 180, TURN}, {35, 35, 120, TURN}, {20, 3, 72, SWAP},  {20, 3, 72, SWAP | NEG},
  {40, 5, 102, SWAP}, {40, 5, 102, SWAP | NEG}, {20, 20, 0, TAP}, {20, 3, 60, SWAP | TAP},
};

enum { ST_START, ST_IN, ST_RUN, ST_CLEAR, ST_MISS, ST_END };
#define MAXP 32
static struct {
  int16_t ang[MAXP], r[MAXP]; /* each pin's angle on the core (tenths) and distance from its centre */
  uint8_t num[MAXP], n;       /* the number it shows (0: none), pins */
  uint8_t level, shots, popped, landed, state, hitter, v;
  int8_t d;
  int32_t angle;                /* the core's, tenths of a degree */
  uint32_t t, t0, pt, pending;  /* frames played, at the last timer, in this state; shots waiting */
  uint32_t launch[26];          /* the frame each ball was shot */
} g;

/* coreball.sav: the level to play, the furthest reached, the guide read */
static struct {
  uint8_t magic, version, flags, level, best, pad[3];
  uint32_t sum;
} sv;
#define SAVE_NAME "coreball.sav"
#define SEEN 2

static color_t strip[SCREEN_W * STRIP_H];
static int ov, sel;
enum { OV_NONE, OV_MENU, OV_SURE, OV_HELP, OV_LEVELS };

static uint32_t checksum(void) {
  uint32_t h = 0x811C9DC5u;
  for (const uint8_t *p = &sv.magic; p < (const uint8_t *)&sv.sum; p++) h = (h ^ *p) * 0x01000193u;
  return h;
}
static void save(void) {
  sv.magic = 'C', sv.version = 1, sv.sum = checksum();
  np_file_write(SAVE_NAME, &sv, sizeof sv);
}
static void load(void) {
  uint32_t len = 0;
  const uint8_t *d = np_file_read(SAVE_NAME, &len);
  uint8_t *v = (uint8_t *)&sv;
  for (uint32_t i = 0; i < sizeof sv; i++) v[i] = d && len == sizeof sv ? d[i] : 0;
  if (sv.magic != 'C' || sv.version != 1 || sv.sum != checksum() || sv.level < 1 || sv.best > LAST + 1 ||
      sv.level > sv.best)
    for (uint32_t i = 0; i < sizeof sv; i++) v[i] = 0;
  if (!sv.level) sv.level = sv.best = 1;
}

/* ------------------------------------------------------------ the rules */
static void start(int level) { /* the black panel slides in, then the level */
  g.level = (uint8_t)level, g.state = ST_IN, g.pt = 0;
}
static void begin(void) {
  uint16_t l = levels[g.level - 1];
  const uint8_t *p = pats[L_PAT(l)];
  int n = L_PINS(l);
  g.n = (uint8_t)n, g.shots = (uint8_t)L_SHOTS(l), g.popped = g.landed = 0;
  for (int i = 0; i < n; i++) /* added last to first, as there */
    g.ang[i] = (int16_t)(((n - 1 - i) * 360 + n - 1) / n * 10), g.r[i] = 150, g.num[i] = 0;
  g.angle = 0, g.t = g.t0 = g.pending = 0, g.v = p[0], g.d = p[3] & NEG ? -1 : 1;
  g.state = ST_RUN;
}
static int off_down(int a) { /* how far (tenths) an angle on the core is from straight down, now */
  a = (a + g.angle - 900) % 3600;
  if (a < 0) a += 3600;
  return a > 1800 ? 3600 - a : a;
}
static void tick(void) {
  g.pt++;
  if (g.state == ST_IN) {
    if (g.pt >= 12) {
      if (g.level > LAST) g.state = ST_END;
      else begin();
    }
  } else if (g.state == ST_RUN) {
    const uint8_t *p = pats[L_PAT(levels[g.level - 1])];
    g.t++;
    for (; g.pending; g.pending--) { /* the shots since the last frame */
      if (p[3] & TAP) g.d = (int8_t)-g.d;
      if (g.popped < g.shots) g.launch[g.popped++] = g.t;
    }
    if (p[2] && g.t - g.t0 > p[2]) {
      g.t0 = g.t;
      if (p[3] & SWAP) g.v = g.v == p[0] ? p[1] : p[0];
      else g.d = (int8_t)-g.d;
    }
    g.angle = (g.angle + 3600 + g.v * g.d) % 3600;
    for (int i = 0; i < g.n; i++) g.r[i] = 150;
    while (g.landed < g.popped && g.t - g.launch[g.landed] >= 3) { /* a ball lands under the core */
      int k = g.n++;
      g.ang[k] = (int16_t)((900 - g.angle + 3600) % 3600), g.r[k] = 164, g.num[k] = (uint8_t)(g.shots - g.landed++);
      for (int i = 0; i < k; i++)
        if (off_down(g.ang[i]) <= HIT) {
          g.state = ST_MISS, g.hitter = (uint8_t)k, g.pt = 0;
          return;
        }
      if (g.popped == g.shots) { /* the last one in: cleared, even with a ball still flying */
        g.state = ST_CLEAR, g.pt = 0;
        sv.level = (uint8_t)(g.level + 1);
        if (sv.level > sv.best) sv.best = sv.level;
        save();
        return;
      }
    }
  } else if (g.state == ST_CLEAR || g.state == ST_MISS) {
    for (int i = 0; i < g.n; i++)
      if (g.state == ST_MISS) g.r[i] = 150;
      else if (g.pt <= SPAN) g.r[i] += 25; /* the pins fly off */
    if (g.pt > SPAN + 11) g.state = ST_START, g.level = sv.level;
  }
}

/* ------------------------------------------------------------ drawing */
#define WHITE 0xFFFF
#define GRAY HEX(0xC8C8C8)
#define INK HEX(0x666666)
static float sin10(int a) { /* the sine of a in tenths of a degree */
  a %= 3600;
  if (a < 0) a += 3600;
  float s = a >= 1800 ? -1 : 1;
  if (a >= 1800) a -= 1800;
  if (a > 900) a = 1800 - a;
  float x = a * (3.14159265f / 1800), x2 = x * x;
  return s * x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72))));
}
static void put(int x, int y, color_t c, int a32) {
  if ((unsigned)x < SCREEN_W && y >= gfx_y0 && y < gfx_y1 && a32 > 0) {
    color_t *p = gfx_buf + (y - gfx_y0) * SCREEN_W + x;
    *p = gfx_mix(*p, c, a32 > 32 ? 32 : a32);
  }
}
/* a white line, smoothed, only what crosses the strip */
static void line(float x0, float y0, float x1, float y1) {
  float dx = x1 - x0, dy = y1 - y0;
  bool steep = (dy < 0 ? -dy : dy) > (dx < 0 ? -dx : dx);
  float a0 = steep ? y0 : x0, b0 = steep ? x0 : y0, a1 = steep ? y1 : x1, b1 = steep ? x1 : y1;
  if (a1 < a0) {
    float t = a0;
    a0 = a1, a1 = t, t = b0, b0 = b1, b1 = t;
  }
  float k = a1 > a0 ? (b1 - b0) / (a1 - a0) : 0;
  float lo = a0, hi = a1;
  if (steep) lo = lo > gfx_y0 ? lo : gfx_y0, hi = hi < gfx_y1 - 1 ? hi : gfx_y1 - 1;
  else {
    lo = lo > 0 ? lo : 0, hi = hi < SCREEN_W - 1 ? hi : SCREEN_W - 1;
    if (k) { /* the columns where it is in the strip */
      float u = a0 + (gfx_y0 - 2 - b0) / k, w = a0 + (gfx_y1 + 1 - b0) / k;
      if (u > w) {
        float t = u;
        u = w, w = t;
      }
      lo = lo > u ? lo : u, hi = hi < w ? hi : w;
    } else if (b0 < gfx_y0 - 1 || b0 > gfx_y1 + 1) return;
  }
  for (int a = (int)__builtin_floorf(lo); a <= (int)hi; a++) {
    float b = b0 + (a + 0.5f - a0) * k - 0.5f;
    int bi = (int)__builtin_floorf(b), f = (int)((b - bi) * 32);
    put(steep ? bi : a, steep ? a : bi, WHITE, 32 - f);
    put(steep ? bi + 1 : a, steep ? a : bi + 1, WHITE, f);
  }
}

/* a ball: white, its number (1 to 26) in 3 x 5 digits */
static void ball(float x, float y, int r, int num) {
  static const uint16_t digits[10] = {0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF};
  int cx = (int)(x + 0.5f), cy = (int)(y + 0.5f);
  if (cy + r < gfx_y0 || cy - r >= gfx_y1) return;
  gfx_circle(cx, cy, r, WHITE, 256);
  if (!num) return;
  int x0 = num > 9 ? cx - 4 : cx - 2;
  for (int n = num > 9 ? 2 : 1, v = num; n--; v /= 10)
    for (int i = 0; i < 15; i++)
      if (digits[v % 10] >> (14 - i) & 1) put(x0 + 4 * n + i % 3, cy - 2 + i / 3, 0, 32);
}

static void keys(const char *ok, color_t c) {
  gfx_text(&np_font_small, 6, 14, ok, c, 256);
  gfx_text_right(&np_font_small, 314, 14, T("Back  menu"), c, 256);
}

static void play_scene(void) {
  bool clear = g.state == ST_CLEAR, miss = g.state == ST_MISS;
  gfx_fill(0, gfx_y0, SCREEN_W, gfx_y1 - gfx_y0, clear ? HEX(0x1CB01A) : miss ? HEX(0xB8111C) : 0);
  if (g.state == ST_END) {
    gfx_text_center(&np_font_body, 160, (int)CY, "to be continued...", HEX(0xFFFF00), 256);
  } else {
    for (int i = 0; i < g.n; i++) { /* each pin's line, then the pin */
      int a = g.ang[i] + g.angle;
      float c = sin10(a + 900), s = sin10(a), r = g.r[i] * S, x = 160 + c * r, y = CY + s * r;
      line(160, CY, x, y);
      int rad = 5;
      if (miss && i == g.hitter) /* the ball that hit swells and shrinks */
        rad = g.pt >= 13 ? 6 : g.pt >= 10 ? 9 : g.pt >= 7 ? 6 : g.pt >= 4 ? 11 : 5;
      ball(x, y, rad, g.num[i]);
    }
    gfx_circle(160, 86, 21, WHITE, 256);
    char s[4] = {0};
    s[0] = (char)('0' + g.level / 10), s[1] = (char)('0' + g.level % 10);
    gfx_text_center(&np_font_title, 160, (int)(CY + 25 * S + 0.5f), g.level > 9 ? s : s + 1, 0, 256);
    /* the queue follows the last ball shot; the balls in flight */
    int top = g.landed < g.popped ? 436 - 12 * (int)(g.t - g.launch[g.popped - 1]) - 6 : 400;
    for (int b = g.shots - 1; b >= g.landed; b--) {
      int y = b >= g.popped ? top + 36 * (b - g.popped) : 394 - 12 * (int)(g.t - g.launch[b]);
      ball(160, y * S, 5, g.shots - b);
    }
  }
  keys(T("OK  shoot"), HEX(0x9A9A9A));
}

static void start_scene(void) {
  gfx_fill(0, gfx_y0, SCREEN_W, gfx_y1 - gfx_y0, GRAY);
  gfx_text_center(&np_font_title, 160, 62, "Core Ball", 0, 256);
  gfx_rrect_outline(124, 86, 72, 72, 11, 2, INK, 256);
  for (int y = 99; y < 125; y++) { /* the play sign */
    int h = y < 112 ? y - 99 : 124 - y, w = h * 22 / 13;
    gfx_fill(151, y, w, 1, INK);
    put(151 + w, y, INK, (h * 22 % 13) * 32 / 13);
  }
  char s[12] = "level ", *e = s + 6;
  if (sv.level > 9) *e++ = (char)('0' + sv.level / 10);
  *e++ = (char)('0' + sv.level % 10);
  gfx_text_center(&np_font_small, 160, 146, s, INK, 256);
  keys(T("OK  play"), INK);
}

static void levels_scene(void) {
  gfx_fill(0, gfx_y0, SCREEN_W, gfx_y1 - gfx_y0, GRAY);
  gfx_text_center(&np_font_title, 160, 42, T("Levels"), 0, 256);
  for (int i = 0; i < LAST; i++) {
    int x = 12 + i % 10 * 30, y = 56 + i / 10 * 28;
    if (y + 24 <= gfx_y0 || y >= gfx_y1) continue;
    bool open = i < sv.best, on = i == sel;
    char s[3] = {(char)('0' + (i + 1) / 10), (char)('0' + (i + 1) % 10), 0};
    if (on) gfx_rrect(x - 2, y - 2, 30, 28, 7, 0, 256);
    else gfx_rrect_outline(x, y, 26, 24, 5, 1, open ? INK : HEX(0xB0B0B0), 256);
    gfx_text_center(&np_font_body, x + 13, y + 17, i < 9 ? s + 1 : s, on ? WHITE : open ? 0 : HEX(0xA0A0A0), 256);
  }
  keys(T("OK  play"), INK);
}

static const char *const items[] = {T("Resume"), T("Restart"), T("Levels"), T("How to play"), T("Quit"), T("No"),
                                    T("Yes"), T("Play")};
static const uint8_t ov_first[4] = {0, 0, 5, 7}, ov_count[4] = {0, 5, 2, 1};

static void scene(void *ctx) {
  (void)ctx;
  if (ov == OV_LEVELS) {
    levels_scene();
    return;
  }
  if (g.state == ST_START) start_scene();
  else if (g.state == ST_IN) { /* gray, and the black panel coming in from the right */
    int x = SCREEN_W - SLIDE * (int)g.pt;
    gfx_fill(0, gfx_y0, SCREEN_W, gfx_y1 - gfx_y0, GRAY);
    gfx_fill(x > 0 ? x : 0, gfx_y0, SCREEN_W, gfx_y1 - gfx_y0, 0);
  } else {
    play_scene();
    if (g.pt >= SPAN && (g.state == ST_CLEAR || g.state == ST_MISS)) /* the gray panel from the left */
      gfx_fill(0, gfx_y0, SLIDE * (int)(g.pt - SPAN + 1), gfx_y1 - gfx_y0, GRAY);
  }
  if (!ov) return;
  int h = ov == OV_MENU ? 200 : ov == OV_SURE ? 120 : 170, top = 120 - h / 2, n = ov_count[ov];
  gfx_fill_alpha(0, 0, SCREEN_W, SCREEN_H, 0, 110);
  gfx_rrect(50, top, 220, h, 12, WHITE, 256);
  gfx_rrect_outline(50, top, 220, h, 12, 2, INK, 256);
  static const char *const titles[4] = {0, T("Menu"), T("Quit Core Ball?"), T("How to play")};
  gfx_text_center(&np_font_title, 160, top + 30, titles[ov], 0, 256);
  if (ov == OV_HELP)
    gfx_paragraph(&np_font_body, 160, top + 54, 196, 15,
                  T("Press OK to shoot a ball at the spinning core. Don't hit the balls already on it. Shoot them all to clear the level."),
                  HEX(0x333333), 256);
  for (int i = 0; i < n; i++) {
    int y = top + h - 36 - 30 * (n - 1 - i);
    gfx_rrect(90, y, 140, 24, 12, sel == i ? 0 : HEX(0xE4E4E4), 256);
    gfx_text_center(&np_font_body, 160, y + 17, items[ov_first[ov] + i], sel == i ? WHITE : HEX(0x333333), 256);
  }
}

/* ------------------------------------------------------------ the game */
/* the keys pressed this frame; true to quit */
static bool press(uint64_t hit) {
  bool ok = hit & (KEY(eadk_key_ok) | KEY(eadk_key_exe)), back = hit & KEY(eadk_key_back);
  int dx = !!(hit & KEY(eadk_key_right)) - !!(hit & KEY(eadk_key_left));
  int dy = !!(hit & KEY(eadk_key_down)) - !!(hit & KEY(eadk_key_up));
  if (ov == OV_LEVELS) {
    int s = sel + dx + 10 * dy;
    if (s >= 0 && s < sv.best && s < LAST) sel = s;
    if (back) ov = OV_MENU, sel = 2;
    else if (ok) ov = OV_NONE, sv.level = (uint8_t)(sel + 1), save(), start(sel + 1), sel = 0;
  } else if (ov) {
    sel = (sel + dy + ov_count[ov]) % ov_count[ov];
    if (back) ov = ov == OV_SURE ? OV_MENU : OV_NONE, sel = 4;
    else if (ok) {
      int was = ov, choice = sel;
      ov = OV_NONE, sel = 0;
      if (was == OV_MENU) { /* Resume, Restart, Levels, How to play, Quit (asks first) */
        if (choice == 1) start(g.level);
        else if (choice == 2) ov = OV_LEVELS, sel = (sv.level < LAST ? sv.level : LAST) - 1;
        else if (choice == 3) ov = OV_HELP;
        else if (choice == 4) ov = OV_SURE;
      } else if (was == OV_SURE) { /* No, Yes */
        if (choice) return true;
        ov = OV_MENU, sel = 4;
      }
    }
  } else if (back) {
    ov = OV_MENU, sel = 0;
    save();
  } else if (ok) {
    if (g.state == ST_START) start(g.level);
    else if (g.state == ST_RUN) g.pending++;
  }
  return false;
}

int main(void) {
  np_app_begin();
  color_t *their_strip = gfx_buf;
  gfx_buf = strip;
  load();
  g.level = sv.level;
  if (!(sv.flags & SEEN)) ov = OV_HELP, sv.flags |= SEEN;
  uint64_t held = eadk_keyboard_scan(), stale = held; /* keys still down from the launcher wait to be let go */
  uint32_t last = (uint32_t)eadk_timing_millis(), acc = 0;
  for (;;) {
    uint32_t now = (uint32_t)eadk_timing_millis();
    uint64_t raw = eadk_keyboard_scan(), k = raw & ~(stale &= raw), hit = k & ~held;
    held = k;
    if (raw & (KEY(eadk_key_home) | KEY(eadk_key_on_off)) || press(hit)) break;
    /* sixty frames a second, as the original, catching up after a slow one; still under a menu */
    uint32_t el = now - last;
    last = now;
    acc += ov ? 0 : (el > 100 ? 100 : el) * 60;
    for (; acc >= 1000; acc -= 1000) tick();
    eadk_display_wait_for_vblank();
    gfx_render(scene, 0, 0, SCREEN_H);
    uint32_t spent = (uint32_t)eadk_timing_millis() - now;
    if (spent < 16) eadk_timing_msleep(16 - spent);
  }
  save();
  gfx_buf = their_strip;
  return np_app_end();
}
