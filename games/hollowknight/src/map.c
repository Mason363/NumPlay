/* The map: the Knight's Map Control FSM (the map key held on the ground: the map out, walking with it; tapped twice:
 * the inventory's), the HUD's Quick Map (its backdrop, the area's name, or no map) and Game_Map, the area's part shown:
 * its rooms as Cornifer drew them, rough until the quill maps them, their pins, the arrows to the areas next to them,
 * the compass (Wayward Compass: where the Knight is), the shade's mark. The rooms visited, mapped, where a cocoon was
 * broken: PlayerData's (scenesVisited, scenesMapped, scenesEncounteredCocoon). */
#pragma GCC optimize("Os")   /* (its code small: not where a frame's time goes) */
#include <math.h>
#include "game.h"

#define DT 0.02f
#define BUTTON_DOWN_TIME 0.1f   /* (Button Down Time) */
#define DOUBLE_TIME 0.25f       /* (Check Double's wait; Close Doub Check's open time) */
#define WALK_SPEED 4.0f
#define HUD_PX (FOCAL / (-1.342f - CAM_Z))

typedef struct { uint8_t first, n; int16_t have; float qx, qy; int16_t title; uint32_t zones; } MapArea;
typedef struct { uint8_t area, room; int16_t sprite, full; float x, y, w, h; uint32_t rgb; uint8_t rough; } MapRoom;
typedef struct { uint8_t room, kind; int16_t sprite; float x, y; int16_t f1, f2; } MapPin;
typedef struct { uint8_t room; int16_t sprite; float x, y, ang; int16_t text; float tx, top; uint8_t align; int16_t visited; } MapNext;
typedef struct { int8_t area, room; int16_t text; float x, top; uint32_t rgb; uint8_t align, world_only; } MapName;
static const MapArea areas[] = MAP_AREAS;
static const MapRoom rooms[] = MAP_ROOMS;
static const MapPin pins[] = MAP_PINS;
static const MapNext nexts[] = MAP_NEXT;
static const MapName names[] = MAP_NAMES;
#define NAREAS (int)(sizeof areas / sizeof areas[0])
#define NMROOMS (int)(sizeof rooms / sizeof rooms[0])

enum {
  MC_INACTIVE, MC_BUTTON_DOWN, MC_CHECK_DOUBLE, MC_OPEN, MC_IDLE, MC_TURN, MC_WALK, MC_CLOSE, MC_CLOSE_DOUBLE_FAIL
};
static struct {
  uint8_t st;
  float t, open_time;
  bool in_control, walk_right, walking;
  /* the quick map: shown (and its area), the backdrop's, the name's, the no map message's fades */
  bool shown;
  bool pins_hidden;    /* (the HUD camera's culling mask without the pins' layer: the map's key's None) */
  int8_t area;         /* (-1: no map of it) */
  float bg_a, name_a, nomap_a, t_open;
  Anim compass;
} mc;

static bool held(void) { return g_hero.keys & K_MAP; }
static bool pressed(void) { return (g_hero.keys & K_MAP) && !(g_hero.prev_keys & K_MAP); }

/* ---------------------------------------------------------------- PlayerData's lists */
static bool bit(const uint8_t *b, int i) { return i >= 0 && i < MAX_ROOM_BITS && (b[i >> 3] >> (i & 7) & 1); }
static void set_bit(uint8_t *b, int i) {
  if (i >= 0 && i < MAX_ROOM_BITS) b[i >> 3] |= (uint8_t)(1 << (i & 7));
}

void map_room_entered(void) { set_bit(g_pd.rooms_visited, ROOM_BASE + g_room.id); }   /* (SceneManager: scenesVisited) */
void map_scene_visited(int r) { set_bit(g_pd.rooms_visited, r); }          /* (AddToScenesVisited: a map piece) */
void map_cocoon_broken(void) { set_bit(g_pd.rooms_cocoon, ROOM_BASE + g_room.id); }   /* (AddToCocoonList) */

static bool have_area(int a) { return areas[a].have == -1 || (areas[a].have >= 0 && pd_flag(areas[a].have)); }

/* UpdateGameMap: with the quill, the rooms visited whose area's map the Knight has, mapped -> any new */
bool map_update(void) {
  if (!pd_flag(PDF_HAS_QUILL)) return false;
  bool any = false;
  for (int i = 0; i < NMROOMS; i++) {
    int r = rooms[i].room;
    if (r == 255 || !bit(g_pd.rooms_visited, r) || bit(g_pd.rooms_mapped, r) || !have_area(rooms[i].area)) continue;
    set_bit(g_pd.rooms_mapped, r);
    any = true;
  }
  return any;
}

/* ---------------------------------------------------------------- Map Control */
static struct {
  bool on;
  float t, a, quill_a;
  Anim quill;
} mm;   /* (the map's update message: below) */
static void quick_open(void);
static void quick_close(void);

void map_reset(void) {
  memset(&mc, 0, sizeof mc);
  mc.compass.sprite = -1;
  memset(&mm, 0, sizeof mm);
  mm.quill.sprite = -1;
}

static bool can_quick_map(void) { return hero_can_quick_map(); }

static void regain(void) {
  /* Regain Control (not at a bench) */
  g_pd.disable_pause = false;
  if (!pd_flag(PDF_AT_BENCH)) {
    hero_regain_control();
    hero_start_anim_control();
  }
  mc.in_control = false;
  mc.st = MC_INACTIVE, mc.open_time = 0;
}

static void open_map(void) {
  /* Open Map: the Knight's control taken, Map Open */
  mc.open_time = 0;
  g_pd.disable_pause = true;
  quick_open();
  mc.in_control = true;
  hero_relinquish_control();
  hero_stop_anim_control();
  anim_play_from_frame(&g_hero.anim, CLIP_KNIGHT_MAP_OPEN, 0);
  mc.st = MC_OPEN;
}

static void close_map(void) {
  /* Close Map: Map Away */
  quick_close();
  g_hero.body.vx = 0;
  mc.walking = false;
  anim_play_from_frame(&g_hero.anim, CLIP_KNIGHT_MAP_AWAY, 0);
  mc.st = MC_CLOSE;
}

static void idle(void) {
  g_hero.body.vx = 0;
  mc.walking = false;
  anim_play(&g_hero.anim, CLIP_KNIGHT_MAP_IDLE);
  mc.st = MC_IDLE;
}

static void open_inventory_map(void) {
  /* (Double!: OPEN INVENTORY MAP) */
  inv_open_map();
}

/* (HERO DAMAGED, LEAVING SCENE, FSM CANCEL: Cancel All) */
void map_cancel(void) {
  if (mc.st == MC_INACTIVE) return;
  quick_close();
  bool in_control = mc.in_control;
  mc.st = MC_INACTIVE, mc.open_time = 0, mc.walking = false, mc.in_control = false;
  if (in_control) {
    g_pd.disable_pause = false;
    hero_start_anim_control();
  }
}

