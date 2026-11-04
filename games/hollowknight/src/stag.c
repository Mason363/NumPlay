/*
 * The stag's menu (Stag Map: the stations opened, a map of them, the selector at the chosen one) and its rides
 * (Cinematic_Stag_travel: its film left out, the screen kept black as long, then the new room's door_stagExit).
 * Its places in HUD units from the menu's (tools/stag.py); it opens growing to its full size.
 */
#include <math.h>
#include "game.h"

#pragma GCC optimize("Os")

#define DT 0.02f
#define HUD_PX (FOCAL / (-1.342f - CAM_Z))
#define NSTATIONS STAG_N
#define RESERVE 80   /* (the frame's instances kept for it) */
#define STAG_TITLE_OVER 0.16f   /* (Stations' baseline over its divider, as the game has it) */
#define STAG_KEYS_GAP 0.15f     /* (between the two actions' lines) */

typedef struct {
  float x, y, kx, ky;
  int16_t up, down;
  float delay;
} Border;

static const UiPiece backing = STAG_BACKING, pieces[NSTATIONS + 1] = STAG_PIECES, map_knight = STAG_MAP_KNIGHT,
                     map_shadow = STAG_MAP_SHADOW, ptr_l = STAG_POINTER_L, ptr_r = STAG_POINTER_R,
                     list_knight = STAG_LIST_KNIGHT, divider = STAG_DIVIDER;
static const UiText t_row = STAG_T_ROW, t_stations = STAG_T_STATIONS, t_act_confirm = STAG_T_ACT_CONFIRM,
                    t_act_cancel = STAG_T_ACT_CANCEL;
static const float at[2] = STAG_AT, marks[NSTATIONS][2] = STAG_MARKS, selector[4] = STAG_SELECTOR, row0[2] = STAG_ROW,
                   key_confirm[2] = STAG_KEY_CONFIRM, key_cancel[2] = STAG_KEY_CANCEL;
static const int16_t opened[NSTATIONS] = STAG_OPENED, names[NSTATIONS] = TXT_STAG_NAMES,
                     names_str[NSTATIONS] = STAG_NAMES_STR;
static const Border borders[2] = {STAG_BORDER_TOP, STAG_BORDER_BOTTOM};

enum { SG_CLOSED, SG_OPEN, SG_CHOSEN };
static struct {
  uint8_t st, n, cur, init, fsm, var;
  uint8_t rows[NSTATIONS];   /* (the stations listed: their numbers) */
  bool cancel;
  float t;                   /* since it opened; then in SG_CHOSEN */
  float rep;
  uint32_t prev;
  Anim border[2], sel;
  /* the ride */
  bool riding;
  int16_t next_scene;
  float ride_t;
} sg;

bool stag_busy(void) { return sg.st != SG_CLOSED || sg.riding; }
void stag_reset(void) { memset(&sg, 0, sizeof sg); }
void stag_next_scene(int str) { sg.next_scene = (int16_t)str; }

static void stag_draw(void);

void stag_menu_open(int fsm, int var) {
  if (sg.st != SG_CLOSED) return;
  /* (UI List Stag: the stations opened, in order; the one the stag is at first (Initial Item, Cancel Item)) */
  sg.n = 0, sg.init = 0;
  int at_station = (int)g_pd.stag_position1 - 1;
  for (int i = 0; i < NSTATIONS; i++) {
    if (opened[i] >= 0 && !pd_flag(opened[i])) continue;
    if (i == at_station) sg.init = sg.n;
    sg.rows[sg.n++] = (uint8_t)i;
  }
  sg.cur = sg.init, sg.fsm = (uint8_t)fsm, sg.var = (uint8_t)var;
  sg.st = SG_OPEN, sg.t = 0, sg.rep = 0, sg.cancel = false;
  sg.prev = g_hero.keys;
  /* (Up: the HUD out; Border Display: its clips up; the selector's clip) */
  hud_slide(true);
  for (int i = 0; i < 2; i++) anim_play_from_frame(&sg.border[i], borders[i].up, 0);
  anim_play(&sg.sel, CLIP_STAGUI_STAG_MAP_SELECTION_CURSOR);
}

void stag_travel(void) {
  /* (StagTravel: the film at least 3.5 s, skipped after 1.5) */
  sg.riding = true, sg.ride_t = 0;
  sg.prev = g_hero.keys;
}

