/* Among Us: the ship around the player: the floor, doors, vents, bodies and players in depth order (objects in
 * front drawn again over whoever is behind them), then the dark outside the player's light. */
#include <math.h>
#include <string.h>
#include "game.h"
#include "gfx.h"
#include "view.h"
#include "world.h"

View view;

/* ---- the camera: the ship's pixels at the screen's top-left ---- */

static inline int sx_of(float x) { return (int)floorf(x * PX_PER_UNIT + SHIP_OX) - view.cx; }
static inline int sy_of(float y) { return (int)floorf(-y * PX_PER_UNIT + SHIP_OY) - view.cy; }

/* ---- the floor ---- */

static void draw_tiles(void) {
  const ShipTiles *S = (const ShipTiles *)blob(BLOB_SHIP);
  const uint16_t *map = ship_map(S);
  int y0 = gfx_y0 + view.cy, y1 = y0 + BAND;
  int ty0 = y0 >> 4, ty1 = (y1 - 1) >> 4;
  int tx0 = view.cx >> 4, tx1 = (view.cx + SCREEN_W - 1) >> 4;
  for (int ty = ty0; ty <= ty1; ty++) {
    int r0 = ty * 16 < y0 ? y0 - ty * 16 : 0, r1 = ty * 16 + 16 > y1 ? y1 - ty * 16 : 16;
    for (int tx = tx0; tx <= tx1; tx++) {
      int sx = tx * 16 - view.cx;
      int c0 = sx < 0 ? -sx : 0, c1 = sx + 16 > SCREEN_W ? SCREEN_W - sx : 16;
      unsigned t = (tx >= 0 && ty >= 0 && tx < S->tw && ty < S->th) ? map[ty * S->tw + tx] : 0xFFFF;
      if (t == 0xFFFF) {
        /* space, and its stars */
        for (int r = r0; r < r1; r++) {
          uint16_t *row = gfx_band + (ty * 16 + r - view.cy - gfx_y0) * SCREEN_W + sx;
          for (int c = c0; c < c1; c++) row[c] = 0;
        }
        uint32_t h = (uint32_t)tx * 73856093u ^ (uint32_t)ty * 19349663u;
        h ^= h >> 13, h *= 0x5bd1e995u, h ^= h >> 15;
        if ((h & 3) == 0) {
          int px = (int)(h >> 8 & 15), py = (int)(h >> 12 & 15);
          if (py >= r0 && py < r1 && px >= c0 && px < c1)
            gfx_band[(ty * 16 + py - view.cy - gfx_y0) * SCREEN_W + sx + px] = (h >> 20 & 1) ? 0xFFFF : 0x8C71;
        }
        continue;
      }
      const uint8_t *tile = ship_tile(S, (int)t);
      const uint16_t *pal = (const uint16_t *)(const void *)tile;
      const uint8_t *px = tile + 32;
      for (int r = r0; r < r1; r++) {
        uint16_t *row = gfx_band + (ty * 16 + r - view.cy - gfx_y0) * SCREEN_W + sx;
        const uint8_t *src = px + r * 8;
        int c = c0;
        if (c & 1) row[c] = pal[src[c >> 1] & 15], c++;
        for (; c + 1 < c1; c += 2) {
          uint8_t b = src[c >> 1];
          row[c] = pal[b >> 4], row[c + 1] = pal[b & 15];
        }
        if (c < c1) row[c] = pal[src[c >> 1] >> 4];
      }
    }
  }
}

/* ---- the things in depth order ---- */

enum { D_PLAYER, D_BODY, D_DOOR, D_FRONT };
typedef struct {
  float z;
  uint8_t kind, i;
  int16_t x, y;          /* screen position of its pivot */
  int16_t x0, y0, x1, y1; /* its rectangle */
  const Pic *pic;
  const uint16_t *pal;
  uint8_t flip, alpha;
} Item;
#define MAX_ITEMS (MAXP * 2 + 20)
static Item items[MAX_ITEMS];
static int nitems;
/* front objects near the screen */
static uint8_t fronts[N_FRONT];
static int nfronts;

