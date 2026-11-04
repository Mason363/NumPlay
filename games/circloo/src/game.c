/* circloO: the screens around the levels (a row of circles to pick from, as the game's obj_menu draws them),
 * the unlocking of levels and the saves. */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "gfx.h"
#include "phys.h"
#include "plat.h"

Game g;
Save save;

/* ---- saves: "COOS", a version, then Save ---- */

#define SAVE_NAME "circloo.sav"
#define SAVE_MAGIC 0x534F4F43u
#define SAVE_VERSION 1

typedef struct {
  uint32_t magic;
  uint16_t version, size;
  Save s;
} SaveFile;
_Static_assert(sizeof(SaveFile) == 112 && offsetof(SaveFile, s.done) == 12 && offsetof(SaveFile, s.best) == 44,
               "the save's layout (tests/check.py and NumPlay's tests/test_games.py read it)");

static void save_defaults(void) {
  memset(&save, 0, sizeof save);
  save.unlocked[0] = save.unlocked[1] = 2;
}

/* a copy in a Python script, "#>circloo.sav:base64": installing apps from the NumWorks website keeps only Python
 * scripts, so the save comes back from it when the file itself is gone (as NumPlay's big games keep theirs) */
#define COPY_NAME "circloo_saves.py"
static const char copy_head[] = "# circloO's saves: keep this file\n";
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
#define COPY_BYTES (1 + sizeof copy_head - 1 + 2 + sizeof SAVE_NAME - 1 + 1 + (sizeof(SaveFile) + 2) / 3 * 4 + 2)

static bool copy_write(const SaveFile *f) {
  uint8_t o[COPY_BYTES], *p = o;
  const uint8_t *d = (const uint8_t *)f;
  uint32_t len = sizeof *f;
  *p++ = 0;   /* (the script's status byte: not imported) */
  memcpy(p, copy_head, sizeof copy_head - 1), p += sizeof copy_head - 1;
  *p++ = '#', *p++ = '>';
  memcpy(p, SAVE_NAME, sizeof SAVE_NAME - 1), p += sizeof SAVE_NAME - 1;
  *p++ = ':';
  for (uint32_t i = 0; i < len; i += 3, p += 4) {
    /* (written out: GCC 15.2 for the calculator got a loop over the four wrong, see launcher/src/storage.c) */
    uint32_t left = len - i, v = (uint32_t)d[i] << 16 | (left > 1 ? d[i + 1] << 8 : 0) | (left > 2 ? d[i + 2] : 0);
    p[0] = (uint8_t)b64[v >> 18 & 63];
    p[1] = (uint8_t)b64[v >> 12 & 63];
    p[2] = left > 1 ? (uint8_t)b64[v >> 6 & 63] : '=';
    p[3] = left > 2 ? (uint8_t)b64[v & 63] : '=';
  }
  *p++ = '\n';
  *p++ = 0;
  return plat_save(COPY_NAME, o, (uint32_t)(p - o));
}

static int b64_value(uint8_t c) {
  for (int i = 0; i < 64; i++)
    if ((uint8_t)b64[i] == c) return i;
  return -1;
}

static bool copy_read(SaveFile *f) {
  uint32_t n, nl = sizeof SAVE_NAME - 1;
  const uint8_t *c = plat_load(COPY_NAME, &n), *l = NULL;
  if (!c) return false;
  for (uint32_t i = 0; i + nl + 3 <= n && !l; i++)
    if (c[i] == '#' && c[i + 1] == '>' && !memcmp(c + i + 2, SAVE_NAME, nl) && c[i + 2 + nl] == ':') l = c + i + 3 + nl;
  if (!l) return false;
  uint32_t len = 0;
  while (l + len < c + n && l[len] != '\n' && l[len]) len++;
  if (len != (sizeof *f + 2) / 3 * 4) return false;
  uint8_t *out = (uint8_t *)f;
  uint32_t o = 0;
  for (uint32_t i = 0; i < len; i += 4) {
    int v0 = b64_value(l[i]), v1 = b64_value(l[i + 1]);
    int v2 = l[i + 2] == '=' ? 0 : b64_value(l[i + 2]), v3 = l[i + 3] == '=' ? 0 : b64_value(l[i + 3]);
    if (v0 < 0 || v1 < 0 || v2 < 0 || v3 < 0) return false;
    uint32_t w = (uint32_t)(v0 << 18 | v1 << 12 | v2 << 6 | v3);
    if (o < sizeof *f) out[o++] = (uint8_t)(w >> 16);
    if (o < sizeof *f) out[o++] = (uint8_t)(w >> 8);
    if (o < sizeof *f) out[o++] = (uint8_t)w;
  }
  return true;
}

