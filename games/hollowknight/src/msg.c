/* The message as the Knight takes an item (UI Msg Get Item: its Msg Control FSM): a dark backdrop, the item's icon and
 * name fade up, then how to use it (Tap or Press, the key, two lines), then the stop marker; a key sends it away.
 * Places in HUD units from the screen's center (its prefab, made at the origin, drawn by the HUD's camera). */
#pragma GCC optimize("Os")   /* (its code small: not where a frame's time goes) */
#include <math.h>
#include "game.h"

#define DT 0.02f
#define HUD_PX (FOCAL / (-1.342f - CAM_Z))

static const int16_t table[][6] = MSG_TABLE;   /* (name, prefix, Tap or Press, the two lines, the key's name) */
static const int16_t icons[] = {SPRITE_MSG_ICON_FIREBALL, SPRITE_MSG_ICON_DASH};

enum { MS_OFF, MS_TOP_UP, MS_BOT_UP, MS_STOP_UP, MS_DETECT, MS_DOWN };
/* (color_fader: what fades together; each its delay going up) */
enum { F_BG, F_ICON, F_NAME, F_PREFIX, F_PRESS, F_MSG1, F_MSG2, F_BUTTON, F_STOP, F_FLEUR, NF };
static const float up_delay[NF] = {0, 0.5f, 0.5f, 0.5f, 0, 0, 0.5f, 0, 0, 0};

static struct {
  uint8_t st, item;
  float t;
  float a[NF];      /* each part's alpha */
  float t_up[NF];   /* (its UP's time: -1 none) */
  bool down;
  uint32_t prev_keys;
} m;

static void notices_off(void);
static void notices_tick(void);

void msg_show(int item) {
  notices_off();
  memset(&m, 0, sizeof m);
  m.item = (uint8_t)item;
  for (int i = 0; i < NF; i++) m.t_up[i] = -1;
  /* Top Up: the backdrop, the icon, the name and its prefix (and the game saved) */
  m.t_up[F_BG] = m.t_up[F_ICON] = m.t_up[F_NAME] = m.t_up[F_PREFIX] = 0;
  save_game();
  m.st = MS_TOP_UP, m.t = 0;
  m.prev_keys = g_hero.keys;
}

bool msg_shown(void) { return m.st != MS_OFF; }

/* the HUD Blanker White (Blanker Control): FADE IN to white, FADE OUT from it, linearly over its Fade Time */
static struct {
  float alpha, from, to, t, time;
  bool on;
} wb = {0, 0, 0, 0, 1, false};

void white_blanker_time(float t) { wb.time = t; }

void white_blanker_fade(bool in) {
  wb.from = in ? 0 : 1, wb.to = in ? 1 : 0, wb.t = 0, wb.on = true;
  wb.alpha = wb.from;
}

/* the Prompt Blanker (its FSM: UP to black at 2/3 in 0.85 s, DOWN to none in 0.85 s, linearly) and the focus tutorial
 * (Focus_prompt_temp's ColorFaders: up after 3 s in 1 s, down in 0.15 s) */
typedef struct {
  float alpha, from, to, t, time, delay;
} Fade;
static Fade pb, fp;

static void fade_start(Fade *f, float to, float time, float delay) {
  f->from = f->alpha, f->to = to, f->t = 0, f->time = time, f->delay = delay;
}
static void fade_tick(Fade *f) {
  if (f->alpha == f->to) return;
  /* (the focus tutorial: not up before the tablet's text is on its last page; the game's holds it on one page, the
   * bigger text here on two) */
  if (f == &fp && f->to > f->alpha && f->t >= f->delay && !dialogue_last_page()) return;
  f->t += DT;
  float k = f->time > 0 ? (f->t - f->delay) / f->time : 1;
  if (k > 0) f->alpha = f->from + (f->to - f->from) * (k > 1 ? 1 : k);
}

void prompt_blanker(bool up) { fade_start(&pb, up ? 0.6667f : 0, 0.85f, 0); }

/* the first map's lesson (First Map: FIRST MAP UP, DOWN): its parts up in 1 s after half a second, the stop after 2 in
 * 0.1; down in 1 (the stop 0.1) */
static Fade fm, fm_stop;
void first_map_prompt(bool up) {
  fade_start(&fm, up ? 1 : 0, 1, up ? 0.5f : 0);
  fade_start(&fm_stop, up ? 1 : 0, 0.1f, up ? 2 : 0);
}
void focus_prompt_fade(bool up) { fade_start(&fp, up ? 1 : 0, up ? 1 : 0.15f, up ? 3 : 0); }

