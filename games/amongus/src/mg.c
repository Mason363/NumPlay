/* Among Us: the minigames, made for the keypad: each task's panel from the game's own pictures, at the phone's
 * size (40 px a unit), its root at MG_X, MG_Y. This file: opening and closing, and the first half of them. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "mg.h"
#include "view.h"
#include "world.h"

void hud_keys(const char *s);

#define WHITE 0xFFFF
#define BLACK 0x0000

void mg_keys(const char *s) { hud_keys(s); }
void mg_bg(int blob_id) { gfx_tiles((const TilePic *)unpacked(blob_id, 0), MG_X, MG_Y); }
void mg_status(const char *s, int y, uint16_t c) { gfx_text(FONT_MID, MG_X, y, s, c, BLACK, 1, 1); }

static int kind_of(int cons, int task) {
  if (task < MAX_TASKS) {
    const Task *t = &g.p[g.me].tasks[task];
    switch (TASK_DEFS[t->def].type) {
    case T_SWIPE_CARD: return MG_CARD;
    case T_FIX_WIRING: return MG_WIRES;
    case T_CLEAR_ASTEROIDS: return MG_ASTEROIDS;
    case T_ALIGN_ENGINE: return MG_ALIGN;
    case T_SUBMIT_SCAN: return MG_SCAN;
    case T_INSPECT_SAMPLE: return MG_SAMPLE;
    case T_FUEL_ENGINES: return MG_FUEL;
    case T_START_REACTOR: return MG_SIMON;
    case T_EMPTY_CHUTE:
    case T_EMPTY_GARBAGE: return MG_GARBAGE;
    case T_UPLOAD_DATA: return MG_UPLOAD;
    case T_CALIBRATE: return MG_CALIBRATE;
    case T_CHART_COURSE: return MG_COURSE;
    case T_CLEAN_O2: return MG_LEAVES;
    case T_UNLOCK_MANIFOLDS: return MG_MANIFOLDS;
    case T_STABILIZE: return MG_STEERING;
    case T_PRIME_SHIELDS: return MG_SHIELDS;
    case T_DIVERT_POWER: return t->step ? MG_ACCEPT : MG_DIVERT;
    }
  }
  const ConsoleRec *C = &CONSOLES[cons];
  if (C->kind == 2) return MG_EMERGENCY;
  if (C->kind == 3) return MG_CAMS;
  if (C->types >> T_RESET_REACTOR & 1) return MG_HANDS;
  if (C->types >> T_RESTORE_O2 & 1) return MG_KEYPAD;
  if (C->types >> T_FIX_LIGHTS & 1) return MG_LIGHTS;
  return MG_COMMS;
}

static void open1(int kind);

void mg_open(int cons, int task) {
  memset(g.mg, 0, sizeof g.mg);
  M->cons = (uint8_t)cons;
  M->task = (uint8_t)(task < MAX_TASKS ? task : NONE);
  M->kind = (uint8_t)kind_of(cons, task);
  g.overlay = O_TASK;
  if (M->kind <= MG_COURSE) open1(M->kind);
  else mg2_open(M->kind);
}

bool mg_blocks_move(void) { return g.overlay != O_NONE; }

void mg_finish(void) {
  if (M->done) return;
  M->done = 1;
  M->done_t = 0;
  if (M->task != NONE) task_step_done(g.me, M->task);
}

static void tick1(uint32_t keys, uint32_t pressed);
static void draw1(void);

void mg_tick(uint32_t keys, uint32_t pressed) {
  M->t += DT;
  if (M->done) {
    /* the panel stays a moment, then closes (Minigame.Close after the task is done) */
    M->done_t += DT;
    if (M->done_t > 0.75f || (pressed & K_BACK)) g.overlay = O_NONE;
    return;
  }
  /* a sabotage fixed meanwhile, or the player dead: closed */
  if (M->kind >= MG_HANDS && M->kind <= MG_KEYPAD && !g.sab) {
    g.overlay = O_NONE;
    return;
  }
  if (pressed & K_BACK) {
    g.overlay = O_NONE;
    return;
  }
  if (M->kind <= MG_COURSE) tick1(keys, pressed);
  else mg2_tick(keys, pressed);
}

void mg_draw(void) {
  view_draw();
  gfx_fill(0, gfx_y0, SCREEN_W, BAND, BLACK, 110);
  if (M->kind <= MG_COURSE) draw1();
  else mg2_draw();
}

/* ---- Swipe Card (CardSlideGame: AcceptedTime 0.33-0.6 s) ---- */

#define CARD_Y 82
#define CARD_X0 72
#define CARD_X1 248

static void card_tick(uint32_t keys, uint32_t pressed) {
  /* phase 0 in the wallet, 1 at the reader, 2 swiping, 3 a bad read shown */
  if (M->phase == 0 && (pressed & K_OK)) M->phase = 1, M->a = CARD_X0, snprintf(M->text, sizeof M->text, "Please swipe card");
  else if (M->phase == 1 && (keys & K_RIGHT)) M->phase = 2, M->b = 0, M->c = 0;
  else if (M->phase == 2) {
    M->b += DT;
    if (keys & K_RIGHT) {
      M->c += DT;
      float v = 30 * M->c;   /* faster as it's held (a finger's swipe) */
      if (v > 12) v = 12;
      M->a += v * DT * PX_PER_UNIT;
    }
    if (M->a >= CARD_X1) {
      M->a = CARD_X1;
      if (M->b < 0.33f) snprintf(M->text, sizeof M->text, "Too fast. Try again.");
      else if (M->b > 0.6f) snprintf(M->text, sizeof M->text, "Too slow. Try again.");
      else {
        snprintf(M->text, sizeof M->text, "Accepted. Thank you.");
        M->flags = 2;
        mg_finish();
        return;
      }
      M->phase = 3, M->flags = 1, M->c = 0;
    } else if (!(keys & K_RIGHT) && M->c > 0) {
      snprintf(M->text, sizeof M->text, "Bad read. Try again.");
      M->phase = 3, M->flags = 1, M->c = 0;
    }
  } else if (M->phase == 3) {
    M->a += (CARD_X0 - M->a) * 0.3f;
    M->c += DT;
    if (M->c > 0.4f) M->phase = 1, M->a = CARD_X0;
  }
}

