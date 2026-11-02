/* The screens: the title, the deck, how to play, the battle with its pause and result. */
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "gfx.h"
#include "res.h"

Game g;
Battle bt;

#define GOLD RGB(0xFFD23C)
#define BLUE RGB(0x3C8CF0)
#define RED RGB(0xE8403C)
#define PINK RGB(0xD83CE0)
#define PANEL RGB(0x1C2A4A)

/* arenas.csv: Arena1 to Arena7 from these trophies; TID_INFO_ARENA1 to 7 */
static const int32_t ARENA_TROPHIES[NARENAS] = {0, 400, 800, 1100, 1400, 1700, 3000};
static const char *const ARENA_NAMES[NARENAS] = {"Goblin Stadium", "Bone Pit", "Barbarian Bowl", "P.E.K.K.A's Playhouse",
                                                 "Spell Valley", "Royal Arena", "Legendary Arena"};
int arena_of(int32_t trophies) {
  int a = 0;
  for (int i = 0; i < NARENAS; i++)
    if (trophies >= ARENA_TROPHIES[i]) a = i;
  return a;
}
const char *arena_name(int a) { return ARENA_NAMES[a < 0 ? 0 : a >= NARENAS ? NARENAS - 1 : a]; }

/* the training camp's BasicDeck16: Knight, Archers, Skeleton Army, Goblins, Fireball, Giant, Valkyrie, Mini P.E.K.K.A. */
static const uint8_t DEFAULT_DECK[8] = {CA_KNIGHT, CA_ARCHER, CA_SKELETONARMY, CA_GOBLINS, CA_FIREBALL, CA_GIANT, CA_VALKYRIE, CA_MINIPEKKA};

static void go(int screen) {
  g.screen = screen;
  g.ms = 0;
  g.sel = 0;
  g.confirm = false;
}

void game_init(void) {
  memset(&g, 0, sizeof g);
  g.running = true;
  memcpy(g.prof.deck, DEFAULT_DECK, 8);
  save_load();
  g.hold = -1;
  g.cx = 11, g.cy = 3;
  go(SC_TITLE);
  if (resume_load()) {
    view_load_arena(bt.arena);
    go(SC_PAUSE);
  }
}

static void start_battle(void) {
  uint32_t seed = plat_millis() * 2654435761u ^ (uint32_t)g.prof.wins * 7919u ^ (uint32_t)g.prof.losses * 104729u;
  if (!seed) seed = 1;
  int arena = arena_of(g.prof.trophies);
  uint32_t r = seed;
  int deck = (int)(sim_rand(&r) % NBOTDECKS);
  battle_init(&bt, g.prof.deck, BOT_DECKS[deck], arena, seed);
  view_load_arena(arena);
  g.field = false;
  g.slot = 0;
  g.cx = 11, g.cy = 3;
  g.msg = NULL;
  g.steps = 0;
  go(SC_BATTLE);
}

static void finish_battle(void) {
  /* trophies: K_FACTOR 60 against an even opponent, half of it */
  g.old_arena = arena_of(g.prof.trophies);
  g.dtroph = bt.winner == 0 ? 30 : bt.winner == 1 ? -30 : 0;
  if (g.prof.trophies + g.dtroph < 0) g.dtroph = -g.prof.trophies;
  g.prof.trophies += g.dtroph;
  if (g.prof.trophies > g.prof.best) g.prof.best = g.prof.trophies;
  if (bt.winner == 0) g.prof.wins++;
  else if (bt.winner == 1) g.prof.losses++;
  else g.prof.draws++;
  if (bt.winner == 0 && bt.side[0].crowns == 3) g.prof.three++;
  save_write();
  resume_clear();
  go(SC_RESULT);
}

/* ------------------------------------------------------------------ keys */
static uint32_t pressed(void) { return g.keys & ~g.prev; }

/* arrows: once, then again every 90 ms after 300 ms held */
static uint32_t arrows(void) {
  uint32_t a = g.keys & (K_LEFT | K_RIGHT | K_UP | K_DOWN);
  if (!a) return 0;
  if (a & ~g.prev) return a & ~g.prev;
  if (g.held_ms >= 300 && (g.held_ms - 300) % 90 < 17) return a;
  return 0;
}
static bool ok(void) { return pressed() & (K_OK | K_EXE); }

static void say(const char *m) {
  g.msg = m;
  g.msg_t = 90;
}

