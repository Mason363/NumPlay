/* Drawing, in the launcher's strips (launcher/src/gfx.c): the lab, the run, the HUD and the screens.
 * The world is drawn at 2/3 of the game's points; the pictures are Halfbrick's (art.h). */
#include <string.h>
#include "../../../launcher/src/gfx.h"
#include "draw.h"
#include "art.h"
#include "data.h"

#define SX(v) ((int)((v) * 2 / 3))
#define WHITE HEX(0xFFFFFF)
#define GOLD HEX(0xFFD447)
#define INK HEX(0x0C1220)
#define MUTED HEX(0xB8C2D2)
#define FOCUS HEX(0xFFC21A)

/* ---------------------------------------------------------------- pictures */
enum { FLIP_X = 1, FLIP_Y = 2, WHITEN = 4 };

static int from_row; /* (blit draws only from this row down: the band under the key line, drawn again) */
static void blit(const jj_sheet *s, int f, int ax, int ay, int flags) {
  const jj_frame *fr = &s->f[f];
  int w = fr->w, h = fr->h;
  int x = flags & FLIP_X ? ax - fr->x - w + 1 : ax + fr->x;
  int y = flags & FLIP_Y ? ay - fr->y - h + 1 : ay + fr->y;
  if (y >= gfx_y1 || y + h <= gfx_y0 || x >= SCREEN_W || x + w <= 0) return;
  const uint8_t *base = s->data + fr->data;
  int r0 = y < gfx_y0 ? gfx_y0 - y : 0, r1 = y + h > gfx_y1 ? gfx_y1 - y : h;
  if (r0 < from_row - y) r0 = from_row - y;
  for (int r = r0; r < r1; r++) {
    int src = flags & FLIP_Y ? h - 1 - r : r;
    const uint8_t *p = base + (base[2 * src] | base[2 * src + 1] << 8);
    const uint8_t *end = base + (base[2 * src + 2] | base[2 * src + 3] << 8);
    color_t *row = gfx_buf + (y + r - gfx_y0) * SCREEN_W;
    int i = 0;
    while (p < end && i < w) {
      unsigned tag = *p++, n = (tag & 127) + 1;
      if (tag & 128) {
        unsigned c = *p++;
        if (c) {
          color_t col = flags & WHITEN ? WHITE : s->pal[c];
          for (unsigned k = 0; k < n; k++) {
            int xx = flags & FLIP_X ? x + w - 1 - (i + (int)k) : x + i + (int)k;
            if ((unsigned)xx < SCREEN_W) row[xx] = col;
          }
        }
        i += (int)n;
      } else {
        for (unsigned k = 0; k < n; k++) {
          unsigned c = s->bits == 4 ? (p[k / 2] >> (k & 1 ? 0 : 4)) & 15 : p[k];
          if (c) {
            int xx = flags & FLIP_X ? x + w - 1 - (i + (int)k) : x + i + (int)k;
            if ((unsigned)xx < SCREEN_W) row[xx] = flags & WHITEN ? WHITE : s->pal[c];
          }
        }
        p += s->bits == 4 ? (n + 1) / 2 : n;
        i += (int)n;
      }
    }
  }
}

/* a band of a background picture, opaque, from the top of the screen at x (the strip's rows from from_row on) */
static void band(const jj_sheet *s, int f, int x) {
  const jj_frame *fr = &s->f[f];
  int w = fr->w;
  if (x >= SCREEN_W || x + w <= 0) return;
  const uint8_t *base = s->data + fr->data;
  const uint16_t *pal = s->pal;
  int r0 = gfx_y0 > from_row ? gfx_y0 : from_row, r1 = gfx_y1 < fr->h ? gfx_y1 : fr->h;
  for (int r = r0; r < r1; r++) {
    const uint8_t *p = base + (base[2 * r] | base[2 * r + 1] << 8);
    color_t *row = gfx_buf + (r - gfx_y0) * SCREEN_W;
    int i = x;   /* the screen column of the next pixel */
    while (i < SCREEN_W && i < x + w) {
      unsigned tag = *p++;
      int n = (int)(tag & 127) + 1, a = i < 0 ? 0 : i, b = i + n > SCREEN_W ? SCREEN_W : i + n;
      if (tag & 128) {
        color_t c = pal[*p++];
        for (int k = a; k < b; k++) row[k] = c;
      } else {
        const uint8_t *q = p + (a - i);
        for (int k = a; k < b; k++) row[k] = pal[*q++];
        p += n;
      }
      i += n;
    }
  }
}

