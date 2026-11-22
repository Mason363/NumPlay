/* The Final Earth 2: what the user interface's files (ui*.c) share.
 *
 * The interface is drawn immediate-mode: each frame, the city's panel, the
 * build menu, the open window and the overlays are laid out again from the
 * game's state and turned into a list of primitives (boxes, sprites, text) on
 * game_draw's stack, which one band function draws over the city. Nothing a
 * window shows is kept between frames: only which window is open, its scroll
 * and the focused button. */
#ifndef FE_UII_H
#define FE_UII_H
#include "bld.h"
#include "build.h"
#include "classes.h"
#include "data.h"
#include "gfx.h"
#include "input.h"
#include "lang_data.h"
#include "story.h"
#include "ui.h"
#include "unlocks.h"
#include "upgrades.h"
#include "win.h"

/* ---------------------------------------------------------------- colours */
#define C_BLACK 0x0000
#define C_WHITE 0xFFFF
#define C_RED 0xF800                       /* [red] (0xFF0000) */
#define C_GREEN 0x07E0                     /* [green] */
#define C_BLUE 0x0204                      /* [blue] (0x004090) */

/* ---------------------------------------------------------------- the frame's primitives (ui_draw.c) */
enum { PR_RECT, PR_BOX, PR_SPR, PR_TEXT, PR_TEXTROT, PR_CLIP, PR_SPR_SMALL, PR_KEYS };
typedef struct {
  int16_t x, y, w, h;      /* text: w is its bytes */
  uint8_t kind, a1, a2, f; /* alphas (box: border, fill); text: font; sprite: flags */
  uint16_t c1, c2;         /* colours (box: border, fill); sprite: SPR_*, tint */
  union {
    const char *s;
    struct { int16_t sx, sy; } part;
  } u;
} UiPrim;

#define UI_PRIMS 200
#define UI_TEXT 1024   /* the most seen: about 530 */
struct UiFrame {
  UiPrim p[UI_PRIMS];
  char text[UI_TEXT];
  int n, ntext, dropped;
  int16_t cx0, cy0, cx1, cy1;   /* the clip of what is being added */
  /* the window's tooltip, drawn after the help arrows (the original's tooltip stage is above them) */
  struct { char header[64], text[200]; int16_t x, y; bool on; } tip;
};
extern UiFrame *uf;   /* the frame being made (NULL: nothing is drawn, as when only laying out) */

/* a 9-patch box of the original's (each is a 1-pixel border round a fill) */
typedef struct { uint16_t border, fill; uint8_t ba, fa; } Box;
/* the original's 9-patches and buttons: normal, hover (focused) and active (pressed or on) */
enum { BX_WINDOW, BX_WINDOW_OPAQUE, BX_TOOLTIP, BX_INFO, BX_HINTINFO, BX_ACTIVE, BX_DISABLED,
       BX_BUTTON, BX_BUTTON_HOVER, BX_BUTTON_DOWN,                    /* spr_button */
       BX_TRANS, BX_TRANS_HOVER, BX_TRANS_DOWN,                       /* spr_transparentbutton_info */
       BX_BLD, BX_BLD_HOVER, BX_BLD_DOWN,                             /* spr_buildingbutton */
       BX_TRANSB, BX_TRANSB_HOVER, BX_TRANSB_DOWN,                    /* spr_transparentbutton */
       BX_CHECKED, BX_CHECKED_HOVER, BX_CHECKED_DOWN,                 /* spr_button_checked */
       BX_ACTIVABLE, BX_ACTIVABLE_HOVER, BX_ACTIVABLE_DOWN,           /* spr_button_activable */
       BX_GRAY, BX_GRAY_HOVER, BX_GRAY_DOWN,                          /* spr_button_gray */
       BX_SCROLL, BX_HANDLE, BX_COUNT };
extern const Box boxes[BX_COUNT];