static void card_draw(void) {
  mg_bg(BLOB_MG_CARD);
  if (M->phase == 0) {
    gfx_spr(pic(PIC_MG_CARD_CARD), MG_CARD_CARD_X, MG_CARD_CARD_Y, false, NULL, 256);
    gfx_spr(pic(PIC_MG_CARD_FRONT), MG_CARD_FRONT_X, MG_CARD_FRONT_Y, false, NULL, 256);
  } else {
    gfx_spr(pic(PIC_MG_CARD_FRONT), MG_CARD_FRONT_X, MG_CARD_FRONT_Y, false, NULL, 256);
    gfx_spr(pic(PIC_MG_CARD_CARD), (int)M->a, CARD_Y, false, NULL, 256);
  }
  gfx_spr(pic(PIC_MG_CARD_TOP), MG_CARD_TOP_X, MG_CARD_TOP_Y, false, NULL, 256);
  /* the lights: red for a bad read, green when accepted */
  gfx_spr(pic(PIC_MG_CARD_RED), MG_CARD_RED_X, MG_CARD_RED_Y, false, NULL, 256);
  if (M->flags != 1) gfx_spr_tint(pic(PIC_MG_CARD_RED), MG_CARD_RED_X, MG_CARD_RED_Y, BLACK, 160);
  gfx_spr(pic(PIC_MG_CARD_GREEN), MG_CARD_GREEN_X, MG_CARD_GREEN_Y, false, NULL, 256);
  if (M->flags != 2) gfx_spr_tint(pic(PIC_MG_CARD_GREEN), MG_CARD_GREEN_X, MG_CARD_GREEN_Y, BLACK, 160);
  gfx_text(FONT_MID, 74, 26, M->phase == 0 ? "Please insert card" : M->text, rgb565(0x4CFF4C), 0, 0, 0);
  mg_keys(M->phase == 0 ? "OK take the card out   Back close" : "hold right to swipe   Back close");
}

/* ---- Fix Wiring (WireMinigame): red, blue, yellow, magenta ---- */

static const uint32_t WIRE_COLORS[4] = {0xFF0000, 0x2626FF, 0xFFEB04, 0xFF00FF};

static void wires_open(void) {
  for (int k = 0; k < 4; k++) M->order[k] = (uint8_t)k, M->order[4 + k] = (uint8_t)k;
  for (int s = 0; s < 2; s++)
    for (int k = 3; k > 0; k--) {
      int j = rnd_n(k + 1);
      uint8_t t = M->order[s * 4 + k];
      M->order[s * 4 + k] = M->order[s * 4 + j], M->order[s * 4 + j] = t;
    }
  for (int k = 0; k < 4; k++) M->state[k] = NONE;   /* where each left wire is plugged */
}

static void wires_tick(uint32_t pressed) {
  /* phase 0: choosing a wire on the left; 1: carrying it to the right */
  if (pressed & K_UP) M->n = (uint8_t)((M->n + 3) & 3);
  if (pressed & K_DOWN) M->n = (uint8_t)((M->n + 1) & 3);
  if (M->phase == 0) {
    if (pressed & K_OK && M->state[M->n] == NONE) M->sel = M->n, M->phase = 1;
  } else {
    if (pressed & K_OK) {
      int left = M->order[M->sel], right = M->order[4 + M->n];
      if (left == right) {
        M->state[M->sel] = M->n;
        int done = 0;
        for (int k = 0; k < 4; k++) done += M->state[k] != NONE;
        if (done == 4) mg_finish();
      }
      M->phase = 0;
      /* the next free wire */
      for (int k = 0; k < 4; k++)
        if (M->state[(M->sel + k) & 3] == NONE) {
          M->n = (uint8_t)((M->sel + k) & 3);
          break;
        }
    }
  }
}

static void wire(int y0, int x1, int y1, uint32_t col) {
  gfx_line(88, (float)y0, (float)x1, (float)y1, 9, BLACK, 256);
  gfx_line(88, (float)y0, (float)x1, (float)y1, 6, rgb565(col), 256);
}

