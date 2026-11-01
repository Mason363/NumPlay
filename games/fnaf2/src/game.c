/* The scenes around the nights: the title, the newspaper, how to play, the night's card, 6 AM, the paychecks
 * and the pink slip, the game over and the custom night; keys and saves. */
#include <string.h>
#include "game.h"
#include "gfx.h"
#include "res.h"

Game g;
static uint32_t seed = 0x2C1B3C6Du;

uint32_t rnd(void) {
  seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5;
  return seed;
}

static bool pressed(uint32_t k) { return (g.pressed & k) != 0; }

void caption(const char *s) {
  for (int i = 0; i < 4; i++)
    if (g.cap[i] == s && g.cap_t[i] > 1.2f) {   /* (the same again: it stays up) */
      g.cap_t[i] = 3;
      return;
    }
  memmove(g.cap, g.cap + 1, 3 * sizeof g.cap[0]);
  memmove(g.cap_t, g.cap_t + 1, 3 * sizeof g.cap_t[0]);
  g.cap[3] = s;
  g.cap_t[3] = 3;
}

static void go(int scene) {
  g.scene = scene;
  g.t = 0;
  g.menu = 0;
}

void start_night(int night) {
  g.n.night = (uint8_t)night;
  memset(g.cap, 0, sizeof g.cap);
  go(SC_INTRO);
}

void game_init(void) {
  seed ^= plat_millis() * 2654435761u;
  if (!seed) seed = 0x2C1B3C6Du;
  g.running = true;
  g.held = 0xFFFFFFFFu;   /* (the key that opened the app is not a press) */
  save_restore_copy();
  save_load();
  if (save_resume_load()) {   /* Home left a night: back in it, paused */
    go(SC_NIGHT);
    g.paused = 1;
  } else
    go(SC_TITLE);
}

/* ------------------------------------------------------------------ the title */
enum { M_NEW, M_CONTINUE, M_SIXTH, M_CUSTOM };

static int title_light = 24;
static float light_t;

static void title_step(void) {
  light_t += DT;
  if (light_t >= 0.3f) light_t = 0, title_light = 22 + (int)(rnd() % 14);
  g.menu_n = (uint8_t)(2 + (g.stars & 1) + ((g.stars >> 1) & 1));
  if (g.menu >= g.menu_n) g.menu = 0;
  if (pressed(K_UP | K_DIGIT(8))) g.menu = (uint8_t)((g.menu + g.menu_n - 1) % g.menu_n);
  if (pressed(K_DOWN | K_DIGIT(2))) g.menu = (uint8_t)((g.menu + 1) % g.menu_n);
  if (pressed(K_BACK)) g.running = false;
  if (!pressed(K_OK)) return;
  switch (g.menu) {
  case M_NEW:
    g.reached = 1;
    save_progress();
    g.n.night = 1;
    go(SC_NEWS);
    break;
  case M_CONTINUE: start_night(g.reached); break;
  case M_SIXTH: start_night(6); break;
  case M_CUSTOM: go(SC_CUSTOM); break;
  }
}

static void title_draw(void) {
  pic_draw(PIC_TITLE, 0, 0);
  gfx_dim(0, gfx_y0, SCREEN_W, BAND_H, title_light);
  gfx_static(0, gfx_y0, SCREEN_W, BAND_H, 7, rnd());
  int sy = (int)(g.t / 20 * (SCREEN_H + 10)) % (SCREEN_H + 10) - 10;
  gfx_blend(0, sy, SCREEN_W, 10, WHITE, 3);
  gfx_img(IMG_TITLE, 20, 16, WHITE);
  for (int i = 0; i < 4; i++)
    if (g.stars >> i & 1) gfx_img(IMG_STAR, 20 + 27 * i, 128, WHITE);
  static const char *const items[] = {"New Game", "Continue", "6th Night", "Custom Night"};
  int y0 = g.stars ? 158 : 148;
  for (int i = 0; i < g.menu_n; i++) {
    int y = y0 + 18 * i;
    gfx_text(FONT_BIG, 44, y, items[i], WHITE);
    if (g.menu == i) gfx_text(FONT_BIG, 20, y, ">>", WHITE);
  }
  if (g.menu == M_CONTINUE) {
    char s[] = "Night 0";
    s[6] = (char)('0' + g.reached);
    gfx_text(FONT_HUD, 44, y0 + 18 * g.menu_n + 4, s, WHITE);
  }
}

