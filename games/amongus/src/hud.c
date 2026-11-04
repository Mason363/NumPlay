/* Among Us: the HUD over the ship: the task bar and list, the room's name, the buttons, the key line. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "gfx.h"
#include "view.h"
#include "world.h"

int hud_action(int *what);
int kill_target(int i);

#define YELLOW 0xFFE0
#define GREEN 0x07E0

static void task_line(const Task *t, char *buf, int n) {
  int steps = task_steps(t), step = t->step;
  const char *room = ROOM_NAMES[task_room(t)];
  if (TASK_DEFS[t->def].type == T_FUEL_ENGINES) steps = 2, step = t->step / 2;
  if (TASK_DEFS[t->def].type == T_INSPECT_SAMPLE && t->timer > 0 && !task_done(t))
    snprintf(buf, (size_t)n, "%s: %s %ds", room, task_name(t), (int)ceilf(t->timer));
  else if (steps > 1)
    snprintf(buf, (size_t)n, "%s: %s (%d/%d)", room, task_name(t), step, steps);
  else
    snprintf(buf, (size_t)n, "%s: %s", room, task_name(t));
}

static void sab_line(char *buf, int n) {
  switch (g.sab) {
  case SAB_REACTOR: {
    int k = (g.reactor_hold[0] != NONE) + (g.reactor_hold[1] != NONE);
    snprintf(buf, (size_t)n, "Reactor Meltdown in %d (%d/2)", (int)ceilf(g.sab_t), k);
    break;
  }
  case SAB_O2:
    snprintf(buf, (size_t)n, "Oxygen depleted in %d (%d/2)", (int)ceilf(g.sab_t), (g.o2_done & 1) + (g.o2_done >> 1 & 1));
    break;
  case SAB_LIGHTS: {
    int on = 0;
    for (int k = 0; k < 5; k++) on += g.switches >> k & 1;
    snprintf(buf, (size_t)n, "Fix Lights (%d%%)", on * 20);
    break;
  }
  case SAB_COMMS: snprintf(buf, (size_t)n, "Comms Sabotaged"); break;
  default: buf[0] = 0;
  }
}

static void draw_tasks(void) {
  const Player *M = &g.p[g.me];
  /* the bar: TOTAL TASKS COMPLETED */
  if (g.opt.taskbar != 2) {
    gfx_fill(4, 4, 124, 13, 0x0000, 200);
    gfx_fill(5, 5, 122, 11, rgb565(0x2B3A2E), 256);
    float f = g.opt.taskbar == 0 ? tasks_progress() : g.task_bar;
    gfx_fill(5, 5, (int)(122 * f), 11, rgb565(0x44D844), 256);
    gfx_text(FONT_SMALL, 66, 14, "TOTAL TASKS COMPLETED", 0xFFFF, 0, 1, 1);
  }
  char buf[64];
  int y = g.opt.taskbar != 2 ? 22 : 4, lines = 0;
  /* the list */
  const char *L[MAX_TASKS + 3];
  uint16_t C[MAX_TASKS + 3];
  static char store[MAX_TASKS + 3][64];
  if (g.sab) {
    sab_line(store[lines], 64);
    L[lines] = store[lines], C[lines] = ((int)(g.t * 2) & 1) ? 0xF800 : 0xFFFF, lines++;
  }
  if (M->imp) {
    L[lines] = "Sabotage and kill everyone.", C[lines] = 0xF800, lines++;
    L[lines] = "Fake Tasks:", C[lines] = 0xFFFF, lines++;
  } else if (!M->alive) {
    L[lines] = "You're dead, finish your tasks", C[lines] = 0xFFFF, lines++;
  }
  if (g.sab == SAB_COMMS && !M->imp) {
    L[lines] = "Comms Sabotaged", C[lines] = 0xF800, lines++;
  } else
    for (int k = 0; k < M->ntasks && lines < MAX_TASKS + 3; k++) {
      const Task *t = &M->tasks[k];
      task_line(t, store[lines], 64);
      L[lines] = store[lines];
      C[lines] = task_done(t) ? GREEN : (t->step > 0 || t->timer > 0) ? YELLOW : 0xFFFF;
      lines++;
    }
  int w = 0;
  for (int k = 0; k < lines; k++) {
    int tw = gfx_text_width(FONT_SMALL, L[k]);
    if (tw > w) w = tw;
  }
  if (y + lines * 11 + 4 < gfx_y0 || y > gfx_y0 + BAND) return;
  gfx_fill(4, y, w + 8, lines * 11 + 4, 0x0000, 130);
  for (int k = 0; k < lines; k++) gfx_text(FONT_SMALL, 8, y + 11 + k * 11, L[k], C[k], 0, 0, 0);
  (void)buf;
}