void save_read(void) {
  save_defaults();
  uint32_t len;
  const uint8_t *d = plat_load(SAVE_NAME, &len);
  SaveFile f;
  if (d && len >= sizeof f) memcpy(&f, d, sizeof f);
  else if (d || !copy_read(&f)) return;
  if (f.magic != SAVE_MAGIC || f.version != SAVE_VERSION || f.size != sizeof(Save)) return;
  for (int m = 0; m < 2; m++) {
    if (f.s.unlocked[m] < 2 || f.s.unlocked[m] > 40 || f.s.bonus[m] > 5) return;
    for (int i = 0; i < 16; i++)
      if (f.s.done[m][i] > 1) return;
  }
  save = f.s;
  save.hard = save.hard && save.unlocked[0] > 10;
}

bool save_write(void) {
  SaveFile f = {SAVE_MAGIC, SAVE_VERSION, sizeof(Save), save};
  bool ok = plat_save(SAVE_NAME, &f, sizeof f);
  return copy_write(&f) && ok;
}

/* ---- the menus ---- */

enum { IT_MODE, IT_LEVEL, IT_HELP, IT_CREDITS, IT_CONTINUE, IT_RETRY, IT_MAIN, IT_NEXT };
#define SLIDE 8

static int levels_in(int m) { return m ? NHARD : NBASE; }
static int playable(int m) {
  int n = save.unlocked[m];
  return n < levels_in(m) ? n : levels_in(m);
}

/* the level the main menu shows first: the first one not done yet */
static int first_pick(int m) {
  for (int i = 1; i <= playable(m); i++)
    if (!save.done[m][i]) return i;
  return playable(m);
}

static void menu(int state, int sel) {
  g.state = state;
  g.nitems = 0;
  if (state == S_MENU) {
    if (save.unlocked[0] > 10) g.items[g.nitems++] = IT_MODE;
    g.items[g.nitems++] = IT_LEVEL;
    g.items[g.nitems++] = IT_HELP;
    g.items[g.nitems++] = IT_CREDITS;
  } else if (state == S_PAUSE) {
    g.items[g.nitems++] = IT_CONTINUE, g.items[g.nitems++] = IT_RETRY, g.items[g.nitems++] = IT_MAIN;
  } else if (state == S_DONE) {
    g.items[g.nitems++] = IT_NEXT, g.items[g.nitems++] = IT_RETRY, g.items[g.nitems++] = IT_MAIN;
  }
  g.sel = g.from = sel;
  g.anim = 0;
}

static int sel_of(int item) {
  for (int i = 0; i < g.nitems; i++)
    if (g.items[i] == item) return i;
  return 0;
}

static void start(int level, bool hard) {
  save.hard = hard;
  level_load(level, hard);
  g.state = S_PLAY;
  g.zoom = 0;
}

static void main_menu(void) {
  menu(S_MENU, 0);
  g.sel = g.from = sel_of(IT_LEVEL);
  colours_for((float)g.pick[save.hard]);
}

static void complete(void) {
  int m = L.hard, d = L.level;
  bool before = save.unlocked[0] > 10;
  if (!save.done[m][d]) save.done[m][d] = 1, save.unlocked[m]++;
  if (save.bonus[m] < d / 5) save.bonus[m]++, save.unlocked[m]++;
  g.last_time = L.frames;
  g.new_best = !save.best[m][d] || L.frames < save.best[m][d];
  if (g.new_best) save.best[m][d] = (uint16_t)(L.frames > 65535 ? 65535 : L.frames);
  g.msg = !before && save.unlocked[0] > 10;
  g.msg_t = 0;
  save_write();
  menu(S_DONE, 0);
}

