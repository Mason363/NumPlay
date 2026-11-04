/* Drawing (draw.h): the level into the launcher's strip (gfx_buf, rows gfx_y0 to gfx_y1), clipped to the field. */
#include <math.h>
#include <string.h>
#include "draw.h"
#include "game.h"
#include "res.h"
#include "world.h"

static int clip_x0 = 0, clip_x1 = SCREEN_W;
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void draw_pic(int pic, int x, int y) {
  const Pic *p = res_pic(pic);
  int y0 = y > gfx_y0 ? y : gfx_y0, y1 = y + p->h < gfx_y1 ? y + p->h : gfx_y1;
  int x0 = x > clip_x0 ? x : clip_x0, x1 = x + p->w < clip_x1 ? x + p->w : clip_x1;
  if (y0 >= y1 || x0 >= x1) return;
  const uint16_t *pal = res_pic_palette(p);
  const uint8_t *px = res_pic_pixels(p);
  bool wide = p->flags & 2, opaque = p->flags & 1;
  for (int yy = y0; yy < y1; yy++) {
    uint32_t row = (uint32_t)(yy - y) * p->w;
    color_t *d = gfx_buf + (yy - gfx_y0) * SCREEN_W;
    for (int xx = x0; xx < x1; xx++) {
      uint32_t at = row + (uint32_t)(xx - x);
      unsigned k = wide ? px[at] : (px[at >> 1] >> ((at & 1) * 4)) & 15;
      if (k || opaque) d[xx] = pal[k];
    }
  }
}

void draw_pic_at(int pic, float sx, float sy, float scale, float sn, float cs, bool flip) {
  if (scale < 0.05f) return;
  const Pic *p = res_pic(pic);
  float px = p->px4 / 4.0f, py = p->py4 / 4.0f;
  float ex = fmaxf(px, p->w - px), ey = fmaxf(py, p->h - py);
  float r = sqrtf(ex * ex + ey * ey) * scale + 1;
  int y0 = (int)floorf(sy - r), y1 = (int)ceilf(sy + r), x0 = (int)floorf(sx - r), x1 = (int)ceilf(sx + r);
  if (y1 <= gfx_y0 || y0 >= gfx_y1 || x1 <= clip_x0 || x0 >= clip_x1) return;
  y0 = clampi(y0, gfx_y0, gfx_y1), y1 = clampi(y1, gfx_y0, gfx_y1);
  x0 = clampi(x0, clip_x0, clip_x1), x1 = clampi(x1, clip_x0, clip_x1);
  const uint16_t *pal = res_pic_palette(p);
  const uint8_t *pix = res_pic_pixels(p);
  float inv = 1 / scale, fl = flip ? -1.0f : 1.0f;
  int32_t dux = (int32_t)lroundf(cs * inv * fl * 65536), dvx = (int32_t)lroundf(-sn * inv * 65536);
  int32_t duy = (int32_t)lroundf(sn * inv * fl * 65536), dvy = (int32_t)lroundf(cs * inv * 65536);
  float fx = x0 + 0.5f - sx, fy = y0 + 0.5f - sy;
  int32_t u0 = (int32_t)lroundf(((fx * cs + fy * sn) * inv * fl + px) * 65536);
  int32_t v0 = (int32_t)lroundf(((-fx * sn + fy * cs) * inv + py) * 65536);
  uint32_t w = p->w, h = p->h;
  bool wide = p->flags & 2;
  for (int yy = y0; yy < y1; yy++, u0 += duy, v0 += dvy) {
    color_t *d = gfx_buf + (yy - gfx_y0) * SCREEN_W;
    int32_t u = u0, v = v0;
    for (int xx = x0; xx < x1; xx++, u += dux, v += dvx) {
      if (u < 0 || v < 0) continue;
      uint32_t iu = (uint32_t)u >> 16, iv = (uint32_t)v >> 16;
      if (iu >= w || iv >= h) continue;
      uint32_t at = iv * w + iu;
      unsigned k = wide ? pix[at] : (pix[at >> 1] >> ((at & 1) * 4)) & 15;
      if (k) d[xx] = pal[k];
    }
  }
}