void ui_clip(int x, int y, int w, int h);
void ui_noclip(void);
void ui_rect(int x, int y, int w, int h, uint16_t c, uint8_t a);
void ui_box(int x, int y, int w, int h, int box);
UiPrim *ui_box_later(int x, int y, int w, int box);   /* a box whose height is set once known */
void ui_spr(uint16_t spr, int x, int y);
void ui_spr_ex(uint16_t spr, int sx, int sy, int w, int h, int x, int y, uint8_t flags, uint8_t alpha, uint16_t tint);
void ui_frame(uint16_t spr, int fw, int i, int x, int y, uint8_t flags, uint8_t alpha);   /* frame of a strip */
/* a whole sprite at 3/5 of its size (the mobile version's 0.6 scale), each pixel the nearest of the sprite's */
void ui_spr_small(uint16_t spr, int x, int y);
void ui_ring(int x, int y, int w, int h, int t, uint16_t c);
#define FOCUS_RING 0x4CA0   /* the keypad focus: the hint boxes' green (0x4D9600) */   /* a frame t pixels thick round (x, y, w, h) */
/* a row of keys, each a button cw x h with one character of chars (n of them): focused (255 none) is hovered,
 * or pressed when down (one primitive for the row) */
void ui_keys(int x, int y, int cw, int h, const char *chars, int n, int focused, bool down);
/* one line of text (n bytes, -1: all); copied into the frame unless keep (a string that lasts) */
int ui_text_n(uint8_t font, int x, int y, const char *s, int n, uint16_t color, uint8_t alpha, bool keep);
static inline int ui_text(uint8_t font, int x, int y, const char *s, uint16_t color) {
  return ui_text_n(font, x, y, s, -1, color, 255, false);
}
void ui_text_rot(uint8_t font, int x, int y, const char *s, uint16_t color);   /* turned a quarter clockwise */
/* a paragraph wrapped at max_w (PIXI's BitmapText): draws it if y is given (INT16_MIN: measure only);
 * returns its height (lines x line height), sets its widest line */
int ui_para(uint8_t font, int x, int y, int max_w, const char *s, uint16_t color, bool keep, int *width);
/* the colour prefix of the original's texts ("[red]..."): the text after it and its colour */
const char *ui_color_prefix(const char *s, uint16_t *color);
void ui_frame_begin(UiFrame *f);
void ui_frame_end(void);             /* adds the band function that draws the frame */
static inline int ui_line_h(uint8_t font) { return gfx_font_height(font); }
int ui_text_w(uint8_t font, const char *s);

/* ---------------------------------------------------------------- texts and numbers (ui_draw.c) */
/* Localize.lo: "[0]", "[1]"... replaced by the arguments (numbers as JavaScript writes them, or strings) */
typedef struct { const char *s; double v; } LoArg;
#define LN(v) ((LoArg){NULL, (double)(v)})
#define LS(s) ((LoArg){(s), 0})
const char *lo(char *buf, int size, const char *text, int n, const LoArg *args);
const char *lo_1(char *buf, int size, const char *text, LoArg a);
const char *lo_2(char *buf, int size, const char *text, LoArg a, LoArg b);
const char *lo_3(char *buf, int size, const char *text, LoArg a, LoArg b, LoArg c);
int fmt_num(char *out, int size, double v);      /* String(v) */
int fmt_int(char *out, int size, long v);
char *str_cat(char *dst, int size, const char *src);   /* appends, returns dst */
/* common.MathExtensions.largeNumberFormat: 12345 as "12.3k" etc. (the axis of the materials graph) */
int fmt_large(char *out, int size, double v, bool alt);
int ui_float_fmt(char *out, int size, double v, int decimals);   /* MathExtensions.floatFormat */

/* ---------------------------------------------------------------- materials (ui_draw.c) */
extern const uint16_t mat_icon[M_COUNT];
extern const char *const mat_lname[M_COUNT];     /* "food", "wood"... as lo(name) */
const char *mat_title(int m, char *buf, int size);   /* "Food", "Wood"... (firstToUpper(lo(name))) */
/* MaterialsCostDisplay: the icons and amounts of a cost (red where the city has too little), and
 * before_k / after_k around knowledge; returns its displayWidth (measures only when y is INT16_MIN) */
int ui_cost(int x, int y, const float cost[M_COUNT], const char *before_k, const char *after_k, bool city_red);
/* IconListDisplay */
typedef struct { uint16_t spr; const char *text; } IconText;
int ui_icon_list(int x, int y, const IconText *it, int n);
void type_image(int x, int y, int cls);   /* a building type's picture: its button back and first 20x20 (ui_info.c) */