/* ------------------------------------------------------------------ the newspaper, how to play, the card */
static int fade_in_out(float t, float in, float hold, float out) {   /* 0 black to 32 */
  float a = t < in ? t / in : t < hold ? 1 : t < hold + out ? 1 - (t - hold) / out : 0;
  return (int)(a * 32);
}

static void news_draw(void) {
  pic_draw(PIC_NEWSPAPER, 0, 0);
  gfx_dim(0, gfx_y0, SCREEN_W, BAND_H, fade_in_out(g.t, 1.5f, 8, 1.5f));
}

static void news_step(void) {
  if (pressed(K_OK) && g.t > 1.5f && g.t < 8) g.t = 8;
  if (g.t >= 9.5f) {
    if (!g.keys_seen) {
      go(SC_KEYS);
      g.after_keys = 0;
    } else
      start_night(1);
  }
}

static void keys_draw(void) {
  static const char *const rows[][2] = {
    {"In the office", NULL},
    {"4 6, < >", "look left and right"},
    {"5, up", "the flashlight, down the hall"},
    {"7 1   9 3", "the left and right vent lights"},
    {"2, down", "the Freddy mask on and off"},
    {"OK", "the monitor up and down"},
    {"On the monitor", NULL},
    {"0 to 9, < >", "a camera (1 again: 11, 12)"},
    {"up", "the light; on CAM 11: wind"},
    {"Back", "pause"},
  };
  gfx_fill(0, gfx_y0, SCREEN_W, BAND_H, BLACK);
  gfx_text_c(FONT_BIG, 160, 6, "How to survive", WHITE);
  int y = 30;
  for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; i++) {
    if (!rows[i][1]) {
      gfx_text(FONT_HUD, 12, y + 2, rows[i][0], RGB(0x8FD18F));
      y += 17;
      continue;
    }
    gfx_text(FONT_HUD, 20, y, rows[i][0], WHITE);
    gfx_text(FONT_TEXT, 112, y, rows[i][1], RGB(0xC8C8C8));
    y += 15;
  }
  gfx_text_c(FONT_TEXT, 160, 190, "Keep the music box wound. Mask whoever gets in,", RGB(0xC8C8C8));
  gfx_text_c(FONT_TEXT, 160, 203, "flash Foxy in the hall. Last until 6 AM.", RGB(0xC8C8C8));
  if ((int)(g.t * 2) & 1) gfx_text_c(FONT_HUD, 160, 224, "OK", WHITE);
}

static void keys_step(void) {
  if (!pressed(K_OK | K_BACK) || g.t < 0.4f) return;
  if (!g.keys_seen) {
    g.keys_seen = 1;
    save_progress();
  }
  if (g.after_keys) {
    g.scene = SC_NIGHT;
    g.paused = 1;
  } else
    start_night(g.n.night ? g.n.night : 1);
}

static const char *const nth[] = {"", "1st Night", "2nd Night", "3rd Night", "4th Night", "5th Night", "6th Night",
                                  "7th Night"};
/* a word for each night, in place of the calls (they run a minute or two each) */
static const char *const hint[] = {"",
                                   "Wind the music box on CAM 11. Mask whoever gets in.",
                                   "Light the vents. The flashlight's battery won't come back.",
                                   "The old ones wake up. The mask won't fool Foxy: flash him.",
                                   "Balloon Boy takes your lights if he gets in.",
                                   "Everyone is restless tonight.",
                                   "One more night.",
                                   "Custom Night."};

static void intro_draw(void) {
  gfx_fill(0, gfx_y0, SCREEN_W, BAND_H, BLACK);
  gfx_text_c(FONT_HUGE, 160, 84, "12:00 AM", WHITE);
  gfx_text_c(FONT_BIG, 160, 116, nth[g.n.night], WHITE);
  /* (a word too wide for the screen: a line a sentence) */
  const char *h = hint[g.n.night], *stop = strstr(h, ". ");
  if (gfx_text_w(FONT_TEXT, h) <= 300 || !stop) {
    gfx_text_c(FONT_TEXT, 160, 150, h, RGB(0x9A9A9A));
  } else {
    char first[64];
    int n = (int)(stop - h) + 1;
    memcpy(first, h, (size_t)n), first[n] = 0;
    gfx_text_c(FONT_TEXT, 160, 146, first, RGB(0x9A9A9A));
    gfx_text_c(FONT_TEXT, 160, 162, stop + 2, RGB(0x9A9A9A));
  }
  if (g.t < 0.15f) gfx_static(0, gfx_y0, SCREEN_W, BAND_H, 20, rnd());
  if (g.t > 2.2f) gfx_dim(0, gfx_y0, SCREEN_W, BAND_H, (int)((3.2f - g.t) * 32));
}

