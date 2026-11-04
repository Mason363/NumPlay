/* UNO on the NumWorks calculator: you against one to three bots, with the official rules and scoring to 500
 * points (game.c), the bots (bot.c) and the screens (draw.c).
 *
 * Keys: Left and Right pick a card, OK plays it, Up draws, shift says UNO (or catches a bot that forgot), Back
 * pauses; Home saves and quits. */
#include <string.h>
#include "draw.h"
#include "game.h"
#include "plat.h"
#include "save.h"

static uno_progress sv;
#define G (&sv.g)
static uno_ui ui;
static uint32_t now, bot_at, catch_at, end_at, saved_at;
static uint8_t catcher = UNO_NONE, open_seen = UNO_NONE;
static bool dirty;
/* frames drawn, the slowest (ms) and those over 33 ms: read by tools/emu.py */
#if !defined(HOST) && !defined(NP_PORT)
__attribute__((used, externally_visible))
#endif
uint32_t perf_frames, perf_max, perf_slow;

static const char *const COLOR_NAME[4] = {"Red", "Yellow", "Green", "Blue"};

static char *cat(char *p, const char *s) {
  while (*s) *p++ = *s++;
  *p = 0;
  return p;
}
static char *num(char *p, unsigned v) {
  char t[8];
  int n = 0;
  do t[n++] = (char)('0' + v % 10); while ((v /= 10) && n < 8);
  while (n) *p++ = t[--n];
  *p = 0;
  return p;
}
/* "Bot 1 draws" / "You draw" (mid: "you draw") */
static char *verb_as(char *p, int who, const char *one, const char *you, bool mid) {
  p = cat(p, who || !mid ? uno_name(who) : "you");
  *p++ = ' ';
  return cat(p, who ? one : you);
}
static char *verb(char *p, int who, const char *one, const char *you) { return verb_as(p, who, one, you, false); }
static char *verb_mid(char *p, int who, const char *one, const char *you) { return verb_as(p, who, one, you, true); }

static void save(void) {
  sv.setup = ui.setup, sv.has_game = ui.has_game;
  save_write(&sv);
  saved_at = now;
}

/* the message line, from what just happened */
static void describe(void) {
  uno_game *g = G;
  char *p = ui.msg, card[24];
  int who = g->ev_who;
  switch (g->ev) {
    case E_DEAL:
      p = verb(p, g->dealer, "deals: ", "deal: ");
      uno_card_name(g->ev_card, card);
      p = cat(p, card);
      if (g->ev_victim != UNO_NONE) p = cat(p, ", "), p = verb_mid(p, g->ev_victim, "draws 2", "draw 2");
      break;
    case E_PLAY:
      uno_card_name(g->ev_card, card);
      p = verb(p, who, "played ", "played ");
      p = cat(p, card);
      if (g->ev_victim != UNO_NONE) {
        p = cat(p, ": ");
        p = verb_mid(p, g->ev_victim, "draws ", "draw ");
        p = num(p, g->ev_n);
      }
      if (g->ev_by == who) p = cat(p, ", UNO!");
      break;
    case E_COLOR: p = verb(p, who, "called ", "called "), p = cat(p, COLOR_NAME[g->ev_n & 3]); break;
    case E_DRAW:
      if (!g->ev_n) p = verb(p, who, "had nothing to draw", "had nothing to draw");
      else if (g->ev_n == 1) p = verb(p, who, "drew a card", "drew a card");
      else p = verb(p, who, "drew ", "drew "), p = num(p, g->ev_n), p = cat(p, " cards");
      break;
    case E_KEEP: p = verb(p, who, "kept it", "kept it"); break;
    case E_TAKE: p = verb(p, who, "took ", "took "), p = num(p, g->ev_n), p = cat(p, " cards"); break;
    case E_CHALLENGE_WON:
      p = verb(p, who, "challenged: ", "challenged: ");
      p = verb_mid(p, g->ev_victim, "bluffed, draws 4", "bluffed, draw 4");
      break;
    case E_CHALLENGE_LOST: p = verb(p, who, "challenged and took ", "challenged and took "), p = num(p, g->ev_n); break;
    case E_UNO: p = cat(p, uno_name(who)), p = cat(p, ": UNO!"); break;
    case E_CAUGHT:
      p = verb(p, g->ev_by, "caught ", "caught ");
      p = cat(p, who ? uno_name(who) : "you");
      p = cat(p, ": 2 cards");
      break;
    default: break;
  }
  *p = 0;
}

