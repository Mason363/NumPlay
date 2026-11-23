/* The Final Earth 2: the story's windows and overlays (progress.Story's
 * showGoalText, gui.CurrentMissionsWindow with progress.GoalHelp,
 * gui.HelpArrow and CityGUI's tutorial arrows, gui.TopScreenNotification,
 * the end of a scenario) and the story's hooks into the interface. */
#include <math.h>
#include "game.h"
#include "uii.h"

uint8_t tut_flags;
int16_t tut_close_warning;
bool tut_fix_builder;           /* City.postCreateBuilder: the first builder is fixed in place */
static uint16_t tutorial;       /* the goal's showTutorial */
static float arrow_time;        /* HelpArrow.arrowTime */
/* useBigMovement: the first build arrow and the workers' arrow come in on a quarter circle round the screen's
 * centre, fading in (their angle, pi/2 to 0; they turn as well in the original) */
static float circle1, circle2;
static const char *pending_message;   /* a story message waiting for no window to be open */
/* the top-screen notification */
static const char *note_text, *note_title;
static int16_t note_time;
static float note_fade;
static char note_buf[160];

void story_ui_reset(void) {
  tut_flags = 0;
  tut_close_warning = 0;
  tut_fix_builder = false;
  tutorial = 0;
  arrow_time = 0;
  pending_message = NULL;
  note_text = note_title = NULL;
  note_time = 0;
}

/* ---------------------------------------------------------------- goal texts */
/* Story.showGoalText: the inline commands ([n] not done, [c] done, [nonmobi] the computer version's words, [mobi]
 * the mobile version's, [/] all again, [d] the time taken); others are dropped, as the original does. The
 * computer version's words are kept (its notes read well on a keypad), then "click on" and "tap on" become the
 * keypad's "press OK on" */
const char *goal_text(int gi, char *buf, int size) {
  const StoryGoal *g = &stories[story.index].goals[gi];
  bool complete = true;
  if (gi == story.goal)
    for (int i = 0; i < g->nsub; i++) complete &= story_subgoal_complete(i);
  const char *s = g->text ? g->text : "";
  char raw[1100];
  int o = 0, rsize = (int)sizeof raw < size ? (int)sizeof raw : size;
  bool adding = true;
  while (*s && o < rsize - 1) {
    const char *e = *s == '[' ? strchr(s, ']') : NULL;
    if (!e) {
      if (adding) raw[o++] = *s;
      s++;
      continue;
    }
    int n = (int)(e - s - 1);
    const char *c = s + 1;
#define IS(x) (n == (int)strlen(x) && !strncmp(c, x, (size_t)n))
    if (IS("n")) adding = !complete;
    else if (IS("c")) adding = complete;
    else if (IS("mobi")) adding = false;
    else if (IS("nonmobi")) adding = true;
    else if (IS("nmobi")) adding = false;
    else if (IS("nnonmobi")) adding = !complete;
    else if (IS("/")) adding = true;
    else if (IS("d")) {
      /* the time taken: days, hours and minutes */
      char t[48];
      int ts = (int)city.timeSinceStart, d = 1 + ts / 1440, h = ts / 60 % 24, m = ts % 60;
      raw[o] = 0;
      str_cat(raw, rsize, d == 1 ? L_one_day : lo_1(t, sizeof t, L_n_days, LN(d)));
      str_cat(raw, rsize, ", ");
      str_cat(raw, rsize, h == 1 ? L_one_hour : lo_1(t, sizeof t, L_n_hours, LN(h)));
      str_cat(raw, rsize, ", ");
      str_cat(raw, rsize, L_and);
      str_cat(raw, rsize, " ");
      str_cat(raw, rsize, m == 1 ? L_one_minute : lo_1(t, sizeof t, L_n_minutes, LN(m)));
      o = (int)strlen(raw);
    }
#undef IS
    s = e + 1;
  }
  raw[o] = 0;
  /* the mouse's and the touch screen's words: the keypad's */
  static const char *const from[] = {"click on", "Click on", "tap on", "Tap on"};
  static const char *const to[] = {"press OK on", "Press OK on", "press OK on", "Press OK on"};
  o = 0;
  for (const char *r = raw; *r && o < size - 1;) {
    int k = 0;
    while (k < 4 && strncmp(r, from[k], strlen(from[k]))) k++;
    if (k < 4) {
      buf[o] = 0;
      str_cat(buf, size, to[k]);
      o = (int)strlen(buf);
      r += strlen(from[k]);
    } else {
      buf[o++] = *r++;
    }
  }
  buf[o] = 0;
  return buf;
}