static void intro_step(void) {
  if (pressed(K_OK) && g.t > 0.5f && g.t < 2.2f) g.t = 2.2f;
  if (g.t < 3.2f) return;
  night_begin(g.n.night);
  go(SC_NIGHT);
}

/* ------------------------------------------------------------------ the night, paused */
static void night_scene_step(void) {
  if (g.paused) {
    if (g.confirm) {
      if (pressed(K_LEFT | K_RIGHT | K_UP | K_DOWN | K_DIGIT(4) | K_DIGIT(6))) g.pause_item ^= 1;
      if (pressed(K_BACK)) g.confirm = 0, g.pause_item = 2;
      if (pressed(K_OK)) {
        if (g.pause_item) go(SC_TITLE), g.paused = 0;
        g.confirm = 0, g.pause_item = 2;
      }
      return;
    }
    if (pressed(K_UP | K_DIGIT(8))) g.pause_item = (uint8_t)((g.pause_item + 2) % 3);
    if (pressed(K_DOWN | K_DIGIT(2))) g.pause_item = (uint8_t)((g.pause_item + 1) % 3);
    if (pressed(K_BACK)) g.paused = 0;
    if (pressed(K_OK)) {
      if (g.pause_item == 0) g.paused = 0;
      else if (g.pause_item == 1) go(SC_KEYS), g.after_keys = 1;
      else g.confirm = 1, g.pause_item = 0;
    }
    return;
  }
  if (pressed(K_BACK)) {
    g.paused = 1;
    g.pause_item = 0;
    return;
  }
  night_step();
}

static void pause_draw(void) {
  view_night();
  gfx_dim(0, gfx_y0, SCREEN_W, BAND_H, 9);
  if (g.confirm) {
    gfx_text_c(FONT_BIG, 160, 84, "Quit this night?", WHITE);
    gfx_text_c(FONT_TEXT, 160, 106, "It starts over next time.", RGB(0xB0B0B0));
    gfx_text(FONT_BIG, 104, 132, "No", WHITE);
    gfx_text(FONT_BIG, 190, 132, "Yes", WHITE);
    gfx_text(FONT_BIG, g.pause_item ? 168 : 82, 132, ">>", WHITE);
    return;
  }
  gfx_text_c(FONT_HUGE, 160, 62, "Paused", WHITE);
  static const char *const items[] = {"Resume", "How to play", "Quit night"};
  for (int i = 0; i < 3; i++) {
    gfx_text(FONT_BIG, 112, 104 + 20 * i, items[i], WHITE);
    if (g.pause_item == i) gfx_text(FONT_BIG, 88, 104 + 20 * i, ">>", WHITE);
  }
}

/* ------------------------------------------------------------------ the custom night's modes */
static const struct {
  const char *name;
  int8_t ai[10];   /* CUSTOM_ORDER: W.Freddy, W.Bonnie, W.Chica, W.Foxy, BB, T.Freddy, T.Bonnie, T.Chica, Mangle, G.Freddy */
} MODES[] = {
  {"Custom", {0}},
  {"20/20/20/20", {20, 20, 20, 20, 0, 0, 0, 0, 0, 0}},
  {"New & Shiny", {0, 0, 0, 0, 10, 10, 10, 10, 10, 0}},
  {"Double Trouble", {0, 20, 0, 5, 0, 0, 20, 0, 0, 0}},
  {"Night of Misfits", {0, 0, 0, 0, 20, 0, 0, 0, 20, 10}},
  {"Foxy Foxy", {0, 0, 0, 20, 0, 0, 0, 0, 20, 0}},
  {"Ladies Night", {0, 0, 20, 0, 0, 0, 0, 20, 20, 0}},
  {"Freddy's Circus", {20, 0, 0, 10, 10, 20, 0, 0, 0, 10}},
  {"Cupcake Challenge", {5, 5, 5, 5, 5, 5, 5, 5, 5, 5}},
  {"Fazbear Fever", {10, 10, 10, 10, 10, 10, 10, 10, 10, 10}},
  {"Golden Freddy", {20, 20, 20, 20, 20, 20, 20, 20, 20, 20}},
};
#define NMODES (int)(sizeof MODES / sizeof MODES[0])

static bool custom_is(int mode) { return !memcmp(g.custom, MODES[mode].ai, sizeof g.custom); }