/* ------------------------------------------------------------------ the battle's keys */
static void battle_keys(void) {
  uint32_t p = pressed(), a = arrows();
  for (int k = 0; k < 4; k++)
    if (p & K_DIGIT(k + 1)) {
      g.slot = k;
      g.field = true;
      return;
    }
  if (!g.field) {
    if (a & K_LEFT) g.slot = (g.slot + 3) & 3;
    if (a & K_RIGHT) g.slot = (g.slot + 1) & 3;
    if (ok() || (p & K_UP)) g.field = true;
    else if (p & K_BACK) go(SC_PAUSE);
    return;
  }
  if (a & K_LEFT && g.cx > 0) g.cx--;
  if (a & K_RIGHT && g.cx < 31) g.cx++;
  if (a & K_UP && g.cy > 0) g.cy--;
  if (a & K_DOWN && g.cy < 17) g.cy++;
  if (p & K_BACK) g.field = false;
  if (ok()) {
    int32_t x = g.cx * 1000 + 500, y = g.cy * 1000 + 500;
    const char *why = battle_check(&bt, 0, g.slot, x, y);
    if (why) say(why);
    else if (battle_play(&bt, 0, g.slot, x, y)) g.field = false;
  }
}

/* ------------------------------------------------------------------ the deck's keys */
#define DCOLS 8
#define DROWS_SHOWN 3
static int deck_has(int card) {
  for (int k = 0; k < 8; k++)
    if (g.prof.deck[k] == card) return k;
  return -1;
}
static int deck_cursor_card(void) {
  if (g.drow == 0) return g.prof.deck[g.dcol];
  int i = (g.drow - 1) * DCOLS + g.dcol;
  return i < NCARDS ? i : -1;
}

static void deck_keys(void) {
  uint32_t a = arrows(), p = pressed();
  int rows = 1 + (NCARDS + DCOLS - 1) / DCOLS;
  if (a & K_LEFT && g.dcol > 0) g.dcol--;
  if (a & K_RIGHT && g.dcol < DCOLS - 1) g.dcol++;
  if (a & K_UP && g.drow > 0) g.drow--;
  if (a & K_DOWN && g.drow < rows - 1) g.drow++;
  while (g.drow > 0 && (g.drow - 1) * DCOLS + g.dcol >= NCARDS) g.dcol--;
  if (g.drow > 0) {
    if (g.drow - 1 < g.dscroll) g.dscroll = g.drow - 1;
    if (g.drow - 1 >= g.dscroll + DROWS_SHOWN) g.dscroll = g.drow - DROWS_SHOWN;
  }
  if (p & K_BACK) {
    if (g.hold >= 0) g.hold = -1;
    else {
      save_write();
      go(SC_TITLE);
      g.sel = 1;
    }
    return;
  }
  if (!ok()) return;
  int card = deck_cursor_card();
  if (card < 0) return;
  if (g.hold < 0) {
    /* pick one: from the deck, or a card not in it */
    if (g.drow == 0) g.hold = 100 + g.dcol;
    else if (deck_has(card) < 0) g.hold = card, g.drow = 0;
    else g.drow = 0, g.dcol = deck_has(card);
    return;
  }
  if (g.hold >= 100) {
    int slot = g.hold - 100;
    if (g.drow == 0) {   /* two deck cards: swapped */
      uint8_t v = g.prof.deck[slot];
      g.prof.deck[slot] = g.prof.deck[g.dcol], g.prof.deck[g.dcol] = v;
    } else if (deck_has(card) < 0) {
      g.prof.deck[slot] = (uint8_t)card;
    }
  } else if (g.drow == 0) {
    g.prof.deck[g.dcol] = (uint8_t)g.hold;
  }
  g.hold = -1;
  save_write();
}

