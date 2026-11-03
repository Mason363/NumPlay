/* Among Us: the screens around a round: the title, the lobby's settings, the intro, pause, How to Play, the
 * kill, the report, the ejection and the end. And game_tick / game_draw, which hand each screen its turn. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "gfx.h"
#include "plat.h"
#include "view.h"
#include "world.h"

bool game_running = true;
static uint32_t last_keys;
void play_tick(uint32_t keys, uint32_t pressed);
void hud_keys(const char *s);
void game_forget(void);
void view_prepare(void);
void art_title(void);
void art_kill(float t);
void art_report(float t);
void art_eject(float t);
void art_stars(float t, float speed);
void art_player(int color, float x, float y, float scale, bool flip, int alpha);
void art_lineup(const uint8_t *who, int n, float t);

#define WHITE 0xFFFF
#define CREW_BLUE 0x8EFF
#define IMP_RED 0xF800

/* ---- the lobby's settings (GameOptionsMenu) ---- */

enum {
  OPT_BOTS, OPT_ROLE, OPT_COLOR, OPT_IMPS, OPT_CONFIRM, OPT_MEETINGS, OPT_ANON, OPT_MEETCD, OPT_DISCUSS, OPT_VOTE,
  OPT_SPEED, OPT_CREWV, OPT_IMPV, OPT_KILLCD, OPT_KILLD, OPT_TASKBAR, OPT_VISUAL, OPT_COMMON, OPT_LONG, OPT_SHORT,
  N_OPT
};
static const char *const OPT_NAMES[N_OPT] = {
    "Bots",          "Your Role",       "Your Color",       "# Impostors",     "Confirm Ejects",
    "# Emergency Meetings", "Anonymous Votes", "Emergency Cooldown", "Discussion Time", "Voting Time",
    "Player Speed",  "Crewmate Vision", "Impostor Vision",  "Kill Cooldown",   "Kill Distance",
    "Task Bar Updates", "Visual Tasks", "# Common Tasks",  "# Long Tasks",    "# Short Tasks"};