void map_tick(void) {
  bool down = pressed(), on = held();
  bool ended = g_hero.anim.events & ANIM_DONE;
  /* (Open time: counted idle and with the map out; from naught as it is opened) */
  if (mc.st == MC_INACTIVE || (mc.st >= MC_OPEN && mc.st <= MC_WALK)) mc.open_time = fminf(3, mc.open_time + DT);
  /* (leaving the ground: Quick Cancel) */
  if (mc.st >= MC_OPEN && mc.st <= MC_WALK && !g_hero.cs.on_ground) {
    quick_close();
    regain();
    return;
  }
  switch (mc.st) {
    case MC_INACTIVE:
      if (!down) break;
      /* Button Down Check */
      if (g_pd.disable_pause || !pd_flag(PDF_HAS_MAP) || pd_flag(PDF_AT_BENCH) || !can_quick_map()) break;
      mc.st = MC_BUTTON_DOWN, mc.t = 0;
      break;
    case MC_BUTTON_DOWN:
      if (down) {
        open_inventory_map();
        mc.st = MC_INACTIVE, mc.open_time = 0;
        break;
      }
      if ((mc.t += DT) < BUTTON_DOWN_TIME) break;
      /* Button Still Down?, Can QuickMap?, Has Map? */
      if (on && !g_pd.disable_pause && can_quick_map() && pd_flag(PDF_HAS_MAP)) open_map();
      else if (on) mc.st = MC_INACTIVE, mc.open_time = 0;
      else mc.st = MC_CHECK_DOUBLE, mc.t = 0;
      break;
    case MC_CHECK_DOUBLE:
      if (down && !g_pd.disable_pause && can_quick_map()) {
        open_inventory_map();
        mc.st = MC_INACTIVE, mc.open_time = 0;
      } else if ((mc.t += DT) >= DOUBLE_TIME)
        mc.st = MC_INACTIVE, mc.open_time = 0;
      break;
    case MC_OPEN:
      if (!on) close_map();
      else if (ended || !g_hero.anim.playing) idle();
      break;
    case MC_IDLE:
      if (!on) {
        close_map();
        break;
      }
      if (g_hero.keys & (K_LEFT | K_RIGHT)) {
        /* Turn R?, Turn L?: turned already, walking; else Map Turn first */
        mc.walk_right = (g_hero.keys & K_RIGHT) != 0;
        g_hero.body.vx = 0;
        if (g_hero.cs.facing_right == mc.walk_right) {
          mc.st = MC_WALK;
          anim_play(&g_hero.anim, CLIP_KNIGHT_MAP_WALK);
        } else {
          anim_play_from_frame(&g_hero.anim, CLIP_KNIGHT_MAP_TURN, 0);
          mc.st = MC_TURN;
        }
      }
      break;
    case MC_TURN:
      if (!on) close_map();
      else if (ended || !g_hero.anim.playing) {
        mc.st = MC_WALK;
        anim_play(&g_hero.anim, CLIP_KNIGHT_MAP_WALK);
      }
      break;
    case MC_WALK:
      if (!on) {
        close_map();
        break;
      }
      if (!(g_hero.keys & (mc.walk_right ? K_RIGHT : K_LEFT))) {
        idle();
        break;
      }
      /* (MAPWALK START; FaceRight, FaceLeft; its speed each frame) */
      mc.walking = true;
      hero_face(mc.walk_right);
      g_hero.body.vx = mc.walk_right ? WALK_SPEED : -WALK_SPEED;
      break;
    case MC_CLOSE:
      if (down) {
        /* Close Doub Check: soon after it was opened, the inventory's map; else the Map Away goes on */
        if (mc.open_time <= DOUBLE_TIME) {
          anim_play(&g_hero.anim, CLIP_KNIGHT_IDLE);
          open_inventory_map();
          mc.in_control = false;
          g_pd.disable_pause = false;
          hero_regain_control();
          hero_start_anim_control();
          mc.st = MC_INACTIVE, mc.open_time = 0;
          break;
        }
        mc.st = MC_CLOSE_DOUBLE_FAIL;
      }
      if (ended || !g_hero.anim.playing) regain();
      break;
    case MC_CLOSE_DOUBLE_FAIL:
      if (ended || !g_hero.anim.playing) regain();
      break;
  }
}

/* ---------------------------------------------------------------- the quick map */
static void quick_open(void) {
  /* Open: the backdrop up; 0.2 s, then the area's map and name (no map: its message) */
  mc.shown = true, mc.t_open = 0, mc.area = -2;
  anim_play(&mc.compass, CLIP_MAPUI_IDLE);
}

static void quick_close(void) {
  /* Close: the map gone at once; the name, the backdrop, the message fade */
  mc.shown = false;
}

static int area_of_zone(void) {
  for (int a = 0; a < NAREAS; a++)
    if (areas[a].zones >> g_pd.map_zone & 1) return a;
  return -1;
}

static void map_msg_tick(void);

void map_hud_tick(void) {
  map_msg_tick();
  /* (FadeGroups: the backdrop in 0.4 s, out 0.25; the name in 0.4, out 0.3; the message in 0.3, out 0.25) */
  if (mc.shown) {
    mc.t_open += DT;
    mc.bg_a = fminf(1, mc.bg_a + DT / 0.4f);
    if (mc.t_open >= 0.2f && mc.area == -2) {
      int a = area_of_zone();
      mc.area = (int8_t)(a >= 0 && have_area(a) ? a : -1);
    }
    if (mc.area >= 0) mc.name_a = fminf(1, mc.name_a + DT / 0.4f);
    else if (mc.area == -1) mc.nomap_a = fminf(1, mc.nomap_a + DT / 0.3f);
    mc.compass.events = 0;
    anim_play(&mc.compass, mc.walking ? CLIP_MAPUI_WALK : CLIP_MAPUI_IDLE);
    anim_update(&mc.compass, DT);
  } else {
    mc.bg_a = fmaxf(0, mc.bg_a - DT / 0.25f);
    mc.name_a = fmaxf(0, mc.name_a - DT / 0.3f);
    mc.nomap_a = fmaxf(0, mc.nomap_a - DT / 0.25f);
  }
}

/* the map's colors: a tint each, afresh each frame: the quick map's on the HUD (the menus', the item message's: never up
 * with it); the inventory's (its overlay: each sprite's colors made as it is drawn) its own, times its pane's alpha */