/* ------------------------------------------------------------------ the step */
void game_step(uint32_t keys) {
  g.prev = g.keys;
  g.keys = keys;
  g.held_ms = (keys & (K_LEFT | K_RIGHT | K_UP | K_DOWN)) && (keys & (K_LEFT | K_RIGHT | K_UP | K_DOWN)) == (g.prev & (K_LEFT | K_RIGHT | K_UP | K_DOWN))
                  ? g.held_ms + 17 : 0;
  g.ms += 17;
  if (g.msg_t > 0) g.msg_t--;
  if (keys & K_HOME) {
    if (g.screen == SC_BATTLE || g.screen == SC_PAUSE) resume_write();
    save_write();
    g.running = false;
    return;
  }
  uint32_t p = pressed(), a = arrows();
  switch (g.screen) {
  case SC_TITLE:
    if (a & K_UP) g.sel = (g.sel + 2) % 3;
    if (a & K_DOWN) g.sel = (g.sel + 1) % 3;
    if (ok()) {
      if (g.sel == 0) {
        if (!g.prof.guide) {
          g.prof.guide = 1;
          save_write();
          go(SC_HELP);
          g.back_to = SC_BATTLE;
        } else {
          start_battle();
        }
      } else if (g.sel == 1) {
        go(SC_DECK);
        g.drow = 0, g.dcol = 0, g.dscroll = 0, g.hold = -1;
      } else {
        go(SC_HELP);
        g.back_to = SC_TITLE;
      }
    }
    break;
  case SC_DECK:
    deck_keys();
    break;
  case SC_HELP:
    if (ok() || (p & K_BACK)) {
      if (g.back_to == SC_BATTLE) start_battle();
      else if (g.back_to == SC_PAUSE) go(SC_PAUSE);
      else go(SC_TITLE), g.sel = 2;
    }
    break;
  case SC_BATTLE:
    battle_keys();
    if (g.screen != SC_BATTLE) break;
    if (++g.steps % 3 == 0) {
      int32_t before = bt.time_ms;
      bool ot = bt.overtime;
      battle_step(&bt);
      if (!ot && bt.overtime) say("Sudden death!");
      else if (!bt.overtime && before > 60000 && bt.time_ms <= 60000) say("60 seconds left");
      if (bt.over) finish_battle();
    }
    break;
  case SC_PAUSE:
    if (g.confirm) {
      if (ok()) {
        /* leaving counts as a loss, as in the game */
        bt.over = 1, bt.winner = 1;
        finish_battle();
      } else if (p & K_BACK) {
        g.confirm = false;
      }
      break;
    }
    if (a & K_UP) g.sel = (g.sel + 2) % 3;
    if (a & K_DOWN) g.sel = (g.sel + 1) % 3;
    if (p & K_BACK) go(SC_BATTLE);
    else if (ok()) {
      if (g.sel == 0) go(SC_BATTLE);
      else if (g.sel == 1) go(SC_HELP), g.back_to = SC_PAUSE;
      else g.confirm = true;
    }
    break;
  case SC_RESULT:
    if (g.ms > 600 && (ok() || (p & K_BACK))) go(SC_TITLE);
    break;
  }
}

/* ------------------------------------------------------------------ drawing */
static void button(int x, int y, int w, int h, const char *s, bool sel) {
  gfx_round_rect(x, y, w, h, 4, sel ? RGB(0xA06A10) : RGB(0x0E1626));
  gfx_round_rect(x + 1, y + 1, w - 2, h - 3, 3, sel ? GOLD : RGB(0x34507E));
  gfx_text_oc(FONT_MID, x + w / 2, y + (h - gfx_line_h(FONT_MID)) / 2, s, WHITE);
}

static void draw_title(void) {
  gfx_fill(0, 0, SCREEN_W, SCREEN_H, RGB(0x0A1830));
  gfx_img(IMG_TITLE, 0, 0, 0, false);
  int x = 186, w = 128;
  char t[40];
  int a = arena_of(g.prof.trophies);
  gfx_text_oc(FONT_SMALL, x + w / 2, 12, arena_name(a), RGB(0xC8D8F0));
  snprintf(t, sizeof t, "%ld trophies", (long)g.prof.trophies);
  gfx_text_oc(FONT_MID, x + w / 2, 26, t, GOLD);
  static const char *const items[3] = {"Battle", "Deck", "How to play"};
  for (int i = 0; i < 3; i++) button(x, 62 + i * 40, w, 32, items[i], g.sel == i);
  snprintf(t, sizeof t, "Won %ld   Lost %ld", (long)g.prof.wins, (long)g.prof.losses);
  gfx_text_c(FONT_SMALL, x + w / 2, 192, t, RGB(0xA8B8D0));
  gfx_text_c(FONT_SMALL, x + w / 2, 222, "OK choose  Home quit", RGB(0x8090B0));
}

static void card_icon(int card, int x, int y, bool sel, bool dim) {
  if (sel) gfx_round_rect(x - 3, y - 3, 42, 51, 4, GOLD);
  else gfx_round_rect(x - 1, y - 1, 38, 47, 3, RGB(0x0E1626));
  gfx_img_dim(CARDS[card].icon, x + 18, y + 22, 0, false, dim ? 20 : 0);
  gfx_img(IMG_DROP_BIG, x + 6, y + 38, 0, false);
  char t[4];
  snprintf(t, sizeof t, "%ld", (long)CARDS[card].cost);
  gfx_text_oc(FONT_NUM, x + 6, y + 34, t, WHITE);
}