void white_blanker_reset(void) {
  wb.on = false, wb.alpha = 0, wb.time = 1;
  memset(&pb, 0, sizeof pb), memset(&fp, 0, sizeof fp), memset(&fm, 0, sizeof fm), memset(&fm_stop, 0, sizeof fm_stop);
}

static void white_blanker_tick(void) {
  if (!wb.on) return;
  wb.t += DT;
  float k = wb.time > 0 ? wb.t / wb.time : 1;
  wb.alpha = wb.from + (wb.to - wb.from) * (k > 1 ? 1 : k);
  if (k >= 1 && wb.to == 0) wb.on = false;
}

void msg_tick(void) {
  white_blanker_tick();
  fade_tick(&pb), fade_tick(&fp), fade_tick(&fm), fade_tick(&fm_stop);
  notices_tick();
  if (m.st == MS_OFF) return;
  uint32_t pressed = g_hero.keys & ~m.prev_keys;
  m.prev_keys = g_hero.keys;
  m.t += DT;
  for (int i = 0; i < NF; i++) {
    if (m.down) {
      /* DOWN: to nothing in 1 s (the stop marker 0.15 s) */
      m.a[i] -= DT / (i == F_STOP ? 0.15f : 1);
      if (m.a[i] < 0) m.a[i] = 0;
    } else if (m.t_up[i] >= 0) {
      m.t_up[i] += DT;
      float k = (m.t_up[i] - up_delay[i]) / 1;
      if (k > 0) m.a[i] = k > 1 ? 1 : k;
    }
  }
  switch (m.st) {
    case MS_TOP_UP:
      if (m.t >= 2.5f) {
        /* the fleur (all at once), Bot Up: how to use it */
        m.a[F_FLEUR] = 1;
        m.t_up[F_PRESS] = m.t_up[F_MSG1] = m.t_up[F_MSG2] = m.t_up[F_BUTTON] = 0;
        m.st = MS_BOT_UP, m.t = 0;
      }
      break;
    case MS_BOT_UP:
      if (m.t >= 3) m.a[F_STOP] = 1, m.st = MS_STOP_UP, m.t = 0;
      break;
    case MS_STOP_UP:
      if (m.t >= 0.25f) m.st = MS_DETECT;
      break;
    case MS_DETECT:
      if (pressed & (K_SPELL | K_JUMP | K_ATTACK)) m.down = true, m.st = MS_DOWN, m.t = 0;
      break;
    case MS_DOWN:
      if (m.t >= 1) {
        m.st = MS_OFF;
        vm_broadcast(VMEV_GET_ITEM_MSG_END);
      }
      break;
  }
}

/* a text: its lines centered on x (align 0) or ending at x (1); the first's top or middle at y -> its width (pixels) */
static float line(int text, int style, float x, float y, int align, bool top, float a) {
  uint32_t al = (uint32_t)(a * 255 + 0.5f);
  if (text < 0) return 0;
  const uint8_t *s = text_get(text), *st = font_style(style);
  float py = VIEW_H / 2 - y * HUD_PX + (top ? font_asc(st) : (font_asc(st) - font_desc(st)) / 2), wmax = 0;
  for (int i = 0;;) {
    int e = i;
    while (s[e] && s[e] != TEXT_BR) e++;
    float w = text_width(style, s + i, e - i);
    if (w > wmax) wmax = w;
    if (al) gfx_text(style, VIEW_W / 2 + x * HUD_PX - (align == 0 ? w / 2 : align == 1 ? w : 0), py, s + i, e - i, al << 24 | 0xFFFFFF, 0);
    if (!s[e]) break;
    i = e + 1, py += font_line(st);
  }
  return wmax;
}

static void sprite_k(int sprite, float x, float y, float k, float a, int slot) {
  uint8_t al = (uint8_t)(a * 255 + 0.5f);
  if (!al) return;
  Inst in;
  sprite_inst(sprite, x, y, 0, k, k, gfx_dyn_tint(slot, 255, 255, 255, al), &in);
  gfx_hud(&in, 0);
}

static void sprite(int sprite, float x, float y, float a, int slot) {
  uint8_t al = (uint8_t)(a * 255 + 0.5f);
  if (!al) return;
  Inst in;
  sprite_inst(sprite, x, y, 0, 1, 1, gfx_dyn_tint(slot, 255, 255, 255, al), &in);
  gfx_hud(&in, 0);
}

/* the HUD Blanker: black over the screen (under the message), its color's alpha as the scripts set it */
static float blank_alpha;
static bool blank_on;
void blanker_set(float alpha, bool on) { blank_alpha = alpha, blank_on = on; }