void game_init(void) {
  save_read();
  g.running = true;
  g.pick[0] = first_pick(0), g.pick[1] = first_pick(1);
  main_menu();
#ifdef CO_START /* (speed tests: make EXTRA=-DCO_START=9, or 106 for hard level 6) */
  start(CO_START % 100, CO_START > 100);
#ifdef CO_RINGS /* (and every ring open: make EXTRA="-DCO_START=106 -DCO_RINGS=6") */
  void level_debug_grow(void);
  for (int r = 0; r < CO_RINGS; r++) level_debug_grow();
#endif
#endif
}

static void act(int item) {
  int m = save.hard;
  switch (item) {
  case IT_MODE:
    save.hard = !save.hard;
    colours_for((float)g.pick[save.hard]);
    break;
  case IT_LEVEL: g.zoom = 1; break;
  case IT_HELP: g.state = S_HELP; break;
  case IT_CREDITS: g.state = S_CREDITS; break;
  case IT_CONTINUE: g.state = S_PLAY; break;
  case IT_RETRY: start(L.level, L.hard); break;
  case IT_MAIN:
    save_write();
    g.pick[L.hard] = L.level;
    main_menu();
    break;
  case IT_NEXT:
    m = L.hard;
    if (L.level < levels_in(m)) {
      g.pick[m] = L.level + 1;
      start(L.level + 1, m);
    } else if (!m) {
      g.pick[1] = first_pick(1);
      start(g.pick[1], true);   /* "Hard Mode" */
    } else {
      g.pick[1] = L.level;
      save.hard = 1;
      main_menu();
      g.state = S_CREDITS;
    }
    break;
  }
}

void game_tick(uint32_t keys) {
  uint32_t press = keys & ~g.prev;
  g.prev = keys;
  if (g.msg) g.msg_t++;
  if (g.anim > 0) g.anim--;
  switch (g.state) {
  case S_PLAY:
    if (press & K_BACK) {
      menu(S_PAUSE, 0);
      return;
    }
    level_tick(keys);
    if (L.complete) complete();
    return;
  case S_CREDITS:
  case S_HELP: {
    int item = g.state == S_HELP ? IT_HELP : IT_CREDITS;
    if (press & (K_BACK | K_OK)) main_menu(), g.sel = g.from = sel_of(item);
    return;
  }
  case S_DONE:
    level_tick(0);   /* (the level goes on moving behind) */
    break;
  default: break;
  }
  if (g.zoom) {
    if (++g.zoom > 12) start(g.pick[save.hard], save.hard);
    return;
  }
  int item = g.items[g.sel];
  if (press & (K_LEFT | K_RIGHT)) {
    int to = g.sel + (press & K_RIGHT ? 1 : -1);
    if (to >= 0 && to < g.nitems) g.from = g.sel, g.sel = to, g.anim = SLIDE;
  } else if (item == IT_LEVEL && press & (K_UP | K_DOWN)) {
    int m = save.hard, p = g.pick[m] + (press & K_UP ? 1 : -1);
    if (p >= 1 && p <= playable(m)) g.pick[m] = p, colours_for((float)p);
  } else if (press & K_OK) {
    act(item);
  } else if (press & K_BACK) {
    if (g.state == S_PAUSE) g.state = S_PLAY;
    else if (g.state == S_DONE) act(IT_MAIN);
    else g.running = false;
  }
}

/* ---- drawing ---- */

static const char *label(int item, char *buf) {
  switch (item) {
  case IT_MODE: return save.hard ? "Hard mode" : "Normal#mode";
  case IT_LEVEL: snprintf(buf, 8, "%d", g.pick[save.hard]); return buf;
  case IT_HELP: return "How to play";
  case IT_CREDITS: return "Credits";
  case IT_CONTINUE: return "Continue";
  case IT_RETRY: return "Retry level";
  case IT_MAIN: return "Main menu";
  default: return L.level < levels_in(L.hard) ? "Next level" : L.hard ? "Credits" : "Hard Mode";
  }
}

static void time_text(char *buf, int frames) {
  int cs = frames * 100 / 60;
  snprintf(buf, 24, "%d.%02d s", cs / 100, cs % 100);
}

