/* Buckshot for the NumWorks calculator, part of NumPlay.
 * Inspired by Buckshot Roulette by Mike Klubnika. Made by Mason Chen. */
#include <eadk.h>
#include <stddef.h>
#include "../../common/epsilon_app.h"
#include "../../common/epsilon_files.h"
#include "../../common/jump.h"
#include "game.h"
#include "scene.h"
#include "story.h"

#ifdef __ELF__ /* app name and API level, for the calculator's installer */
#ifndef NP_PORT /* (another target: platform/port.mk names the app) */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Buckshot";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif
#endif

#ifndef BR_START_STAGE
#define BR_START_STAGE 0
#endif
#ifndef BR_START_MODE
#define BR_START_MODE MODE_STORY
#endif

/* Home and On/Off leave the app from anywhere; "Main menu" leaves a match. */
static np_jump_t leave, to_menu;
static int in_match;

/* ------------------------------------------------------------------ saving */

static const char SAVE_NAME[] = "buckshot.sav";
#define SAVE_MAGIC 0x42
#define SAVE_VERSION 2

typedef struct {
  uint8_t magic, version, has_match, pad;
  profile_t profile;
  match_t match;
  uint32_t check;
} save_t;

static save_t S;
static match_t checkpoint; /* the match as it was at the last safe point */
static int have_checkpoint;

static uint32_t checksum(const uint8_t *p, uint32_t n) {
  uint32_t h = 2166136261u;
  while (n--) h = (h ^ *p++) * 16777619u;
  return h;
}

static void write_save(void) {
  S.magic = SAVE_MAGIC;
  S.version = SAVE_VERSION;
  S.profile = P;
  S.has_match = have_checkpoint;
  if (have_checkpoint) S.match = checkpoint;
  else S.match = (match_t){0};
  S.check = checksum((const uint8_t *)&S, offsetof(save_t, check));
  ef_write(SAVE_NAME, &S, sizeof S);
}

void save_profile(void) { write_save(); }

void save_match(void) {
  if (G.phase == PH_NONE || G.phase > PH_LAST) {
    have_checkpoint = 0;
  } else {
    checkpoint = G;
    have_checkpoint = 1;
  }
  write_save();
}

static void load_save(void) {
  uint32_t n;
  const uint8_t *p = ef_read(SAVE_NAME, &n);
  if (!p || n != sizeof S) return;
  save_t s;
  uint8_t *d = (uint8_t *)&s;
  for (uint32_t i = 0; i < n; i++) d[i] = p[i]; /* the record may be unaligned */
  if (s.magic != SAVE_MAGIC || s.version != SAVE_VERSION || s.check != checksum(d, offsetof(save_t, check))) return;
  P = s.profile;
  P.last_name[6] = 0;
  if (s.has_match && s.match.phase > PH_NONE && s.match.phase <= PH_LAST && s.match.stage < 3 &&
      s.match.nshell <= 8 && s.match.nshown <= 8 && s.match.mode <= MODE_DON && s.match.load < 250 &&
      s.match.maxhp <= 6 && s.match.hp[0] <= 6 && s.match.hp[1] <= 6) {
    checkpoint = s.match;
    checkpoint.name[6] = 0;
    for (int k = 0; k < 2; k++)
      for (int g = 0; g < 8; g++)
        if (checkpoint.items[k][g] < -1 || checkpoint.items[k][g] >= IT_COUNT) checkpoint.items[k][g] = -1;
    have_checkpoint = 1;
  }
}

static int quit(void) {
  write_save();
  return np_app_end();
}

/* ------------------------------------------------------------------- input */

/* The physical key behind an event, to tell a key still held (the emulator
 * reports it again and again) from a new press. */
static const uint8_t LETTER_KEY[26] = {18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
                                       31, 32, 33, 34, 36, 37, 38, 39, 40, 42, 43, 44, 45};

static int held_key = -1;   /* key of the last event, while it has not been seen released */
static uint64_t held_t;

static void track_release(void) {
  if (held_key >= 0 && !eadk_keyboard_key_down(eadk_keyboard_scan(), (eadk_key_t)held_key)) held_key = -1;
}

