/* Portal Returns for NumWorks: title screen, menus, help, saving. */
#include <limits.h>
#include <string.h>
#include "../../common/epsilon_app.h"
#include "../../common/epsilon_files.h"
#include "../../common/jump.h"
#include "portal.h"
#include "assets.h"

#ifdef __ELF__ /* app name and API level, for the calculator's installer */
#ifndef NP_PORT /* (another target: platform/port.mk names the app) */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Portal";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif
#endif

#define SPT(a) (S_E1C7 + ((a) - 0xE1C7))

save_t save;
static np_jump_t leave;

/* ------------------------------------------------------------ input */
static uint64_t keys;
bool key_down(int k) { return keys >> k & 1; }

void scan_keys(void) {
  keys = eadk_keyboard_scan();
  if (key_down(eadk_key_home) || key_down(eadk_key_on_off)) np_jump(leave);
}

/* Menus read the keyboard too (rather than the event queue), so a key
 * that is still down from before never counts as a new press. Arrows
 * repeat when held. */
static uint64_t prev_keys = ~(uint64_t)0;
static int rep = -1;
static uint64_t rep_at;

void flush_keys(void) {
  scan_keys();
  prev_keys = keys;
  rep = -1;
}

/* back in a chamber: wait until OK, EXE and Back are up, so the key that
 * closed a menu does not also grab a cube or open the menu again */
static void release_keys(void) {
  for (;;) {
    scan_keys();
    if (!key_down(eadk_key_ok) && !key_down(eadk_key_exe) && !key_down(eadk_key_back)) return;
    eadk_timing_msleep(5);
  }
}

int wait_key(void) {
  for (;;) {
    scan_keys();
    uint64_t now = eadk_timing_millis(), fresh = keys & ~prev_keys;
    prev_keys = keys;
    if (fresh) {
      uint32_t lo = (uint32_t)fresh; /* 32-bit ctz: no libgcc helper in the module */
      int k = lo ? __builtin_ctzl(lo) : 32 + __builtin_ctzl((uint32_t)(fresh >> 32));
      rep = k <= eadk_key_right ? k : -1;
      rep_at = now + 350;
      return k;
    }
    if (rep >= 0 && key_down(rep) && now >= rep_at) {
      rep_at = now + 70;
      return rep;
    }
    if (rep >= 0 && !key_down(rep)) rep = -1;
    eadk_timing_msleep(10);
  }
}

/* ------------------------------------------------------------ saving */
static const char SAVE_NAME[] = "portal.sav";

static u8 checksum(const save_t *s) {
  const u8 *p = (const u8 *)s;
  u8 c = 0x5A;
  for (unsigned i = 0; i < sizeof *s - 1; i++) c = (u8)(c * 31 + p[i]);
  return c;
}

void save_write(void) {
  save.magic = 'P', save.version = 1;
  save.sum = checksum(&save);
  ef_write(SAVE_NAME, &save, sizeof save);
}

static void save_load(void) {
  memset(&save, 0, sizeof save);
  save.unlocked[0] = save.unlocked[1] = 1;
  save.level[0] = save.level[1] = 1;
  save.scheme = 1;
  uint32_t n;
  const u8 *p = ef_read(SAVE_NAME, &n);
  if (!p || n != sizeof save) return;
  save_t s;
  memcpy(&s, p, sizeof s);
  if (s.magic != 'P' || s.version != 1 || s.sum != checksum(&s)) return;
  for (int k = 0; k < 2; k++) {
    int max = pack_levels(k);
    if (s.unlocked[k] < 1 || s.unlocked[k] > max) s.unlocked[k] = 1;
    if (s.level[k] < 1 || s.level[k] > s.unlocked[k]) s.level[k] = s.unlocked[k];
  }
  if (s.scheme > 3) s.scheme = 1;
  if (s.pack > 1) s.pack = 0;
  save = s;
}

void quit_now(void) { np_jump(leave); }

static int quit(void) {
  save_write();
  return np_app_end();
}

static bool is_done(int pack, int n) { return save.done[pack][(n - 1) >> 3] >> ((n - 1) & 7) & 1; }

void level_started(int pack, int n) {
  save.pack = pack;
  save.level[pack] = n;
  if (n > save.unlocked[pack]) save.unlocked[pack] = n;
  save_write();
}