static void wires_draw(void) {
  mg_bg(BLOB_MG_WIRES);
  for (int k = 0; k < 4; k++) {
    int ly = MG_WIRES_HEAD_XY[k][1], ry = MG_WIRES_RNODE_XY[k][1];
    uint32_t lc = WIRE_COLORS[M->order[k]], rc = WIRE_COLORS[M->order[4 + k]];
    /* the right ends */
    gfx_fill(234, ry - 5, 30, 10, BLACK, 256);
    gfx_fill(234, ry - 4, 30, 8, rgb565(rc), 256);
    gfx_spr(pic(PIC_MG_WIRES_RNODE), MG_WIRES_RNODE_XY[k][0], ry, false, NULL, 256);
    gfx_spr(pic(PIC_X_WIREICONS0001 + M->order[4 + k]), 254, ry, false, NULL, 256);
    /* the left wires: plugged, carried or waiting */
    gfx_fill(56, ly - 5, 34, 10, BLACK, 256);
    gfx_fill(56, ly - 4, 34, 8, rgb565(lc), 256);
    int hx = MG_WIRES_HEAD_XY[k][0], hy = ly;
    if (M->state[k] != NONE) hx = 220, hy = MG_WIRES_RNODE_XY[M->state[k]][1];
    else if (M->phase == 1 && M->sel == k) hx = 205, hy = MG_WIRES_RNODE_XY[M->n][1];
    if (hx != MG_WIRES_HEAD_XY[k][0]) wire(ly, hx, hy, lc);
    gfx_spr(pic(PIC_MG_WIRES_HEAD), hx, hy, false, NULL, 256);
    gfx_fill(hx - 10, hy - 3, 7, 6, rgb565(lc), 256);
    gfx_spr(pic(PIC_X_WIREICONS0001 + M->order[k]), MG_WIRES_SYM_XY[k][0], ly, false, NULL, 256);
    /* the lights: on when a wire is in */
    if (M->state[k] != NONE) {
      gfx_fill(64, ly - 18, 10, 4, 0xFFE0, 256);
      gfx_fill(248, MG_WIRES_RNODE_XY[M->state[k]][1] - 18, 10, 4, 0xFFE0, 256);
    }
  }
  /* the choice */
  int y = M->phase == 0 ? MG_WIRES_HEAD_XY[M->n][1] : MG_WIRES_RNODE_XY[M->n][1];
  int x = M->phase == 0 ? 46 : 272;
  float q[6] = {(float)x - 6, (float)y - 7, (float)x - 6, (float)y + 7, (float)x + 6, (float)y};
  if (M->phase) q[0] = q[2] = (float)x + 6, q[4] = (float)x - 6;
  gfx_poly(q, 3, rgb565(0x05FF00), 256);
  mg_keys(M->phase == 0 ? "up down choose a wire   OK pick it up   Back close" : "up down choose where   OK plug it in");
}

/* ---- Clear Asteroids (WeaponsMinigame: 20 to destroy) ---- */

#define AST_X0 60
#define AST_X1 260
#define AST_Y0 12
#define AST_Y1 212

static void ast_tick(uint32_t keys, uint32_t pressed) {
  Task *t = &g.p[g.me].tasks[M->task];
  /* the reticle */
  float sp = 160 * DT;
  if (keys & K_LEFT) M->a -= sp;
  if (keys & K_RIGHT) M->a += sp;
  if (keys & K_UP) M->b -= sp;
  if (keys & K_DOWN) M->b += sp;
  if (M->t < DT * 1.5f) M->a = 160, M->b = 112;
  if (M->a < AST_X0 + 10) M->a = AST_X0 + 10;
  if (M->a > AST_X1 - 10) M->a = AST_X1 - 10;
  if (M->b < AST_Y0 + 10) M->b = AST_Y0 + 10;
  if (M->b > AST_Y1 - 10) M->b = AST_Y1 - 10;
  /* asteroids come in from the right */
  M->c -= DT;
  if (M->c <= 0) {
    M->c = 0.5f + rnd_f() * 0.6f;
    for (int k = 0; k < 10; k++) {
      Thing *a = &M->th[k];
      if (a->on) continue;
      a->on = 1;
      a->kind = (uint8_t)rnd_n(5);
      a->x = AST_X1 + 30;
      a->y = AST_Y0 + 20 + rnd_f() * (AST_Y1 - AST_Y0 - 40);
      a->vx = -(40 + rnd_f() * 50);
      a->vy = (rnd_f() - 0.5f) * 40;
      a->a = 0;
      break;
    }
  }
  for (int k = 0; k < 10; k++) {
    Thing *a = &M->th[k];
    if (!a->on) continue;
    if (a->on == 2) {
      a->a += DT;
      if (a->a > 0.3f) a->on = 0;
      continue;
    }
    a->x += a->vx * DT, a->y += a->vy * DT;
    if (a->x < AST_X0 - 40 || a->y < AST_Y0 - 40 || a->y > AST_Y1 + 40) a->on = 0;
  }
  if (M->flags) M->flags--;
  if (pressed & K_OK) {
    M->flags = 4;   /* the shot's lines, a few frames */
    for (int k = 0; k < 10; k++) {
      Thing *a = &M->th[k];
      if (a->on != 1) continue;
      float dx = a->x - M->a, dy = a->y - M->b;
      if (dx * dx + dy * dy < 20 * 20) {
        a->on = 2, a->a = 0;
        task_step_done(g.me, M->task);
        if (task_done(t)) M->done = 1, M->done_t = -0.5f;
        break;
      }
    }
  }
}

static void ast_draw(void) {
  mg_bg(BLOB_MG_ASTEROIDS);
  for (int k = 0; k < 10; k++) {
    const Thing *a = &M->th[k];
    if (!a->on) continue;
    const Pic *p = pic(a->on == 2 ? PIC_X_WEAPONS_EXPLOSION : PIC_X_WEAPONS_ASTEROID1 + a->kind);
    gfx_spr_clip(p, (int)a->x, (int)a->y, false, NULL, 256, AST_X0, AST_Y0, AST_X1, AST_Y1);
  }
  if (M->flags) {
    gfx_line(AST_X0, AST_Y1, M->a, M->b, 2, rgb565(0x7CFC7C), 256);
    gfx_line(AST_X1, AST_Y1, M->a, M->b, 2, rgb565(0x7CFC7C), 256);
  }
  gfx_spr(pic(PIC_MG_ASTEROIDS_RETICLE), (int)M->a, (int)M->b, false, NULL, 256);
  char b[32];
  const Task *t = &g.p[g.me].tasks[M->task];
  snprintf(b, sizeof b, "Destroyed: %d", t->step);
  gfx_text(FONT_MID, AST_X0 + 6, AST_Y1 - 6, b, rgb565(0x7CFC7C), BLACK, 1, 0);
  mg_keys("arrows aim   OK fire   Back close");
}

