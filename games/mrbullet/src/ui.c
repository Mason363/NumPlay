/* The screens (ui.h): the title, the modes, the levels, a level with its key lines, the pause menu, the result,
 * how to play and quitting. Every screen says in its bottom line what the keys do. */
#include <math.h>
#include <string.h>
#include "draw.h"
#include "game.h"
#include "plat.h"
#include "res.h"
#include "save.h"
#include "ui.h"

enum { S_TITLE, S_MODES, S_LEVELS, S_PLAY, S_PAUSE, S_RESULT, S_HOWTO, S_QUIT, S_COUNT };

static Progress P;
static int screen = S_TITLE, howto_back = S_TITLE, quit_back = S_TITLE;
static int sel, mode, level = -1;
static bool dirty = true;
static uint8_t *level_mem;
static uint32_t held, edge, last_ms, acc_ms, hold_ms;
static int result_stars, result_new;

static const char *const MODE_NAME[MODE_COUNT] = {"Classic", "Grenades", "Hostages", "Friendly Fire"};
static const int MODE_STARS[MODE_COUNT] = {0, 50, 120, 200};   /* stars that open it */
static const char *const PRAISE[4] = {"Good!", "Well done!", "Awesome!", "Fantastic!"};

/* ------------------------------------------------------------------ text */
static char *put(char *p, const char *s) {
  while (*s) *p++ = *s++;
  *p = 0;
  return p;
}
static char *num(char *p, uint32_t v) {
  char t[12];
  int n = 0;
  do t[n++] = (char)('0' + v % 10); while ((v /= 10) && n < 11);
  while (n) *p++ = t[--n];
  *p = 0;
  return p;
}

/* ------------------------------------------------------------------ progress */
static int total_stars(void) {
  int s = 0;
  for (int i = 0; i < LEVEL_COUNT; i++) s += P.stars[i];
  return s;
}
static int mode_stars(int m) {
  int s = 0, f = res_mode_first(m), n = res_mode_count(m);
  for (int i = f; i < f + n; i++) s += P.stars[i];
  return s;
}
static bool mode_open(int m) { return total_stars() >= MODE_STARS[m]; }
static bool level_open(int l) {
  int m = game_mode_of(l), f = res_mode_first(m);
  return l == f || (P.flags[l - 1] & LV_CLEARED) || (P.flags[l] & LV_CLEARED);
}

/* ------------------------------------------------------------------ playing */
static void start(int l) {
  level = l;
  mode = game_mode_of(l);
  P.last[mode] = (uint16_t)l;
  P.mode = (uint8_t)mode;
  if (!game_load(l, level_mem)) {
    game_end();
    screen = S_LEVELS;
    return;
  }
  acc_ms = 0;
  screen = S_PLAY;
}

static void finish(void) {
  if (g.state == ST_WON) {
    int st = game_stars();
    result_stars = st;
    result_new = st > P.stars[level];
    if (st > P.stars[level]) P.stars[level] = (uint8_t)st;
    P.flags[level] |= LV_CLEARED;
    save_write(&P);
  } else {
    result_stars = -1;
  }
  sel = 0;
  screen = S_RESULT;
}

void ui_quit(void) {
  game_end();
  save_write(&P);
}

/* ------------------------------------------------------------------ keys */
static bool pressed(uint32_t k) { return (edge & k) != 0; }

static void menu_keys(int count) {
  if (pressed(KB_UP)) sel = (sel + count - 1) % count, dirty = true;
  if (pressed(KB_DOWN)) sel = (sel + 1) % count, dirty = true;
}

static void play_keys(uint32_t dt) {
  uint32_t aim = held & (KB_UP | KB_DOWN);
  if (aim) hold_ms += dt;
  else hold_ms = 0;
  float rate = hold_ms < 350 ? 0.35f : hold_ms < 900 ? 1.0f : 2.0f;   /* radians a second */
  if (pressed(KB_UP)) game_aim_by(0.0175f), dirty = true;
  else if (held & KB_UP) game_aim_by(rate * dt / 1000.0f), dirty = true;
  if (pressed(KB_DOWN)) game_aim_by(-0.0175f), dirty = true;
  else if (held & KB_DOWN) game_aim_by(-rate * dt / 1000.0f), dirty = true;
  if (g.grenades) {
    if (held & KB_RIGHT) g.power = fminf(1, g.power + 0.6f * dt / 1000.0f), dirty = true;
    if (held & KB_LEFT) g.power = fmaxf(0, g.power - 0.6f * dt / 1000.0f), dirty = true;
  } else {
    if (pressed(KB_LEFT)) game_turn(true), dirty = true;
    if (pressed(KB_RIGHT)) game_turn(false), dirty = true;
  }
  if (pressed(KB_OK) && game_can_fire()) game_fire(), dirty = true;
}

