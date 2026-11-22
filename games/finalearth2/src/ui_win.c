/* The Final Earth 2: windows (the original's GUI.createWindow and its
 * window parts: gui.TextElement, gui.TextButton, gui.ContainerButton,
 * gui.ContainerWithScrollbar, gui.UpgradeWindowParts...).
 *
 * A window is kept as its kind and subject only. Each frame its contents
 * function runs with a WinOut that lays the elements out from the top and
 * draws those that show; the same function, run without drawing, finds the
 * buttons for the arrows and OK. */
#include "uii.h"

Win wins[WIN_STACK];
int nwins;
char ui_text_buf[200];

/* GUI.get_windowHeight: the screen less spaceAboveBelowWindow; at 320x240 the window may use nearly all of it, so
 * that what fits the screen needs no scrolling (a taller one scrolls) */
#define WIN_MAX_H (SCREEN_H - TITLE_H - 2 - 4)
#define TITLE_H 17         /* the title row: spr_button_windowheader's height */
#define CLOSE_W 23

/* ---------------------------------------------------------------- the stack */
static void win_init(Win *wn, int kind, uint16_t arg, uint16_t arg2) {
  memset(wn, 0, sizeof *wn);
  wn->kind = (uint8_t)kind;
  wn->arg = arg;
  wn->arg2 = arg2;
  wn->focus = 0;
  wn->flags = WF2_NEW;
  /* backspace on a building: its window asks "Really Destroy?" at once */
  if (kind == WK_BUILDING && (arg2 & WF2_CONFIRM)) wn->flags |= WF2_CONFIRM;
  if (windefs[kind].flags & WD_PAUSE) win_pause();
}

/* GUI.createWindow + addWindowToStack */
void win_open(int kind, uint16_t arg, uint16_t arg2) {
  if (nwins == WIN_STACK) {
    memmove(wins, wins + 1, sizeof(Win) * (WIN_STACK - 1));
    nwins--;
  }
  win_init(&wins[nwins++], kind, arg, arg2);
}

void win_open_only(int kind, uint16_t arg, uint16_t arg2) {
  win_close_all();
  win_open(kind, arg, arg2);
}

void win_replace(int kind, uint16_t arg, uint16_t arg2) {
  if (!nwins) {
    win_open(kind, arg, arg2);
    return;
  }
  win_init(&wins[nwins - 1], kind, arg, arg2);
}

/* CityGUI.closeWindow: the game goes on if it was paused for the window */
static void unpause_for_window(void) {
  if (city.paused && ui.paused_for_window) {
    city.paused = false;
    ui.paused_for_window = false;
  }
}

void win_close(void) {
  if (!nwins) return;
  Win *wn = &wins[nwins - 1];
  if (wn->kind == WK_BUILDING || wn->kind == WK_FOLLOW_LIST) cu.selected = 0;
  if (nwins >= 2 && (wins[nwins - 2].kind == WK_BUILDING || wins[nwins - 2].kind == WK_FOLLOW_LIST)) cu.selected = wins[nwins - 2].arg;
  if (wn->kind == WK_CITIZEN) cu.following = 0;
  if (wn->kind == WK_FIND) cu.highlight_cls = 0;
  if (wn->kind == WK_GOAL || wn->kind == WK_TEXT) tut_flags |= TF_WINDOW_CLOSED;
  if (wn->kind == WK_TEXT) ui.home_unsaved = false;
  nwins--;
  if (!nwins) unpause_for_window();
}

void win_close_all(void) {
  while (nwins) win_close();
}

void win_pause(void) {
  bool was_paused = city.paused, was_for_window = ui.paused_for_window;
  city.paused = true;
  if (was_for_window || !was_paused) ui.paused_for_window = true;
}

bool win_is_open(int kind) { return nwins && wins[nwins - 1].kind == kind; }

static const char *text_win_title, *text_win_text;
void text_content(WinOut *w) {
  if (text_win_title && *text_win_title) win_title(w, text_win_title);
  win_para(w, FONT_STANDARD, text_win_text, 250, C_BLACK);
  win_bottom(w, NULL, NULL, 0, L_close, ACT_CLOSE);
}

/* GUI.showSimpleWindow: a text (and a title) that last */
void win_open_text(const char *title, const char *text) {
  text_win_title = title;
  text_win_text = text;
  win_open(WK_TEXT, 0, 0);
}

/* ---------------------------------------------------------------- laying out */
static int win_title_x, win_title_y, content_clip[4];
static int vis(WinOut *w, int y, int h) { return y + h > w->vis0 && y < w->vis1; }

/* the focused element, framed so that the keypad's focus can't be missed (the original only lightens a hovered
 * button's border) */
static void focus_ring(WinOut *w, int x, int y, int wd, int h) {
  w->ring[0] = (int16_t)x, w->ring[1] = (int16_t)y, w->ring[2] = (int16_t)wd, w->ring[3] = (int16_t)h;
  w->ring_on = true;
}

/* a focusable element at content (x, y), w x h: whether it is focused */
static bool focusable(WinOut *w, int x, int y, int wd, int h, int action) {
  int i = w->nfocus++;
  if (w->mode == WO_NAV && w->nav && w->nnav < NAV_MAX) {
    NavRect *r = &w->nav[w->nnav++];
    r->x = (int16_t)x, r->y = (int16_t)y, r->w = (int16_t)wd, r->h = (int16_t)h, r->action = (int16_t)action;
  }
  bool f = w->win && i == w->win->focus;
  w->last_focus = f;
  if (f) {
    w->focus_y0 = y, w->focus_y1 = y + h;
    w->tip_x = w->x0 + x + wd / 2, w->tip_y = w->y0 + y;
    if (w->mode == WO_PRESS) w->pressed = action;
    focus_ring(w, x, y, wd, h);
  }
  return f;
}

