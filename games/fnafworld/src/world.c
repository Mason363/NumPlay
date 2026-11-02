/* FNAF World: the overworld, as the overworld frame's events run it (events 1123-1153, 1394-1440, 1543-1559,
 * 1632-1699, 1786-1845): walking with the four feelers, the areas, encounters, chests and their chips, switches
 * and the barriers they open, the key and its doors, the bosses, the shops, the jumps.
 *
 * The map is tools/world.py's: drawn at 0.4 around the walker ("8bit hitbox", the game's coordinates). */
#include <string.h>
#include "anims.h"
#include "data.h"
#include "game.h"
#include "gfx.h"
#include "res.h"

#define W g.w
#define VW 800   /* the view, the game's pixels (320 x 240 at 0.4) */
#define VH 600

enum { O_ZONE, O_CHEST, O_COINS, O_BOSS, O_SWITCH, O_BARRIER, O_SHOP, O_KEY, O_PORT };

typedef struct { int16_t x, y; uint16_t w, h, spr; } Ground;
static const uint8_t *wsec;
static int ng, ni, no, wrows, wstride;
static const uint8_t *groundp, *instp, *objp, *wallp;

static void world_load(void) {
  if (wsec) return;
  wsec = section(SEC_WORLD);
  ng = (int)rd16(wsec), ni = (int)rd16(wsec + 2), no = (int)rd16(wsec + 4);
  wrows = (int)rd16(wsec + 6), wstride = (int)rd16(wsec + 8);
  groundp = wsec + 12;
  instp = groundp + 12 * ng;
  objp = instp + 6 * ni;
  wallp = objp + 14 * no;
}

typedef struct { uint8_t kind, n; int16_t x0, y0, x1, y1; uint16_t s0, s1; } Obj;
static Obj obj(int i) {
  const uint8_t *p = objp + 14 * i;
  Obj o = {p[0], p[1], (int16_t)rds16(p + 2), (int16_t)rds16(p + 4), (int16_t)rds16(p + 6), (int16_t)rds16(p + 8),
           (uint16_t)rd16(p + 10), (uint16_t)rd16(p + 12)};
  return o;
}

const char *area_name(int area) {
  switch (area) {
    case 2: return "Choppy's Woods";
    case 5: return "Dusting Fields";
    case 8: return "Lilygear Lake";
    case 11: return "Mysterious Mine";
    case 14: return "Blacktomb Yard";
    case 17: return "Deep-Metal Mine";
    case 20: return "Pinwheel Circus";
    case 23: return "&*___TWRE";
    case 26: return "Pinwheel Funhouse";
    case 50: return "Geist Lair";
    default: return "Fazbear Hills";
  }
}

/* is a barrier there (not opened yet)? 1670-1797 */
static bool barrier_on(int n) {
  if (n >= 1 && n <= 3) return !g.f.sw[n];
  if (n == 4) return !g.f.sw[4];
  if (n == 5) return !g.f.sw[5];
  if (n >= 6 && n <= 9) return !g.f.sw[n];
  if (n == 10) return !g.f.key;
  return false;
}

static bool wall_at(int x, int y) {
  if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return true;
  int cx = x / CELL, cy = y / CELL;
  if (cy >= wrows) return true;
  return wallp[cy * wstride + (cx >> 3)] >> (cx & 7) & 1;
}
/* a feeler (its box) on an obstacle backdrop ("feel top":C-23) */
static bool feel_wall(int x0, int y0, int w, int h) {
  for (int y = y0 - y0 % CELL; y < y0 + h; y += CELL)
    for (int x = x0 - x0 % CELL; x < x0 + w; x += CELL)
      if (wall_at(x < x0 ? x0 : x, y < y0 ? y0 : y)) return true;
  return false;
}
static bool overlap(int x0, int y0, int w, int h, const Obj *o) {
  return x0 < o->x1 && x0 + w > o->x0 && y0 < o->y1 && y0 + h > o->y0;
}
static bool feel_barrier(int x0, int y0, int w, int h) {
  for (int i = 0; i < no; i++) {
    Obj o = obj(i);
    if (o.kind == O_BARRIER && barrier_on(o.n) && overlap(x0, y0, w, h, &o)) return true;
  }
  return false;
}

void world_save_pos(void) {   /* 'x', 'y', 'area' and the time */
  g.f.x = W.x, g.f.y = W.y, g.f.area = W.area;
  g.dirty = 1;
}

