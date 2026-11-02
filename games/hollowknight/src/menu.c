/* The title screen, the save profiles and the pause menu (UIManager: Menu_Title's MainMenuScreen, SaveProfileScreen and
 * PauseMenuScreen, their pieces where tools/menu.py found them): what is selected, what the keys do. And how to play
 * on the calculator (its keys, as the Options' Keyboard screen shows them, then the basics): before a new game, and
 * from the title and the pause menu, where Options is. */
#pragma GCC optimize("Os")   /* (its code small: not where a frame's time goes) */
#include "game.h"

#define DT 0.02f
#define HUD_PX (FOCAL / (-1.342f - CAM_Z))   /* (pixels a HUD unit) */
#define FADE_TIME 0.2f                       /* (MENU_FADE_SPEED 5: a screen fades in, out in a fifth of a second) */

typedef struct {
  int16_t sprite;
  float ox, oy, sx, sy;   /* (its pivot from its rect's center; its scale as drawn) */
} Piece;
typedef struct {
  uint8_t zone;
  Piece p;
} ZonePiece;

static const Piece logo = MENU_LOGO, pointer = MENU_POINTER, profiles_fleur = MENU_PROFILES_FLEUR,
                   slot_fleur = MENU_SLOT_FLEUR, slot_selector = MENU_SLOT_SELECTOR, slot_cursor = MENU_SLOT_CURSOR,
                   slot_orb = MENU_SLOT_ORB, slot_geo = MENU_SLOT_GEO, slot_health = MENU_SLOT_HEALTH,
                   pause_top = MENU_PAUSE_TOP, pause_bot = MENU_PAUSE_BOT, keys_fleur = MENU_KEYS_FLEUR;
static const int16_t key_texts[MENU_NKEYS][2] = MENU_KEYS;   /* (what a key does, the key) */
static const ZonePiece zone_bgs[MENU_SLOT_NZONES] = MENU_SLOT_ZONES;
static const int16_t zone_texts[MENU_SLOT_NZONES] = TXT_ZONES;

enum { MS_TITLE, MS_PROFILES, MS_GAME, MS_PAUSE, MS_HELP, MS_LOADING, MS_CROSS };
/* (the save profiles' rows: the slots, then Back; a slot's Clear Save, its prompt's Yes and No) */
enum { PS_SLOT, PS_CLEAR, PS_PROMPT_NO, PS_PROMPT_YES };

static struct {
  uint8_t screen, sel, part;
  uint8_t page, from, slot;   /* (how to play: its page; the screen it was opened from, the new game's slot) */
  uint32_t prev_keys;
  float t;                 /* (time on this screen: its fade in) */
  int8_t state[SAVE_SLOTS];   /* (save_stats: 0 none, 1 a game, -1 corrupted) */
  SaveStats stats[SAVE_SLOTS];
  char num[SAVE_SLOTS][3], geo[SAVE_SLOTS][12], time[SAVE_SLOTS][16];
  bool quit, fresh;           /* (loading: a new game) */
  uint8_t cross_part;         /* (MS_CROSS: the part of Hallownest the way leads into) */
} mn;

/* a number's digits at p (at least `width`, zero padded) -> past them */
static char *digits(char *p, uint32_t v, int width) {
  char t[10];
  int n = 0;
  do t[n++] = (char)('0' + v % 10), v /= 10;
  while (v && n < 10);
  while (n < width && n < 10) t[n++] = '0';
  while (n) *p++ = t[--n];
  return p;
}

static void read_slots(void) {
  for (int i = 0; i < SAVE_SLOTS; i++) {
    mn.state[i] = (int8_t)save_stats(i, &mn.stats[i]);
    mn.num[i][0] = (char)('1' + i), mn.num[i][1] = '.', mn.num[i][2] = 0;
    *digits(mn.geo[i], mn.stats[i].geo > 0 ? (uint32_t)mn.stats[i].geo : 0, 1) = 0;
    /* (SaveStats.GetPlaytimeHHMM: hours if any, then minutes) */
    float pt = mn.stats[i].play_time;
    unsigned m = pt > 0 && pt < 3.6e8f ? (unsigned)(pt / 60) : 0, h = m / 60;
    char *q = mn.time[i];
    if (h) q = digits(q, h % 100000u, 1), *q++ = 'h', *q++ = ' ', q = digits(q, m % 60, 2);
    else q = digits(q, m, 1);
    q[0] = 'm', q[1] = 0;
  }
}