/* ---------------------------------------------------------------- notices, the charm tutorial */
/* (Relic Msg, Charm Msg: at (-11.91, -6.22) on the HUD, up 4.25 s then down; their parts fade up in 0.3 s, down in 0.2.
 * Charm Tute Msg: at (0, 2.77); its parts (color_fader) up in 1 s after their delays, its stop at 5 s, down in 1 s as
 * it is closed. A new one sends the others away: DESTROY JOURNAL MSG) */
typedef struct {
  int16_t sprite;
  float kx, ky;   /* (its x and y scales from the one it was made at) */
} Piece;
static const Piece notice_bb = NOTICE_BACKBOARD, tute_fleur = TUTE_FLEUR, tute_bb = TUTE_BACKBOARD, tute_image = TUTE_IMAGE,
                   tute_stop = TUTE_STOP;
static const int16_t notice_icons[] = NOTICE_ICONS, charm_icons[41] = CHARM_ICONS, charm_names[41] = CHARM_NAMES;
enum { NT_OFF, NT_RELIC, NT_CHARM, NT_TUTE };
static struct {
  uint8_t kind;
  int16_t icon, text, next_icon;
  float t, t_down;   /* (time up; time since it went down: -1 not yet) */
  bool input;        /* (the tutorial's Input: a key sends it down, then the Knight's control back, as a shop's) */
  uint32_t prev_keys;
} nt = {NT_OFF, -1, -1, -1, 0, -1, false, 0};

static void notices_off(void) { nt.kind = NT_OFF; }
void notices_close(void) { notices_off(); }

void notice_icon(int k) { nt.next_icon = k >= 0 && k < (int)(sizeof notice_icons / sizeof notice_icons[0]) ? notice_icons[k] : -1; }

static void notice_start(int kind, int icon, int text) {
  m.st = MS_OFF;   /* (the item message: gone, as a new one shows) */
  nt.kind = (uint8_t)kind, nt.icon = (int16_t)icon, nt.text = (int16_t)text, nt.t = 0, nt.t_down = -1, nt.input = false;
}

void notice_show(int text) { notice_start(NT_RELIC, nt.next_icon, text); }

void charm_notice(int id) {
  if (id < 0 || id > 40) return;
  /* (in a tutorial just made: its own Charm Get Msg's charm) */
  if (nt.kind == NT_TUTE && nt.t == 0) nt.icon = charm_icons[id], nt.text = charm_names[id];
  else notice_start(NT_CHARM, charm_icons[id], charm_names[id]);
}

void charm_tute(void) { notice_start(NT_TUTE, -1, -1); }

/* (a shop's: Input on; the shop region off until it is gone, SHOP REGION INACTIVE, then ACTIVE) */
void charm_tute_input(void) {
  static const uint8_t inactive[NUM_ROOMS] = VMEV_SHOP_REGION_INACTIVE;
  notice_start(NT_TUTE, -1, -1);
  nt.input = true, nt.prev_keys = g_hero.keys;
  if (inactive[g_room.id] != 255) vm_broadcast(inactive[g_room.id]);
}

void charm_tute_close(void) {
  if (nt.kind == NT_TUTE && nt.t_down < 0) nt.t_down = 0;
}

static void notices_tick(void) {
  if (nt.kind == NT_OFF) return;
  uint32_t pressed = g_hero.keys & ~nt.prev_keys;
  nt.prev_keys = g_hero.keys;
  nt.t += DT;
  if (nt.t_down >= 0) nt.t_down += DT;
  if (nt.kind != NT_TUTE) {
    if (nt.t >= 4.25f && nt.t_down < 0) nt.t_down = 0;
    if (nt.t_down >= 1) nt.kind = NT_OFF;
    return;
  }
  /* (Stop, Wait For Input: the inventory, a spell, the nail or a jump sends it Down) */
  if (nt.input && nt.t >= 5 && nt.t_down < 0 && (pressed & (K_INV | K_SPELL | K_ATTACK | K_JUMP))) nt.t_down = 0;
  if (nt.t_down >= 1) {
    nt.kind = NT_OFF;
    if (nt.input) {
      /* (Regain Control?, Destroy: SHOP REGION ACTIVE) */
      static const uint8_t active[NUM_ROOMS] = VMEV_SHOP_REGION_ACTIVE;
      hero_regain_control();
      g_pd.disable_pause = false;
      hero_start_anim_control();
      if (active[g_room.id] != 255) vm_broadcast(active[g_room.id]);
    }
  }
}

