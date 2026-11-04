/* Among Us: the minigames' second half: the O2 filter, the manifolds, steering, shields, power, and the sabotages'
 * fixes (the reactor's hands, the lights' switches, the comms' dial, the oxygen keypad), the cameras and the
 * emergency button. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "mg.h"
#include "view.h"
#include "world.h"

#define WHITE 0xFFFF
#define BLACK 0x0000

/* noise for the pictures only (the game's own random numbers stay the game's) */
static uint32_t noise_state = 0x1234567;
static uint32_t noise(void) {
  noise_state ^= noise_state << 13, noise_state ^= noise_state >> 17, noise_state ^= noise_state << 5;
  return noise_state;
}

/* ---- Clean O2 Filter (LeafMinigame: the leaves out through the left) ---- */

#define LEAF_X0 96
#define LEAF_X1 255
#define LEAF_EXIT 98

static void leaves_open(void) {
  M->n = 6;
  for (int k = 0; k < 6; k++) {
    Thing *a = &M->th[k];
    a->on = 1;
    a->kind = (uint8_t)rnd_n(7);
    a->x = 140 + rnd_f() * 105;
    a->y = 30 + rnd_f() * 165;
    a->a = rnd_f() * 6.28f;
  }
  M->a = 200, M->b = 112;   /* the hand */
  M->sel = NONE;
}

static void leaves_tick(uint32_t keys, uint32_t pressed) {
  float sp = 150 * DT;
  if (keys & K_LEFT) M->a -= sp;
  if (keys & K_RIGHT) M->a += sp;
  if (keys & K_UP) M->b -= sp;
  if (keys & K_DOWN) M->b += sp;
  if (M->a < 70) M->a = 70;
  if (M->a > LEAF_X1) M->a = LEAF_X1;
  if (M->b < 18) M->b = 18;
  if (M->b > 206) M->b = 206;
  /* OK held: carrying the leaf under the hand */
  if ((pressed & K_OK) && M->sel == NONE) {
    float bd = 22 * 22;
    for (int k = 0; k < 6; k++) {
      Thing *a = &M->th[k];
      float dx = a->x - M->a, dy = a->y - M->b;
      if (a->on == 1 && dx * dx + dy * dy < bd) bd = dx * dx + dy * dy, M->sel = (uint8_t)k;
    }
  }
  if (!(keys & K_OK)) M->sel = NONE;
  for (int k = 0; k < 6; k++) {
    Thing *a = &M->th[k];
    if (a->on == 2) {
      /* sucked out */
      a->x -= 300 * DT, a->a += DT * 8;
      if (a->x < 40) {
        a->on = 0;
        task_step_done(g.me, M->task);
        if (task_done(&g.p[g.me].tasks[M->task])) M->done = 1, M->done_t = 0;
      }
      continue;
    }
    if (!a->on) continue;
    if (M->sel == k) {
      a->x = M->a, a->y = M->b;
      if (a->x < LEAF_EXIT) a->on = 2, M->sel = NONE;
    } else {
      /* the air moves them about */
      a->a += DT * 0.6f;
      a->x += sinf(M->t * 1.3f + k) * 6 * DT;
      a->y += cosf(M->t * 1.1f + 2 * k) * 6 * DT;
      if (a->x < LEAF_EXIT + 12) a->x = LEAF_EXIT + 12;
    }
  }
}

static void leaves_draw(void) {
  mg_bg(BLOB_MG_LEAVES);
  for (int k = 0; k < 6; k++) {
    const Thing *a = &M->th[k];
    if (a->on) gfx_pic(pic(PIC_X_O2_LEAF1 + a->kind), a->x, a->y, 1, 1, a->a, NULL, 256);
  }
  /* the hand (the game's controller cursor: a green ring and its dot) */
  gfx_disc(M->a, M->b, 13, rgb565(0x00FF0D), M->sel == NONE ? 70 : 120);
  gfx_disc(M->a, M->b, 3, rgb565(0x00FF0D), 256);
  mg_keys(M->sel == NONE ? "arrows move   hold OK on a leaf to grab it" : "drag it out to the left");
}