static void add(int kind, int i, float z, int x, int y, const Pic *p, const uint16_t *pal, bool flip, int alpha) {
  if (!p || nitems >= MAX_ITEMS) return;
  Item *it = &items[nitems];
  int x0 = x - (flip ? p->w - p->ox : p->ox), y0 = y - p->oy;
  if (x0 >= SCREEN_W || y0 >= SCREEN_H || x0 + p->w <= 0 || y0 + p->h <= 0) return;
  *it = (Item){z, (uint8_t)kind, (uint8_t)i, (int16_t)x, (int16_t)y, (int16_t)x0, (int16_t)y0, (int16_t)(x0 + p->w),
               (int16_t)(y0 + p->h), p, pal, flip, (uint8_t)(alpha > 255 ? 255 : alpha)};
  nitems++;
}

const Pic *player_frame(const Player *P, float t) {
  if (!P->alive) return pic(PIC_GHOST + ((int)(t * 12) % N_GHOST));
  if (P->vent_anim == 1) {
    int f = (int)(P->anim_t * 24);
    return f < 7 ? pic(PIC_JUMP + f) : NULL;
  }
  if (P->vent_anim == 2) {
    int f = (int)(P->anim_t * 24) - 1;
    if (f < 0) return NULL;
    return pic(PIC_JUMP + (f < 7 ? 6 - f : 0));
  }
  if (P->walking) return pic(PIC_WALK + ((int)(P->anim_t * 24) % 12));
  return pic(PIC_IDLE);
}

bool player_visible(int j) {
  const Player *M = &g.p[g.me], *Q = &g.p[j];
  if (j == g.me) return Q->vent == NONE || Q->vent_anim;
  if (Q->vent != NONE && !Q->vent_anim) return false;
  if (!M->alive) return true;
  if (!Q->alive) return false;
  float dx = Q->pos.x - M->pos.x, dy = Q->pos.y - M->pos.y, r = view.radius + 0.3f;
  if (dx * dx + dy * dy > r * r) return false;
  return !ship_light_between((V2){M->pos.x, M->pos.y + FEET_Y}, (V2){Q->pos.x, Q->pos.y + FEET_Y});
}

static bool body_visible(const Body *B) {
  const Player *M = &g.p[g.me];
  if (!M->alive) return true;
  float dx = B->pos.x - M->pos.x, dy = B->pos.y - M->pos.y, r = view.radius + 0.3f;
  if (dx * dx + dy * dy > r * r) return false;
  return !ship_light_between((V2){M->pos.x, M->pos.y + FEET_Y}, B->pos);
}

void view_prepare(void) {
  const Player *M = &g.p[g.me];
  view.cx = (int)floorf(g.cam.x * PX_PER_UNIT + SHIP_OX) - SCREEN_W / 2;
  view.cy = (int)floorf(-g.cam.y * PX_PER_UNIT + SHIP_OY) - SCREEN_H / 2;
  view.radius = vision_radius(g.me);
  view.dark = M->alive;
  if (view.dark) {
    V2 c = {M->pos.x, M->pos.y + FEET_Y};
    float pts[2 * VIEW_MAXPTS];
    view.npts = ship_vision(c, view.radius, pts, VIEW_MAXPTS);
    for (int k = 0; k < view.npts; k++) {
      view.pts[2 * k] = pts[2 * k] * PX_PER_UNIT + SHIP_OX - view.cx;
      view.pts[2 * k + 1] = -pts[2 * k + 1] * PX_PER_UNIT + SHIP_OY - view.cy;
    }
  }
  nitems = 0;
  /* doors */
  for (int d = 0; d < N_DOORS; d++) {
    const DoorRec *D = &DOORS[d];
    float a = g.door_anim[d];
    int f;
    if (g.door_t[d] > 0) f = (int)(a * 24) < DOOR_CLOSE_FRAMES ? (int)(a * 24) : DOOR_CLOSE_FRAMES - 1;
    else f = (int)(a * 24) < DOOR_OPEN_FRAMES ? DOOR_CLOSE_FRAMES + (int)(a * 24) : 0;
    add(D_DOOR, d, D->y / 1000.0f, sx_of(D->x), sy_of(D->y), pic((D->front ? PIC_DOOR_FRONT : PIC_DOOR_SIDE) + f), NULL,
        false, 256);
  }
  /* bodies */
  for (int b = 0; b < MAXP; b++) {
    const Body *B = &g.bodies[b];
    if (!B->on || !body_visible(B)) continue;
    add(D_BODY, b, B->pos.y / 1000.0f, sx_of(B->pos.x + 0.04f), sy_of(B->pos.y - 0.016f), pic(PIC_DEAD),
        color_pal[g.p[B->who].color], false, 256);
  }
  /* players: the dead only for the dead, see-through */
  for (int j = 0; j < g.nplayers; j++) {
    const Player *Q = &g.p[j];
    if (!player_visible(j)) continue;
    const Pic *p = player_frame(Q, g.t + j * 0.37f);
    add(D_PLAYER, j, Q->pos.y / 1000.0f, sx_of(Q->pos.x), sy_of(Q->pos.y + FEET_Y), p, color_pal[Q->color], Q->flip,
        Q->alive ? 256 : 160);
  }
  /* back to front (the larger z farther) */
  for (int a = 1; a < nitems; a++) {
    Item v = items[a];
    int b = a - 1;
    while (b >= 0 && items[b].z < v.z) items[b + 1] = items[b], b--;
    items[b + 1] = v;
  }
  nfronts = 0;
  for (int f = 0; f < N_FRONT; f++) {
    const Pic *p = pic(FRONT[f].pic);
    int x = FRONT[f].x - view.cx, y = FRONT[f].y - view.cy;
    if (x < SCREEN_W && y < SCREEN_H && x + p->w > 0 && y + p->h > 0) fronts[nfronts++] = (uint8_t)f;
  }
}