static uint32_t tint_used[6];
static int tint_n;
static bool to_overlay;   /* (drawing the inventory's map pane) */
static float pane_a;
static uint8_t map_tint(uint32_t rgb, uint8_t a) {
  static const uint8_t hud_slots[] = {29, 30, 31, 25}, pane_slots[] = {15, 22, 23, 24, 25, 26};
  const uint8_t *slots = to_overlay ? pane_slots : hud_slots;
  int max = to_overlay ? (int)sizeof pane_slots : (int)sizeof hud_slots;
  if (to_overlay) a = (uint8_t)(a * pane_a + 0.5f);
  else if (rgb == 0xFFFFFF && a == 255) return gfx_dyn_tint(16, 255, 255, 255, 255);   /* (the HUD's white) */
  uint32_t key = rgb << 8 | a;
  int i = 0;
  while (i < tint_n && tint_used[i] != key) i++;
  if (i == tint_n) {
    if (tint_n < max) tint_n++;
    else i = max - 1;
    tint_used[i] = key;
  }
  return gfx_dyn_tint(slots[i], (uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb, a);
}

static void hud_sprite(int sprite, float x, float y, float k, float deg, uint8_t tint) {
  if (sprite < 0) return;
  Inst in;
  if (deg != 0) sprite_inst_rot(sprite, x, y, 0, k, k, deg, tint, &in);
  else sprite_inst(sprite, x, y, 0, k, k, tint, &in);
  if (to_overlay) gfx_overlay(&in);
  else gfx_hud(&in, 0);
}

/* a text: its lines centred on x, from its left or to its right (align 0 left, 1 centred, 2 right), its top at top */
static void hud_text(int text, int style, float x, float top, int align, uint32_t rgb, float a) {
  if (to_overlay) a *= pane_a;
  uint8_t al = (uint8_t)(a * 255 + 0.5f);
  if (text < 0 || !al) return;
  const uint8_t *s = text_get(text), *st = font_style(style);
  float py = VIEW_H / 2 - top * HUD_PX + font_asc(st);
  for (int i = 0;;) {
    int e = i;
    while (s[e] && s[e] != TEXT_BR) e++;
    float w = text_width(style, s + i, e - i);
    gfx_text(style, VIEW_W / 2 + x * HUD_PX - (align == 1 ? w / 2 : align == 2 ? w : 0), py, s + i, e - i,
             (uint32_t)al << 24 | rgb, 0);
    if (!s[e]) break;
    i = e + 1, py += font_line(st);
  }
}

/* a next area's name (centered at x, its top): its edge by the arrow where the game has it, the text being bigger
 * than the one its place was made for (tools/gamemap.py: the message's size then, MSG_LAYOUT_EM pixels an em) */
#define MSG_LAYOUT_EM 8.23f
#define NEXT_MARGIN 0.25f   /* (a name kept this far in from the screen's side, ...) */
#define NEXT_UNDER 0.45f    /* (... its top this far under its arrow) */
static void next_name_at(const MapNext *n, float ox, float oy, float k, float *x, float *top) {
  const uint8_t *st = font_style(STYLE_MSG), *s = text_get(n->text);
  float r = MSG_LAYOUT_EM / font_em(st);
  float w = text_width(STYLE_MSG, s, (int)strlen((const char *)s)) / HUD_PX, h = (font_asc(st) + font_desc(st)) / HUD_PX;
  float w0 = w * r, h0 = h * r, t0 = oy + n->top * k;
  float cx0 = ox + n->tx * k + (n->align == 0 ? w0 / 2 : n->align == 2 ? -w0 / 2 : 0), cy0 = t0 - h0 / 2;
  float dx = cx0 - (ox + n->x * k), dy = cy0 - (oy + n->y * k);
  *x = cx0, *top = cy0 + h / 2;
  if (fabsf(dx) > fabsf(dy)) *x = dx > 0 ? cx0 - w0 / 2 + w / 2 : cx0 + w0 / 2 - w / 2;   /* (beside it: that side kept) */
  else if (dy > 0) *top = t0 - h0 + h;                                                    /* (over it: its bottom kept) */
  else *top = t0;                                                                         /* (under it: its top kept) */
  /* (past the screen's side: inside it, under the arrow) */
  float edge = VIEW_W / 2 / HUD_PX - NEXT_MARGIN;
  if (*x + w / 2 > edge || *x - w / 2 < -edge) {
    *x = *x + w / 2 > edge ? edge - w / 2 : -edge + w / 2;
    *top = oy + n->y * k - NEXT_UNDER;
  }
}

/* a room of the map shown: rough with its area's map; else once mapped */
static bool room_shown(int i) {
  const MapRoom *r = &rooms[i];
  bool mapped = r->room != 255 && bit(g_pd.rooms_mapped, r->room);
  return r->rough || (mapped && pd_flag(PDF_HAS_QUILL));
}

static bool pin_shown(const MapPin *p) {
  if (p->kind == 1) return pd_flag(PDF_HAS_PIN_COCOON) && rooms[p->room].room != 255 && bit(g_pd.rooms_cocoon, rooms[p->room].room);
  return pd_flag(p->f1) && (p->f2 < 0 || pd_flag(p->f2));
}

/* the area's map at (ox, oy), k HUD units a map unit (its sprites made at the quick map's), the next areas' arrows or
 * not */
static void draw_area(int a, float ox, float oy, float k, bool quick) {
  float sk = k / MAP_QUICK_K;
  for (int i = areas[a].first; i < areas[a].first + areas[a].n; i++) {
    const MapRoom *r = &rooms[i];
    if (!room_shown(i)) continue;
    bool mapped = r->room != 255 && bit(g_pd.rooms_mapped, r->room);
    hud_sprite(mapped && r->full >= 0 ? r->full : r->sprite, ox + r->x * k, oy + r->y * k, sk, 0, map_tint(r->rgb, 255));
  }
  /* (the pins: the HUD camera's layer 30, as Map Key Pref has it) */
  for (unsigned i = 0; i < sizeof pins / sizeof pins[0] && !mc.pins_hidden; i++) {
    const MapPin *p = &pins[i];
    if (rooms[p->room].area != a || !room_shown(p->room) || !pin_shown(p)) continue;
    hud_sprite(p->sprite, ox + p->x * k, oy + p->y * k, sk, 0, map_tint(0xFFFFFF, 255));
  }
  for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++) {
    const MapName *n = &names[i];
    if (n->area != a || (n->room >= 0 && !room_shown(n->room)) || (quick && n->world_only)) continue;
    hud_text(n->text, STYLE_MSG, ox + n->x * k, oy + n->top * k, n->align, n->rgb, 1);
  }
  if (!quick) return;
  for (unsigned i = 0; i < sizeof nexts / sizeof nexts[0]; i++) {
    const MapNext *n = &nexts[i];
    if (rooms[n->room].area != a || !room_shown(n->room) || (n->visited >= 0 && !pd_flag(n->visited))) continue;
    hud_sprite(n->sprite, ox + n->x * k, oy + n->y * k, sk, n->ang, map_tint(0xFFFFFF, 255));
    float x, top;
    next_name_at(n, ox, oy, k, &x, &top);
    hud_text(n->text, STYLE_MSG, x, top, 1, 0xFFFFFF, 1);
  }
}

