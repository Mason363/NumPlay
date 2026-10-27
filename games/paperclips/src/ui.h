/* The screen (ui.c): the console at the top, the paperclip count, the tabs of the game's panels, the panel with its
 * buttons, the key line; and the pause menu, how to play and the questions over them. */
#ifndef PC_UI_H
#define PC_UI_H
#include <stdbool.h>
#include <stdint.h>
#include "pc.h"

enum { T_CLIPS, T_BUSINESS, T_WIRE, T_POWER, T_SPACE, T_PROBES, T_COMBAT, T_COMPUTER, T_PROJECTS, T_INVEST,
       T_STRATEGY, T_COUNT };
enum { O_NONE, O_GUIDE, O_PAUSE, O_HELP, O_QUIT, O_RESTART, O_NEW };   /* what's over the game */
enum { PAUSE_RESUME, PAUSE_HELP, PAUSE_QUIT, PAUSE_NEW };
#define PC_HELP_PAGES 4

typedef struct {
  int tab;
  int focus[T_COUNT];              /* the button in focus in each tab (pc.h's B_*), -1 for the first */
  int focus_at[T_COUNT];           /* and its place in the tab, for when it goes */
  int scroll[T_COUNT];
  int overlay, sel, page, under;
  uint8_t seen[PC_PROJECTS];       /* projects seen on the Projects tab */
  uint32_t clock;                  /* ms, for the blinks */
} pc_ui;

void ui_init(pc_ui *u);
void ui_up(pc_ui *u);
void ui_down(pc_ui *u);
void ui_tab(pc_ui *u, int d);      /* -1 or +1 */
int ui_focused(pc_ui *u);          /* the button in focus, or -1 */
bool ui_repeats(int b);            /* holding OK presses it again */
int ui_pause_count(void);
void ui_render(pc_ui *u);
bool ui_tab_shown(int t);
void ui_layout(pc_ui *u);          /* works out the tab's buttons without drawing (for the keys) */
int ui_buttons(pc_ui *u, int *out, int max);   /* the tab's buttons, in order */
#endif