enum { GW_NEXT = 1, GW_NEW_CITY };

void story_goal_content(WinOut *w) {
  int gi = w->win->arg;
  const StoryGoal *g = &stories[story.index].goals[gi];
  if (g->title && *g->title) win_title(w, g->title);
  char buf[1100];
  win_para(w, FONT_STANDARD, goal_text(gi, buf, sizeof buf), 250, C_BLACK);
  if (!strcmp(g->name, "Win")) {
    const char *label;
    int a;
    if (g->nextStory >= 0 && story_requirements(g->nextStory)) label = L_next_scenario, a = GW_NEXT;
    else label = L_new_city, a = GW_NEW_CITY;
    win_bottom(w, &label, &a, 1, L_close, ACT_CLOSE);
  } else {
    win_bottom(w, NULL, NULL, 0, L_close, ACT_CLOSE);
  }
}

void story_goal_action(Win *wn, int a) {
  const StoryGoal *g = &stories[story.index].goals[wn->arg];
  if (a == GW_NEXT) {
    win_close_all();
    menu_new_city(g->nextStory);
  } else if (a == GW_NEW_CITY) {
    win_close_all();
    win_open(WK_SCENARIOS, 0, 0);
  }
}

/* ---------------------------------------------------------------- the quick mission (progress.GoalHelp) */
/* the current tasks: the goal's quick text, then the buildings' missions */
int goal_help(char *buf, int size, const char **header) {
  int n = 0;
  buf[0] = 0;
  const StoryGoal *g = story_goal();
  const char *one_cat = NULL, *one_text = NULL;
  if (g && g->quickText && *g->quickText) {
    str_cat(buf, size, "- ");
    str_cat(buf, size, L_main_goal);
    str_cat(buf, size, "\n");
    str_cat(buf, size, g->quickText);
    one_cat = L_main_goal, one_text = g->quickText;
    n++;
  }
  for (int i = 1; i < MAX_PERMS; i++) {
    Perm *p = &perms[i];
    if (!(p->flags & PF_USED) || !is_building(p)) continue;
    const char *cat, *text;
    if (!bld_global_goal(p, &cat, &text)) continue;
    if (n) str_cat(buf, size, "\n\n");
    str_cat(buf, size, "- ");
    str_cat(buf, size, cat);
    str_cat(buf, size, "\n");
    str_cat(buf, size, text);
    one_cat = cat, one_text = text;
    n++;
  }
  if (n == 1) {
    /* one task: its text, with "Current Task" (or its category) as the header */
    buf[0] = 0;
    str_cat(buf, size, one_text);
    if (header) *header = one_cat == L_main_goal ? L_current_task : one_cat;
  } else if (header) {
    *header = L_current_tasks;
  }
  return n;
}

bool story_has_quick_text(void) {
  char buf[8];
  (void)buf;
  const StoryGoal *g = story_goal();
  if (g && g->quickText && *g->quickText) return true;
  for (int i = 1; i < MAX_PERMS; i++) {
    const char *cat, *text;
    if ((perms[i].flags & PF_USED) && is_building(&perms[i]) && bld_global_goal(&perms[i], &cat, &text)) return true;
  }
  return false;
}

void story_missions_content(WinOut *w) {
  char buf[800];
  const char *header = NULL;
  int n = goal_help(buf, sizeof buf, &header);
  win_title(w, n == 1 ? L_current_task : L_current_tasks);
  if (n == 1) {
    /* CurrentMissionsWindow always lists them as "- category\ntext" */
    const StoryGoal *g = story_goal();
    char b2[800] = "- ";
    str_cat(b2, sizeof b2, header == L_current_task ? L_main_goal : header);
    str_cat(b2, sizeof b2, "\n");
    str_cat(b2, sizeof b2, buf);
    (void)g;
    win_para(w, FONT_STANDARD, b2, 250, C_BLACK);
  } else {
    win_para(w, FONT_STANDARD, n ? buf : "---", 250, C_BLACK);
  }
  win_bottom(w, NULL, NULL, 0, L_close, ACT_CLOSE);
}

/* ---------------------------------------------------------------- the story's hooks */
void ui_show_goal_text(int goal) {
  if (ui.title && !ui.setup) return;   /* a free play's welcome waits under its options window */
  win_close_all();
  win_open(WK_GOAL, (uint16_t)goal, 0);
  win_pause();
}