/* ---------------------------------------------------------------- the tooltip (ui_city.c) */
typedef struct {
  const char *header, *text;
  const float *cost;           /* MaterialsCostDisplay, or NULL */
  const IconText *extra;       /* icons right of the header */
  int nextra;
  const IconText *extra2;      /* a row of icons under the text */
  int nextra2;
  const float *cost2;          /* operating cost, after cost2_text */
  const char *cost2_text;
} Tip;
/* Tooltip.setTextDisplay, next to (ax, ay) (a hover position) or at the top-left (continuous) */
void ui_tooltip(const Tip *t, int ax, int ay, bool continuous);

/* ---------------------------------------------------------------- windows (ui_win.c) */
enum {
  WK_NONE, WK_TEXT, WK_BUILDING, WK_CITIZEN, WK_FOLLOW_LIST, WK_MATERIAL, WK_POPULATION, WK_HAPPINESS, WK_HOUSES,
  WK_WORKERS, WK_BIRTH, WK_ENTERTAINMENT, WK_FIND, WK_UPGRADES, WK_INVITE, WK_GOAL, WK_MISSIONS, WK_GAME_MENU,
  WK_RESTART, WK_SCENARIOS, WK_FREE_PLAY, WK_CREDITS, WK_DESTROY, WK_REPLACE, WK_REPLACE_TO, WK_CITY_NAME, WK_COLOR,
  WK_COUNT
};
typedef struct {
  uint8_t kind;
  uint8_t flags;               /* WF2_* */
  uint16_t arg, arg2;          /* its subject: a permanent, a citizen, a material... */
  int16_t scroll;              /* content pixels scrolled */
  uint8_t focus;               /* the focused button */
  int16_t cw, ch;              /* its content's size, measured last frame */
  int16_t tw;                  /* its title's width */
} Win;
/* WF2_KEYS: the window's buttons have the arrows (not the city's cursor); WF2_TIP: the player has moved the
 * focus, so the focused button's tooltip shows */
enum { WF2_PAUSED = 1, WF2_CONFIRM = 2, WF2_ALL = 4, WF2_NO_CLOSE = 8, WF2_OPAQUE = 16, WF2_NEW = 32, WF2_KEYS = 64,
       WF2_TIP = 128 };
#define WIN_STACK 5
extern Win wins[WIN_STACK];
extern int nwins;
static inline Win *win_top(void) { return nwins ? &wins[nwins - 1] : NULL; }

/* the layout of a window's contents (win.h's WinOut) */
enum { WO_DRAW, WO_NAV, WO_PRESS };
typedef struct { int16_t x, y, w, h; int16_t action; } NavRect;
#define NAV_MAX 192   /* (a window's focusables: the workers' window has four a kind of work building) */
struct WinOut {
  uint8_t mode;
  Win *win;
  Perm *perm;                  /* a building's window: its permanent (win_button's actions go to it) */
  int x0, y0;                  /* the content's top-left on screen (scrolled) */
  int cw;                      /* the content's width (last frame's) */
  int y;                       /* where the next element goes (content coordinates) */
  int maxw, vis0, vis1;        /* widest element; the content rows shown */
  int nfocus;                  /* focusables so far */
  int pressed;                 /* WO_PRESS: the focused button's action (-1 none) */
  int focus_y0, focus_y1;      /* the focused button's rows */
  bool row;                    /* buttons go side by side (win_row_begin) */
  int rx, rh;
  UiPrim *pending_box;         /* a button box that grows with what follows (win_button's description) */
  bool pending;
  struct {                     /* win_button's label and action, until its box is closed */
    char label[72];
    int action, y, focus_index;
    bool enabled, body, focused, cost;
  } pend;
  NavRect *nav;                /* WO_NAV: the focusables' rectangles */
  int nnav;
  int title_w;
  int last_focus;              /* the last focusable added was the focused one (win_tip) */
  int tip_x, tip_y;            /* the focused button's tooltip: where, and what */
  const char *tip_header, *tip_text;
  char tip_buf[200], tip_head[64];
  int16_t ring[4];             /* the focused element (content x, y, w, h), drawn framed when ring_on */
  bool ring_on;
  struct { const char *label, *tip_header, *tip; int16_t action; } bottom[2];   /* win_bottom_button's */
  int nbottom;
  bool later, muted;           /* a building window's second run of its class (win_later), its elements left out */
};
/* the tooltip of the button just added, shown while it is focused (gui.Tooltip on hover) */
void win_tip(WinOut *w, const char *header, const char *text);
/* elements a window's contents is made of (and win.h's win_title, win_text, win_info, win_button, win_cost) */
void win_title_icon(WinOut *w, const char *s, uint16_t icon);   /* a title with an icon */
void win_title_tint(WinOut *w, const char *s, uint16_t icon, uint16_t tint);   /* (its icon tinted) */
void win_header(WinOut *w, const char *s);         /* UpgradeWindowParts.createHeader (Arial15) */
/* a button: a box of the given style (BX_BUTTON..., | BXF_ACTIVE when on) round a label; returns true if focused */
#define BXF_ACTIVE 0x80
bool win_text_button(WinOut *w, const char *label, int action, int box, bool fill_width, bool centred);
/* UpgradeWindowParts.createActivatableButton: a name, a description (Arial10) and a cost in a box
 * (green when active, a button otherwise); returns true if focused */
