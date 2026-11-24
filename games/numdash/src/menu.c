/* Main menu (with the icon running across it), level select, icon kit and
 * the settings / stats / how-to-play popups. */
#include "app.h"
#include "ui.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "../../common/np_text.h"

/* Colours the main menu cycles through and the level select page tints. */
static const uint8_t page_colors[9][3] = {
  {0, 0, 232}, {227, 0, 229}, {233, 0, 115}, {233, 0, 0}, {231, 112, 0}, {233, 232, 0}, {0, 231, 0}, {0, 227, 228}, {0, 112, 229}};
static color_t page_color(int i) {
  i %= 9;
  if (i < 0) i += 9;
  return rgb(page_colors[i][0], page_colors[i][1], page_colors[i][2]);
}
static int wrap(int v, int n) { v %= n; return v < 0 ? v + n : v; }
/* GD colours a level's page by its place among all the official levels */
static color_t level_color(int i) {
#if ND_EXTRA_LEVELS
  /* (Meltdown's and SubZero's by their place in their own game) */
  static const uint8_t gd_index[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 13, 19, 21, 0, 1, 2, 1};
#else
  static const uint8_t gd_index[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 13, 19, 21};
#endif
  return page_color(i < (int)sizeof(gd_index) ? gd_index[i] : i);
}

enum { POP_NONE, POP_SETTINGS, POP_STATS, POP_HELP };
static int popup, popup_sel, help_page;
static float popup_t;

/* ------------------------------------------------------------ main menu */

static Level menu_level;
static SceneOpts menu_opts;
static color_t run_c1, run_c2;
static bool run_hold;
static int run_speed_roll;
static unsigned run_tick;

static void runner_reset(void) {
  float gx = app.g.ground_x, bx = app.g.bg_x;
  bool keep = app.g.L == &menu_level;
  game_start(&app.g, &menu_level, true);
  app.g.menu_camera = true;
  app.g.cam_x = 0;
  app.g.cam_y = 0;
  if (keep) { app.g.ground_x = gx; app.g.bg_x = bx; }
  Player *p = &app.g.p;
  p->x = -120;
  p->speed = (uint8_t)(frand() * 4) & 3;
  if (frand() < 0.4f) p->mode = MODE_SHIP;
  p->ceiling_y = 999999;
  run_c1 = icon_color((int)(frand() * 42));
  run_c2 = icon_color((int)(frand() * 42));
  run_hold = frand() < 0.5f;
  fx_reset();
}

static void runner_tick(void) {
  Game *g = &app.g;
  bool press = false;
  if ((run_tick++ & 3) == 0) {
    int r = (int)(frand() * 16);
    if (g->p.mode == MODE_CUBE) run_speed_roll = r == 0 ? 4 : run_speed_roll;
    else if (r == 0) run_hold = !run_hold;
  }
  if (g->p.mode == MODE_CUBE) {
    press = run_speed_roll > 0;
    if (run_speed_roll > 0) run_speed_roll--;
  } else {
    if (g->p.y >= 250) run_hold = false;
    press = run_hold;
  }
  game_step(g, press);
  if (g->p.x > VIEW_W + 90 || g->dead) runner_reset();
}

static void menu_colors(float dt) {
  app.menu_color_t += dt / 4.0f;
  if (app.menu_color_t >= 1) {
    app.menu_color_t = 0;
    app.menu_color = wrap(app.menu_color + 1, 9);
  }
  color_t c = c_mix(page_color(app.menu_color), page_color(app.menu_color + 1), (unsigned)(app.menu_color_t * 256));
  uint8_t rgb3[3] = {(uint8_t)c_r(c), (uint8_t)c_g(c), (uint8_t)c_b(c)};
  memcpy(app.g.ch[CH_BG].cur, rgb3, 3);
  memcpy(app.g.ch[CH_G1].cur, rgb3, 3);
  memset(app.g.ch[CH_LINE].cur, 255, 3);
  memset(app.g.ch[CH_OBJ].cur, 255, 3);
}

static float btn_scale(int id) {
  float s = 1;
  if (app.sel == id) s = 1.1f + 0.025f * sinf(app.t * 5);
  if (app.press_id == id) s = 1.1f + 0.16f * ui_ease_bounce_out(fminf(app.press_t / 0.12f, 1));
  return s;
}

