/* The screens: the HUD (hotbar, hearts, hunger, experience, air), Minecraft's menus (the title, the worlds, a new
 * world, the game paused, death), and its containers (the inventory and its 2 x 2 crafting, the crafting table,
 * the furnace, chests, the Creative inventory), with Minecraft's pictures and font.
 *
 * Containers on the keypad: the arrows move between slots, 2nd picks up or puts down (a left click), enter takes
 * half or puts one (a right click), y= with 2nd moves a whole stack across (a shift click), clear closes.
 * A screen is drawn in bands of the 3D view's buffer, then copied to the screen: nothing flickers. */
#include "nb.h"
#include "rs.h"
#include "draw.h"

int gui = GUI_NONE, menu_action;
Tile tiles[MAX_TILES];
int n_tiles;
Tile *gui_tile;
static bool hud_dirty = true;
static int tip_ticks, msg_ticks;
static char msg[48];
static int sel;           /* the slot or button selected */
static bool shown;        /* the screen is drawn as it is (else: draw what changes) */
static bool full;         /* all of it to draw (just opened) */

/* ---------------------------------------------------------------- drawing off screen, then onto it */
static void offscreen(int x, int y, int w, int h, void (*fn)(void)) {
  render_stale = 1;
  if (x < 0) w += x, x = 0;
  if (y < 0) h += y, y = 0;
  if (x + w > SCREEN_W) w = SCREEN_W - x;
  if (y + h > SCREEN_H) h = SCREEN_H - y;
  if (w <= 0 || h <= 0) return;
  int band = RW * RH / w;
  for (int y0 = y; y0 < y + h; y0 += band) {
    int bh = y + h - y0 < band ? y + h - y0 : band;
    for (int r = 0; r < bh; r++) memcpy(rbuf + r * w, scr + (y0 + r) * SCREEN_W + x, (size_t)w);
    cv = (Canvas){rbuf, w, x, y0, w, bh};
    fn();
    for (int r = 0; r < bh; r++) memcpy(scr + (y0 + r) * SCREEN_W + x, rbuf + r * w, (size_t)w);
  }
  canvas_screen();
}

/* darkens what is on the screen (Minecraft's grey veil over the world behind a screen) */
#ifndef NB_VEIL_IN
#define NB_VEIL_IN 4
#endif
static const uint8_t *veil_row;
static void veil_span(unsigned a, unsigned b) {
  for (unsigned n; a < b; a += n) {
    n = b - a > 65535 ? 65535 : b - a;
    rs_shade(scr + a, veil_row, (uint16_t)n);   /* (through the shade row, in assembly) */
  }
}
/* (but for the rectangle hx, hy, hw, hh: under a panel's inside, drawn all over) */
static void veil(int level, int hx, int hy, int hw, int hh) {
  veil_row = render_veil_row((uint8_t)level);
  unsigned a = 0;
  for (int r = hy; r < hy + hh; r++) {
    unsigned b = (unsigned)r * SCREEN_W + (unsigned)hx;
    veil_span(a, b);
    a = b + (unsigned)hw;
  }
  veil_span(a, SCREEN_W * SCREEN_H);
}

void gui_message(const char *s) {
  strncpy(msg, s, sizeof msg - 1);
  msg_ticks = 60;
  hud_dirty = true;
}

/* ---------------------------------------------------------------- the HUD */
static int last_slot = -1, last_id = -1;
int xp_level, xp_points;   /* experience: the level, the points into it */
int xp_need(int l) { return l >= 30 ? 9 * l - 158 : l >= 15 ? 5 * l - 38 : 2 * l + 7; }