/* ---- Unlock Manifolds (UnlockManifoldsMinigame: 1 to 10 in order) ---- */

static void manifolds_open(void) {
  for (int k = 0; k < 10; k++) M->order[k] = (uint8_t)k;
  for (int k = 9; k > 0; k--) {
    int j = rnd_n(k + 1);
    uint8_t t = M->order[k];
    M->order[k] = M->order[j], M->order[j] = t;
  }
}

static void manifolds_tick(uint32_t pressed) {
  if (M->c > 0) {
    M->c -= DT;
    if (M->c <= 0) M->n = 0;
    return;
  }
  if (pressed & K_LEFT) M->sel = (uint8_t)(M->sel % 5 ? M->sel - 1 : M->sel + 4);
  if (pressed & K_RIGHT) M->sel = (uint8_t)(M->sel % 5 == 4 ? M->sel - 4 : M->sel + 1);
  if (pressed & (K_UP | K_DOWN)) M->sel = (uint8_t)((M->sel + 5) % 10);
  if (pressed & K_OK) {
    if (M->order[M->sel] == M->n) {
      if (++M->n == 10) mg_finish();
    } else
      M->c = 0.6f;   /* wrong: they all go out */
  }
}

static void manifolds_draw(void) {
  mg_bg(BLOB_MG_MANIFOLDS);
  for (int k = 0; k < 10; k++) {
    int x = MG_X + (int)((-1.70f + 0.85f * (k % 5)) * PX_PER_UNIT), y = MG_Y - (int)((k < 5 ? 0.43f : -0.43f) * PX_PER_UNIT);
    const Pic *p = pic(PIC_X_REACTORBUTTON01 + M->order[k]);
    gfx_spr(p, x, y, false, NULL, 256);
    if (M->order[k] < M->n) gfx_spr_tint(p, x, y, rgb565(0x5BE0FF), 120);
    if (M->c > 0) gfx_spr_tint(p, x, y, 0xF800, 110);
    if (k == M->sel && !M->done) gfx_rect(x - 16, y - 16, 33, 33, 2, 0xFFE0);
  }
  mg_keys("arrows choose   OK press, from 1 to 10   Back close");
}

/* ---- Stabilize Steering (NavigationMinigame: the crosshair to the middle) ---- */

static void steering_open(void) {
  float a = rnd_f() * 6.28f;
  M->a = cosf(a) * 55, M->b = sinf(a) * 55;
}

static void steering_tick(uint32_t keys) {
  float sp = 70 * DT;
  if (keys & K_LEFT) M->a -= sp;
  if (keys & K_RIGHT) M->a += sp;
  if (keys & K_UP) M->b -= sp;
  if (keys & K_DOWN) M->b += sp;
  float d = sqrtf(M->a * M->a + M->b * M->b);
  if (d > 80) M->a *= 80 / d, M->b *= 80 / d;
  if (!(keys & (K_LEFT | K_RIGHT | K_UP | K_DOWN)) && d < 4) {
    M->a = M->b = 0;
    mg_finish();
  }
}

static void steering_draw(void) {
  mg_bg(BLOB_MG_STEERING);
  int x = MG_X + (int)M->a, y = MG_Y + (int)M->b;
  uint16_t c = M->done ? rgb565(0x37FF37) : rgb565(0x8DE8FF);
  /* the crosshair's two lines across the round screen */
  for (int yy = MG_Y - 88; yy <= MG_Y + 88; yy++) {
    float w = sqrtf(88 * 88 - (float)(yy - MG_Y) * (yy - MG_Y));
    if (yy == y) gfx_fill(MG_X - (int)w, yy, (int)(2 * w), 1, c, 256);
    if (fabsf((float)(x - MG_X)) < w) gfx_fill(x, yy, 1, 1, c, 256);
  }
  gfx_spr(pic(PIC_MG_STEERING_CROSS), x, y, false, NULL, 256);
  mg_keys("arrows move the crosshair to the middle   Back close");
}