void world_enter(void) {
  world_load();
  memset(&W, 0, sizeof W);
  W.x = START_X, W.y = START_Y, W.dir = 3;
  if (g.f.x > 0 && !g.f.resetpos) W.x = g.f.x, W.y = g.f.y, W.area = g.f.area;   /* 9 */
  if (g.f.resetpos) {   /* 1285: back at the start, the bosses back */
    g.f.resetpos = 0;
    world_save_pos();
  }
}

static void note(int n, int k) { W.note = (uint8_t)n, W.note_t = 150, W.note_n = (uint8_t)k; }
enum { N_NONE, N_CHIP, N_KEY, N_SWITCH, N_MEMBER, N_CHALLENGER, N_SAVED, N_COINS };

void world_step(void) {
  uint32_t k = g.keys;
  /* the clock ('hours', 'min', 'seconds') */
  if (++W.sec_t >= 60) {
    W.sec_t = 0;
    if (++g.f.seconds >= 60) {
      g.f.seconds = 0;
      if (++g.f.minutes >= 60) g.f.minutes = 0, g.f.hours = (uint8_t)(g.f.hours < 255 ? g.f.hours + 1 : 255);
    }
  }
  if (W.note_t > 0) W.note_t--;
  if (W.note == N_CHALLENGER) {   /* 1199-1202: the banner, then its battle */
    if (W.note_t == 0) {
      W.note = 0;
      battle_challenger(W.note_n);
      g.scene = SC_BATTLE;
    }
    return;
  }
  if (W.grace > 0) W.grace--;   /* 1136 */
  /* walking (1125-1128): 2 pixels a frame, if the feeler that way is free */
  W.walking = 0;
  int x = W.x, y = W.y;
  if ((k & K_UP) && !(k & K_DOWN) &&
      !feel_wall(x - FEEL_TOP_HX, y - 16 - FEEL_TOP_HY, FEEL_TOP_W, FEEL_TOP_H)) W.y -= 2, W.walking = 1, W.dir = 1;
  if ((k & K_DOWN) && !(k & K_UP) &&
      !feel_wall(x - FEEL_BOTTOM_HX, y + 14 - FEEL_BOTTOM_HY, FEEL_BOTTOM_W, FEEL_BOTTOM_H)) W.y += 2, W.walking = 1, W.dir = 3;
  if ((k & K_RIGHT) && !(k & K_LEFT) &&
      !feel_wall(x + 16 - FEEL_RIGHT_HX, y - FEEL_RIGHT_HY, FEEL_RIGHT_W, FEEL_RIGHT_H)) W.x += 2, W.walking = 1, W.dir = 0;
  if ((k & K_LEFT) && !(k & K_RIGHT) &&
      !feel_wall(x - 15 - FEEL_LEFT_HX, y - FEEL_LEFT_HY, FEEL_LEFT_W, FEEL_LEFT_H)) W.x -= 2, W.walking = 1, W.dir = 2;
  /* the barriers push back (1635-1664) */
  if (feel_barrier(W.x - FEEL_TOP_HX, W.y - 16 - FEEL_TOP_HY, FEEL_TOP_W, FEEL_TOP_H)) W.y += 20;
  if (feel_barrier(W.x - FEEL_BOTTOM_HX, W.y + 14 - FEEL_BOTTOM_HY, FEEL_BOTTOM_W, FEEL_BOTTOM_H)) W.y -= 20;
  if (feel_barrier(W.x + 16 - FEEL_RIGHT_HX, W.y - FEEL_RIGHT_HY, FEEL_RIGHT_W, FEEL_RIGHT_H)) W.x -= 20;
  if (feel_barrier(W.x - 15 - FEEL_LEFT_HX, W.y - FEEL_LEFT_HY, FEEL_LEFT_W, FEEL_LEFT_H)) W.x += 20;
  if (W.walking) W.img++;
  else W.img = 0;
  /* what the hitbox touches */
  int hx = W.x - HIT_HX, hy = W.y - HIT_HY;
  static uint8_t on_port, hold;
  bool any_port = false;
  for (int i = 0; i < no; i++) {
    Obj o = obj(i);
    if (!overlap(hx, hy, HIT_W, HIT_H, &o)) continue;
    switch (o.kind) {
      case O_ZONE:   /* 1142-1152 */
        if (W.area != o.n) {
          W.area = o.n;
          if (o.n == 5 && !g.f.w3) g.f.w3 = 1;   /* 1803 */
        }
        break;
      case O_CHEST:   /* 1419-1440 */
        if (!g.f.chest[o.n]) {
          g.f.chest[o.n] = 1;
          note(N_CHIP, o.n);
          world_save_pos();
        }
        break;
      case O_SWITCH:   /* 1665-1669, 1790-1793 */
        if (!g.f.sw[o.n]) {
          g.f.sw[o.n] = 1;
          note(N_SWITCH, o.n);
          world_save_pos();
        }
        break;
      case O_KEY:   /* 1788: the key, guarded by Bouncer */
        if (!g.f.key) {
          g.f.key = 1;
          note(N_KEY, 0);
          battle_start(9);
          g.scene = SC_BATTLE;
          return;
        }
        break;
      case O_BOSS:   /* 1543-1558 */
        if (!(W.boss_won[i >> 3] >> (i & 7) & 1) && o.n != 10 && o.n != 6) {
          W.boss_won[i >> 3] |= (uint8_t)(1 << (i & 7));
          battle_start(o.n);
          g.scene = SC_BATTLE;
          return;
        }
        break;
      case O_PORT: {   /* 1168-1185, 1393-1396, 1465-1504: to another place */
        any_port = true;
        bool go = false;
        if (o.n == 0) go = !on_port;
        else if (o.n == 3) go = (g.pressed & K_DOWN) != 0;
        else if (k & K_UP) go = ++hold > (o.n == 1 ? 75 : 25);
        else hold = 0;
        on_port = 1;
        if (go) {
          W.x = (int16_t)o.s0, W.y = (int16_t)o.s1;
          hold = 0;
          return;
        }
        break;
      }
      case O_SHOP:   /* 1696-1698, 1835-1838: after standing on it a moment */
        if (!W.walking && ++W.enc_t > 50) {
          W.enc_t = 0;
          world_save_pos();
          if (o.n == 1) g.shop_kind = (uint8_t)(W.area == 0 ? 1 : W.area), g.scene = SC_SHOP, g.t = 0, g.menu = 0;
          else if (o.n == 2) g.scene = SC_ARMOR, g.t = 0, g.menu = 0;
          return;
        }
        break;
    }
  }
  if (!any_port) on_port = 0, hold = 0;
  /* encounters (1133): every 2000 ms walking, one in four */
  W.sec_t += 0;
  static uint32_t enc_ms;
  if (W.walking) {
    enc_ms += 50;
    if (enc_ms >= 2000 * 3) {
      enc_ms -= 2000 * 3;
      if (!W.grace && rnd_n(4) == 1) {
        battle_start(0);
        g.scene = SC_BATTLE;
      }
    }
  }
}