bool ui_frame(uint32_t keys, uint32_t now) {
  edge = keys & ~held;
  held = keys;
  uint32_t dt = last_ms ? now - last_ms : 0;
  if (dt > 100) dt = 100;
  last_ms = now;
  if (pressed(KB_HOME)) {
    ui_quit();
    return false;
  }
  switch (screen) {
  case S_TITLE:
    menu_keys(3);
    if (pressed(KB_OK)) {
      if (sel == 0) screen = S_MODES, sel = P.mode;
      else if (sel == 1) howto_back = S_TITLE, screen = S_HOWTO;
      else quit_back = S_TITLE, screen = S_QUIT, sel = 1;
      dirty = true;
    } else if (pressed(KB_BACK)) {
      quit_back = S_TITLE, screen = S_QUIT, sel = 1, dirty = true;
    }
    break;
  case S_MODES:
    menu_keys(MODE_COUNT);
    if (pressed(KB_OK) && mode_open(sel)) {
      mode = sel;
      int l = P.last[mode];
      if (game_mode_of(l) != mode || !level_open(l)) l = res_mode_first(mode);
      sel = l - res_mode_first(mode);
      screen = S_LEVELS, dirty = true;
    } else if (pressed(KB_BACK)) {
      screen = S_TITLE, sel = 0, dirty = true;
    }
    break;
  case S_LEVELS: {
    int n = res_mode_count(mode);
    int old = sel;
    if (pressed(KB_LEFT)) sel--;
    if (pressed(KB_RIGHT)) sel++;
    if (pressed(KB_UP)) sel -= 4;
    if (pressed(KB_DOWN)) sel += 4;
    if (sel < 0) sel = 0;
    if (sel >= n) sel = n - 1;
    if (sel != old) dirty = true;
    if (pressed(KB_OK) && level_open(res_mode_first(mode) + sel)) {
      start(res_mode_first(mode) + sel);
      if (!P.guide) P.guide = 1, howto_back = S_PLAY, screen = S_HOWTO;
      dirty = true;
    } else if (pressed(KB_BACK)) {
      screen = S_MODES, sel = mode, dirty = true;
    }
    break;
  }
  case S_PLAY:
    if (pressed(KB_BACK)) {
      screen = S_PAUSE, sel = 0, dirty = true;
      break;
    }
    play_keys(dt);
    /* 60 steps a second, at most 3 a frame (a slow frame: the game slows down rather than skip) */
    acc_ms += dt * 60;
    for (int k = 0; acc_ms >= 1000 && k < 3; k++) {
      acc_ms -= 1000;
      game_step();
      dirty = true;
    }
    if (acc_ms >= 1000) acc_ms = 0;
    if (g.state != ST_PLAY) finish();
    break;
  case S_PAUSE:
    menu_keys(5);
    if (pressed(KB_BACK)) screen = S_PLAY, dirty = true;
    else if (pressed(KB_OK)) {
      if (sel == 0) screen = S_PLAY;
      else if (sel == 1) start(level);
      else if (sel == 2) game_end(), screen = S_LEVELS, sel = level - res_mode_first(mode);
      else if (sel == 3) howto_back = S_PAUSE, screen = S_HOWTO;
      else quit_back = S_PAUSE, screen = S_QUIT, sel = 1;
      dirty = true;
    }
    break;
  case S_RESULT: {
    bool won = result_stars >= 0;
    bool more = won && level + 1 < res_mode_first(mode) + res_mode_count(mode);
    int count = more ? 3 : 2;
    menu_keys(count);
    if (pressed(KB_OK)) {
      int pick = more ? sel : sel + 1;
      if (pick == 0) start(level + 1);
      else if (pick == 1) start(level);
      else game_end(), screen = S_LEVELS, sel = level - res_mode_first(mode);
      dirty = true;
    } else if (pressed(KB_BACK)) {
      game_end(), screen = S_LEVELS, sel = level - res_mode_first(mode), dirty = true;
    }
    break;
  }
  case S_HOWTO:
    if (pressed(KB_OK) || pressed(KB_BACK)) {
      screen = howto_back, sel = 0, dirty = true;
      if (screen == S_PLAY) save_write(&P);
    }
    break;
  case S_QUIT:
    if (pressed(KB_UP) || pressed(KB_DOWN)) sel ^= 1, dirty = true;
    if (pressed(KB_BACK)) screen = quit_back, sel = 0, dirty = true;
    else if (pressed(KB_OK)) {
      if (sel == 0) {
        ui_quit();
        return false;
      }
      screen = quit_back, sel = 0, dirty = true;
    }
    break;
  }
  return true;
}