/* ---- Prime Shields (ShieldMinigame: the red ones to white) ---- */

static void shields_open(void) {
  uint8_t s;
  do s = (uint8_t)(rnd() & 0x7F);
  while (s == 0x7F);
  M->n = s;   /* the hexes already on, as bits */
}

static void shields_tick(uint32_t pressed) {
  /* the arrows move to the nearest hex that way */
  int dx = (pressed & K_RIGHT) ? 1 : (pressed & K_LEFT) ? -1 : 0, dy = (pressed & K_DOWN) ? 1 : (pressed & K_UP) ? -1 : 0;
  if (dx || dy) {
    int best = -1;
    float bs = 1e9f;
    for (int k = 0; k < 7; k++) {
      if (k == M->sel) continue;
      float ex = MG_SHIELDS_HEX_XY[k][0] - MG_SHIELDS_HEX_XY[M->sel][0], ey = MG_SHIELDS_HEX_XY[k][1] - MG_SHIELDS_HEX_XY[M->sel][1];
      float along = ex * dx + ey * dy, side = fabsf(ex * dy - ey * dx);
      if (along <= 5) continue;
      float s = along + side * 2;
      if (s < bs) bs = s, best = k;
    }
    if (best >= 0) M->sel = (uint8_t)best;
  }
  for (int d = 1; d <= 7; d++)
    if (pressed & ((uint32_t)K_D0 << d)) M->sel = (uint8_t)(d - 1);
  if (pressed & K_OK) {
    M->n |= (uint8_t)(1 << M->sel);
    if (M->n == 0x7F) mg_finish();
  }
}

static void shields_draw(void) {
  mg_bg(BLOB_MG_SHIELDS);
  for (int k = 0; k < 7; k++) {
    int x = MG_SHIELDS_HEX_XY[k][0], y = MG_SHIELDS_HEX_XY[k][1];
    gfx_spr(pic(PIC_MG_SHIELDS_HEX), x, y, false, NULL, M->n >> k & 1 ? 120 : 256);
    if (!(M->n >> k & 1)) gfx_spr_tint(pic(PIC_MG_SHIELDS_HEX), x, y, 0xF800, 200);
    if (k == M->sel && !M->done) gfx_rect(x - 10, y - 10, 21, 21, 2, 0xFFE0);
  }
  mg_keys("arrows choose   OK prime a red one   Back close");
}

/* ---- Divert Power (DivertPowerMinigame: the target's slider up) and Accept Diverted Power ---- */

/* the sliders left to right: lower engine, upper engine, weapons, shields, navigation, communications, O2, security */
static const uint8_t DIVERT_ROOMS[8] = {R_LOWER_ENGINE, R_UPPER_ENGINE, R_WEAPONS, R_SHIELDS, R_NAV, R_COMMS, R_O2, R_SECURITY};
static const float DIVERT_X[8] = {-1.88f, -1.36f, -0.82f, -0.27f, 0.26f, 0.79f, 1.33f, 1.87f};

static int divert_target(void) {
  const Task *t = &g.p[g.me].tasks[M->task];
  for (int k = 0; k < 8; k++)
    if (DIVERT_ROOMS[k] == TASK_DEFS[t->def].target) return k;
  return 0;
}

static void divert_tick(uint32_t keys) {
  if (keys & K_UP) M->a += DT * 1.4f;
  if (keys & K_DOWN) M->a -= DT * 1.4f;
  if (M->a < 0) M->a = 0;
  if (M->a >= 1) M->a = 1, mg_finish();
}