/* the title's background: the lab's picture, side by side */
static void lab(int scroll) {
  int w = pic_lab.f[0].w;
  int x = -(scroll % w);
  if (x > 0) x -= w;
  for (; x < SCREEN_W; x += w) band(&pic_lab, 0, x);
}

/* ---------------------------------------------------------------- the rooms
 * levelgeneration.xml: one room after another, each in from the lab, a few of its middle chunks, and out to the lab
 * again. Here the lab comes between the others: a run starts with 2 to 4 of the lab's pictures, then the glass tube
 * under the sea, the lava cave or the greenhouse (never the same twice running), then 1 to 3 of the lab's, and so
 * on. Made from the run's own number (jj_run.level), so the same run always has the same rooms. */
enum { PC_LAB, PC_TUBE_IN, PC_TUBE_OUT, PC_CAVE_IN, PC_CAVE, PC_CAVE_OUT, PC_GREEN_IN, PC_GREEN, PC_GREEN_OUT };
static const jj_sheet *const piece_pic[] = {&pic_lab, &pic_rm_tube_in, &pic_rm_tube_out, &pic_rm_cave_in, &pic_rm_cave,
                                            &pic_rm_cave_out, &pic_rm_green_in, &pic_rm_green, &pic_rm_green_out};
static int piece_w(int pc) {
  const jj_sheet *s = piece_pic[pc];
  int w = 0;
  for (int f = 0; f < s->n; f++) w += s->f[f].w;
  return w;
}

static const uint8_t room_in[3] = {PC_TUBE_IN, PC_CAVE_IN, PC_GREEN_IN};
static struct {
  uint32_t level, rng;     /* the run's, and the dice where the walk has got to */
  int32_t x;               /* where the current piece starts (pixels of scroll) */
  uint8_t piece, room;     /* room 255: the lab */
  uint8_t left;            /* in a room: its pieces after this one; in the lab: lab pictures after this one */
  uint8_t last;            /* the last room */
} walk;

static void next_piece(void) {
  walk.x += piece_w(walk.piece);
  if (walk.room != 255) {
    if (walk.left > 1) walk.piece = (uint8_t)(room_in[walk.room] + 1), walk.left--;          /* a middle chunk */
    else if (walk.left == 1) walk.piece = (uint8_t)(room_in[walk.room] + (walk.room ? 2 : 1)), walk.left = 0;   /* out */
    else walk.piece = PC_LAB, walk.room = 255, walk.left = (uint8_t)(jj_rand(&walk.rng) % 3);
    return;
  }
  if (walk.left) {
    walk.left--;
    return;
  }
  int r = (int)(jj_rand(&walk.rng) % 2);   /* the next room: not the last one */
  if (r >= walk.last) r++;
  walk.room = walk.last = (uint8_t)r;
  walk.piece = room_in[r];
  /* the tube is its two halves; the cave has 1 or 2 chunks of rock (chunkMaxSize 2048), the greenhouse 2 to 5 */
  walk.left = (uint8_t)(r == 0 ? 1 : r == 1 ? 2 + jj_rand(&walk.rng) % 2 : 3 + jj_rand(&walk.rng) % 4);
}