void level_done(int pack, int n) {
  save.done[pack][(n - 1) >> 3] |= 1 << ((n - 1) & 7);
  if (n < pack_levels(pack)) {
    if (n + 1 > save.unlocked[pack]) save.unlocked[pack] = n + 1;
    save.level[pack] = n + 1;
  } else if (pack == 0) save.flags |= 1;
  save_write();
}

/* ------------------------------------------------------------ colors */
/* The four color schemes of the original (COLOR on the title screen). */
void apply_scheme(void) {
  memcpy(pal, PAL_DEFAULT, sizeof pal);
  pal[0] = pal[1] = 0; /* scheme 0 keeps the table's black */
  static const u8 bg[4] = {0xFF, 0x4A, 0x6B, 0x00};
  u8 b = bg[save.scheme], f = save.scheme ? 0xFF : 0x00;
  if (save.scheme) pal[0] = pal[1] = 0xFFFF;
  pal[2] = b << 8 | b;
  text_fg = f << 8 | f;
  text_bg = screen_bg = pal[2];
}

/* ------------------------------------------------------------ title */
static void title_art(void) { /* EC34 */
  cls();
  text("P", 0x1D, 0x29);
  spr(S_DD13, 0x22, 0x25);
  text("RTAL", 0x2B, 0x29);
  text("RETURNS", 0x47, 0x29);
  spr1(TITLE_PIC0, 0x70, 0x1F);
  spr1(TITLE_PIC1, 0x1D, 0xA1);
  spr1(TITLE_PIC2, 0x48, 0xA1);
  spr1(TITLE_PIC3, 0x73, 0xA1);
}

static void item(const char *s, int row, bool on) {
  u16 f = text_fg;
  if (on) text_fg = pal[C_BLUE];
  text(s, 0x1D, 0x42 + 0x18 * row);
  text_fg = f;
}

/* "<12>": the selector (the number lights up), a star once that chamber is done */
static void selector(int x2, int y, int n, bool on, bool done) {
  u16 f = text_fg;
  fill(x2 * 2, y, 70, 8, screen_bg);
  x2 = text("<", x2, y);
  if (on) text_fg = pal[C_BLUE];
  x2 = number(n, x2, y);
  text_fg = f;
  x2 = text(">", x2, y);
  if (done) { /* a check mark in the font's style */
    static const u8 check[8] = {0x00, 0x03, 0x03, 0x06, 0xC6, 0x6C, 0x38, 0x10};
    static u16 px[64];
    for (int i = 0; i < 64; i++) px[i] = check[i >> 3] >> (7 - (i & 7)) & 1 ? pal[C_ORANGE] : screen_bg;
    eadk_display_push_rect((eadk_rect_t){x2 * 2 + 2, y, 8, 8}, px);
  }
}

static void menu_redraw(const char *const *items, int n, int sel) {
  for (int i = 0; i < n; i++) item(items[i], i, i == sel);
}

static void progress_line(int pack) {
  int done = 0, total = pack_levels(pack);
  for (int i = 1; i <= total; i++) done += is_done(pack, i);
  char s[24], *o = s;
  if (done >= 10) *o++ = '0' + done / 10;
  *o++ = '0' + done % 10;
  *o++ = '/';
  *o++ = '0' + total / 10, *o++ = '0' + total % 10;
  strcpy(o, T(" COMPLETE"));
  fill(0, 212 - 3 * NP_TEXT_EXTRA, 320, 8 + 6 * NP_TEXT_EXTRA, screen_bg); /* (and accents, Chinese) */
#if NP_TEXT_EXTRA
  text(s, (160 - text_w(s)) / 2, 212);
#else
  int len = (int)strlen(s);
  text(s, (160 - len * 5) / 2, 212);
#endif
}

static void help(void);
static void credits(void);