/* a part's alpha: up over `up` after `delay`, down over `down` once it went down */
static float fader(float delay, float up, float down) {
  float a = (nt.t - delay) / up;
  a = a < 0 ? 0 : a > 1 ? 1 : a;
  if (nt.t_down >= 0) {
    float d = 1 - nt.t_down / down;
    if (d < a) a = d < 0 ? 0 : d;
  }
  return a;
}

static void piece(const Piece *p, float x, float y, float a, int slot) {
  uint8_t al = (uint8_t)(a * 255 + 0.5f);
  if (!al || p->sprite < 0) return;
  Inst in;
  sprite_inst(p->sprite, x, y, 0, p->kx, p->ky, gfx_dyn_tint(slot, 255, 255, 255, al), &in);
  gfx_hud(&in, 0);
}

static void notices_draw(void) {
  if (nt.kind == NT_OFF) return;
  if (nt.kind != NT_TUTE) {
    const float x = -11.91f, y = -6.22f;
    float a = fader(0, 0.3f, 0.2f);
    piece(&notice_bb, x + 2.82f, y - 0.12f, a * 0.797f, 22);
    /* (a charm's icon: made larger, for the inventory) */
    if (nt.icon >= 0) sprite_k(nt.icon, x - 0.46f, y - 0.05f, nt.kind == NT_CHARM ? 1 / CHARM_ICON_K : 1, a, 23);
    /* (its Text: left-aligned in its box, 6.12 wide about (3.64, -0.03)) */
    line(nt.text, STYLE_NOTICE, x + 3.64f - 3.06f, y - 0.03f, 2, false, a);
    return;
  }
  const float x = 0, y = 2.77f;
  float a0 = fader(0, 1, 1), a1 = fader(1, 1, 1), a3 = fader(3, 1, 1);
  piece(&tute_bb, x, y - 0.53f, a0, 22);
  piece(&tute_fleur, x, y + 2.06f, a0, 24);
  /* (its Charm Get Msg: no backboard) */
  if (nt.icon >= 0) sprite_k(nt.icon, x - 2.9f, y + 0.83f, 1 / CHARM_ICON_K, a0, 23);
  line(nt.text, STYLE_NOTICE, x + 1.2f - 3.06f, y + 0.85f, 2, false, a0);
  /* (Title, Subtitle, Text: their tops at their rects' tops, 4.02 high) */
  line(TXT_CHARM_TUTE_TITLE, STYLE_TUTE_TITLE, x, y + 1.55f + 2.01f, 0, true, a0);
  line(TXT_CHARM_TUTE_SUB, STYLE_TUTE, x, y - 2.29f + 2.01f, 0, true, a1);
  piece(&tute_image, x, y - 4.51f, a3, 25);
  line(TXT_CHARM_REMINDER, STYLE_TUTE, x, y - 9.59f + 2.01f, 0, true, a3);
  if (nt.t >= 5 && nt.t_down < 0) piece(&tute_stop, x + 0.05f, y - 9.19f, 1, 26);
}

/* the key's box (the calculator's key, by name: as wide as the name), its left at x */
static void key_box(int text, float x, float y, float a, int slot) {
  float w = line(text, STYLE_PROMPT, 0, y, 0, false, 0) / HUD_PX + 0.5f, kx = x + w / 2;
  uint8_t al = (uint8_t)(a * 255 + 0.5f);
  if (!al) return;
  Inst in;
  sprite_inst(SPRITE_MSG_KEY, kx, y, 0, w / 0.95f, 1, gfx_dyn_tint(slot, 255, 255, 255, al), &in);
  gfx_hud(&in, 0);
  line(text, STYLE_PROMPT, kx, y, 0, false, a);
}

static void first_map_draw(void) {
  static const struct { int16_t text; float x, top; } rows[4] = FIRSTMAP_TEXTS;
  static const struct { int16_t sprite; float x, y, kx, ky; } parts[3] = FIRSTMAP_PIECES;
  static const struct { int16_t text; float x, y; } key = FIRSTMAP_KEY;
  float a = fm.alpha;
  if (a <= 0 && fm_stop.alpha <= 0) return;
  if (a > 0) gfx_hud_fill(-15, -9, 15, 9, gfx_dyn_tint(26, 0, 0, 0, (uint8_t)(FIRSTMAP_BG_A * a * 255 + 0.5f)));
  for (int i = 0; i < 3; i++) {
    float pa = i == 2 ? fm_stop.alpha : a;
    uint8_t al = (uint8_t)(pa * 255 + 0.5f);
    if (!al) continue;
    Inst in;
    sprite_inst(parts[i].sprite, parts[i].x, parts[i].y, 0, parts[i].kx, parts[i].ky, gfx_dyn_tint(29 + i, 255, 255, 255, al), &in);
    gfx_hud(&in, 0);
  }
  static const int styles[4] = {STYLE_MSG_NAME, STYLE_MSG, STYLE_MSG, STYLE_MSG};
  for (int i = 0; i < 4; i++) {
    int style = styles[i];   /* (a size smaller where the bigger text would run off the screen) */
    while (style != STYLE_MSG_NAME && style != STYLE_MSG_XS && line(rows[i].text, style, 0, 0, 0, true, 0) > VIEW_W - 8)
      style = style == STYLE_MSG ? STYLE_MSG_S : STYLE_MSG_XS;
    line(rows[i].text, style, rows[i].x, rows[i].top, 0, true, a);
  }
  /* (the key: its box about its place) */
  float w = line(key.text, STYLE_PROMPT, 0, key.y, 0, false, 0) / HUD_PX + 0.5f;
  key_box(key.text, key.x - w / 2, key.y, a, 25);
}