bool hud_draw(void) {
  static uint32_t sig;
  /* redrawn when something on it changed */
  uint32_t s = (uint32_t)pl.slot * 7 + (uint32_t)pl.health * 131 + (uint32_t)(pl.air / 30) * 1031 + (uint32_t)xp_points * 7919 +
               (uint32_t)xp_level * 104729 + (tip_ticks > 0) + (msg_ticks > 0) * 2 + (uint32_t)pl.mode * 999331;
  for (int i = 0; i < 9; i++) s = s * 31 + pl.inv[i].id * 257 + pl.inv[i].n + pl.inv[i].dmg * 7;
  if (held()->id != last_id || pl.slot != last_slot) {
    tip_ticks = held()->id ? 40 : 0;
    last_id = held()->id, last_slot = pl.slot;
  }
  if (s == sig && !hud_dirty) return false;
  sig = s, hud_dirty = false, hud_changed = 1;
  cv = (Canvas){hud_layer, HUD_W, HUD_X, HUD_Y, HUD_W, HUD_H};
  memset(hud_layer, 0, HUD_W * HUD_H);
  int x0 = SCREEN_W / 2 - 91, yb = SCREEN_H - 22;
  sprite(SP_HOTBAR, x0, yb);
  sprite(SP_HOTBAR_SEL, x0 - 1 + pl.slot * 20, yb - 1);
  for (int i = 0; i < 9; i++) stack_draw(&pl.inv[i], x0 + 3 + i * 20, yb + 3);
  if (!pl.mode) {
    /* the experience bar and level, the hearts, the hunger (always full: no hunger here), the air */
    sprite_part(SP_XP_BG, x0, SCREEN_H - 29, 0, 0, 182, 5);
    int w = xp_points * 183 / xp_need(xp_level);
    if (w > 0) sprite_part(SP_XP, x0, SCREEN_H - 29, 0, 0, w, 5);
    if (xp_level > 0) {
      char t[6];
      int n = xp_level, i = 5;
      t[i] = 0;
      do t[--i] = (char)('0' + n % 10), n /= 10;
      while (n && i);
      int tx = SCREEN_W / 2 - text_width(t + i) / 2, ty = SCREEN_H - 35;
      text_plain(tx + 1, ty, t + i, C_BLACK), text_plain(tx - 1, ty, t + i, C_BLACK);
      text_plain(tx, ty + 1, t + i, C_BLACK), text_plain(tx, ty - 1, t + i, C_BLACK);
      text_plain(tx, ty, t + i, C_GREEN);
    }
    int hy = SCREEN_H - 39;
    for (int i = 0; i < 10; i++) {
      int x = x0 + i * 8;
      sprite(SP_HEART_BG, x, hy);
      if (pl.health > i * 2 + 1) sprite(SP_HEART, x, hy);
      else if (pl.health == i * 2 + 1) sprite(SP_HEART_HALF, x, hy);
      sprite(SP_FOOD_BG, x0 + 182 - 9 - i * 8, hy);
      sprite(SP_FOOD, x0 + 182 - 9 - i * 8, hy);
    }
    if (pl.air < 300) {
      int full = (pl.air * 10 + 299) / 300, pop = (pl.air * 10) / 300;
      for (int i = 0; i < full; i++) sprite(i < pop ? SP_BUBBLE : SP_BUBBLE_POP, x0 + 182 - 9 - i * 8, hy - 10);
    }
  }
  const char *t = msg_ticks > 0 ? msg : tip_ticks > 0 ? (held()->id >= IT0 ? it_label[held()->id - IT0] : blk_label[held()->id]) : 0;
  if (t) text_center(SCREEN_W / 2, SCREEN_H - (pl.mode ? 38 : 59 - 9), t, C_WHITE);
  canvas_screen();
  return true;
}

/* ---------------------------------------------------------------- tiles: chests and furnaces */
Tile *tile_at(int x, int y, int z) {
  for (int i = 0; i < n_tiles; i++)
    if (tiles[i].x == x && tiles[i].y == y && tiles[i].z == z) return &tiles[i];
  return 0;
}
Tile *tile_new(int x, int y, int z, int kind, int facing) {
  Tile *t = tile_at(x, y, z);
  if (t) return t;
  if (n_tiles >= MAX_TILES) return 0;
  t = &tiles[n_tiles++];
  memset(t, 0, sizeof *t);
  t->x = (int16_t)x, t->y = (uint8_t)y, t->z = (int16_t)z, t->kind = (uint8_t)kind, t->facing = (uint8_t)facing;
  return t;
}
void tile_remove(int x, int y, int z) {
  Tile *t = tile_at(x, y, z);
  if (!t) return;
  if (gui_tile == t) gui_tile = 0;
  *t = tiles[--n_tiles];
}

/* ---------------------------------------------------------------- crafting */
static Stack grid[9];      /* the crafting grid open (2 x 2 in the inventory: the first 4, as 2 wide) */
static Stack result;
static int grid_w;         /* 2 or 3 */

static bool ing_ok(int code, int id) {
  if (code == 255) return id == 0;
  if (code == 254) return id == B_PLANKS_OAK || id == B_PLANKS_SPRUCE || id == B_PLANKS_BIRCH;
  if (code == 253) return id == B_LOG_OAK || id == B_LOG_SPRUCE || id == B_LOG_BIRCH;
  return id == code;
}
static void craft_find(void) {
  result = (Stack){0, 0, 0};
  int x0 = 9, y0 = 9, x1 = -1, y1 = -1;
  for (int i = 0; i < grid_w * grid_w; i++)
    if (grid[i].id) {
      int x = i % grid_w, y = i / grid_w;
      if (x < x0) x0 = x;
      if (x > x1) x1 = x;
      if (y < y0) y0 = y;
      if (y > y1) y1 = y;
    }
  if (x1 < 0) return;
  int w = x1 - x0 + 1, h = y1 - y0 + 1;
  for (const uint8_t *r = recipes; r[3]; r += 5 + r[3] * r[4]) {
    if (r[3] != w || r[4] != h) continue;
    for (int mirror = 0; mirror < 2; mirror++) {
      bool ok = true;
      for (int y = 0; y < h && ok; y++)
        for (int x = 0; x < w && ok; x++) {
          int rx = mirror ? w - 1 - x : x;
          ok = ing_ok(r[5 + y * w + rx], grid[(y0 + y) * grid_w + x0 + x].id);
        }
      if (ok) {
        result = (Stack){(uint8_t)(r[0] | r[1] << 8), r[2], 0};
        return;
      }
    }
  }
}
static void craft_take(void) {
  for (int i = 0; i < grid_w * grid_w; i++)
    if (grid[i].id && !--grid[i].n) grid[i].id = 0;
  craft_find();
}

/* ---------------------------------------------------------------- the containers */
typedef struct { int16_t x, y; Stack *s; uint8_t kind; } Slot;   /* kind: 0 normal, 1 result, 2 creative */
enum { SK_NORMAL, SK_RESULT, SK_CREATIVE, SK_OUTPUT };
static Slot slots[64];
static int n_slots, panel_x, panel_y, panel_w, panel_h, panel_sprite;
static int creative_scroll;
static Stack creative_items[B_N + IT_N];
static int n_creative;

