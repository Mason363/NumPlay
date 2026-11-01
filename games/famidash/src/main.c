/* Famidash on the NumWorks calculator: tfdsoft's Geometry Dash for the NES, made for the calculator.
 * The game (game.c) is Famidash's own code in C, drawn with its NES art (ppu.c). This file has its menus
 * (the title screen, the level select, the level complete screen, customize, settings and the fun
 * settings), the keys and the saves.
 *
 * Keys: OK, EXE or Up jump (held where the game holds); in practice mode Down places a checkpoint and
 * backspace removes the last one. Back pauses (Resume, Practice, Quit game, asked first). Home saves
 * and quits. The game runs at the NES's 60 frames a second whatever the calculator manages to draw. */
#include <stdio.h>
#include <string.h>
#include "fd.h"
#include "gfx.h"
#include "plat.h"

#define SAVE_NAME "famidash.sav"
#define COPY_NAME "famidash_saves.py"
#define MAGIC 0x31444D46u /* "FMD1" */
#define VERSION 1

/* ---------------------------------------------------------------- the save */
typedef struct {
  uint32_t magic;
  uint8_t version, flags, icon, retro; /* flags: 1 how to play read */
  uint8_t colors[3], plat;
  uint8_t viseffects, trails, autocp, invisible;
  uint8_t disco, invisblocks, seesaw, gameboy;
  uint8_t last_level, pad[3];
  progress_t prog;
  uint32_t play_ms;
  uint32_t sum;
} save_t;
static save_t sv;