void ui_show_tutorial(int t) {
  if (ui.title) return;
  tutorial = (uint16_t)t;
  arrow_time = 0;
  circle1 = circle2 = (float)M_PI / 2;
  tut_flags = 0;
  tut_fix_builder = t == 1;
  if (t == 2 || t == 101) tut_close_warning = 120;
}

void ui_show_simple(const char *text) {
  win_close_all();
  win_open_text(NULL, text);
}

void ui_show_message(const char *text) {
  if (ui.title) return;
  pending_message = text;
}

bool ui_story_window_open(void) {
  for (int i = 0; i < nwins; i++)
    if (wins[i].kind == WK_GOAL) return true;
  return false;
}

/* GUI.notify: a TopScreenNotification (time in the original's frames) */
void notify_top(const char *text, const char *title, int time) {
  note_text = text;
  note_title = title;
  note_time = (int16_t)time;
  note_fade = 1;
}

/* Happiness: the very unhappy citizens' warning (showSimpleWindow + pauseForWindow) */
void ui_pause_for_window(void) {
  if (ui.title) return;
  const char *a = story_is("cityofthekey") ? L_citizens_unhappy_key : city.materials.m[M_FOOD] == 0 ? L_citizens_unhappy_food
                : L_citizens_unhappy;
  ui_text_buf[0] = 0;
  str_cat(str_cat(str_cat(ui_text_buf, sizeof ui_text_buf, a), sizeof ui_text_buf, " "), sizeof ui_text_buf, L_more_happiness_info);
  win_close_all();
  win_open_text(L_citizens_unhappy_title, ui_text_buf);
  win_pause();
}

void story_ui_tick(void) {
  arrow_time += TIMEMOD;
  if (pending_message && !nwins) {
    win_open_text(NULL, pending_message);
    pending_message = NULL;
  }
  if (note_text || note_title) {
    if (note_time > 0) note_time = (int16_t)(note_time - (int)TIMEMOD);
    else if ((note_fade -= 0.05f * TIMEMOD) < 0) note_text = note_title = NULL;
  }
  if (tut_close_warning > 0) tut_close_warning = (int16_t)(tut_close_warning - (int)TIMEMOD);
  if (!nwins) tut_flags |= TF_WINDOW_CLOSED;
}