/* (copied: the texts may be in the caller's buffers, gone before the tooltip is drawn) */
void win_tip(WinOut *w, const char *header, const char *text) {
  if (!w->last_focus || w->mode != WO_DRAW) return;
  w->tip_head[0] = 0;
  if (header) str_cat(w->tip_head, sizeof w->tip_head, header);
  if (text && text != w->tip_buf) w->tip_buf[0] = 0, str_cat(w->tip_buf, sizeof w->tip_buf, text);
  w->tip_header = header ? w->tip_head : NULL;
  w->tip_text = text ? w->tip_buf : NULL;
}

static bool drawing(WinOut *w) { return w->mode == WO_DRAW; }
static int sx(WinOut *w, int x) { return w->x0 + x; }
static int sy(WinOut *w, int y) { return w->y0 + y; }
static void grow(WinOut *w, int width) {
  if (width > w->maxw) w->maxw = width;
}

static void close_pending(WinOut *w);
static void pending_body(WinOut *w);

/* TextElement: font, its padding (addToPadding), wrapped at max_w */
static const int8_t pad_top[FONT_COUNT] = {[FONT_STANDARD] = -2, [FONT_10] = -3, [FONT_15] = -3, [FONT_16] = -2, [FONT_18] = -3};
static const int8_t pad_bottom[FONT_COUNT] = {[FONT_STANDARD] = 2, [FONT_10] = 3, [FONT_15] = 2, [FONT_16] = 4, [FONT_18] = 2};

static int para_at(WinOut *w, int x, uint8_t font, const char *s, int max_w, uint16_t color, int *width) {
  if (!s || !*s) {
    if (width) *width = 0;
    return 0;
  }
  uint16_t col = color;
  s = ui_color_prefix(s, &col);
  int wd, h = ui_para(font, 0, INT16_MIN, max_w, s, col, false, &wd);
  int eh = h + pad_top[font] + pad_bottom[font];
  if (drawing(w) && vis(w, w->y, eh)) ui_para(font, sx(w, x), sy(w, w->y) + pad_top[font], max_w, s, col, false, NULL);
  if (width) *width = wd;
  return eh;
}

void win_para(WinOut *w, uint8_t font, const char *s, int max_w, uint16_t color) {
  if (w->muted) return;
  close_pending(w);
  int wd;
  w->y += para_at(w, 0, font, s, max_w, color, &wd);
  grow(w, wd);
}

void win_space(WinOut *w, int h) {
  if (w->muted) return;
  close_pending(w);
  w->y += h;
}

/* windowAddTitleText: Arial16 after 3 pixels (and an icon: 1, the icon 4 lower, 2), the close button at the right */
void win_title_icon(WinOut *w, const char *s, uint16_t icon) { win_title_tint(w, s, icon, GFX_NO_TINT); }
void win_title_tint(WinOut *w, const char *s, uint16_t icon, uint16_t tint) {
  if (w->muted) return;
  if (w->title_w) {
    /* a window has one title: a second one is a heading in the text (as AlienRuins' "Findings") */
    win_space(w, 3);
    win_header(w, s);
    return;
  }
  int iw = icon ? 1 + spr_w(icon) + 2 : 0;
  w->title_w = 3 + iw + ui_text_w(FONT_16, s) + 3 + CLOSE_W;   /* a small gap before the close button */
  if (w->win) w->win->tw = (int16_t)w->title_w;
  if (drawing(w) && s) {
    /* the title row is not scrolled: drawn at the window's top, outside the contents' clip */
    ui_noclip();
    if (icon) ui_spr_ex(icon, 0, 0, spr_w(icon), spr_h(icon), win_title_x + 3 + 1, win_title_y + 4, 0, 255, tint);
    ui_text(FONT_16, win_title_x + 3 + iw, win_title_y + 1, s, C_BLACK);
    ui_clip(content_clip[0], content_clip[1], content_clip[2], content_clip[3]);
  }
}
void win_title(WinOut *w, const char *s) { win_title_icon(w, s, 0); }

void win_text(WinOut *w, const char *s) {
  if (w->muted) return;
  if (w->pending) {
    /* a button's description (createActivatableButton): Arial10 inside its box */
    pending_body(w);
    int wd;
    w->y += para_at(w, 3, FONT_10, s, 250, C_BLACK, &wd);
    grow(w, wd + 6);
    return;
  }
  win_para(w, FONT_STANDARD, s, 250, C_BLACK);
}

void win_info(WinOut *w, uint16_t icon, const char *s) {
  if (w->muted) return;
  if (!icon) {
    win_para(w, FONT_STANDARD, s, 250, C_BLACK);
    return;
  }
  close_pending(w);
  int iw = spr_w(icon);
  if (drawing(w) && vis(w, w->y, 12)) ui_spr(icon, sx(w, 0), sy(w, w->y) + 1);
  int wd;
  int h = para_at(w, iw + 2, FONT_STANDARD, s, 250 - iw - 2, C_BLACK, &wd);
  w->y += h > 12 ? h : 12;
  grow(w, iw + 2 + wd);
}

void win_cost(WinOut *w, const float cost[M_COUNT]) {
  if (w->muted) return;
  bool inside = w->pending;
  if (inside) {
    pending_body(w);
    w->pend.cost = true;
  } else {
    close_pending(w);
  }
  int x = inside ? 3 : 0;
  int wd = ui_cost(0, INT16_MIN, cost, "", " to research", true);
  if (drawing(w) && vis(w, w->y, 12)) {
    char after[24] = " ";
    ui_cost(sx(w, x), sy(w, w->y), cost, "", str_cat(after, sizeof after, L_to_research), true);
  }
  w->y += 12;
  grow(w, wd + x * 2);
}