static const char *title(int item) {
  if (g.state == S_PAUSE) return "Game paused";
  if (g.state == S_DONE) return "Level complete!";
  return item == IT_MODE ? "Choose a game mode" : item == IT_LEVEL ? "Choose a level to play" : "circloO";
}

static const char *hint(int item) {
  if (item == IT_LEVEL)
    return "Use the up and down arrow keys to choose a level, or the left and right arrow keys to select another option. "
           "OK to confirm.";
  if (item == IT_MODE) return "Use the left and right arrow keys to select an option. Press OK to change game mode.";
  return "Use the left and right arrow keys to select an option. Press OK to confirm.";
}

/* the widest word of a label */
static int word_w(int font, const char *s) {
  int w = 0;
  while (*s) {
    int n = 0;
    while (s[n] && s[n] != ' ' && s[n] != '#') n++;
    int ww = gfx_text_w(font, s, n);
    if (ww > w) w = ww;
    s += n;
    if (*s) s++;
  }
  return w;
}

static void draw_menu(void) {
  uint16_t A = g.hue_a, B = g.hue_b;
  if (g.state == S_MENU) gfx_fill(A);
  else gfx_rect(0, 0, SCREEN_W, SCREEN_H, A, 205);
  float t = g.anim ? (float)g.anim / SLIDE : 0;
  t = t * t * (3 - 2 * t);
  float pos = g.sel + (g.from - g.sel) * t;
  char buf[32];
  for (int i = 0; i < g.nitems; i++) {
    float d = fabsf(i - pos), id = d < 1 ? 1 - d : 0;
    float x = SCREEN_W / 2 + (i - pos) * 150, y = 108, r = 17 + 51 * id;
    if (x + r < 0 || x - r > SCREEN_W) continue;
    gfx_disc(x, y, r, B, 256);
    int font = id > 0.7f ? F_BIG : id > 0.3f ? F_MEDIUM : F_SMALL;
    const char *s = label(g.items[i], buf);
    while (font > F_SMALL && word_w(font, s) > r * 1.75f) font--;
    int w = (int)(r * 1.8f), n = gfx_wrap(font, 0, 0, s, w, 0, 0, AL_CENTER, 0), h = gfx_font_h(font);
    char sub[48] = "";
    if (id > 0.7f) {
      int item = g.items[i], m = save.hard;
      if (item == IT_LEVEL && save.done[m][g.pick[m]]) {
        char tb[24];
        time_text(tb, save.best[m][g.pick[m]]);
        snprintf(sub, sizeof sub, save.best[m][g.pick[m]] ? "Level complete#Best %s" : "Level complete", tb);
      } else if (item == IT_CONTINUE) {
        snprintf(sub, sizeof sub, "%d/7 circles collected", L.taken);
      }
    }
    int sn = sub[0] ? gfx_wrap(F_SMALL, 0, 0, sub, (int)(r * 1.6f), 0, 0, AL_CENTER, 0) : 0;
    float top = y - (n * h + sn * gfx_font_h(F_SMALL)) / 2.0f - 2;
    gfx_wrap(font, x, top, s, w, A, 256, AL_CENTER, 1);
    if (sn) gfx_wrap(F_SMALL, x, top + n * h, sub, (int)(r * 1.6f), A, 256, AL_CENTER, 1);
    if (g.items[i] == IT_LEVEL && id > 0.7f) {   /* (up and down change it) */
      int m = save.hard;
      float ax = x, ay = y - r + 7;
      if (g.pick[m] < playable(m)) {
        float px[3] = {ax - 5, ax + 5, ax}, py[3] = {ay + 3, ay + 3, ay - 3};
        gfx_poly(px, py, 3, A, 256);
      }
      ay = y + r - 7;
      if (g.pick[m] > 1) {
        float px[3] = {ax - 5, ax + 5, ax}, py[3] = {ay - 3, ay - 3, ay + 3};
        gfx_poly(px, py, 3, A, 256);
      }
    }
  }
  int item = g.items[g.sel];
  gfx_text(F_MEDIUM, SCREEN_W / 2, 3, title(item), 99, B, 256, AL_CENTER);
  if (g.state == S_DONE) {
    char tb[24], line[64];
    time_text(tb, g.last_time);
    if (g.new_best) snprintf(line, sizeof line, "%s, a new best", tb);
    else {
      char bb[24];
      time_text(bb, save.best[L.hard][L.level]);
      snprintf(line, sizeof line, "%s (best %s)", tb, bb);
    }
    gfx_text(F_SMALL, SCREEN_W / 2, 24, line, 99, B, 256, AL_CENTER);
    if (g.msg && g.msg_t < 60 * 6)
      gfx_wrap(F_SMALL, SCREEN_W / 2, 40, "You unlocked hard mode! Activate it from the main menu.", 300, B, 256, AL_CENTER, 1);
  }
  const char *h = hint(item);
  int n = gfx_wrap(F_SMALL, 0, 0, h, SCREEN_W - 12, 0, 0, AL_CENTER, 0);
  gfx_wrap(F_SMALL, SCREEN_W / 2, SCREEN_H - 3 - n * gfx_font_h(F_SMALL), h, SCREEN_W - 12, mix(B, 0xFFFF, 64), 256, AL_CENTER, 1);
  if (g.zoom) gfx_disc(SCREEN_W / 2, SCREEN_H / 2, 80 + g.zoom * 14, B, 256);
}