/* ------------------------------------------------------------------ 6 AM, the paychecks */
static void six_begin(void) {
  int n = g.n.night;
  if (n < 5 && g.reached < n + 1) g.reached = (uint8_t)(n + 1);
  if (n == 5) g.stars |= 1;
  if (n == 6) g.stars |= 2;
  if (n == 7 && custom_is(1)) g.stars |= 4;
  if (n == 7 && custom_is(10)) g.stars |= 8;
  save_progress();
  caption("*chimes*");
}

static void six_draw(void) {
  gfx_fill(0, gfx_y0, SCREEN_W, BAND_H, BLACK);
  float r = g.t < 1 ? 0 : g.t > 6 ? 1 : (g.t - 1) / 5;
  int cy = 104, dh = 30, off = (int)(r * dh);
  if (gfx_y0 + BAND_H > cy - 2 && gfx_y0 < cy + dh) {
    gfx_text(FONT_HUGE, 118, cy - off, "5", WHITE);
    gfx_text(FONT_HUGE, 118, cy + dh - off, "6", WHITE);
    gfx_fill(110, cy - dh, 30, dh, BLACK);   /* (only the window between shows) */
    gfx_fill(110, cy + dh, 30, dh, BLACK);
  }
  gfx_text(FONT_HUGE, 148, cy, "AM", WHITE);
  gfx_text_c(FONT_TEXT, 160, 170, g.t > 6.2f ? "*children cheering*" : "*chimes*", RGB(0xA0A0A0));
  gfx_dim(0, gfx_y0, SCREEN_W, BAND_H, fade_in_out(g.t, 1, 10.5f, 1));
}

static void six_step(void) {
  if (g.t < DT * 1.5f) six_begin();
  if (pressed(K_OK) && g.t > 6.5f && g.t < 10.5f) g.t = 10.5f;
  if (g.t < 11.5f) return;
  int n = g.n.night;
  if (n < 5) start_night(n + 1);
  else {
    g.paper = (uint8_t)(n - 5);
    go(SC_PAPER);
  }
}

static void paper_draw(void) {
  static const int pics[3] = {PIC_PAYCHECK, PIC_OVERTIME, PIC_PINKSLIP};
  pic_draw(pics[g.paper], 0, 0);
  gfx_dim(0, gfx_y0, SCREEN_W, BAND_H, fade_in_out(g.t, 2, 15, 2));
}

static void paper_step(void) {
  if (pressed(K_OK) && g.t > 2 && g.t < 15) g.t = 15;
  if (g.t >= 17) go(g.paper == 1 ? SC_CUSTOM : SC_TITLE);
}

/* ------------------------------------------------------------------ the end of a night */
static void over_draw(void) {
  if (g.t < 2) {
    gfx_fill(0, gfx_y0, SCREEN_W, BAND_H, BLACK);
    gfx_static(0, gfx_y0, SCREEN_W, BAND_H, 30, rnd());
    return;
  }
  pic_draw(PIC_GAMEOVER, 0, 0);
  gfx_static(0, gfx_y0, SCREEN_W, BAND_H, g.t < 2.5f ? 26 : 6, rnd());
  gfx_text_r(FONT_HUGE, 306, 200, "Game Over", WHITE);
}

static void over_step(void) {
  if ((pressed(K_OK) && g.t > 2.5f) || g.t > 12) go(SC_TITLE);
}

/* ------------------------------------------------------------------ the custom night */
static const char *const short_names[10] = {"W.Freddy", "W.Bonnie", "W.Chica", "W.Foxy", "BB",
                                            "T.Freddy", "T.Bonnie", "T.Chica", "Mangle", "G.Freddy"};

