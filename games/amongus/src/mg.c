/* Among Us: the minigames (each task's and each fix's panel), the emergency button and the cameras. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "gfx.h"
#include "view.h"
#include "world.h"

void hud_keys(const char *s);

typedef struct {
  uint8_t task, kind, done, pad;
  float t, done_t;
} MgHead;
#define MG ((MgHead *)(void *)g.mg)

void mg_open(int cons, int task) {
  memset(g.mg, 0, sizeof g.mg);
  g.mg_cons = (uint8_t)cons;
  MG->task = (uint8_t)task;
  g.overlay = O_TASK;
}

bool mg_blocks_move(void) { return g.overlay != O_NONE; }

void mg_tick(uint32_t keys, uint32_t pressed) {
  MG->t += DT;
  if (g.overlay == O_EMERGENCY) {
    if (pressed & (K_OK | K_EXE)) {
      g.overlay = O_NONE;
      start_meeting(g.me, NONE);
    }
    if (pressed & K_BACK) g.overlay = O_NONE;
    return;
  }
  if (g.overlay == O_CAMS) {
    if (pressed & K_BACK) g.overlay = O_NONE;
    return;
  }
  if (pressed & K_BACK) {
    g.overlay = O_NONE;
    return;
  }
  (void)keys;
  if (pressed & K_OK) {
    if (MG->task < MAX_TASKS) task_step_done(g.me, MG->task);
    g.overlay = O_NONE;
  }
}

void mg_draw(void) {
  view_draw();
  gfx_fill(0, 0, SCREEN_W, SCREEN_H, 0x0000, 120);
  if (g.overlay == O_CAMS) {
    for (int k = 0; k < N_CAMS; k++) {
      int x = 8 + (k & 1) * 156, y = 8 + (k >> 1) * 108;
      view_camera((V2){CAMS[k].x, CAMS[k].y}, 104.0f / (2 * CAMS[k].size), x, y, 148, 104);
    }
    hud_keys("Back close");
    return;
  }
  gfx_fill(60, 40, 200, 150, rgb565(0x404040), 256);
  gfx_text(FONT_BIG, 160, 110, g.overlay == O_EMERGENCY ? "EMERGENCY" : "Task", 0xFFFF, 0, 0, 1);
  hud_keys("OK do it   Back close");
}