static void main_activate(int id) {
  switch (id) {
    case 0: app_go(SCR_GARAGE); break;
    case 1: app_go(SCR_SELECT); break;
    case 2: app_go(SCR_CREATOR); break;
    case 3: popup = POP_HELP; help_page = 0; popup_t = 0; break;
    case 4: popup = POP_SETTINGS; popup_sel = 0; popup_t = 0; break;
    default: popup = POP_STATS; popup_t = 0; break;
  }
}

static void main_draw(void) {
  scene_draw();
  gfx_layer_draw(160, 38, 256, rgb(215, 255, 100), rgb(70, 180, 10), 256);
  gfx_text_center(FONT_SMALL, 160, 62, T("NUMWORKS EDITION"), rgb(255, 255, 255), rgb(200, 230, 255), 230);
  ui_sprite(SPR_BTN_GARAGE, 62, 118, btn_scale(0), 256);
  ui_sprite(SPR_BTN_PLAY, 160, 114, btn_scale(1), 256);
  ui_sprite(SPR_BTN_CREATOR, 258, 118, btn_scale(2), 256);
  ui_sprite(SPR_BTN_HELP, 112, 208, btn_scale(3), 256);
  ui_sprite(SPR_BTN_SETTINGS, 160, 208, btn_scale(4), 256);
  ui_sprite(SPR_BTN_STATS, 208, 208, btn_scale(5), 256);
}

/* ------------------------------------------------------------ popups */

static const char *const option_names[6] = {T("SHOW PERCENT"), T("PROGRESS BAR"), T("AUTO CHECKPOINTS"), T("LOW DETAIL"), T("SHOW FPS"), T("NO SHAKE")};
static const uint8_t option_bits[6] = {OPT_PERCENT, OPT_BAR, OPT_AUTOCHECK, OPT_LOWDETAIL, OPT_FPS, OPT_NOSHAKE};

static void popup_tick(void) {
  if (app_hit(K_BACK)) {
    if (popup == POP_SETTINGS) app_save_progress();
    popup = POP_NONE;
    return;
  }
  if (popup == POP_SETTINGS) {
    if (app_hit(K_UP)) popup_sel = wrap(popup_sel - 1, 6);
    if (app_hit(K_DOWN)) popup_sel = wrap(popup_sel + 1, 6);
    if (app_accept() || app_hit(K_LEFT | K_RIGHT)) progress.options ^= option_bits[popup_sel];
  } else if (popup == POP_HELP) {
    if (app_hit(K_LEFT | K_RIGHT)) help_page ^= 1;
    if (app_accept()) { if (help_page == 0) help_page = 1; else popup = POP_NONE; }
  } else if (app_accept()) {
    popup = POP_NONE;
  }
}

static void stat_row(int y, const char *label, unsigned long v, int icon) {
  char buf[16];
  gfx_format_uint(buf, v);
  gfx_text(FONT_SMALL, 58, y, label, 0xffff, 0xffff, 256);
  gfx_text_right(FONT_BIG, icon >= 0 ? 244 : 262, y + 3, buf, 0xffff, 0xffff, 256);
  if (icon >= 0) gfx_sprite(icon, 256, y - 4, 0, 0xffff, 256, BLEND_NORMAL);
}