int jj_room_at(uint32_t level, int scroll);
static void rooms(uint32_t level, int scroll) {
  jj_room_at(level, scroll);   /* (the walk to where the screen starts) */
  /* draw from the piece the screen starts in; the walk stays there */
  typeof(walk) keep = walk;
  int x = walk.x - scroll;
  while (x < SCREEN_W) {
    const jj_sheet *s = piece_pic[walk.piece];
    for (int f = 0; f < s->n; f++) band(s, f, x), x += s->f[f].w;
    next_piece();
  }
  walk = keep;
}

static void plot(int x, int y, color_t c) {
  if ((unsigned)x < SCREEN_W && y >= gfx_y0 && y < gfx_y1) gfx_buf[(y - gfx_y0) * SCREEN_W + x] = c;
}

/* a thick line: spans of `w` pixels across its main direction */
static void line(int x0, int y0, int x1, int y1, int w, color_t c) {
  int dx = x1 - x0, dy = y1 - y0, adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
  int n = adx > ady ? adx : ady;
  if (n == 0) n = 1;
  int lo = (y0 < y1 ? y0 : y1) - w, hi = (y0 > y1 ? y0 : y1) + w;
  if (hi < gfx_y0 || lo >= gfx_y1) return;
  for (int i = 0; i <= n; i++) {
    int x = x0 + dx * i / n, y = y0 + dy * i / n;
    if (adx > ady) {
      if (y + w / 2 < gfx_y0 || y - w / 2 >= gfx_y1) continue;
      for (int k = -w / 2; k < w - w / 2; k++) plot(x, y + k, c);
    } else {
      if (y < gfx_y0 || y >= gfx_y1) continue;
      for (int k = -w / 2; k < w - w / 2; k++) plot(x + k, y, c);
    }
  }
}

static void text(const np_font_t *f, int x, int y, const char *s, color_t c) { gfx_text(f, x, y, s, c, 256); }
static void center(const np_font_t *f, int y, const char *s, color_t c) { gfx_text_center(f, SCREEN_W / 2, y, s, c, 256); }
static void outlined(const np_font_t *f, int x, int y, const char *s, color_t c, bool centered) {
  if (centered) x -= gfx_text_width(f, s) / 2;
  for (int k = 0; k < 4; k++) text(f, x + (k == 0) - (k == 1), y + (k == 2) - (k == 3), s, INK);
  text(f, x, y, s, c);
}

static char *num(char *p, uint32_t v) {
  char t[12];
  int n = 0;
  do t[n++] = (char)('0' + v % 10), v /= 10;
  while (v);
  while (n) *p++ = t[--n];
  *p = 0;
  return p;
}

/* the HUD's numbers, in the game's own font */
static int digits(int x, int y, uint32_t v, bool metre) {
  char s[12];
  num(s, v);
  for (char *p = s; *p; p++) blit(&pic_digits, *p - '0', x, y, 0), x += pic_digit_adv[*p - '0'];
  if (metre) blit(&pic_digits, 10, x + 1, y, 0), x += pic_digit_adv[10] + 1;
  return x;
}

/* the key line, in the dark band under the floor */
static void keyline(const char *s) { gfx_text_center(&np_font_small, SCREEN_W / 2, 233, s, MUTED, 256); }

/* ---------------------------------------------------------------- the run */
static const jj_run *R;

static void zapper(const jj_zapper *z, uint32_t t) {
  int cx = SX(z->x), cy = SX(z->y);
  int hl = z->len / 2;
  int dx = jj_cos(z->ang) * hl / 1024 * 2 / 3, dy = jj_sin(z->ang) * hl / 1024 * 2 / 3;
  int ext = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
  if (cx + ext + 16 < 0 || cx - ext - 16 >= SCREEN_W || cy + ext + 16 < gfx_y0 || cy - ext - 16 >= gfx_y1) return;
  bool rot = z->spin != 0;
  if (!z->dud) {
    /* the electricity: an orange glow, a yellow band, a white core that flickers along */
    color_t glow = rot ? HEX(0xE0571A) : HEX(0xE89A14), band = rot ? HEX(0xFF9A3C) : HEX(0xFFE04A);
    int j = (int)(t / 2 % 3) - 1;
    line(cx - dx, cy - dy, cx + dx, cy + dy, 7, glow);
    line(cx - dx, cy - dy, cx + dx, cy + dy, 4, band);
    line(cx - dx + (dy ? j : 0), cy - dy + (dx ? j : 0), cx + dx - (dy ? j : 0), cy + dy - (dx ? j : 0), 2, WHITE);
  }
  int f = z->dud ? 0 : (int)(t / 3 % 4);
  const jj_sheet *orb = rot ? &pic_orbr : &pic_orb;
  blit(orb, f, cx - dx, cy - dy, 0);
  blit(orb, f, cx + dx, cy + dy, 0);
}