static inline void plot(int x, int y, color_t c) {
  if (y >= gfx_y0 && y < gfx_y1 && x >= clip_x0 && x < clip_x1) gfx_buf[(y - gfx_y0) * SCREEN_W + x] = c;
}

void draw_line(float x0, float y0, float x1, float y1, color_t c) {
  float dx = x1 - x0, dy = y1 - y0;
  float n = fmaxf(fabsf(dx), fabsf(dy));
  if ((y0 < gfx_y0 - 1 && y1 < gfx_y0 - 1) || (y0 > gfx_y1 && y1 > gfx_y1)) return;
  int steps = (int)n + 1;
  if (steps > 2000) steps = 2000;
  float sx = n > 0 ? dx / n : 0, sy = n > 0 ? dy / n : 0;
  for (int i = 0; i < steps; i++) plot((int)floorf(x0 + sx * i), (int)floorf(y0 + sy * i), c);
}

void draw_disc(float cx, float cy, float r, color_t c) {
  int y0 = clampi((int)floorf(cy - r), gfx_y0, gfx_y1), y1 = clampi((int)ceilf(cy + r) + 1, gfx_y0, gfx_y1);
  for (int y = y0; y < y1; y++) {
    float dy = y + 0.5f - cy, w2 = r * r - dy * dy;
    if (w2 < 0) continue;
    float w = sqrtf(w2);
    int a = clampi((int)ceilf(cx - w - 0.5f), clip_x0, clip_x1), b = clampi((int)floorf(cx + w - 0.5f) + 1, clip_x0, clip_x1);
    color_t *d = gfx_buf + (y - gfx_y0) * SCREEN_W;
    for (int x = a; x < b; x++) d[x] = c;
  }
}

/* ------------------------------------------------------------------ polygons */
#define MAXP (SCRATCH_FLOATS / 2)
#define px_ mb_scratch
#define py_ (mb_scratch + MAXP)
static uint16_t cstart[17];
static int ncont;

enum { PAT_SOLID, PAT_BRICKS, PAT_GLASS };
typedef struct {
  color_t fill, alt, edge, edge2;
  uint8_t pattern;
} Style;

static void fill(const Style *st, float ymin, float ymax) {
  int ya = clampi((int)floorf(ymin), gfx_y0, gfx_y1), yb = clampi((int)ceilf(ymax) + 1, gfx_y0, gfx_y1);
  float xs[64];
  for (int y = ya; y < yb; y++) {
    float yc = y + 0.5f;
    int n = 0;
    for (int c = 0; c < ncont; c++) {
      int s = cstart[c], e = cstart[c + 1];
      for (int i = s; i < e; i++) {
        int j = i + 1 < e ? i + 1 : s;
        float ay = py_[i], by = py_[j];
        if ((ay <= yc) == (by <= yc)) continue;
        if (n < 64) xs[n++] = px_[i] + (yc - ay) * (px_[j] - px_[i]) / (by - ay);
      }
    }
    for (int i = 1; i < n; i++) {
      float v = xs[i];
      int k = i - 1;
      while (k >= 0 && xs[k] > v) xs[k + 1] = xs[k], k--;
      xs[k + 1] = v;
    }
    color_t *d = gfx_buf + (y - gfx_y0) * SCREEN_W;
    for (int i = 0; i + 1 < n; i += 2) {
      int a = clampi((int)ceilf(xs[i] - 0.5f), clip_x0, clip_x1);
      int b = clampi((int)ceilf(xs[i + 1] - 0.5f), clip_x0, clip_x1);
      if (st->pattern == PAT_BRICKS) {
        int row = y / 4, mortar_row = (y & 3) == 0;
        for (int x = a; x < b; x++) d[x] = (mortar_row || ((x + (row & 1) * 5) % 10) == 0) ? st->alt : st->fill;
      } else if (st->pattern == PAT_GLASS) {
        for (int x = a; x < b; x++) d[x] = ((x + y) % 9 < 2) ? st->alt : st->fill;
      } else {
        for (int x = a; x < b; x++) d[x] = st->fill;
      }
    }
  }
  /* the edge */
  for (int c = 0; c < ncont; c++) {
    int s = cstart[c], e = cstart[c + 1];
    for (int i = s; i < e; i++) {
      int j = i + 1 < e ? i + 1 : s;
      draw_line(px_[i], py_[i], px_[j], py_[j], st->edge);
      if (st->edge2 != st->edge) draw_line(px_[i], py_[i] + 1, px_[j], py_[j] + 1, st->edge2);
    }
  }
}