static const int16_t scenes_str[NSTATIONS] = STAG_SCENES_STR;
static const uint16_t scenes_room[NSTATIONS] = STAG_SCENES_ROOM;   /* (0x8000 and its number: a room of the other app) */
static int room_named(int str) {
  for (int i = 0; i < NSTATIONS; i++)
    if (scenes_str[i] == str) return scenes_room[i];
  return -1;
}

uint32_t stag_tick(uint32_t keys) {
  uint32_t pressed = keys & ~sg.prev;
  sg.prev = keys;
  if (sg.riding) {
    sg.ride_t += DT;
    if (sg.ride_t >= 3.5f || (sg.ride_t >= 1.5f && (pressed & (K_OK | K_BACK | K_JUMP | K_ATTACK)))) {
      sg.riding = false;
      int room = room_named(sg.next_scene);
      if (room >= 0) game_transition(room, STR_DOOR_STAGEXIT, GATE_DOOR, 0, false);
    }
    return 0;
  }
  if (sg.st == SG_CLOSED) return keys;
  g_gfx_overlay = stag_draw, g_gfx_reserve = RESERVE;
  sg.t += DT;
  for (int i = 0; i < 2; i++) sg.border[i].events = 0, anim_update(&sg.border[i], DT);
  sg.sel.events = 0;
  anim_update(&sg.sel, DT);
  if (sg.st == SG_CHOSEN) {
    /* (Selection Made: its pause, a cancel's 0.2 s; then the menu gone at once, its choice to who asked) */
    if (sg.t >= (sg.cancel ? 0.2f : 0.15f)) {
      sg.st = SG_CLOSED;
      g_gfx_overlay = NULL, g_gfx_reserve = 0;
      vm_fsm_set(sg.fsm, sg.var, (uint32_t)names_str[sg.rows[sg.cur]], VMEV_CONTINUE);
    }
    return 0;
  }
  if (sg.t < 0.5f) return 0;   /* (the list active after half a second) */
  /* ui_list: up and down (again after 0.25 s held, then each 0.15 s), round */
  uint32_t ud = keys & (K_UP | K_DOWN);
  int move = 0;
  if (!ud) sg.rep = 0;
  else if (pressed & ud) sg.rep = 0.25f, move = (pressed & K_DOWN) ? 1 : -1;
  else if ((sg.rep -= DT) <= 0) sg.rep = 0.15f, move = (keys & K_DOWN) ? 1 : -1;
  if (move && sg.n) sg.cur = (uint8_t)((sg.cur + sg.n + move) % sg.n);
  if (pressed & K_OK) sg.st = SG_CHOSEN, sg.t = 0;
  else if (pressed & K_BACK) sg.st = SG_CHOSEN, sg.t = 0, sg.cancel = true, sg.cur = sg.init;
  return 0;
}

/* ---------------------------------------------------------------- drawing: on the HUD, over all */
static float grow(void) {
  /* (Up: iTweenScaleTo from 1 to its size, half a second, ease out) */
  float u = sg.t >= 0.5f || sg.st == SG_CHOSEN ? 1 : sinf(sg.t / 0.5f * 1.5707964f);
  return (1 + (STAG_K - 1) * u) / STAG_K;
}

static void put(int sprite, float x, float y, float kx, float ky, uint8_t tint) {
  float g = grow();
  Inst in;
  sprite_inst(sprite, at[0] + x * g, at[1] + y * g, 0, kx * g, ky * g, tint, &in);
  gfx_overlay(&in);
}
static void put_piece(const UiPiece *p, float dx, float dy, uint8_t tint) { put(p->sprite, p->x + dx, p->y + dy, p->kx, p->ky, tint); }

static void put_text(int text, int style, const UiText *t, float dy, int align, float a) {
  float g = grow();
  UiText u = {at[0] + t->x * g, at[1] + (t->top + dy) * g, t->w * g};
  ui_text_at(text, style, &u, 0, 0, align, a);
}

/* a text's width, HUD units */
static float width_of(int text, int style) {
  const uint8_t *s = text_get(text);
  return text_width(style, s, (int)strlen((const char *)s)) / HUD_PX;
}

/* an action's key (ActionButtonIcon: the calculator's, by name) -> its left (from the menu's place) */
static float key_box(const float *k, int name, float a, uint8_t white) {
  float g = grow();
  return k[0] - ui_key(name, at[0] + k[0] * g, at[1] + k[1] * g, white, a);
}

/* an action's text, ending before its key (the text bigger than the game's: kept clear of it; the keys' actions'
 * size, as elsewhere) */