static void lasers(void) {
  if (!R->laser) return;
  const jj_lpat_t *p = &jj_lpats[R->laser - 1];
  int t = R->laser_t, in = t < 20 ? 20 - t : t > p->time ? t - p->time : 0;   /* the emitters slide in and out */
  for (int b = 0; b < p->count; b++) {
    int y, phase, k;
    jj_beam_state(R->laser - 1, b, t, &y, &phase, &k);
    int sy = SX(y);
    if (sy + 12 < gfx_y0 || sy - 12 >= gfx_y1) continue;
    int xl = 11 - in, xr = SCREEN_W - 12 + in;
    if (phase == 1) {
      /* charging: a thin line that comes and goes, faster near the end */
      if ((k * k / 400 + k) / 6 % 2 == 0) line(xl, sy, xr, sy, 1, HEX(0xFF5A3C));
    } else if (phase == 2) {
      int fl = (k / 2) & 1;
      line(xl, sy, xr, sy, 11, HEX(0xC8281E));
      line(xl, sy, xr, sy, 7 + fl, HEX(0xFF8A3C));
      line(xl, sy, xr, sy, 3, HEX(0xFFF6D8));
    }
    int f = phase == 2 ? 2 + ((k / 3) & 1) : phase == 1 ? (k > 60 ? 1 : 0) : 0;
    blit(&pic_emitter, f, xl, sy, 0);
    blit(&pic_emitter, f, xr, sy, FLIP_X);
  }
}

static float run_dx(void) { return R->state == JJ_ALIVE ? jj_speed(R) / 60.0f : R->dvx; }