static const color_t SWITCH_COL[] = {HEX(0xE53935), HEX(0x1E88E5), HEX(0x43A047), HEX(0xFDD835),
                                     HEX(0x8E24AA), HEX(0xFB8C00)};

static void style_of(const Obj *o, Style *st) {
  uint32_t f = o->flags;
  st->pattern = PAT_SOLID;
  if (o->sw) {
    color_t c = SWITCH_COL[g.sw[o->sw - 1].colour % 6];
    st->fill = c, st->edge = st->edge2 = gfx_mix(c, 0, 12);
  } else if (f & F_BE) {
    st->fill = HEX(0xE0352B), st->edge = st->edge2 = HEX(0x7A1410);
  } else if (f & F_BR) {
    st->fill = HEX(0x4DBDAE), st->alt = HEX(0x8FDDD1), st->edge = st->edge2 = HEX(0x2A7C73);
    st->pattern = PAT_GLASS;
  } else if (f & F_BO) {   /* a TNT crate */
    st->fill = HEX(0xE8392F), st->edge = st->edge2 = HEX(0x9C1F17);
  } else if (f & F_HV) {
    st->fill = HEX(0x8D99A6), st->edge = st->edge2 = HEX(0x3F4952);
  } else if (f & F_F) {
    st->fill = HEX(0x4B5459), st->alt = HEX(0x343C3F), st->edge = HEX(0xA5A6A8), st->edge2 = HEX(0x67767D);
    st->pattern = PAT_BRICKS;
  } else {
    st->fill = HEX(0xC98A45), st->edge = st->edge2 = HEX(0x7D5124);
  }
}

static float bb_y0[MAX_OBJ], bb_y1[MAX_OBJ];

static void draw_obj(int i) {
  const Obj *o = &g.obj[i];
  if (o->gone || o->kind == K_POINT) return;
  float bx = o->cx, by = o->cy, s = 0, c = 1;
  if (o->body >= 0 && world_alive(o->body)) world_xf(o->body, &bx, &by, &s, &c);
  Style st;
  style_of(o, &st);
  if (o->kind == K_SEG) {   /* a ball */
    float x = SX(bx), y = SY(by), r = o->r * FIELD_S;
    if (y + r < gfx_y0 || y - r > gfx_y1) return;
    draw_disc(x, y, r + 0.6f, st.edge);
    draw_disc(x, y, r - 0.6f, st.fill);
    return;
  }
  if (gfx_y0 == 0) {
    /* the first strip: its rows on the screen */
    const uint8_t *q = o->outline;
    float lo = 1e9f, hi = -1e9f;
    for (int k = 0; k < o->nout; k++) {
      float lx = (int16_t)(q[0] | q[1] << 8) * 0.125f, ly = (int16_t)(q[2] | q[3] << 8) * 0.125f;
      float y = SY(by + s * lx + c * ly);
      if (y < lo) lo = y;
      if (y > hi) hi = y;
      q += 4;
    }
    bb_y0[i] = lo, bb_y1[i] = hi;
  }
  if (bb_y1[i] + 2 < gfx_y0 || bb_y0[i] - 2 > gfx_y1) return;
  const uint8_t *q = o->outline;
  int n = 0;
  ncont = 0;
  cstart[0] = 0;
  int total = o->nout;
  const uint8_t *hq = o->outline + 4 * o->nout + 1;   /* (the holes, past their number) */
  for (int part = 0; part <= o->nholes && ncont < 16; part++) {
    if (part) {
      total = *hq++;
      q = hq;
      hq += 4 * total;
    }
    for (int k = 0; k < total && n < MAXP; k++, q += 4) {
      float lx = (int16_t)(q[0] | q[1] << 8) * 0.125f, ly = (int16_t)(q[2] | q[3] << 8) * 0.125f;
      px_[n] = SX(bx + c * lx - s * ly), py_[n] = SY(by + s * lx + c * ly);
      n++;
    }
    cstart[++ncont] = (uint16_t)n;
  }
  fill(&st, bb_y0[i], bb_y1[i]);
  /* a TNT crate's word, while it stands (it goes off before it can tumble far) */
  if ((o->flags & F_BO) && !o->sw && fabsf(s) < 0.34f && c > 0 && n) {
    float mx = 0, my = 0, w0 = 1e9f, w1 = -1e9f;
    for (int k = 0; k < cstart[1]; k++) {
      mx += px_[k], my += py_[k];
      if (px_[k] < w0) w0 = px_[k];
      if (px_[k] > w1) w1 = px_[k];
    }
    mx /= cstart[1], my /= cstart[1];
    if (w1 - w0 >= 18 && bb_y1[i] - bb_y0[i] >= 10 && my - 6 < gfx_y1 && my + 6 > gfx_y0)
      gfx_text_center(&np_font_small, (int)mx, (int)my + 4, "TNT", HEX(0xFFFFFF), 256);
  }
}