static void divert_draw(void) {
  mg_bg(BLOB_MG_DIVERT);
  int tg = divert_target();
  for (int k = 0; k < 8; k++) {
    int x = MG_X + (int)(DIVERT_X[k] * PX_PER_UNIT);
    float f = k == tg ? M->a : 0;
    /* the tank fills with the power sent */
    if (f > 0) gfx_fill(x - 4, 114 - (int)(f * 32), 9, (int)(f * 32), rgb565(0xFCFF5C), 256);
    gfx_spr(pic(PIC_MG_DIVERT_SWITCH), x, MG_DIVERT_SWITCH_Y - (int)(f * 46), false, NULL, 256);
    if (k == tg && !M->done) gfx_rect(x - 12, MG_DIVERT_SWITCH_Y - (int)(f * 46) - 10, 24, 20, 2, 0xFFE0);
  }
  char b[48];
  snprintf(b, sizeof b, "Divert power to %s", ROOM_NAMES[DIVERT_ROOMS[tg]]);
  gfx_text(FONT_SMALL, MG_X, 214, b, WHITE, BLACK, 1, 1);
  mg_keys("hold up to push the lit slider   Back close");
}

static void accept_tick(uint32_t pressed) {
  if (pressed & K_OK) M->phase = 1, M->b = 0;
  if (M->phase) {
    M->b += DT;
    if (M->b > 0.3f) mg_finish();
  }
}

static void accept_draw(void) {
  mg_bg(BLOB_MG_ACCEPT);
  if (M->phase) {
    /* the power through */
    uint16_t c = rgb565(0xFCFF5C);
    for (int k = -2; k <= 2; k++) {
      gfx_line(52, (float)(MG_ACCEPT_SWITCH_Y + 12 * k), 150, MG_ACCEPT_SWITCH_Y, 2, c, 230);
      gfx_line(170, MG_ACCEPT_SWITCH_Y, 268, (float)(MG_ACCEPT_SWITCH_Y + 14 * k), 2, c, 230);
    }
  }
  gfx_pic(pic(PIC_MG_ACCEPT_SWITCH), MG_ACCEPT_SWITCH_X, MG_ACCEPT_SWITCH_Y, 1, 1, M->phase ? 0 : -0.7f, NULL, 256);
  mg_keys("OK flip the switch   Back close");
}

/* ---- Reset Reactor (ReactorMinigame: both hands at once) ---- */

static void hands_tick(uint32_t keys) {
  if (keys & K_OK) g.reactor_hold[CONSOLES[M->cons].id & 1] = g.me;
}

static void hands_draw(void) {
  bool held = g.reactor_hold[CONSOLES[M->cons].id & 1] == g.me;
  bool other = g.reactor_hold[(CONSOLES[M->cons].id & 1) ^ 1] != NONE;
  gfx_fill(91, 49, 132, 150, held ? rgb565(0x4FE7DA) : rgb565(0x4DE3D6), 256);
  if (held) {
    int y = 49 + (int)(fmodf(M->t * 90, 150));
    gfx_fill(91, y, 132, 4, WHITE, 180);
  }
  mg_bg(BLOB_MG_HANDS);
  const char *s = !held ? "Hold to stop meltdown" : other ? "Reactor stabilized" : "Waiting for second user";
  gfx_text(FONT_MID, MG_X, 37, s, BLACK, 0, 0, 1);
  mg_keys("hold OK on the hand   Back close");
}

/* ---- Fix Lights (SwitchMinigame: all five up) ---- */

static void lights_tick(uint32_t pressed) {
  if (pressed & K_LEFT) M->sel = (uint8_t)((M->sel + 4) % 5);
  if (pressed & K_RIGHT) M->sel = (uint8_t)((M->sel + 1) % 5);
  int flip = -1;
  if (pressed & K_OK) flip = M->sel;
  for (int d = 1; d <= 5; d++)
    if (pressed & ((uint32_t)K_D0 << d)) flip = d - 1, M->sel = (uint8_t)(d - 1);
  if (flip >= 0) g.switches ^= (uint8_t)(1 << flip);
  if (g.switches == 0x1F) mg_finish();
}