static void barry_draw(void) {
  int bx = SX(JJ_BX), by = SX(R->y);
  uint32_t t = R->t;
  if (R->state == JJ_ALIVE && R->safe && (t / 4) & 1) return;   /* blinks while he can't be hit */
  if (R->state != JJ_ALIVE) {
    if (R->state == JJ_ZAPPED) blit(&pic_barry, (t / 3) & 1 ? 10 : 11, bx, by, 0);
    else if (R->y < JJ_FLOOR || R->vy != 0)   /* thrown about, spinning */
      blit(&pic_tumble, R->pose * 8 + (((R->rot + 4096) >> 13) & 7), bx, SX(R->y - 16), 0);
    else blit(&pic_barry, 12, bx, by, 0);   /* sliding to a stop on his front */
    return;
  }
  switch (R->ride) {
  case JJ_FOOT: {
    int f;
    if (R->ground) f = (int)(t / 5 % 4);
    else if (R->held) f = 4 + (int)(t / 3 % 4);
    else f = 8 + (int)(t / 8 % 2);
    blit(&pic_barry, f, bx, by, 0);
    if (R->held) blit(&pic_jflash, (int)(t % 4), bx - 5, by - 1, 0);   /* the gun's flash, under its barrels */
    break;
  }
  case JJ_MECH: {
    /* drive 0-3, jump 4, no boost 5, boost 6-7 (its thrusters), falling 8 */
    int f;
    if (R->ground) f = (int)(t / 5 % 4);
    else if (R->boost) f = 4;
    else if (R->held) f = 6 + (int)(t / 4 % 2);
    else f = R->vy > 1 ? 8 : 5;
    blit(&pic_mech, f, bx, by, 0);
    break;
  }
  case JJ_HOG:
    blit(&pic_hog, R->ground ? (int)(t / 4 % 4) : R->vy < 0 ? 4 : 5, bx, by, 0);
    break;
  case JJ_BIRD: {
    static const uint8_t flap[] = {0, 1, 2, 3, 4, 3};
    int f = R->ground ? 0 : R->vy < -1 ? flap[t / 3 % 6] : 3;
    blit(&pic_bird, f, bx, by, 0);
    break;
  }
  case JJ_GRAV: {
    int f = R->ground ? (int)(t / 4 % 4) : 4;
    if (!R->ground && ((R->flip && R->vy > 0.5f) || (!R->flip && R->vy < -0.5f))) f = 5 + (int)(t / 4 % 2);
    if (R->flip) blit(&pic_grav, f, bx, SX(R->y - jj_box_h(JJ_GRAV)) - 1, FLIP_Y);
    else blit(&pic_grav, f, bx, by, 0);
    break;
  }
  case JJ_CFT: {
    int h = jj_box_h(JJ_CFT);
    int cy = SX(R->y - h / 2.0f);
    /* the arrow: from the Teleporter to where a press sends Barry */
    int ang = jj_sin(R->cft) * 16384 / 1024;
    int tx = bx + jj_cos((uint16_t)ang) * 64 / 1024, ty = cy - jj_sin((uint16_t)ang) * 64 / 1024;
    for (int i = 2; i < 8; i++) plot(bx + (tx - bx) * i / 8, cy + (ty - cy) * i / 8, HEX(0x9FE8FF));
    blit(&pic_cft, R->boost ? 4 + (12 - R->boost) / 3 : (int)(t / 6 % 4), bx, cy, 0);
    blit(&pic_cft, 8, tx, ty, 0);
    break;
  }
  }
}

