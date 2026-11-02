/* The screens that are the calculator's, not the original's: the keys the first time, Continue or
 * New game on the title when a game was left, and the pause menu (Back pauses as Start did, with
 * Resume and Quit game, which asks first; quitting keeps the game, as Home does). */
#include "s1.h"
#include "game.h"
#include "ui.h"

void keys_screen(void) {
  ui_clear();
  ui_box(0, 0, 320, 224, UI_BLUE);
  ui_text_centred(10, UI_YELLOW, 2, "SONIC THE HEDGEHOG");
  ui_text_centred(34, UI_WHITE, 1, "HOW TO PLAY");
  ui_text(16, 54, UI_YELLOW, 1, "ARROWS");
  ui_text(136, 54, UI_WHITE, 1, "RUN, LOOK UP, DUCK");
  ui_text(16, 66, UI_YELLOW, 1, "DOWN, RUNNING");
  ui_text(136, 66, UI_WHITE, 1, "ROLL");
  ui_text(16, 78, UI_YELLOW, 1, "OK, EXE, 5, 0");
  ui_text(136, 78, UI_WHITE, 1, "JUMP");
  ui_text(16, 90, UI_YELLOW, 1, "BACK");
  ui_text(136, 90, UI_WHITE, 1, "START, PAUSE");
  ui_text(16, 102, UI_YELLOW, 1, "HOME");
  ui_text(136, 102, UI_WHITE, 1, "SAVE AND QUIT");
  ui_text(16, 124, UI_WHITE, 1, "COLLECT RINGS: HIT WITH RINGS, YOU");
  ui_text(16, 136, UI_WHITE, 1, "ONLY LOSE THEM. 100 RINGS: 1 UP.");
  ui_text(16, 148, UI_WHITE, 1, "END AN ACT WITH 50 RINGS AND JUMP");
  ui_text(16, 160, UI_WHITE, 1, "INTO THE GIANT RING: SPECIAL STAGE.");
  ui_text(16, 172, UI_WHITE, 1, "FIND THE 6 CHAOS EMERALDS!");
  ui_text_centred(198, UI_YELLOW, 1, "PRESS OK");
  vdp_display = false;
  do {
    V.vblank_routine = id_VBlank_Sega;
    WaitForVBlank();
  } while (!(V.jpadpress1 & (btnABC | btnStart)));
  ui_clear();
  save_set_seen_keys();
}

static const char *const zone_names[7] = {"GREEN HILL", "LABYRINTH", "MARBLE", "STAR LIGHT",
                                          "SPRING YARD", "SCRAP BRAIN", "FINAL"};

/* the title's menu, drawn; sel: the line chosen */
static char cont_line[40];
void continue_menu_draw(int sel) {
  int z, a, l;
  save_peek(&z, &a, &l);
  if (z > 6) z = 6;
  /* (Scrap Brain's third act is in Labyrinth Zone) */
  const char *name = z == 1 && a == 3 ? "SCRAP BRAIN" : zone_names[z];
  int act = z == 1 && a == 3 ? 3 : a + 1;
  char *p = cont_line;
  for (const char *s = name; *s;) *p++ = *s++;
  for (const char *s = " ZONE"; *s;) *p++ = *s++;
  if (z != 6) {
    for (const char *s = " ACT "; *s;) *p++ = *s++;
    *p++ = (char)('0' + act);
  }
  *p = 0;
  ui_clear();
  ui_box(40, 150, 240, 64, UI_DARK);
  ui_text(56, 158, sel == 0 ? UI_YELLOW : UI_WHITE, 1, sel == 0 ? "> CONTINUE" : "  CONTINUE");
  ui_text(72, 170, sel == 0 ? UI_YELLOW : UI_WHITE, 1, cont_line);
  ui_text(56, 190, sel == 1 ? UI_YELLOW : UI_WHITE, 1, sel == 1 ? "> NEW GAME" : "  NEW GAME");
}

/* ------------------------------------------------------------------ pause */
static void pause_draw(int sel, bool confirm, int csel) {
  ui_clear();
  if (!confirm) {
    ui_box(96, 72, 128, 76, UI_DARK);
    ui_text_centred(82, UI_WHITE, 2, "PAUSE");
    ui_text(120, 110, sel == 0 ? UI_YELLOW : UI_WHITE, 1, sel == 0 ? "> RESUME" : "  RESUME");
    ui_text(120, 126, sel == 1 ? UI_YELLOW : UI_WHITE, 1, sel == 1 ? "> QUIT GAME" : "  QUIT GAME");
  } else {
    ui_box(64, 80, 192, 60, UI_DARK);
    ui_text_centred(92, UI_WHITE, 1, "QUIT THE GAME?");
    ui_text(104, 120, csel == 0 ? UI_YELLOW : UI_WHITE, 1, csel == 0 ? "> YES" : "  YES");
    ui_text(176, 120, csel == 1 ? UI_YELLOW : UI_WHITE, 1, csel == 1 ? "> NO" : "  NO");
  }
}

void PauseGame(void) {
  if (!V.lives) {
    V.f_pause = 0;
    return;
  }
  if (!V.f_pause && !(V.jpadpress1 & btnStart)) return;
  V.f_pause = 1;
  int sel = 0, csel = 1;
  bool confirm = false;
  for (;;) {
    pause_draw(sel, confirm, csel);
    V.vblank_routine = id_VBlank_Paused;
    WaitForVBlank();
    uint8_t p = V.jpadpress1;
    if (!confirm) {
      if (p & btnStart) break;
      if (p & (btnUp | btnDn)) sel ^= 1;
      if (p & btnABC) {
        if (sel == 0) break;
        confirm = true, csel = 1;
      }
    } else {
      if (p & btnStart) confirm = false;
      if (p & (btnL | btnR | btnUp | btnDn)) csel ^= 1;
      if (p & btnABC) {
        if (csel == 0) {
          ui_clear();
          game_quit();
        }
        confirm = false;
      }
    }
  }
  ui_clear();
  V.f_pause = 0;
}

void pause_overlay_draw(void) {}