static void draw_items(void) {
  for (int k = 0; k < nitems; k++) {
    const Item *it = &items[k];
    if (!gfx_rows(it->y0, it->y1)) continue;
    gfx_spr(it->pic, it->x, it->y, it->flip, it->pal, it->alpha);
    if (it->kind == D_DOOR) continue;
    /* what stands in front of it, drawn again over it */
    for (int n = 0; n < nfronts; n++) {
      const FrontRec *F = &FRONT[fronts[n]];
      if (F->z >= it->z) continue;
      const Pic *p = pic(F->pic);
      int x = F->x - view.cx, y = F->y - view.cy;
      if (x >= it->x1 || y >= it->y1 || x + p->w <= it->x0 || y + p->h <= it->y0) continue;
      gfx_spr_clip(p, x, y, false, NULL, 256, it->x0, it->y0, it->x1, it->y1);
    }
    /* doors in front of it */
    for (int n = k + 1; n < nitems; n++) {
      const Item *d = &items[n];
      if (d->kind != D_DOOR || d->x0 >= it->x1 || d->y0 >= it->y1 || d->x1 <= it->x0 || d->y1 <= it->y0) continue;
      gfx_spr_clip(d->pic, d->x, d->y, false, NULL, 256, it->x0, it->y0, it->x1, it->y1);
    }
  }
}

/* the vents' lids while someone jumps in or out (enterVent: 18 frames at 24 a second) */
static void draw_vents(void) {
  for (int v = 0; v < N_VENTS; v++) {
    float a = g.t - g.vent_t[v];
    if (a < 0 || a >= N_VENT_FRAMES / 24.0f) continue;
    int f = (int)(a * 24);
    gfx_spr(pic(PIC_VENT + f), sx_of(VENTS[v].x), sy_of(VENTS[v].y), false, NULL, 256);
  }
}

/* outside the light: the ship darker (and nobody there) */
static void draw_dark(void) {
  if (!view.dark) return;
  /* the polygon's edges that reach this band */
  static float e[4 * 256];
  int ne = 0;
  float b0 = (float)gfx_y0, b1 = (float)(gfx_y0 + BAND);
  for (int i = 0, j = view.npts - 1; i < view.npts; j = i++) {
    float yi = view.pts[2 * i + 1], yj = view.pts[2 * j + 1];
    if ((yi < b0 && yj < b0) || (yi > b1 && yj > b1) || yi == yj || ne >= 256) continue;
    e[4 * ne] = view.pts[2 * j], e[4 * ne + 1] = yj, e[4 * ne + 2] = view.pts[2 * i], e[4 * ne + 3] = yi;
    ne++;
  }
  for (int r = 0; r < BAND; r++) {
    float yc = gfx_y0 + r + 0.5f, xs[48];
    int k = 0;
    for (int n = 0; n < ne && k < 48; n++) {
      const float *q = e + 4 * n;
      if ((q[1] > yc) != (q[3] > yc)) xs[k++] = q[0] + (yc - q[1]) * (q[2] - q[0]) / (q[3] - q[1]);
    }
    for (int a = 1; a < k; a++) {
      float v = xs[a];
      int b = a - 1;
      while (b >= 0 && xs[b] > v) xs[b + 1] = xs[b], b--;
      xs[b + 1] = v;
    }
    uint16_t *row = gfx_band + r * SCREEN_W;
    int x = 0;
    for (int s = 0; s + 1 < k; s += 2) {
      int xa = (int)ceilf(xs[s] - 0.5f), xb = (int)ceilf(xs[s + 1] - 0.5f);
      if (xa > SCREEN_W) xa = SCREEN_W;
      if (xb > SCREEN_W) xb = SCREEN_W;
      for (; x < xa; x++) row[x] = (uint16_t)(((row[x] >> 2) & 0x39E7) + ((row[x] >> 3) & 0x18E3));
      if (xb > x) x = xb;
    }
    for (; x < SCREEN_W; x++) row[x] = (uint16_t)(((row[x] >> 2) & 0x39E7) + ((row[x] >> 3) & 0x18E3));
  }
}