int key_wait(int ms) {
  uint64_t end = eadk_timing_millis() + (ms < 0 ? 0 : ms);
  for (;;) {
    int32_t t = 10;
    if (ms >= 0) {
      int64_t left = (int64_t)(end - eadk_timing_millis());
      if (left <= 0) return -1;
      if (left < t) t = (int32_t)left;
    }
    /* a release is only believed when seen before the event is read: an event
     * read while the key was still down is that same press */
    track_release();
    int e = eadk_event_get(&t);
    if (e == 6 || e == 8) np_jump(leave); /* Home, On/Off */
    int k, key;
    if (e <= KEY_EXE) k = key = e;
    else if (e >= 54 && e <= 57) k = key = e - 54;                          /* shifted arrows */
    else if (e >= 126 && e <= 151) k = KEY_LETTER + e - 126, key = LETTER_KEY[e - 126]; /* Alpha + key */
    else if (e >= 180 && e <= 205) k = KEY_LETTER + e - 180, key = LETTER_KEY[e - 180];
    else continue;
    if (key == 12 || key == 13) continue; /* Shift and Alpha on their own */
    uint64_t now = eadk_timing_millis();
    if (key == held_key) {
      /* not released since: only arrows and Backspace repeat, and not too fast */
      if (!(key <= KEY_RIGHT || key == KEY_BACKSPACE) || now - held_t < 110) continue;
    }
    held_key = key;
    held_t = now;
    return k;
  }
}

static int wait_until(uint64_t end, int skippable) {
  for (;;) {
    int64_t left = (int64_t)(end - eadk_timing_millis());
    if (left <= 0) return 0;
    uint64_t t0 = eadk_timing_millis();
    int k = key_wait(left > 20 ? 20 : (int)left);
    if (k == KEY_BACK && in_match) {
      app_pause_menu();
      end += eadk_timing_millis() - t0;
    } else if (skippable && IS_OK(k)) {
      return 1;
    }
  }
}

void pause_ms(int ms) { wait_until(eadk_timing_millis() + (ms > 0 ? ms : 0), 0); }
int pause_skip(int ms) { return wait_until(eadk_timing_millis() + (ms > 0 ? ms : 0), 1); }

/* -------------------------------------------------------------------- menus */

static int menu_list(const char *const *items, int n, int x, int y, int *sel, int (*draw)(void)) {
  for (;;) {
    draw();
    for (int i = 0; i < n; i++) {
      uint8_t c = i == *sel ? C_WHITE : C_GREY;
      gfx_text(&font_big, items[i], x, y + i * (font_big.h + 6), c);
      if (i == *sel) gfx_text(&font_big, ">", x - 12, y + i * (font_big.h + 6), C_WHITE);
    }
    gfx_present();
    int k = key_wait(-1);
    if (k == KEY_UP && *sel > 0) (*sel)--;
    else if (k == KEY_DOWN && *sel < n - 1) (*sel)++;
    else if (IS_OK(k)) return *sel;
    else if (k == KEY_BACK) return -1;
  }
}

static int draw_frozen(void) { return 0; }

void app_pause_menu(void) {
  static const char *const items[] = {T("RESUME"), T("MAIN MENU")};
  int sel = 0;
  gfx_darken(0, 0, GFX_W, GFX_H, 2);
  gfx_text_c(&font_big, T("PAUSED"), 160, 70, C_WHITE);
  int r = menu_list(items, 2, 124, 110, &sel, draw_frozen);
  if (r == 1) {
    /* back to the menu: the match resumes from its last safe point */
    np_jump(to_menu);
  }
  if (redraw) redraw();
  gfx_present();
}

/* the title: the Dealer waiting at his table, up close */
static int draw_title(void) {
  G.stage = 1, G.mode = MODE_STORY, G.sawed = G.dcuffed = 0;
  for (int g = 0; g < 8; g++) G.items[1][g] = IT_NONE;
  table_reset();
  view_dealer();
  table_draw();
  gfx_darken(0, 0, 130, 240, 2);
  gfx_darken(130, 0, 20, 240, 1);
  gfx_sprite(IMG_LOGO, 0, 0);
  return 0;
}

static void credits(void) {
  static const char *const lines =
      T("INSPIRED BY BUCKSHOT ROULETTE\nBY MIKE KLUBNIKA.\nNOT AFFILIATED WITH MIKE KLUBNIKA\nOR CRITICAL REFLEX.\n\nEVERY PICTURE IS RENDERED FROM THE\nGAME'S OWN SCENES, MODELS AND TEXTURES\nBY MIKE KLUBNIKA, THROUGH THE OPEN\nBUCKSHOT ROULETTE PROJECT (1503DEV).\nFONTS: FAKE RECEIPT BY RAY LARABIE,\nDOT MATRIX BY DIONAEA.\n\nMADE BY MASON CHEN AS PART OF NUMPLAY.");
  gfx_clear(C_BLACK);
  gfx_text_c(&font_big, T("CREDITS"), 160, 14, C_WHITE);
  gfx_text_c(&font_small, lines, 160, 38, C_WHITE);
  char best[NP_TEXT_EXTRA ? 80 : 40] = T("BEST DOUBLE OR NOTHING: ");
  int n = 0;
  while (best[n]) n++;
  story_money(best + n, P.best);
  gfx_text_c(&font_small, best, 160, 222, C_GREY);
  gfx_present();
  for (;;) {
    int k = key_wait(-1);
    if (IS_OK(k) || k == KEY_BACK) return;
  }
}