static void popup_draw(void) {
  float s = ui_ease_elastic_out(popup_t / 0.5f, 0.6f);
  ui_dim((unsigned)(fminf(popup_t / 0.14f, 1) * 120));
  int w = (int)(272 * s), h = (int)(200 * s);
  if (w < 8 || h < 8) return;
  if (popup == POP_HELP) ui_window_blue(160 - w / 2, 120 - h / 2, w, h);
  else ui_window_brown(160 - w / 2, 120 - h / 2, w, h);
  if (s < 0.85f) return;
  if (popup == POP_SETTINGS) {
    ui_title(FONT_BIG, 160, 44, T("SETTINGS"));
    for (int i = 0; i < 6; i++) {
      int y = 66 + i * 23;
      bool on = (progress.options & option_bits[i]) != 0, focus = popup_sel == i;
      ui_checkbox(92, y, on, focus ? 1.15f + 0.03f * sinf(app.t * 6) : 1.0f);
      gfx_text(FONT_SMALL, 108, y + 4, option_names[i], focus ? GOLD_TOP : 0xffff, focus ? GOLD_BOTTOM : 0xffff, 256);
    }
    gfx_text_center(FONT_SMALL, 160, 208, T("OK: TOGGLE    BACK: SAVE"), rgb(255, 230, 190), rgb(255, 230, 190), 220);
  } else if (popup == POP_STATS) {
    ui_title(FONT_BIG, 160, 44, T("STATS"));
    unsigned completed = 0, stars = 0, coins = 0;
    for (int i = 0; i < LEVEL_COUNT; i++) {
      if (progress.lv[i].normal >= 100) { completed++; stars += level_defs[i].stars; }
      coins += (unsigned)__builtin_popcount(progress.lv[i].coins);
    }
    stat_row(74, T("TOTAL JUMPS"), progress.jumps, -1);
    stat_row(98, T("TOTAL ATTEMPTS"), progress.attempts, -1);
    stat_row(122, T("COMPLETED LEVELS"), completed, -1);
    stat_row(146, T("STARS"), stars, SPR_STAR_UI);
    stat_row(170, T("SECRET COINS"), coins, SPR_COIN_UI);
  } else {
    static const char *const pages[2][8] = {
      {T("HOW TO PLAY"), T("OK, EXE OR UP: JUMP"), T("HOLD TO KEEP JUMPING OR TO FLY"), T("TAP IN MID AIR ON AN ORB"), T("BACK: PAUSE    HOME: SAVE AND QUIT"),
       T("PRACTICE: 0 PLACES A CHECKPOINT"), T("BACKSPACE REMOVES THE LAST ONE"), T("START PRACTICE FROM THE PAUSE MENU")},
      {T("LEVEL EDITOR"), T("ARROWS: MOVE    OK: USE TOOL"), T("0: BUILD / EDIT / DELETE"), T("+ -  OR TOOLBOX: PICK OBJECT"), T("SHIFT: ROTATE    XNT: COPY"),
       T("ALPHA: UNDO    BACKSPACE: ERASE"), T("VAR: SAVE    LN: LEVEL SETTINGS"), T("EXE: PLAYTEST    BACK: EXIT")}};
    ui_title(FONT_BIG, 160, 44, pages[help_page][0]);
    for (int i = 1; i < 8; i++) gfx_text_center(FONT_SMALL, 160, 58 + i * 18, pages[help_page][i], 0xffff, 0xffff, 256);
    ui_nav_dots(160, 206, 2, help_page);
  }
}

/* ------------------------------------------------------------ level select */

static void select_tick(void) {
  if (app.press_id >= 0) return;
  if (app_hit(K_LEFT)) app.select_page--;
  if (app_hit(K_RIGHT)) app.select_page++;
  if (app_hit(K_BACK)) { app_go(SCR_MENU); return; }
  if (app_accept()) app_begin_press(100);
}

static void select_frame(float dt) {
  float target = (float)app.select_page, d = target - app.select_scroll;
  app.select_scroll += d * fminf(1, dt * 9);
  if (fabsf(d) < 0.002f) app.select_scroll = target;
  if (app.press_id == 100 && app.press_t >= 0.12f) {
    app.press_id = -1;
    app.level = wrap(app.select_page, LEVEL_COUNT);
    app.practice = false;
    app.testing = false;
    app_go(SCR_PLAY);
  }
}