bool ui_dirty(void) { return dirty; }

/* ------------------------------------------------------------------ drawing */
static void key_line(const char *s) {
  if (gfx_y1 <= 222) return;
  gfx_fill(0, 222, SCREEN_W, 18, COL_INK);
  gfx_text_center(&np_font_small, SCREEN_W / 2, 235, s, COL_MUTED, 256);
}

static void backdrop(void) {
  gfx_fill(0, gfx_y0, SCREEN_W, gfx_y1 - gfx_y0, COL_PANEL);
  draw_pic(PIC_BG, FIELD_X0, 0);
  gfx_fill_alpha(FIELD_X0, gfx_y0, FIELD_W, gfx_y1 - gfx_y0, COL_INK, 110);
}

static void button(int x, int y, int w, int h, const char *s, bool on, bool locked) {
  if (y + h < gfx_y0 || y > gfx_y1) return;
  gfx_rrect(x, y, w, h, 7, on ? COL_RED : COL_PANEL_LIGHT, 256);
  if (on) gfx_rrect_outline(x - 2, y - 2, w + 4, h + 4, 9, 2, COL_FOCUS, 256);
  gfx_text_center(&np_font_body, x + w / 2, y + h / 2 + 5, s, locked ? COL_MUTED : COL_TEXT, 256);
}

static void scene_title(void *ctx) {
  (void)ctx;
  backdrop();
  draw_pic(PIC_TITLE, (SCREEN_W - res_pic(PIC_TITLE)->w) / 2, 22);
  static const char *const items[3] = {"Play", "How to play", "Quit"};
  for (int i = 0; i < 3; i++) button(100, 104 + i * 36, 120, 28, items[i], sel == i, false);
  key_line("Up Down choose  -  OK select  -  Home quit");
}

static void scene_modes(void *ctx) {
  (void)ctx;
  backdrop();
  gfx_text_center(&np_font_title, SCREEN_W / 2, 30, "Modes", COL_TEXT, 256);
  for (int m = 0; m < MODE_COUNT; m++) {
    int y = 44 + m * 42;
    bool open = mode_open(m);
    button(40, y, 240, 34, "", sel == m, !open);
    if (y + 34 < gfx_y0 || y > gfx_y1) continue;
    gfx_text(&np_font_body, 54, y + 22, MODE_NAME[m], open ? COL_TEXT : COL_MUTED, 256);
    char t[24], *p = t;
    if (open) {
      p = num(p, (uint32_t)mode_stars(m));
      p = put(p, " / ");
      num(p, (uint32_t)res_mode_count(m) * 3);
    } else {
      p = num(p, (uint32_t)total_stars());
      p = put(p, " / ");
      num(p, (uint32_t)MODE_STARS[m]);
    }
    gfx_text_right(&np_font_small, 248, y + 21, t, COL_TEXT, 256);
    if (open) draw_pic(PIC_STAR, 252, y + 7);
    else draw_pic(PIC_PADLOCK, 256, y + 10);
  }
  key_line("Up Down choose  -  OK open  -  Back title");
}

static void scene_levels(void *ctx) {
  (void)ctx;
  backdrop();
  int first = res_mode_first(mode), n = res_mode_count(mode);
  int page = sel / 16, pages = (n + 15) / 16;
  char t[40], *p = t;
  p = put(p, MODE_NAME[mode]);
  p = put(p, "  -  Chapter ");
  p = num(p, (uint32_t)page + 1);
  p = put(p, " of ");
  num(p, (uint32_t)pages);
  gfx_text_center(&np_font_body, SCREEN_W / 2, 20, t, COL_TEXT, 256);
  for (int k = 0; k < 16; k++) {
    int i = page * 16 + k;
    if (i >= n) break;
    int x = 52 + (k % 4) * 56, y = 30 + (k / 4) * 47;
    if (y + 44 < gfx_y0 || y > gfx_y1) continue;
    bool open = level_open(first + i), on = sel == i;
    gfx_rrect(x, y, 48, 42, 8, on ? COL_RED : open ? COL_PANEL_LIGHT : COL_INK, 256);
    if (on) gfx_rrect_outline(x - 2, y - 2, 52, 46, 10, 2, COL_FOCUS, 256);
    if (open) {
      char s[8];
      num(s, (uint32_t)i + 1);
      gfx_text_center(&np_font_body, x + 24, y + 20, s, COL_TEXT, 256);
      for (int st = 0; st < 3; st++) {
        int sx = x + 7 + st * 13, sy = y + 28;
        if (st < P.stars[first + i]) gfx_circle(sx + 4, sy + 4, 4, COL_FOCUS, 256);
        else gfx_circle(sx + 4, sy + 4, 4, COL_INK, 200);
      }
    } else {
      draw_pic(PIC_PADLOCK, x + 24 - res_pic(PIC_PADLOCK)->w / 2, y + 14);
    }
  }
  key_line("Arrows choose  -  OK play  -  Back modes");
}