/* ------------------------------------------------------------------ characters */
static const color_t COLS[3][6] = {COLOURS_HERO, COLOURS_ENEMY, COLOURS_HOSTAGE};

static void quad(float x, float y, float s, float c, float w, float h, float from, float to, color_t col) {
  /* the part of a piece from `from` to `to` of its length (0 top, 1 bottom) */
  float rx = c * w / 2, ry = s * w / 2, dx = -s * h, dy = c * h;
  float tx = x - dx / 2, ty = y - dy / 2;
  float ax = tx + dx * from, ay = ty + dy * from, bx = tx + dx * to, by = ty + dy * to;
  px_[0] = SX(ax - rx), py_[0] = SY(ay - ry);
  px_[1] = SX(ax + rx), py_[1] = SY(ay + ry);
  px_[2] = SX(bx + rx), py_[2] = SY(by + ry);
  px_[3] = SX(bx - rx), py_[3] = SY(by - ry);
  float lo = fminf(fminf(py_[0], py_[1]), fminf(py_[2], py_[3])), hi = fmaxf(fmaxf(py_[0], py_[1]), fmaxf(py_[2], py_[3]));
  if (hi + 1 < gfx_y0 || lo - 1 > gfx_y1) return;
  ncont = 1, cstart[0] = 0, cstart[1] = 4;
  Style st = {col, col, col, col, PAT_SOLID};
  /* (no edge: draw its fill only) */
  int ya = clampi((int)floorf(lo), gfx_y0, gfx_y1), yb = clampi((int)ceilf(hi) + 1, gfx_y0, gfx_y1);
  (void)st;
  for (int yy = ya; yy < yb; yy++) {
    float yc = yy + 0.5f, xs[4];
    int n = 0;
    for (int i = 0; i < 4; i++) {
      int j = (i + 1) & 3;
      if ((py_[i] <= yc) == (py_[j] <= yc)) continue;
      xs[n++] = px_[i] + (yc - py_[i]) * (px_[j] - px_[i]) / (py_[j] - py_[i]);
    }
    if (n < 2) continue;
    float a0 = fminf(xs[0], xs[1]), b0 = fmaxf(xs[0], xs[1]);
    int a = clampi((int)ceilf(a0 - 0.5f), clip_x0, clip_x1), b = clampi((int)ceilf(b0 - 0.5f), clip_x0, clip_x1);
    if (b == a && b0 - a0 > 0.2f && a < clip_x1) b = a + 1;   /* (thin limbs keep a pixel) */
    color_t *d = gfx_buf + (yy - gfx_y0) * SCREEN_W;
    for (int x = a; x < b; x++) d[x] = col;
  }
}

static void draw_limb(const Char *ch, int p, color_t col, color_t end, float end_from) {
  float x, y, s, c, w, h;
  if (!game_part(ch, p, &x, &y, &s, &c)) return;
  game_part_size(ch, p, &w, &h);
  /* the sheet's limbs reach past their joints */
  h *= 1.2f;
  quad(x, y, s, c, w, h, 0, end_from, col);
  if (end_from < 1) quad(x, y, s, c, w * (p == P_SHIN_F || p == P_SHIN_B ? 1.0f : 0.95f), h, end_from, 1, end);
}