static void go(int screen, int sel) {
  mn.screen = (uint8_t)screen, mn.sel = (uint8_t)sel, mn.part = PS_SLOT, mn.t = 0;
  if (screen == MS_PROFILES) read_slots();
}

void menu_start(void) {
  memset(&mn, 0, sizeof mn);
  save_restore();   /* (the saves the app's install took away: from their copy) */
  go(MS_TITLE, 0);
}

bool menu_quit(void) { return mn.quit; }

static const int16_t cross_apps[CROSS_NAPPS] = TXT_CROSS_APPS;
void menu_cross(int part) {
  go(MS_CROSS, 0);
  mn.cross_part = (uint8_t)part;
}
bool menu_in_game(void) {
  return mn.screen == MS_GAME || mn.screen == MS_PAUSE || (mn.screen == MS_HELP && mn.from == MS_PAUSE);
}

/* a new game in a slot: the Knight falling into King's Pass from its top (the opening's entry gate, top1); else at its
 * respawn point */
static void new_game(int slot) {
  game_new();
  save_select(slot);
#ifdef ROOM_TUTORIAL_01   /* (the first part's) */
  if (game_enter_gate(ROOM_TUTORIAL_01, "top1")) {
    g_game.fade = 1, g_screen_fade = 255;   /* (from black) */
    game_fade_scene_in();
    return;
  }
  room_load(ROOM_TUTORIAL_01);
  int n;
  const Ent *e = room_ents(&n);
  float x = 35.46f, y = 11.37f;
  bool right = true;
  for (int i = 0; i < n; i++)
    if (e[i].type == ENT_RESPAWN) x = e[i].x0, y = e[i].y0, right = e[i].flags & FACING_RIGHT;
  game_enter(ROOM_TUTORIAL_01, x, y, right);
#endif
}

static void help_open(int from, int slot) {
  go(MS_HELP, 0);
  mn.from = (uint8_t)from, mn.slot = (uint8_t)slot, mn.page = 0;
}
static void help_close(void) {   /* (back where it was opened, on its How to Play) */
  if (mn.from == MS_PROFILES) go(MS_PROFILES, mn.slot);
  else go(mn.from, 1);
}

/* (a slot chosen: its game loaded, at its bench; or a new one) */
static void pick_slot(int slot) {
  if (mn.state[slot] == 1) {
    /* (the slot says Loading..., the screen fades out, then the game loads) */
    mn.screen = MS_LOADING, mn.fresh = false, mn.slot = (uint8_t)slot, mn.t = 0;
  } else if (mn.state[slot] == 0 && HK_PART_N == 1)
    help_open(MS_PROFILES, slot);   /* (how to play first; a new game begins in the first part only) */
  /* (a corrupted save: only cleared) */
}

/* the screen faded out: the game loads (a new one from how to play, or the slot's) */
static void load_game(void) {
  int slot = mn.slot;
  if (mn.fresh) {
    new_game(slot);
    go(MS_GAME, 0);
    return;
  }
  game_new();
  /* (the way the Knight went into this part of Hallownest from another; else his bench; else, his bench in another
   * part, where this part's world meets it) */
  bool loaded = save_load(slot) && (game_enter_cross() || game_respawn() || game_enter_home());
  if (!loaded) {
    /* (a file that does not load after all: shown as corrupted) */
    go(MS_PROFILES, slot);
    mn.state[slot] = -1;
    return;
  }
  go(MS_GAME, 0);
}

/* the keys newly down this frame */
static uint32_t pressed(uint32_t keys) { return keys & ~mn.prev_keys; }