static void add_slot(int x, int y, Stack *s, int kind) { slots[n_slots++] = (Slot){(int16_t)x, (int16_t)y, s, (uint8_t)kind}; }
static void add_player_slots(int x, int y) {
  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 9; c++) add_slot(x + c * 18, y + r * 18, &pl.inv[9 + r * 9 + c], SK_NORMAL);
  for (int c = 0; c < 9; c++) add_slot(x + c * 18, y + 58, &pl.inv[c], SK_NORMAL);
}

static void creative_list(void) {
  n_creative = 0;
  for (int b = 1; b < B_N; b++)
    if (icon_of[b] != 255) creative_items[n_creative++] = (Stack){(uint8_t)b, 64, 0};
  for (int i = 0; i < IT_N; i++) creative_items[n_creative++] = (Stack){(uint8_t)(IT0 + i), 1, 0};
}

static void layout(void) {
  n_slots = 0;
  panel_w = 176, panel_h = 166;
  switch (gui) {
    case GUI_INVENTORY:
      panel_sprite = SP_INVENTORY;
      grid_w = 2;
      for (int i = 0; i < 4; i++) add_slot(88 + (i & 1) * 18, 26 + (i >> 1) * 18, &grid[i], SK_NORMAL);
      add_slot(144, 36, &result, SK_RESULT);
      add_player_slots(8, 84);
      break;
    case GUI_CRAFTING:
      panel_sprite = SP_CRAFTING_TABLE;
      grid_w = 3;
      for (int i = 0; i < 9; i++) add_slot(30 + (i % 3) * 18, 17 + (i / 3) * 18, &grid[i], SK_NORMAL);
      add_slot(124, 35, &result, SK_RESULT);
      add_player_slots(8, 84);
      break;
    case GUI_FURNACE:
      panel_sprite = SP_FURNACE;
      add_slot(56, 17, &gui_tile->s[0], SK_NORMAL);
      add_slot(56, 53, &gui_tile->s[1], SK_NORMAL);
      add_slot(116, 35, &gui_tile->s[2], SK_OUTPUT);
      add_player_slots(8, 84);
      break;
    case GUI_CHEST:
      panel_sprite = SP_CHEST, panel_h = 167;
      for (int i = 0; i < 27; i++) add_slot(8 + (i % 9) * 18, 18 + (i / 9) * 18, &gui_tile->s[i], SK_NORMAL);
      add_player_slots(8, 85);
      break;
    case GUI_CREATIVE:
      panel_sprite = SP_CREATIVE;
      panel_w = 195, panel_h = 136;
      for (int i = 0; i < 45; i++) {
        int k = creative_scroll * 9 + i;
        add_slot(9 + (i % 9) * 18, 18 + (i / 9) * 18, k < n_creative ? &creative_items[k] : 0, SK_CREATIVE);
      }
      for (int c = 0; c < 9; c++) add_slot(9 + c * 18, 112, &pl.inv[c], SK_NORMAL);
      break;
  }
  panel_x = (SCREEN_W - panel_w) / 2, panel_y = (SCREEN_H - panel_h) / 2;
}

/* the darkened world where a container's names go (above its panel), kept while it is open to be put back under
 * the next name (in the HUD's layer: not shown then) */
#define STRIP_H 14
static void strip_keep(void) {
  int x = panel_x - 2, w = panel_w + 4, y = panel_y - STRIP_H;
  for (int r = 0; r < STRIP_H; r++) memcpy(hud_layer + r * w, scr + (y + r) * SCREEN_W + x, (size_t)w);
  hud_changed = 1;
}

static void draw_panel(void) {
  int px = panel_x, py = panel_y;
  blit(hud_layer, panel_w + 4, px - 2, py - STRIP_H, panel_w + 4, STRIP_H);
  sprite(panel_sprite, px, py);
  if (gui != GUI_CREATIVE) sprite(SP_INV_BOTTOM, px, py + (gui == GUI_CHEST ? 80 : 79));
  if (gui == GUI_INVENTORY) {
    sprite(SP_STEVE, px + 39, py + 16);
    text_plain(px + 86, py + 16, "Crafting", C_DARK);
  } else if (gui == GUI_CRAFTING) text_plain(px + 28, py + 6, "Crafting", C_DARK);
  else if (gui == GUI_FURNACE) {
    text_center(px + 88, py + 6, "Furnace", C_DARK);
    Tile *t = gui_tile;
    if (t->burn > 0 && t->burn_max > 0) {
      int h = (int)((int32_t)t->burn * 13 / t->burn_max) + 1;
      sprite_part(SP_FLAME, px + 56, py + 36 + 14 - h, 0, 14 - h, 14, h);
    }
    if (t->cook > 0) sprite_part(SP_ARROW, px + 79, py + 34, 0, 0, t->cook * 24 / 200 + 1, 17);
  } else if (gui == GUI_CHEST) text_plain(px + 8, py + 6, "Chest", C_DARK);
  else if (gui == GUI_CREATIVE) {
    text_plain(px + 8, py + 6, "Creative Inventory", C_DARK);
    int rows = (n_creative + 8) / 9 - 5;
    sprite(SP_SCROLLER, px + 175, py + 18 + (rows > 0 ? creative_scroll * (112 - 18 - 15) / rows : 0));
  }
  if (gui != GUI_INVENTORY) {
    if (gui != GUI_CREATIVE) text_plain(px + 8, py + (gui == GUI_CHEST ? 73 : 72), "Inventory", C_DARK);
  }
  for (int i = 0; i < n_slots; i++) {
    const Slot *s = &slots[i];
    if (s->s) stack_draw(s->s, px + s->x, py + s->y);
    if (i == sel) {
      /* the slot selected: Minecraft's white highlight, and a frame you cannot miss */
      int x = px + s->x, y = py + s->y;
      fill(x - 1, y - 1, 18, 1, C_WHITE), fill(x - 1, y + 16, 18, 1, C_WHITE);
      fill(x - 1, y - 1, 1, 18, C_WHITE), fill(x + 16, y - 1, 1, 18, C_WHITE);
    }
  }
  /* the stack carried, over the selected slot */
  if (pl.cursor.id && sel < n_slots) stack_draw(&pl.cursor, px + slots[sel].x + 6, py + slots[sel].y - 6);
  /* the name of what is under the cursor, at the top */
  const Slot *s = sel < n_slots ? &slots[sel] : 0;
  if (s && s->s && s->s->id && !pl.cursor.id) {
    const char *t = s->s->id >= IT0 ? it_label[s->s->id - IT0] : blk_label[s->s->id];
    int w = text_width(t) + 6, x = px + panel_w / 2 - w / 2, y = py - 13;
    fill(x, y, w, 12, C_20);
    text(x + 3, y + 2, t, C_WHITE);
  }
}