/* ---- Align Engine Output (AlignGame: the slider on its arc, YRange +-2.13) ---- */

static void align_open(void) {
  M->a = (rnd() & 1 ? 1 : -1) * (1.0f + rnd_f() * 1.0f);   /* the slider, in units */
}

static void align_tick(uint32_t keys) {
  if (keys & K_UP) M->a += 1.6f * DT;
  if (keys & K_DOWN) M->a -= 1.6f * DT;
  if (M->a > 2.13f) M->a = 2.13f;
  if (M->a < -2.13f) M->a = -2.13f;
  if (!(keys & (K_UP | K_DOWN)) && fabsf(M->a) < 0.06f) {
    M->a = 0;
    mg_finish();
  }
}

static void align_draw(void) {
  mg_bg(BLOB_MG_ALIGN);
  float y = M->a * PX_PER_UNIT;
  /* the engine turns with the slider */
  gfx_pic(pic(PIC_MG_ALIGN_ENGINE), MG_ALIGN_ENGINE_X, MG_ALIGN_ENGINE_Y, 1, 1, -M->a * 0.2f, NULL, 256);
  /* the centre line, and the slider on its arc */
  gfx_line(70, MG_Y, 205, MG_Y, 1, M->done ? rgb565(0x37FF37) : rgb565(0xFF4040), 200);
  float R = 230, dx = R - sqrtf(R * R - y * y);
  gfx_spr(pic(PIC_MG_ALIGN_SLIDER), MG_ALIGN_SLIDER_X - (int)dx, MG_Y - (int)y, false, NULL, 256);
  mg_keys("up down move the slider to the middle   Back close");
}

/* ---- Submit Scan (MedScanMinigame: one at a time on the scanner) ---- */

int scanning_player(void) {
  for (int i = 0; i < g.nplayers; i++)
    if (i != g.me && g.p[i].alive && g.p[i].busy && g.p[i].bot.task < MAX_TASKS &&
        TASK_DEFS[g.p[i].tasks[g.p[i].bot.task].def].type == T_SUBMIT_SCAN)
      return i;
  return -1;
}

static void scan_tick(void) {
  Player *P = &g.p[g.me];
  if (M->phase == 0) {
    int w = scanning_player();
    if (w < 0) {
      M->phase = 1, M->b = 0;
      /* onto the scanner */
      P->pos = (V2){CONSOLES[M->cons].x, CONSOLES[M->cons].y - 0.05f};
      P->flip = 0;
    } else
      M->sel = (uint8_t)w;
  } else if (M->phase == 1) {
    M->b += DT;
    P->busy = 1;
    if (M->b >= 10) {
      P->busy = 0;
      mg_finish();
    }
  }
}

static void scan_draw(void) {
  mg_bg(BLOB_MG_SCAN);
  const Player *P = &g.p[g.me];
  char b[64];
  if (M->phase == 0) snprintf(b, sizeof b, "Waiting for %s...", COLOR_NAMES[g.p[M->sel].color]);
  else snprintf(b, sizeof b, M->done ? "Scan complete" : "Scanning...");
  gfx_text(FONT_MID, 72, 191, b, WHITE, 0, 0, 0);
  if (M->phase) {
    /* the readout and the bar */
    static const char *const BT[] = {"O-", "A+", "B-", "AB", "O+", "A-"};
    snprintf(b, sizeof b, "ID: %c%c%c%c%c  HT: 3' 6\"  WT: 92lb", 'A' + P->color, 'K' + P->color % 7, 'R', 'D' + P->color % 5,
             'X' - P->color % 4);
    gfx_text(FONT_SMALL, 72, 46, b, WHITE, 0, 0, 0);
    snprintf(b, sizeof b, "C: %s   BT: %s", COLOR_NAMES[P->color], BT[P->color % 6]);
    gfx_text(FONT_SMALL, 72, 60, b, WHITE, 0, 0, 0);
    float f = M->b / 10;
    if (f > 1) f = 1;
    gfx_fill(70, 160, (int)(180 * f), 10, rgb565(0x00C017), 256);
  }
  mg_keys("stay still while the scan runs   Back close");
}

/* ---- Inspect Sample (SampleMinigame: 60 s for the reagent) ---- */

static void sample_open(void) {
  const Task *t = &g.p[g.me].tasks[M->task];
  if (t->timer == 0) M->phase = 3, M->sel = 2, M->n = (uint8_t)rnd_n(5);
  else if (t->timer > 0) M->phase = 2;
}

static void sample_tick(uint32_t pressed) {
  Task *t = &g.p[g.me].tasks[M->task];
  if (M->phase == 0 && (pressed & K_OK)) M->phase = 1, M->b = 0;
  else if (M->phase == 1) {
    M->b += DT;
    if (M->b > 2) {
      M->phase = 2;
      t->timer = 60;
    }
  } else if (M->phase == 2 && t->timer == 0) M->phase = 3, M->sel = 2, M->n = (uint8_t)rnd_n(5);
  else if (M->phase == 3) {
    if (pressed & K_LEFT) M->sel = (uint8_t)((M->sel + 4) % 5);
    if (pressed & K_RIGHT) M->sel = (uint8_t)((M->sel + 1) % 5);
    if (pressed & K_OK) {
      if (M->sel == M->n) mg_finish();
      else {
        /* a wrong one: start over */
        M->phase = 0;
        t->timer = -1;
      }
    }
  }
}