bool menu_tick(uint32_t keys) {
  uint32_t p = pressed(keys);
  mn.prev_keys = keys;
  mn.t += DT;
  switch (mn.screen) {
    case MS_TITLE:
      /* Start Game, How to Play, Quit Game */
      if (p & K_UP) mn.sel = (uint8_t)((mn.sel + 2) % 3);
      if (p & K_DOWN) mn.sel = (uint8_t)((mn.sel + 1) % 3);
      if (p & K_OK) {
        if (mn.sel == 0) go(MS_PROFILES, 0);
        else if (mn.sel == 1) help_open(MS_TITLE, 0);
        else mn.quit = true;
      }
      return true;
    case MS_PROFILES:
      if (mn.part >= PS_PROMPT_NO) {
        /* Clear Save?: No, Yes */
        if (p & (K_LEFT | K_RIGHT)) mn.part = mn.part == PS_PROMPT_NO ? PS_PROMPT_YES : PS_PROMPT_NO;
        if (p & K_BACK) mn.part = PS_CLEAR;
        else if (p & K_OK) {
          if (mn.part == PS_PROMPT_YES) save_clear(mn.sel), read_slots(), mn.part = PS_SLOT;
          else mn.part = PS_CLEAR;
        }
        return true;
      }
      if (p & K_UP) mn.sel = (uint8_t)(mn.sel ? mn.sel - 1 : SAVE_SLOTS), mn.part = PS_SLOT;
      if (p & K_DOWN) mn.sel = (uint8_t)(mn.sel < SAVE_SLOTS ? mn.sel + 1 : 0), mn.part = PS_SLOT;
      /* (a slot with a game, or a corrupted one: its Clear Save, to the right) */
      if ((p & K_RIGHT) && mn.sel < SAVE_SLOTS && mn.state[mn.sel]) mn.part = PS_CLEAR;
      if (p & K_LEFT) mn.part = PS_SLOT;
      if (p & K_BACK) go(MS_TITLE, 0);
      else if (p & K_OK) {
        if (mn.sel == SAVE_SLOTS) go(MS_TITLE, 0);
        else if (mn.part == PS_CLEAR) mn.part = PS_PROMPT_NO;
        else pick_slot(mn.sel);
      }
      return true;
    case MS_GAME:
      /* (the pause key: the game paused, unless PlayerData's disablePause or a room changing) */
      if ((p & K_PAUSE) && !g_pd.disable_pause && !game_changing_room()) {
        go(MS_PAUSE, 0);
        return true;
      }
      return false;
    case MS_HELP:
      /* its two pages; Back goes back, OK past the second starts the new game (or goes back) */
      if (p & K_BACK) help_close();
      else if (mn.page == 0 && (p & (K_OK | K_RIGHT))) mn.page = 1;
      else if (mn.page == 1 && (p & K_LEFT)) mn.page = 0;
      else if (mn.page == 1 && (p & K_OK)) {
        if (mn.from == MS_PROFILES) mn.screen = MS_LOADING, mn.fresh = true, mn.t = 0;
        else help_close();
      }
      return true;
    case MS_LOADING:
      /* (drawn once faded: then the load, the screen as it was meanwhile) */
      if (mn.t > FADE_TIME + DT) load_game();
      return true;
    case MS_CROSS:
      /* (the way on is in another app: Quit Game; Back, the title) */
      if (p & K_OK) mn.quit = true;
      else if (p & K_BACK) go(MS_TITLE, 0);
      return true;
    case MS_PAUSE:
      /* Continue, How to Play, Quit to Menu */
      if (p & K_UP) mn.sel = (uint8_t)((mn.sel + 2) % 3);
      if (p & K_DOWN) mn.sel = (uint8_t)((mn.sel + 1) % 3);
      if (p & (K_PAUSE | K_BACK)) go(MS_GAME, 0);
      else if (p & K_OK) {
        if (mn.sel == 0) go(MS_GAME, 0);
        else if (mn.sel == 1) help_open(MS_PAUSE, 0);
        else {
          /* Quit to Menu: the game saved (the Knight at his bench as it loads), the title */
          save_game();
          go(MS_TITLE, 0);
        }
      }
      return true;
  }
  return false;
}

/* ---------------------------------------------------------------- drawing */
static uint8_t white(int slot, float a) { return gfx_dyn_tint(29 + slot, 255, 255, 255, (uint8_t)(a * 255 + 0.5f)); }