bool win_activatable(WinOut *w, bool active, int action, const char *name, const char *desc, const float *cost,
                     const char *before_k);
/* gui.CheckboxButton (policies): checked or not */
bool win_checkbox(WinOut *w, bool checked, int action, const char *name, const char *desc, const float *cost);
/* windowAddSimpleButton: an icon and a text in a small button (residents, workers...) */
bool win_simple_button(WinOut *w, uint16_t icon, const char *text, int action);
bool win_small_button(WinOut *w, int x, int wd, int h, int action);   /* focusable, drawn by the caller */
void win_row_begin(WinOut *w);
void win_row_end(WinOut *w);
/* the bottom row: extra buttons on the left, Close (or another) on the right */
void win_bottom(WinOut *w, const char *const *labels, const int *actions, int n, const char *close_label, int close_action);
void win_bottom_tips(WinOut *w, const char *const *labels, const int *actions, const char *const *tips, int n,
                     const char *close_label, int close_action);
void win_bottom_ex(WinOut *w, const char *const *labels, const int *actions, const char *const *tip_headers,
                   const char *const *tips, int n, const char *close_label, int close_action);
#define ACT_CLOSE 900
#define ACT_BACK 901
#define ACT_DESTROY 902   /* a building window's Destroy (focused when it opens asking "Really Destroy?") */
/* reserves an area of the content (graphs); returns its top-left on screen or false if not visible */
bool win_area(WinOut *w, int width, int height, int *sx, int *sy);

void win_open(int kind, uint16_t arg, uint16_t arg2);       /* on top of the stack */
void win_open_only(int kind, uint16_t arg, uint16_t arg2);  /* clearWindowStack, then open */
void win_close(void);                                       /* goPreviousWindow */
void win_close_all(void);
bool win_action_rect(int action, int16_t r[4]);   /* a button of the top window on screen (for the tutorial's arrows) */
bool win_screen_rect(int16_t r[4]);                /* the top window on screen */
void win_replace(int kind, uint16_t arg, uint16_t arg2);    /* in place of the top one */
void win_pause(void);                                       /* CityGUI.pauseForWindow */
bool win_is_open(int kind);
void win_draw(void);
void win_draw_tip(void);                                    /* the top window's tooltip (after the help arrows) */
void win_input(void);                                       /* keys while a window is open */
const char *win_kind_title(Win *wn, char *buf, int size);