static void put_back(Stack *s) {
  if (s->id) {
    int left = inv_add(s->id, s->n, s->dmg);
    (void)left;
    *s = (Stack){0, 0, 0};
  }
}

static void close_container(void) {
  put_back(&pl.cursor);
  for (int i = 0; i < 9; i++) put_back(&grid[i]);
  result = (Stack){0, 0, 0};
  gui = GUI_NONE;
}

/* a click on slot i: 2nd (right 0), enter (right 1), y= and 2nd (shift) */
static void click(int i, int right, bool shift) {
  Slot *s = &slots[i];
  if (!s->s) return;
  Stack *st = s->s, *c = &pl.cursor;
  if (s->kind == SK_CREATIVE) {
    if (c->id) *c = (Stack){0, 0, 0};
    else if (st->id) *c = (Stack){st->id, (uint8_t)(right ? 1 : item_max(st->id)), 0};
    return;
  }
  if (shift && st->id) {
    /* across: the player's slots to the container (or the hotbar and back), the result all into the inventory */
    if (s->kind == SK_RESULT) {
      while (result.id && inv_add(result.id, result.n, 0) == 0) craft_take();
      return;
    }
    bool player = st >= &pl.inv[0] && st < &pl.inv[36];
    if (player && gui == GUI_CHEST) {
      for (int k = 0; k < 27 && st->id; k++) {
        Stack *d = &gui_tile->s[k];
        if (!d->id) *d = *st, *st = (Stack){0, 0, 0};
        else if (d->id == st->id && d->n < item_max(d->id)) {
          int m = item_max(d->id) - d->n < st->n ? item_max(d->id) - d->n : st->n;
          d->n = (uint8_t)(d->n + m);
          if (!(st->n = (uint8_t)(st->n - m))) st->id = 0;
        }
      }
    } else if (player && gui == GUI_FURNACE) {
      int k = it_fuel[st->id >= IT0 ? B_N + st->id - IT0 : st->id] ? 1 : 0;
      for (int j = 0; j < 2 && st->id; j++) {
        Stack *d = &gui_tile->s[(k + j) & 1];
        if (!d->id) *d = *st, *st = (Stack){0, 0, 0};
      }
    } else if (player) {
      /* the hotbar and the rest of the inventory */
      int from = (int)(st - pl.inv), a = from < 9 ? 9 : 0, b = from < 9 ? 36 : 9;
      Stack t = *st;
      *st = (Stack){0, 0, 0};
      for (int k = a; k < b && t.n; k++)
        if (pl.inv[k].id == t.id && pl.inv[k].n < item_max(t.id)) {
          int m = item_max(t.id) - pl.inv[k].n < t.n ? item_max(t.id) - pl.inv[k].n : t.n;
          pl.inv[k].n = (uint8_t)(pl.inv[k].n + m), t.n = (uint8_t)(t.n - m);
        }
      for (int k = a; k < b && t.n; k++)
        if (!pl.inv[k].id) pl.inv[k] = t, t.n = 0;
      if (t.n) *st = t;
    } else {
      int left = inv_add(st->id, st->n, st->dmg);
      if (left) st->n = (uint8_t)left;
      else *st = (Stack){0, 0, 0};
    }
    if (gui == GUI_INVENTORY || gui == GUI_CRAFTING) craft_find();
    return;
  }
  if (s->kind == SK_RESULT) {
    if (!st->id) return;
    if (!c->id) *c = *st, craft_take();
    else if (c->id == st->id && c->n + st->n <= item_max(c->id)) c->n = (uint8_t)(c->n + st->n), craft_take();
    return;
  }
  if (s->kind == SK_OUTPUT) {
    if (!st->id) return;
    if (!c->id) *c = *st, *st = (Stack){0, 0, 0};
    else if (c->id == st->id && c->n + st->n <= item_max(c->id)) c->n = (uint8_t)(c->n + st->n), *st = (Stack){0, 0, 0};
    return;
  }
  if (!right) {
    if (!c->id) *c = *st, *st = (Stack){0, 0, 0};
    else if (!st->id) *st = *c, *c = (Stack){0, 0, 0};
    else if (st->id == c->id && item_max(c->id) > 1) {
      int m = item_max(c->id) - st->n < c->n ? item_max(c->id) - st->n : c->n;
      st->n = (uint8_t)(st->n + m);
      if (!(c->n = (uint8_t)(c->n - m))) c->id = 0;
    } else {
      Stack t = *st;
      *st = *c, *c = t;
    }
  } else {
    if (!c->id && st->id) {
      int h = (st->n + 1) / 2;
      *c = (Stack){st->id, (uint8_t)h, st->dmg};
      if (!(st->n = (uint8_t)(st->n - h))) st->id = 0;
    } else if (c->id && (!st->id || (st->id == c->id && st->n < item_max(c->id)))) {
      if (!st->id) *st = (Stack){c->id, 0, c->dmg};
      st->n++;
      if (!--c->n) c->id = 0;
    }
  }
  if (gui == GUI_INVENTORY || gui == GUI_CRAFTING) craft_find();
}