static void piece(const Piece *pc, float cx, float cy, bool flip, uint8_t tint) {
  if (pc->sprite < 0) return;   /* (none: an area of another part's, by name only) */
  Inst in;
  sprite_inst(pc->sprite, cx + (flip ? -pc->ox : pc->ox), cy + pc->oy, 0, flip ? -pc->sx : pc->sx, pc->sy, tint, &in);
  gfx_hud(&in, 0);
}

/* a text: its x its center (align 0), left (1) or right (2), y its middle (HUD units) -> its width (HUD units) */
static float text_at(int style, const uint8_t *s, int n, float x, float y, int align, float a) {
  const uint8_t *st = font_style(style);
  float w = text_width(style, s, n), px = VIEW_W / 2 + x * HUD_PX - (align == 0 ? w / 2 : align == 2 ? w : 0);
  /* (Trajan's capitals: their middle at y) */
  gfx_text(style, px, VIEW_H / 2 - y * HUD_PX + font_asc(st) * 0.36f, s, n, (uint32_t)(a * 255 + 0.5f) << 24 | 0xFFFFFF, 0);
  return w / HUD_PX;
}

static float text_id(int style, int id, float x, float y, int align, float a) {
  const uint8_t *s = text_get(id);
  int n = 0;
  while (s[n] && s[n] != TEXT_BR) n++;
  return text_at(style, s, n, x, y, align, a);
}

/* a button: its text centered on x at y, its pointers either side if selected */
static void button(int text, float x, float y, bool sel, float a) {
  float w = text_id(STYLE_MENU, text, x, y, 0, a);
  if (!sel) return;
  piece(&pointer, x - w / 2 - MENU_POINTER_GAP, y, false, white(0, a));
  piece(&pointer, x + w / 2 + MENU_POINTER_GAP, y, true, white(0, a));
}

static void draw_slot(int i, float a) {
  float cx = MENU_SLOT_X, cy = MENU_SLOT_Y + i * MENU_SLOT_DY;
  bool sel = mn.sel == i;
  uint8_t full = white(0, a);
  if (sel && mn.part == PS_SLOT) {
    /* (Selector: the slot lit; its cursors) */
    piece(&slot_selector, cx + MENU_SLOT_SELECTOR_X, cy + MENU_SLOT_SELECTOR_Y, false, white(1, a * 0.5f));
    piece(&slot_cursor, cx + MENU_SLOT_CURSOR_X, cy + MENU_SLOT_CURSOR_Y, false, full);
    piece(&slot_cursor, cx + MENU_SLOT_CURSOR_X2, cy + MENU_SLOT_CURSOR_Y, true, full);
  }
  text_at(STYLE_MENU_TITLE, (const uint8_t *)mn.num[i], 2, cx + MENU_SLOT_NUM_X, cy, 0, a);
  if (mn.part >= PS_PROMPT_NO && sel) {
    /* Clear Save?: No, Yes */
    text_id(STYLE_MENU, TXT_PROFILE_CLEAR_PROMPT, cx + MENU_SLOT_PROMPT_X, cy, 0, a);
    float wy = text_id(STYLE_MENU, TXT_NAV_YES, cx + MENU_SLOT_YES_X, cy, 0, a);
    float wn = text_id(STYLE_MENU, TXT_NAV_NO, cx + MENU_SLOT_NO_X, cy, 0, a);
    float x = mn.part == PS_PROMPT_YES ? cx + MENU_SLOT_YES_X : cx + MENU_SLOT_NO_X, w = mn.part == PS_PROMPT_YES ? wy : wn;
    piece(&pointer, x - w / 2 - MENU_POINTER_GAP, cy, false, full);
    piece(&pointer, x + w / 2 + MENU_POINTER_GAP, cy, true, full);
    return;
  }
  if (mn.state[i] == 0) {
    /* (a new game: in the first part only) */
    text_id(HK_PART_N == 1 ? STYLE_MENU : STYLE_MENU_SMALL, HK_PART_N == 1 ? TXT_PROFILE_NEW_GAME : TXT_CROSS_NEW,
            cx + MENU_SLOT_NEW_X, cy, 1, a);
    return;
  }
  if (mn.state[i] < 0) {
    text_id(STYLE_MENU, TXT_PROFILE_CORRUPTED, cx + MENU_SLOT_NEW_X, cy, 1, a);
  } else {
    const SaveStats *st = &mn.stats[i];
    /* its area's background (a zone without one: none), its fleur, its masks, soul orb, geo, area, play time */
    for (int k = 0; k < MENU_SLOT_NZONES; k++)
      if (zone_bgs[k].zone == st->zone) piece(&zone_bgs[k].p, cx, cy + MENU_SLOT_BG_DY, false, full);
    piece(&slot_fleur, cx + MENU_SLOT_FLEUR_X, cy + MENU_SLOT_FLEUR_Y, false, full);
    piece(&slot_orb, cx + MENU_SLOT_ORB_X, cy + MENU_SLOT_ORB_Y, false, full);
    for (int k = 0; k < st->max_health && k < 11; k++)
      piece(&slot_health, cx + MENU_SLOT_HEALTH_X + k * MENU_SLOT_HEALTH_DX, cy + MENU_SLOT_HEALTH_Y, false, full);
    piece(&slot_geo, cx + MENU_SLOT_GEO_X, cy + MENU_SLOT_GEO_Y, false, full);
    text_at(STYLE_MENU_SMALL, (const uint8_t *)mn.geo[i], (int)strlen(mn.geo[i]), cx + MENU_SLOT_GEOTXT_X,
            cy + MENU_SLOT_GEOTXT_Y, 1, a);
    for (int k = 0; k < MENU_SLOT_NZONES; k++)
      if (zone_bgs[k].zone == st->zone)
        text_id(STYLE_MENU_SMALL, zone_texts[k], cx + MENU_SLOT_LOC_X, cy + MENU_SLOT_LOC_Y, 2, a);
    text_at(STYLE_MENU_SMALL, (const uint8_t *)mn.time[i], (int)strlen(mn.time[i]), cx + MENU_SLOT_TIME_X,
            cy + MENU_SLOT_TIME_Y, 2, a);
  }
  /* Clear Save, to the right */
  float w = text_id(STYLE_MENU, TXT_PROFILE_CLEAR_BUTTON, cx + MENU_SLOT_CLEAR_X, cy, 0, a);
  if (sel && mn.part == PS_CLEAR) {
    piece(&pointer, cx + MENU_SLOT_CLEAR_X - w / 2 - MENU_POINTER_GAP, cy, false, full);
    piece(&pointer, cx + MENU_SLOT_CLEAR_X + w / 2 + MENU_POINTER_GAP, cy, true, full);
  }
}