static void level_page(int page, int dx) {
  int i = wrap(page, LEVEL_COUNT);
  const LevelDef *d = &level_defs[i];
  const LevelStat *s = &progress.lv[i];
  int cx = 160 + dx;
  if (cx < -170 || cx > 490) return;
  float sc = app.press_id == 100 && page == app.select_page ? 1 + 0.12f * ui_ease_bounce_out(fminf(app.press_t / 0.12f, 1)) : 1;
  int w = (int)(242 * sc), h = (int)(84 * sc);
  ui_window_dark(cx - w / 2, 80 - h / 2, w, h, 90);
  /* difficulty 1-5: easy .. insane, 6: demon */
  int face = d->difficulty >= 6 ? SPR_FACE_DEMON : SPR_FACE1 + (d->difficulty > 0 ? d->difficulty - 1 : 0);
  gfx_sprite(face, cx - 97, 82, 0, 0xffff, 256, BLEND_NORMAL);
  gfx_text_center(FONT_BIG, cx + 17, 87, d->name, 0xffff, 0xffff, 256);
  char buf[8];
  gfx_format_uint(buf, d->stars);
  gfx_text_right(FONT_SMALL, cx + 99, 53, buf, 0xffff, 0xffff, 256);
  gfx_sprite(SPR_STAR_UI, cx + 107, 49, 0, 0xffff, 256, BLEND_NORMAL);
  for (int k = 0; k < 3; k++)
    gfx_sprite((s->coins >> k & 1) ? SPR_COIN_UI : SPR_COIN_UI_EMPTY, cx + 72 + k * 17, 108, 0, 0xffff, 256, BLEND_NORMAL);
  ui_title(FONT_SMALL, cx, 138, T("NORMAL MODE"));
  ui_progress_bar(cx, 149, 220, 14, s->normal, rgb(0, 255, 0), true);
  ui_title(FONT_SMALL, cx, 173, T("PRACTICE MODE"));
  ui_progress_bar(cx, 184, 220, 14, s->practice, rgb(0, 255, 255), true);
}

static void select_draw(void) {
  float sc = app.select_scroll;
  int base = (int)floorf(sc);
  float fr = sc - base;
  color_t c = c_mix(level_color(wrap(base, LEVEL_COUNT)), level_color(wrap(base + 1, LEVEL_COUNT)), (unsigned)(fr * 256));
  ui_gradient_bg(c);
  scene_ground(c, 0xffff, 0, 212, false, true);
  ui_side_art();
  ui_top_bar(12, true);
  level_page(base, (int)(-fr * 320));
  level_page(base + 1, (int)((1 - fr) * 320));
  gfx_sprite(SPR_NAV_ARROW, 20, 120, XF_FLIPX, 0xffff, 256, BLEND_NORMAL);
  gfx_sprite(SPR_NAV_ARROW, 300, 120, 0, 0xffff, 256, BLEND_NORMAL);
  gfx_sprite(SPR_ARROW_BACK, 22, 26, 0, 0xffff, 256, BLEND_NORMAL);
  ui_nav_dots(160, 228, LEVEL_COUNT, wrap(app.select_page, LEVEL_COUNT));
}

/* ------------------------------------------------------------ icon kit */

static void garage_tick(void) {
  if (app_hit(K_BACK)) { app_save_progress(); app_go(SCR_MENU); return; }
  if (app.row == 0) {
    if (app_hit(K_LEFT | K_RIGHT)) app.garage_tab ^= 1;
    if (app_hit(K_DOWN)) app.row = 1;
    if (app_accept()) app.garage_tab ^= 1;
    return;
  }
  int c = app.sel % 14, r = app.sel / 14;
  if (app_hit(K_LEFT)) c = wrap(c - 1, 14);
  if (app_hit(K_RIGHT)) c = wrap(c + 1, 14);
  if (app_hit(K_DOWN)) r = r < 2 ? r + 1 : 2;
  if (app_hit(K_UP)) { if (r == 0) app.row = 0; else r--; }
  app.sel = r * 14 + c;
  if (app_accept()) {
    if (app.garage_tab == 0) progress.color1 = (uint8_t)app.sel;
    else progress.color2 = (uint8_t)app.sel;
  }
}