/* The Prelude chambers (the original's CUSTOM level packs) */
static void custom_menu(void) {
  title_art();
  int redraw = 1;
  bool armed = false; /* 5 pressed once: the next 5 resets */
  for (;;) {
    if (redraw) {
      item(T("PRELUDE"), 0, true);
      selector(0x47, 0x42, save.level[1], true, is_done(1, save.level[1]));
      fill(0x1D * 2, 0x8A - 3 * NP_TEXT_EXTRA, 320 - 0x1D * 2, 8 + 6 * NP_TEXT_EXTRA, screen_bg);
      text(armed ? T("PRESS 5 AGAIN TO RESET") : T("PRESS 5 TO RESET"), 0x1D, 0x8A);
      progress_line(1);
    }
    redraw = 1;
    int k = wait_key();
    bool was = armed;
    armed = false;
    if (k == eadk_key_left && save.level[1] > 1) save.level[1]--;
    else if (k == eadk_key_right && save.level[1] < save.unlocked[1]) save.level[1]++;
    else if (k == eadk_key_five) {
      if (!was) armed = true;
      else {
        save.unlocked[1] = save.level[1] = 1;
        memset(save.done[1], 0, sizeof save.done[1]);
        save_write();
      }
    } else if (k == eadk_key_ok || k == eadk_key_exe) {
      play(1, save.level[1]);
      return;
    } else if (k == eadk_key_back) return;
    else redraw = was;
  }
}

static void title(void) {
  static const char *const items[] = {T("LEVEL "), T("CUSTOM"), T("COLOR"), T("HELP")};
  int sel = 0;
  for (;;) {
    flush_keys(); /* keys still down from a chamber or a page are not presses */
    apply_scheme();
    title_art();
    int redraw = 1;
    for (;;) {
      if (redraw) {
        menu_redraw(items, 4, sel);
        selector(0x47, 0x42, save.level[0], sel == 0, is_done(0, save.level[0]));
        progress_line(0);
      }
      redraw = 1;
      int k = wait_key();
      if (k == eadk_key_up && sel > 0) sel--;
      else if (k == eadk_key_down && sel < 3) sel++;
      else if (k == eadk_key_left && sel == 0 && save.level[0] > 1) save.level[0]--;
      else if (k == eadk_key_right && sel == 0 && save.level[0] < save.unlocked[0]) save.level[0]++;
      else if (k == eadk_key_ok || k == eadk_key_exe) {
        if (sel == 0) play(0, save.level[0]);
        else if (sel == 1) custom_menu();
        else if (sel == 2) {
          save.scheme = (save.scheme + 1) & 3;
          save_write();
          sel = 0; /* like the original: back on LEVEL */
        } else help();
        break;
      } else if (k == eadk_key_two) {
        credits();
        break;
      } else if (k == eadk_key_back) np_jump(leave);
      else redraw = 0;
    }
  }
}

/* ------------------------------------------------------------ pause */
/* EB1D: the title art with RETURN / RESTART / QUIT.
 * Returns 0 to go on, 1 to quit to the title, 2 to restart the level. */
int pause_menu(void) {
  static const char *const items[] = {T("RETURN"), T("RESTART"), T("QUIT")};
  save_write();
  flush_keys(); /* the Back that opened the menu is not a press in it */
  title_art();
  int sel = 0, r;
  for (;;) {
    menu_redraw(items, 3, sel);
    int k = wait_key();
    if (k == eadk_key_up && sel > 0) sel--;
    else if (k == eadk_key_down && sel < 2) sel++;
    else if (k == eadk_key_ok || k == eadk_key_exe) {
      r = sel == 0 ? 0 : sel == 1 ? 2 : 1;
      break;
    } else if (k == eadk_key_back) {
      r = 0;
      break;
    }
  }
  if (r != 1) release_keys();
  return r;
}

/* ------------------------------------------------------------ help and credits */
static void page_wait(void) {
  for (;;) {
    int k = wait_key();
    if (k == eadk_key_ok || k == eadk_key_exe || k == eadk_key_back || k == eadk_key_right || k == eadk_key_left) return;
  }
}

static void key_box(int x, int y, char c, u16 col) {
  fill(x, y, 22, 22, pal[C_EDGE]);
  fill(x + 2, y + 2, 18, 18, col);
  u16 f = text_fg, b = text_bg;
  text_bg = col;
  glyph(c, (x + 7) / 2, y + 7);
  text_fg = f, text_bg = b;
}

static void icon_field(int x2, int y, u16 c) {
  u16 k = pal[C_FIELD];
  pal[C_FIELD] = c;
  spr(SPT(0xE71D), x2, y);
  pal[C_FIELD] = k;
}