/* the windows' contents and actions, by kind (ui_bwin.c, ui_info.c, ui_story.c, ui_menu.c) */
typedef struct {
  void (*content)(WinOut *w);
  void (*action)(Win *wn, int action);
  uint8_t flags;               /* WD_* */
} WinDef;
/* WD_TOP: WindowPosition.Top; WD_OWN_CLOSE: closing goes to the window's action (as ACT_CLOSE or ACT_BACK) */
enum { WD_OPAQUE = 1, WD_PAUSE = 2, WD_NO_CLOSE_BUTTON = 4, WD_TOP = 8, WD_OWN_CLOSE = 16 };
extern const WinDef windefs[WK_COUNT];
void bwin_content(WinOut *w);
void bwin_action(Win *wn, int a);
void cwin_content(WinOut *w);
void cwin_action(Win *wn, int a);
void flist_content(WinOut *w);
void flist_action(Win *wn, int a);
void destroy_content(WinOut *w);
void destroy_action(Win *wn, int a);
void replace_content(WinOut *w);
void replace_action(Win *wn, int a);
void replace_to_content(WinOut *w);
void replace_to_action(Win *wn, int a);
void city_name_content(WinOut *w);
void city_name_action(Win *wn, int a);
void color_content(WinOut *w);
void color_action(Win *wn, int a);
void info_material(WinOut *w);
void info_population(WinOut *w);
void info_population_action(Win *wn, int a);
void info_happiness(WinOut *w);
void info_happiness_action(Win *wn, int a);
void info_houses(WinOut *w);
void info_houses_action(Win *wn, int a);
void info_workers(WinOut *w);
void info_workers_action(Win *wn, int a);
void info_birth(WinOut *w);
void info_birth_action(Win *wn, int a);
void info_entertainment(WinOut *w);
void info_find(WinOut *w);
void info_find_action(Win *wn, int a);
void info_upgrades(WinOut *w);
void info_upgrades_action(Win *wn, int a);
void info_invite(WinOut *w);
void info_invite_action(Win *wn, int a);
void story_goal_content(WinOut *w);
void story_goal_action(Win *wn, int a);
void story_missions_content(WinOut *w);
void menu_game_content(WinOut *w);
void menu_game_action(Win *wn, int a);
void menu_restart_content(WinOut *w);
void menu_restart_action(Win *wn, int a);
void menu_scenarios_content(WinOut *w);
void menu_scenarios_action(Win *wn, int a);
void menu_free_play_content(WinOut *w);
void menu_free_play_action(Win *wn, int a);
void menu_credits_content(WinOut *w);
void text_content(WinOut *w);
/* the colour picker and typing (ui_pick.c): the picker for a decoration (arg: its permanent) or for the build
 * menu's any-colour look (arg: the class); typing a text (TX_*: the city's name, a colour code) */
enum { COLOR_FOR_BUILDING, COLOR_FOR_BUILDER };
enum { TX_CITY_NAME, TX_COLOR_CODE };
extern int32_t color_picked, color_picked_on_menu_open;
void color_picker_open(int target, uint16_t arg);
void text_window_open(int which);
bool pick_keys(Win *wn);
/* WK_TEXT windows: a title and a text that last (flash, or ui_text_buf) */
void win_open_text(const char *title, const char *text);
extern char ui_text_buf[200];   /* a text made at run time for a simple window or a notification */