/* the compass (the Knight's room's place on the map, as far across it as he is across the room) */
static bool compass_at(float ox, float oy, float k, float *x, float *y) {
  for (int i = 0; i < NMROOMS; i++) {
    const MapRoom *r = &rooms[i];
    if (r->room != ROOM_BASE + g_room.id || r->w <= 0 || !g_room.h || g_room.h->w <= 0 || g_room.h->h <= 0) continue;
    *x = ox + (r->x - r->w / 2 + g_hero.body.x / g_room.h->w * r->w) * k;
    *y = oy + (r->y - r->h / 2 + g_hero.body.y / g_room.h->h * r->h) * k;
    return true;
  }
  return false;
}

/* the shade's mark: the middle of the room it is in (PlayerData's shadeMapPos) */
static bool shade_at(float ox, float oy, float k, float *x, float *y) {
  if (!g_pd.soul_limited) return false;
  for (int i = 0; i < NMROOMS; i++) {
    const MapRoom *r = &rooms[i];
    int lr = r->room - ROOM_BASE;   /* (its number in this part) */
    if (r->room == 255 || lr < 0 || lr >= NUM_ROOMS || strcmp(room_name(lr), g_pd.shade_scene)) continue;
    *x = ox + r->x * k, *y = oy + r->y * k;
    return true;
  }
  return false;
}

static void draw_marks(float ox, float oy, float k) {
  static const struct { int16_t sprite; float dx, dy; uint8_t a; } shade[2] = MAP_SHADE;
  static const int16_t markers[4] = MAP_MARKERS;
  float sk = k / MAP_QUICK_K, x, y;
  if (shade_at(ox, oy, k, &x, &y))
    for (int i = 0; i < 2; i++)
      hud_sprite(shade[i].sprite, x + shade[i].dx * k, y + shade[i].dy * k, sk, 0, map_tint(0xFFFFFF, shade[i].a));
  uint8_t white = map_tint(0xFFFFFF, 255);
  for (int c = 0; c < 4; c++)
    for (int m = 0; m < g_pd.markers_placed[c] && m < 6; m++)
      hud_sprite(markers[c], ox + g_pd.markers[c][m][0] * 0.01f * k, oy + g_pd.markers[c][m][1] * 0.01f * k, sk, 0, white);
  if (charm_on(2) && mc.compass.sprite >= 0 && compass_at(ox, oy, k, &x, &y))
    hud_sprite(mc.compass.sprite, x, y, MAP_COMPASS_K * k, 0, white);
}

static void map_msg_draw(void);

void map_draw(void) {
  tint_n = 0, to_overlay = false;
  map_msg_draw();
  if (mc.bg_a <= 0 && mc.name_a <= 0 && mc.nomap_a <= 0) return;
  float bga = MAP_BG_A * mc.bg_a;
  if (bga > 0) gfx_hud_fill(-15, -9, 15, 9, gfx_dyn_tint(27, 0, 0, 0, (uint8_t)(bga * 255 + 0.5f)));
  if (mc.shown && mc.area >= 0) {
    const MapArea *ar = &areas[mc.area];
    draw_area(mc.area, ar->qx, ar->qy, MAP_QUICK_K, true);
    draw_marks(ar->qx, ar->qy, MAP_QUICK_K);
  }
  /* the area's name on its backboard; no map: its symbol and line */
  static const struct { float x, top; } title = MAP_TITLE;
  static const struct { int16_t sprite; float x, y, kx, ky; uint8_t a; } back = MAP_TITLE_BACK;
  if (mc.name_a > 0 && mc.area >= 0) {
    Inst in;
    sprite_inst(back.sprite, back.x, back.y, 0, back.kx, back.ky, map_tint(0xFFFFFF, (uint8_t)(back.a * mc.name_a)), &in);
    gfx_hud(&in, 0);
    hud_text(areas[mc.area].title, STYLE_DIALOGUE, title.x, title.top, 1, 0xFFFFFF, mc.name_a);
  }
  static const struct { int16_t sprite; float x, y; } nomap = MAP_NO_MAP;
  static const struct { int16_t text; float x, top; } nomsg = MAP_NO_MAP_MSG;
  if (mc.nomap_a > 0) {
    hud_sprite(nomap.sprite, nomap.x, nomap.y, 1, 0, map_tint(0xFFFFFF, (uint8_t)(255 * mc.nomap_a)));
    hud_text(nomsg.text, STYLE_DIALOGUE, nomsg.x, nomsg.top, 1, 0xFFFFFF, mc.nomap_a);
  }
}

bool map_shown(void) { return mc.st != MC_INACTIVE && mc.st != MC_BUTTON_DOWN && mc.st != MC_CHECK_DOUBLE; }

/* the quick map from a bench (Bench Control's Map Idle: OPEN QUICK MAP; Close Map: CLOSE QUICK MAP) */
void map_quick(bool open) {
  if (open) quick_open();
  else quick_close();
}

/* ---------------------------------------------------------------- the map's update message */
/* (Map Msg: up (0.3 s: its backboard, its line, its quill writing), 3 s, Map Complete, then down: the backboard and
 * the line in 0.2 s, the quill in 1; gone after 1 s) */
void map_msg_show(void) {
  memset(&mm, 0, sizeof mm);
  mm.on = true;
  anim_play_from_frame(&mm.quill, CLIP_JOURNALMSG_MAP_WRITING, 0);
}

static void map_msg_tick(void) {
  if (!mm.on) return;
  mm.t += DT;
  mm.quill.events = 0;
  anim_update(&mm.quill, DT);
  if (mm.t < 3.3f) {
    mm.a = mm.quill_a = fminf(1, mm.t / 0.3f);
    return;
  }
  if (mm.quill.clip != CLIP_JOURNALMSG_MAP_COMPLETE) anim_play_from_frame(&mm.quill, CLIP_JOURNALMSG_MAP_COMPLETE, 0);
  float d = mm.t - 3.3f;
  mm.a = fmaxf(0, 1 - d / 0.2f), mm.quill_a = fmaxf(0, 1 - d);
  if (d >= 1) mm.on = false;
}