static uint32_t fnv(const void *d, uint32_t n) {
  const uint8_t *p = d;
  uint32_t h = 2166136261u;
  for (uint32_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}
static bool save_ok(const save_t *s) {
  if (s->magic != MAGIC || s->version != VERSION || s->sum != fnv(s, offsetof(save_t, sum))) return false;
  if (s->icon >= MAX_ICONS || s->last_level >= NLEVELS) return false;
  for (int i = 0; i < 3; i++)
    if (s->colors[i] > 0x3F) return false;
  if (s->disco & ~0x1F || s->gameboy > 8) return false;
  for (int i = 0; i < NLEVELS; i++)
    if (s->prog.normal[i] > 100 || s->prog.practice[i] > 100 || s->prog.inormal[i] > 100 || s->prog.ipractice[i] > 100)
      return false;
  return true;
}
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char head[] = "# Famidash keeps a copy of your game here,\n# so that installing it again doesn't erase it.\n#>" SAVE_NAME ":";
static uint8_t copy[1 + sizeof head + (sizeof(save_t) + 2) / 3 * 4 + 2];

static void sync_out(void) { /* the game's settings into the save */
  sv.icon = icon, sv.retro = retro_mode, memcpy(sv.colors, icon_colors, 3), sv.plat = options & platformer ? 1 : 0;
  sv.viseffects = viseffects, sv.trails = trails, sv.autocp = auto_practicepoints, sv.invisible = invisible;
  sv.disco = discomode, sv.invisblocks = invisblocks, sv.seesaw = cam_seesaw ? 1 : 0, sv.gameboy = gameboy_mode;
  sv.last_level = level < NLEVELS ? level : 0;
  sv.prog = prog;
}
static void sync_in(void) {
  icon = sv.icon, retro_mode = sv.retro, memcpy(icon_colors, sv.colors, 3), options = sv.plat ? platformer : 0;
  viseffects = sv.viseffects, trails = sv.trails, auto_practicepoints = sv.autocp, invisible = sv.invisible;
  discomode = sv.disco, invisblocks = sv.invisblocks, cam_seesaw = sv.seesaw, gameboy_mode = sv.gameboy;
  level = sv.last_level;
  prog = sv.prog;
}
static void save(void) {
  sync_out();
  sv.sum = fnv(&sv, offsetof(save_t, sum));
  plat_save(SAVE_NAME, &sv, sizeof sv);
  uint8_t *p = copy;
  const uint8_t *f = (const uint8_t *)&sv;
  *p++ = 0; /* (a script's first byte: not imported) */
  memcpy(p, head, sizeof head - 1), p += sizeof head - 1;
  for (uint32_t i = 0; i < sizeof sv; i += 3, p += 4) {
    uint32_t left = sizeof sv - i;
    uint32_t v = (uint32_t)f[i] << 16 | (left > 1 ? (uint32_t)f[i + 1] << 8 : 0) | (left > 2 ? f[i + 2] : 0);
    p[0] = (uint8_t)b64[v >> 18 & 63], p[1] = (uint8_t)b64[v >> 12 & 63];
    p[2] = left > 1 ? (uint8_t)b64[v >> 6 & 63] : '=', p[3] = left > 2 ? (uint8_t)b64[v & 63] : '=';
  }
  *p++ = '\n';
  plat_save(COPY_NAME, copy, (uint32_t)(p - copy));
}
static int b64v(uint8_t c) {
  for (int i = 0; i < 64; i++)
    if ((uint8_t)b64[i] == c) return i;
  return c == '=' ? 0 : -1;
}
static bool copy_read(save_t *out) {
  uint32_t n;
  const uint8_t *c = plat_load(COPY_NAME, &n);
  if (!c) return false;
  const uint32_t hl = sizeof head - 1, m = (sizeof(save_t) + 2) / 3 * 4;
  for (uint32_t i = 0; i + hl + m <= n; i++) {
    if (memcmp(c + i, head, hl)) continue;
    const uint8_t *l = c + i + hl;
    static uint8_t buf[(sizeof(save_t) + 2) / 3 * 3];
    for (uint32_t k = 0; k < m; k += 4) {
      int v0 = b64v(l[k]), v1 = b64v(l[k + 1]), v2 = b64v(l[k + 2]), v3 = b64v(l[k + 3]);
      if (v0 < 0 || v1 < 0 || v2 < 0 || v3 < 0) return false;
      uint32_t w = (uint32_t)(v0 << 18 | v1 << 12 | v2 << 6 | v3);
      buf[k / 4 * 3] = (uint8_t)(w >> 16), buf[k / 4 * 3 + 1] = (uint8_t)(w >> 8), buf[k / 4 * 3 + 2] = (uint8_t)w;
    }
    memcpy(out, buf, sizeof *out);
    return save_ok(out);
  }
  return false;
}
static void fresh(void) {
  memset(&sv, 0, sizeof sv);
  sv.magic = MAGIC, sv.version = VERSION;
  sv.colors[0] = 0x2A, sv.colors[1] = 0x2C, sv.colors[2] = 0x0F; /* setdefaultoptions */
  sv.viseffects = 1, sv.autocp = 1;
}
static void load(void) {
  uint32_t n;
  const uint8_t *d = plat_load(SAVE_NAME, &n);
  static save_t t;
  if (d && n == sizeof t) {
    memcpy(&t, d, sizeof t);
    if (save_ok(&t)) { sv = t; sync_in(); return; }
  }
  if (copy_read(&t)) { sv = t; sync_in(); return; }
  fresh();
  sync_in();
}

/* ---------------------------------------------------------------- screens */
enum { S_TITLE, S_LEVELS, S_PLAY, S_DONE, S_CUSTOM, S_SETTINGS, S_FUN, S_HELP, S_CREDITS };
static int screen = S_TITLE, help_back = S_TITLE, help_page;
static uint32_t frame;     /* 60 a second */
static uint32_t now_ms;
static uint16_t keys, hit; /* calculator keys held, and newly pressed this step */
extern uint8_t titleicon;
void fd_draw_title_player(uint8_t x, uint8_t y, uint8_t gm, uint8_t mini);
const unsigned char *fd_number_sprite(int i);
const unsigned char *fd_metasprite(int t);
extern int32_t text_x, text_y;
extern uint8_t text_tiles[16], text_n, nt_fill_tile, nt_wrap_split;

#define NT(nt, x, y) nametable[nt][(y) * 32 + (x)]
static void nt_load(const uint8_t *src) { memcpy(nametable[0], src, 1024), memcpy(nametable[1], src, 1024); }
static void nt_put(int n, int x, int y, uint8_t t) {
  if (n == 2) NT(0, x, y) = t, NT(1, x, y) = t;
  else NT(n, x, y) = t;
}
static void nt_text(int n, int x, int y, const char *s, const uint8_t *cmap) {
  for (; *s; s++, x++) nt_put(n, x, y, cmap[(uint8_t)*s & 0x7F]);
}
/* draw_padded_text: centred in a block of len tiles */
static void nt_padded(int n, int x, int y, const char *s, int len, const uint8_t *cmap) {
  int l = (int)strlen(s), pad = (len - l) / 2;
  for (int i = 0; i < len; i++) nt_put(n, x + i, y, cmap[' ']);
  nt_text(n, x + pad, y, s, cmap);
}

/* the NES screens' "PRESS B ..." lines, in the calculator's words */
static void nt_line(int y, const char *s) {
  const uint8_t *m = cmap_mainmenu_charmap;
  for (int x = 3; x <= 28; x++) nt_put(2, x, y, m[' ']);
  nt_text(2, 16 - (int)(strlen(s) + 1) / 2, y, s, m);
}

static void menu_banks(uint8_t b) { /* mmc3_set_8kb_chr */
  chr_bg[0] = b, chr_bg[1] = (uint8_t)(b + 1), chr_bg[2] = (uint8_t)(b + 2), chr_bg[3] = (uint8_t)(b + 3);
  chr_spr[0] = (uint8_t)(b + 5), chr_spr[1] = (uint8_t)(b + 7);
}

/* fades: pal_fade_to, 2 frames a step */
static int fade_dir, fade_t;
static void fade_in(void) { fade_dir = 1, fade_t = 0, pal_bright_level = 0; }
static void fade_out(void) { fade_dir = -1, fade_t = 0; }
static bool fading(void) { return fade_dir != 0; }
static void fade_tick(void) {
  if (!fade_dir) return;
  if (++fade_t < 2) return;
  fade_t = 0;
  if (fade_dir > 0) { if (pal_bright_level < 4) pal_bright_level++; else fade_dir = 0; }
  else if (pal_bright_level > 0) pal_bright_level--;
  else fade_dir = 0;
}
static int next_screen = -1; /* after a fade out */
static void go(int s) {
  next_screen = s;
  fade_out();
}
static void enter(int s);

/* ---------------------------------------------------------------- the title screen (titlescreen.c) */
enum { BTN_MAIN, BTN_COMM, BTN_SOUND, BTN_SETTINGS, BTN_HELP, BTN_FUN, BTN_CUSTOM };
static const uint8_t btn_x[7] = {15, 21, 9, 15, 21, 27, 9}, btn_y[7] = {11, 11, 17, 17, 17, 1, 11};
static uint8_t menusel, title_sel_drawn = 0xFF, tmp8_scroll, title_speed, title_mode, title_x, title_y, title_tele;
static int8_t title_tmpi8;
static uint8_t title_grav, title_ninja, title_mini, title_charge;
static uint8_t discoframe_t;
static const uint8_t BG_Table2[] = {0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C};
static const int8_t UFO_Title_Jump_Table[] = {5, 4, 3, 3, 2, 2, 2, 2, 1, 1, 1, 1, 1, 0, -1, -1, -1, -1, -1, -2, -2, -2, -2, -3, -3, -4, -5};
static const int8_t POGO_Title_Jump_Table[] = {6, 6, 5, 5, 4, 4, 3, 3, 2, 2, 2, 2, 1, 1, 1, 1, 1, 0, -1, -1, -1, -1, -1, -2, -2, -2, -2,
                                               -3, -3, -4, -4, -5, -5, -6, -6};
enum { TM_CUBE, TM_SHIP, TM_BALL, TM_UFO, TM_ROBOT, TM_SPIDER, TM_WAVE, TM_SWING, TM_NINJA, TM_POGO, TM_SNAKE, TM_FOOTBALL };

static void unlocked_check(void) {
  all_levels_complete = 0xFC;
  for (int i = 0; i <= 12; i++)
    if (!prog.complete[i]) all_levels_complete = 0;
}

static void set_title_icon(void) {
  int m = title_mode % 12;
  if (m == TM_CUBE || m == TM_SHIP || m == TM_UFO) {
    uint8_t t;
    do t = newrand() & 31;
    while (t >= MAX_ICONS);
    titleicon = t;
    chr_spr[0] = retro_mode ? 18 : (uint8_t)(40 + t * 2);
  } else if (m == TM_NINJA) chr_spr[0] = retro_mode ? 22 : 92;
  else if (m == TM_FOOTBALL) chr_spr[0] = 94;
  else if (m == TM_ROBOT || m == TM_SPIDER || m == TM_BALL) chr_spr[0] = retro_mode ? 22 : 20;
  else chr_spr[0] = retro_mode ? 26 : 24;
  if (title_mini && (m == TM_SHIP || m == TM_UFO || m == TM_WAVE || m == TM_SWING || m == TM_POGO || m == TM_SPIDER))
    chr_spr[0] = retro_mode ? 26 : 24;
}

static void roll_new_mode(void) {
  title_speed = newrand() & 3;
  if (!title_speed) title_speed = 1;
  title_grav = 0, title_x = 8, title_y = 0xA0, title_tmpi8 = 0, title_tele = 0xFF, title_charge = 0;
  uint8_t old = title_mode;
  do title_mode = newrand() & 31;
  while (title_mode == 22 || title_mode == 10 || title_mode > 23 || title_mode == old);
  if (retro_mode && (title_mode == 0 || title_mode == 12)) title_mode = old;
  title_mini = title_mode >= 12;
  int m = title_mode % 12;
  if (m == TM_UFO || m == TM_SHIP || m == TM_WAVE) {
    uint8_t r;
    do r = newrand();
    while (r > 0xA0 || r < 0x20);
    title_y = r;
  }
  set_title_icon();
}

static void title_bounds(void) {
  uint8_t floor_y = title_mini ? 164 : 160;
  if (title_y >= floor_y) title_y = floor_y, title_ninja = 3;
  else if (title_y <= 8) title_y = 8;
}

static void title_move(void) {
  int m = title_mode % 12;
  uint8_t floor_y = title_mini ? 164 : 160;
  switch (m) {
  case TM_CUBE: case TM_NINJA: case TM_ROBOT:
    if (title_tele <= 0x1A) title_y = (uint8_t)(title_y - UFO_Title_Jump_Table[title_tele]), title_tele++;
    else title_y = (uint8_t)(title_y + 4);
    if (title_y >= floor_y && title_y < 0xF0) title_y = floor_y;
    if ((title_y == floor_y || (m == TM_NINJA && title_ninja)) && !(newrand() & 15)) title_tele = 0, title_ninja--;
    else if (title_y < 8 || title_y >= 0xF0) title_y = 8, title_tele = 0x0E;
    else if (title_y == floor_y) title_ninja = 3;
    break;
  case TM_UFO:
    if (title_tele <= 0x1A) title_y = (uint8_t)(title_y - UFO_Title_Jump_Table[title_tele]), title_tele++;
    else title_y = (uint8_t)(title_y + 4);
    if (!(newrand() & 15)) title_tele = 0;
    title_bounds();
    break;
  case TM_SHIP:
    if (frame & 1 && !(newrand() & 7)) title_grav ^= 0xFF;
    title_y = (uint8_t)(title_y - title_tmpi8);
    if (title_y >= 160 && title_y < 0xF0) title_y = 160;
    else if (title_y < 8 || title_y >= 0xF0) title_y = 8, title_grav = 0, title_tmpi8 = 0;
    if (title_grav) { if (!(frame & 7) && title_tmpi8 < 3) title_tmpi8++; }
    else if (!(frame & 7) && title_tmpi8 > -3) title_tmpi8--;
    break;
  case TM_SWING:
    if (frame & 3 && !(newrand() & 15)) title_grav ^= 0xFF;
    if ((frame & 3) == 0) {
      if (title_grav) { if (--title_tmpi8 <= -3) title_tmpi8 = -3; }
      else if (++title_tmpi8 >= 3) title_tmpi8 = 3;
    }
    title_y = (uint8_t)(title_y + title_tmpi8);
    title_bounds();
    break;
  case TM_WAVE: {
    uint8_t r = newrand() & 63;
    if (frame & 1 && r >= 60) title_grav ^= 0xFF;
    uint8_t d = (uint8_t)(title_mini ? title_speed << 1 : title_speed);
    title_y = (uint8_t)(title_grav ? title_y - d : title_y + d);
    title_bounds();
    break;
  }
  case TM_POGO:
    if (title_tele <= 0x22) title_y = (uint8_t)(title_y - POGO_Title_Jump_Table[title_tele]), title_tele++;
    else title_y = (uint8_t)(title_y + 5);
    if (title_y >= floor_y && title_y < 0xF0) title_tele = 0;
    title_bounds();
    break;
  case TM_FOOTBALL:
    if (title_charge <= 50 && title_charge) {
      if (newrand() < 0xFB) title_charge++;
      else title_charge = 0;
    } else if (!title_charge) {
      if (newrand() < 0x08) title_charge++;
    } else title_charge = 0;
    title_bounds();
    break;
  default: title_bounds(); break;
  }
}

static void title_sel_draw(void) {
  if (title_sel_drawn == menusel) return;
  if (title_sel_drawn != 0xFF) {
    if (title_sel_drawn == BTN_FUN) nt_put(2, 26, 2, cmap_mainmenu_charmap[' ']), nt_put(2, 26, 3, cmap_mainmenu_charmap[' ']);
    else nt_put(2, btn_x[title_sel_drawn], btn_y[title_sel_drawn], cmap_mainmenu_charmap[' ']),
         nt_put(2, btn_x[title_sel_drawn] + 1, btn_y[title_sel_drawn], cmap_mainmenu_charmap[' ']);
  }
  if (menusel == BTN_FUN) nt_put(2, 26, 2, 0x6F), nt_put(2, 26, 3, 0x7F);
  else nt_put(2, btn_x[menusel], btn_y[menusel], 0x5E), nt_put(2, btn_x[menusel] + 1, btn_y[menusel], 0x5F);
  title_sel_drawn = menusel;
}

static void enter_title(void) {
  bg_mode = BG_NAMETABLE;
  nt_wrap_split = 1;
  menu_banks(98);
  chr_spr[1] = 96;
  unlocked_check();
  gameState = STATE_MENU;
  ppu_set_palette(all_levels_complete == 0xFC ? pal_splashMenu2 : pal_splashMenu, pal_paletteDefaultSP);
  pal_col(0x1D, icon_colors[2]), pal_col(0x1E, icon_colors[0]), pal_col(0x1F, icon_colors[1]);
  nt_load(nt_title);
  /* the sound test's button: no sound on the calculator, so it goes (its tiles become the backdrop's) */
  for (int y = 15; y <= 19; y++)
    for (int x = 7; x <= 12; x++) nt_put(2, x, y, NT(0, 3, 15));
  if (all_levels_complete != 0xFC) nt_put(2, 27, 2, 0x19), nt_put(2, 28, 2, 0x1A), nt_put(2, 27, 3, 0x2D), nt_put(2, 28, 3, 0x4D);
  else nt_put(2, 27, 2, 0x1B), nt_put(2, 28, 2, 0x1C), nt_put(2, 27, 3, 0x1D), nt_put(2, 28, 3, 0x1E);
  title_sel_drawn = 0xFF;
  if (menusel == BTN_SOUND || menusel > BTN_CUSTOM) menusel = BTN_MAIN;
  title_sel_draw();
  nt_scroll_x = 0, nt_scroll_y = 0, nt_split_y0 = 181, nt_split_y1 = 240, nt_split_x = 0;
  do discoframe_t = newrand() & 15;
  while (discoframe_t > 11);
  roll_new_mode();
}

static void color_picker(void) {
  uint8_t t = (uint8_t)(0x80 + BG_Table2[discoframe_t]);
  if (t < 0x80) t += 0x80;
  else if (t >= 0xF0) t -= 0x80;
  pal_col(0, t & 0x3F), pal_col(0x11, t & 0x3F);
  if (++discoframe_t == 12) discoframe_t = 0;
}

static void title_tick(void) {
  pal_col(0x11, icon_colors[2]), pal_col(0x12, icon_colors[0]), pal_col(0x13, icon_colors[1]);
  newrand(), newrand(), newrand();
  title_x = (uint8_t)(title_x + title_speed);
  if (title_x >= 0xFB) roll_new_mode();
  title_move();
  if (!(frame & 127)) color_picker();
  if (fading()) return;
  static const int8_t order[7] = {BTN_CUSTOM, BTN_MAIN, BTN_COMM, BTN_SETTINGS, BTN_HELP, BTN_FUN, -1};
  if (hit & (PK_RIGHT | PK_LEFT)) { /* the NES order, skipping the sound test */
    int i = 0;
    while (order[i] != menusel && order[i] >= 0) i++;
    if (order[i] < 0) i = 1;
    i = (i + ((hit & PK_RIGHT) ? 1 : 5)) % 6;
    menusel = (uint8_t)order[i];
  }
  if (hit & (PK_UP | PK_DOWN)) {
    if (menusel == BTN_CUSTOM || menusel == BTN_MAIN) menusel = (hit & PK_DOWN) ? BTN_SETTINGS : BTN_FUN;
    else if (menusel == BTN_COMM) menusel = (hit & PK_DOWN) ? BTN_HELP : BTN_FUN;
    else if (menusel == BTN_SETTINGS) menusel = (hit & PK_UP) ? BTN_MAIN : BTN_SETTINGS;
    else if (menusel == BTN_HELP) menusel = (hit & PK_UP) ? BTN_COMM : BTN_HELP;
    else if (menusel == BTN_FUN && (hit & PK_DOWN)) menusel = BTN_COMM;
  }
  title_sel_draw();
  tmp8_scroll = (uint8_t)(tmp8_scroll + MSB(cube_speed[1]));
  nt_split_x = tmp8_scroll;
  if (hit & (PK_OK | PK_EXE)) {
    switch (menusel) {
    case BTN_MAIN: level = level < NOFFICIAL ? level : 0; go(S_LEVELS); break;
    case BTN_COMM: level = level >= NOFFICIAL ? level : NOFFICIAL; go(S_LEVELS); break;
    case BTN_SETTINGS: go(S_SETTINGS); break;
    case BTN_HELP: help_back = S_TITLE, help_page = 0; go(S_HELP); break;
    case BTN_FUN:
      if (all_levels_complete == 0xFC) go(S_FUN);
      break;
    case BTN_CUSTOM: go(S_CUSTOM); break;
    }
  }
  if (hit & PK_BACK) help_back = S_TITLE, go(S_CREDITS);
}

static void title_sprites(void) {
  oam_clear();
  if (title_x > 0xF7) return;
  int m = title_mode % 12;
  uint8_t gm = (uint8_t)m;
  if (m == TM_SNAKE) gm = 6;
  player_vel_y[0] = (title_y == (title_mini ? 164 : 160) || title_y == 8) ? 0 : 1;
  if (m == TM_SHIP || m == TM_WAVE || m == TM_SWING) player_vel_y[0] = (int16_t)(-title_tmpi8 * 0x100);
  fd_draw_title_player(title_x, title_y, gm, title_mini);
}

/* ---------------------------------------------------------------- the level select (levelselection.c) */
static uint16_t lsel_tmp8;
static uint8_t lsel_tmp4, lsel_hold, lsel_start;
static const uint8_t colors_list[9] = {0x12, 0x14, 0x15, 0x16, 0x17, 0x28, 0x2A, 0x2C, 0x11};
static const uint8_t difficulty_pal_A[7] = {0x21, 0x2A, 0x28, 0x16, 0x24, 0x16, 0x28};
static const uint8_t difficulty_pal_B[7] = {0x06, 0x30, 0x30, 0x30, 0x06, 0x30, 0x21};
static const char *const coin_counter[8] = {"___", "^__", "_^_", "^^_", "__^", "^_^", "_^^", "^^^"};

static uint8_t pct_normal(int l) { return invisblocks ? prog.inormal[l] : prog.normal[l]; }
static uint8_t pct_practice(int l) { return invisblocks ? prog.ipractice[l] : prog.practice[l]; }

static void draw_progress_bar_bg(int nt, int x, int y, uint8_t pct) {
  if (pct >= 5) {
    nt_put(nt, x, y, 0x8c);
    int t = pct >= 100 ? 95 : pct, n = 0;
    do t -= 5, n++;
    while (t >= 5);
    for (int i = 0; i < n; i++) nt_put(nt, x + 1 + i, y, 0x6b);
    for (int i = 0; i < 20 - n; i++) nt_put(nt, x + n + i, y, 0x02);
  } else {
    nt_put(nt, x, y, 0x7c);
    for (int i = 0; i < 18; i++) nt_put(nt, x + 1 + i, y, 0x02);
  }
  nt_put(nt, x + 19, y, pct >= 100 ? 0x8d : 0x7d);
}

static void refreshmenu(void) {
  int n = level & 1;
  const level_t *L = &level_list[level];
  if (invisblocks) nt_text(2, 12, 9, "INVISIBLE", cmap_color1_charmap);
  if (L->up[0]) nt_padded(n, 8, 10, L->up, 17, cmap_color1_charmap);
  else for (int i = 0; i < 17; i++) nt_put(n, 8 + i, 10, cmap_color1_charmap[' ']);
  nt_padded(n, 8, 11, L->lo, 17, cmap_color1_charmap);
  if (invisblocks ? prog.icomplete[level] : prog.complete[level]) nt_put(n, 7, 9, cmap_color1_charmap['y']), nt_put(n, 8, 9, cmap_color1_charmap['z']);
  else nt_put(n, 7, 9, cmap_color1_charmap[' ']), nt_put(n, 8, 9, cmap_color1_charmap[' ']);
  uint8_t d = L->difficulty;
  pal_col(0x0a, difficulty_pal_A[d]), pal_col(0x0b, difficulty_pal_B[d]);
  uint8_t t = (uint8_t)(cmap_color1_charmap['a'] + d * 2);
  nt_put(n, 7, 10, t), nt_put(n, 8, 10, (uint8_t)(t + 1)), nt_put(n, 7, 11, (uint8_t)(t + 0x10)), nt_put(n, 8, 11, (uint8_t)(t + 0x11));
  char st[4];
  snprintf(st, sizeof st, "%2d", L->stars);
  nt_text(n, 22, 9, st, cmap_color1_charmap);
  uint8_t c = colors_list[level % 9];
  pal_col(0, c), pal_col(0xE, c);
  memcpy(PAL_BUF_RAW + 16, pal_paletteLVLSelectSP, 16);
  pal_col(0x10, c), pal_col(0x1E, c);
  int k = (invisblocks ? (prog.icoin3[level] << 2 | prog.icoin2[level] << 1 | prog.icoin1[level])
                       : (prog.coin3[level] << 2 | prog.coin2[level] << 1 | prog.coin1[level])) & 7;
  nt_text(n, 22, 12, coin_counter[k], cmap_color1_charmap);
  draw_progress_bar_bg(n, 6, 16, pct_normal(level));
  draw_progress_bar_bg(n, 6, 19, pct_practice(level));
}

static void put_number(int x, int y, int i) { oam_meta_spr(x, y, fd_number_sprite(i)); }
static void draw_percentage(int x, int y, uint8_t v) {
  int d2 = v / 100, d1 = v / 10 % 10, d0 = v % 10;
  if (d2) put_number(x, y, d2);
  if (d2 || d1) x += 8, put_number(x, y, d1);
  x += 8, put_number(x, y, d0);
  x += 8, put_number(x, y, 10);
}
static int lsel_off(void) { return lsel_tmp4 ? (uint8_t)(lsel_tmp8 + 1) : -(int)(lsel_tmp8 & 0xFF); }
static void draw_bar_sprites(int row, uint8_t pct, int idx) {
  int off = lsel_off(), x = 5 * 8 + 1, y = row * 8 - 1;
  put_number((uint8_t)(x + off), y, 22);
  put_number((uint8_t)(x + 1 + off), y, 25);
  put_number((uint8_t)(206 + off), y, 26);
  uint16_t p = (uint16_t)(x << 8);
  if (pct) {
    uint8_t t3 = 1;
    do t3++, p = (uint16_t)(p + 409);
    while (t3 < pct);
  }
  put_number((uint8_t)((p >> 8) + off), y, 22 + idx);
}
static void draw_both_progress_bars(void) {
  int off = lsel_off();
  draw_percentage((uint8_t)(112 + off), 127, pct_normal(level));
  draw_bar_sprites(16, pct_normal(level), 1);
  draw_percentage((uint8_t)(112 + off), 151, pct_practice(level));
  draw_bar_sprites(19, pct_practice(level), 2);
}

static void enter_levels(void) {
  bg_mode = BG_NAMETABLE;
  menu_banks(98);
  chr_spr[0] = 0xFE, chr_spr[1] = 96;
  ppu_set_palette(pal_oldsplashMenu, NULL);
  nt_load(nt_levelsel);
  practice_point_count = 0;
  lsel_tmp8 = 0xff00, lsel_tmp4 = 1, lsel_start = 0, lsel_hold = 0;
  nt_scroll_y = 0, nt_split_y0 = 31, nt_split_y1 = 149;
  refreshmenu();
  nt_scroll_x = ((level - lsel_tmp4) & 1) << 8;
  nt_split_x = nt_scroll_x + (lsel_tmp4 ? (lsel_tmp8 >> 8) : (lsel_tmp8 & 0xFF));
}

static void lsel_change(int d) {
  int lo = level < NOFFICIAL ? 0 : NOFFICIAL, hi = level < NOFFICIAL ? NOFFICIAL : NLEVELS;
  int l = level + d;
  if (l < lo) l = hi - 1;
  if (l >= hi) l = lo;
  level = (uint8_t)l;
  lsel_tmp8 = (uint16_t)((lsel_tmp8 & 0xFF00) | 0xFF);
  lsel_tmp4 = d > 0;
  refreshmenu();
  nt_scroll_x = ((level - lsel_tmp4) & 1) << 8;
}

static void levels_tick(void) {
  if (lsel_start) { /* start_the_level: 30 frames */
    if (lsel_start <= 30 && ++lsel_start > 30) { game_begin(); go(S_PLAY); }
    goto scroll;
  }
  if (!fading()) {
    if (hit & (PK_OK | PK_EXE)) lsel_start = 1;
    else if (hit & PK_BACK) go(S_TITLE);
    if (hit & (PK_LEFT | PK_RIGHT)) lsel_hold = 0;
    if (!lsel_start && ((hit & PK_RIGHT) || ((keys & PK_RIGHT) && lsel_hold >= 15))) lsel_change(1), lsel_hold = 0;
    if (!lsel_start && ((hit & PK_LEFT) || ((keys & PK_LEFT) && lsel_hold >= 15))) lsel_change(-1), lsel_hold = 0;
    if (lsel_hold < 15) lsel_hold++;
  }
scroll:
  nt_split_x = nt_scroll_x + (lsel_tmp4 ? (lsel_tmp8 >> 8) : (lsel_tmp8 & 0xFF));
  uint8_t lo = (uint8_t)lsel_tmp8;
  if (lo > 0) lo = (uint8_t)(lo - ((lo >> 2) + 1));
  lsel_tmp8 = (uint16_t)((uint8_t)(lo ^ 0xFF) << 8 | lo);
}

/* ---------------------------------------------------------------- playing */
enum { P_FADEIN, P_RUN, P_DYING, P_FADEOUT, P_PAUSE, P_QUIT };
static int phase, phase_t, pause_sel, auto_cp_seen;
extern uint8_t current_deco_type, current_spike_set, current_block_set, current_saw_set;
static uint8_t spr_bank0, spr_bank1 = 28;

static void game_banks(void) {
  uint8_t p = parallax_scroll_x & 1;
  chr_bg[0] = (uint8_t)(current_spike_set + p);
  chr_bg[1] = (uint8_t)(current_block_set + p);
  chr_bg[3] = current_saw_set == 111 ? 111 : (uint8_t)(current_saw_set + p);
  if (no_parallax) {
    uint8_t s = current_block_set == 12 ? 90 : current_block_set == 8 ? 88 : current_block_set == 80 ? 84 : current_block_set == 82 ? 86 : 16;
    chr_bg[2] = (uint8_t)(s + p);
  } else chr_bg[2] = (uint8_t)(112 + parallax_scroll_x);
  uint8_t ib1 = retro_mode ? 22 : 20, ib2 = retro_mode ? 26 : 24, ib3 = retro_mode ? 18 : (uint8_t)(40 + icon * 2);
  uint8_t gm = gamemode, mini = player_mini[0];
  if (gm == GAMEMODE_NINJA && !retro_mode) spr_bank0 = 92;
  else if ((mini && gm != GAMEMODE_CUBE && gm != GAMEMODE_BALL && gm != GAMEMODE_ROBOT && gm != GAMEMODE_FOOTBALL) ||
           gm == GAMEMODE_SWING || gm == GAMEMODE_WAVE || gm == GAMEMODE_SNAKE || gm == GAMEMODE_POGO) spr_bank0 = ib2;
  else if (gm == GAMEMODE_CUBE || gm == GAMEMODE_SHIP || gm == GAMEMODE_UFO) spr_bank0 = ib3;
  else if (gm == GAMEMODE_FOOTBALL) spr_bank0 = 94;
  else spr_bank0 = ib1;
  if ((kandoframecnt & 0x1F) == 0x10) spr_bank1 = (uint8_t)(current_deco_type + 2);
  else if ((kandoframecnt & 0x1F) == 0x00) spr_bank1 = current_deco_type;
  chr_spr[0] = spr_bank0, chr_spr[1] = spr_bank1;
}

/* "ATTEMPT 12": put into the first screen's tiles, as unrle_first_screen does */
static void attempt_text(void) {
  static const uint8_t att[7] = {0xF0, 0xF1, 0xF1, 0xF2, 0xF3, 0xF4, 0xF1};
  memset(text_tiles, 0, sizeof text_tiles);
  int digits = 7;
  while (digits > 1 && !attemptCounter[digits - 1]) digits--;
  for (int i = 0; i < 7; i++) text_tiles[i] = att[i];
  for (int i = 0; i < digits; i++) text_tiles[14 - i] = (uint8_t)(0xF5 + attemptCounter[i]);
  text_n = 15;
  int32_t sx = (int32_t)scroll_x, x = (sx & ~0x1FF) + 48;
  if (x < sx - 32) x += 512;
  /* row 15 of the lower nametable: 121 lines under the top of the level's first view */
  text_x = x, text_y = (scroll_y + 121) & ~7;
  if (x + 15 * 8 > sx + 256 + 32) text_n = 0;
}

static uint32_t level_frame; /* when the level was chosen */
static void play_start(void) {
  if (attemptCounter[0] + attemptCounter[1] + attemptCounter[2] == 0) level_frame = frame;
  game_start();
  attempt_text();
  phase = P_FADEIN, phase_t = 0;
  fade_in();
}

static void play_input(void) {
  uint8_t h = 0;
  if (keys & (PK_OK | PK_EXE)) h |= PAD_A;
  if (keys & PK_UP) h |= PAD_UP;
  if (keys & PK_LEFT) h |= PAD_LEFT;
  if (keys & PK_RIGHT) h |= PAD_RIGHT;
  if (keys & PK_DOWN) h |= PAD_B;
  if (keys & PK_DEL) h |= PAD_SELECT;
  joypad1.press = (uint8_t)(h & ~joypad1.hold);
  joypad1.release = (uint8_t)(~h & joypad1.hold);
  joypad1.hold = h;
}

static void enter_done(void);
static void play_tick(void) {
  switch (phase) {
  case P_FADEIN:
    play_input();
    if (!fading()) phase = P_RUN;
    return;
  case P_RUN:
    if (hit & PK_BACK) { phase = P_PAUSE, pause_sel = 0; return; }
    play_input();
    game_step();
    game_banks();
    if (gameState == STATE_LVLDONE) {
      set_completion_data();
      save();
      enter_done();
      return;
    }
    if (game_dead()) {
      phase = P_DYING, phase_t = 0;
      game_death_frame(0);
      return;
    }
    game_draw();
    return;
  case P_DYING:
    play_input();
    if (++phase_t < 30) { game_death_frame(phase_t); return; }
    phase = P_FADEOUT;
    fade_out();
    return;
  case P_FADEOUT:
    if (fading()) return;
    game_reset();
    attempt_text();
    game_banks();
    game_draw();
    phase = P_FADEIN;
    fade_in();
    return;
  case P_PAUSE:
  case P_QUIT:
    joypad1.press = 0;
    if (hit & PK_BACK) { phase = P_RUN; return; }
    if (hit & PK_UP) pause_sel = (pause_sel + 2) % 3, phase = P_PAUSE;
    if (hit & PK_DOWN) pause_sel = (pause_sel + 1) % 3, phase = P_PAUSE;
    if (hit & (PK_OK | PK_EXE)) {
      if (pause_sel == 0) phase = P_RUN;
      else if (pause_sel == 1) {
        if (!practice_point_count) game_store_practice();
        else {
          /* practice off: the level again from its start */
          practice_point_count = 0;
          phase = P_FADEOUT;
          fade_out();
          return;
        }
        phase = P_RUN;
      } else if (phase == P_QUIT) {
        save();
        go(S_LEVELS);
      } else phase = P_QUIT;
    }
    return;
  }
}

/* ---------------------------------------------------------------- the level complete screen (state_lvldone.h) */
static int done_state, done_tmp1, done_coin;
static uint16_t done_tmp5, done_tmp6;
static uint8_t done_sel;
static void enter_done(void) {
  screen = S_DONE;
  bg_mode = BG_NAMETABLE;
  oam_clear();
  chr_bg[0] = 106, chr_bg[1] = 110, chr_bg[2] = 108, chr_bg[3] = 109, chr_spr[1] = 96;
  pal_bright_level = 4, fade_dir = 0;
  ppu_set_palette(pal_paletteMenu, pal_paletteDefaultSP);
  pal_col(0x0A, 0x2A), pal_col(0x0B, 0x21);
  memcpy(nametable[0], practice_point_count ? nt_practicedone : nt_leveldone, 1024);
  memcpy(nametable[1], nametable[0], 1024);
  nt_fill_tile = 0xFE;
  /* attempts (right-aligned at 20) and jumps (from 18) */
  int digits = 7;
  while (digits > 1 && !attemptCounter[digits - 1]) digits--;
  for (int i = 0; i < digits; i++) nt_put(0, 20 - i, 13, (uint8_t)(0xD0 + attemptCounter[i]));
  char j[8];
  snprintf(j, sizeof j, "%u", (unsigned)jumps);
  for (int i = 0; j[i]; i++) nt_put(0, 18 + i, 15, (uint8_t)(0xD0 + j[i] - '0'));
  done_state = 0, done_tmp5 = 0, done_tmp6 = 0xf000, done_tmp1 = 0, done_coin = 0, done_sel = 1;
  nt_scroll_x = 0, nt_scroll_y = 0xe8, nt_split_y0 = nt_split_y1 = -1;
}
static void lvl_done_update(void) {
  nt_put(0, 8, 23, done_sel ? 0xFF : 0x94), nt_put(0, 9, 23, done_sel ? 0xFF : 0x95);
  nt_put(0, 22, 23, done_sel ? 0x94 : 0xFF), nt_put(0, 23, 23, done_sel ? 0x95 : 0xFF);
}
static void done_tick(void) {
  kandoframecnt++;
  switch (done_state) {
  case 0: case 1: case 2: {
    done_tmp5 = (uint16_t)(done_tmp5 + 0x40);
    done_tmp6 = (uint16_t)(done_tmp6 - done_tmp5);
    nt_scroll_y = done_tmp6 >> 8;
    if (done_state == 0 && (done_tmp6 >> 8) < 10) done_state = 1, done_tmp5 = (uint16_t)((uint8_t)-0x06 << 8 | (done_tmp5 & 0xFF));
    else if (done_state == 1 && (done_tmp6 >> 8) < 5 && !(done_tmp5 & 0x8000)) done_state = 2, done_tmp5 = (uint16_t)((uint8_t)-0x03 << 8 | (done_tmp5 & 0xFF));
    else if (done_state == 2 && (done_tmp6 >> 8) < 3 && !(done_tmp5 & 0x8000)) done_state = 3, nt_scroll_y = 0;
    break;
  }
  case 3: done_coin = 0, done_state = 4, done_tmp1 = 1; break;
  case 4: case 5: case 6: {
    static const uint8_t cx[3] = {11, 15, 19};
    int c = done_state - 4;
    if (coins & (1 << c)) {
      nt_put(0, cx[c], 18, 0x90), nt_put(0, cx[c] + 1, 18, 0x91), nt_put(0, cx[c], 19, 0xA0), nt_put(0, cx[c] + 1, 19, 0xA1);
      if (done_tmp1 == 1) done_tmp1 = 50;
    }
    done_tmp1--;
    if (done_tmp1 == 0 || done_tmp1 == 30) done_state++, done_tmp1 = 1;
    break;
  }
  case 7:
    lvl_done_update();
    if (hit & (PK_LEFT | PK_RIGHT)) done_sel ^= 1, lvl_done_update();
    if (hit & (PK_OK | PK_EXE)) {
      if (done_sel) go(S_LEVELS);
      else coins = 0, go(S_PLAY);
    }
    if (hit & PK_BACK) go(S_LEVELS);
    break;
  }
}

/* ---------------------------------------------------------------- customize (customize.c) */
static const uint8_t iconTable[] = {0, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E, 0x20, 0x22, 0x24, 0x26, 0x40, 0x42,
                                    0x44, 0x46, 0x48, 0x4A, 0x4C, 0x4E, 0x60, 0x62, 0x64, 0x66, 0x68, 0x6A, 0x6C, 0x6E};
static uint8_t cust_sel = 3;
static void cust_marks(int s, bool on) {
  /* the arrows around the chosen thing: h i (left) and f g (right) */
  static const uint8_t lx[4] = {4, 13, 22, 13}, ly[4] = {14, 14, 14, 8};
  int x = lx[s], y = ly[s];
  const uint8_t *m = cmap_mainmenu_customize;
  nt_put(0, x, y, on ? m['h'] : m[' ']), nt_put(0, x, y + 1, on ? m['i'] : m[' ']);
  nt_put(0, x + 5, y, on ? m['f'] : m[' ']), nt_put(0, x + 5, y + 1, on ? m['g'] : m[' ']);
}
static void cust_icon(void) {
  const uint8_t *m = cmap_mainmenu_customize;
  uint8_t t = retro_mode ? (uint8_t)(m['a'] + 0x6E) : (uint8_t)(iconTable[icon] + m['a']);
  nt_put(0, 15, 8, t), nt_put(0, 16, 8, (uint8_t)(t + 1)), nt_put(0, 15, 9, (uint8_t)(t + 0x10)), nt_put(0, 16, 9, (uint8_t)(t + 0x11));
}
static void enter_custom(void) {
  bg_mode = BG_NAMETABLE;
  menu_banks(102);
  chr_spr[0] = 0xFE, chr_spr[1] = 96;
  ppu_set_palette(pal_paletteMenu, NULL);
  pal_col(0, 0);
  nt_load(nt_customize);
  nt_line(25, "PRESS BACK TO RETURN");
  int coins_n = 0, stars_n = 0;
  for (int i = 0; i < NLEVELS; i++) {
    coins_n += prog.coin1[i] + prog.coin2[i] + prog.coin3[i];
    if (invisblocks ? prog.icomplete[i] : prog.complete[i]) stars_n += level_list[i].stars;
  }
  char b[8];
  snprintf(b, sizeof b, "%2d", stars_n > 99 ? 99 : stars_n);
  for (int i = 0; i < 2; i++) nt_put(0, 26 + i, 4, b[i] == ' ' ? 0xFF : (uint8_t)(0xD0 + b[i] - '0'));
  snprintf(b, sizeof b, "%2d", coins_n > 99 ? 99 : coins_n);
  for (int i = 0; i < 2; i++) nt_put(0, 26 + i, 5, b[i] == ' ' ? 0xFF : (uint8_t)(0xD0 + b[i] - '0'));
  cust_sel = 3;
  for (int s = 0; s < 4; s++) cust_marks(s, s == cust_sel);
  cust_icon();
  nt_scroll_x = nt_scroll_y = 0, nt_split_y0 = nt_split_y1 = -1;
}
static void colorinc(uint8_t *c) { /* customize.c */
  if (++*c & 0x30) {
    if ((*c & 0x0F) >= 0x0D) *c = (uint8_t)((*c + 0x10) & 0x30);
  } else if ((((uint8_t)(*c - 0x0D)) & 0xFE) == 0) *c = 0x0F;
}
static void colordec(uint8_t *c) {
  if (--*c & 0x30) {
    if ((*c & 0x0F) >= 0x0D) *c = (uint8_t)((*c & 0x30) | 0x0C);
  } else if ((((uint8_t)(*c - 0x0D)) & 0xFE) == 0) *c = 0x0C;
}
static void custom_tick(void) {
  pal_col(0x0a, icon_colors[0]), pal_col(0x0b, icon_colors[1]), pal_col(0x09, icon_colors[2]);
  if (fading()) return;
  static uint8_t hold;
  if (hit & (PK_LEFT | PK_RIGHT)) {
    cust_marks(cust_sel, false);
    cust_sel = (uint8_t)((cust_sel + ((hit & PK_RIGHT) ? 1 : 3)) % 4);
    cust_marks(cust_sel, true);
  }
  bool up = (hit & PK_UP) || ((keys & PK_UP) && hold >= 15), dn = (hit & PK_DOWN) || ((keys & PK_DOWN) && hold >= 15);
  if (cust_sel == 3) {
    if (!retro_mode && up) icon = icon + 1 >= MAX_ICONS ? 0 : icon + 1, hold = 0;
    if (!retro_mode && dn) icon = icon == 0 ? MAX_ICONS - 1 : icon - 1, hold = 0;
    cust_icon();
  } else {
    if (hit & PK_UP) colorinc(&icon_colors[cust_sel]);
    if (hit & PK_DOWN) colordec(&icon_colors[cust_sel]);
  }
  if (hold < 15) hold++;
  if (hit & (PK_BACK | PK_OK | PK_EXE)) save(), go(S_TITLE);
}

/* ---------------------------------------------------------------- settings and fun settings */
static uint8_t set_sel;
#define NSET 3
#define NFUN 7
static void set_rows(void) {
  const uint8_t *m = cmap_mainmenu_charmap;
  bool fun = screen == S_FUN;
  int n = fun ? NFUN : NSET;
  for (int i = 0; i < n; i++) {
    int y = (fun ? 5 : 3) + i * 2;
    nt_put(0, 4, y, i == set_sel ? m['c'] : m[' ']);
    bool on;
    if (!fun) on = i == 0 ? viseffects : i == 1 ? trails == 1 : auto_practicepoints;
    else on = i == 0 ? invisible : i == 1 ? (options & platformer) != 0 : i == 2 ? retro_mode : i == 3 ? discomode != 0
              : i == 4 ? invisblocks : i == 5 ? cam_seesaw != 0 : gameboy_mode != 0;
    nt_put(0, 26, y, on ? m['g'] : m['f']);
    if (fun && i == 3) {
      nt_put(0, 25, y, ' ');
      if (discomode) nt_put(0, 25, y, (uint8_t)(m['1'] + (discomode & 1 ? 0 : discomode & 2 ? 1 : discomode & 4 ? 2 : discomode & 8 ? 3 : 4)));
      else nt_put(0, 25, y, m[' ']);
    }
    if (fun && i == 6) {
      static const char *const gb[9] = {"", "GREY", "RED", "GREEN", "BLUE", "YELLOW", "PURPLE", "CYAN", "DARK"};
      nt_padded(0, 18, y, gb[gameboy_mode], 7, m);
    }
  }
}
static void enter_settings(bool fun) {
  bg_mode = BG_NAMETABLE;
  menu_banks(98);
  chr_spr[0] = 0xFE, chr_spr[1] = 96;
  ppu_set_palette(fun ? pal_paletteMenu : pal_paletteSettings, NULL);
  nt_load(fun ? nt_funsettings : nt_settings);
  const uint8_t *m = cmap_mainmenu_charmap;
  /* the rows: the original screen's frame, its own words for the settings the calculator keeps */
  static const char *const rows_set[NSET] = {"VISUAL EFFECTS", "EXTRA TRAILS", "AUTO CHECKPOINTS"};
  static const char *const rows_fun[NFUN] = {"INVISIBLE MODE", "PLATFORMER MODE", "RETRO MODE", "DISCO MODE", "BLIND MODE",
                                             "CAM SEESAW", "GAMEBOY"};
  int y0 = fun ? 5 : 3;
  for (int y = y0; y <= 21; y++)
    for (int x = 3; x <= 27; x++) nt_put(0, x, y, m[' ']);
  for (int i = 0; i < (fun ? NFUN : NSET); i++) nt_text(0, 6, y0 + i * 2, fun ? rows_fun[i] : rows_set[i], m);
  if (fun) nt_line(23, "PRESS OK TO TOGGLE");
  nt_line(24, "PRESS BACK TO SAVE/RETURN");
  set_sel = 0;
  set_rows();
  nt_scroll_x = nt_scroll_y = 0, nt_split_y0 = nt_split_y1 = -1;
}
static void settings_tick(void) {
  if (fading()) return;
  bool fun = screen == S_FUN;
  int n = fun ? NFUN : NSET;
  if (hit & (PK_DOWN | PK_RIGHT)) set_sel = (uint8_t)((set_sel + 1) % n);
  if (hit & (PK_UP | PK_LEFT)) set_sel = (uint8_t)((set_sel + n - 1) % n);
  if (hit & (PK_OK | PK_EXE)) {
    if (!fun) {
      if (set_sel == 0) { viseffects ^= 1; if (!viseffects) trails = 0; }
      else if (set_sel == 1) trails = trails == 1 ? 0 : 1;
      else auto_practicepoints ^= 1;
    } else switch (set_sel) {
      case 0: invisible ^= 1; break;
      case 1: options ^= platformer; break;
      case 2: retro_mode ^= 1; break;
      case 3:
        if (discomode & 0x10) discomode = 0;
        else { discomode = (uint8_t)(discomode << 1); if (!discomode) discomode = 1; }
        break;
      case 4: invisblocks ^= 1; break;
      case 5: cam_seesaw = cam_seesaw ? 0 : 1; break;
      case 6: gameboy_mode = gameboy_mode == 8 ? 0 : gameboy_mode + 1; break;
      }
  }
  set_rows();
  if (hit & PK_BACK) save(), go(S_TITLE);
}
static const uint8_t gameboy_check_table[9] = {0x00, 0x01, 0x21, 0x41, 0x81, 0x61, 0xA1, 0xC1, 0xE1};

/* ---------------------------------------------------------------- credits: the game's three credits screens */
static void enter_credits(void) {
  bg_mode = BG_NAMETABLE;
  menu_banks(98);
  chr_spr[0] = 0xFE, chr_spr[1] = 96;
  ppu_set_palette(pal_paletteCredits, NULL);
  memcpy(nametable[0], help_page == 0 ? nt_credits : help_page == 1 ? nt_credits2 : nt_credits3, 1024);
  memcpy(nametable[1], nametable[0], 1024);
  nt_scroll_x = nt_scroll_y = 0, nt_split_y0 = nt_split_y1 = -1;
}
static void credits_tick(void) {
  if (fading()) return;
  if (hit & (PK_OK | PK_EXE | PK_RIGHT)) {
    if (help_page < 2) help_page++, enter_credits();
    else help_page = 0, go(S_TITLE);
  }
  if (hit & PK_LEFT && help_page > 0) help_page--, enter_credits();
  if (hit & PK_BACK) help_page = 0, go(S_TITLE);
}

/* ---------------------------------------------------------------- how to play */
static void help_tick(void) {
  if (fading()) return;
  if (hit & (PK_OK | PK_EXE | PK_RIGHT)) {
    if (help_page < 1) help_page++;
    else {
      sv.flags |= 1;
      if (help_back == S_PLAY) screen = S_PLAY, phase = P_PAUSE;
      else go(help_back);
    }
  }
  if (hit & PK_LEFT && help_page > 0) help_page--;
  if (hit & PK_BACK) {
    sv.flags |= 1;
    if (help_back == S_PLAY) screen = S_PLAY, phase = P_PAUSE;
    else go(help_back);
  }
}

static void enter(int s) {
  screen = s;
  oam_clear();
  nt_wrap_split = 0, nt_fill_tile = 0;
  pal_bright_level = 0;
  switch (s) {
  case S_TITLE: enter_title(); break;
  case S_LEVELS: enter_levels(); break;
  case S_PLAY: bg_mode = BG_LEVEL; play_start(); game_banks(); game_draw(); return;
  case S_CUSTOM: enter_custom(); break;
  case S_SETTINGS: enter_settings(false); break;
  case S_FUN: enter_settings(true); break;
  case S_CREDITS: help_page = 0; enter_credits(); break;
  case S_HELP: bg_mode = BG_NAMETABLE; menu_banks(98); ppu_set_palette(pal_paletteMenu, NULL); break;
  }
  fade_in();
}

static void tick(void) {
  frame++;
  fade_tick();
  if (next_screen >= 0 && !fading()) {
    int s = next_screen;
    next_screen = -1;
    enter(s);
    return;
  }
  switch (screen) {
  case S_TITLE: title_tick(); title_sprites(); break;
  case S_LEVELS:
    levels_tick();
    oam_clear();
    draw_both_progress_bars();
    break;
  case S_PLAY: if (next_screen < 0) play_tick(); break;
  case S_DONE: done_tick(); break;
  case S_CUSTOM: custom_tick(); break;
  case S_SETTINGS: case S_FUN: settings_tick(); break;
  case S_HELP: help_tick(); break;
  case S_CREDITS: credits_tick(); break;
  }
  emphasis = gameboy_check_table[gameboy_mode];
  if (screen == S_PLAY && (phase == P_PAUSE || phase == P_QUIT)) emphasis |= 0xE0; /* COL_EMP_DARK */
}

/* ---------------------------------------------------------------- drawing */
#define W() HEX(0xFFFFFF)
#define DIM() HEX(0xC8D0D8)
static void text_c(const np_font_t *f, int cx, int base, const char *s, color_t c) { gfx_text_center(f, cx, base, s, c, 256); }
static void keyline(const char *s) {
  gfx_fill_alpha(0, 222, 320, 18, HEX(0x000000), 170);
  text_c(&np_font_small, 160, 235, s, DIM());
}
static void box(int x, int y, int w, int h) {
  gfx_rrect(x, y, w, h, 8, HEX(0x101828), 230);
  gfx_rrect_outline(x, y, w, h, 8, 2, HEX(0x5CC8FF), 256);
}
static void button(int x, int y, int w, int h, const char *s, bool on) {
  if (on) gfx_rrect(x, y, w, h, 6, HEX(0xFFD24A), 256);
  else gfx_rrect_outline(x, y, w, h, 6, 1, HEX(0x7C8A90), 256);
  text_c(&np_font_body, x + w / 2, y + h / 2 + 5, s, on ? HEX(0x101010) : W());
}

static const char *const help_lines[2][7] = {
    {"Jump over the spikes and reach the end.", "OK, EXE or Up: jump. Hold it to keep jumping,",
     "to fly the ship and to go up with the wave.", "Tap it on a ring (an orb) to jump in the air.",
     "Pads and portals act on their own:", "they change your gravity, speed, size and mode.", "Three coins hide in every level."},
    {"Back pauses: Resume, Practice, Quit game.", "Practice puts checkpoints down as you play.",
     "Down places one, backspace removes the last.", "Coins don't count in practice.",
     "Play: the 27 main levels. Next to it: 18 more.", "In the tower levels, Left and Right walk.",
     "Beat the first 13 for the fun settings."}};

static void scene(void *ctx) {
  (void)ctx;
  if (screen == S_HELP) {
    gfx_vgrad(0, gfx_y0, 320, gfx_y1 - gfx_y0, 0x2A1A5E, 0x0E0820);
    text_c(&np_font_title, 160, 36, help_page == 0 ? "How to play" : "Practice and menus", HEX(0xFFD24A));
    for (int i = 0; i < 7; i++) text_c(&np_font_body, 160, 70 + i * 21, help_lines[help_page][i], W());
    text_c(&np_font_small, 160, 214, help_page == 0 ? "1 / 2" : "2 / 2", DIM());
    keyline(help_page == 0 ? "OK: next page   Back: close" : "OK: close   Left: back a page");
    return;
  }
  ppu_strip(gfx_buf, gfx_y0, gfx_y1);
  switch (screen) {
  case S_TITLE: keyline("Arrows: choose   OK: open   Back: credits   Home: quit"); break;
  case S_LEVELS: keyline("Left Right: level   OK: play   Back: title"); break;
  case S_DONE: if (done_state >= 7) keyline("Left Right: choose   OK: go"); break;
  case S_CUSTOM: keyline("Left Right: icon or colour   Up Down: change   Back: done"); break;
  case S_SETTINGS: case S_FUN: keyline("Up Down: choose   OK: switch   Back: done"); break;
  case S_CREDITS: keyline("OK: next   Back: title"); break;
  case S_PLAY:
    if (phase == P_PAUSE || phase == P_QUIT) {
      box(80, 52, 160, 124);
      text_c(&np_font_title, 160, 80, "Paused", W());
      const char *it[3] = {"Resume", practice_point_count ? "Practice off" : "Practice", phase == P_QUIT ? "Sure? OK quits" : "Quit game"};
      for (int i = 0; i < 3; i++) button(96, 92 + i * 26, 128, 22, it[i], pause_sel == i);
      keyline("Up Down: choose   OK: go   Back: resume");
    } else if (practice_point_count && phase == P_RUN) {
      if (frame < (uint32_t)auto_cp_seen + 240) keyline("Practice: Down places a checkpoint, backspace removes one");
    } else if (game_platformer() && frame < level_frame + 300) keyline("Left Right: walk   OK: jump   Back: pause");
    else if (!(sv.flags & 2) && phase == P_RUN) keyline("OK, EXE or Up: jump (hold to keep jumping)   Back: pause");
    break;
  }
}

/* ---------------------------------------------------------------- the loop */
#ifndef HOST
__attribute__((used, externally_visible))
#endif
uint32_t perf_frames, perf_max, perf_slow;
static color_t strip[SCREEN_W * STRIP_H];

int main(void) {
  plat_begin();
  gfx_buf = strip;
  ppu_init();
  now_ms = plat_millis();
  rng_seed ^= plat_random() ^ now_ms;
  if (!rng_seed) rng_seed = 1;
  load();
  enter(S_TITLE);
  if (!(sv.flags & 1)) help_back = S_TITLE, help_page = 0, screen = S_HELP;
  uint32_t held = plat_keys(), last = plat_millis(), saved = last;
  uint32_t acc = 0;
  for (;;) {
    uint32_t k = plat_keys();
    if (k & PK_HOME) break;
    now_ms = plat_millis();
    uint32_t dt = now_ms - last;
    last = now_ms;
    if (dt > 200) dt = 200;
    sv.play_ms += dt;
#ifdef HOST
    acc = 1000; /* (tests: one step a frame) */
    (void)dt;
#else
    acc += dt * 60;
#endif
    int steps = 0;
    keys = (uint16_t)k;
    while (acc >= 1000 && steps < 4) {
      acc -= 1000, steps++;
      hit = (uint16_t)(k & ~held);
      held = k;
      tick();
      if (screen == S_PLAY && phase == P_RUN && (joypad1.press & PAD_A)) sv.flags |= 2;
      if (screen == S_PLAY && practice_point_count && !auto_cp_seen) auto_cp_seen = (int)frame;
      if (screen == S_PLAY && !practice_point_count) auto_cp_seen = 0;
    }
    if (acc > 4000) acc = 0;
    if (screen != S_PLAY && now_ms - saved > 60000) save(), saved = now_ms;
    uint32_t t0 = plat_millis();
    gfx_render(scene, NULL, 0, 240);
    uint32_t t = plat_millis() - t0;
    perf_frames++, perf_max = t > perf_max ? t : perf_max, perf_slow += t > 33;
#ifndef HOST
    uint32_t spent = plat_millis() - now_ms;
    if (spent < 16) plat_sleep(16 - spent);
#endif
  }
  save();
  return plat_end();
}