static void play(int resume) {
  in_match = 1;
  if (np_save_jump(to_menu)) {
    in_match = 0;
    gfx_light(256, 0, 0);
    gfx_shake(0, 0);
    if (have_checkpoint) G = checkpoint;
    return;
  }
  if (resume) {
    G = checkpoint;
    game_run();
  } else if (BR_START_STAGE) { /* development: straight to a stage */
    game_new(BR_START_MODE);
    G.stage = BR_START_STAGE - 1;
    G.name[0] = 'A', G.name[1] = 'B', G.name[2] = 'C';
    P.intro_line = INTRO_LINES, P.read_items = 1;
    P.loads_told = 2;
#ifdef BR_DEMO_TURN
    { /* development: a set turn for recordings (round 2, your glass, beer, cigarettes, cuffs, saw; his saw, glass, beer) */
      static const int8_t mine[8] = {1, 2, -1, 3, -1, 4, 0, -1}, his[8] = {0, -1, 1, -1, 2, -1, -1, -1};
      static const uint8_t shells[4] = {1, 0, 1, 1};
      G.phase = PH_PLAYER, G.load = 1, G.maxhp = 4, G.hp[0] = G.hp[1] = 3, G.nshell = G.nshown = 4;
      for (int i = 0; i < 4; i++) G.shell[i] = G.shown[i] = shells[i];
      for (int g = 0; g < 8; g++) G.items[0][g] = mine[g], G.items[1][g] = his[g], G.seq[0][g] = g, G.seq[1][g] = 8 + g;
      G.seqn = 16;
#if BR_DEMO_TURN == 2
      G.phase = PH_DEALER; /* his turn: he sees a live round and turns the barrel on you */
      G.items[1][2] = -1;
#endif
    }
#endif
    game_run();
  } else {
    G.phase = PH_RETRY;
  }
  /* a new run starts in the bathroom: from the menu, after "retry" in
   * heaven, and after dying in Double or Nothing */
  while (G.phase == PH_RETRY) {
    int mode = story_intro();
    if (mode < 0) break;
    game_new(mode);
    have_checkpoint = 0; /* a new run replaces the one on hold */
    story_enter(mode);
    game_run();
  }
  gfx_light(256, 0, 0);
  in_match = 0;
}

int main(void) {
  np_app_begin();
  if (np_save_jump(leave)) return quit();
  gfx_init();
#ifdef BR_TEST_DEATH
  G.mode = MODE_STORY;
  story_death();
  gfx_clear(C_WHITE);
  gfx_present();
  for (;;) key_wait(-1);
#endif
#ifdef BR_BENCH
  {
    /* development: how long the table view and its parts take to draw */
    uint32_t t[5];
    G.stage = 1;
    G.maxhp = G.hp[0] = G.hp[1] = 4;
    table_reset();
    for (int g = 0; g < 8; g++) G.items[0][g] = (int8_t)g, G.items[1][g] = (int8_t)(8 - g);
    uint64_t t0 = eadk_timing_millis();
    for (int i = 0; i < 10; i++) gfx_plate(IMG_PL_TABLE);
    t[0] = eadk_timing_millis() - t0, t0 = eadk_timing_millis();
    for (int i = 0; i < 10; i++) table_draw();
    t[1] = eadk_timing_millis() - t0, t0 = eadk_timing_millis();
    for (int i = 0; i < 10; i++) gfx_present();
    t[2] = eadk_timing_millis() - t0, t0 = eadk_timing_millis();
    for (int i = 0; i < 10; i++) gfx_plate(IMG_PL_HEALTH);
    t[3] = eadk_timing_millis() - t0, t0 = eadk_timing_millis();
    view_dealer();
    T.d.pose = IMG_D_GUN_AIMS;
    for (int i = 0; i < 10; i++) table_draw();
    t[4] = eadk_timing_millis() - t0;
    char s[80], *o = s;
    for (int k = 0; k < 5; k++) o = story_money(o, t[k]), *o++ = ' ';
    *o = 0;
    gfx_clear(C_BLACK);
    gfx_text(&font_big, s, 10, 100, C_WHITE);
    gfx_present();
    for (;;) key_wait(-1);
  }
#endif
  G.rng = eadk_random() | 1;
  load_save();
  int sel = 0;
  for (;;) {
    const char *items[4];
    int ids[4], n = 0;
    if (have_checkpoint) items[n] = T("CONTINUE"), ids[n++] = 0;
    items[n] = T("START"), ids[n++] = 1;
    items[n] = T("CREDITS"), ids[n++] = 2;
    items[n] = T("EXIT"), ids[n++] = 3;
    if (sel >= n) sel = 0;
    int r = menu_list(items, n, 36, 150, &sel, draw_title);
    if (r < 0 || ids[r] == 3) return quit();
    if (ids[r] == 2) credits();
    else play(ids[r] == 0);
  }
}