static void map_msg_draw(void) {
  static const struct { float x, y, qx, qy, tx, ty; int16_t text, sprite; float bx, by, kx, ky; uint8_t a; } m = MAP_MSG;
  if (!mm.on) return;
  const float k = MAP_MSG_K;
  Inst in;
  if (mm.a > 0) {
    sprite_inst(m.sprite, m.x + m.bx, m.y + m.by, 0, m.kx, m.ky, map_tint(0xFFFFFF, (uint8_t)(m.a * mm.a)), &in);
    gfx_hud(&in, 0);
    /* (its line: left aligned, its middle on its place) */
    const uint8_t *st = font_style(STYLE_MSG);
    const uint8_t *s = text_get(m.text);
    int n = (int)strlen((const char *)s);
    float py = VIEW_H / 2 - (m.y + m.ty) * HUD_PX + (font_asc(st) - font_desc(st)) / 2;
    gfx_text(STYLE_MSG, VIEW_W / 2 + (m.x + m.tx) * HUD_PX, py, s, n, (uint32_t)(mm.a * 255 + 0.5f) << 24 | 0xFFFFFF, 0);
  }
  if (mm.quill_a > 0 && mm.quill.sprite >= 0) {
    sprite_inst(mm.quill.sprite, m.x + m.qx, m.y + m.qy, 0, k, k, map_tint(0xFFFFFE, (uint8_t)(255 * mm.quill_a)), &in);
    gfx_hud(&in, 0);
  }
  (void)k;
}

/* ---------------------------------------------------------------- the inventory's map pane */
/* (Inventory/Map/World Map's UI Control: the wide map, the area chosen white, the others grey; Map Zoom into Game_Map
 * (WorldMap: all the areas had, the next areas' arrows not), panned (GameMap's Update, KeepWithinBounds); the map's key
 * (Map Key: the pins had, the key and the pins shown or not); the markers' menu (MapMarkerMenu: placed and taken back,
 * six of each kind). Places: the pane's from the inventory's, Game_Map's from the HUD's middle.) */
typedef struct { UiPiece piece; int16_t text; UiText name; float zx, zy, cx, cy, ext[4]; } WidePiece;
#define MAP_NAME_GAP 0.4f   /* (the least room between two areas' names) */
typedef struct { UiPiece back; float key[2]; UiText text; int16_t texts[3]; } MapAction;
typedef struct { UiPiece icon; int16_t text; UiText at; } KeyRow;
static const WidePiece wide[NAREAS] = MAP_WIDE;
static const MapAction act_confirm = MAP_ACT_CONFIRM, act_marker = MAP_ACT_MARKER, act_cancel = MAP_ACT_CANCEL,
                       act_change = MAP_ACT_CHANGE, act_key = MAP_ACT_KEY;
static const KeyRow key_rows[MAP_KEY_N] = MAP_KEY_ROWS;
static const int16_t key_flags[MAP_KEY_N] = MAP_KEY_FLAGS;
static const UiPiece key_bb = MAP_KEY_BB;
static const struct { int16_t sprite; float x, y, k, deg; } pan_arrows[4] = MAP_PAN_ARROWS;
static const UiPiece mm_pieces[4] = MAP_MM_PIECES;   /* (backboard, its shadow, the cursor's back, the cursor) */
static const int16_t mm_markers[4] = MAP_MM_MARKERS;
static const struct { float x, y, x0, dx, y0; } mm_row = MAP_MM_ROW;
static const UiText mm_amount = MAP_MM_AMOUNT;
static const struct { int16_t sprite; float x, y, minx, maxx, miny, maxy, speed, pull; } mm_place = MAP_MM_PLACE;
static const struct { float x, y, k0, k; } zoom = MAP_ZOOM;
static const float pan_base[4] = MAP_PAN;

enum { A_TOWN, A_CROSSROADS, A_GREENPATH };   /* (MAP_AREAS' order) */
enum { MP_WIDE, MP_ZOOM, MP_ZOOMED, MP_ZOOM_OUT, MP_MARKERS, MP_MARKER_CANCEL };
#define ARROW_L (-1)
#define ARROW_R (-2)
#define ZOOM_TIME 0.4f       /* (Map Zoom: iTween 0.4 s easeOutSine; Zoomed In after 0.45) */
#define ZOOM_OUT_TIME 0.25f  /* (Zoom Out: 0.25 s linear; Map Up after 0.1, Map Off 0.15 later, Reset 0.1 later) */
#define MAX_MARKERS 6
static struct {
  int8_t sel;           /* the area chosen (ARROW_L, ARROW_R: the pane's arrows) */
  int8_t hit;           /* the marker under the placement cursor (kind * 8 + its index; -1 none) */
  uint8_t st, marker;   /* (marker: the kind chosen, 0..3: b, r, y, w) */
  float t;              /* (the state's time) */
  float x, y, k, ox, oy;   /* Game_Map's place and scale; zooming out: from where */
  float wide_a, nav_a, key_a, mm_a;   /* (the wide map's pieces; World Map's FadeGroup; Map Key's; Map Markers') */
  float px, py, cur_x;  /* the placement cursor (from the markers' menu's place); the menu's cursor */
  float confirm_t, place_t;   /* (MapMarkerMenu's confirmTimer, placementTimer) */
  bool closed;          /* (the inventory closing: Game_Map's areas gone, the map's key down) */
} mp;

static void select_area(int a) {
  mp.sel = (int8_t)a;
  inv_cursor_arrow(a < 0 ? a : 0);
}

/* UI Control's moves (each way: the areas tried in order, their maps had; then where it goes) */
static int wide_move(int from, uint32_t dir) {
  bool cr = have_area(A_CROSSROADS), gp = have_area(A_GREENPATH);
  switch (from) {
    case A_TOWN:   /* (T Down, T Left, T Right) */
      if (dir & K_DOWN) return cr ? A_CROSSROADS : gp ? A_GREENPATH : A_TOWN;
      if (dir & K_LEFT) return gp ? A_GREENPATH : ARROW_L;
      return dir & K_RIGHT ? ARROW_R : A_TOWN;
    case A_CROSSROADS:   /* (CR Up, CR Left, CR Right, CR Down) */
      if (dir & K_UP) return A_TOWN;
      if (dir & K_LEFT) return gp ? A_GREENPATH : ARROW_L;
      return dir & K_RIGHT ? ARROW_R : A_CROSSROADS;
    case A_GREENPATH:   /* (GP Up, GP Right, GP Left, GP Down) */
      if (dir & K_UP) return A_TOWN;
      if (dir & K_RIGHT) return cr ? A_CROSSROADS : A_TOWN;
      return dir & K_LEFT ? ARROW_L : A_GREENPATH;
    case ARROW_L:   /* (To Map) */
      return dir & K_RIGHT ? (gp ? A_GREENPATH : A_TOWN) : ARROW_L;
    default:   /* (To Map 2) */
      return dir & K_LEFT ? (cr ? A_CROSSROADS : A_TOWN) : ARROW_R;
  }
}

/* (WorldMap's bounds: widened by the areas had, or the one the Knight is in with the compass on) */
static void pan_bounds(float *b) {
  memcpy(b, pan_base, sizeof pan_base);
  int z = area_of_zone();
  for (int a = 0; a < NAREAS; a++) {
    if (!have_area(a) && !(a == z && charm_on(2))) continue;
    const float *e = wide[a].ext;
    b[0] = fminf(b[0], e[0]), b[1] = fmaxf(b[1], e[1]), b[2] = fminf(b[2], e[2]), b[3] = fmaxf(b[3], e[3]);
  }
}