/* a sprite at twice the size (the 4x4 cube reads better as an icon) */
static void spr_x2(const u8 *s, int x, int y) {
  for (int j = 0; j < s[0]; j++)
    for (int i = 0; i < s[1] * 2; i++) {
      u8 v = s[2 + j * s[1] + i / 2];
      fill(x + 2 * i, y + 2 * j, 2, 2, pal[i & 1 ? v & 15 : v >> 4]);
    }
}

static bool help_page(int page) {
  cls();
  if (page == 0) {
    static const char *const keys[][2] = {{T("LEFT RIGHT"), T("RUN")}, {T("UP"), T("JUMP")},
                                               {T("OK"), T("PICK UP / DROP")}, {T("BACK"), T("PAUSE")}};
    text(T("HOW TO PLAY"), 5, 10);
    for (int i = 0; i < 4; i++) {
      text(keys[i][0], 10, 30 + 16 * i);
      text(keys[i][1], NP_TEXT_EXTRA ? 80 : 75, 30 + 16 * i); /* (room for longer keys' names) */
    }
    static const char pad[] = "789456123";
    for (int i = 0; i < 9; i++) key_box(20 + (i % 3) * 26, 104 + (i / 3) * 26, pad[i], i == 4 ? pal[C_ORANGE] : pal[C_BLUE]);
    text(T("1-9 SHOOT A"), 55, 108);
    text(T("PORTAL THAT WAY"), 55, 124);
    text(T("5 SWITCHES THE"), 55, 148);
    text(T("NEXT COLOR:"), 55, 164);
    spr(SPT(0xE1FF), 0x71, 164);
    text(T("REACH THE RIGHT SIDE"), 5, 196);
    text(T("OF EACH TEST CHAMBER."), 5, 212);
  } else {
    text(T("TEST ELEMENTS"), 5, 10);
    static const char *const lines[][2] = {
      {T("BUTTON: STAND OR PUT A"), T("CUBE ON IT TO OPEN DOORS")},
      {T("CUBE: OK PICKS IT UP"), T("AND DROPS IT")},
      {T("FIZZLER: NO PORTALS"), T("OR CUBES GET THROUGH")},
      {T("ELECTRIC FIELDS AND"), T("SPIKES ARE DEADLY")},
      {T("GLASS: BEAMS GO THROUGH,"), T("YOU DO NOT")},
      {T("PELLET: DEADLY. GUIDE IT"), T("INTO A RECEIVER")},
    };
    for (int i = 0; i < 6; i++) {
      int y = 32 + i * 32;
      switch (i) {
      case 0: spr(SPT(0xE6D9), 2, y + 6); break;
      case 1: spr_x2(SPT(0xE74B), 8, y + 2); break;
      case 2: icon_field(2, y + 5, pal[C_FIZZ]); break;
      case 3: spr(tile_sprite(0x0F), 2, y + 6), icon_field(2, y, pal[C_ELEC]); break;
      case 4: spr(tile_sprite(0x17), 2, y); break;
      default: spr(SPT(0xE741), 2, y + 4), spr(tile_sprite(0x15), 6, y);
      }
      text(lines[i][0], 17, y);
      text(lines[i][1], 17, y + 12);
    }
  }
  text(T("OK >"), 0x8A, 226);
  for (;;) {
    int k = wait_key();
    if (k == eadk_key_back) return false;
    if (k == eadk_key_ok || k == eadk_key_exe || k == eadk_key_right) return true;
  }
}

static void help(void) {
  for (int p = 0; p < 2; p++)
    if (!help_page(p)) return;
  credits();
}

static void credits(void) {
  static const char *const lines[] = {
    T("CREDITS"),
    T(" INSPIRED BY PORTAL RETURNS"),
    T(" BY MATEOCONLECHUGA. PORTAL"),
    T(" IS BY VALVE. NOT AFFILIATED"),
    T(" WITH EITHER OF THEM."),
    T(" CHAMBERS: MATEOCONLECHUGA"),
    T(" SPRITES AND TILES: CKH4"),
    T(" PRELUDE: UNICORN, AFTER"),
    T(" BUILDERBOY'S PORTAL PRELUDE"),
    T(" ORIGINAL TESTERS: UNICORN,"),
    " JAMESV, RALPHW74, 123OUTERME",
    "",
    T(" MADE BY MASON CHEN"),
    T(" AS PART OF NUMPLAY."),
  };
  cls();
  for (unsigned i = 0; i < sizeof lines / sizeof *lines; i++) text(lines[i], 5, 8 + 16 * i);
  page_wait();
}