/* ---------------------------------------------------------------- HelpArrow */
enum { HA_DOWN, HA_LEFT, HA_DOWN_HINT };
/* an arrow at r (x, y, w, h), and maybe a text in a green box */
static void help_arrow_ex(const int16_t r[4], int dir, const char *text, float *circle) {
  float wob = sinf(arrow_time / 10) * 2;
  int cx = r[0] + r[2] / 2, cy = r[1] + r[3] / 2;
  int ax = 0, ay = 0, aw = 0, ah = 0;
  if (dir == HA_DOWN) {
    ax = cx - 10, ay = r[1] - 2 - (int)wob - 30, aw = 20, ah = 30;
    uint8_t alpha = 255;
    if (circle && *circle > 0) {
      float c = *circle, px = (float)cx, py = (float)(ay + 30);
      *circle -= (0.015f + 0.04f * (c / ((float)M_PI / 2))) * TIMEMOD;
      px = cosf(c) * (px - SCREEN_W / 2) + SCREEN_W / 2;
      py = sinf(c) * (SCREEN_H / 2 - py) + py;
      ax = (int)px - 10, ay = (int)py - 30;
      alpha = (uint8_t)(255 * fmaxf(0, cosf(c)));
    }
    ui_spr_ex(SPR_TUTORIALARROWDOWN, 0, 0, spr_w(SPR_TUTORIALARROWDOWN), spr_h(SPR_TUTORIALARROWDOWN), ax, ay, 0, alpha,
              GFX_NO_TINT);
  } else if (dir == HA_LEFT) {
    ax = r[0] + r[2] + 2 + (int)wob, ay = cy - 10, aw = 30, ah = 20;
    ui_spr(SPR_TUTORIALARROWLEFT, ax, ay);
  } else {
    ax = cx - 5, ay = r[1] - 2 - 6, aw = 10, ah = 6;
    ui_spr(SPR_HINTINFO_ARROW, ax, ay);
  }
  if (!text) return;
  /* HelpArrow.addText: Arial, 133 wide (200 for long ones), in spr_9p_hintinfo */
  int tw, maxw = strlen(text) < 45 ? 133 : 200;
  int th = ui_para(FONT_STANDARD, 0, INT16_MIN, maxw, text, C_BLACK, true, &tw);
  int bw = tw + 6, bh = th + 6, bx, by;
  if (dir == HA_DOWN) {
    bx = cx > bw + 5 + 10 ? cx - 10 - 2 - bw : cx + 10 + 2;
    by = r[1] - 2 - bh;
  } else if (dir == HA_LEFT) {
    bx = r[0] + r[2] + bw > SCREEN_W ? SCREEN_W - bw - 5 : r[0] + r[2];
    by = cy - 2 - 10 - bh;
  } else {
    if (cx > bw + 5 + 5) bx = cx - bw + 5;
    else if (cx - 10 + bw < SCREEN_W) bx = cx - 5;
    else bx = 5;
    by = r[1] - 2 - 6 - bh;
  }
  if (bx + bw > SCREEN_W) bx = SCREEN_W - bw;
  if (bx < 0) bx = 0;
  if (by < 0) by = 0;
  /* the small screen: a hint for something beside an open window goes beside it, narrower, not over it */
  int16_t wr[4];
  if (dir == HA_DOWN_HINT && win_screen_rect(wr) && bx < wr[0] + wr[2] && bx + bw > wr[0] && by < wr[1] + wr[3] &&
      by + bh > wr[1] && SCREEN_W - (wr[0] + wr[2]) - 2 >= 50) {
    maxw = SCREEN_W - (wr[0] + wr[2]) - 2 - 6;
    th = ui_para(FONT_STANDARD, 0, INT16_MIN, maxw, text, C_BLACK, true, &tw);
    bw = tw + 6, bh = th + 6;
    bx = SCREEN_W - bw, by = r[1] - 2 - 6 - bh;
    if (by < 0) by = 0;
  }
  ui_box(bx, by, bw, bh, BX_HINTINFO);
  ui_para(FONT_STANDARD, bx + 3, by + 2, maxw, text, C_BLACK, true, NULL);
  (void)ax, (void)ay, (void)aw, (void)ah;
}
static void help_arrow(const int16_t r[4], int dir, const char *text) { help_arrow_ex(r, dir, text, NULL); }

static bool window_closed(void) { return (tut_flags & TF_WINDOW_CLOSED) && !nwins; }

/* the button a window shows for an action, on screen */