static void custom_draw(void) {
  gfx_fill(0, gfx_y0, SCREEN_W, BAND_H, BLACK);
  gfx_text_c(FONT_BIG, 160, 4, "Customize Night", WHITE);
  for (int i = 0; i < 10; i++) {
    int x = 8 + 62 * (i % 5), y = 28 + 92 * (i / 5);
    pic_draw(PIC_FACE0 + i, x + 3, y);
    gfx_frame_rect(x + 1, y - 2, 52, 52, g.custom_sel == i ? WHITE : RGB(0x404040));
    gfx_text_c(FONT_TEXT, x + 27, y + 52, short_names[i], RGB(0xC8C8C8));
    char s[3] = {(char)('0' + g.custom[i] / 10), (char)('0' + g.custom[i] % 10), 0};
    gfx_text_c(FONT_BIG, x + 27, y + 67, g.custom[i] >= 10 ? s : s + 1, WHITE);
    if (g.custom_sel == i) {
      gfx_text(FONT_HUD, x + 4, y + 70, "<", WHITE);
      gfx_text(FONT_HUD, x + 44, y + 70, ">", WHITE);
    }
  }
  int sel = g.custom_sel == 10;
  gfx_text(FONT_TEXT, 10, 214, "Mode:", RGB(0x909090));
  gfx_text(FONT_HUD, 48, 213, MODES[g.custom_mode].name, sel ? WHITE : RGB(0xB0B0B0));
  if (sel) gfx_frame_rect(44, 210, 150, 17, WHITE);
  gfx_text(FONT_TEXT, 10, 228, "< > pick   up down: level   OK: ready", RGB(0x707070));
  gfx_text_r(FONT_BIG, 312, 208, "Ready", (int)(g.t * 2) & 1 ? WHITE : RGB(0xC0C0C0));
}

static void custom_step(void) {
  if (pressed(K_LEFT | K_DIGIT(4))) g.custom_sel = (uint8_t)((g.custom_sel + 10) % 11);
  if (pressed(K_RIGHT | K_DIGIT(6))) g.custom_sel = (uint8_t)((g.custom_sel + 1) % 11);
  int d = 0;
  uint32_t up = K_UP | K_DIGIT(8), down = K_DOWN | K_DIGIT(2);
  if (pressed(up)) d = 1, g.repeat = 0.35f;
  else if (pressed(down)) d = -1, g.repeat = 0.35f;
  else if (g.held & (up | down) && g.custom_sel < 10) {   /* held: faster */
    g.repeat -= DT;
    if (g.repeat <= 0) g.repeat = 0.06f, d = g.held & up ? 1 : -1;
  }
  if (d && g.custom_sel == 10) {   /* a mode: its levels */
    g.custom_mode = (uint8_t)((g.custom_mode + NMODES + d) % NMODES);
    if (g.custom_mode == 0) g.custom_mode = (uint8_t)(d > 0 ? 1 : NMODES - 1);
    memcpy(g.custom, MODES[g.custom_mode].ai, sizeof g.custom);
  } else if (d) {
    int8_t *v = &g.custom[g.custom_sel];
    *v = (int8_t)(*v + d < 0 ? 0 : *v + d > 20 ? 20 : *v + d);
    g.custom_mode = 0;
    for (int m = 1; m < NMODES; m++)
      if (custom_is(m)) g.custom_mode = (uint8_t)m;
  }
  if (pressed(K_BACK)) {
    save_progress();
    go(SC_TITLE);
  }
  if (pressed(K_OK)) {
    save_progress();
    start_night(7);
  }
}

/* ------------------------------------------------------------------ steps and drawing */
void game_step(uint32_t keys) {
  g.pressed = keys & ~g.held;
  g.held = keys;
  for (int i = 0; i < 4; i++)
    if (g.cap_t[i] > 0) g.cap_t[i] -= DT;
  if (g.blip > 0) g.blip -= DT;
  if (keys & K_HOME) {   /* Home (or On/Off): a night is kept as it is */
    if (g.scene == SC_NIGHT) save_resume();
    g.running = false;
    return;
  }
  g.t += DT;
  switch (g.scene) {
  case SC_TITLE: title_step(); break;
  case SC_NEWS: news_step(); break;
  case SC_KEYS: keys_step(); break;
  case SC_INTRO: intro_step(); break;
  case SC_NIGHT: night_scene_step(); break;
  case SC_SCARE:
    g.scare_t += DT;
    if (g.scare_t > 1.1f) go(SC_OVER);
    break;
  case SC_SIX: six_step(); break;
  case SC_PAPER: paper_step(); break;
  case SC_OVER: over_step(); break;
  case SC_CUSTOM: custom_step(); break;
  }
}

void game_draw(void) {
  static void (*const draws[])(void) = {
    [SC_TITLE] = title_draw, [SC_NEWS] = news_draw, [SC_KEYS] = keys_draw, [SC_INTRO] = intro_draw,
    [SC_NIGHT] = view_night, [SC_SCARE] = view_scare, [SC_SIX] = six_draw, [SC_PAPER] = paper_draw,
    [SC_OVER] = over_draw, [SC_CUSTOM] = custom_draw};
  gfx_frame(g.scene == SC_NIGHT && g.paused ? pause_draw : draws[g.scene]);
}