/* the slot the arrow leads to from slot `sel` */
static int slot_towards(int dx, int dy) {
  const Slot *a = &slots[sel];
  int best = -1;
  int32_t bd = 0x7FFFFFFF;
  for (int i = 0; i < n_slots; i++) {
    if (i == sel) continue;
    int ex = slots[i].x - a->x, ey = slots[i].y - a->y;
    int along = dx ? ex * dx : ey * dy, across = dx ? ey : ex;
    if (along <= 0) continue;
    int32_t d = (int32_t)along + (int32_t)(across < 0 ? -across : across) * 3;
    if (d < bd) bd = d, best = i;
  }
  return best;
}

/* ---------------------------------------------------------------- menus */
typedef struct { int16_t x, y, w; uint8_t on, act; const char *label; } Btn;
static Btn btns[8];
static int n_btns;
enum { BA_PLAY, BA_NEW, BA_QUIT, BA_SELECT, BA_CREATE, BA_CANCEL, BA_MODE, BA_DO_CREATE, BA_DELETE, BA_DO_DELETE,
       BA_RESUME, BA_SAVE_QUIT, BA_RESPAWN, BA_TITLE, BA_DONE, BA_DIST };
static WorldInfo winfo[MAX_WORLDS];
static int wslot[MAX_WORLDS], n_worlds, wsel;
static int create_mode;
static char mode_label[24], dist_label[28];

static void add_btn(int x, int y, int w, int on, int act, const char *label) {
  btns[n_btns++] = (Btn){(int16_t)x, (int16_t)y, (int16_t)w, (uint8_t)on, (uint8_t)act, label};
}

static void list_worlds(void) {
  n_worlds = 0;
  for (int s = 1; s <= MAX_WORLDS; s++)
    if (world_info(s, &winfo[n_worlds])) wslot[n_worlds++] = s;
  /* the latest played first */
  for (int i = 1; i < n_worlds; i++)
    for (int j = i; j > 0 && winfo[j].played > winfo[j - 1].played; j--) {
      WorldInfo t = winfo[j];
      winfo[j] = winfo[j - 1], winfo[j - 1] = t;
      int k = wslot[j];
      wslot[j] = wslot[j - 1], wslot[j - 1] = k;
    }
  if (wsel >= n_worlds) wsel = 0;
}

static void menu_layout(void) {
  n_btns = 0;
  int w = SCREEN_W, h = SCREEN_H;
  switch (gui) {
    case GUI_TITLE: {
      int j = h / 4 + 48;
      add_btn(w / 2 - 100, j, 200, 1, BA_PLAY, "Singleplayer");
      add_btn(w / 2 - 100, j + 24, 200, 0, BA_PLAY, "Multiplayer");
      add_btn(w / 2 - 100, j + 60, 200, 1, BA_QUIT, "Quit Game");
      break;
    }
    case GUI_WORLDS:
      add_btn(w / 2 - 154, h - 52, 150, n_worlds > 0, BA_SELECT, "Play Selected World");
      add_btn(w / 2 + 4, h - 52, 150, world_free_slot() != 0, BA_CREATE, "Create New World");
      add_btn(w / 2 - 154, h - 28, 150, n_worlds > 0, BA_DELETE, "Delete");
      add_btn(w / 2 + 4, h - 28, 150, 1, BA_CANCEL, "Cancel");
      break;
    case GUI_CREATE:
      strcpy(mode_label, create_mode ? "Game Mode: Creative" : "Game Mode: Survival");
      add_btn(w / 2 - 75, 100, 150, 1, BA_MODE, mode_label);
      add_btn(w / 2 - 155, h - 28, 150, 1, BA_DO_CREATE, "Create New World");
      add_btn(w / 2 + 5, h - 28, 150, 1, BA_CANCEL, "Cancel");
      break;
    case GUI_CONFIRM:
      add_btn(w / 2 - 155, h / 6 + 96, 150, 1, BA_DO_DELETE, "Delete");
      add_btn(w / 2 + 5, h / 6 + 96, 150, 1, BA_CANCEL, "Cancel");
      break;
    case GUI_PAUSE:
      add_btn(w / 2 - 100, h / 4 + 8, 200, 1, BA_RESUME, "Back to Game");
      strcpy(dist_label, view_dist == 8 ? "Render Distance: 8 blocks"
                         : view_dist == 10 ? "Render Distance: 10 blocks" : "Render Distance: 13 blocks");
      add_btn(w / 2 - 100, h / 4 + 56, 200, 1, BA_DIST, dist_label);
      add_btn(w / 2 - 100, h / 4 + 104, 200, 1, BA_SAVE_QUIT, "Save and Quit to Title");
      break;
    case GUI_DEATH:
      add_btn(w / 2 - 100, h / 4 + 72, 200, 1, BA_RESPAWN, "Respawn");
      add_btn(w / 2 - 100, h / 4 + 96, 200, 1, BA_TITLE, "Title Screen");
      break;
    case GUI_CONTROLS:
      add_btn(w / 2 - 100, h - 24, 200, 1, BA_DONE, "Done");
      break;
  }
  if (sel >= n_btns) sel = 0;
  while (n_btns && !btns[sel].on) sel = (sel + 1) % n_btns;
}