static void garage_draw(void) {
  ui_gradient_bg(rgb(170, 170, 170));
  gfx_sprite(SPR_ARROW_BACK, 22, 26, 0, 0xffff, 256, BLEND_NORMAL);
  ui_title(FONT_BIG, 160, 30, T("ICON KIT"));
  /* preview on a small floor */
  gfx_round_rect(70, 84, 180, 4, 2, 0, 90);
  float bob = 0;
  scene_draw_player_icon(MODE_CUBE, 124, 64 + bob, 0, 1.6f, app_p1(), app_p2(), false, 256);
  scene_draw_player_icon(MODE_SHIP, 204, 66, 0, 1.3f, app_p1(), app_p2(), false, 256);
  ui_text_button(118, 108, 84, 22, T("COLOR 1"), app.garage_tab == 0 ? BTN_GREEN : BTN_GRAY, app.row == 0 && app.garage_tab == 0 ? 1.08f : 1);
  ui_text_button(202, 108, 84, 22, T("COLOR 2"), app.garage_tab == 1 ? BTN_GREEN : BTN_GRAY, app.row == 0 && app.garage_tab == 1 ? 1.08f : 1);
  int cur = app.garage_tab == 0 ? progress.color1 : progress.color2;
  for (int i = 0; i < 42; i++) {
    int x = 13 + (i % 14) * 21, y = 128 + (i / 14) * 22;
    gfx_round_rect(x - 1, y - 1, 20, 20, 3, 0, 256);
    gfx_round_rect(x + 1, y + 1, 16, 16, 2, icon_color(i), 256);
    if (i == cur) {
      gfx_round_rect(x - 3, y - 3, 24, 24, 4, 0xffff, 110);
      gfx_round_rect(x + 1, y + 1, 16, 16, 2, icon_color(i), 256);
      gfx_sprite(SPR_CHECK_ON, x + 9, y + 9, 0, 0xffff, 200, BLEND_NORMAL);
    }
    if (app.row == 1 && i == app.sel) {
      gfx_round_rect(x - 4, y - 4, 26, 3, 1, rgb(255, 255, 0), 256);
      gfx_round_rect(x - 4, y + 19, 26, 3, 1, rgb(255, 255, 0), 256);
      gfx_fill(x - 4, y - 4, 3, 26, rgb(255, 255, 0));
      gfx_fill(x + 19, y - 4, 3, 26, rgb(255, 255, 0));
    }
  }
  ui_side_art();
}

/* ------------------------------------------------------------ loading */

static const char *const tips[] = {
  T("LISTEN TO THE MUSIC TO HELP TIME YOUR JUMPS"), T("BACK FOR MORE ARE YA?"), T("USE PRACTICE MODE TO LEARN THE LAYOUT OF A LEVEL"),
  T("IF AT FIRST YOU DON'T SUCCEED, TRY, TRY AGAIN..."), T("CUSTOMIZE YOUR CHARACTER'S ICON AND COLOR!"),
  T("SPIKES ARE NOT YOUR FRIENDS. DON'T FORGET TO JUMP"), T("BUILD YOUR OWN LEVELS USING THE LEVEL EDITOR"),
  T("CAN YOU BEAT THEM ALL?"), T("PRO TIP: DON'T CRASH"), T("HOLD DOWN TO KEEP JUMPING"), T("PRO TIP: JUMP"),
  T("PLAY, CRASH, RINSE AND REPEAT"), T("ONLY ONE BUTTON REQUIRED TO CRASH"), T("IT'S ALL IN THE TIMING"), T("FAKE SPIKES ARE FAKE"),
  T("WHERE DID I PUT THAT COIN..."), T("CALCULATING CHANCE OF SUCCESS"), T("LOADING WILL BE FINISHED... SOON")};
static int tip;

static void loading_draw(void) {
  scene_background(rgb(0, 102, 255), 0, 0);
  gfx_layer_draw(160, 96, 256, rgb(215, 255, 100), rgb(70, 180, 10), 256);
  float t = app.t / 1.2f;
  if (t > 1) t = 1;
  int w = 200, x = 60, y = 132;
  gfx_round_rect(x - 2, y - 2, w + 4, 14, 6, 0, 180);
  gfx_round_rect(x, y, w, 10, 5, rgb(20, 20, 20), 256);
  int fw = (int)((w - 4) * t);
  if (fw > 6) {
    gfx_round_rect(x + 2, y + 2, fw, 6, 3, rgb(90, 255, 30), 256);
    gfx_add(x + 4, y + 3, fw - 4, 1, 0xffff, 90);
  }
  /* long tips wrap onto two lines at the space nearest the middle */
  const char *t0 = tips[tip];
  if (gfx_text_width(FONT_SMALL, t0) <= 300) {
    gfx_text_center(FONT_SMALL, 160, 170, t0, 0xffff, 0xffff, 256);
    return;
  }
  int n = (int)strlen(t0), cut = -1, skip = 1;
#if NP_TEXT_EXTRA
  /* (or after a Chinese letter, unless punctuation follows it) */
  for (const char *q = t0; *q;) {
    int at = (int)(q - t0), sk = 1;
    uint32_t c = np_utf8(&q);
    if (c != ' ') at = -1;
    if (c >= 0x2E80 && *q) {
      const char *r = q;
      uint32_t d = np_utf8(&r);
      if (!(d >= 0x3000 && d < 0x3040) && !(d >= 0xFF00 && d < 0xFF66)) at = (int)(q - t0), sk = 0;
    }
    if (at >= 0 && (cut < 0 || abs(at - n / 2) < abs(cut - n / 2))) cut = at, skip = sk;
  }
#else
  for (int i = 0; i < n; i++)
    if (t0[i] == ' ' && (cut < 0 || abs(i - n / 2) < abs(cut - n / 2))) cut = i;
#endif
  char line[NP_TEXT_EXTRA ? 128 : 64];
  memcpy(line, t0, (size_t)cut);
  line[cut] = 0;
  gfx_text_center(FONT_SMALL, 160, 166, line, 0xffff, 0xffff, 256);
  gfx_text_center(FONT_SMALL, 160, 180, t0 + cut + skip, 0xffff, 0xffff, 256);
}

