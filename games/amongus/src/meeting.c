/* Among Us: the meeting (MeetingHud): discussion, then voting with a skip, the votes shown, then the ejection.
 * The bots talk in short lines, as the game's quick chat does. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "gfx.h"
#include "world.h"

void hud_keys(const char *s);
void art_player(int color, float x, float y, float scale, bool flip, int alpha);
void art_meeting_bg(void);
void art_plate(int x, int y, int i, bool sel, bool voted, bool dead);
void art_vote_icon(int x, int y, int color, bool anon);
void art_skip(int x, int y, bool sel);
void art_confirm(int x, int y, bool yes);

enum {
  L_BODY_IN, L_NEAR_BODY, L_SAW_KILL, L_SAW_VENT, L_WAS_IN, L_WITH, L_SUS, L_WHO, L_WHERE, L_SKIP, L_NOT_ME,
  L_SCANNED, L_BUTTON
};

const char *line_text(const Line *l, char *b, int n) {
  const char *a = l->a < MAXP ? COLOR_NAMES[g.p[l->a].color] : "";
  const char *r = l->b < N_ROOMS ? ROOM_NAMES[l->b] : "";
  switch (l->kind) {
  case L_BODY_IN: snprintf(b, (size_t)n, "%s is dead. Body in %s", a, r); break;
  case L_NEAR_BODY: snprintf(b, (size_t)n, "%s was near the body in %s", a, r); break;
  case L_SAW_KILL: snprintf(b, (size_t)n, "I saw %s kill in %s!", a, r); break;
  case L_SAW_VENT: snprintf(b, (size_t)n, "%s vented in %s!", a, r); break;
  case L_WAS_IN:
    if (l->a == R_HALLWAY) snprintf(b, (size_t)n, "I was doing tasks");
    else snprintf(b, (size_t)n, "I was in %s doing tasks", l->a < N_ROOMS ? ROOM_NAMES[l->a] : "");
    break;
  case L_WITH:
    if (l->b == R_HALLWAY) snprintf(b, (size_t)n, "I was with %s", a);
    else snprintf(b, (size_t)n, "I was with %s in %s", a, r);
    break;
  case L_SUS: snprintf(b, (size_t)n, "%s is sus", a); break;
  case L_WHO: snprintf(b, (size_t)n, "Who?"); break;
  case L_WHERE: snprintf(b, (size_t)n, "Where?"); break;
  case L_SKIP: snprintf(b, (size_t)n, "Skip"); break;
  case L_NOT_ME: snprintf(b, (size_t)n, "Not me"); break;
  case L_SCANNED: snprintf(b, (size_t)n, "%s scanned, they're safe", a); break;
  case L_BUTTON: snprintf(b, (size_t)n, "I called it. Anyone sus?"); break;
  default: b[0] = 0;
  }
  return b;
}

static void finish_votes(void) {
  int counts[MAXP + 1] = {0};
  for (int i = 0; i < g.nplayers; i++)
    if (g.p[i].alive && g.votes[i] != NONE) counts[g.votes[i]]++;
  int best = -1, bc = 0, tie = 0;
  for (int k = 0; k <= SKIP; k++) {
    if (counts[k] > bc) bc = counts[k], best = k, tie = 0;
    else if (counts[k] == bc && bc) tie = 1;
  }
  g.tie = (uint8_t)tie;
  g.ejected = (best >= 0 && best != SKIP && !tie) ? (uint8_t)best : NONE;
}

static int living_voters(void) {
  int n = 0;
  for (int i = 0; i < g.nplayers; i++) n += g.p[i].alive;
  return n;
}

static void after_meeting(void) {
  /* back in the cafeteria, as after every meeting */
  if (g.ejected != NONE) g.p[g.ejected].alive = 0;
  for (int b = 0; b < MAXP; b++) g.bodies[b].on = 0;
  int n = g.nplayers;
  for (int i = 0; i < n; i++) {
    Player *P = &g.p[i];
    spawn_at_table(i);
    P->kill_cd = kill_cooldown();
    P->busy = 0;
    Bot *B = &P->bot;
    B->state = 0;
    B->plen = 0;
    B->near_body = B->found_body = NONE;
    if (B->saw_kill != NONE && !g.p[B->saw_kill].alive) B->saw_kill = NONE;
    if (B->saw_vent != NONE && !g.p[B->saw_vent].alive) B->saw_vent = NONE;
    B->t = 0.5f + rnd_f();
  }
  g.cam = (V2){g.p[g.me].pos.x, g.p[g.me].pos.y + FEET_Y};
  g.emergency_cd = g.opt.meet_cd;
  g.task_bar = tasks_progress();
  g.overlay = O_NONE;
}