static void sample_draw(void) {
  mg_bg(BLOB_MG_SAMPLE);
  const Task *t = &g.p[g.me].tasks[M->task];
  /* the tubes, filled from left to right */
  int filled = M->phase == 1 ? (int)(M->b / 2 * 5.99f) : M->phase >= 2 ? 5 : 0;
  for (int k = 0; k < 5; k++) {
    if (k >= filled) continue;
    int x = MG_SAMPLE_LIQUID_XY[k][0], y = MG_SAMPLE_LIQUID_XY[k][1];
    gfx_spr(pic(PIC_MG_SAMPLE_LIQUID), x, y, false, NULL, 256);
    if (M->phase == 3 && k == M->n) gfx_spr_tint(pic(PIC_MG_SAMPLE_LIQUID), x, y, 0xF800, 200);
  }
  /* the buttons under them, left to right */
  static const uint8_t ORDER[5] = {2, 1, 0, 3, 4};
  for (int k = 0; k < 5; k++) {
    int i = ORDER[k];
    gfx_spr(pic(PIC_MG_SAMPLE_BUTTON), MG_SAMPLE_BUTTON_XY[i][0], MG_SAMPLE_BUTTON_XY[i][1], false, NULL, 256);
    if (M->phase == 3 && M->sel == k) gfx_rect(MG_SAMPLE_BUTTON_XY[i][0] - 9, MG_SAMPLE_BUTTON_XY[i][1] - 9, 18, 18, 2, 0xFFE0);
  }
  gfx_spr(pic(PIC_MG_SAMPLE_CONFIRM), MG_SAMPLE_CONFIRM_X, MG_SAMPLE_CONFIRM_Y, false, NULL, 256);
  char b[40];
  if (M->phase == 0) snprintf(b, sizeof b, "Press to start");
  else if (M->phase == 1) snprintf(b, sizeof b, "Adding reagent.");
  else if (M->phase == 2) snprintf(b, sizeof b, "%ds remaining", (int)ceilf(t->timer));
  else snprintf(b, sizeof b, M->done ? "Thank you!" : "Select anomaly.");
  gfx_text(FONT_SMALL, MG_X, 204, b, rgb565(0x7CFC7C), 0, 0, 1);
  mg_keys(M->phase == 0 ? "OK start   Back close"
                        : M->phase == 3 ? "left right choose the odd one   OK select" : "Back close (it keeps going)");
}

/* ---- Fuel Engines (RefuelStage: hold 3 s; the can in Storage, then each engine) ---- */

static void fuel_tick(uint32_t keys) {
  if (keys & K_OK) {
    M->b += DT / 3;
    if (M->b >= 1) M->b = 1, mg_finish();
  }
}

static void fuel_draw(void) {
  const Task *t = &g.p[g.me].tasks[M->task];
  bool can = !(t->step & 1);
  /* black behind the glass, the fuel rising behind the outline */
  if (can) gfx_fill(98, 31, 117, 140, BLACK, 256);
  else gfx_fill(121, 21, 79, 162, BLACK, 256);
  if (can) {
    int h = (int)(M->b * 112);
    gfx_fill(104, 162 - h, 98, h, rgb565(0xC4AA14), 256);
  } else {
    int h = (int)(M->b * 116);
    gfx_fill(146, 168 - h, 26, h, rgb565(0xC4AA14), 256);
  }
  mg_bg(can ? BLOB_MG_FUEL1 : BLOB_MG_FUEL2);
  gfx_spr(pic(PIC_MG_FUEL1_BUTTON), MG_FUEL1_BUTTON_X, MG_FUEL1_BUTTON_Y, false, NULL, 256);
  gfx_spr(pic(PIC_MG_FUEL1_LIGHT), MG_FUEL1_LIGHT_X, MG_FUEL1_LIGHT_Y, false, NULL, 256);
  if (!M->done) gfx_spr_tint(pic(PIC_MG_FUEL1_LIGHT), MG_FUEL1_LIGHT_X, MG_FUEL1_LIGHT_Y, BLACK, 140);
  gfx_spr(pic(PIC_MG_FUEL1_LIGHT), MG_FUEL1_LIGHT_X + 22, MG_FUEL1_LIGHT_Y, false, NULL, 256);
  gfx_spr_tint(pic(PIC_MG_FUEL1_LIGHT), MG_FUEL1_LIGHT_X + 22, MG_FUEL1_LIGHT_Y, M->done ? 0x07E0 : 0x0200, 220);
  mg_keys("hold OK to fill   Back close");
}

/* ---- Start Reactor (SimonSaysGame: 5 rounds, one more light each) ---- */

static void simon_open(void) {
  for (int k = 0; k < 5; k++) M->order[k] = (uint8_t)rnd_n(9);
  M->phase = 0, M->b = -0.6f;
}

static int simon_key(uint32_t pressed) {
  /* the digit keys as the grid (7 8 9 on top), or the cursor */
  static const int8_t DIG[10] = {-1, 6, 7, 8, 3, 4, 5, 0, 1, 2};
  for (int d = 1; d <= 9; d++)
    if (pressed & ((uint32_t)K_D0 << d)) return DIG[d];
  if (pressed & K_LEFT) M->sel = (uint8_t)(M->sel % 3 ? M->sel - 1 : M->sel + 2);
  if (pressed & K_RIGHT) M->sel = (uint8_t)(M->sel % 3 == 2 ? M->sel - 2 : M->sel + 1);
  if (pressed & K_UP) M->sel = (uint8_t)((M->sel + 6) % 9);
  if (pressed & K_DOWN) M->sel = (uint8_t)((M->sel + 3) % 9);
  if (pressed & K_OK) return M->sel;
  return -1;
}