/* ---------------------------------------------------------------- buttons */
/* the box of a button in its state: normal, focused (hover) or down (pressed or on) */
/* BXF_ACTIVE: TextButton's isActive (it is on), shown pressed */
static int state_box(int box, bool focused, bool down) {
  if (box & BXF_ACTIVE) box &= ~BXF_ACTIVE, down = true;
  return box + (down ? 2 : focused ? 1 : 0);
}

bool win_text_button(WinOut *w, const char *label, int action, int box, bool fill_width, bool centred) {
  close_pending(w);
  int tw = ui_text_w(FONT_STANDARD, label), bw = tw + 6, bh = 15;
  int x = w->row ? w->rx : 0;
  if (fill_width && w->cw > bw) bw = w->cw;
  bool f = focusable(w, x, w->y, bw, bh, action);
  if (drawing(w) && vis(w, w->y, bh)) {
    bool down = f && key_held(K_OK);
    ui_box(sx(w, x), sy(w, w->y), bw, bh, state_box(box, f, down));
    int tx = centred ? (bw - tw) / 2 : 3;
    ui_text(FONT_STANDARD, sx(w, x) + tx, sy(w, w->y) + 1, label, C_BLACK);
  }
  if (w->row) {
    w->rx += bw;
    if (bh > w->rh) w->rh = bh;
    grow(w, w->rx);
  } else {
    w->y += bh;
    grow(w, tw + 6);
  }
  return f;
}

void win_row_begin(WinOut *w) {
  close_pending(w);
  w->row = true;
  w->rx = 0;
  w->rh = 0;
}
void win_row_end(WinOut *w) {
  w->row = false;
  w->y += w->rh;
}

/* windowAddBottomButtons: extra buttons (2 apart) on the left, the close button on the right */
void win_bottom(WinOut *w, const char *const *labels, const int *actions, int n, const char *close_label,
                int close_action) {
  win_bottom_ex(w, labels, actions, NULL, NULL, n, close_label, close_action);
}
void win_bottom_tips(WinOut *w, const char *const *labels, const int *actions, const char *const *tips, int n,
                     const char *close_label, int close_action) {
  win_bottom_ex(w, labels, actions, NULL, tips, n, close_label, close_action);
}
void win_bottom_ex(WinOut *w, const char *const *labels, const int *actions, const char *const *tip_headers,
                   const char *const *tips, int n, const char *close_label, int close_action) {
  close_pending(w);
  int x = 0;
  for (int i = 0; i < n; i++) {
    int bw = ui_text_w(FONT_STANDARD, labels[i]) + 6;
    bool f = focusable(w, x, w->y, bw, 15, actions[i]);
    if ((tips && tips[i]) || (tip_headers && tip_headers[i])) win_tip(w, tip_headers ? tip_headers[i] : NULL, tips ? tips[i] : NULL);
    if (drawing(w) && vis(w, w->y, 15)) {
      ui_box(sx(w, x), sy(w, w->y), bw, 15, state_box(BX_BUTTON, f, f && key_held(K_OK)));
      ui_text(FONT_STANDARD, sx(w, x) + 3, sy(w, w->y) + 1, labels[i], C_BLACK);
    }
    x += bw + 2;
  }
  if (close_label) {
    int bw = ui_text_w(FONT_STANDARD, close_label) + 6;
    int cx = w->cw - bw;
    if (cx < x) cx = x;
    bool f = focusable(w, cx, w->y, bw, 15, close_action);
    if (drawing(w) && vis(w, w->y, 15)) {
      ui_box(sx(w, cx), sy(w, w->y), bw, 15, state_box(BX_BUTTON, f, f && key_held(K_OK)));
      ui_text(FONT_STANDARD, sx(w, cx) + 3, sy(w, w->y) + 1, close_label, C_BLACK);
    }
    x += bw;
  }
  grow(w, x);
  w->y += 15;
}

/* a class's button for the bottom row, left of Close (the building's window adds the row) */
void win_bottom_button(WinOut *w, const char *label, int action, const char *tip_header, const char *tip) {
  if (w->muted) return;
  if (w->nbottom >= 2) return;
  w->bottom[w->nbottom].label = label;
  w->bottom[w->nbottom].action = (int16_t)action;
  w->bottom[w->nbottom].tip_header = tip_header;
  w->bottom[w->nbottom].tip = tip;
  w->nbottom++;
}

/* UpgradeWindowParts.createHeader, and with one_action >= 0 addOneAndMaxButtons on its row: One (on unless all)
 * and All (Max when max), right of the heading, with their tooltips */
void win_heading(WinOut *w, const char *title, int one_action, int all_action, bool all, bool max, const char *one_tip,
                 const char *all_tip) {
  if (w->muted) return;
  close_pending(w);   /* (a button before it ends first: the One and All buttons go on the heading's row) */
  int y0 = w->y;
  win_header(w, title);
  if (one_action < 0) return;
  int one_w = ui_text_w(FONT_STANDARD, L_one) + 6;
  const char *all_label = max ? L_max : L_all;
  int all_w = ui_text_w(FONT_STANDARD, all_label) + 6;
  /* the buttons sit on the header's row, at the right (after a filler) */
  int save_y = w->y;
  w->y = y0;
  win_row_begin(w);
  w->rx = w->cw - one_w - 2 - all_w;
  if (w->rx < ui_text_w(FONT_15, title) + 2) w->rx = ui_text_w(FONT_15, title) + 2;
  /* (the one in use shows pressed: TextButton's isActive) */
  win_text_button(w, L_one, one_action, BX_BUTTON | (all ? 0 : BXF_ACTIVE), false, false);
  win_tip(w, NULL, one_tip);
  w->rx += 2;
  win_text_button(w, all_label, all_action, BX_BUTTON | (all ? BXF_ACTIVE : 0), false, false);
  win_tip(w, NULL, all_tip);
  win_row_end(w);
  if (w->y < save_y) w->y = save_y;
}