/* ------------------------------------------------------------ hints */
/* A small sign over the top row of the chamber, shown once per save.
 * While it is up, the game draws around it (sign_clip). */
static int hint_pending[8], nhints;
static const char *const HINTS[] = {
  T("ARROWS: RUN, JUMP. EXIT RIGHT"), T("1-9: SHOOT A PORTAL THAT WAY"), T("5: SWITCH THE NEXT COLOR"),
  T("OK: GRAB / DROP A CUBE"), T("BUTTONS OPEN DOORS WHILE HELD"), T("BLUE FIELDS ERASE PORTALS"),
  T("RED FIELDS KILL: STAY CLEAR"), T("GUIDE PELLETS INTO RECEIVERS"), T("SPIKES KILL: STAY CLEAR"),
};

/* hints 0-7 in save.hints, hint 8 in bit 1 of save.flags */
static bool hint_seen(int h) { return h < 8 ? save.hints >> h & 1 : save.flags >> 1 & 1; }
static void hint_mark(int h) {
  if (h < 8) save.hints |= 1 << h;
  else save.flags |= 2;
}

void hint(int h) {
  if (hint_seen(h)) return;
  for (int i = 0; i < nhints; i++)
    if (hint_pending[i] == h) return;
  if (nhints < 8) hint_pending[nhints++] = h;
}

static int hint_t, hint_now = -1, hint_gap, sign_x, sign_w;

static void sign_draw(void) {
  sign_clip(0, 0);
  u16 ink = save.scheme ? pal[C_BG] : 0xFFFF, paper = save.scheme ? 0xFFFF : 0x0000;
  fill(sign_x, 1, sign_w, 14, paper);
  u16 f = text_fg, b = text_bg;
  text_fg = ink, text_bg = paper;
  text(HINTS[hint_now], sign_x / 2 + 3, 4);
  text_fg = f, text_bg = b;
  sign_clip(sign_x, sign_x + sign_w);
}

/* Where the sign hides the least: over plain wall rather than open air,
 * doors, droppers or fields, as near the middle as possible. */
static int sign_place(int w) {
  int best = 0, best_cost = INT_MAX;
  for (int x = 0; x + w <= 320; x += 2) {
    int cost = 0;
    for (int c = x / 16; c <= (x + w - 1) / 16; c++) {
      int t = top_tile(c);
      cost += t >= 2 && t < 0x0F ? 0 : t == 1 ? 40 : 60; /* wall, air, the rest */
    }
    cost = cost * 64 + (x + w / 2 > 160 ? x + w / 2 - 160 : 160 - x - w / 2);
    if (cost < best_cost) best_cost = cost, best = x;
  }
  return best;
}

/* called by the game every frame: draws / removes the sign */
bool hint_tick(void (*restore)(void)) {
  if (hint_now >= 0) {
    if (--hint_t > 0) return true;
    hint_now = -1;
    hint_gap = 24; /* half a second between two signs */
    sign_clip(0, 0);
    restore();
    return false;
  }
  if (hint_gap > 0) hint_gap--;
  if (!nhints || hint_gap) return false;
  hint_now = hint_pending[0];
  for (int i = 1; i < nhints; i++) hint_pending[i - 1] = hint_pending[i];
  nhints--;
  hint_mark(hint_now);
  save_write();
  hint_t = 47 * 4;
#if NP_TEXT_EXTRA
  sign_w = text_w(HINTS[hint_now]) * 2 + 8;
#else
  sign_w = (int)strlen(HINTS[hint_now]) * 10 + 8;
#endif
  sign_x = sign_place(sign_w);
  sign_draw();
  return true;
}

void hint_hide(void) { sign_clip(0, 0); }             /* before the pause menu */
void hint_show(void) { if (hint_now >= 0) sign_draw(); } /* after the chamber is redrawn */
void hints_clear(void) { nhints = 0, hint_now = -1, hint_gap = 0, sign_clip(0, 0); }

/* ------------------------------------------------------------ main */
int main(void) {
  np_app_begin();
  if (np_save_jump(leave)) return quit();
  assets_init();
  save_load();
#ifdef CHEAT
  save.unlocked[0] = save.level[0] = 40, save.unlocked[1] = 38;
#endif
  title();
  return quit();
}