static void lights_draw(void) {
  mg_bg(BLOB_MG_LIGHTS);
  for (int k = 0; k < 5; k++) {
    bool on = g.switches >> k & 1;
    int x = MG_LIGHTS_SWITCH_XY[k][0], y = MG_LIGHTS_SWITCH_XY[k][1];
    gfx_pic(pic(PIC_MG_LIGHTS_SWITCH), x, y, 1, on ? 1 : -1, 0, NULL, 256);
    int lx = MG_LIGHTS_LIGHT_XY[k][0], ly = MG_LIGHTS_LIGHT_XY[k][1];
    gfx_spr(pic(PIC_MG_LIGHTS_LIGHT), lx, ly, false, NULL, 256);
    gfx_spr_tint(pic(PIC_MG_LIGHTS_LIGHT), lx, ly, on ? 0x07E0 : rgb565(0x1A4D1A), 190);
    if (k == M->sel) gfx_rect(x - 12, y - 16, 24, 32, 2, rgb565(0x2BFF00));
  }
  /* the graph: the light the switches let through */
  int on = 0;
  for (int k = 0; k < 5; k++) on += g.switches >> k & 1;
  gfx_fill(77, 74, (int)(166 * on / 5), 14, rgb565(0xFCFF5C), 256);
  mg_keys("left right choose   OK flip (or 1-5)   Back close");
}

/* ---- Fix Comms (TuneRadioMinigame: the dial onto the signal) ---- */

static void comms_open(void) {
  M->c = (rnd_f() * 2 - 1) * 1.6f;          /* the station, in radians of the dial */
  do M->a = (rnd_f() * 2 - 1) * 1.8f;
  while (fabsf(M->a - M->c) < 0.6f);
}

static void comms_tick(uint32_t keys) {
  if (keys & K_LEFT) M->a -= 1.4f * DT;
  if (keys & K_RIGHT) M->a += 1.4f * DT;
  if (M->a < -1.92f) M->a = -1.92f;   /* DialRange -110..110 degrees */
  if (M->a > 1.92f) M->a = 1.92f;
  if (fabsf(M->a - M->c) < 0.06f) {
    M->b += DT;
    if (M->b > 0.6f) fix_sabotage(), M->done = 1, M->done_t = 0;
  } else
    M->b = 0;
}

static void wave(int cx, int cy, float amount, uint16_t c) {
  int prevy = 0;
  for (int x = -36; x <= 36; x++) {
    float n = amount * (float)((int)((noise() >> 9) & 255) - 128) / 128.0f;
    int y = cy + (int)(sinf(x * 0.17f + M->t * 6) * 10 + n * 14);
    if (x > -36) gfx_line((float)(cx + x - 1), (float)prevy, (float)(cx + x), (float)y, 2, c, 256);
    prevy = y;
  }
}

static void comms_draw(void) {
  mg_bg(BLOB_MG_COMMS);
  float d = fabsf(M->a - M->c);
  wave(127, 108, 0, rgb565(0x1CFF1C));
  wave(222, 108, d > 1 ? 1 : d, rgb565(0x1CFF1C));
  gfx_spr(pic(PIC_MG_COMMS_GLASS), MG_COMMS_GLASS_X, MG_COMMS_GLASS_Y, false, NULL, 256);
  gfx_spr(pic(PIC_MG_COMMS_GLASS), MG_COMMS_GLASS_X + 95, MG_COMMS_GLASS_Y, false, NULL, 256);
  gfx_pic(pic(PIC_MG_COMMS_DIALSHADOW), MG_COMMS_DIALSHADOW_X, MG_COMMS_DIALSHADOW_Y, 1, 1, M->a, NULL, 256);
  gfx_pic(pic(PIC_MG_COMMS_DIAL), MG_COMMS_DIAL_X, MG_COMMS_DIAL_Y, 1, 1, M->a, NULL, 256);
  /* the lights: red, green when tuned */
  bool ok = d < 0.06f || M->done;
  gfx_disc(247, 140, 4, ok ? rgb565(0x400000) : 0xF800, 256);
  gfx_disc(259, 141, 4, ok ? 0x07E0 : rgb565(0x003000), 256);
  mg_keys("left right turn the dial onto the signal   Back close");
}

/* ---- Restore Oxygen (KeypadGame: the code on the note) ---- */