static void draw_buttons(void) {
  for (int i = 0; i < n_btns; i++)
    button(btns[i].x, btns[i].y, btns[i].w, !btns[i].on ? 0 : i == sel ? 2 : 1, btns[i].label);
}

/* the title's name, in stone, like the game's logo (as NumBlocks draws it) */
static void logo(int y) {
  const char *s = "NumBlocks";
  const int sc = 4;
  int w = text_width(s) * sc, x0 = (SCREEN_W - w) / 2;
  for (int d = 3; d >= 1; d--) text_big(x0 + d, y + d, s, d == 3 ? C_20 : d == 2 ? C_40 : C_DARK, sc);
  for (int r = 0; r < 8 * sc; r++) {
    int x = x0;
    for (const char *c = s; *c; c++) {
      const uint8_t *g = font_bits + (*c - 32) * 8;
      for (int k = 0; k < 8 * sc; k++)
        if (g[r / sc] & (1 << (k / sc))) {
          int u = ((x + k) / 2) & 15, v = ((y + r) / 2) & 15;
          uint8_t col = tex_px[TX_STONE][v * 16 + u];
          if (r < sc) col = shade_cm[31][col] == col ? C_C0 : col;
          fill(x + k, y + r, 1, 1, col);
        }
      x += font_w[*c - 32] * sc;
    }
  }
}

static void splash(void) {
  /* "Also try Minecraft!" in yellow, as the title's splash (straight: the CE has no time to turn text) */
  const char *s = "Also try Minecraft!";
  int x = SCREEN_W / 2 + 40, y = 70;
  text(x, y, s, C_YELLOW);
}

static void draw_menu(void) {
  int w = SCREEN_W, h = SCREEN_H;
  switch (gui) {
    case GUI_TITLE:
      logo(30);
      splash();
      text(2, h - 10, "NumBlocks CE", C_WHITE);
      text(w - text_width("Inspired by Minecraft, not affiliated with Mojang") - 2, h - 20,
           "Inspired by Minecraft, not affiliated with Mojang", C_WHITE);
      text(w - text_width("Made by Mason Chen as part of NumPlay") - 2, h - 10, "Made by Mason Chen as part of NumPlay",
           C_WHITE);
      break;
    case GUI_WORLDS: {
      dirt_background(0, h);
      text_center(w / 2, 12, "Select World", C_WHITE);
      for (int k = 0; k < n_worlds; k++) {
        int x = w / 2 - 110, y = 30 + k * 30;
        if (k == wsel) {
          fill(x - 2, y - 2, 220, 30, C_70);
          fill(x - 1, y - 1, 218, 28, C_BLACK);
        }
        text(x + 2, y + 1, winfo[k].name, C_WHITE);
        char line[40];
        strcpy(line, winfo[k].mode ? "Creative Mode" : "Survival Mode");
        text(x + 2, y + 12, line, C_GRAY);
      }
      if (!n_worlds) text_center(w / 2, 80, "No worlds yet", C_GRAY);
      break;
    }
    case GUI_CREATE:
      dirt_background(0, h);
      text_center(w / 2, 20, "Create New World", C_WHITE);
      text(w / 2 - 100, 47, "World Name", C_GRAY);
      fill(w / 2 - 101, 59, 202, 22, C_GRAY);
      fill(w / 2 - 100, 60, 200, 20, C_BLACK);
      text(w / 2 - 96, 66, cur_name, C_LIGHT);
      text_center(w / 2, 126, create_mode ? "Unlimited resources, free flying and" : "Search for resources, crafting, gain",
                  C_GRAY);
      text_center(w / 2, 138, create_mode ? "destroy blocks instantly" : "levels, health", C_GRAY);
      text_center(w / 2, 160, "Seed: (random)", C_GRAY);
      break;
    case GUI_CONFIRM:
      dirt_background(0, h);
      text_center(w / 2, 70, "Are you sure you want to delete this world?", C_WHITE);
      text_center(w / 2, 90, n_worlds ? winfo[wsel].name : "", C_GRAY);
      break;
    case GUI_PAUSE:
      text_center(w / 2, 40, "Game menu", C_WHITE);
      break;
    case GUI_DEATH:
      text_big(w / 2 - text_width("You died!"), 60, "You died!", C_WHITE, 2);
      break;
    case GUI_CONTROLS: {
      /* the keys, plainly */
      dirt_background(0, h);
      text_center(w / 2, 8, "Controls", C_WHITE);
      static const char *const k[][2] = {
        {"arrows", "Look around"}, {"cos  ,  (  )", "Walk (like W A S D)"}, {"y=", "Jump (twice in Creative: fly)"},
        {"alpha", "Sneak"}, {"del", "Sprint"}, {"2nd", "Mine, attack"}, {"clear", "Place, use"},
        {"vars", "Inventory"}, {"1 to 9", "Pick a hotbar slot"}, {"x,T,0,n", "Drop the item"},
        {"(-)", "Pause"}, {"mode", "Save and quit"}};
      for (int i = 0; i < 12; i++) {
        text(w / 2 - 130, 26 + i * 15, k[i][0], C_YELLOW);
        text(w / 2 - 50, 26 + i * 15, k[i][1], C_WHITE);
      }
      break;
    }
  }
  draw_buttons();
}