static void act_text(int text, const UiText *t, float key_left, float dy) {
  UiText u = *t;
  if (u.x > key_left - 0.15f) u.x = key_left - 0.15f;
  put_text(text, STYLE_MSG_S, &u, dy, 2, 1);
}

/* how much higher than the game's a text must be to keep above what is under it (its top at top, what is under it at
 * under; as far over it as the game's) */
static float raise(int style, float top, float under, float over) {
  const uint8_t *st = font_style(style);
  float d = font_asc(st) / HUD_PX + over - (top - under);
  return d > 0 ? d : 0;
}

static void stag_draw(void) {
  /* the Faders: up over half a second; the map's pieces the same; the selector after half a second */
  float fade = sg.st == SG_CHOSEN ? 1 : fminf(1, sg.t / 0.5f), sel_a = fminf(1, fmaxf(0, (sg.t - 0.5f) / 0.5f));
  if (sg.st == SG_CHOSEN) sel_a = 1;
  uint8_t white = gfx_dyn_tint(29, 255, 255, 255, 255), faded = gfx_dyn_tint(30, 255, 255, 255, (uint8_t)(fade * 255));
  put_piece(&backing, 0, 0, gfx_dyn_tint(31, 255, 255, 255, (uint8_t)(fade * STAG_BACKING_A * 255)));
  /* the map: its core, the pieces of the stations opened */
  put_piece(&pieces[0], 0, 0, faded);
  for (int i = 0; i < sg.n; i++) put_piece(&pieces[1 + sg.rows[i]], 0, 0, faded);
  /* the Knight's icon (and its shadow) at the station it is at, the selector at the chosen one (MARKER UP: as the
   * list is active) */
  if (sg.t >= 0.5f || sg.st == SG_CHOSEN) {
    const float *m = marks[sg.rows[sg.init]], *c = marks[sg.rows[sg.cur]];
    put_piece(&map_shadow, m[0], m[1], gfx_dyn_tint(24, 0, 0, 0, 255));
    put_piece(&map_knight, m[0], m[1], white);
    if (sg.sel.sprite >= 0) put(sg.sel.sprite, c[0], c[1], selector[2], selector[3], gfx_dyn_tint(25, 255, 255, 255, (uint8_t)(sel_a * 255)));
  }
  /* the list: its title and divider, its rows, the pointers at the current one, the Knight's icon by his station */
  put_piece(&divider, 0, 0, faded);
  put_text(TXT_STAG_STATIONS, STYLE_MSG, &t_stations, raise(STYLE_MSG, t_stations.top, divider.y, STAG_TITLE_OVER), 1, fade);
  for (int i = 0; i < sg.n; i++) put_text(names[sg.rows[i]], STYLE_TUTE, &t_row, -i * STAG_ROW_DY, 1, fade);
  /* (the names bigger than the game's: the icon and the pointers kept clear of them) */
  float kx = fminf(list_knight.x, row0[0] - width_of(names[sg.rows[sg.init]], STYLE_TUTE) / 2 - 0.6f) - list_knight.x;
  put_piece(&list_knight, kx, -sg.init * STAG_ROW_DY, white);
  if (sg.t >= 0.5f || sg.st == SG_CHOSEN) {
    float y = row0[1] - sg.cur * STAG_ROW_DY;
    float px = fmaxf(ptr_r.x, width_of(names[sg.rows[sg.cur]], STYLE_TUTE) / 2 + 0.4f);
    put_piece(&ptr_l, row0[0] - px - ptr_l.x, y, white);
    put_piece(&ptr_r, row0[0] + px - ptr_r.x, y, white);
    /* the action keys (Buttons: after half a second) */
    /* (Travel over Cancel: higher, for both their bigger text) */
    const uint8_t *st = font_style(STYLE_MSG_S);
    float up = (font_asc(st) + font_desc(st)) / HUD_PX + STAG_KEYS_GAP - (key_confirm[1] - key_cancel[1]);
    up = up > 0 ? up : 0;
    const float kc[2] = {key_confirm[0], key_confirm[1] + up};
    act_text(TXT_STAG_CTRL_TRAVEL, &t_act_confirm, key_box(kc, TXT_KEY_OK, 1, white), up);
    act_text(TXT_STAG_CTRL_CANCEL, &t_act_cancel, key_box(key_cancel, TXT_KEY_BACK, 1, white), 0);
  }
  for (int i = 0; i < 2; i++)
    if (sg.border[i].sprite >= 0) put(sg.border[i].sprite, borders[i].x, borders[i].y, borders[i].kx, borders[i].ky, white);
}