static void card_stats(int card, int x, int y) {
  const CardDef *c = &CARDS[card];
  static const char *const rar[4] = {"Common", "Rare", "Epic", "Legendary"};
  char t[96];
  int lvl = card_level(card), idx = card_lvl_index(card);
  snprintf(t, sizeof t, "%s  Level %d  %ld elixir", rar[c->rarity], lvl, (long)c->cost);
  gfx_text(FONT_SMALL, x, y, t, RGB(0xC8D8F0));
  t[0] = 0;
  if (c->unit != NONE) {
    const UnitDef *u = &UNITS[c->unit];
    int32_t d = u->proj != NONE ? PROJS[u->proj].dmg : u->dmg;
    if (u->flags & U_BOMB) d = u->death_dmg;
    snprintf(t, sizeof t, "HP %ld  Damage %ld  Hit %ld.%lds%s", (long)lv(u->hp, idx), (long)lv(d, idx), (long)(u->hit_ms / 1000),
             (long)(u->hit_ms % 1000 / 100), c->count > 1 ? "" : "");
    if (c->count > 1) snprintf(t + strlen(t), sizeof t - strlen(t), "  x%ld", (long)c->count);
    if (!u->dmg && u->proj == NONE) snprintf(t, sizeof t, "HP %ld  Lifetime %lds", (long)lv(u->hp, idx), (long)(u->life_ms / 1000));
  } else if (c->proj != NONE) {
    snprintf(t, sizeof t, "Damage %ld  Radius %ld.%ld", (long)lv(PROJS[c->proj].dmg, idx), (long)(c->radius / 1000),
             (long)(c->radius % 1000 / 100));
  } else if (c->area != NONE) {
    const AreaDef *ar = &AREAS[c->area];
    if (ar->dmg) snprintf(t, sizeof t, "Damage %ld  Radius %ld.%ld", (long)lv(ar->dmg, idx), (long)(c->radius / 1000), (long)(c->radius % 1000 / 100));
    else if (ar->proj != NONE) snprintf(t, sizeof t, "Damage %ld  3 targets", (long)lv(PROJS[ar->proj].dmg, idx));
    else snprintf(t, sizeof t, "Duration %ld.%lds  Radius %ld", (long)((ar->life_ms + ar->life_per_level * idx) / 1000),
                  (long)((ar->life_ms + ar->life_per_level * idx) % 1000 / 100), (long)(c->radius / 1000));
  }
  gfx_text(FONT_SMALL, x, y + 11, t, WHITE);
}

static void draw_deck(void) {
  gfx_fill(0, 0, SCREEN_W, SCREEN_H, RGB(0x14223E));
  gfx_text_o(FONT_SMALL, 6, 2, "Battle Deck", WHITE);
  int sum = 0;
  for (int k = 0; k < 8; k++) sum += CARDS[g.prof.deck[k]].cost;
  char t[48];
  snprintf(t, sizeof t, "Average Elixir cost: %d.%d", sum / 8, sum * 10 / 8 % 10);
  gfx_text_r(FONT_SMALL, 314, 2, t, PINK);
  for (int k = 0; k < 8; k++)
    if (!(g.drow == 0 && g.dcol == k)) card_icon(g.prof.deck[k], 6 + k * 39, 15, false, false);
  if (g.drow == 0) card_icon(g.prof.deck[g.dcol], 6 + g.dcol * 39, 15, true, false);
  if (g.hold >= 100) gfx_frame_rect(4 + (g.hold - 100) * 39, 13, 40, 49, WHITE);
  int card = deck_cursor_card();
  gfx_fill(0, 63, SCREEN_W, 30, RGB(0x0E1830));
  if (card >= 0) {
    gfx_text_o(FONT_MID, 6, 65, CARDS[card].name, WHITE);
    card_stats(card, 120, 66);
  }
  for (int pass = 0; pass < 2; pass++)
    for (int r = 0; r < DROWS_SHOWN; r++) {
      int row = g.dscroll + r;
      for (int c = 0; c < DCOLS; c++) {
        int i = row * DCOLS + c;
        if (i >= NCARDS) break;
        bool sel = g.drow == row + 1 && g.dcol == c;
        if (sel != (pass == 1)) continue;   /* (the chosen one last, its frame on top) */
        card_icon(i, 6 + c * 39, 96 + r * 43, sel, deck_has(i) >= 0);
        if (g.hold == i) gfx_frame_rect(4 + c * 39, 94 + r * 43, 40, 49, WHITE);
      }
    }
  const char *hint = g.hold < 0 ? "OK pick a card   arrows move   Back done" : "OK put it here   Back cancel";
  gfx_fill(0, 228, SCREEN_W, 12, RGB(0x0E1830));
  gfx_text_c(FONT_SMALL, 160, 229, hint, RGB(0xC8D8F0));
}