static void tutorial_draw(void) {
  int16_t r[4];
  bool none_shown = cu.category < 0;
  bool builder = cu.build_kind != BK_NONE;
  /* showWindowCloseWarning: an arrow at the goal window's Close button */
  if ((tutorial == 1 && arrow_time >= 180) || (tutorial == 2 || tutorial == 101)) {
    if (win_is_open(WK_GOAL) && (tutorial == 1 || tut_close_warning <= 0) && win_action_rect(ACT_CLOSE, r)) help_arrow(r, HA_LEFT, NULL);
  }
  if (nwins && !win_is_open(WK_WORKERS)) return;
  switch (tutorial) {
  case 1:
    if (window_closed() && !builder) {
      /* the keypad's own hint: which key leads to the build menu */
      if (none_shown && city_build_rect(BR_FIRST_CATEGORY, r))
        help_arrow_ex(r, HA_DOWN, cu.zone == Z_BUILD ? NULL : L_keypad_toolbox_hint, &circle1);
      else if (!none_shown && city_build_rect(BR_LAST_BUILDING, r)) help_arrow(r, HA_LEFT, NULL);
    }
    break;
  case 101:
    if (window_closed() && city.materials.m[M_WOOD] < 4 && city_panel_rect(PR_MAT0 + M_WOOD, r)) help_arrow(r, HA_DOWN_HINT, L_see_wood_here);
    if (city.materials.m[M_WOOD] >= 4 && !builder) {
      if (none_shown && city_build_rect(BR_FIRST_CATEGORY, r)) help_arrow(r, HA_DOWN, L_you_have_enough_wood);
      else if (!none_shown && city_build_rect(BR_SECOND_LAST_BUILDING, r)) help_arrow(r, HA_LEFT, L_you_have_enough_wood);
    }
    break;
  case 2: {
    bool prio = tut_flags & TF_PRIO;
    bool workers_open = win_is_open(WK_WORKERS);
    /* the second help: enough stone, no woodcutters */
    bool woodcutters = false;
    for (int i = 1; i < MAX_PERMS && !woodcutters; i++)
      woodcutters = (perms[i].flags & PF_USED) && perms[i].cls == CLS_WoodcuttingCentre && perms[i].nworkers > 0;
    bool second = city.materials.m[M_STONE] > 6 && city.materials.m[M_WOOD] < 6 && !woodcutters;
    /* (the keypad's hint says which key reaches the panel, unless the second help's arrow is there too) */
    if (window_closed() && !workers_open && !prio && !second && city_panel_rect(PR_WORK, r))
      help_arrow_ex(r, HA_DOWN, cu.zone == Z_PANEL ? NULL : L_keypad_var_hint, &circle2);
    if (workers_open && !prio && win_action_rect(200 + CLS_StoneMine, r)) help_arrow(r, HA_LEFT, NULL);
    if (prio && (!nwins || workers_open) && city_panel_rect(PR_MAT0 + M_WOOD, r)) help_arrow(r, HA_DOWN_HINT, L_great_gather_stone);
    if (second) {
      extern int priority_jobs[CLS_COUNT];
      bool on_stone = priority_jobs[CLS_StoneMine] >= 3;
      if (!workers_open && city_panel_rect(PR_WORK, r)) help_arrow(r, HA_DOWN, L_you_have_enough_stone);
      if (workers_open && on_stone && win_action_rect(300 + CLS_StoneMine, r)) help_arrow(r, HA_LEFT, L_reduce_stone_mining_priority);
      if (workers_open && !on_stone && win_action_rect(200 + CLS_WoodcuttingCentre, r)) help_arrow(r, HA_LEFT, L_increase_woodcutting_priority);
    }
    break;
  }
  case 3:
    if (!(tut_flags & TF_HELP_USED) && !nwins && city_panel_rect(PR_HELP, r)) help_arrow(r, HA_DOWN_HINT, L_hint_hint);
    break;
  case 4:
    if (!(tut_flags & TF_SPEED_USED) && !nwins && cu.speed == 0 && city_panel_rect(PR_SPEED, r)) help_arrow(r, HA_DOWN_HINT, L_speedup_hint);
    break;
  case 4001:
    if (!win_is_open(WK_INVITE)) {
      if (city_build_rect(BR_INVITE, r)) help_arrow(r, HA_LEFT, NULL);
      else if (city_build_rect(BR_MANAGEMENT, r)) help_arrow(r, HA_DOWN, NULL);
    }
    break;
  }
}

/* TopScreenNotification: header and text, the screen's width, at the top */
static void notification_draw(void) {
  if (!note_text && !note_title) return;
  int y = 0, hh = 0, th = 0, w = SCREEN_W;
  uint8_t a = (uint8_t)(255 * (note_fade < 0 ? 0 : note_fade > 1 ? 1 : note_fade));
  if (note_title && *note_title) y--, hh = ui_para(FONT_16, 0, INT16_MIN, w - 10, note_title, C_BLACK, true, NULL), y += hh + 2;
  int ty = y;
  if (note_text && *note_text) y++, ty = y, th = ui_para(FONT_STANDARD, 0, INT16_MIN, w - 10, note_text, C_BLACK, true, NULL), y += th;
  y += 2;
  ui_noclip();
  if (a < 255) {
    ui_rect(0, 0, w, y, boxes[BX_TOOLTIP].fill, (uint8_t)(a * 230 / 255));
  } else {
    ui_box(0, 0, w, y, BX_TOOLTIP);
  }
  if (hh) ui_para(FONT_16, 2, -1, w - 10, note_title, C_BLACK, true, NULL);
  if (th) ui_para(FONT_STANDARD, 3, ty, w - 10, note_text, C_BLACK, true, NULL);
}

void story_ui_draw_help(void) {
  if (!ui.title) tutorial_draw();
}
void story_ui_draw_notes(void) { notification_draw(); }

/* MetaGame.winScenario */
void meta_win_scenario(int s, float time) {
  if (s < 0 || s >= 4) return;
  bool had = meta.won >> s & 1;
  if (!had || (int32_t)time < meta.best[s]) meta.best[s] = (int32_t)time;
  meta.won |= (uint8_t)(1 << s);
  extern void meta_save(void);
  meta_save();
}

/* tl;dr and other notes of the original that the calculator has no use for are left out */
void story_notify(const char *text) { (void)text; (void)note_buf; }