/* ---------------------------------------------------------------- opening, input, drawing */
void gui_open(int screen) {
  int prev = gui;
  render_stale = 1;
  gui = screen;
  sel = 0;
  shown = false, full = true;
  if (screen == GUI_INVENTORY || screen == GUI_CRAFTING || screen == GUI_FURNACE || screen == GUI_CHEST ||
      screen == GUI_CREATIVE) {
    if (screen == GUI_CREATIVE) creative_scroll = 0, creative_list();
    layout();
    /* start on the hotbar's slot held */
    for (int i = 0; i < n_slots; i++)
      if (slots[i].s == held()) sel = i;
    craft_find();
  } else {
    if (screen == GUI_WORLDS) list_worlds();
    menu_layout();
  }
  (void)prev;
  hud_dirty = true;
}

void gui_close(void) {
  render_stale = 1;
  if (gui >= GUI_INVENTORY && gui <= GUI_CREATIVE) close_container();
  else gui = GUI_NONE;
  hud_dirty = true;
}

void gui_input(uint32_t keys, uint32_t pressed) {
  (void)keys;
  if (gui == GUI_NONE) return;
  bool container = gui >= GUI_INVENTORY && gui <= GUI_CREATIVE;
  if (container) {
    int dx = (pressed & K_RIGHT ? 1 : 0) - (pressed & K_LEFT ? 1 : 0), dy = (pressed & K_DOWN ? 1 : 0) - (pressed & K_UP ? 1 : 0);
    if (gui == GUI_CREATIVE && dy && sel < 45) {
      int row = sel / 9, rows = (n_creative + 8) / 9;
      if (dy < 0 && row == 0 && creative_scroll > 0) {
        creative_scroll--, layout(), shown = false;
        return;
      }
      if (dy > 0 && row == 4 && creative_scroll + 5 < rows) {
        creative_scroll++, layout(), shown = false;
        return;
      }
    }
    if (dx || dy) {
      int n = slot_towards(dx, dy);
      if (n >= 0) sel = n, shown = false;
    }
    if (pressed & K_OK) click(sel, 0, (keys & K_JUMP) != 0), shown = false;
    if (pressed & K_ENTER) click(sel, 1, false), shown = false;
    if (pressed & (K_ESC | K_INV)) gui_close();
    for (int k = 0; k < 9; k++)
      if ((pressed & K_SLOT(k)) && slots[sel].s && slots[sel].kind == SK_NORMAL) {
        /* a number: swap with that hotbar slot (Minecraft's number keys over a slot) */
        Stack t = pl.inv[k];
        pl.inv[k] = *slots[sel].s, *slots[sel].s = t;
        shown = false;
        if (gui == GUI_INVENTORY || gui == GUI_CRAFTING) craft_find();
      }
    return;
  }
  /* menus: up and down (and left and right in rows), 2nd or enter presses */
  if (gui == GUI_WORLDS && n_worlds && (pressed & (K_UP | K_DOWN)) && sel == 0) {
    /* the list: up and down choose the world while the first button is selected */
  }
  int old = sel;
  if (pressed & (K_DOWN | K_RIGHT | K_UP | K_LEFT)) {
    int dir = pressed & (K_DOWN | K_RIGHT) ? 1 : -1;
    if (gui == GUI_WORLDS && (pressed & (K_UP | K_DOWN)) && n_worlds > 1 && sel == 0 && !(pressed & (K_LEFT | K_RIGHT))) {
      wsel = (wsel + dir + n_worlds) % n_worlds;
      shown = false;
      return;
    }
    for (int k = 0; k < n_btns; k++) {
      sel = (sel + dir + n_btns) % n_btns;
      if (btns[sel].on) break;
    }
  }
  if (sel != old) shown = false;
  if ((pressed & K_ESC) && gui != GUI_TITLE && gui != GUI_DEATH) {
    if (gui == GUI_PAUSE) gui_close();
    else if (gui == GUI_WORLDS) gui_open(GUI_TITLE);
    else if (gui == GUI_CREATE || gui == GUI_CONFIRM) gui_open(GUI_WORLDS);
    else if (gui == GUI_CONTROLS) gui_close();
    return;
  }
  if (!(pressed & (K_OK | K_ENTER)) || !n_btns || !btns[sel].on) return;
  switch (btns[sel].act) {
    case BA_PLAY: gui_open(GUI_WORLDS); break;
    case BA_QUIT: menu_action = ACT_QUIT; break;
    case BA_SELECT: cur_world = wslot[wsel], menu_action = ACT_PLAY; break;
    case BA_CREATE: {
      create_mode = 0;
      /* a name not taken yet */
      strcpy(cur_name, "New World");
      for (int n = 2; n < 10; n++) {
        bool taken = false;
        for (int k = 0; k < n_worlds; k++)
          if (!strcmp(winfo[k].name, cur_name)) taken = true;
        if (!taken) break;
        strcpy(cur_name, "New World 2");
        cur_name[10] = (char)('0' + n);
      }
      gui_open(GUI_CREATE);
      break;
    }
    case BA_MODE: create_mode ^= 1, menu_layout(), shown = false; break;
    case BA_DO_CREATE: {
      extern int new_mode;
      new_mode = create_mode;
      menu_action = ACT_NEW;
      break;
    }
    case BA_DELETE: gui_open(GUI_CONFIRM); break;
    case BA_DO_DELETE:
      delete_world(wslot[wsel]);
      gui_open(GUI_WORLDS);
      break;
    case BA_CANCEL: gui_open(gui == GUI_WORLDS ? GUI_TITLE : GUI_WORLDS); break;
    case BA_RESUME: gui_close(); break;
    case BA_SAVE_QUIT: menu_action = ACT_SAVE_QUIT; break;
    case BA_RESPAWN: menu_action = ACT_RESPAWN; break;
    case BA_TITLE: menu_action = ACT_TITLE; break;
    case BA_DONE: gui_close(); break;
    case BA_DIST:
      /* (fewer blocks seen: more pictures a second) */
      view_dist = view_dist == 8 ? 10 : view_dist == 10 ? 13 : 8;
      options_save();
      menu_layout();
      shown = false;
      break;
  }
}