/* a class's button (win_button): CycleValueButton / FullSizeTextButton, or with a description and a cost
 * following it, UpgradeWindowParts.createActivatableButton */
void win_button(WinOut *w, uint16_t icon, const char *label, int action, bool enabled) {
  if (w->muted) return;
  close_pending(w);
  (void)icon;
  w->pend.label[0] = 0;
  str_cat(w->pend.label, sizeof w->pend.label, label);
  w->pend.action = action;
  w->pend.enabled = enabled;
  w->pend.body = false;
  w->pend.cost = false;
  w->pend.y = w->y;
  w->pend.focus_index = w->nfocus++;
  w->pend.focused = w->win && w->pend.focus_index == w->win->focus;
  w->pending = true;
  w->pending_box = drawing(w) ? ui_box_later(sx(w, 0), sy(w, w->y), w->cw, BX_BUTTON) : NULL;
  w->y += 3;
}

/* the first description or cost after a button: its name goes at the top left */
static void pending_body(WinOut *w) {
  if (w->pend.body) return;
  w->pend.body = true;
  int tw;
  w->y += para_at(w, 3, FONT_STANDARD, w->pend.label, 250, C_BLACK, &tw);
  grow(w, tw + 6);
}

static void close_pending(WinOut *w) {
  if (!w->pending) return;
  w->pending = false;
  UiPrim *box = w->pending_box;
  w->pending_box = NULL;
  int y0 = w->pend.y, h;
  if (!w->pend.body) {
    /* FullSizeTextButton: the label centred, 3 above and 0 below */
    int tw = ui_text_w(FONT_STANDARD, w->pend.label);
    h = 15;
    if (drawing(w) && vis(w, y0, h))
      ui_text(FONT_STANDARD, sx(w, (w->cw - tw) / 2), sy(w, y0) + 1, w->pend.label, w->pend.enabled ? C_BLACK : 0x8410);
    grow(w, tw + 6);
    w->y = y0 + h;
  } else {
    w->y += 1;
    h = w->y - y0;
  }
  /* the option chosen (a description, no cost, not enabled) is a box, not a button: nothing to focus */
  bool chosen = !w->pend.enabled && w->pend.body && !w->pend.cost;
  if (chosen) {
    if (w->nfocus == w->pend.focus_index + 1) w->nfocus--;
    w->pend.focused = false;
  }
  /* the button's rectangle for the arrows and OK */
  if (!chosen && w->mode == WO_NAV && w->nav && w->nnav < NAV_MAX) {
    NavRect *r = &w->nav[w->nnav++];
    r->x = 0, r->y = (int16_t)y0, r->w = (int16_t)w->cw, r->h = (int16_t)h, r->action = (int16_t)w->pend.action;
  }
  if (w->pend.focused) {
    w->focus_y0 = y0, w->focus_y1 = y0 + h;
    if (w->mode == WO_PRESS && w->pend.enabled) w->pressed = w->pend.action;
    focus_ring(w, 0, y0, w->cw, h);
  }
  if (box) {
    box->h = (int16_t)h;
    bool f = w->pend.focused;
    /* not enabled: with a cost, one the city can't pay (a button all the same); with a description only,
     * the option chosen (createActivatableButton's active box); alone, greyed */
    int style = w->pend.enabled || w->pend.cost ? state_box(BX_BUTTON, f, f && key_held(K_OK))
              : w->pend.body ? BX_ACTIVE : state_box(BX_GRAY, f, false);
    const Box *b = &boxes[style];
    box->c1 = b->border, box->c2 = b->fill, box->a1 = b->ba, box->a2 = b->fa;
    /* a box wholly outside the view is not drawn */
    if (sy(w, y0) + h <= uf->cy0 || sy(w, y0) >= uf->cy1) box->h = 0;
  }
  w->y += w->pend.body ? 2 : 4;   /* the spacing after it */
}

/* createActivatableButton: name, description (Arial10) and cost; active ones are green boxes, not buttons */
bool win_activatable(WinOut *w, bool active, int action, const char *name, const char *desc, const float *cost,
                     const char *before_k) {
  close_pending(w);
  int y0 = w->y, tw, h = 3;
  bool f = false;
  int nf = active ? -1 : w->nfocus++;
  if (!active) f = w->win && nf == w->win->focus;
  UiPrim *box = drawing(w) ? ui_box_later(sx(w, 0), sy(w, y0), w->cw, BX_BUTTON) : NULL;
  w->y += 3;
  w->y += para_at(w, 3, FONT_STANDARD, name, 250, C_BLACK, &tw);
  grow(w, tw + 6);
  if (desc && *desc) {
    w->y += para_at(w, 3, FONT_10, desc, 250, C_BLACK, &tw);
    grow(w, tw + 6);
  }
  if (cost) {
    bool any = false;
    for (int m = 0; m < M_COUNT; m++) any |= cost[m] > 0;
    if (any) {
      int cw = ui_cost(0, INT16_MIN, cost, before_k, L_to_research, true);
      if (drawing(w) && vis(w, w->y, 12)) {
        char after[24] = " ";
        str_cat(after, sizeof after, L_to_research);
        ui_cost(sx(w, 3), sy(w, w->y), cost, before_k, after, true);
      }
      w->y += 12;
      grow(w, cw + 6);
    }
  }
  w->y += 1;
  h = w->y - y0;
  if (!active) {
    if (w->mode == WO_NAV && w->nav && w->nnav < NAV_MAX) {
      NavRect *r = &w->nav[w->nnav++];
      r->x = 0, r->y = (int16_t)y0, r->w = (int16_t)w->cw, r->h = (int16_t)h, r->action = (int16_t)action;
    }
    if (f) {
      w->focus_y0 = y0, w->focus_y1 = y0 + h;
      if (w->mode == WO_PRESS) w->pressed = action;
      focus_ring(w, 0, y0, w->cw, h);
    }
  }
  if (box) {
    box->h = (int16_t)h;
    const Box *b = &boxes[active ? BX_ACTIVE : state_box(BX_BUTTON, f, f && key_held(K_OK))];
    box->c1 = b->border, box->c2 = b->fill, box->a1 = b->ba, box->a2 = b->fa;
    if (sy(w, y0) + h <= uf->cy0 || sy(w, y0) >= uf->cy1) box->h = 0;
  }
  w->y += 2;
  return f;
}