/* someone just forgot UNO: the bots that will catch them, and when */
static void plan_catch(void) {
  uno_game *g = G;
  open_seen = g->uno_open, catcher = UNO_NONE;
  if (open_seen == UNO_NONE) return;
  uint32_t best = 0xFFFFFFFFu;
  for (int b = 1; b < g->r.players; b++) {
    uint32_t ms;
    if (b != open_seen && bot_catches(g, b, g->r.level, &ms) && ms < best) best = ms, catcher = (uint8_t)b;
  }
  if (catcher != UNO_NONE) catch_at = now + best;
  /* time for the forgetful to catch themselves, or for you to catch a bot, before the next player starts */
  if ((int32_t)(bot_at - (now + 1800)) < 0) bot_at = now + 1800;
}

static void after(uint32_t wait) {
  uno_game *g = G;
  bot_at = now + wait;
  if (g->uno_open != open_seen) plan_catch();
  if (g->phase >= P_HAND_OVER) {
    end_at = now + 1500, ui.show_end = 0, ui.sel = 0;
    if (g->phase == P_GAME_OVER) ui.has_game = 0;
    save();
  }
  if (g->turn == 0 && g->phase == P_DRAWN) ui.pick = g->drawn;
  if (g->count[0] && ui.pick >= g->count[0]) ui.pick = (uint8_t)(g->count[0] - 1);
  if (!g->count[0]) ui.pick = 0;
  dirty = true;
}

static void start(void) {
  uno_new_game(G, ui.setup, plat_random() ^ now ^ 0x5EED);
  ui.has_game = 1, ui.pick = 0, ui.show_end = 0, ui.screen = V_PLAY;
  open_seen = catcher = UNO_NONE;
  describe();
  after(1200);
  save();
}

/* Deal: how to play first, the first time */
static void deal(void) {
  if (!sv.guide) {
    sv.guide = 1;
    ui.first_run = 1, ui.page = 0, ui.under = V_SETUP, ui.screen = V_HELP;
    return;
  }
  start();
}

static void bot_step(void) {
  uno_game *g = G;
  int p = g->turn;
  uno_move m = bot_move(g, g->r.level);
  bot_do(g, m);
  describe();
  if (g->phase == P_COLOR && g->turn == p) {   /* its Wild: the colour at once */
    bool said = g->ev_by == p;
    bot_do(g, bot_move(g, g->r.level));
    char card[24], *q = ui.msg;
    uno_card_name(g->pile[g->pn - 1], card);
    q = cat(q, uno_name(p)), q = cat(q, " played "), q = cat(q, card), q = cat(q, ": ");
    q = cat(q, COLOR_NAME[g->color & 3]);
    if (said) cat(q, ", UNO!");
  }
  after(m.act == M_DRAW && g->phase == P_DRAWN ? 700 : 1000);
}

static void shift_key(void) {
  uno_game *g = G;
  if (g->uno_open != UNO_NONE && g->uno_open != 0) {
    if (uno_catch(g, 0)) describe(), after(1000);
  } else if (uno_call(g, 0)) {
    describe();
    after(g->turn == 0 ? 0 : 1000);
  } else {
    strcpy(ui.msg, "UNO is for your next-to-last card");
    dirty = true;
  }
}

static void human_play(int i) {
  uno_game *g = G;
  if (!uno_play(g, i, false)) {
    strcpy(ui.msg, g->phase == P_DRAWN ? "Only the new card can be played" : "That card doesn't go");
    dirty = true;
    return;
  }
  describe();
  if (g->phase == P_COLOR) ui.sel = (uint8_t)bot_best_color(g, 0, LEVEL_NORMAL);
  after(1000);
}

