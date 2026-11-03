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
  case L_WAS_IN: snprintf(b, (size_t)n, "I was in %s doing tasks", l->a < N_ROOMS ? ROOM_NAMES[l->a] : ""); break;
  case L_WITH: snprintf(b, (size_t)n, "I was with %s in %s", a, r); break;
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
    float a = 6.2831853f * i / n - 1.5707963f;
    P->pos = (V2){-0.72f + cosf(a) * 1.6f, 0.62f + sinf(a) * 1.6f - FEET_Y};
    P->vel = (V2){0, 0};
    P->vent = NONE;
    P->vent_anim = 0;
    P->walking = 0;
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

void meeting_tick(uint32_t keys, uint32_t pressed) {
  (void)keys;
  g.meet_t += DT;
  bot_meeting_tick();
  Player *M = &g.p[g.me];
  if (g.meet_stage == 0 && g.meet_t >= g.opt.discuss) g.meet_stage = 1, g.meet_t = 0;
  if (g.meet_stage == 1) {
    bot_votes();
    /* the player's vote: the plates (two columns), then Skip */
    if (M->alive && g.votes[g.me] == NONE) {
      int s = g.sel & 15, n = g.nplayers;
      bool confirm = g.sel & 16;
      if (!confirm) {
        if (pressed & K_DOWN) s = s == SKIP ? s : (s + 2 < n ? s + 2 : SKIP);
        if (pressed & K_UP) s = s == SKIP ? n - 1 - ((n - 1) & 1 ? 0 : 0) : (s >= 2 ? s - 2 : s);
        if (pressed & K_LEFT) s = s == SKIP ? s : s & ~1;
        if (pressed & K_RIGHT) s = s == SKIP ? s : (s | 1) < n ? s | 1 : s;
        if (s > SKIP) s = SKIP;
        bool ok = s == SKIP || (s < n && g.p[s].alive);
        if ((pressed & (K_OK | K_EXE)) && ok) confirm = true;
      } else {
        if (pressed & (K_OK | K_EXE)) g.votes[g.me] = (uint8_t)s, confirm = false;
        if (pressed & K_BACK) confirm = false;
      }
      g.sel = (uint8_t)(s | (confirm ? 16 : 0));
    }
    int voted = 0;
    for (int i = 0; i < g.nplayers; i++) voted += g.p[i].alive && g.votes[i] != NONE;
    if (voted >= living_voters() || g.meet_t >= g.opt.vote * 5) {
      finish_votes();
      g.meet_stage = 2;
      g.meet_t = 0;
    }
  } else if (g.meet_stage == 2 && g.meet_t >= 5) {
    after_meeting();
    g.screen = S_EJECT;
    g.screen_t = 0;
  }
  if ((pressed & K_BACK) && !(g.sel & 16) && g.meet_stage != 1) {
    /* (nothing to close: the meeting can't be left) */
  }
  if (pressed & K_TOOL) g.line_shown = g.line_shown ? 0 : 1;
}

static const char *stage_text(char *b, int n) {
  if (g.meet_stage == 0) snprintf(b, (size_t)n, "Voting Begins In: %ds", (int)ceilf(g.opt.discuss - g.meet_t));
  else if (g.meet_stage == 1) snprintf(b, (size_t)n, "Voting Ends In: %ds", (int)ceilf(g.opt.vote * 5 - g.meet_t));
  else snprintf(b, (size_t)n, "Proceeding In: %ds", (int)ceilf(5 - g.meet_t));
  return b;
}

void meeting_draw(void) {
  art_meeting_bg();
  char b[64];
  /* the plates: two columns of five */
  int sel = g.sel & 15;
  bool confirm = g.sel & 16;
  const Player *M = &g.p[g.me];
  bool can_vote = g.meet_stage == 1 && M->alive && g.votes[g.me] == NONE;
  for (int i = 0; i < g.nplayers; i++) {
    int x = 12 + (i & 1) * 150, y = 26 + (i >> 1) * 29;
    art_plate(x, y, i, can_vote && sel == i, g.votes[i] != NONE && g.meet_stage == 1, !g.p[i].alive);
    if (i == g.caller && g.meet_stage < 2) {
      /* the megaphone of who called it */
      gfx_text(FONT_SMALL, x + 138, y + 11, g.reported == NONE ? "!" : "!", 0xFFE0, 0, 1, 2);
    }
    if (g.meet_stage == 2) {
      /* who voted for this one */
      int k = 0;
      for (int v = 0; v < g.nplayers; v++)
        if (g.p[v].alive && g.votes[v] == i) art_vote_icon(x + 40 + 11 * k++, y + 19, g.p[v].color, g.opt.anon);
    }
    if (can_vote && sel == i && confirm) {
      gfx_fill(x + 90, y + 3, 44, 20, 0x0000, 200);
      gfx_text(FONT_MID, x + 112, y + 17, "Vote?", 0xFFE0, 0, 0, 1);
    }
  }
  /* skip, and the clock */
  int ys = 26 + 5 * 29;
  art_skip(12, ys, can_vote && sel == SKIP);
  if (can_vote && sel == SKIP && confirm) gfx_text(FONT_MID, 112, ys + 14, "Skip?", 0xFFE0, 0x0000, 1, 0);
  if (g.meet_stage == 2) {
    int k = 0;
    for (int v = 0; v < g.nplayers; v++)
      if (g.p[v].alive && g.votes[v] == SKIP) art_vote_icon(80 + 11 * k++, ys + 9, g.p[v].color, g.opt.anon);
  }
  gfx_text(FONT_MID, 308, ys + 13, stage_text(b, sizeof b), 0xFFFF, 0x0000, 1, 2);
  /* what the others said: the last two lines (Toolbox: more) */
  int shown = g.line_shown ? 6 : 2;
  int y0 = g.line_shown ? 60 : 196;
  if (g.line_shown) gfx_fill(6, y0 - 12, 308, shown * 15 + 6, 0x0000, 220);
  else gfx_fill(6, y0 - 12, 308, 32, 0x0000, 150);
  int first = g.nlines > shown ? g.nlines - shown : 0;
  for (int k = first; k < g.nlines; k++) {
    const Line *l = &g.lines[k];
    int y = y0 + (k - first) * 15;
    const Player *P = &g.p[l->who];
    art_player(P->color, 16, (float)y + 2, 0.45f, false, 256);
    gfx_text(FONT_MID, 26, y, COLOR_NAMES[P->color], 0xFFFF, 0, 0, 0);
    int w = gfx_text_width(FONT_MID, COLOR_NAMES[P->color]);
    gfx_text(FONT_MID, 30 + w, y, line_text(l, b, sizeof b), 0xD69A, 0, 0, 0);
  }
  if (g.meet_stage == 1 && can_vote)
    hud_keys(confirm ? "OK vote   Back cancel" : "arrows choose   OK vote   toolbox chat");
  else
    hud_keys(g.meet_stage == 0 ? "Discuss!   toolbox chat" : "toolbox chat");
}