static void draw_shoe(const Char *ch, int p) {
  float x, y, s, c, w, h;
  if (!game_part(ch, p, &x, &y, &s, &c)) return;
  game_part_size(ch, p, &w, &h);
  float side = ch->left ? -1.0f : 1.0f;
  /* the foot: forward of the shin's bottom */
  float bx = x - s * h * 0.55f, by = y + c * h * 0.55f;
  float fx = bx + c * w * 0.45f * side, fy = by + s * w * 0.45f * side;
  quad(fx, fy, s, c, w * 1.9f, h * 0.22f, 0, 1, 0);
}

static void draw_char(const Char *ch) {
  int role = ch->role;
  const color_t *col = COLS[role];
  float k = ch->h / 80;
  int torso = role == R_HERO ? PIC_HERO_TORSO : role == R_ENEMY ? PIC_ENEMY_TORSO : PIC_HOSTAGE_TORSO;
  int head = role == R_HERO ? PIC_HERO_HEAD : role == R_ENEMY ? PIC_ENEMY_HEAD : PIC_HOSTAGE_HEAD;
  if (!ch->alive) head = role == R_HERO ? PIC_HERO_DEAD : role == R_ENEMY ? PIC_ENEMY_DEAD : PIC_HOSTAGE_DEAD;
  bool flip = ch->left != 0;
  draw_limb(ch, P_ARM_B, col[1], col[1], 1);
  draw_limb(ch, P_FORE_B, col[1], col[2], 0.72f);
  draw_limb(ch, P_THIGH_B, col[4], col[4], 1);
  draw_limb(ch, P_SHIN_B, col[4], col[4], 1);
  draw_shoe(ch, P_SHIN_B);
  draw_limb(ch, P_THIGH_F, col[3], col[3], 1);
  draw_limb(ch, P_SHIN_F, col[3], col[3], 1);
  draw_shoe(ch, P_SHIN_F);
  float x, y, s, c, w, h;
  if (game_part(ch, P_TORSO, &x, &y, &s, &c)) draw_pic_at(torso, SX(x), SY(y), k, s, c, flip);
  if (game_part(ch, P_HEAD, &x, &y, &s, &c)) {
    game_part_size(ch, P_HEAD, &w, &h);
    float nx = x - s * h / 2 * -1.0f, ny = y + c * h / 2;   /* the neck: the head's bottom */
    nx = x + (-s) * (h / 2), ny = y + c * (h / 2);
    draw_pic_at(head, SX(nx), SY(ny), k, s, c, flip);
  }
  /* the shooter's gun, in his hand */
  if (ch == &g.ch[g.hero] && ch->alive) {
    float sx, sy, mx, my, dx = cosf(g.aim), dy = sinf(g.aim);
    game_gun(&sx, &sy, &mx, &my);
    float hx = sx + dx * 30 * k, hy = sy + dy * 30 * k;
    if (g.grenades) {
      draw_disc(SX(hx), SY(hy), 2.6f * k, HEX(0x3D4A23));
    } else {
      float ex = hx + dx * 15 * k, ey = hy + dy * 15 * k;
      draw_line(SX(hx), SY(hy), SX(ex), SY(ey), 0);
      draw_line(SX(hx) - dy, SY(hy) + dx, SX(ex) - dy, SY(ey) + dx, HEX(0x222222));
    }
  }
  draw_limb(ch, P_ARM_F, col[0], col[0], 1);
  draw_limb(ch, P_FORE_F, col[0], col[2], 0.72f);
}

/* ------------------------------------------------------------------ the world */
static void draw_pins(void) {
  for (int i = 0; i < g.npin; i++) {
    const Pin *p = &g.pin[i];
    if (p->t == 3) {
      if (p->joint < 0 || !world_joint_alive(p->joint)) continue;
      float ax, ay, bx, by;
      world_joint_anchors(p->joint, &ax, &ay, &bx, &by);
      draw_line(SX(ax), SY(ay), SX(bx), SY(by), HEX(0x6B4423));
      draw_line(SX(ax) + 1, SY(ay), SX(bx) + 1, SY(by), HEX(0x8E6236));
    } else if (p->t == 5) {
      draw_line(SX(p->x0), SY(p->y0), SX(p->x1), SY(p->y1), HEX(0x2B3238));
    }
  }
}