/* ---------------------------------------------------------------- the city screen (ui_city.c) */
enum { Z_CITY, Z_BUILD, Z_PANEL };
typedef struct {
  int16_t cx, cy;              /* the cursor: its cell's top-left (city pixels) */
  uint8_t zone;                /* Z_* where the arrows go when no window is open */
  int8_t category;             /* the build menu's shown category, -1 none */
  int8_t bfocus;               /* build menu focus: -1 the category row, else the shown buttons' index */
  int8_t cfocus;               /* the focused category button */
  int8_t pfocus;               /* the panel's focused button */
  bool panel_hidden;           /* the hide-UI toggle */
  bool modes_open;             /* all the building modes shown */
  bool mgmt_open;              /* the management options shown */
  /* the builder */
  int8_t build_kind;           /* BK_* */
  uint8_t build_cls;           /* a class, a decoration or a world resource */
  uint8_t build_decor;         /* a decoration's look (as_multi_decor) */
  bool build_mirror;           /* the builder's mirroring (Builder.mirrored) */
  int32_t build_color;         /* the builder's colour (decorationAppearanceColor), -1 none */
  bool mirrored;
  uint8_t speed;               /* simulationSpeedChosen */
  uint16_t highlight_cls;      /* FindBuildingsOfType: the class highlighted + 1 */
  uint16_t following;          /* the citizen followed (FollowingCitizen), 0 none */
  uint16_t selected;           /* the permanent whose window is open (spr_selectedbuilding) */
  float gui_timer;             /* GUI.guiTimer */
} CityUi;
extern CityUi cu;
enum { BK_NONE, BK_BUILDING, BK_DECORATION, BK_RESOURCE, BK_MOVE };   /* BK_MOVE: explored alien ruins (moving_resource) */
extern uint16_t moving_resource;
void city_ui_reset(void);
void city_input(void);
void city_draw_overlay(void);   /* in the city's layer: cursor, builder, highlights */
void city_draw_ui(void);        /* panel and build menu */
void city_draw_tips(void);      /* their tooltips, and the builder's */
void builder_start(int kind, int index, int decor);
void builder_start_ex(int kind, int index, int decor, bool mirror, int32_t color);
void builder_stop(void);
void camera_follow(float x, float y);   /* centre the view there (FollowingCitizen) */
void camera_clamp_view(void);
/* the cursor's cell: its world, column and row (-1 when on no world) */
int cursor_cell(int *col, int *row);
void cursor_to(int x, int y);
bool city_panel_rect(int which, int16_t r[4]);   /* tutorial arrows: a panel button's rectangle */
bool city_build_rect(int which, int16_t r[4]);   /* tutorial arrows: a build menu button's rectangle */
enum { PR_HELP, PR_PAUSE, PR_SPEED, PR_MENU, PR_HIDE, PR_POP, PR_HAPPY, PR_HOUSES, PR_WORK, PR_MAT0 };
enum { BR_FIRST_CATEGORY, BR_LAST_BUILDING, BR_SECOND_LAST_BUILDING, BR_INVITE, BR_MANAGEMENT };
extern const float simulation_speeds[5];

/* ---------------------------------------------------------------- story overlays (ui_story.c) */
void story_ui_reset(void);
void story_ui_tick(void);
void story_ui_draw_help(void);    /* the tutorial's help arrows (gameHelpStage) */
void story_ui_draw_notes(void);   /* the top notification (notificationStage, over everything) */
bool story_has_quick_text(void);
void notify_top(const char *text, const char *title, int time);   /* GUI.notify (TopScreenNotification) */
/* the goal text with the inline commands handled ([n], [c], [mobi]...): into buf */
const char *goal_text(int goal, char *buf, int size);
extern uint8_t tut_flags;   /* tutorial arrows' remembered steps (TF_*) */
enum { TF_HELP_USED = 1, TF_SPEED_USED = 2, TF_PRIO = 4, TF_WINDOW_CLOSED = 8, TF_EVER_PRIO = 16 };
extern int16_t tut_close_warning;   /* showWindowCloseWarning's countdown */

/* ---------------------------------------------------------------- menus and saves (ui_menu.c) */
typedef struct {
  uint8_t won;                 /* bits: scenarios won */
  int32_t best[4];             /* their best times (minutes) */
} Meta;
extern Meta meta;
void meta_load(void);
void title_open(void);           /* the title screen (the display city, the title, its buttons) */
void title_input(void);
void title_draw(void);
void menu_new_city(int story);   /* createNewGameState */
bool ui_save(bool tell);         /* save_game; tells the player when it fails */
bool ui_home(void);              /* Home: saves; true when the game may quit */
void autosave_tick(void);
bool story_requirements(int s);  /* progress.StoryLoader.hasCompletedRequirements */

/* ---------------------------------------------------------------- shared state (ui.c) */
typedef struct {
  bool title;                  /* the title screen is shown */
  bool setup;                  /* a free play city is being set up (its options window over it) */
  bool paused_for_window;
  float autosave_timer;        /* seconds since the last save (handleAutoSave) */
  float seconds_since_save;
  uint8_t save_failed;         /* the last save failed: a window says so */
  bool home_unsaved;           /* Home's save failed: its window is open (Home again quits) */
} UiState;
extern UiState ui;
/* the class's texts and look in the build menu (lang_data.c) */
static inline const char *cls_title(int c) { return cls_text[c].name; }
#endif