/* ------------------------------------------------------------ dispatch */

void menu_enter(Screen s) {
  popup = POP_NONE;
  if (s == SCR_LOADING) {
    tip = (int)(frand() * (sizeof(tips) / sizeof(tips[0])));
    gfx_layer_build(FONT_HUGE, "GEOMETRY DASH");
  } else if (s == SCR_MENU) {
    memset(&menu_level, 0, sizeof(menu_level));
    menu_level.name = "";
    level_load_flat(&menu_level, level_objs, 0);
    menu_level.end_x = menu_level.wall_x = 65000;
    for (int c = 0; c < CH_COUNT; c++) memset(menu_level.colors[c], 255, 3);
    app.g.L = NULL;
    runner_reset();
    gfx_layer_build(FONT_HUGE, "GEOMETRY DASH");
    app.sel = 1;
  } else if (s == SCR_SELECT) {
    app.select_page = app.level < LEVEL_COUNT ? app.level : 0;
    app.select_scroll = (float)app.select_page;
  } else if (s == SCR_GARAGE) {
    app.row = 1;
    app.sel = progress.color1;
    app.garage_tab = 0;
  }
}

void menu_tick(void) {
  switch (app.screen) {
    case SCR_MENU:
      runner_tick();
      if (popup) { popup_tick(); break; }
      if (app.press_id >= 0) break;
      if (app_hit(K_BACK)) { app_dialog_quit(); break; }
      if (app_hit(K_LEFT)) app.sel = app.sel < 3 ? wrap(app.sel - 1, 3) : 3 + wrap(app.sel - 4, 3);
      if (app_hit(K_RIGHT)) app.sel = app.sel < 3 ? wrap(app.sel + 1, 3) : 3 + wrap(app.sel - 2, 3);
      if (app_hit(K_DOWN) && app.sel < 3) app.sel = app.sel + 3;
      if (app_hit(K_UP) && app.sel >= 3) app.sel -= 3;
      if (app_accept()) app_begin_press(app.sel);
      break;
    case SCR_SELECT: select_tick(); break;
    case SCR_GARAGE: garage_tick(); break;
    case SCR_LOADING:
      if (app.t >= 1.35f || (app.hit && app.t > 0.2f)) app_go(SCR_MENU);
      break;
    default: break;
  }
}

void menu_frame(float dt) {
  if (popup) popup_t += dt;
  if (app.screen == SCR_MENU) {
    menu_colors(dt);
    fx_update(&app.g, dt, run_c1, run_c2, false);
    if (app.press_id >= 0 && app.press_t >= 0.12f) {
      int id = app.press_id;
      app.press_id = -1;
      main_activate(id);
    }
    memset(&menu_opts, 0, sizeof(menu_opts));
    menu_opts.p1 = run_c1;
    menu_opts.p2 = run_c2;
    menu_opts.time = app.t;
    scene_prepare(&app.g, &menu_opts);
  } else if (app.screen == SCR_SELECT) {
    select_frame(dt);
  }
}

void menu_draw(void) {
  switch (app.screen) {
    case SCR_MENU:
      main_draw();
      if (popup) popup_draw();
      break;
    case SCR_SELECT: select_draw(); break;
    case SCR_GARAGE: garage_draw(); break;
    case SCR_LOADING: loading_draw(); break;
    default: break;
  }
}