static void draw_names(void) {
  const Player *M = &g.p[g.me];
  for (int k = 0; k < nitems; k++) {
    const Item *it = &items[k];
    if (it->kind != D_PLAYER) continue;
    const Player *Q = &g.p[it->i];
    uint16_t c = (M->imp && Q->imp) ? rgb565(0xFF1919) : 0xFFFF;
    gfx_text(FONT_SMALL, it->x, it->y - 19, COLOR_NAMES[Q->color], c, 0, 1, 1);
  }
}

void view_draw(void) {
  draw_tiles();
  draw_vents();
  draw_items();
  draw_dark();
  draw_names();
}

/* the ship seen through a camera (scale px a unit, centred on c, in the rectangle) */
void view_camera(V2 c, float scale, int rx, int ry, int rw, int rh) {
  const ShipTiles *S = (const ShipTiles *)blob(BLOB_SHIP);
  const uint16_t *map = ship_map(S);
  float k = PX_PER_UNIT / scale;
  int y0 = ry > gfx_y0 ? ry : gfx_y0, y1 = ry + rh < gfx_y0 + BAND ? ry + rh : gfx_y0 + BAND;
  float sx0 = c.x * PX_PER_UNIT + SHIP_OX - (rw / 2) * k, sy0 = -c.y * PX_PER_UNIT + SHIP_OY - (rh / 2) * k;
  for (int y = y0; y < y1; y++) {
    uint16_t *row = gfx_band + (y - gfx_y0) * SCREEN_W;
    int py = (int)(sy0 + (y - ry) * k);
    for (int x = rx; x < rx + rw; x++) {
      int px = (int)(sx0 + (x - rx) * k);
      int tx = px >> 4, ty = py >> 4;
      unsigned t = (px >= 0 && py >= 0 && tx < S->tw && ty < S->th) ? map[ty * S->tw + tx] : 0xFFFF;
      if (t == 0xFFFF) {
        row[x] = 0;
        continue;
      }
      const uint8_t *tile = ship_tile(S, (int)t);
      uint8_t b = tile[32 + (py & 15) * 8 + ((px & 15) >> 1)];
      row[x] = ((const uint16_t *)(const void *)tile)[px & 1 ? b & 15 : b >> 4];
    }
  }
  /* who is there */
  for (int j = 0; j < g.nplayers; j++) {
    const Player *Q = &g.p[j];
    if (!Q->alive || (Q->vent != NONE && !Q->vent_anim)) continue;
    float px = rx + rw / 2 + (Q->pos.x - c.x) * scale, py = ry + rh / 2 - (Q->pos.y + FEET_Y - c.y) * scale;
    if (px < rx - 20 || px > rx + rw + 20 || py < ry - 20 || py > ry + rh + 20) continue;
    const Pic *p = player_frame(Q, g.t);
    if (!p) continue;
    float s = scale / PX_PER_UNIT;
    int x0 = (int)px, yy0 = (int)py;
    (void)x0, (void)yy0;
    gfx_pic(p, px, py, Q->flip ? -s : s, s, 0, color_pal[Q->color], 256);
  }
  for (int b = 0; b < MAXP; b++) {
    const Body *B = &g.bodies[b];
    if (!B->on) continue;
    float px = rx + rw / 2 + (B->pos.x - c.x) * scale, py = ry + rh / 2 - (B->pos.y - c.y) * scale;
    if (px < rx - 20 || px > rx + rw + 20 || py < ry - 20 || py > ry + rh + 20) continue;
    float s = scale / PX_PER_UNIT;
    gfx_pic(pic(PIC_DEAD), px, py, s, s, 0, color_pal[g.p[B->who].color], 256);
  }
}