static void world(void) {
  uint32_t t = R->t;
  rooms(R->level, (int)(R->dist / 24));   /* 16ths of a point at 2/3: / 24 */
  for (int i = 0; i < JJ_SCIENTISTS; i++) {
    const jj_scientist *s = &R->sci[i];
    if (!s->on) continue;
    static const uint8_t shock[3] = {15, 15, 16};
    int f;
    switch (s->state) {
    case JJ_SCI_WALK: f = (int)(s->t / 5 % 4); break;
    case JJ_SCI_RUN: f = 4 + (int)(s->t / 5 % 4); break;
    case JJ_SCI_FETAL: f = 14; break;
    case JJ_SCI_ZAPPED: f = shock[s->t / 2 % 3]; break;
    case JJ_SCI_AIR: f = 8 + s->pose; break;
    default: f = 12 + (s->pose & 1); break;
    }
    blit(s->kind ? &pic_sci2 : &pic_sci1, f, SX(s->x), SX(s->y) + 1, s->dir < 0 ? FLIP_X : 0);
  }
  for (int i = 0; i < JJ_SPARKS; i++)
    if (R->spark[i].t) blit(&pic_spark, R->spark[i].t - 1, SX(R->spark[i].x), SX(JJ_FLOOR) + 1, 0);
  for (int i = 0; i < JJ_COINSETS; i++) {
    const jj_coinset *c = &R->coin[i];
    if (!c->on) continue;
    int w = jj_pat_w[c->pat], h = jj_pat_h[c->pat];
    for (int row = 0; row < h; row++) {
      int y = SX(c->y + row * JJ_COIN_GAP);
      if (y + 6 < gfx_y0 || y - 6 >= gfx_y1) continue;
      for (int col = 0; col < w; col++)
        if (jj_coin_at(c, col, row)) {
          int x = SX(c->x + col * JJ_COIN_GAP);
          if (x > -8 && x < SCREEN_W + 8) blit(&pic_coin, (int)((t / 4 + (uint32_t)col) % 8), x, y, 0);
        }
    }
  }
  for (int i = 0; i < JJ_TOKENS; i++) {
    const jj_token *k = &R->token[i];
    if (k->on) blit(&pic_token, (int)(t / 4 % 8), SX(k->x), SX(k->y + 60.0f * jj_sin(k->phase) / 1024.0f), 0);
  }
  if (R->box.on) blit(&pic_box, (int)(t / 5 % 4), SX(R->box.x), SX(R->box.y + 6.0f * jj_sin(R->box.phase) / 1024.0f), 0);
  for (int i = 0; i < JJ_ZAPPERS; i++)
    if (R->zap[i].on) zapper(&R->zap[i], t);
  lasers();
  /* the bullets, turned the way they go on the screen (0 to 40 degrees back from straight down) */
  float dx = run_dx();
  for (int i = 0; i < JJ_BULLETS; i++) {
    const jj_bullet *b = &R->bul[i];
    if (b->vy == 0) continue;
    float back = (dx - b->vx) / b->vy;   /* tan of the angle */
    int f = back < 0.0875f ? 0 : back < 0.268f ? 1 : back < 0.466f ? 2 : back < 0.7f ? 3 : 4;
    blit(&pic_bullet, f, SX(b->x), SX(b->y), 0);
  }
  for (int i = 0; i < JJ_SHELLS; i++)
    if (R->shell[i].on) blit(&pic_shell, R->shell[i].rot, SX(R->shell[i].x), SX(R->shell[i].y), 0);
  barry_draw();
  for (int i = 0; i < JJ_MISSILES; i++) {
    const jj_missile *m = &R->mis[i];
    if (!m->on) continue;
    int y = SX(m->y);
    if (m->state == JJ_FLY) {
      blit(&pic_missile, 4 + ((m->t / 3) & 1), SX(m->x) + 9, y, 0);
      blit(&pic_missile, (int)(m->t / 3 % 4), SX(m->x), y, 0);
    } else {
      int f = m->state == JJ_WARN ? (m->t / 8) & 1 : 2 + ((m->t / 3) & 1);
      blit(&pic_warn, f, SCREEN_W - 14, y, 0);
    }
  }
  for (int i = 0; i < JJ_FX; i++) {
    const jj_fx *e = &R->fx[i];
    if (!e->t) continue;
    if (e->t < 30) {
      blit(&pic_boom, e->t / 5, SX(e->x), SX(e->y), 0);
    }
  }
}

static void hud(bool keys) {
  int x = digits(6, 4, jj_metres(R), true);
  (void)x;
  blit(&pic_coin, 0, 12, 34, 0);
  char s[16];
  num(s, R->coins);
  outlined(&np_font_body, 21, 39, s, GOLD, false);
  if (R->tokens) {
    blit(&pic_stoken, 0, 4, 46, 0);
    num(s, R->tokens);
    outlined(&np_font_body, 25, 58, s, WHITE, false);
  }
  if (keys) {
    from_row = 222, rooms(R->level, (int)(R->dist / 24)), from_row = 0; /* (nothing of the run over the key line) */
    gfx_fill_alpha(0, 222, SCREEN_W, SCREEN_H - 222, INK, 150);           /* (and the rooms' sand and ferns dimmed under it) */
    static const char *const k[JJ_RIDES] = {"Hold OK to fly   Back pause", "Hold OK to jump and glide   Back pause",
                                            "OK jump   Back pause", "OK flap   Back pause", "OK flip gravity   Back pause",
                                            "OK teleport to the arrow   Back pause"};
    keyline(R->state == JJ_ALIVE ? k[R->ride] : "Back pause");
  }
}

/* ---------------------------------------------------------------- screens */
static const jj_view *V;

static void dim(int a) { gfx_fill_alpha(0, gfx_y0, SCREEN_W, gfx_y1 - gfx_y0, INK, a); }

static void panel(int x, int y, int w, int h) {
  gfx_rrect(x, y, w, h, 10, HEX(0x16203A), 240);
  gfx_rrect_outline(x, y, w, h, 10, 2, HEX(0x6E7C99), 256);
}