static void keypad_tick(uint32_t pressed) {
  if (M->c > 0) {
    M->c -= DT;
    if (M->c <= 0) M->n = 0;
    return;
  }
  for (int d = 0; d <= 9; d++)
    if ((pressed & ((uint32_t)K_D0 << d)) && M->n < 5) M->order[M->n++] = (uint8_t)d;
  if ((pressed & (K_OK | K_EXE)) && M->n) {
    uint32_t v = 0;
    for (int k = 0; k < M->n; k++) v = v * 10 + M->order[k];
    if (M->n == 5 && v == g.o2_code) {
      g.o2_done |= (uint8_t)(CONSOLES[M->cons].room == R_ADMIN ? 1 : 2);
      M->phase = 1;
      M->done = 1, M->done_t = 0;
    } else
      M->c = 0.8f;   /* a bad code: the display flashes and clears */
  }
}

static void keypad_draw(void) {
  mg_bg(BLOB_MG_KEYPAD);
  gfx_spr(pic(PIC_MG_KEYPAD_X), MG_KEYPAD_X_X, MG_KEYPAD_X_Y, false, NULL, 256);
  gfx_spr(pic(PIC_MG_KEYPAD_CHECK), MG_KEYPAD_CHECK_X, MG_KEYPAD_CHECK_Y, false, NULL, 256);
  /* the sticky note with today's code */
  gfx_spr(pic(PIC_MG_KEYPAD_NOTE), MG_KEYPAD_NOTE_X, MG_KEYPAD_NOTE_Y, false, NULL, 256);
  char b[12];
  snprintf(b, sizeof b, "%05u", (unsigned)g.o2_code);
  gfx_text(FONT_BIG, MG_KEYPAD_NOTE_X - 2, MG_KEYPAD_NOTE_Y + 6, b, BLACK, 0, 0, 1);
  /* the display */
  for (int k = 0; k < M->n; k++) b[k] = (char)('0' + M->order[k]);
  b[M->n] = 0;
  if (M->c > 0 && ((int)(M->c * 8) & 1)) snprintf(b, sizeof b, "BAD");
  if (M->phase) snprintf(b, sizeof b, "OK");
  gfx_text(FONT_BIG, 132, 35, b, M->phase ? 0x07E0 : M->c > 0 ? 0xF800 : 0x07E0, 0, 0, 0);
  mg_keys("type the code with the digits   OK enter   Back close");
}

/* ---- the cameras (SurveillanceMinigame: four views) ---- */

static void cams_draw(void) {
  mg_bg(BLOB_MG_CAMS);
  /* the four views on the screen (x 71-248, y 42-128) */
  for (int k = 0; k < 4; k++) {
    int x = 71 + (k & 1) * 90, y = 42 + (k >> 1) * 44, w = 88, h = 42;
    if (y + h <= gfx_y0 || y >= gfx_y0 + BAND) continue;
    if (g.sab == SAB_COMMS) {
      /* static */
      for (int yy = y; yy < y + h; yy++)
        if (yy >= gfx_y0 && yy < gfx_y0 + BAND)
          for (int xx = x; xx < x + w; xx++) {
            uint32_t r = noise();
            gfx_band[(yy - gfx_y0) * SCREEN_W + xx] = (r & 0x100) ? 0x8C71 : 0x2104;
          }
      if (fmodf(M->t, 2) < 1) gfx_text(FONT_SMALL, x + w / 2, y + h / 2 + 4, "COMMS SABOTAGED", WHITE, BLACK, 1, 1);
      continue;
    }
    view_camera((V2){CAMS[k].x, CAMS[k].y}, (float)h / (2 * CAMS[k].size), x, y, w, h);
  }
  mg_keys("Back close");
}

/* ---- the emergency button (EmergencyMinigame) ---- */

static void emergency_tick(uint32_t pressed) {
  const Player *P = &g.p[g.me];
  M->b += DT;
  bool can = P->alive && P->emergencies > 0 && g.emergency_cd <= 0 && g.sab != SAB_REACTOR && g.sab != SAB_O2;
  if (can && (pressed & K_OK) && M->b > 0.3f) {
    g.overlay = O_NONE;
    start_meeting(g.me, NONE);
  }
}