static void simon_tick(uint32_t pressed) {
  Task *t = &g.p[g.me].tasks[M->task];
  int round = t->step;   /* this round shows round + 1 lights */
  M->b += DT;
  if (M->phase == 0) {
    /* the computer's side: a light each 0.5 s */
    if (M->b >= (round + 1) * 0.5f + 0.2f) M->phase = 1, M->n = 0, M->b = 0;
  } else if (M->phase == 1) {
    int k = simon_key(pressed);
    if (k >= 0) {
      M->stage = (uint8_t)k, M->c = 0.25f;
      if (k != M->order[M->n]) {
        M->phase = 2, M->b = 0;   /* wrong: red, then from the start */
        t->step = 0;
      } else if (++M->n > round) {
        task_step_done(g.me, M->task);
        if (task_done(t)) M->done = 1, M->done_t = 0;
        else M->phase = 0, M->b = -0.6f;
      }
    }
  } else if (M->phase == 2 && M->b > 1) M->phase = 0, M->b = -0.6f;
  if (M->c > 0) M->c -= DT;
}

static void simon_draw(void) {
  mg_bg(BLOB_MG_SIMON);
  const Task *t = &g.p[g.me].tasks[M->task];
  int round = t->step;
  const float S = 1.35f * PX_PER_UNIT;
  /* the left screen: the light now shown */
  int lit = -1;
  if (M->phase == 0 && M->b >= 0) {
    int k = (int)(M->b / 0.5f);
    if (k <= round && fmodf(M->b, 0.5f) < 0.38f) lit = M->order[k];
  }
  for (int k = 0; k < 9; k++) {
    int x = (int)(MG_X + (-1.72f + 0.47f * (k % 3)) * S), y = (int)(MG_Y - (0.23f - 0.47f * (k / 3)) * S);
    uint16_t c = M->phase == 2 ? 0xF800 : k == lit ? rgb565(0x3D8CFF) : 0x0000;
    if (c) gfx_fill(x - 11, y - 11, 23, 23, c, 256);
  }
  /* the buttons */
  for (int k = 0; k < 9; k++) {
    int x = MG_SIMON_BUTTON_XY[k][0], y = MG_SIMON_BUTTON_XY[k][1];
    gfx_spr(pic(PIC_X_SSBUTTON1 + k), x, y, false, NULL, 256);
    if (M->c > 0 && M->stage == k) gfx_spr_tint(pic(PIC_X_SSBUTTON1 + k), x, y, rgb565(0x3D8CFF), 150);
    if (M->phase == 1 && M->sel == k) gfx_rect(x - 13, y - 13, 26, 26, 2, rgb565(0x1CFF00));
  }
  /* the rounds' lights on both sides */
  for (int k = 0; k < 5; k++) {
    bool on = k < round || (M->phase == 1 && k < M->n);
    for (int s = 0; s < 2; s++) {
      int x = (int)(MG_X + ((s ? 0.57f : -1.97f) + 0.34f * k) * S), y = (int)(MG_Y - 0.91f * S);
      gfx_spr(pic(PIC_X_SSLIGHTS1 + k), x, y, false, NULL, 256);
      bool show = s ? on : k <= round;
      if (!show) gfx_spr_tint(pic(PIC_X_SSLIGHTS1 + k), x, y, BLACK, 140);
    }
  }
  mg_keys(M->phase == 1 ? "repeat it: 7 8 9 / 4 5 6 / 1 2 3, or arrows and OK" : "watch the lights   Back close");
}

/* ---- Empty Garbage / Empty Chute (EmptyGarbageMinigame: pull the lever and hold) ---- */

static void garbage_open(void) {
  const Task *t = &g.p[g.me].tasks[M->task];
  bool leaves = TASK_DEFS[t->def].type == T_EMPTY_CHUTE;
  for (int k = 0; k < 12; k++) {
    Thing *a = &M->th[k];
    a->on = 1;
    a->kind = (uint8_t)(leaves ? rnd_n(7) : 10 + rnd_n(6));
    a->x = 75 + rnd_f() * 95;
    a->y = 110 + rnd_f() * 90;
    a->vy = 0;
    a->a = rnd_f() * 6.28f;
  }
}

static void garbage_tick(uint32_t keys) {
  bool pull = keys & K_DOWN;
  M->a += ((pull ? 1.0f : 0) - M->a) * 0.35f;   /* the handle: 0 up, 1 down */
  int left = 0;
  for (int k = 0; k < 12; k++) {
    Thing *a = &M->th[k];
    if (!a->on) continue;
    if (M->a > 0.8f) {
      a->vy += 900 * DT;
      a->y += a->vy * DT;
      a->a += DT * 3;
      if (a->y > 240) a->on = 0;
    }
    left += a->on;
  }
  if (!left && !M->done) mg_finish();
}