/* a button of the game's (its picture has its name), see-through when it can't be used, a cooldown number over it */
static void button(int x, int y, int pic_id, bool on, int cd) {
  gfx_spr(pic(pic_id), x, y, false, NULL, on ? 256 : 96);
  if (cd > 0) {
    char b[8];
    snprintf(b, sizeof b, "%d", cd);
    gfx_text(FONT_HUGE, x, y + 9, b, 0xFFFF, 0x0000, 2, 1);
  }
}

/* the action button's picture and word (for the key line) */
static int act_pic(int a, bool imp, const char **word) {
  switch (a) {
  case 2: *word = "report"; return PIC_UI_REPORTBUTTON;
  case 3: *word = "vent"; return PIC_UI_VENT;
  case 5: *word = "admin"; return PIC_UI_MAPADMIN;
  case 6: *word = "cameras"; return PIC_UI_SECURITY;
  case 1:
  case 4: *word = "use"; return PIC_UI_USEBUTTON;
  default: *word = imp ? "sabotage" : "use"; return imp ? PIC_UI_SABOTAGE : PIC_UI_USEBUTTON;
  }
}

static void draw_keys(const char *s) {
  gfx_fill(0, 228, SCREEN_W, 12, 0x0000, 190);
  gfx_text(FONT_SMALL, 160, 237, s, 0xFFFF, 0, 0, 1);
}

void hud_keys(const char *s) { draw_keys(s); }

/* in a vent: an arrow toward each vent it leads to (Vent.Buttons) */
static void vent_arrows(const Player *M) {
  const VentRec *V = &VENTS[M->vent];
  float px = M->pos.x * PX_PER_UNIT + SHIP_OX - view.cx, py = -M->pos.y * PX_PER_UNIT + SHIP_OY - view.cy;
  for (int k = 0; k < 3; k++) {
    if (V->link[k] < 0) continue;
    float dx = VENTS[V->link[k]].x - V->x, dy = -(VENTS[V->link[k]].y - V->y), l = sqrtf(dx * dx + dy * dy);
    dx /= l, dy /= l;
    float cx = px + dx * 38, cy = py + dy * 38;
    for (int o = 1; o >= 0; o--) {
      float tip = 9 + 2.5f * o, back = 5 + 2.5f * o, side = 7 + 2.5f * o;
      float t[6] = {cx + dx * tip, cy + dy * tip, cx - dx * back - dy * side, cy - dy * back + dx * side,
                    cx - dx * back + dy * side, cy - dy * back - dx * side};
      gfx_poly(t, 3, o ? 0x0000 : 0xFFFF, 256);
    }
  }
}

void hud_draw(void) {
  const Player *M = &g.p[g.me];
  if (M->vent != NONE && !M->vent_anim) vent_arrows(M);
  draw_tasks();
  /* the room's name (RoomTracker), for a while after entering */
  if (g.room_t > 0 && g.room_shown != R_HALLWAY) {
    int a = g.room_t > 0.5f ? 256 : (int)(g.room_t * 512);
    (void)a;
    gfx_text(FONT_BIG, 160, 222, ROOM_NAMES[g.room_shown], 0xFFFF, 0x0000, 1, 1);
  }
  /* the critical sabotage's red flash */
  if ((g.sab == SAB_REACTOR || g.sab == SAB_O2) && fmodf(g.t, 1.2f) < 0.6f) {
    gfx_fill(0, 0, SCREEN_W, 6, 0xF800, 150);
    gfx_fill(0, SCREEN_H - 18, SCREEN_W, 6, 0xF800, 150);
    gfx_fill(0, 6, 6, SCREEN_H - 24, 0xF800, 150);
    gfx_fill(SCREEN_W - 6, 6, 6, SCREEN_H - 24, 0xF800, 150);
  }
  int what = -1, a = hud_action(&what);
  if (M->vent != NONE) a = 3;
  char keys[96];
  const char *word;
  bool imp = M->imp && M->alive;
  int bp = act_pic(a, M->imp, &word);
  button(296, 204, bp, a != 0 || M->imp, 0);
  if (imp) {
    int cd = M->kill_cd > 0 ? (int)ceilf(M->kill_cd) : 0;
    button(254, 204, PIC_UI_KILLBUTTON, cd == 0 && kill_target(g.me) >= 0, cd);
  }
  if (M->vent != NONE)
    snprintf(keys, sizeof keys, "arrows other vents   OK leave   toolbox sabotage   Back pause");
  else if (imp)
    snprintf(keys, sizeof keys, "OK %s   EXE kill   toolbox sabotage   Back pause", word);
  else
    snprintf(keys, sizeof keys, "arrows walk   OK %s   toolbox %s   Back pause", word, M->imp ? "sabotage" : "map");
  draw_keys(keys);
}