static void opt_value(int k, char *b, int n) {
  const Options *o = &g.opt;
  switch (k) {
  case OPT_BOTS: snprintf(b, (size_t)n, "%d", o->bots); break;
  case OPT_ROLE: snprintf(b, (size_t)n, "%s", o->role == 0 ? "Random" : o->role == 1 ? "Crewmate" : "Impostor"); break;
  case OPT_COLOR: snprintf(b, (size_t)n, "%s", COLOR_NAMES[o->color]); break;
  case OPT_IMPS: snprintf(b, (size_t)n, "%d", o->impostors); break;
  case OPT_CONFIRM: snprintf(b, (size_t)n, "%s", o->confirm ? "On" : "Off"); break;
  case OPT_MEETINGS: snprintf(b, (size_t)n, "%d", o->meetings); break;
  case OPT_ANON: snprintf(b, (size_t)n, "%s", o->anon ? "On" : "Off"); break;
  case OPT_MEETCD: snprintf(b, (size_t)n, "%ds", o->meet_cd); break;
  case OPT_DISCUSS: snprintf(b, (size_t)n, "%ds", o->discuss); break;
  case OPT_VOTE: snprintf(b, (size_t)n, "%ds", o->vote * 5); break;
  case OPT_SPEED: snprintf(b, (size_t)n, "%d.%02dx", o->speed / 4, o->speed % 4 * 25); break;
  case OPT_CREWV: snprintf(b, (size_t)n, "%d.%02dx", o->crew_vision / 4, o->crew_vision % 4 * 25); break;
  case OPT_IMPV: snprintf(b, (size_t)n, "%d.%02dx", o->imp_vision / 4, o->imp_vision % 4 * 25); break;
  case OPT_KILLCD: snprintf(b, (size_t)n, o->kill_cd & 1 ? "%d.5s" : "%ds", o->kill_cd / 2); break;
  case OPT_KILLD: snprintf(b, (size_t)n, "%s", o->kill_dist == 0 ? "Short" : o->kill_dist == 1 ? "Medium" : "Long"); break;
  case OPT_TASKBAR:
    snprintf(b, (size_t)n, "%s", o->taskbar == 0 ? "Always" : o->taskbar == 1 ? "Meetings" : "Never");
    break;
  case OPT_VISUAL: snprintf(b, (size_t)n, "%s", o->visual ? "On" : "Off"); break;
  case OPT_COMMON: snprintf(b, (size_t)n, "%d", o->common); break;
  case OPT_LONG: snprintf(b, (size_t)n, "%d", o->longt); break;
  case OPT_SHORT: snprintf(b, (size_t)n, "%d", o->shortt); break;
  }
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void opt_change(int k, int d) {
  Options *o = &g.opt;
  switch (k) {
  case OPT_BOTS: o->bots = (uint8_t)clampi(o->bots + d, 3, 9); break;
  case OPT_ROLE: o->role = (uint8_t)((o->role + 3 + d) % 3); break;
  case OPT_COLOR: o->color = (uint8_t)((o->color + N_COLORS + d) % N_COLORS); break;
  case OPT_IMPS: o->impostors = (uint8_t)clampi(o->impostors + d, 1, 3); break;
  case OPT_CONFIRM: o->confirm ^= 1; break;
  case OPT_MEETINGS: o->meetings = (uint8_t)clampi(o->meetings + d, 0, 9); break;
  case OPT_ANON: o->anon ^= 1; break;
  case OPT_MEETCD: o->meet_cd = (uint8_t)clampi(o->meet_cd + 5 * d, 0, 60); break;
  case OPT_DISCUSS: o->discuss = (uint8_t)clampi(o->discuss + 15 * d, 0, 120); break;
  case OPT_VOTE: o->vote = (uint8_t)clampi(o->vote + 3 * d, 3, 60); break;
  case OPT_SPEED: o->speed = (uint8_t)clampi(o->speed + d, 2, 12); break;
  case OPT_CREWV: o->crew_vision = (uint8_t)clampi(o->crew_vision + d, 1, 20); break;
  case OPT_IMPV: o->imp_vision = (uint8_t)clampi(o->imp_vision + d, 1, 20); break;
  case OPT_KILLCD: o->kill_cd = (uint8_t)clampi(o->kill_cd + 5 * d, 20, 120); break;
  case OPT_KILLD: o->kill_dist = (uint8_t)clampi(o->kill_dist + d, 0, 2); break;
  case OPT_TASKBAR: o->taskbar = (uint8_t)((o->taskbar + 3 + d) % 3); break;
  case OPT_VISUAL: o->visual ^= 1; break;
  case OPT_COMMON: o->common = (uint8_t)clampi(o->common + d, 0, 2); break;
  case OPT_LONG: o->longt = (uint8_t)clampi(o->longt + d, 0, 3); break;
  case OPT_SHORT: o->shortt = (uint8_t)clampi(o->shortt + d, 0, 5); break;
  }
  if (o->common + o->longt + o->shortt == 0) o->shortt = 1;
}

static int lobby_top;

static void lobby_tick(uint32_t pressed) {
  int n = N_OPT + 1;   /* the settings, then Start */
  if (pressed & K_UP) g.sel = (uint8_t)((g.sel + n - 1) % n);
  if (pressed & K_DOWN) g.sel = (uint8_t)((g.sel + 1) % n);
  if (g.sel < N_OPT) {
    if (pressed & K_LEFT) opt_change(g.sel, -1);
    if (pressed & K_RIGHT) opt_change(g.sel, 1);
  }
  if ((pressed & (K_OK | K_EXE)) && g.sel == N_OPT) {
    settings_save();
    game_new();
    return;
  }
  if ((pressed & (K_OK | K_EXE)) && g.sel < N_OPT) opt_change(g.sel, 1);
  if (pressed & K_BACK) {
    settings_save();
    g.screen = S_TITLE;
    g.sel = 0;
  }
  if (g.sel < lobby_top) lobby_top = g.sel;
  if (g.sel >= lobby_top + 9) lobby_top = g.sel - 8;
}

static void lobby_draw(void) {
  art_stars(g.screen_t, 0.3f);
  gfx_fill(30, 8, 260, 216, rgb565(0x0E1A2B), 230);
  gfx_rect(30, 8, 260, 216, 2, rgb565(0x9AB3C9));
  gfx_text(FONT_BIG, 160, 28, "Game Settings", WHITE, 0, 0, 1);
  gfx_text(FONT_SMALL, 160, 41, "Map: The Skeld", rgb565(0x9AB3C9), 0, 0, 1);
  char b[24];
  for (int r = 0; r < 9; r++) {
    int k = lobby_top + r;
    int y = 48 + r * 17;
    if (k == N_OPT) {
      bool s = g.sel == N_OPT;
      gfx_fill(110, y + 1, 100, 15, s ? rgb565(0x44D844) : rgb565(0x23452A), 256);
      gfx_rect(110, y + 1, 100, 15, 1, WHITE);
      gfx_text(FONT_MID, 160, y + 13, "Start", WHITE, 0, 0, 1);
      break;
    }
    bool s = g.sel == k;
    if (s) gfx_fill(36, y, 248, 17, rgb565(0x2D4F73), 256);
    gfx_text(FONT_MID, 42, y + 13, OPT_NAMES[k], WHITE, 0, 0, 0);
    opt_value(k, b, sizeof b);
    gfx_fill(206, y + 2, 72, 13, 0x0000, 160);
    gfx_text(FONT_MID, 242, y + 13, b, s ? 0xFFE0 : WHITE, 0, 0, 1);
    if (s) {
      gfx_text(FONT_MID, 199, y + 13, "<", 0xFFE0, 0, 0, 1);
      gfx_text(FONT_MID, 284, y + 13, ">", 0xFFE0, 0, 0, 1);
    }
  }
  hud_keys(g.sel < N_OPT ? "up down choose   left right change   Back title" : "OK start   up down settings   Back title");
}

/* ---- the title (MainMenu) ---- */

static void title_tick(uint32_t pressed) {
  if (pressed & K_UP) g.sel = (uint8_t)((g.sel + 1) % 2);
  if (pressed & K_DOWN) g.sel = (uint8_t)((g.sel + 1) % 2);
  if (pressed & (K_OK | K_EXE)) {
    if (g.sel == 0) g.screen = S_LOBBY, g.sel = N_OPT, lobby_top = N_OPT - 8;
    else g.screen = S_HOWTO, g.back_screen = S_TITLE, g.sel = 0;
    g.screen_t = 0;
  }
  if (pressed & K_BACK) game_running = false;
}

static void menu_button(int y, const char *s, bool on) {
  gfx_fill(100, y, 120, 22, on ? WHITE : 0x0000, on ? 256 : 150);
  gfx_rect(100, y, 120, 22, 2, WHITE);
  gfx_text(FONT_BIG, 160, y + 16, s, on ? 0x0000 : WHITE, 0, 0, 1);
}

static void title_draw(void) {
  art_title();
  menu_button(150, "Play", g.sel == 0);
  menu_button(178, "How to Play", g.sel == 1);
  hud_keys("up down choose   OK go   Back quit");
}

/* ---- How to Play ---- */

static const char *const HOWTO[][6] = {
    {"Crewmates", "Do your tasks around the ship.", "Find the Impostor and vote them out.",
     "The task bar fills as tasks get done.", "Report a body or press the button to meet.", NULL},
    {"Impostors", "Kill the crewmates, one at a time.", "Sabotage the ship and use the vents.",
     "Fake tasks and don't get caught.", "Win when the crew is as few as you.", NULL},
    {"Keys", "Arrows: walk", "OK: use, report, vent", "EXE: kill", "Toolbox: map or sabotage", "Back: pause   Home: save and quit"},
};

static void howto_tick(uint32_t pressed) {
  if (pressed & (K_RIGHT | K_OK | K_EXE)) {
    if (g.sel < 2) g.sel++;
    else g.screen = g.back_screen, g.sel = 0;
  }
  if ((pressed & K_LEFT) && g.sel) g.sel--;
  if (pressed & K_BACK) g.screen = g.back_screen, g.sel = 0;
}

static void howto_draw(void) {
  art_stars(g.t + g.screen_t, 0.3f);
  gfx_fill(20, 20, 280, 190, rgb565(0x0E1A2B), 230);
  gfx_rect(20, 20, 280, 190, 2, rgb565(0x9AB3C9));
  const char *const *p = HOWTO[g.sel];
  gfx_text(FONT_HUGE, 160, 56, p[0], g.sel == 1 ? IMP_RED : CREW_BLUE, 0, 0, 1);
  for (int k = 1; k < 6 && p[k]; k++) gfx_text(FONT_MID, 160, 74 + 22 * k, p[k], WHITE, 0, 0, 1);
  char b[16];
  snprintf(b, sizeof b, "%d/3", g.sel + 1);
  gfx_text(FONT_SMALL, 160, 202, b, rgb565(0x9AB3C9), 0, 0, 1);
  hud_keys("OK next   left back a page   Back close");
}

/* ---- pause ---- */

static void pause_tick(uint32_t pressed) {
  if (g.sel >= 10) {
    /* "Quit the game?" */
    if (pressed & (K_LEFT | K_RIGHT | K_UP | K_DOWN)) g.sel ^= 1;
    if (pressed & (K_OK | K_EXE)) {
      if (g.sel == 10) {
        game_forget();
        g.screen = S_TITLE, g.sel = 0;
      } else
        g.sel = 2;
    }
    if (pressed & K_BACK) g.sel = 2;
    return;
  }
  if (pressed & K_UP) g.sel = (uint8_t)((g.sel + 2) % 3);
  if (pressed & K_DOWN) g.sel = (uint8_t)((g.sel + 1) % 3);
  if (pressed & (K_OK | K_EXE)) {
    if (g.sel == 0) g.screen = g.back_screen;
    else if (g.sel == 1) g.screen = S_HOWTO, g.sel = 0;
    else g.sel = 11;
  }
  if (pressed & K_BACK) g.screen = g.back_screen;
}

static void pause_draw(void) {
  /* the round behind it, darker */
  if (g.back_screen == S_PLAY) {
    view_draw();
    gfx_fill(0, 0, SCREEN_W, SCREEN_H, 0x0000, 150);
  } else art_stars(g.t, 0.3f);
  if (g.sel >= 10) {
    gfx_fill(60, 80, 200, 80, rgb565(0x0E1A2B), 240);
    gfx_rect(60, 80, 200, 80, 2, WHITE);
    gfx_text(FONT_BIG, 160, 108, "Quit the game?", WHITE, 0, 0, 1);
    for (int k = 0; k < 2; k++) {
      bool on = g.sel == 10 + k;
      gfx_fill(80 + k * 90, 122, 70, 22, on ? WHITE : 0x0000, on ? 256 : 150);
      gfx_rect(80 + k * 90, 122, 70, 22, 2, WHITE);
      gfx_text(FONT_BIG, 115 + k * 90, 138, k ? "No" : "Yes", on ? 0x0000 : WHITE, 0, 0, 1);
    }
    hud_keys("left right choose   OK confirm   Back no");
    return;
  }
  gfx_text(FONT_HUGE, 160, 70, "Paused", WHITE, 0x0000, 1, 1);
  menu_button(92, "Resume", g.sel == 0);
  menu_button(120, "How to Play", g.sel == 1);
  menu_button(148, "Quit game", g.sel == 2);
  hud_keys("up down choose   OK go   Back resume   Home save and quit");
}

/* ---- the round's own screens ---- */

static void intro_draw(void) {
  float t = g.screen_t;
  art_stars(0, 0);
  if (t < 1.6f) {
    /* "Shhhhhh!" */
    gfx_text(FONT_HUGE, 160, 130, "Shhhhhh!", IMP_RED, 0x0000, 2, 1);
    return;
  }
  const Player *M = &g.p[g.me];
  uint8_t who[MAXP];
  int n = 0;
  who[n++] = (uint8_t)g.me;
  for (int i = 0; i < g.nplayers; i++)
    if (i != g.me && (!M->imp || g.p[i].imp)) who[n++] = (uint8_t)i;
  art_lineup(who, n, t - 1.6f);
  if (M->imp) {
    gfx_text(FONT_HUGE, 160, 52, "Impostor", IMP_RED, 0x0000, 2, 1);
  } else {
    gfx_text(FONT_HUGE, 160, 52, "Crewmate", CREW_BLUE, 0x0000, 2, 1);
    int k = 0;
    for (int i = 0; i < g.nplayers; i++) k += g.p[i].imp;
    char b[48];
    snprintf(b, sizeof b, "There %s %d Impostor%s among us", k == 1 ? "is" : "are", k, k == 1 ? "" : "s");
    gfx_text(FONT_MID, 160, 74, b, WHITE, 0x0000, 1, 1);
  }
}

static void end_draw(void) {
  art_stars(0, 0);
  const Player *M = &g.p[g.me];
  bool won = (g.winner == 1) == (M->imp != 0);
  uint8_t who[MAXP];
  int n = 0;
  for (int i = 0; i < g.nplayers; i++)
    if ((g.p[i].imp != 0) == (g.winner == 1)) who[n++] = (uint8_t)i;
  art_lineup(who, n, g.screen_t);
  gfx_text(FONT_HUGE, 160, 52, won ? "Victory" : "Defeat", won ? CREW_BLUE : IMP_RED, 0x0000, 2, 1);
  static const char *const WHY[2][4] = {{"Crewmates voted out the Impostors", "", "Crewmates finished their tasks", ""},
                                         {"", "Impostors killed the crew", "", "Impostors sabotaged the ship"}};
  const char *why = WHY[g.winner & 1][g.end_reason & 3];
  if (*why) gfx_text(FONT_MID, 160, 74, why, WHITE, 0x0000, 1, 1);
  if (g.screen_t > 2) hud_keys("OK play again   Back title");
}

/* ---- the turn and the drawing ---- */

void game_tick(uint32_t keys) {
  uint32_t pressed = keys & ~last_keys;
  last_keys = keys;
  if (pressed & K_HOME) {
    /* save and quit, from anywhere */
    if (g.screen == S_TITLE || g.screen == S_LOBBY || g.screen == S_END || g.screen == S_HOWTO) settings_save();
    else game_save();
    game_running = false;
    return;
  }
  g.screen_t += DT;
  switch (g.screen) {
  case S_TITLE: title_tick(pressed); break;
  case S_LOBBY: lobby_tick(pressed); break;
  case S_HOWTO: howto_tick(pressed); break;
  case S_PAUSE: pause_tick(pressed); break;
  case S_INTRO:
    if (g.screen_t > 6.5f || (g.screen_t > 2 && (pressed & (K_OK | K_EXE)))) g.screen = S_PLAY, g.screen_t = 0;
    break;
  case S_PLAY: {
    int was = g.p[g.me].room;
    play_tick(keys, pressed);
    /* the room's name when entering one */
    if (g.p[g.me].room != was || g.room_t <= -1) g.room_shown = g.p[g.me].room, g.room_t = 2.5f;
    if (g.room_t > 0) g.room_t -= DT;
    /* the camera (FollowerCamera: a lerp at 5 a second) */
    V2 to = {g.p[g.me].pos.x, g.p[g.me].pos.y + FEET_Y};
    g.cam.x += (to.x - g.cam.x) * (5 * DT), g.cam.y += (to.y - g.cam.y) * (5 * DT);
    /* now and then: saved */
    if ((int)(g.t * 30) % (30 * 60) == 0 && g.t > 1) game_save();
    break;
  }
  case S_KILLED:
    /* the round goes on meanwhile */
    play_tick(0, 0);
    if (g.screen == S_KILLED && g.screen_t > 2.6f) g.screen = S_PLAY, g.screen_t = 0;
    break;
  case S_REPORT:
    if (g.screen_t > 2.5f) {
      g.screen = S_MEETING;
      g.screen_t = 0;
      g.meet_stage = 0;
      g.meet_t = 0;
      memset(g.votes, NONE, sizeof g.votes);
      g.sel = 0;
      bot_meeting_start();
    }
    break;
  case S_MEETING: meeting_tick(keys, pressed); break;
  case S_EJECT:
    if (g.screen_t > 7.5f) {
      g.screen = S_PLAY;
      g.screen_t = 0;
      check_win();
    }
    break;
  case S_END:
    if (g.screen_t > 2 && (pressed & (K_OK | K_EXE))) g.screen = S_LOBBY, g.sel = N_OPT, lobby_top = N_OPT - 8, game_forget();
    else if (g.screen_t > 2 && (pressed & K_BACK)) g.screen = S_TITLE, g.sel = 0, game_forget();
    break;
  }
}

static void draw_band(void) {
  switch (g.screen) {
  case S_TITLE: title_draw(); break;
  case S_LOBBY: lobby_draw(); break;
  case S_HOWTO: howto_draw(); break;
  case S_PAUSE: pause_draw(); break;
  case S_INTRO: intro_draw(); break;
  case S_PLAY:
    view_draw();
    if (g.overlay == O_NONE) hud_draw();
    else if (g.overlay == O_TASK || g.overlay == O_EMERGENCY || g.overlay == O_CAMS) mg_draw();
    else map_draw(g.overlay);
    break;
  case S_KILLED:
    view_draw();
    art_kill(g.screen_t);
    break;
  case S_REPORT:
    view_draw();
    art_report(g.screen_t);
    break;
  case S_MEETING: meeting_draw(); break;
  case S_EJECT: art_eject(g.screen_t); break;
  case S_END: end_draw(); break;
  }
}

void game_draw(void) {
  if (g.screen == S_PLAY || g.screen == S_KILLED || g.screen == S_REPORT || (g.screen == S_PAUSE && g.back_screen == S_PLAY))
    view_prepare();
  gfx_frame(draw_band);
}