/* gui.CheckboxButton (createCheckboxButton): a checkbox and the name, then the description */
bool win_checkbox(WinOut *w, bool checked, int action, const char *name, const char *desc, const float *cost) {
  close_pending(w);
  (void)cost;
  int y0 = w->y, tw;
  int nf = w->nfocus++;
  bool f = w->win && nf == w->win->focus;
  UiPrim *box = drawing(w) ? ui_box_later(sx(w, 0), sy(w, y0), w->cw, BX_BUTTON) : NULL;
  w->y += 3;
  if (drawing(w) && vis(w, w->y, 12)) ui_frame(SPR_CHECKBOX, 8, checked ? 0 : 1, sx(w, 3), sy(w, w->y), 0, 255);
  int h1 = para_at(w, 3 + 8 + 3, FONT_STANDARD, name, 250, C_BLACK, &tw);
  w->y += h1 > 8 ? h1 : 8;
  grow(w, tw + 6 + 11);
  w->y += para_at(w, 3, FONT_10, desc, 250, C_BLACK, &tw);
  grow(w, tw + 6);
  w->y += 1;
  int h = w->y - y0;
  if (w->mode == WO_NAV && w->nav && w->nnav < NAV_MAX) {
    NavRect *r = &w->nav[w->nnav++];
    r->x = 0, r->y = (int16_t)y0, r->w = (int16_t)w->cw, r->h = (int16_t)h, r->action = (int16_t)action;
  }
  if (f) {
    w->focus_y0 = y0, w->focus_y1 = y0 + h;
    if (w->mode == WO_PRESS) w->pressed = action;
    focus_ring(w, 0, y0, w->cw, h);
  }
  if (box) {
    box->h = (int16_t)h;
    const Box *b = &boxes[state_box(checked ? BX_CHECKED : BX_BUTTON, f, f && key_held(K_OK))];
    box->c1 = b->border, box->c2 = b->fill, box->a1 = b->ba, box->a2 = b->fa;
    if (sy(w, y0) + h <= uf->cy0 || sy(w, y0) >= uf->cy1) box->h = 0;
  }
  w->y += 2;
  return f;
}

void win_setting(WinOut *w, bool checked, int action, const char *text) {
  if (w->muted) return;
  win_checkbox(w, checked, action, text, "", NULL);
}

void win_later(WinOut *w) {
  close_pending(w);
  w->muted = !w->later;
}

/* windowAddSimpleButton: [icon text] in a small button; several go side by side (4 apart) */
bool win_simple_button(WinOut *w, uint16_t icon, const char *text, int action) {
  close_pending(w);
  int iw = spr_w(icon), ih = spr_h(icon);
  int tw = ui_text_w(FONT_STANDARD, text);
  int bw = 2 + iw + 1 + tw + 3, bh = 2 + (ih > 13 ? ih : 13) - 1;
  int x = w->row ? w->rx : 0;
  bool f = focusable(w, x, w->y, bw, bh, action);
  if (drawing(w) && vis(w, w->y, bh)) {
    ui_box(sx(w, x), sy(w, w->y), bw, bh, state_box(BX_BUTTON, f, f && key_held(K_OK)));
    ui_spr(icon, sx(w, x) + 2, sy(w, w->y) + 2);
    ui_text(FONT_STANDARD, sx(w, x) + 2 + iw + 1, sy(w, w->y) + 1, text, C_BLACK);
  }
  if (w->row) {
    w->rx += bw + 4;
    if (bh > w->rh) w->rh = bh;
    grow(w, w->rx - 4);
  } else {
    w->y += bh;
    grow(w, bw);
  }
  return f;
}

/* a focusable spot of the content at (x, w->y), wd x h, drawn by the caller: whether it is focused */
bool win_small_button(WinOut *w, int x, int wd, int h, int action) {
  close_pending(w);
  return focusable(w, x, w->y, wd, h, action);
}

/* UpgradeWindowParts.createHeader: Arial15 (its padding top 3 cancels the font's -3; bottom 0 + 2), then 2 */
void win_header(WinOut *w, const char *s) {
  close_pending(w);
  int tw, h = ui_para(FONT_15, 0, INT16_MIN, 250, s, C_BLACK, true, &tw);
  if (drawing(w) && vis(w, w->y, h + 2)) ui_para(FONT_15, sx(w, 0), sy(w, w->y), 250, s, C_BLACK, false, NULL);
  w->y += h + 2;
  grow(w, tw + 2);
  w->y += 2;
}

bool win_area(WinOut *w, int width, int height, int *x, int *y) {
  close_pending(w);
  bool v = drawing(w) && vis(w, w->y, height);
  *x = sx(w, 0);
  *y = sy(w, w->y);
  w->y += height;
  grow(w, width);
  return v;
}

/* ---------------------------------------------------------------- drawing the window */
static bool win_has_title(Win *wn) { return wn->tw > 0; }