static void draw_help(void) {
  gfx_fill(0, 0, SCREEN_W, SCREEN_H, RGB(0x14223E));
  gfx_text_oc(FONT_MID, 160, 4, "How to play", GOLD);
  static const char *const lines[] = {
      "Knock down the enemy's towers. A Princess tower is a crown, the King tower is three and wins at once.",
      "Elixir fills up by itself. Each card costs some. You hold four cards; one played, the Next one takes its place.",
      ("1 to 4 (or left, right and OK) picks a card. Arrows move it on the field, OK drops it. Troops and buildings go on "
       "your side, spells anywhere."),
      "3 minutes. In the last minute elixir comes twice as fast. Even crowns at the end: overtime, the first crown wins.",
      "Back pauses. Home keeps the battle and quits."};
  int y = 24;
  for (unsigned i = 0; i < sizeof lines / sizeof lines[0]; i++) y += gfx_wrap(FONT_SMALL, 10, y, 300, lines[i], WHITE) + 6;
  gfx_text_c(FONT_SMALL, 160, 226, "OK continue", RGB(0xC8D8F0));
}

static void draw_pause(void) {
  if (!gfx_rows(40, 140)) return;
  gfx_blend(0, 0, SCREEN_W, 180, BLACK, 14);
  gfx_round_rect(90, 40, 140, 136, 6, PANEL);
  gfx_text_oc(FONT_MID, 160, 46, g.confirm ? "Quit battle?" : "Paused", WHITE);
  if (g.confirm) {
    gfx_wrap(FONT_SMALL, 100, 72, 120, "It counts as a loss. OK quit, Back stay.", WHITE);
    return;
  }
  static const char *const items[3] = {"Resume", "How to play", "Quit battle"};
  for (int i = 0; i < 3; i++) button(100, 70 + i * 34, 120, 28, items[i], g.sel == i);
}

static void draw_result(void) {
  if (!gfx_rows(30, 150)) return;
  gfx_blend(0, 0, SCREEN_W, 180, BLACK, 16);
  const char *t = bt.winner == 0 ? "You win!" : bt.winner == 1 ? "You lose" : "Draw";
  gfx_text_oc(FONT_BIG, 160, 34, t, bt.winner == 0 ? GOLD : bt.winner == 1 ? RED : WHITE);
  for (int i = 0; i < 3; i++) {
    int x = 130 + i * 30;
    if (i < bt.side[0].crowns) gfx_img(IMG_CROWN, x, 92, 0, false);
    else gfx_img_dim(IMG_CROWN, x, 92, 0, false, 24);
  }
  char s[48];
  snprintf(s, sizeof s, "%s%d trophies", g.dtroph > 0 ? "+" : "", g.dtroph);
  gfx_text_oc(FONT_MID, 160, 104, s, g.dtroph > 0 ? GOLD : WHITE);
  int a = arena_of(g.prof.trophies);
  if (a != g.old_arena) {
    snprintf(s, sizeof s, "%s %s", a > g.old_arena ? "Up to" : "Down to", arena_name(a));
    gfx_text_oc(FONT_SMALL, 160, 126, s, RGB(0xC8D8F0));
  }
  if (g.ms > 600) gfx_text_oc(FONT_SMALL, 160, 150, "OK continue", WHITE);
}

static void draw(void) {
  switch (g.screen) {
  case SC_TITLE: draw_title(); break;
  case SC_DECK: draw_deck(); break;
  case SC_HELP: draw_help(); break;
  default:
    view_battle();
    view_hud();
    if (g.screen == SC_PAUSE) draw_pause();
    if (g.screen == SC_RESULT) draw_result();
  }
}

void game_draw(void) {
  if (g.screen == SC_BATTLE || g.screen == SC_PAUSE || g.screen == SC_RESULT) view_prepare();
  gfx_frame(draw);
}