static void draw_panel_off(void) { draw_panel(); }
static void draw_menu_off(void) { draw_menu(); }
static void draw_buttons_off(void) { draw_buttons(); }

bool gui_needs_world(void) { return gui == GUI_NONE || gui == GUI_TITLE; }

/* draws the open screen (only what changed) */
void gui_draw(void) {
  if (gui == GUI_NONE || shown) return;
  bool container = gui >= GUI_INVENTORY && gui <= GUI_CREATIVE;
  bool over_world = container || gui == GUI_TITLE || gui == GUI_PAUSE || gui == GUI_DEATH;
  if (full) {
    /* the world behind a screen, darkened (Minecraft's grey veil; red-brown when you died) */
    if (over_world && gui != GUI_TITLE) {
      int in = NB_VEIL_IN;   /* (a panel's corners are rounded: its inside from 4 pixels in) */
      if (container) veil(14, panel_x + in, panel_y + in, panel_w - 2 * in, panel_h - 2 * in);
      else veil(gui == GUI_DEATH ? 12 : 14, 0, 0, 0, 0);
    }
    if (container) strip_keep(), offscreen(panel_x - 2, panel_y - 14, panel_w + 4, panel_h + 16, draw_panel_off);
    else offscreen(0, 0, SCREEN_W, SCREEN_H, draw_menu_off);
  } else if (container) offscreen(panel_x - 2, panel_y - 14, panel_w + 4, panel_h + 16, draw_panel_off);
  else if (over_world) offscreen(0, 0, SCREEN_W, SCREEN_H, draw_buttons_off);
  else offscreen(0, 0, SCREEN_W, SCREEN_H, draw_menu_off);
  shown = true, full = false;
}
void gui_redraw(void) { shown = false, full = true; }

/* ---------------------------------------------------------------- furnaces (TileEntityFurnace: 200 ticks an item) */
static int smelt_of(int id) {
  for (const uint8_t *s = smelt; s[0] != 255; s += 2)
    if (s[0] == id) return s[1] == 0 ? 0 : s[1];
  return 0;
}
static int fuel_of(int id) { return id ? it_fuel[id >= IT0 ? B_N + id - IT0 : id] : 0; }

void gui_tick(void) {
  if (tip_ticks > 0 && !--tip_ticks) hud_dirty = true;
  if (msg_ticks > 0 && !--msg_ticks) hud_dirty = true;
  for (int i = 0; i < n_tiles; i++) {
    Tile *t = &tiles[i];
    if (t->kind != B_FURNACE) continue;
    Stack *in = &t->s[0], *fu = &t->s[1], *out = &t->s[2];
    int r = in->id ? smelt_of(in->id) : 0;
    bool room = r && (!out->id || (out->id == r && out->n < item_max(r)));
    bool was = t->burn > 0;
    if (t->burn > 0) t->burn--;
    if (!t->burn && room && fuel_of(fu->id)) {
      t->burn = t->burn_max = (int16_t)(fuel_of(fu->id) > 32000 ? 32000 : fuel_of(fu->id));
      if (!--fu->n) fu->id = 0;
    }
    if (t->burn > 0 && room) {
      if (++t->cook >= 200) {
        t->cook = 0;
        if (!out->id) *out = (Stack){(uint8_t)r, 1, 0};
        else out->n++;
        if (!--in->n) in->id = 0;
        extern void xp_add(int n);
        if (r == I_IRON_INGOT || r == I_GOLD_INGOT || r == I_DIAMOND) xp_add(1);
      }
    } else if (t->cook > 0) t->cook = t->cook > 2 ? t->cook - 2 : 0;
    bool now = t->burn > 0;
    if (now != was) world_set(t->x, t->y, t->z, now ? B_FURNACE_LIT : B_FURNACE);
    if (gui == GUI_FURNACE && gui_tile == t && (now || was || t->cook)) shown = false;
  }
}