static void run_content(WinOut *w) {
  w->y = 0;
  w->maxw = 0;
  w->nfocus = 0;
  w->pressed = -1;
  w->focus_y0 = w->focus_y1 = -1;
  w->pending_box = NULL;
  w->pending = false;
  w->row = false;
  Win *wn = w->win;
  w->perm = wn->kind == WK_BUILDING ? P(wn->arg) : NULL;
  windefs[wn->kind].content(w);
  close_pending(w);
  if (w->row) win_row_end(w);
}

/* the window's outer size from its content's (last measured) */
static void win_geometry(Win *wn, int *wx, int *wy, int *ww, int *wh, int *sh, bool *scroll) {
  int th = win_has_title(wn) ? TITLE_H : 0;
  int cw = wn->cw;
  if (wn->tw - 6 > cw) cw = wn->tw - 6;
  int inner_h = wn->ch + 4;
  *sh = inner_h < WIN_MAX_H ? inner_h : WIN_MAX_H;
  *scroll = inner_h > WIN_MAX_H;
  *ww = cw + 4 + 2 + (*scroll ? 10 : 0);
  if (*ww < wn->tw) *ww = wn->tw;
  *wh = th + 1 + *sh + 1;
  *wx = SCREEN_W / 2 - *ww / 2;
  *wy = (windefs[wn->kind].flags & WD_TOP) ? 10 : SCREEN_H / 2 - *wh / 2;   /* WindowPosition.Top or Center */
  if (*wy + *wh > SCREEN_H) *wy = SCREEN_H - *wh;
}

/* sets up a WinOut for the top window (its layout from last frame's size) */
static void win_setup(WinOut *w, Win *wn, int mode) {
  memset(w, 0, sizeof *w);
  w->mode = (uint8_t)mode;
  w->win = wn;
  int wx, wy, ww, wh, sh;
  bool scroll;
  win_geometry(wn, &wx, &wy, &ww, &wh, &sh, &scroll);
  int th = win_has_title(wn) ? TITLE_H : 0;
  w->cw = ww - 6 - (scroll ? 10 : 0);
  w->x0 = wx + 3;
  w->y0 = wy + th + 3 - wn->scroll;
  w->vis0 = wn->scroll - 2;
  w->vis1 = wn->scroll + sh - 2;
  win_title_x = wx;
  win_title_y = wy;
}

/* measures a window's contents (when opened, so its first frame is the right size) */
static void win_measure(Win *wn) {
  WinOut w;
  UiFrame *keep = uf;
  uf = NULL;
  for (int pass = 0; pass < 2; pass++) {
    win_setup(&w, wn, WO_NAV);
    run_content(&w);
    wn->cw = (int16_t)w.maxw;
    wn->ch = (int16_t)w.y;
  }
  uf = keep;
}

static void win_scroll_by(Win *wn, int dy) {
  int wx, wy, ww, wh, sh;
  bool scroll;
  win_geometry(wn, &wx, &wy, &ww, &wh, &sh, &scroll);
  int max_scroll = wn->ch + 4 - sh;
  int s = wn->scroll + dy;
  if (s > max_scroll) s = max_scroll;
  if (s < 0) s = 0;
  wn->scroll = (int16_t)s;
}

/* a window just opened: its size measured and its first button that shows focused */
static void win_ready(Win *wn) {
  if (wn->flags & WF2_NEW) {
    win_measure(wn);
    wn->flags &= (uint8_t)~WF2_NEW;
    /* the first button that shows is focused */
    WinOut w;
    NavRect nav[NAV_MAX];
    win_setup(&w, wn, WO_NAV);
    w.nav = nav;
    UiFrame *keep = uf;
    uf = NULL;
    run_content(&w);
    uf = keep;
    wn->focus = 255;
    for (int i = 0; i < w.nnav; i++)
      if (nav[i].y + nav[i].h <= WIN_MAX_H - 4) {
        wn->focus = (uint8_t)i;
        break;
      }
    /* asking to destroy: the destroy button focused (scrolled to) */
    if (wn->flags & WF2_CONFIRM)
      for (int i = 0; i < w.nnav; i++)
        if (nav[i].action == ACT_DESTROY) {
          wn->focus = (uint8_t)i;
          win_scroll_by(wn, nav[i].y + nav[i].h);
        }
  }
}

/* the window drawn with the size its content had last; false if the content came out another size (it is then
 * drawn again: what this pass added to the frame is taken back first) */