/* the plates: two columns, then Skip; the arrows move between them */
#define PLATE_X(i) (18 + ((i) & 1) * 134)
#define PLATE_Y(i) (41 + ((i) >> 1) * 27)
#define SKIP_X 18
#define SKIP_Y 177

static int move_sel(int s, uint32_t pressed) {
  int n = g.nplayers, rows = (n + 1) / 2;
  if (s == SKIP) {
    if (pressed & K_UP) s = (rows - 1) * 2 < n ? (rows - 1) * 2 : n - 1;
    return s;
  }
  int col = s & 1, row = s >> 1;
  if (pressed & K_LEFT) col = 0;
  if (pressed & K_RIGHT) col = 1;
  if (pressed & K_UP) row = row > 0 ? row - 1 : 0;
  if (pressed & K_DOWN) {
    if (row + 1 >= rows || row * 2 + 2 + col >= n + (col ? 0 : 1)) return SKIP;
    row++;
  }
  int t = row * 2 + col;
  if (t >= n) t = n - 1;
  return t;
}

void meeting_tick(uint32_t keys, uint32_t pressed) {
  (void)keys;
  g.meet_t += DT;
  bot_meeting_tick();
  Player *M = &g.p[g.me];
  if (g.meet_stage == 0 && g.meet_t >= g.opt.discuss) g.meet_stage = 1, g.meet_t = 0;
  if (pressed & K_TOOL) {
    g.line_shown = g.line_shown ? 0 : 1;
    return;
  }
  if (g.line_shown && (pressed & K_BACK)) {
    g.line_shown = 0;
    return;
  }
  if (g.meet_stage == 1) {
    bot_votes();
    /* the player's vote: a plate or Skip, then the tick */
    if (M->alive && g.votes[g.me] == NONE && !g.line_shown) {
      int s = g.sel & 15;
      bool confirm = g.sel & 16;
      if (s > SKIP) s = 0;
      if (!confirm) {
        s = move_sel(s, pressed);
        bool ok = s == SKIP || (s < g.nplayers && g.p[s].alive);
        if ((pressed & (K_OK | K_EXE)) && ok) confirm = true;
      } else {
        if (pressed & (K_OK | K_EXE)) g.votes[g.me] = (uint8_t)s, confirm = false;
        if (pressed & (K_BACK | K_LEFT | K_RIGHT | K_UP | K_DOWN)) confirm = false;
      }
      g.sel = (uint8_t)(s | (confirm ? 16 : 0));
    }
    int voted = 0;
    for (int i = 0; i < g.nplayers; i++) voted += g.p[i].alive && g.votes[i] != NONE;
    if (voted >= living_voters() || g.meet_t >= g.opt.vote * 5) {
      finish_votes();
      g.meet_stage = 2;
      g.meet_t = 0;
      g.line_shown = 0;
    }
  } else if (g.meet_stage == 2 && g.meet_t >= 5) {
    after_meeting();
    g.screen = S_EJECT;
    g.screen_t = 0;
  }
}

static const char *stage_text(char *b, int n) {
  if (g.meet_stage == 0) snprintf(b, (size_t)n, "Voting Begins In: %ds", (int)ceilf(g.opt.discuss - g.meet_t));
  else if (g.meet_stage == 1) snprintf(b, (size_t)n, "Voting Ends In: %ds", (int)ceilf(g.opt.vote * 5 - g.meet_t));
  else snprintf(b, (size_t)n, "Proceeding In: %ds", (int)ceilf(5 - g.meet_t));
  return b;
}