static void garbage_draw(void) {
  mg_bg(BLOB_MG_GARBAGE);
  for (int k = 0; k < 12; k++) {
    const Thing *a = &M->th[k];
    if (!a->on) continue;
    const Pic *p = pic(a->kind >= 10 ? PIC_X_GARBAGE_1 + a->kind - 10 : PIC_X_O2_LEAF1 + a->kind);
    gfx_pic(p, a->x, a->y, 1, 1, a->a, NULL, 256);
  }
  gfx_spr(pic(PIC_MG_GARBAGE_BARS), MG_GARBAGE_BARS_X, MG_GARBAGE_BARS_Y, false, NULL, 256);
  gfx_spr(pic(PIC_MG_GARBAGE_HANDLE), MG_GARBAGE_HANDLE_X, MG_GARBAGE_HANDLE_Y + (int)(M->a * 52), false, NULL, 256);
  mg_keys("hold down to pull the lever   Back close");
}

/* ---- Upload / Download Data (UploadDataGame) ---- */

static void upload_tick(uint32_t pressed) {
  if (M->phase == 0 && (pressed & K_OK)) M->phase = 1, M->b = 0;
  else if (M->phase == 1) {
    M->b += DT;
    if (M->b >= 8.5f) mg_finish();
  }
}

static void upload_draw(void) {
  mg_bg(BLOB_MG_UPLOAD);
  const Task *t = &g.p[g.me].tasks[M->task];
  bool up = t->step == 1;
  int fr = M->phase ? (M->b < 0.2f ? (int)(M->b * 20) : 4) : 0;
  if (fr > 4) fr = 4;
  /* the folders, the file running between them */
  gfx_spr(pic(PIC_X_DATATRANSFER_FOLDEROPEN0001 + fr), 106, 104, false, NULL, 256);
  gfx_spr(pic(PIC_X_DATATRANSFER_FOLDEROPEN0001 + fr), 214, 104, false, NULL, 256);
  if (up) gfx_spr(pic(PIC_MG_UPLOAD_TOWER), MG_UPLOAD_TOWER_X, MG_UPLOAD_TOWER_Y, false, NULL, 256);
  if (M->phase == 1 && !M->done) {
    float f = fmodf(M->b, 1.2f) / 1.2f;
    gfx_pic(pic(PIC_X_DATATRANSFER_FILEFILL), 110 + f * 100, 92 - sinf(f * 3.14159f) * 30, 0.35f, 0.35f, 0, NULL, 256);
  }
  const char *src = up ? "My Tablet" : ROOM_NAMES[TASK_DEFS[t->def].room], *dst = up ? "Headquarters" : "My Tablet";
  gfx_text(FONT_SMALL, 98, 125, src, BLACK, 0, 0, 1);
  gfx_text(FONT_SMALL, 222, 125, dst, BLACK, 0, 0, 1);
  if (M->phase == 0) {
    gfx_spr(pic(up ? PIC_MG_UPLOAD_BUTTON : PIC_X_DATATRANSFER_DOWNLOADBUTTON), MG_UPLOAD_BUTTON_X, MG_UPLOAD_BUTTON_Y,
            false, NULL, 256);
  } else {
    float f = M->b / 8.5f;
    if (f > 1) f = 1;
    gfx_fill(83, 133, 154, 9, rgb565(0x1B1B1B), 256);
    gfx_fill(83, 133, (int)(154 * f), 9, rgb565(0x0A8200), 256);
    char b[40];
    snprintf(b, sizeof b, "%d%%", (int)(f * 100));
    gfx_text(FONT_SMALL, 244, 142, b, WHITE, 0, 0, 0);
    if (M->done) snprintf(b, sizeof b, "Complete");
    else snprintf(b, sizeof b, "Estimated Time: %ds", (int)ceilf(8.5f - M->b));
    gfx_text(FONT_SMALL, 84, 154, b, WHITE, 0, 0, 0);
  }
  mg_keys(M->phase == 0 ? (up ? "OK upload   Back close" : "OK download   Back close") : "wait for the transfer");
}

/* ---- Calibrate Distributor (SweepMinigame: stop each dial on its contact) ---- */

static void calib_tick(uint32_t pressed) {
  /* the dials turn (each faster), the current one stops when OK is pressed on its contact */
  for (int k = 0; k < 3; k++)
    if (M->state[k] == 0) M->v[k] = fmodf(M->v[k] + DT * (2.2f + 0.9f * k), 6.2831853f);
  int k = M->n;
  if (k < 3 && (pressed & K_OK)) {
    float a = M->v[k];
    if (a > 3.14159f) a -= 6.2831853f;
    if (fabsf(a) < 0.35f) {
      M->state[k] = 1, M->v[k] = 0, M->n++;
      if (M->n == 3) mg_finish();
    } else {
      /* a miss: all of them again */
      for (int j = 0; j < 3; j++) M->state[j] = 0;
      M->n = 0;
      M->c = 0.5f;
    }
  }
  if (M->c > 0) M->c -= DT;
}

static void calib_draw(void) {
  mg_bg(BLOB_MG_CALIBRATE);
  static const int16_t SP[3][2] = {{MG_CALIBRATE_SPIN1_X, MG_CALIBRATE_SPIN1_Y},
                                   {MG_CALIBRATE_SPIN2_X, MG_CALIBRATE_SPIN2_Y},
                                   {MG_CALIBRATE_SPIN3_X, MG_CALIBRATE_SPIN3_Y}};
  static const int16_t LIT[3][2] = {{MG_CALIBRATE_LIT1_X, MG_CALIBRATE_LIT1_Y},
                                    {MG_CALIBRATE_LIT2_X, MG_CALIBRATE_LIT2_Y},
                                    {MG_CALIBRATE_LIT3_X, MG_CALIBRATE_LIT3_Y}};
  static const uint32_t GAUGE[3] = {0xFFE300, 0x5261FF, 0x6EFAFF};
  for (int k = 0; k < 3; k++) {
    gfx_pic(pic(PIC_MG_CALIBRATE_SPIN1 + k), SP[k][0], SP[k][1], 1, 1, -M->v[k], NULL, 256);
    if (M->state[k]) {
      gfx_spr(pic(PIC_MG_CALIBRATE_LIT1 + k), LIT[k][0], LIT[k][1], false, NULL, 256);
      gfx_fill(194, (int)(44 + 59.3f * k) - 6, 52, 12, rgb565(GAUGE[k]), 256);
    }
    if (k == M->n && !M->done) gfx_rect(201, (int)(60 + 59.3f * k) - 10, 20, 20, 2, rgb565(0x00FF00));
  }
  if (M->c > 0) mg_status("Calibration failed", 214, 0xF800);
  mg_keys("OK when the dial lines up with its contact   Back close");
}