static void play_keys(uint32_t hit) {
  uno_game *g = G;
  if (hit & K_BACK) {
    ui.screen = V_PAUSE, ui.sel = PAUSE_RESUME;
    save();
    return;
  }
  if (g->phase >= P_HAND_OVER) {
    if (!ui.show_end) return;
    if (g->phase == P_GAME_OVER) {
      if (hit & (K_LEFT | K_RIGHT | K_UP | K_DOWN)) ui.sel ^= 1;
      if (hit & K_OK) {
        if (ui.sel == 0) start();
        else ui.screen = V_TITLE, ui.sel = 0, save();
      }
    } else if (hit & K_OK) {
      uno_next_hand(g);
      ui.pick = 0, ui.show_end = 0;
      describe();
      after(1200);
    }
    return;
  }
  if (hit & K_SHIFT) shift_key();
  int n = g->count[0];
  bool mine = g->turn == 0;
  if (mine && g->phase == P_COLOR) {
    if (hit & (K_LEFT | K_UP)) ui.sel = (uint8_t)((ui.sel + 3) % 4);
    if (hit & (K_RIGHT | K_DOWN)) ui.sel = (uint8_t)((ui.sel + 1) % 4);
    if ((hit & K_OK) && uno_choose(g, ui.sel)) describe(), after(1000);
    return;
  }
  if (mine && g->phase == P_PENALTY && g->challengeable) {
    int k = uno_pen_count(g);
    if (hit & K_UP) ui.sel = (uint8_t)((ui.sel + k - 1) % k);
    if (hit & K_DOWN) ui.sel = (uint8_t)((ui.sel + 1) % k);
    if (!(hit & K_OK)) return;
    int item = ui.sel % k;
    if (item == PEN_TAKE) {
      uno_take(g);
    } else if (item == PEN_CHALLENGE) {
      /* the challenged player shows their hand to the challenger */
      int o = g->offender;
      memcpy(ui.reveal, g->hand[o], g->count[o]);
      ui.reveal_n = g->count[o], ui.reveal_who = (uint8_t)o, ui.reveal_color = g->prev_color, ui.reveal_guilty = g->guilty;
      uno_challenge(g);
      ui.screen = V_REVEAL;
    } else {
      for (int i = 0; i < n; i++)
        if (uno_can_play(g, i)) {
          human_play(i);
          return;
        }
    }
    describe();
    after(1000);
    return;
  }
  if (n && (hit & (K_LEFT | K_RIGHT)) && !(mine && g->phase == P_DRAWN)) {
    ui.pick = (uint8_t)((ui.pick + (hit & K_RIGHT ? 1 : n - 1)) % n);
    dirty = true;
  }
  if (!mine) return;
  if (g->phase == P_DRAWN) {
    if (hit & K_OK) human_play(g->drawn);
    else if ((hit & K_UP) && uno_keep(g)) describe(), after(1000);
  } else if (g->phase == P_PENALTY) {
    if ((hit & K_OK) && n) human_play(ui.pick);
    else if ((hit & K_UP) && uno_take(g)) describe(), after(1000);
  } else if (g->phase == P_PLAY) {
    if ((hit & K_OK) && n) human_play(ui.pick);
    else if ((hit & K_UP) && uno_draw(g)) describe(), after(g->phase == P_DRAWN ? 0 : 1000);
  }
}

/* what happens by itself: the bots' turns, their catches, a penalty you can only take, the end of a hand */
static void tick(void) {
  uno_game *g = G;
  if (ui.screen != V_PLAY) return;
  if (g->phase >= P_HAND_OVER) {
    if (!ui.show_end && (int32_t)(now - end_at) >= 0) ui.show_end = 1, dirty = true;
    return;
  }
  if (catcher != UNO_NONE && g->uno_open == open_seen && (int32_t)(now - catch_at) >= 0) {
    int by = catcher;
    catcher = UNO_NONE;
    if (uno_catch(g, by)) describe(), after(1000);
    return;
  }
  if ((int32_t)(now - bot_at) < 0) return;
  if (g->turn != 0) bot_step();
  else if (g->phase == P_PENALTY && !g->challengeable && !uno_can_stack(g)) uno_take(g), describe(), after(1000);
}

/* back to the table from a pause: the bots wait a little */
static void resume(void) {
  ui.screen = V_PLAY;
  bot_at = now + 600;
  if (catcher != UNO_NONE && (int32_t)(catch_at - now) < 600) catch_at = now + 600;
}