/* a chat line: the speaker's head, name and words */
static void chat_line(const Line *l, int x, int y, uint16_t text) {
  char b[64];
  const Player *P = &g.p[l->who];
  gfx_spr(pic(PIC_VOTE_ICON), x + 5, y - 4, false, color_pal[P->color], 256);
  int w = gfx_text(FONT_SMALL, x + 13, y, COLOR_NAMES[P->color], 0xFFFF, 0x0000, 1, 0);
  gfx_text(FONT_SMALL, x + 17 + w, y, line_text(l, b, sizeof b), text, 0x0000, 1, 0);
}

void meeting_draw(void) {
  art_meeting_bg();
  char b[64];
  int sel = g.sel & 15;
  bool confirm = g.sel & 16;
  const Player *M = &g.p[g.me];
  bool can_vote = g.meet_stage == 1 && M->alive && g.votes[g.me] == NONE && !g.line_shown;
  for (int i = 0; i < g.nplayers; i++) {
    int x = PLATE_X(i), y = PLATE_Y(i);
    if (!gfx_rows(y - 2, y + 26)) continue;
    art_plate(x, y, i, can_vote && sel == i, g.votes[i] != NONE && g.meet_stage >= 1 && g.p[i].alive, !g.p[i].alive);
    if (g.meet_stage == 2) {
      /* who voted for this one */
      int k = 0;
      for (int v = 0; v < g.nplayers; v++)
        if (g.p[v].alive && g.votes[v] == i) {
          art_vote_icon(x + 82 + 10 * (k % 5), y + 6 + 10 * (k / 5), g.p[v].color, g.opt.anon);
          k++;
        }
    }
    if (can_vote && sel == i && confirm) art_confirm(x + 84, y + 2, true);
  }
  /* Skip, the clock, the last thing said */
  art_skip(SKIP_X, SKIP_Y, can_vote && sel == SKIP);
  if (can_vote && sel == SKIP && confirm) art_confirm(SKIP_X + 72, SKIP_Y - 2, true);
  if (g.meet_stage == 2) {
    gfx_spr(pic(PIC_UI_SKIPPEDVOTINGTALLY), SKIP_X + 20, SKIP_Y + 8, false, NULL, 256);
    int k = 0;
    for (int v = 0; v < g.nplayers; v++)
      if (g.p[v].alive && g.votes[v] == SKIP) art_vote_icon(SKIP_X + 48 + 10 * k++, SKIP_Y + 8, g.p[v].color, g.opt.anon);
  }
  gfx_text(FONT_MID, 282, SKIP_Y + 12, stage_text(b, sizeof b), 0xFFFF, 0x0000, 1, 2);
  if (g.nlines && !g.line_shown) chat_line(&g.lines[g.nlines - 1], 18, 202, 0xFFFF);
  if (g.line_shown) {
    /* the chat: the last lines said */
    gfx_fill(15, 20, 270, 187, rgb565(0x0E1A2B), 236);
    gfx_text(FONT_BIG, 150, 36, "Chat", 0xFFFF, 0x0000, 1, 1);
    int first = g.nlines > 10 ? g.nlines - 10 : 0;
    for (int k = first; k < g.nlines; k++) chat_line(&g.lines[k], 20, 56 + (k - first) * 15, 0xDEFB);
    if (!g.nlines) gfx_text(FONT_MID, 150, 110, "Nobody has said anything yet", 0xBDF7, 0, 0, 1);
    hud_keys("toolbox or Back close chat");
    return;
  }
  if (can_vote)
    hud_keys(confirm ? "OK vote   Back cancel" : "arrows choose   OK vote   toolbox chat");
  else
    hud_keys(g.meet_stage == 0 ? "discussion   toolbox chat" : "toolbox chat");
}