static void choice(int y, const char *s, bool on) {
  if (on) {
    gfx_rrect(84, y - 15, 152, 22, 6, HEX(0x2C3A5C), 256);
    gfx_rrect_outline(84, y - 15, 152, 22, 6, 2, FOCUS, 256);
  }
  center(&np_font_body, y, s, on ? WHITE : MUTED);
}

/* the logo as the game lays it out (levels/title/objects/title.png): JOYRIDE under JETPACK and a little to the
   right, Barry flying off the end, his flame behind the letters; the whole of it in the middle of the screen */
#define LOGO_W (187 + 103)
static void title(void) {
  lab((int)(V->clock / 2));
  int x0 = (SCREEN_W - LOGO_W) / 2, y0 = 53;
  blit(&pic_hero, 0, x0 + 187, y0 - 33, 0);
  blit(&pic_wjet, 0, x0, y0, 0);
  blit(&pic_wjoy, 0, x0 + 24, y0 + 61, 0);
  outlined(&np_font_title, SCREEN_W / 2, 194, "Press OK to play!", WHITE, true);
  char s[40], *p = s;
  memcpy(p, "Best ", 5), p = num(p + 5, V->best);
  memcpy(p, " m", 3);
  outlined(&np_font_small, 8, 14, s, MUTED, false);
  num(s, V->coins);
  blit(&pic_coin, 0, SCREEN_W - 12 - gfx_text_width(&np_font_small, s), 10, 0);
  outlined(&np_font_small, SCREEN_W - 6 - gfx_text_width(&np_font_small, s), 14, s, GOLD, false);
  if (V->head_start || V->double_coins) {
    const char *b = V->head_start && V->double_coins ? "Next run: HEAD START, DOUBLE COINS"
                    : V->head_start                  ? "Next run: HEAD START"
                                                     : "Next run: DOUBLE COINS";
    outlined(&np_font_small, SCREEN_W / 2, 214, b, GOLD, true);
  }
  keyline("OK play   Back quit");
}

static void guide(void) {
  dim(200);
  panel(14, 6, 292, 206);
  center(&np_font_title, 30, "How to play", WHITE);
  static const char *const lines[] = {"Hold OK, EXE or Up to fly. Let go to fall.", "Dodge zappers, missiles and lasers.",
                                      "A missile warns you, locks on, then fires.", "Grab coins and spin tokens.",
                                      "A vehicle box gives you a ride. It takes a hit.", "Spin tokens play the Final Spin.",
                                      "Back pauses. Home saves and quits."};
  for (int i = 0; i < 7; i++) center(&np_font_small, 54 + i * 15, lines[i], HEX(0xE6EBF2));
  static const char *const credits[] = {"Inspired by Jetpack Joyride by Halfbrick, not affiliated.",
                                        "Brought to NumPlay by Bobydeluxe.", "Made by Mason Chen as part of NumPlay."};
  for (int i = 0; i < 3; i++) center(&np_font_small, 168 + i * 14, credits[i], MUTED);
  keyline("OK continue");
}

static void paused(void) {
  dim(150);
  panel(64, 32, 192, 172);
  center(&np_font_title, 60, "PAUSED", WHITE);
  static const char *const items[] = {"Resume", "Retry", "How to play", "Quit"};
  for (int i = 0; i < 4; i++) choice(94 + i * 28, items[i], V->sel == i);
  keyline("Up/Down choose   OK select   Back resume");
}

static void confirm(void) {
  dim(170);
  panel(64, 84, 192, 60);
  center(&np_font_title, 119, V->sel ? "Quit this run?" : "Retry this run?", WHITE);
  keyline("OK yes   Back no");
}

static const char *const prize_names[JJ_PRIZES] = {"COINS +100", "CASH PRIZE +500", "JACKPOT +1000", "SMALL BLAST",
                                                   "BIG BLAST", "ATOM BLAST", "SECOND CHANCE", "EXTRA SPINS",
                                                   "DOUBLE COINS", "HEAD START"};