/* ---- Chart Course (CourseMinigame: along the dotted course) ---- */

static void course_open(void) {
  /* the start, three stars, the destination */
  M->v[0] = 58, M->v[1] = 100 + rnd_f() * 30;
  for (int k = 1; k < 4; k++) M->v[2 * k] = 58 + 49 * k, M->v[2 * k + 1] = 62 + rnd_f() * 96;
  M->v[6] = MG_COURSE_END_X, M->v[7] = MG_COURSE_END_Y;
  M->n = 0, M->a = 0;
}

static void course_tick(uint32_t keys) {
  if (!(keys & (K_RIGHT | K_OK))) return;
  float sp = 110 * DT;
  while (sp > 0 && M->n < 3) {
    float x0 = M->v[2 * M->n], y0 = M->v[2 * M->n + 1], x1 = M->v[2 * M->n + 2], y1 = M->v[2 * M->n + 3];
    float l = sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
    if (M->a + sp < l) {
      M->a += sp;
      sp = 0;
    } else {
      sp -= l - M->a;
      M->a = 0, M->n++;
    }
  }
  if (M->n >= 3) mg_finish();
}

static void course_draw(void) {
  mg_bg(BLOB_MG_COURSE);
  for (int k = 0; k < 3; k++) {
    float x0 = M->v[2 * k], y0 = M->v[2 * k + 1], x1 = M->v[2 * k + 2], y1 = M->v[2 * k + 3];
    float l = sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
    for (float d = 4; d < l - 4; d += 9) {
      float x = x0 + (x1 - x0) * d / l, y = y0 + (y1 - y0) * d / l;
      gfx_fill((int)x - 1, (int)y - 1, 3, 3, k < M->n ? rgb565(0xFFFFFF) : rgb565(0xB4D7FF), 256);
    }
  }
  gfx_spr(pic(PIC_X_NAV_CHARTCOURSE_START), (int)M->v[0], (int)M->v[1], false, NULL, 256);
  for (int k = 1; k < 4; k++) gfx_spr(pic(PIC_X_NAV_CHARTCOURSE_CHECKPT), (int)M->v[2 * k], (int)M->v[2 * k + 1], false, NULL, 256);
  gfx_spr(pic(PIC_MG_COURSE_END), (int)M->v[6], (int)M->v[7], false, NULL, 256);
  int k = M->n < 3 ? M->n : 2;
  float x0 = M->v[2 * k], y0 = M->v[2 * k + 1], x1 = M->v[2 * k + 2], y1 = M->v[2 * k + 3];
  float l = sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)), f = M->n < 3 ? M->a / l : 1;
  float ang = atan2f(y1 - y0, x1 - x0) + 1.5707963f;
  gfx_pic(pic(PIC_MG_COURSE_SHIP), x0 + (x1 - x0) * f, y0 + (y1 - y0) * f, 1, 1, ang, NULL, 256);
  mg_keys("hold right to fly the course   Back close");
}

/* ---- the first half's dispatch ---- */

static void open1(int kind) {
  switch (kind) {
  case MG_WIRES: wires_open(); break;
  case MG_ALIGN: align_open(); break;
  case MG_SAMPLE: sample_open(); break;
  case MG_SIMON: simon_open(); break;
  case MG_GARBAGE: garbage_open(); break;
  case MG_COURSE: course_open(); break;
  default: break;
  }
}

static void tick1(uint32_t keys, uint32_t pressed) {
  switch (M->kind) {
  case MG_CARD: card_tick(keys, pressed); break;
  case MG_WIRES: wires_tick(pressed); break;
  case MG_ASTEROIDS: ast_tick(keys, pressed); break;
  case MG_ALIGN: align_tick(keys); break;
  case MG_SCAN: scan_tick(); break;
  case MG_SAMPLE: sample_tick(pressed); break;
  case MG_FUEL: fuel_tick(keys); break;
  case MG_SIMON: simon_tick(pressed); break;
  case MG_GARBAGE: garbage_tick(keys); break;
  case MG_UPLOAD: upload_tick(pressed); break;
  case MG_CALIBRATE: calib_tick(pressed); break;
  case MG_COURSE: course_tick(keys); break;
  }
}

static void draw1(void) {
  switch (M->kind) {
  case MG_CARD: card_draw(); break;
  case MG_WIRES: wires_draw(); break;
  case MG_ASTEROIDS: ast_draw(); break;
  case MG_ALIGN: align_draw(); break;
  case MG_SCAN: scan_draw(); break;
  case MG_SAMPLE: sample_draw(); break;
  case MG_FUEL: fuel_draw(); break;
  case MG_SIMON: simon_draw(); break;
  case MG_GARBAGE: garbage_draw(); break;
  case MG_UPLOAD: upload_draw(); break;
  case MG_CALIBRATE: calib_draw(); break;
  case MG_COURSE: course_draw(); break;
  }
}