/* after a battle (1198, 1240): a challenger, or a new member */
void world_after_battle(void) {
  Battle *b = &g.b;
  memcpy(g.f.slot, b->slots, 8);
  if (b->chal == 2 && b->chal_won) {
    if (!g.f.have[b->chal_c]) g.f.have[b->chal_c] = 1, note(N_MEMBER, b->chal_c);
    b->chal = 0;
  } else if (b->won && !b->boss && !b->chal && b->chal_roll == 1 && g.f.mode != 2 && g.f.lv[g.f.slot[0]] >= 3 &&
             W.area != 50) {
    int c = challenger_pick(W.area);
    if (c <= 40 && !g.f.have[c]) {
      note(N_CHALLENGER, c);
      W.note_t = 100;
    }
  }
  world_save_pos();
}

/* ------------------------------------------------------------------ drawing */
static int view_x, view_y;   /* the view's corner, at 0.4 */

void world_band_dim(void) {
  int vx = view_x, vy = view_y;
  gfx_fill(0, gfx_y0, SCREEN_W, BAND_H, RGB(0x203018));
  /* the ground */
  for (int i = 0; i < ng; i++) {
    const uint8_t *p = groundp + 12 * i;
    int x = (int)rds16(p) - vx, y = (int)rds16(p + 2) - vy, w = (int)rd16(p + 4), h = (int)rd16(p + 6);
    if (y >= gfx_y0 + BAND_H || y + h <= gfx_y0 || x >= SCREEN_W || x + w <= 0) continue;
    gfx_spr_tile((int)rd16(p + 8), x, y, w, h, x, y);
  }
  /* the pictures (in the game's order) */
  for (int i = 0; i < ni; i++) {
    const uint8_t *p = instp + 6 * i;
    int y = (int)rds16(p + 2) - vy;
    if (y >= gfx_y0 + BAND_H || y + 160 <= gfx_y0) continue;
    int x = (int)rds16(p) - vx, s = (int)rd16(p + 4);
    if (x >= SCREEN_W || x + 200 <= 0) continue;
    if (y + gfx_spr_h(s) <= gfx_y0 || x + gfx_spr_w(s) <= 0) continue;
    gfx_spr(s, x, y);
  }
  /* what the events act on */
  for (int i = 0; i < no; i++) {
    Obj o = obj(i);
    if (o.s0 == 0xFFFF || o.kind == O_ZONE || o.kind == O_PORT) continue;
    int s = o.s0;
    if (o.kind == O_CHEST && g.f.chest[o.n]) continue;
    if (o.kind == O_BARRIER && !barrier_on(o.n)) continue;
    if (o.kind == O_KEY && g.f.key) continue;
    if (o.kind == O_BOSS && (W.boss_won[i >> 3] >> (i & 7) & 1)) continue;
    if (o.kind == O_SWITCH && g.f.sw[o.n]) s = o.s1;
    int x = o.x0 * 2 / 5 - vx, y = o.y0 * 2 / 5 - vy;
    if (y >= gfx_y0 + BAND_H || y + gfx_spr_h(s) <= gfx_y0 || x >= SCREEN_W || x + gfx_spr_w(s) <= 0) continue;
    gfx_spr(s, x, y);
  }
  /* the walker ("Active 16" on the hitbox: anim 1 in its direction, anim 0 standing) */
  if (W.area == 8) {   /* 1161: on the lake, the raft ("lilly", at the walker -2, +9) */
    int s = SPR_LILLY + W.dir * LILLY_N + (W.img / 4) % LILLY_N;
    gfx_spr(s, (W.x - 2) * 2 / 5 - vx - gfx_spr_hx(s), (W.y + 9) * 2 / 5 - vy - gfx_spr_hy(s));
  } else {
    int s = W.walking ? SPR_WALK + W.dir * WALK_N + (W.img / 2) % WALK_N : SPR_STAND;
    if (!W.walking && W.dir != 3) s = SPR_WALK + W.dir * WALK_N;
    gfx_spr(s, W.x * 2 / 5 - vx - gfx_spr_hx(s), W.y * 2 / 5 - vy - gfx_spr_hy(s));
  }
  /* the corner: the area, the tokens */
  if (gfx_y0 < 16) {
    gfx_blend(0, 0, 320, 15, BLACK, 16);
    gfx_text_s(FONT_SMALL, 4, 1, area_name(W.area), WHITE);
    char t[24];
    uint32_t v = g.f.tokens;
    int p = 23;
    t[p] = 0;
    do t[--p] = (char)('0' + v % 10), v /= 10; while (v && p > 0);
    const char *lbl = " Faz Tokens";
    for (int i = 0; lbl[i] && p + i < 23; i++) {}
    gfx_text_r(FONT_SMALL, 316 - gfx_text_w(FONT_SMALL, " tokens"), 1, t + p, RGB(0xFFD040));
    gfx_text_r(FONT_SMALL, 316, 1, " tokens", WHITE);
  }
  if (gfx_y0 + BAND_H > 226) {
    gfx_blend(0, 226, 320, 14, BLACK, 16);
    gfx_text_s(FONT_SMALL, 4, 227, "Arrows walk  Back menu  Home save and quit", RGB(0xD0D8F0));
  }
  if (W.note_t > 0 && W.note) {
    char s2[48];
    const char *a = "", *b = "";
    switch (W.note) {
      case N_CHIP: a = "Found new chip!", b = CHIP_NAME[W.note_n]; break;
      case N_SWITCH: a = "A switch!", b = "Something opened."; break;
      case N_KEY: a = "Found a key!", b = "Locked doors open now."; break;
      case N_MEMBER: a = "You have a new party member!", b = CHAR_NAME[W.note_n]; break;
      case N_CHALLENGER: a = "A new challenger has appeared!", b = ""; break;
      case N_SAVED: a = "Saved.", b = ""; break;
    }
    (void)s2;
    int y = 92;
    gfx_fill(30, y, 260, b[0] ? 40 : 24, RGB(0x0A1430));
    gfx_frame_rect(30, y, 260, b[0] ? 40 : 24, WHITE);
    gfx_text_c(FONT_TEXT, 160, y + 4, a, WHITE);
    if (b[0]) gfx_text_c(FONT_TEXT, 160, y + 21, b, RGB(0xFFD040));
  }
}

void world_prepare_view(void) {
  int cx = W.x - VW / 2, cy = W.y - VH / 2;   /* 1123: the view on the hitbox */
  if (cx < 0) cx = 0;
  if (cy < 0) cy = 0;
  if (cx > MAP_W - VW) cx = MAP_W - VW;
  if (cy > MAP_H - VH) cy = MAP_H - VH;
  view_x = cx * 2 / 5, view_y = cy * 2 / 5;
}

void world_draw(void) {
  world_prepare_view();
  gfx_frame(world_band_dim);
}