static void keep_within_bounds(void) {
  float b[4];
  pan_bounds(b);
  mp.x = fminf(fmaxf(mp.x, b[0]), b[1]), mp.y = fminf(fmaxf(mp.y, b[2]), b[3]);
}

static bool has_marker(int c) { return pd_flag(PDF_HAS_MARKER_B + c); }
static int spare(int c) { return MAX_MARKERS - g_pd.markers_placed[c]; }
static int marker_kinds(void) { return has_marker(0) + has_marker(1) + has_marker(2) + has_marker(3); }
/* (a kind's place in the menu's row: after the kinds had before it) */
static float marker_x(int c) {
  int n = 0;
  for (int i = 0; i < c; i++) n += has_marker(i);
  return mm_row.x0 + n * mm_row.dx;
}

/* (Activate: the area the Knight is in, its map had; else Dirtmouth. Zoom Shortcut: zoomed into at once) */
static void zoom_in(void);
void map_pane_enter(bool shortcut) {
  memset(&mp, 0, sizeof mp);
  mp.hit = -1, mp.wide_a = mp.nav_a = 1;
  int a = area_of_zone();
  select_area(a >= 0 && have_area(a) ? a : A_TOWN);
  if (mc.compass.sprite < 0) anim_play(&mc.compass, CLIP_MAPUI_IDLE);
  if (shortcut) zoom_in();
}

static void zoom_in(void) {
  /* Map Zoom: the pieces unselected and down, the pane's arrows away; Game_Map from the wide map's place (0.436) */
  mp.st = MP_ZOOM, mp.t = 0;
  mp.x = zoom.x, mp.y = zoom.y, mp.k = zoom.k0;
}

static void zoomed(void) {
  /* Zoomed In: the map's key up, its pins as the key has them; the pan on */
  mp.st = MP_ZOOMED, mp.t = 0;
  mc.pins_hidden = g_pd.map_key_pref == 2;
}

static void markers_open(void) {
  /* Marker Select Menu (OpenMarkerMenu): the first kind with any left chosen (else the first had); the placement cursor
   * at its first place; the map's key down, the pan off */
  mp.st = MP_MARKERS, mp.t = 0;
  mp.marker = 255;
  for (int c = 0; c < 4 && mp.marker == 255; c++)
    if (has_marker(c) && spare(c) > 0) mp.marker = (uint8_t)c;
  for (int c = 0; c < 4 && mp.marker == 255; c++)
    if (has_marker(c)) mp.marker = (uint8_t)c;
  mp.cur_x = mm_row.x0;
  mp.px = mm_place.x, mp.py = mm_place.y;
  mp.confirm_t = MAP_MM_UI_PAUSE, mp.place_t = 0, mp.hit = -1;
}

/* the placement cursor's place on the HUD */
static float place_x(void) { return INV_X + mm_row.x + mp.px; }
static float place_y(void) { return INV_Y + mm_row.y + mp.py; }

/* (PanMap: the cursor moved; at its bounds, the map moved instead (not just after a marker is placed)) */
static bool pan_cursor(uint32_t keys) {
  float ix = (float)((keys & K_RIGHT) != 0) - (float)((keys & K_LEFT) != 0);
  float iy = (float)((keys & K_UP) != 0) - (float)((keys & K_DOWN) != 0);
  if (ix == 0 && iy == 0) return false;
  float vx = ix * mm_place.speed * DT, vy = iy * mm_place.speed * DT;
  float px = mp.px + vx, py = mp.py + vy;
  if (px < mm_place.minx || px > mm_place.maxx) {
    px = px < mm_place.minx ? mm_place.minx : mm_place.maxx;
    if (mp.place_t <= 0) mp.x -= vx;
  }
  if (py < mm_place.miny || py > mm_place.maxy) {
    py = py < mm_place.miny ? mm_place.miny : mm_place.maxy;
    if (mp.place_t <= 0) mp.y -= vy;
  }
  mp.px = px, mp.py = py;
  keep_within_bounds();
  return true;
}

static void marker_at(int c, int i, float *x, float *y) {
  *x = mp.x + g_pd.markers[c][i][0] * 0.01f * mp.k, *y = mp.y + g_pd.markers[c][i][1] * 0.01f * mp.k;
}

/* (InvMarkerCollide: the markers the placement box touches; the one touched last kept while it still is) */
static void find_hit(void) {
  float cx = place_x(), cy = place_y(), x, y;
  int last = -1;
  bool kept = false;
  for (int c = 0; c < 4; c++)
    for (int i = 0; i < g_pd.markers_placed[c] && i < MAX_MARKERS; i++) {
      marker_at(c, i, &x, &y);
      if (hypotf(x - cx, y - cy) > MAP_MM_RADII) continue;
      if (mp.hit == c * 8 + i) kept = true;
      last = c * 8 + i;
    }
  if (!kept) mp.hit = (int8_t)last;
}

static void place_marker(void) {
  int c = mp.marker;
  if (c > 3 || spare(c) <= 0) return;   /* (failure: none of that kind left) */
  int i = g_pd.markers_placed[c]++;
  g_pd.markers[c][i][0] = (int16_t)lrintf((place_x() - mp.x) / mp.k * 100);
  g_pd.markers[c][i][1] = (int16_t)lrintf((place_y() - mp.y) / mp.k * 100);
  mp.place_t = 0.3f;
}

static void remove_marker(void) {
  int c = mp.hit >> 3, i = mp.hit & 7, n = g_pd.markers_placed[c];
  for (int j = i; j + 1 < n; j++) g_pd.markers[c][j][0] = g_pd.markers[c][j + 1][0], g_pd.markers[c][j][1] = g_pd.markers[c][j + 1][1];
  g_pd.markers_placed[c] = (uint8_t)(n - 1);
  mp.hit = -1;
}

/* (MarkerSelectRight: the next kind had, round) */
static void marker_next(void) {
  for (int n = 1; n < 4; n++) {
    int c = (mp.marker + n) % 4;
    if (has_marker(c)) {
      mp.marker = (uint8_t)c;
      return;
    }
  }
}

static float fade_to(float a, bool up, float time) { return up ? fminf(1, a + DT / time) : fmaxf(0, a - DT / time); }

bool map_pane_holds(void) { return mp.st != MP_WIDE && !mp.closed; }   /* (Do Not Close) */
float map_pane_nav(void) { return mp.nav_a; }
void map_pane_close(void) { mp.closed = true, mp.key_a = mp.mm_a = 0; }   /* (Close: CloseQuickMap, MAP KEY DOWN) */