static void focus_prompt_draw(void) {
  static const struct { int16_t sprite; float x, y, kx, ky; } bg = FOCUS_PROMPT_BG;
  static const struct { int16_t text; float x, top; int8_t align; } rows[4] = FOCUS_PROMPT_TEXTS;
  static const struct { int16_t text; float x, y; } key = FOCUS_PROMPT_KEY;
  float a = fp.alpha;
  if (a <= 0) return;
  Inst in;
  sprite_inst(bg.sprite, bg.x, bg.y, 0, bg.kx, bg.ky, gfx_dyn_tint(29, 255, 255, 255, (uint8_t)(a * 255 + 0.5f)), &in);
  gfx_hud(&in, 0);
  /* (its lines in the message's smallest size, still bigger than the game's: each a line under the one above at
   * least, all over the tablet's stop below them; the key centred on its line, as the game's on its) */
  const uint8_t *st = font_style(STYLE_MSG_XS);
  float top = rows[0].top, key_top = top;
  for (int i = 0; i < 4; i++) {
    if (i) top = fminf(rows[i].top, top - font_line(st) / HUD_PX);
    if (rows[i].align == 1) key_top = top;   /* (HOLD, the key after it) */
    line(rows[i].text, STYLE_MSG_XS, rows[i].x, top, rows[i].align, true, a);
  }
  key_box(key.text, key.x, key_top - (font_asc(st) + font_desc(st)) / 2 / HUD_PX, a, 30);
}

void msg_draw(void) {
  if (wb.on && wb.alpha > 0)
    gfx_hud_fill(-15, -9, 15, 9, gfx_dyn_tint(28, 255, 255, 255, (uint8_t)(wb.alpha * 255 + 0.5f)));
  /* (the HUD Blanker and the Prompt Blanker: black, the one over the other) */
  float ba = 1 - (1 - (blank_on && blank_alpha > 0 ? blank_alpha : 0)) * (1 - pb.alpha);
  if (ba > 0) gfx_hud_fill(-15, -9, 15, 9, gfx_dyn_tint(27, 0, 0, 0, (uint8_t)(ba * 255 + 0.5f)));
  focus_prompt_draw();   /* (while the menus cannot be up: their tints') */
  first_map_draw();
  notices_draw();
  if (m.st == MS_OFF) return;
  const int16_t *t = table[m.item];
  /* (BG: black, at 0.766 of its color's alpha) */
  sprite(SPRITE_MSG_BG, 0, 0, m.a[F_BG] * 0.766f, 22);
  sprite(icons[m.item], 0, 4.73f, m.a[F_ICON], 23);
  sprite(SPRITE_MSG_FLEUR, 0, -1.48f, m.a[F_FLEUR], 24);
  sprite(SPRITE_MSG_STOP, 0, -6.84f, m.a[F_STOP], 26);
  line(t[0], STYLE_MSG_NAME, 0, -0.23f, 0, false, m.a[F_NAME]);
  line(t[1], STYLE_MSG, 0, 0.84f, 0, false, m.a[F_PREFIX]);
  line(t[2], STYLE_MSG, -8.5f + 11.1377f * 1.4407f / 2, -2.93f, 1, false, m.a[F_PRESS]);
  line(t[3], STYLE_MSG, 0, -4.64f + 1.0253f * 1.4407f / 2, 0, true, m.a[F_MSG1]);
  line(t[4], STYLE_MSG, 0, -6.15f + 1.7591f * 1.4407f / 2, 0, true, m.a[F_MSG2]);
  /* the key (the calculator's, by name: its key sprite as wide as the name) */
  key_box(t[5], 0.53f - 0.475f, -2.93f, m.a[F_BUTTON], 25);
}