/* how to play: the keys (two columns: what a key does, the key), or the basics; under them Next, then Start Game (a
 * new game) or Back */
static void draw_help(float a, bool loading) {
  float hw = VIEW_W / 2 / HUD_PX - 0.6f;   /* (the columns: from the screen's edges to its middle) */
  text_id(STYLE_MENU_TITLE, mn.page ? TXT_HELP_TITLE : TXT_SCREEN_KEYBOARD, 0, MENU_KEYS_TITLE_Y, 0, a);
  piece(&keys_fleur, 0, MENU_KEYS_FLEUR_Y, false, white(0, a));
  if (mn.page == 0) {
    for (int i = 0; i < MENU_NKEYS; i++) {
      int col = i < 6 ? 0 : 1, row = col ? i - 6 : i;
      float y = MENU_KEYS_ROW_Y + row * MENU_KEYS_DY, x0 = col ? 0.5f : -hw, x1 = col ? hw : -0.5f;
      text_id(STYLE_MENU_SMALL, key_texts[i][0], x0, y, 1, a);
      text_id(STYLE_MENU_SMALL, key_texts[i][1], x1, y, 2, a * 0.75f);
    }
  } else {
    /* (the lines, from the first row down) */
    const uint8_t *s = text_get(TXT_HELP_LINES);
    float line = font_line(font_style(STYLE_TUTE)) / HUD_PX, y = MENU_KEYS_ROW_Y + 0.2f;
    while (*s) {
      int n = 0;
      while (s[n] && s[n] != TEXT_BR) n++;
      text_at(STYLE_TUTE, s, n, 0, y, 0, a);
      s += n + (s[n] == TEXT_BR);
      y -= line;
    }
  }
  if (loading)
    text_id(STYLE_MENU, TXT_PROFILE_LOADING, 0, MENU_KEYS_BUTTON_Y, 0, 1);   /* (in its button's place) */
  else
    button(mn.page == 0 ? TXT_NEXT : mn.from == MS_PROFILES ? TXT_MAIN_START : TXT_NAV_BACK, 0, MENU_KEYS_BUTTON_Y, true, a);
}