static bool win_draw_pass(Win *wn) {
  int wx, wy, ww, wh, sh;
  bool scroll;
  win_geometry(wn, &wx, &wy, &ww, &wh, &sh, &scroll);
  int th = win_has_title(wn) ? TITLE_H : 0;
  ui_noclip();
  ui_box(wx, wy, ww, wh, (windefs[wn->kind].flags & WD_OPAQUE) ? BX_WINDOW_OPAQUE : BX_WINDOW);
  if (th && !(windefs[wn->kind].flags & WD_NO_CLOSE_BUTTON)) {
    /* the close button (spr_button_windowheader, spr_icon_close) */
    ui_frame(SPR_BUTTON_WINDOWHEADER, 23, key_held(K_BACK) ? 2 : 0, wx + ww - CLOSE_W, wy, 0, 255);
    ui_spr(SPR_ICON_CLOSE, wx + ww - CLOSE_W + 1, wy + 1);
  }
  /* the contents, clipped to the scrollable area */
  WinOut w;
  win_setup(&w, wn, WO_DRAW);
  content_clip[0] = wx + 1, content_clip[1] = wy + th + 1, content_clip[2] = ww - 2 - (scroll ? 10 : 0);
  content_clip[3] = sh;
  ui_clip(content_clip[0], content_clip[1], content_clip[2], content_clip[3]);
  run_content(&w);
  if (w.ring_on) ui_ring(sx(&w, w.ring[0]), sy(&w, w.ring[1]), w.ring[2], w.ring[3], 2, FOCUS_RING);
  ui_noclip();
  if (w.title_w == 0) wn->tw = 0;
  wn->cw = (int16_t)w.maxw;
  wn->ch = (int16_t)w.y;
  /* the scroll can't pass the end (the contents may have shrunk) */
  int nx, ny, nw, nh, nsh;
  bool nscroll;
  win_geometry(wn, &nx, &ny, &nw, &nh, &nsh, &nscroll);
  int max_scroll = wn->ch + 4 - nsh;
  if (wn->scroll > max_scroll) wn->scroll = (int16_t)(max_scroll > 0 ? max_scroll : 0);
  if (nx != wx || ny != wy || nw != ww || nh != wh || nscroll != scroll) return false;
  /* a focused button's tooltip, once the player has moved the focus (the original shows it on hover): kept for
   * win_draw_tip */
  if ((w.tip_header || w.tip_text) && (wn->flags & WF2_TIP) && uf) {
    uf->tip.header[0] = uf->tip.text[0] = 0;
    if (w.tip_header) str_cat(uf->tip.header, sizeof uf->tip.header, w.tip_header);
    if (w.tip_text) str_cat(uf->tip.text, sizeof uf->tip.text, w.tip_text);
    uf->tip.x = (int16_t)w.tip_x, uf->tip.y = (int16_t)w.tip_y;
    uf->tip.on = true;
  }
  if (scroll) {
    /* gui.ScrollBar: its background and handle (spr_windowparts) */
    int inner_h = wn->ch + 4, bx = wx + ww - 10, by = wy + th, bh = sh + 2;
    ui_box(bx, by, 10, bh, BX_SCROLL);
    int hh = (sh * bh + inner_h / 2) / inner_h, hy = (wn->scroll * bh + inner_h / 2) / inner_h;
    ui_box(bx, by + hy, 10, hh, BX_HANDLE);
  }
  return true;
}

void win_draw_tip(void) {
  if (!uf || !uf->tip.on) return;
  Tip t = {0};
  t.header = uf->tip.header, t.text = uf->tip.text;
  ui_tooltip(&t, uf->tip.x, uf->tip.y, false);
}

/* the top window: laid out before it shows, so that no frame draws it at an old size (clipped) */
void win_draw(void) {
  Win *wn = win_top();
  if (!wn) return;
  win_ready(wn);
  int n = uf ? uf->n : 0, ntext = uf ? uf->ntext : 0, dropped = uf ? uf->dropped : 0;
  int16_t clip[4] = {0};
  if (uf) clip[0] = uf->cx0, clip[1] = uf->cy0, clip[2] = uf->cx1, clip[3] = uf->cy1;
  int top = nwins;
  extern int ui_redraws;
  for (int pass = 0; pass < 3; pass++) {
    if (win_draw_pass(wn) || nwins != top || win_top() != wn || pass == 2) break;
    ui_redraws++;
    if (uf) {
      uf->n = n, uf->ntext = ntext, uf->dropped = dropped, uf->tip.on = false;
      uf->cx0 = clip[0], uf->cy0 = clip[1], uf->cx1 = clip[2], uf->cy1 = clip[3];
    }
  }
}

/* ---------------------------------------------------------------- keys */

/* moves the focus to the nearest button that way, scrolling to show it (or scrolls if none is near) */
static void win_nav(Win *wn, int dx, int dy) {
  wn->flags |= WF2_TIP;
  WinOut w;
  NavRect nav[NAV_MAX];
  win_setup(&w, wn, WO_NAV);
  w.nav = nav;
  UiFrame *keep = uf;
  uf = NULL;
  run_content(&w);
  uf = keep;
  int wx, wy, ww, wh, sh;
  bool scroll;
  win_geometry(wn, &wx, &wy, &ww, &wh, &sh, &scroll);
  int view0 = wn->scroll, view1 = wn->scroll + sh - 4;
  int cur = wn->focus < w.nnav ? wn->focus : -1;
  /* where the focus is (or the view's edge) */
  int cx, cy0, cy1;
  if (cur >= 0) {
    cx = nav[cur].x + nav[cur].w / 2, cy0 = nav[cur].y, cy1 = nav[cur].y + nav[cur].h;
  } else {
    cx = w.cw / 2;
    cy0 = dy > 0 ? view0 - 1 : view1 + 1;
    cy1 = cy0;
    if (dy == 0) cy0 = view0, cy1 = view1;
  }
  int best = -1, best_score = 1 << 30;
  for (int i = 0; i < w.nnav; i++) {
    if (i == cur) continue;
    NavRect *r = &nav[i];
    int score;
    if (dy) {
      int ry0 = r->y, ry1 = r->y + r->h;
      int gap = dy > 0 ? ry0 - cy1 : cy0 - ry1;
      if (gap < -3) continue;   /* not that way */
      if (dy > 0 && ry0 <= cy0) continue;
      if (dy < 0 && ry1 >= cy1) continue;
      int rcx = r->x + r->w / 2;
      score = (gap > 0 ? gap : 0) * 4 + (rcx > cx ? rcx - cx : cx - rcx);
    } else {
      /* the same row: overlapping rows, that side */
      if (r->y + r->h <= cy0 || r->y >= cy1) continue;
      int rcx = r->x + r->w / 2;
      if (dx > 0 ? rcx <= cx : rcx >= cx) continue;
      score = dx > 0 ? rcx - cx : cx - rcx;
    }
    if (score < best_score) best_score = score, best = i;
  }
  if (best < 0) {
    if (dy) win_scroll_by(wn, dy * 24);
  } else {
    NavRect *r = &nav[best];
    /* far out of view: scroll toward it first (so the text before it can be read) */
    if (dy > 0 && r->y > view1 + 24 && r->y + r->h - view1 > sh / 2) {
      win_scroll_by(wn, 40);
    } else if (dy < 0 && r->y + r->h < view0 - 24 && view0 - r->y > sh / 2) {
      win_scroll_by(wn, -40);
    } else {
      wn->focus = (uint8_t)best;
      if (r->y < view0) win_scroll_by(wn, r->y - view0 - 2);
      else if (r->y + r->h > view1) win_scroll_by(wn, r->y + r->h - view1 + 2);
    }
  }
  /* a focused button scrolled out of view is no longer focused */
  if (wn->focus < w.nnav) {
    NavRect *r = &nav[wn->focus];
    int v0 = wn->scroll, v1 = wn->scroll + sh - 4;
    if (r->y + r->h <= v0 || r->y >= v1) wn->focus = 255;
  }
}