int main(void) {
  plat_begin();
  memset(&ui, 0, sizeof ui);
  catcher = open_seen = UNO_NONE;
  save_load(&sv);
  ui.setup = sv.setup, ui.has_game = sv.has_game;
  if (ui.has_game) describe();
  ui.screen = V_TITLE, ui.sel = 0;
  uint32_t stale = plat_keys(), prev = stale, rep_at = 0;
  uint8_t shown = 0xFF;
  now = plat_millis();
  bot_at = now + 1000;
  dirty = true;
  for (;;) {
    now = plat_millis();
    uint32_t keys = plat_keys();
    stale &= keys;                      /* keys still down from the screen before don't count */
    uint32_t live = keys & ~stale, hit = live & ~prev;
    /* Left or Right held: again after 350 ms, then every 120 ms */
    uint32_t arrows = live & (K_LEFT | K_RIGHT);
    if (hit & arrows) rep_at = now + 350;
    else if (arrows && (int32_t)(now - rep_at) >= 0) hit |= arrows, rep_at = now + 120;
    prev = live;
    if (keys & K_HOME) break;           /* Home: saved below, then out */
    if (hit) dirty = true;
    bool quit = false;
    switch (ui.screen) {
      case V_TITLE: {
        int n = uno_menu_count(&ui);
        if (hit & K_UP) ui.sel = (uint8_t)((ui.sel + n - 1) % n);
        if (hit & K_DOWN) ui.sel = (uint8_t)((ui.sel + 1) % n);
        if (hit & K_BACK) quit = true;
        else if (hit & K_OK) switch (uno_menu_item(&ui, ui.sel % n)) {
          case MENU_CONTINUE: ui.screen = V_PLAY, after(1000); break;
          case MENU_NEW: ui.screen = V_SETUP, ui.under = V_TITLE, ui.sel = ROW_DEAL; break;
          default: ui.screen = V_HELP, ui.under = V_TITLE, ui.page = 0, ui.first_run = 0; break;
        }
        break;
      }
      case V_SETUP: {
        uno_rules *r = &ui.setup;
        if (hit & K_UP) ui.sel = (uint8_t)((ui.sel + SETUP_ROWS - 1) % SETUP_ROWS);
        if (hit & K_DOWN) ui.sel = (uint8_t)((ui.sel + 1) % SETUP_ROWS);
        int d = hit & K_RIGHT ? 1 : hit & K_LEFT ? -1 : 0;
        if (d && ui.sel == ROW_PLAYERS) r->players = (uint8_t)(2 + (r->players - 2 + d + 3) % 3);
        if (d && ui.sel == ROW_LEVEL) r->level = (uint8_t)((r->level + d + 3) % 3);
        if (d && ui.sel == ROW_LENGTH) r->one_hand ^= 1;
        bool toggle = ui.sel >= ROW_HOUSE && ui.sel < ROW_DEAL && (d || (hit & K_OK));
        if (toggle) r->house ^= (uint8_t)(1u << (ui.sel - ROW_HOUSE));
        if (hit & K_BACK) ui.screen = ui.under, ui.sel = ui.under == V_PAUSE ? PAUSE_NEW : (uint8_t)(uno_menu_count(&ui) - 2);
        else if ((hit & K_OK) && !toggle) deal();
        break;
      }
      case V_HELP:
        if (ui.first_run && (hit & K_BACK)) {
          ui.first_run = 0;
          start();
        } else if ((hit & K_RIGHT || (ui.first_run && (hit & K_OK))) && ui.page + 1 < UNO_HELP_PAGES) {
          ui.page++;
        } else if ((hit & K_LEFT) && ui.page) {
          ui.page--;
        } else if (ui.first_run && (hit & K_OK)) {
          ui.first_run = 0;
          start();
        } else if (hit & (K_BACK | K_OK)) {
          ui.screen = ui.under, ui.sel = ui.under == V_PAUSE ? PAUSE_HELP : (uint8_t)(uno_menu_count(&ui) - 1);
        }
        break;
      case V_PLAY: play_keys(hit); break;
      case V_REVEAL:
        if (hit & (K_OK | K_BACK)) resume();
        break;
      case V_PAUSE:
        if (hit & K_UP) ui.sel = (uint8_t)((ui.sel + PAUSE_COUNT - 1) % PAUSE_COUNT);
        if (hit & K_DOWN) ui.sel = (uint8_t)((ui.sel + 1) % PAUSE_COUNT);
        if (hit & K_BACK) resume();
        else if (hit & K_OK) switch (ui.sel) {
          case PAUSE_RESUME: resume(); break;
          case PAUSE_NEW: ui.screen = V_SETUP, ui.under = V_PAUSE, ui.sel = ROW_DEAL; break;
          case PAUSE_HELP: ui.screen = V_HELP, ui.under = V_PAUSE, ui.page = 0, ui.first_run = 0; break;
          default: ui.screen = V_QUIT, ui.sel = 0; break;
        }
        break;
      case V_QUIT:
        if (hit & (K_LEFT | K_RIGHT | K_UP | K_DOWN)) ui.sel ^= 1;
        if (hit & K_BACK) ui.screen = V_PAUSE, ui.sel = PAUSE_QUIT;
        else if (hit & K_OK) {
          if (ui.sel) quit = true;
          else ui.screen = V_PAUSE, ui.sel = PAUSE_QUIT;
        }
        break;
    }
    if (quit) break;
    tick();
    if (ui.has_game && (int32_t)(now - saved_at) >= 30000) save();
    if (ui.screen != shown) {
      stale = keys, prev = 0;            /* a new screen starts with all keys up */
      shown = ui.screen;
      dirty = true;
    }
    if (dirty) {
      ui.clock = now;
      uint32_t t0 = plat_millis();
      uno_render(G, &ui);
      uint32_t spent = plat_millis() - t0;
      perf_frames++;
      if (spent > perf_max) perf_max = spent;
      if (spent > 33) perf_slow++;
      dirty = false;
    } else {
      plat_sleep(10);
    }
  }
  save();
  return plat_end();
}