static void draw_pin_dots(void) {
  for (int i = 0; i < g.npin; i++) {
    const Pin *p = &g.pin[i];
    if (p->t != 2 || p->joint < 0 || !world_joint_alive(p->joint)) continue;
    float ax, ay, bx, by;
    world_joint_anchors(p->joint, &ax, &ay, &bx, &by);
    draw_disc(SX(ax), SY(ay), 2.2f, HEX(0x2B3238));
    draw_disc(SX(ax), SY(ay), 1.0f, HEX(0xB0B8C0));
  }
}

static void draw_switches(void) {
  for (int i = 0; i < g.nsw; i++) {
    const Switch *s = &g.sw[i];
    color_t c = s->on ? HEX(0x9AA3AA) : SWITCH_COL[s->colour % 6];
    float x0 = SX(s->x0), y0 = SY(s->y0), x1 = SX(s->x1), y1 = SY(s->y1);
    for (int k = -1; k <= 1; k++) {
      draw_line(x0 + k, y0, x1 + k, y1, c);
      draw_line(x0, y0 + k, x1, y1 + k, c);
    }
  }
}

static void draw_shots(void) {
  for (int i = 0; i < MAX_SHOT; i++) {
    const Shot *s = &g.shot[i];
    if (s->body < 0) continue;
    float x, y, sn, cs;
    world_xf(s->body, &x, &y, &sn, &cs);
    if (s->kind == 0) {
      for (int k = 0; k + 1 < s->tn; k++)
        draw_line(SX(s->tx[k]), SY(s->ty[k]), SX(s->tx[k + 1]), SY(s->ty[k + 1]),
                  k < 4 ? 0xFFFF : HEX(0xD8DEE4));
      draw_disc(SX(x), SY(y), 1.6f, HEX(0xC99400));
    } else {
      draw_disc(SX(x), SY(y), 5 * FIELD_S + 0.8f, HEX(0x1F2612));
      draw_disc(SX(x), SY(y), 5 * FIELD_S, (s->life / 8) & 1 ? HEX(0x55692F) : HEX(0x76913F));
    }
  }
}

static void draw_booms(void) {
  for (int i = 0; i < MAX_BOOM; i++) {
    const Boom *b = &g.boom[i];
    if (!b->t) continue;
    float t = b->t / 24.0f, r = 160 * FIELD_S * (0.35f + 0.65f * fminf(1, t * 3));
    if (t < 0.7f) draw_disc(SX(b->x), SY(b->y), r, gfx_mix(HEX(0xFF8A1E), HEX(0xFFE08A), (int)(32 * t)));
    if (t < 0.45f) draw_disc(SX(b->x), SY(b->y), r * 0.55f, HEX(0xFFF4C2));
  }
}

static void draw_aim(void) {
  if (!game_can_fire()) return;
  float sx, sy, mx, my;
  game_gun(&sx, &sy, &mx, &my);
  if (g.grenades) {
    float xs[40], ys[40];
    int n = game_arc(xs, ys, 40);
    for (int i = 0; i < n; i++) {
      draw_disc(SX(xs[i]), SY(ys[i]), 1.6f, 0);
      draw_disc(SX(xs[i]), SY(ys[i]), 1.0f, 0xFFFF);
    }
    return;
  }
  float dx = cosf(g.aim), dy = sinf(g.aim);
  float ex = mx + dx * 1400, ey = my + dy * 1400;
  draw_line(SX(mx), SY(my), SX(ex), SY(ey), HEX(0xFF5A4E));
  float rx = SX(mx + dx * 150), ry = SY(my + dy * 150);
  for (int a = 0; a < 24; a++) {
    float t = a * 6.2831853f / 24;
    plot((int)floorf(rx + 4.5f * cosf(t)), (int)floorf(ry + 4.5f * sinf(t)), 0);
  }
  draw_line(rx - 6, ry, rx - 2, ry, 0), draw_line(rx + 2, ry, rx + 6, ry, 0);
  draw_line(rx, ry - 6, rx, ry - 2, 0), draw_line(rx, ry + 2, rx, ry + 6, 0);
}