static void draw_help(void) {
  uint16_t A = g.hue_a, B = g.hue_b;
  gfx_fill(A);
  gfx_disc(SCREEN_W / 2, -150, 210, B, 256);
  gfx_text(F_LARGE, SCREEN_W / 2, 12, "How to play", 99, A, 256, AL_CENTER);
  gfx_disc(42, 88, 13, B, 256), gfx_disc(42, 88, 10, mix(A, 0, 51), 256);
  gfx_disc(42, 128, 6.4f, B, 256), gfx_disc(42, 128, 5.1f, A, 256);
  gfx_disc(42, 168, 6.4f, B, 256), gfx_disc(42, 168, 5.1f, A, 256), gfx_disc(42, 168, 3.2f, B, 256);
  gfx_wrap(F_SMALL, 64, 80, "Left and right roll the ball.", 240, B, 256, AL_LEFT, 1);
  gfx_wrap(F_SMALL, 64, 113, "Collect seven circles to complete a level. Each one makes it grow.", 240, B, 256, AL_LEFT, 1);
  gfx_wrap(F_SMALL, 64, 153, "A circle with a dot is taken by another ball, not yours.", 240, B, 256, AL_LEFT, 1);
  gfx_wrap(F_SMALL, SCREEN_W / 2, 200, "Back pauses. Home saves and quits.", 300, B, 256, AL_CENTER, 1);
  gfx_wrap(F_SMALL, SCREEN_W / 2, SCREEN_H - 18, "OK or Back to go back", 300, mix(B, 0xFFFF, 64), 256, AL_CENTER, 1);
}

static void draw_credits(void) {
  uint16_t A = g.hue_a, B = g.hue_b;
  gfx_fill(A);
  gfx_disc(SCREEN_W / 2, 112, 92, B, 256);
  gfx_text(F_BIG, SCREEN_W / 2, 40, "circloO", 99, A, 256, AL_CENTER);
  gfx_wrap(F_SMALL, SCREEN_W / 2, 86,
           "A game by Florian van Strien#"
           "#This version by Mason Chen#as part of NumPlay.#Not affiliated with#Florian van Strien.",
           170, A, 256, AL_CENTER, 1);
  gfx_wrap(F_SMALL, SCREEN_W / 2, SCREEN_H - 18, "OK or Back to go back", 300, mix(B, 0xFFFF, 64), 256, AL_CENTER, 1);
}

void game_draw(void) {
  if (g.state != S_MENU && g.state != S_CREDITS && g.state != S_HELP && L.ball >= 0) {
    g.camx = W.b[L.ball].x / PX_M, g.camy = W.b[L.ball].y / PX_M;
  }
  for (int y = 0; y < SCREEN_H; y += BAND) {
    gfx_band(y);
    if (g.state == S_CREDITS) draw_credits();
    else if (g.state == S_HELP) draw_help();
    else {
      if (g.state != S_MENU) level_draw();
      if (g.state != S_PLAY) draw_menu();
    }
    gfx_push();
  }
}