static void emergency_draw(void) {
  mg_bg(BLOB_MG_EMERGENCY);
  const Player *P = &g.p[g.me];
  gfx_spr(pic(PIC_MG_EMERGENCY_BUTTON), MG_EMERGENCY_BUTTON_X, MG_EMERGENCY_BUTTON_Y, false, NULL, 256);
  /* the lid lifts as the panel opens */
  if (M->b < 0.3f) gfx_spr(pic(PIC_MG_EMERGENCY_CLOSED), MG_EMERGENCY_CLOSED_X, MG_EMERGENCY_CLOSED_Y, false, NULL, 256);
  else gfx_spr(pic(PIC_MG_EMERGENCY_OPEN), MG_EMERGENCY_OPEN_X, MG_EMERGENCY_OPEN_Y, false, NULL, 256);
  gfx_spr(pic(PIC_MG_EMERGENCY_BUBBLE), MG_EMERGENCY_BUBBLE_X, MG_EMERGENCY_BUBBLE_Y, false, NULL, 256);
  char a[64], b[48];
  b[0] = 0;
  if (g.sab == SAB_REACTOR || g.sab == SAB_O2) snprintf(a, sizeof a, "EMERGENCY MEETINGS CANNOT");
  else snprintf(a, sizeof a, "CREWMEMBER %s HAS", COLOR_NAMES[P->color]);
  for (char *c = a; *c; c++)
    if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 32);
  if (g.sab == SAB_REACTOR || g.sab == SAB_O2) snprintf(b, sizeof b, "BE CALLED DURING CRISES");
  else if (g.emergency_cd > 0) snprintf(b, sizeof b, "EMERGENCY COOLDOWN: %ds", (int)ceilf(g.emergency_cd));
  else snprintf(b, sizeof b, "%d EMERGENCY MEETINGS LEFT", P->alive ? P->emergencies : 0);
  gfx_text(FONT_SMALL, MG_X, 172, a, BLACK, 0, 0, 1);
  gfx_text(FONT_SMALL, MG_X, 186, b, BLACK, 0, 0, 1);
  mg_keys("OK press the button   Back close");
}

/* ---- the second half's dispatch ---- */

void mg2_open(int kind) {
  switch (kind) {
  case MG_LEAVES: leaves_open(); break;
  case MG_MANIFOLDS: manifolds_open(); break;
  case MG_STEERING: steering_open(); break;
  case MG_SHIELDS: shields_open(); break;
  case MG_COMMS: comms_open(); break;
  default: break;
  }
}

void mg2_tick(uint32_t keys, uint32_t pressed) {
  switch (M->kind) {
  case MG_LEAVES: leaves_tick(keys, pressed); break;
  case MG_MANIFOLDS: manifolds_tick(pressed); break;
  case MG_STEERING: steering_tick(keys); break;
  case MG_SHIELDS: shields_tick(pressed); break;
  case MG_DIVERT: divert_tick(keys); break;
  case MG_ACCEPT: accept_tick(pressed); break;
  case MG_HANDS: hands_tick(keys); break;
  case MG_LIGHTS: lights_tick(pressed); break;
  case MG_COMMS: comms_tick(keys); break;
  case MG_KEYPAD: keypad_tick(pressed); break;
  case MG_EMERGENCY: emergency_tick(pressed); break;
  default: break;
  }
}

void mg2_draw(void) {
  switch (M->kind) {
  case MG_LEAVES: leaves_draw(); break;
  case MG_MANIFOLDS: manifolds_draw(); break;
  case MG_STEERING: steering_draw(); break;
  case MG_SHIELDS: shields_draw(); break;
  case MG_DIVERT: divert_draw(); break;
  case MG_ACCEPT: accept_draw(); break;
  case MG_HANDS: hands_draw(); break;
  case MG_LIGHTS: lights_draw(); break;
  case MG_COMMS: comms_draw(); break;
  case MG_KEYPAD: keypad_draw(); break;
  case MG_CAMS: cams_draw(); break;
  case MG_EMERGENCY: emergency_draw(); break;
  default: break;
  }
}