void draw_world(bool aim) {
  clip_x0 = FIELD_X0, clip_x1 = FIELD_X0 + FIELD_W;
  draw_pic(PIC_BG, FIELD_X0, 0);
  draw_pins();
  for (int i = 0; i < g.nobj; i++) draw_obj(i);
  draw_switches();
  draw_pin_dots();
  for (int i = 0; i < g.nchar; i++)
    if (i != g.hero) draw_char(&g.ch[i]);
  if (g.hero >= 0) draw_char(&g.ch[g.hero]);
  draw_shots();
  draw_booms();
  if (aim) draw_aim();
  clip_x0 = 0, clip_x1 = SCREEN_W;
}

/* ------------------------------------------------------------------ the panels */
static void bullet_icon(int x, int y, int kind) {
  /* kind 0 black, 1 gold, 2 spent */
  color_t c = kind == 1 ? HEX(0xF2B807) : kind == 0 ? HEX(0x101418) : HEX(0x3A4756);
  color_t tip = kind == 1 ? HEX(0xFFE07A) : kind == 0 ? HEX(0x505A64) : HEX(0x3A4756);
  gfx_rrect(x, y + 4, 7, 12, 2, c, 256);
  gfx_rrect(x, y, 7, 8, 3, tip, 256);
  if (kind == 2) gfx_rrect(x + 1, y + 1, 5, 14, 2, COL_PANEL, 256);
}

void draw_hud(const char *title, const char *keys1, const char *keys2) {
  /* keys1, keys2: the left and right keys' name and what they do */
  gfx_fill(0, gfx_y0, FIELD_X0, gfx_y1 - gfx_y0, COL_PANEL);
  gfx_fill(FIELD_X0 + FIELD_W, gfx_y0, SCREEN_W - FIELD_X0 - FIELD_W, gfx_y1 - gfx_y0, COL_PANEL);
  gfx_text_center(&np_font_small, FIELD_X0 / 2, 16, title, COL_TEXT, 256);
  /* the bullets: the last one gold */
  int n = g.ammo, per = 4;
  for (int i = 0; i < n; i++) {
    int row = i / per, col = i % per;
    int x = 6 + col * 14, y = 28 + row * 22;
    int kind = i < g.shots ? 2 : (i == n - 1 ? 1 : 0);
    if (g.grenades) {
      if (y + 14 > gfx_y0 && y < gfx_y1) {
        /* a grenade: its body and lever (the last one gold, as the bullets) */
        color_t c = kind == 2 ? HEX(0x3A4756) : kind == 1 ? HEX(0xF2B807) : HEX(0x55692F);
        gfx_circle(x + 4, y + 10, 5, c, 256);
        gfx_fill(x + 2, y + 2, 4, 3, kind == 2 ? HEX(0x3A4756) : HEX(0x2B3238));
      }
    } else if (y + 16 > gfx_y0 && y < gfx_y1) {
      bullet_icon(x, y, kind);
    }
  }
  /* the stars this would give */
  int st = game_stars(), sy = 30 + ((n + per - 1) / per) * 22;
  for (int i = 0; i < 3; i++) {
    int x = 6 + i * 19;
    if (sy + 20 > gfx_y0 && sy < gfx_y1) {
      if (i < st) draw_pic(PIC_STAR, x, sy);
      else gfx_circle(x + 9, sy + 9, 6, COL_PANEL_LIGHT, 256);
    }
  }
  if (g.grenades) {
    int x = FIELD_X0 + FIELD_W + 8, y = 30, h = 76;
    gfx_text(&np_font_small, x, y - 6, "Power", COL_MUTED, 256);
    gfx_rrect(x, y, 12, h, 3, COL_PANEL_LIGHT, 256);
    int f = (int)(g.power * (h - 4));
    gfx_rrect(x + 2, y + h - 2 - f, 8, f, 2, COL_FOCUS, 256);
  }
  /* the keys: each one's name, then what it does */
  int rx = FIELD_X0 + FIELD_W + 6;
  static const char *const lines[8] = {"Up Down", "aim", "", "", "OK", "fire", "Back", "pause"};
  for (int i = 0; i < 8; i++) {
    const char *t = i == 2 ? keys1 : i == 3 ? keys2 : lines[i];
    int y = 128 + i * 13 + (i / 2) * 3;
    if (y - 10 < gfx_y1 && y + 3 > gfx_y0) gfx_text(&np_font_small, rx, y, t, i & 1 ? COL_MUTED : COL_TEXT, 256);
  }
}