static void scene_play(void *ctx) {
  (void)ctx;
  draw_world(screen == S_PLAY);
  char t[24], *p = t;
  p = put(p, "Level ");
  num(p, (uint32_t)(level - res_mode_first(mode) + 1));
  draw_hud(t, "Left Right", g.grenades ? "power" : "turn");
}

static void scene_pause(void *ctx) {
  scene_play(ctx);
  gfx_fill_alpha(FIELD_X0, gfx_y0, FIELD_W, gfx_y1 - gfx_y0, COL_INK, 150);
  static const char *const items[5] = {"Resume", "Restart", "Levels", "How to play", "Quit game"};
  for (int i = 0; i < 5; i++) button(90, 34 + i * 36, 140, 28, items[i], sel == i, false);
}

static void scene_result(void *ctx) {
  scene_play(ctx);
  gfx_fill_alpha(FIELD_X0, gfx_y0, FIELD_W, gfx_y1 - gfx_y0, COL_INK, 150);
  bool won = result_stars >= 0;
  if (won) {
    gfx_text_center(&np_font_title, SCREEN_W / 2, 40, PRAISE[result_stars], COL_TEXT, 256);
    for (int i = 0; i < 3; i++) {
      int x = 160 - 54 + i * 38, y = 52 + (i == 1 ? -4 : 0);
      if (i < result_stars) draw_pic(PIC_STAR_BIG, x, y);
      else if (y + 34 > gfx_y0 && y < gfx_y1) gfx_circle(x + 17, y + 17, 13, COL_PANEL_LIGHT, 256);
    }
  } else {
    const char *why = g.lose == LOSE_HOSTAGE ? "Don't kill the hostages!" : g.lose == LOSE_SELF ? "Don't shoot yourself!"
                                                                                                : "No bullets!";
    gfx_text_center(&np_font_body, SCREEN_W / 2, 60, why, COL_TEXT, 256);
  }
  bool more = won && level + 1 < res_mode_first(mode) + res_mode_count(mode);
  static const char *const items[3] = {"Next level", "Replay", "Levels"};
  int k = 0;
  for (int i = more ? 0 : 1; i < 3; i++, k++) button(90, 104 + k * 36, 140, 28, items[i], sel == k, false);
}

static void scene_howto(void *ctx) {
  (void)ctx;
  backdrop();
  gfx_text_center(&np_font_title, SCREEN_W / 2, 26, "How to play", COL_TEXT, 256);
  static const char *const lines[] = {
    "Shoot every enemy in the level.",
    "Bullets bounce off walls: aim at them",
    "to reach enemies you can't see.",
    "Fewer bullets, more stars.",
    "Glass breaks, red boxes explode,",
    "a coloured switch moves its blocks.",
    "Grenades go off after 2.5 seconds.",
    "Don't hit hostages, or yourself in",
    "Friendly Fire.",
    "",
    "Up Down aim, Left Right turn",
    "(grenades: Left Right power), OK fire.",
  };
  for (unsigned i = 0; i < sizeof lines / sizeof *lines; i++)
    gfx_text_center(&np_font_small, SCREEN_W / 2, 48 + 14 * (int)i, lines[i], COL_TEXT, 256);
  key_line("OK or Back  continue");
}

static void scene_quit(void *ctx) {
  if (quit_back == S_PAUSE) scene_play(ctx);
  else backdrop();
  gfx_fill_alpha(0, gfx_y0, SCREEN_W, gfx_y1 - gfx_y0, COL_INK, 150);
  gfx_text_center(&np_font_body, SCREEN_W / 2, 80, "Quit Mr Bullet?", COL_TEXT, 256);
  button(110, 100, 100, 28, "Quit", sel == 0, false);
  button(110, 136, 100, 28, "Stay", sel == 1, false);
}

static void (*const scenes[S_COUNT])(void *) = {scene_title, scene_modes, scene_levels, scene_play,
                                                scene_pause, scene_result, scene_howto, scene_quit};

void ui_draw(void) {
  gfx_render(scenes[screen], NULL, 0, SCREEN_H);
  dirty = false;
}

void ui_init(uint8_t *mem, uint16_t *strip, void *scratch) {
  level_mem = mem;
  gfx_buf = strip;
  save_scratch(scratch);
  save_load(&P);
  sel = 0;
  screen = S_TITLE;
  dirty = true;
}