int map_pane_tick(uint32_t keys, uint32_t pressed, uint32_t move, bool repeat) {
  int out = MAP_PANE_NONE;
  mp.t += DT;
  mc.compass.events = 0;
  anim_update(&mc.compass, DT);
  switch (mp.st) {
    case MP_WIDE:
      if (move) {
        int to = wide_move(mp.sel, move);
        if (mp.sel == ARROW_L && (move & K_LEFT)) out = repeat ? MAP_PANE_NONE : MAP_PANE_LEFT;       /* (Move Pane L) */
        else if (mp.sel == ARROW_R && (move & K_RIGHT)) out = repeat ? MAP_PANE_NONE : MAP_PANE_RIGHT;
        else select_area(to);
      }
      if (pressed & K_OK) {
        if (mp.sel >= 0) zoom_in();
        else out = mp.sel == ARROW_L ? MAP_PANE_LEFT : MAP_PANE_RIGHT;
      }
      break;
    case MP_ZOOM: {
      float q = fminf(1, mp.t / ZOOM_TIME), e = sinf(q * 1.5707964f);
      const WidePiece *w = &wide[mp.sel];
      mp.k = zoom.k0 + (zoom.k - zoom.k0) * e;
      mp.x = zoom.x + (w->zx - zoom.x) * e, mp.y = zoom.y + (w->zy - zoom.y) * e;
      if (mp.t >= ZOOM_TIME + 0.05f) zoomed();
      break;
    }
    case MP_ZOOMED:
      /* (the pan: the map against the arrows held) */
      mp.x -= ((float)((keys & K_RIGHT) != 0) - (float)((keys & K_LEFT) != 0)) * MAP_PAN_SPEED * DT;
      mp.y -= ((float)((keys & K_UP) != 0) - (float)((keys & K_DOWN) != 0)) * MAP_PAN_SPEED * DT;
      keep_within_bounds();
      if (pressed & (K_INV | K_MAP)) out = MAP_PANE_CLOSE;   /* (Inventory Cancel) */
      else if (pressed & K_BACK) mp.st = MP_ZOOM_OUT, mp.t = 0, mp.ox = mp.x, mp.oy = mp.y;
      else if ((pressed & K_OK) && pd_flag(PDF_HAS_MARKER)) markers_open();
      else if ((pressed & K_DASH) && pd_flag(PDF_HAS_PIN)) {
        /* (the map's key's action: the key and the pins, the pins only, neither) */
        g_pd.map_key_pref = (uint8_t)((g_pd.map_key_pref + 1) % 3);
        mc.pins_hidden = g_pd.map_key_pref == 2;
      }
      break;
    case MP_ZOOM_OUT: {
      float q = fminf(1, mp.t / ZOOM_OUT_TIME);
      mp.k = zoom.k + (zoom.k0 - zoom.k) * q;
      mp.x = mp.ox + (zoom.x - mp.ox) * q, mp.y = mp.oy + (zoom.y - mp.oy) * q;
      if (mp.t >= 0.35f) {
        /* Reset: the area chosen again */
        mp.st = MP_WIDE, mp.wide_a = 1;
        select_area(mp.sel);
      }
      break;
    }
    case MP_MARKERS:
      if (!pan_cursor(keys) && mp.hit >= 0) {
        /* (pulled to the marker it is on) */
        float x, y, q = fminf(1, mm_place.pull * DT);
        marker_at(mp.hit >> 3, mp.hit & 7, &x, &y);
        mp.px += (x - place_x()) * q, mp.py += (y - place_y()) * q;
      }
      find_hit();
      if (mp.confirm_t <= 0) {
        if (pressed & K_OK) {
          if (mp.hit >= 0) remove_marker();
          else place_marker();
        } else if (pressed & K_DASH)
          marker_next();
      }
      if (mp.confirm_t > 0) mp.confirm_t -= DT;
      if (mp.place_t > 0) mp.place_t -= DT;
      if (pressed & K_INV) out = MAP_PANE_CLOSE;   /* (Marker Inv Cancel) */
      else if (pressed & K_BACK) mp.st = MP_MARKER_CANCEL, mp.t = 0;
      break;
    case MP_MARKER_CANCEL:
      if (mp.t >= 0.05f) zoomed();
      break;
    default:
      break;
  }
  /* the fades: the wide map's pieces (down in 0.15 s; up in 0.4 from the zoom out's 0.1 s), World Map's (0.2 s: up from
   * the zoom out's 0.25), the map's key's (0.2), the markers' menu's (0.1); its cursor to the kind chosen (0.1 s) */
  if (mp.st == MP_ZOOM) mp.wide_a = fade_to(mp.wide_a, false, 0.15f);
  else if (mp.st == MP_ZOOM_OUT && mp.t >= 0.1f) mp.wide_a = fade_to(mp.wide_a, true, 0.4f);
  bool nav = mp.st == MP_WIDE || (mp.st == MP_ZOOM_OUT && mp.t >= 0.25f);
  mp.nav_a = fade_to(mp.nav_a, nav, 0.2f);
  mp.key_a = fade_to(mp.key_a, mp.st == MP_ZOOMED && g_pd.map_key_pref == 0 && pd_flag(PDF_HAS_PIN), 0.2f);
  mp.mm_a = fade_to(mp.mm_a, mp.st == MP_MARKERS, 0.1f);
  if (mp.marker < 4) {
    float to = marker_x(mp.marker), d = mm_row.dx * DT / 0.1f;
    mp.cur_x = fabsf(to - mp.cur_x) <= d ? to : mp.cur_x + (to > mp.cur_x ? d : -d);
  }
  return out;
}

/* an action: its backboard, its key, its text (right aligned: ending before the key; left: from after it) */
static void action_draw(const MapAction *m, int which, int key, float ox, float oy, uint8_t white, float a, bool right) {
  ui_piece(&m->back, ox, oy, 1, white);
  float hw = ui_key(key, ox + m->key[0], oy + m->key[1], white, a);
  UiText t = m->text;
  if (right && t.x > m->key[0] - hw - 0.15f) t.x = m->key[0] - hw - 0.15f;
  if (!right && t.x < m->key[0] + hw + 0.15f) t.x = m->key[0] + hw + 0.15f;
  ui_text_at(m->texts[which], STYLE_MSG_S, &t, ox, oy, right ? 2 : 0, a);   /* (as the keys' actions elsewhere) */
}