void menu_draw(void) {
  float a = mn.t / FADE_TIME;
  if (a > 1) a = 1;
  switch (mn.screen) {
    case MS_TITLE:
      piece(&logo, 0, MENU_LOGO_Y, false, white(0, a));
      button(TXT_MAIN_START, 0, MENU_START_Y, mn.sel == 0, a);
      button(TXT_HELP, 0, MENU_HELP_Y, mn.sel == 1, a);
      button(TXT_MAIN_QUIT, 0, MENU_QUIT_Y, mn.sel == 2, a);
      break;
    case MS_PROFILES:
      text_id(STYLE_MENU_TITLE, TXT_SCREEN_SAVE_PROFILES, 0, MENU_PROFILES_TITLE_Y, 0, a);
      piece(&profiles_fleur, 0, MENU_PROFILES_FLEUR_Y, false, white(0, a));
      for (int i = 0; i < SAVE_SLOTS; i++) draw_slot(i, a);
      button(TXT_NAV_BACK, 0, MENU_BACK_Y, mn.sel == SAVE_SLOTS, a);
      break;
    case MS_PAUSE:
      /* (the modal dimmer: the game behind at a fifth) */
      gfx_hud_fill(-15, -9, 15, 9, gfx_dyn_tint(31, 0, 0, 0, (uint8_t)(0.8f * a * 255 + 0.5f)));
      piece(&pause_top, 0, MENU_PAUSE_TOP_Y, false, white(0, a));
      piece(&pause_bot, 0, MENU_PAUSE_BOT_Y, false, white(0, a));
      button(TXT_PAUSE_CONTINUE, 0, MENU_PAUSE_CONTINUE_Y, mn.sel == 0, a);
      button(TXT_HELP, 0, MENU_PAUSE_HELP_Y, mn.sel == 1, a);
      button(TXT_PAUSE_MAIN, 0, MENU_PAUSE_QUIT_Y, mn.sel == 2, a);
      break;
    case MS_HELP:
      if (mn.from == MS_PAUSE) gfx_hud_fill(-15, -9, 15, 9, gfx_dyn_tint(31, 0, 0, 0, (uint8_t)(0.8f * 255 + 0.5f)));
      draw_help(a, false);
      break;
    case MS_CROSS:
      /* the way on: in which app; the game saved; Quit Game */
      piece(&logo, 0, MENU_LOGO_Y, false, white(0, a));
      text_id(STYLE_MENU, TXT_CROSS_TO, 0, MENU_START_Y + 0.9f, 0, a);
      text_id(STYLE_MENU, cross_apps[mn.cross_part < CROSS_NAPPS ? mn.cross_part : 0], 0, MENU_START_Y, 0, a);
      text_id(STYLE_MENU_SMALL, TXT_CROSS_SAVED, 0, MENU_HELP_Y, 0, a);
      button(TXT_MAIN_QUIT, 0, MENU_QUIT_Y, true, a);
      break;
    case MS_LOADING: {
      /* what was there fading out, Loading... where it was chosen (LoadingGameText) */
      float out = 1 - a;
      if (mn.fresh) {
        draw_help(out, true);
        break;
      }
      text_id(STYLE_MENU_TITLE, TXT_SCREEN_SAVE_PROFILES, 0, MENU_PROFILES_TITLE_Y, 0, out);
      piece(&profiles_fleur, 0, MENU_PROFILES_FLEUR_Y, false, white(0, out));
      for (int i = 0; i < SAVE_SLOTS; i++)
        if (i == mn.slot)
          text_id(STYLE_MENU, TXT_PROFILE_LOADING, MENU_SLOT_X + MENU_SLOT_NEW_X, MENU_SLOT_Y + i * MENU_SLOT_DY, 1, 1);
        else
          draw_slot(i, out);
      button(TXT_NAV_BACK, 0, MENU_BACK_Y, false, out);
      break;
    }
  }
}