/* GUI.goPreviousWindow, or the window's own action */
static void win_do(Win *wn, int action) {
  if ((action == ACT_CLOSE || action == ACT_BACK) && !(windefs[wn->kind].flags & WD_OWN_CLOSE)) {
    win_close();
    return;
  }
  if (windefs[wn->kind].action) windefs[wn->kind].action(wn, action);
}

void win_input(void) {
  Win *wn = win_top();
  if (!wn) return;
  win_ready(wn);
  if (key_pressed(K_BACK)) {
    if (!(wn->flags & WF2_NO_CLOSE)) win_do(wn, ACT_BACK);
    return;
  }
  if (pick_keys(wn)) return;
  if (wn->kind == WK_CITIZEN && (wn->flags & WF2_CONFIRM)) {
    /* ChangeCitizenVitalBuildings: the cursor goes over the city; OK on a building makes it the citizen's. var
     * gives the arrows to the window's buttons (centre on the home, on the job) and back */
    if (key_pressed(K_VAR)) {
      wn->flags ^= WF2_KEYS;
      wn->focus = (wn->flags & WF2_KEYS) ? 0 : 255;
      if (wn->flags & WF2_KEYS) wn->flags |= WF2_TIP;
      return;
    }
    if (!(wn->flags & WF2_KEYS)) {
      extern bool city_cursor_keys(void);
      extern bool change_vital_building(uint16_t citizen, uint16_t id);
      wn->focus = 255;
      city_cursor_keys();
      if (input.released & K_OK) {
        int col, row;
        int wo = cursor_cell(&col, &row);
        uint16_t id = wo >= 0 ? perm_at(wo, col, row) : 0;
        if (is_building(P(id))) change_vital_building(wn->arg, id);
      }
      return;
    }
  }
  if ((windefs[wn->kind].flags & WD_TOP) && key_held(K_SHIFT)) {
    /* windows at the top leave the city in view: shift and the arrows move it */
    extern bool city_cursor_keys(void);
    city_cursor_keys();
    return;
  }
  if (key_held(K_SHIFT)) {
    if (key_repeat(K_UP)) win_scroll_by(wn, -60);
    if (key_repeat(K_DOWN)) win_scroll_by(wn, 60);
    return;
  }
  if (key_repeat(K_UP)) win_nav(wn, 0, -1);
  if (key_repeat(K_DOWN)) win_nav(wn, 0, 1);
  if (key_repeat(K_LEFT)) win_nav(wn, -1, 0);
  if (key_repeat(K_RIGHT)) win_nav(wn, 1, 0);
  if (input.released & K_OK) {
    WinOut w;
    win_setup(&w, wn, WO_PRESS);
    UiFrame *keep = uf;
    uf = NULL;
    run_content(&w);
    uf = keep;
    if (w.pressed >= 0) {
      win_do(wn, w.pressed);
    } else if (wn->focus == 255 && !(wn->flags & WF2_NO_CLOSE)) {
      /* nothing focused (a long text): OK closes it, as its Close button would */
      bool has_close = false;
      NavRect nav[NAV_MAX];
      win_setup(&w, wn, WO_NAV);
      w.nav = nav;
      uf = NULL;
      run_content(&w);
      uf = keep;
      for (int i = 0; i < w.nnav; i++) has_close |= nav[i].action == ACT_CLOSE;
      if (has_close) win_do(wn, ACT_CLOSE);
    }
  }
}

/* the top window's button for an action, on screen (the tutorial arrows point at it) */
bool win_screen_rect(int16_t r[4]) {
  Win *wn = win_top();
  if (!wn) return false;
  win_ready(wn);
  int wx, wy, ww, wh, sh;
  bool scroll;
  win_geometry(wn, &wx, &wy, &ww, &wh, &sh, &scroll);
  r[0] = (int16_t)wx, r[1] = (int16_t)wy, r[2] = (int16_t)ww, r[3] = (int16_t)wh;
  return true;
}

bool win_action_rect(int action, int16_t r[4]) {
  Win *wn = win_top();
  if (!wn) return false;
  win_ready(wn);
  WinOut w;
  NavRect nav[NAV_MAX];
  win_setup(&w, wn, WO_NAV);
  w.nav = nav;
  UiFrame *keep = uf;
  uf = NULL;
  run_content(&w);
  uf = keep;
  for (int i = 0; i < w.nnav; i++) {
    if (nav[i].action != action) continue;
    int y = w.y0 + nav[i].y;
    if (nav[i].y + nav[i].h <= w.vis0 || nav[i].y >= w.vis1) return false;
    r[0] = (int16_t)(w.x0 + nav[i].x), r[1] = (int16_t)y, r[2] = nav[i].w, r[3] = nav[i].h;
    return true;
  }
  return false;
}