/* the pane at (ox, oy) (its alpha a; its white); current: the pane shown (else going: Pane Reset, nothing chosen) */
void map_pane_draw(float ox, float oy, float a, uint8_t white, bool current) {
  to_overlay = true, pane_a = a, tint_n = 0;
  int st = current ? mp.st : MP_WIDE;
  /* the wide map: the areas had (Dirtmouth always), their names; chosen: white (in the wide map only) */
  float wa = current ? mp.wide_a : 1;
  if (wa > 0) {
    uint8_t wa8 = (uint8_t)(wa * 255 + 0.5f);
    /* (the names, in the smallest of the bigger text: those of areas side by side moved apart where they would meet) */
    float nx[NAREAS], hw[NAREAS];
    for (int i = 0; i < NAREAS; i++) {
      const uint8_t *s = text_get(wide[i].text);
      nx[i] = wide[i].name.x, hw[i] = text_width(STYLE_MSG_XS, s, (int)strlen((const char *)s)) / HUD_PX / 2 + MAP_NAME_GAP / 2;
    }
    for (int i = 0; i < NAREAS; i++)
      for (int j = 0; j < NAREAS; j++) {
        if (i == j || !have_area(i) || !have_area(j) || fabsf(wide[i].name.top - wide[j].name.top) > 1 || nx[i] > nx[j]) continue;
        float over = nx[i] + hw[i] - (nx[j] - hw[j]);
        if (over > 0) nx[i] -= over / 2, nx[j] += over / 2;
      }
    for (int i = 0; i < NAREAS; i++) {
      if (!have_area(i) || wide[i].piece.sprite < 0) continue;
      bool on = current && st == MP_WIDE && mp.sel == i;
      uint8_t g = on ? 255 : MAP_WIDE_GREY, gn = on ? 255 : MAP_WIDE_NAME_GREY;
      ui_piece(&wide[i].piece, ox, oy, 1, map_tint((uint32_t)g * 0x10101u, wa8));
      hud_text(wide[i].text, STYLE_MSG_XS, ox + nx[i], oy + wide[i].name.top, 1, (uint32_t)gn * 0x10101u, wa);
    }
    /* (its compass: where the Knight's area is) */
    int z = area_of_zone();
    if (charm_on(2) && z >= 0 && mc.compass.sprite >= 0)
      hud_sprite(mc.compass.sprite, ox + wide[z].cx, oy + wide[z].cy, MAP_WIDE_COMPASS_K, 0, map_tint(0xFFFFFF, wa8));
  }
  /* Game_Map, zoomed into: its areas had, the marks on it */
  bool map_on = current && !mp.closed && (st == MP_ZOOM || st == MP_ZOOMED || st == MP_MARKERS ||
                                          st == MP_MARKER_CANCEL || (st == MP_ZOOM_OUT && mp.t < 0.35f));
  if (map_on) {
    for (int i = 0; i < NAREAS; i++)
      if (have_area(i)) draw_area(i, mp.x, mp.y, mp.k, false);
    draw_marks(mp.x, mp.y, mp.k);
  }
  if (current && !mp.closed && st == MP_ZOOMED) {
    /* (the pan's arrows: where there is more of it) */
    float b[4];
    pan_bounds(b);
    bool show[4] = {mp.y > b[2], mp.y < b[3], mp.x < b[1], mp.x > b[0]};
    for (int i = 0; i < 4; i++)
      if (show[i]) hud_sprite(pan_arrows[i].sprite, ox + pan_arrows[i].x, oy + pan_arrows[i].y, pan_arrows[i].k, pan_arrows[i].deg, white);
  }
  /* the map's key: its backboard, its rows (the pins had); its action */
  if (current && pd_flag(PDF_HAS_PIN)) {
    if (mp.key_a > 0) {
      uint8_t kw = map_tint(0xFFFFFF, (uint8_t)(mp.key_a * 255 + 0.5f));
      int n = 0;
      for (int i = 0; i < MAP_KEY_N; i++) n += key_flags[i] >= 0 && pd_flag(key_flags[i]);
      ui_piece(&key_bb, ox, oy + n * MAP_KEY_STEP, 1, kw);
      n = 0;
      for (int i = 0; i < MAP_KEY_N; i++) {
        if (key_flags[i] < 0 || !pd_flag(key_flags[i])) continue;
        ui_piece(&key_rows[i].icon, ox, oy + n * MAP_KEY_STEP, 1, kw);
        ui_text_at(key_rows[i].text, STYLE_MSG, &key_rows[i].at, ox, oy + n * MAP_KEY_STEP, 0, mp.key_a * a);
        n++;
      }
    }
    if (st == MP_ZOOMED && !mp.closed) action_draw(&act_key, g_pd.map_key_pref, TXT_KEY_SHIFT, ox, oy, white, a, true);
  }
  /* the markers' menu: its backboard, its cursor, the kinds had (the chosen bigger; none left: grey), how many are left;
   * the placement cursor; its actions */
  if (current && mp.mm_a > 0) {
    float ma = mp.mm_a;
    uint8_t mw = map_tint(0xFFFFFF, (uint8_t)(ma * 255 + 0.5f)), mg = map_tint(0x808080, (uint8_t)(ma * 255 + 0.5f));
    float bx = ox + mm_row.x, by = oy + mm_row.y;
    ui_piece(&mm_pieces[1], ox, oy, 1, mw);
    ui_piece(&mm_pieces[0], ox, oy, 1, mw);
    ui_piece(&mm_pieces[2], ox + mp.cur_x - mm_row.x0, oy, 1, mw);
    ui_piece(&mm_pieces[3], ox + mp.cur_x - mm_row.x0, oy, 1, mw);
    for (int c = 0; c < 4; c++) {
      if (!has_marker(c)) continue;
      float k = c == mp.marker ? 0.7f : 0.6f, x = bx + marker_x(c), y = by + mm_row.y0;
      bool left = spare(c) > 0;
      hud_sprite(mm_markers[c], x, y, k / 0.7f, 0, left ? mw : mg);
      UiText t = {mm_amount.x * k / 0.6f, mm_amount.top * k / 0.6f, mm_amount.w};
      ui_number_at(spare(c), STYLE_MSG, x + t.x, y + t.top, 1, ma * a);
    }
    if (st == MP_MARKERS || st == MP_MARKER_CANCEL)
      action_draw(&act_cancel, 0, TXT_KEY_BACK, ox, oy, mw, ma * a, true);
    if (marker_kinds() >= 2) action_draw(&act_change, 0, TXT_KEY_SHIFT, ox, oy, mw, ma * a, false);
  }
  if (current && !mp.closed && st == MP_MARKERS) hud_sprite(mm_place.sprite, place_x() - INV_X + ox, place_y() - INV_Y + oy, 1, 0, white);
  /* the confirm action (Zoom In; zoomed: Zoom Out), the markers' (Markers; in their menu: Place, Remove) */
  if (current && (st == MP_WIDE || st == MP_ZOOM)) action_draw(&act_confirm, 0, TXT_KEY_OK, ox, oy, white, a, true);
  if (current && st == MP_ZOOMED) action_draw(&act_confirm, 1, TXT_KEY_BACK, ox, oy, white, a, true);
  if (current && pd_flag(PDF_HAS_MARKER) && (st == MP_ZOOMED || st == MP_MARKERS || st == MP_MARKER_CANCEL))
    action_draw(&act_marker, st == MP_ZOOMED ? 0 : mp.hit >= 0 ? 2 : 1, TXT_KEY_OK, ox, oy, white, a, false);
  to_overlay = false;
}