static void spin(void) {
  gfx_vgrad(0, gfx_y0, SCREEN_W, gfx_y1 - gfx_y0, 0x2A1846, 0x0C0816);
  outlined(&np_font_title, SCREEN_W / 2, 36, "FINAL SPIN", GOLD, true);
  for (int r = 0; r < 3; r++) {
    int x = 62 + r * 70;
    gfx_rrect(x, 54, 56, 96, 8, HEX(0x0A0A14), 256);
    gfx_rrect_outline(x, 54, 56, 96, 8, 3, HEX(0xC9CED8), 256);
    /* a reel: icons scroll while it turns, then it shows its stop */
    int pos = V->reel[r];
    int off = pos % 48, icon = pos / 48;
    for (int k = -1; k <= 1; k++) {
      int yy = 102 + k * 48 + off;
      if (yy < 64 || yy > 140) continue;
      blit(&pic_prize, (icon - k + 30) % JJ_PRIZES, x + 28, yy, 0);
    }
  }
  char s[48], *p = s;
  if (V->spin_msg >= 0) {
    outlined(&np_font_title, SCREEN_W / 2, 176, "YOU WON!", WHITE, true);
    center(&np_font_body, 196, prize_names[V->spin_msg], GOLD);
  }
  blit(&pic_stoken, 0, 98, 206, 0);
  memcpy(p, "SPIN TOKENS: ", 13);
  num(p + 13, V->tokens);
  text(&np_font_body, 122, 220, s, WHITE);
  keyline(V->spinning ? "" : V->tokens ? "OK spin   Back cash in (50 coins each)" : "OK continue");
}

static void results(void) {
  dim(160);
  panel(40, 16, 240, 192);
  center(&np_font_body, 44, "YOU FLEW", MUTED);
  char s[16];
  num(s, V->run_m);
  int w = 0;
  for (char *p = s; *p; p++) w += pic_digit_adv[*p - '0'];
  digits(SCREEN_W / 2 - (w + pic_digit_adv[10]) / 2, 52, V->run_m, true);
  center(&np_font_body, 98, "AND COLLECTED", MUTED);
  num(s, V->run_coins);
  int cw = gfx_text_width(&np_font_title, s);
  blit(&pic_coin, 0, SCREEN_W / 2 - cw / 2 - 10, 115, 0);
  outlined(&np_font_title, SCREEN_W / 2 + 6, 122, s, GOLD, true);
  if (V->new_best) outlined(&np_font_body, SCREEN_W / 2, 144, "NEW BEST!", GOLD, true);
  choice(170, "PLAY AGAIN", V->sel == 0);
  choice(196, "MAIN MENU", V->sel == 1);
  keyline("Up/Down choose   OK select");
}

static void scene(void *ctx) {
  (void)ctx;
  switch (V->screen) {
  case JJ_SCREEN_TITLE: title(); break;
  case JJ_SCREEN_SPIN: spin(); break;
  default:
    world();
    hud(V->screen == JJ_SCREEN_PLAY);
    if (V->screen == JJ_SCREEN_GUIDE) guide();
    else if (V->screen == JJ_SCREEN_PAUSE) paused();
    else if (V->screen == JJ_SCREEN_CONFIRM) confirm();
    else if (V->screen == JJ_SCREEN_RESULTS) results();
    break;
  }
}

int jj_room_at(uint32_t level, int scroll) {
  if (walk.level != level || scroll < walk.x || !walk.rng) {
    walk.level = level, walk.rng = level ? level : 1;
    walk.x = 0, walk.piece = PC_LAB, walk.room = 255, walk.last = 3;
    walk.left = (uint8_t)(1 + jj_rand(&walk.rng) % 3);
  }
  while (walk.x + piece_w(walk.piece) <= scroll) next_piece();
  return walk.room == 255 ? 0 : walk.room + 1;
}

void jj_draw(const jj_run *r, const jj_view *v) {
  R = r, V = v;
  gfx_render(scene, 0, 0, SCREEN_H);
}
