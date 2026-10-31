/* Bad Time Simulator: the screens, drawn strip by strip (the launcher's gfx.c: the calculator has no frame
 * buffer). The fight is in the original's 640 x 480 pixels; the screen shows it at half size. */
#include <string.h>
#include "art.h"
#include "draw.h"
#include "gfx.h"

void plat_push(int x, int y, int w, int h, const uint16_t *px);   /* (plat.h: its key names are the launcher's too) */
void np_push(int x, int y, int w, int h, const color_t *px) { plat_push(x, y, w, h, px); }

static color_t strip[SCREEN_W * STRIP_H];

#define WHITE 0xFFFF
#define BLACK 0x0000
#define RED HEX(0xFF0000)
#define SOUL_BLUE HEX(0x003CFF)
#define BONE_BLUE HEX(0x14A9FF)
#define BONE_ORANGE HEX(0xFFA040)
#define ORANGE HEX(0xFF7F27)
#define YELLOW HEX(0xFFFF00)
#define MAGENTA HEX(0xFF00FF)
#define HP_RED HEX(0xBF0000)
#define GREY HEX(0x8A8A8A)
#define DIM HEX(0x6E737A)
#define GREEN HEX(0x3CDC3C)
#define PLAT_TOP HEX(0x3CFF3C)
#define PLAT_BODY HEX(0x1E8C1E)

/* the shake: everything of the fight moves by it */
static int ox_, oy_;

static inline int S(float v) { return bt_ifloor(v * 0.5f); }
static inline color_t *px(int x, int y) { return strip + (y - gfx_y0) * SCREEN_W + x; }

/* ---------------------------------------------------------------- pictures */
/* a picture's palette in the screen's colours (each picture has its own: converted when it changes) */
static const color_t *pal_of(const art_t *a) {
  static color_t cache[16];
  static const uint32_t *src;
  if (src != a->pal) {
    src = a->pal;
    for (int k = 0; k < 16; k++) cache[k] = gfx_rgb(a->pal[k]);
  }
  return cache;
}
static inline int art_px(const art_t *a, int x, int y) {
  int i = y * a->w + x;
  return (a->px[i >> 1] >> ((i & 1) * 4)) & 15;
}

/* a picture, its anchor at (x, y); mirrored; tint: every pixel that colour (0: its own) */
static void blit(const art_t *a, int x, int y, bool flip, color_t tint, bool tinted) {
  int x0 = x - (flip ? a->w - 1 - a->ax : a->ax), y0 = y - a->ay;
  if (y0 >= gfx_y1 || y0 + a->h <= gfx_y0 || x0 >= SCREEN_W || x0 + a->w <= 0) return;
  const color_t *pal = pal_of(a);
  int ya = y0 < gfx_y0 ? gfx_y0 : y0, yb = y0 + a->h > gfx_y1 ? gfx_y1 : y0 + a->h;
  for (int yy = ya; yy < yb; yy++) {
    int sy = yy - y0;
    for (int sx = 0; sx < a->w; sx++) {
      int xx = x0 + sx;
      if (xx < 0 || xx >= SCREEN_W) continue;
      int c = art_px(a, flip ? a->w - 1 - sx : sx, sy);
      if (c) *px(xx, yy) = tinted ? tint : pal[c];
    }
  }
}

/* a picture turned by ang degrees around its anchor at (cx, cy) on the screen, sa screen pixels a picture
 * pixel along its length (x) and sc across (y) */
static void blit_turned(const art_t *a, float cx, float cy, float ang, float sa, float sc, int alpha32) {
  float c = bt_sinf_deg(ang + 90.0f), s = bt_sinf_deg(ang);
  /* the picture's corners on the screen: its box */
  float lx[2] = {-(float)a->ax * sa, (float)(a->w - a->ax) * sa}, ly[2] = {-(float)a->ay * sc, (float)(a->h - a->ay) * sc};
  float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
  for (int i = 0; i < 2; i++)
    for (int j = 0; j < 2; j++) {
      float X = cx + lx[i] * c - ly[j] * s, Y = cy + lx[i] * s + ly[j] * c;
      minx = X < minx ? X : minx, maxx = X > maxx ? X : maxx;
      miny = Y < miny ? Y : miny, maxy = Y > maxy ? Y : maxy;
    }
  int y0 = bt_ifloor(miny), y1 = bt_ifloor(maxy) + 1, x0 = bt_ifloor(minx), x1 = bt_ifloor(maxx) + 1;
  if (y0 < gfx_y0) y0 = gfx_y0;
  if (y1 > gfx_y1) y1 = gfx_y1;
  if (x0 < 0) x0 = 0;
  if (x1 > SCREEN_W) x1 = SCREEN_W;
  if (y0 >= y1 || x0 >= x1) return;
  const color_t *pal = pal_of(a);
  float ia = 1.0f / sa, ic = 1.0f / sc;
  for (int y = y0; y < y1; y++) {
    float dy = (float)y + 0.5f - cy, dx = (float)x0 + 0.5f - cx;
    float u = (dx * c + dy * s) * ia + (float)a->ax, v = (-dx * s + dy * c) * ic + (float)a->ay;
    float du = c * ia, dv = -s * ic;
    color_t *p = px(x0, y);
    for (int x = x0; x < x1; x++, u += du, v += dv, p++) {
      if (u < 0 || v < 0 || u >= (float)a->w || v >= (float)a->h) continue;
      int k = art_px(a, (int)u, (int)v);
      if (k) *p = alpha32 >= 32 ? pal[k] : gfx_mix(*p, pal[k], alpha32);
    }
  }
}

/* a convex quad (screen pixels, corners in order), blended white */
static void quad(const float qx[4], const float qy[4], color_t col, int alpha32, int cx0, int cx1, int cy0, int cy1) {
  float miny = qy[0], maxy = qy[0];
  for (int i = 1; i < 4; i++) miny = qy[i] < miny ? qy[i] : miny, maxy = qy[i] > maxy ? qy[i] : maxy;
  int y0 = bt_ifloor(miny), y1 = bt_ifloor(maxy) + 1;
  if (y0 < gfx_y0) y0 = gfx_y0;
  if (y1 > gfx_y1) y1 = gfx_y1;
  if (y0 < cy0) y0 = cy0;
  if (y1 > cy1) y1 = cy1;
  for (int y = y0; y < y1; y++) {
    float yc = (float)y + 0.5f, lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < 4; i++) {
      int j = (i + 1) & 3;
      float ya = qy[i], yb = qy[j];
      if ((yc < ya) == (yc < yb)) continue;
      float x = qx[i] + (qx[j] - qx[i]) * (yc - ya) / (yb - ya);
      lo = x < lo ? x : lo, hi = x > hi ? x : hi;
    }
    if (lo > hi) continue;
    int a = bt_ifloor(lo + 0.5f), b = bt_ifloor(hi + 0.5f);
    if (a < cx0) a = cx0;
    if (b > cx1) b = cx1;
    color_t *p = px(a, y);
    for (int x = a; x < b; x++, p++) *p = alpha32 >= 32 ? col : gfx_mix(*p, col, alpha32);
  }
}

/* ---------------------------------------------------------------- the zone and what's in it */
typedef struct {
  int x0, y0, x1, y1;     /* the zone's inside, on the screen */
} clip_t;

static void fill_clip(int x, int y, int w, int h, color_t c, const clip_t *k) {
  int x0 = x < k->x0 ? k->x0 : x, y0 = y < k->y0 ? k->y0 : y;
  int x1 = x + w > k->x1 ? k->x1 : x + w, y1 = y + h > k->y1 ? k->y1 : y + h;
  if (x1 > x0 && y1 > y0) gfx_fill(x0, y0, x1 - x0, y1 - y0, c);
}

/* a bone from (x0, y0) to (x1, y1) on the screen: knobs at both ends */
static void bone(int x0, int y0, int x1, int y1, bool vertical, color_t c, const clip_t *k) {
  int w = x1 - x0, h = y1 - y0;
  if (w <= 0 || h <= 0) return;
  if (vertical) {
    if (h < 6) return fill_clip(x0, y0, w, h, c, k);
    fill_clip(x0 + 1, y0, w - 2, h, c, k);
    fill_clip(x0, y0, 2, 2, c, k), fill_clip(x0 + w - 2, y0, 2, 2, c, k), fill_clip(x0, y0 + 1, w, 1, c, k);
    fill_clip(x0, y1 - 2, 2, 2, c, k), fill_clip(x0 + w - 2, y1 - 2, 2, 2, c, k), fill_clip(x0, y1 - 2, w, 1, c, k);
  } else {
    if (w < 6) return fill_clip(x0, y0, w, h, c, k);
    fill_clip(x0, y0 + 1, w, h - 2, c, k);
    fill_clip(x0, y0, 2, 2, c, k), fill_clip(x0, y0 + h - 2, 2, 2, c, k), fill_clip(x0 + 1, y0, 1, h, c, k);
    fill_clip(x1 - 2, y0, 2, 2, c, k), fill_clip(x1 - 2, y0 + h - 2, 2, 2, c, k), fill_clip(x1 - 2, y0, 1, h, c, k);
  }
}

static color_t bone_color(int c) { return c == COL_BLUE ? BONE_BLUE : c == COL_ORANGE ? BONE_ORANGE : WHITE; }

static void draw_stab(const stab_t *s, const clip_t *k) {
  int x0 = S(s->x) + ox_, y0 = S(s->y) + oy_, x1 = S(s->x + s->w) + ox_, y1 = S(s->y + s->h) + oy_;
  if (s->state == STAB_WARN) {
    /* the warning: a red frame */
    gfx_fill(x0, y0, x1 - x0, 1, RED), gfx_fill(x0, y1 - 1, x1 - x0, 1, RED);
    gfx_fill(x0, y0, 1, y1 - y0, RED), gfx_fill(x1 - 1, y0, 1, y1 - y0, RED);
    return;
  }
  if (s->dir == DIR_DOWN || s->dir == DIR_UP)
    for (int x = x0; x < x1; x += 6) bone(x, y0, x + 5 < x1 ? x + 5 : x1, y1, true, WHITE, k);
  else
    for (int y = y0; y < y1; y += 6) bone(x0, y, x1, y + 5 < y1 ? y + 5 : y1, false, WHITE, k);
}

static void draw_heart(const world_t *w) {
  int x = S(w->hx) + ox_, y = S(w->hy) + oy_;
  color_t c = w->heart_mode == HEART_BLUE ? SOUL_BLUE : RED;
  int dir = w->heart_mode == HEART_BLUE ? w->heart_dir : DIR_DOWN;
  /* turned with its gravity: its point towards the floor it falls to */
  const art_t *a = &ART_HEART[0];
  for (int yy = 0; yy < 8; yy++) {
    int sy = y - 4 + yy;
    if (sy < gfx_y0 || sy >= gfx_y1) continue;
    for (int xx = 0; xx < 8; xx++) {
      int sx = x - 4 + xx, u = xx, v = yy;
      if (dir == DIR_UP) u = 7 - xx, v = 7 - yy;
      else if (dir == DIR_LEFT) u = yy, v = 7 - xx;
      else if (dir == DIR_RIGHT) u = 7 - yy, v = xx;
      if (u >= a->w || v >= a->h || sx < 0 || sx >= SCREEN_W) continue;
      if (art_px(a, u, v)) *px(sx, sy) = c;
    }
  }
}

/* ---------------------------------------------------------------- Sans */
/* his parts as jcw87's Bad Time Simulator stacks them: the legs at his feet, the torso (or, slamming, the whole
 * body: 15 frames a second) on the legs' attach point, the head on the torso's, the sweat on the head's */
static void draw_sans(const world_t *w, int feet_x, int feet_y, uint32_t tick) {
  float t = (float)w->anim_t / 30.0f;
  float tx = 0, ty = 0, hx = 0, hy = 0;
  if (w->body == BODY_NONE) {
    if (w->anim == ANIM_IDLE) {
      float p = t - 1.2f * (float)bt_ifloor(t / 1.2f);
      tx = bt_sinf_deg(360.0f * p / 1.2f), ty = bt_sinf_deg(720.0f * p / 1.2f);
      hy = -bt_sinf_deg(720.0f * p / 1.2f) * 0.4f;
    } else if (w->anim == ANIM_HEADBOB) {
      float p = t - 1.1f * (float)bt_ifloor(t / 1.1f);
      hx = bt_sinf_deg(360.0f * p / 1.1f), hy = bt_sinf_deg(720.0f * p / 1.1f);
    } else if (w->anim == ANIM_TIRED) {
      float p = t - 3.8f * (float)bt_ifloor(t / 3.8f);
      ty = bt_sinf_deg(360.0f * p / 3.8f) * 2, hy = ty;
    }
  }
  /* (offsets in the original's pixels: rounded to the screen's) */
  int itx = bt_ifloor(tx * 0.5f + 0.5f), ity = bt_ifloor(ty * 0.5f + 0.5f);
  int ihx = itx + bt_ifloor(hx * 0.5f + 0.5f), ihy = ity + bt_ifloor(hy * 0.5f + 0.5f);
  const art_t *legs = &ART_LEGS[0];
  blit(legs, feet_x, feet_y, false, 0, false);
  int tbx = feet_x - legs->ax + legs->jx + itx, tby = feet_y - legs->ay + legs->jy + ity;
  const art_t *torso;
  if (w->body != BODY_NONE) {
    static const art_t *const SET[4] = {ART_BODY_UP, ART_BODY_DOWN, ART_BODY_LEFT, ART_BODY_RIGHT};
    static const uint8_t COUNT[4] = {5, 4, 5, 5};
    int n = w->body & 3, k = w->anim_t / 2;
    torso = &SET[n][k < COUNT[n] ? k : COUNT[n] - 1];
  } else {
    torso = &ART_TORSO[w->torso == TORSO_SHRUG];
  }
  blit(torso, tbx, tby, false, 0, false);
  int hbx = tbx - torso->ax + torso->jx + (ihx - itx), hby = tby - torso->ay + torso->jy + (ihy - ity);
  const art_t *head = w->head == HEAD_BLUEEYE && ((tick / 2) & 1) ? ART_HEAD_BLUE2 : &ART_HEAD[w->head & 7];
  blit(head, hbx, hby, false, 0, false);
  if (w->sweat > 0)
    blit(&ART_SWEAT[w->sweat < 3 ? w->sweat - 1 : 2], hbx - head->ax + head->jx, hby - head->ay + head->jy, false, 0,
         false);
}

/* ---------------------------------------------------------------- words */
static const np_font_t *const BODY_FONT = &BT_FONT_BOX;
static const np_font_t *const SMALL = &np_font_small;

/* the first n letters of s, line by line, from (x, baseline) */
static void lines(const np_font_t *f, int x, int base, int step, const char *s, int n, color_t c) {
  char buf[80];
  int k = 0, line = 0;
  for (int i = 0; i <= n; i++) {
    char ch = i < n ? s[i] : 0;
    if (ch == '\n' || ch == 0 || k >= 78) {
      buf[k] = 0;
      if (k) gfx_text(f, x, base + line * step, buf, c, 256);
      k = 0, line++;
      if (!ch) break;
      if (ch == '\n') continue;
    }
    buf[k++] = ch;
  }
}

/* s wrapped to width (into out, with line breaks); returns the number of lines */
static int wrap(const np_font_t *f, const char *s, int width, char *out, int cap) {
  int n = 0, linew = 0, nl = 1, word = 0;
  for (const char *p = s; *p && n < cap - 1; p++) {
    if (*p == '\n') {
      out[n++] = '\n', linew = 0, nl++, word = n;
      continue;
    }
    char one[2] = {*p, 0};
    int cw = gfx_text_width(f, one);
    if (*p == ' ') word = n + 1;
    if (linew + cw > width && word > 0 && out[word - 1] == ' ') {
      out[word - 1] = '\n', nl++;
      linew = 0;
      for (int i = word; i < n; i++) {
        char o[2] = {out[i], 0};
        linew += gfx_text_width(f, o);
      }
    }
    out[n++] = *p, linew += cw;
  }
  out[n] = 0;
  return nl;
}

static void key_line(const char *s) {
  if (s && *s) gfx_text_center(SMALL, SCREEN_W / 2, 10, s, DIM, 256);
}

/* ---------------------------------------------------------------- the fight */
static void draw_ui(const fight_t *f) {
  const world_t *w = &f->w;
  gfx_text(BODY_FONT, 16, 210, "YOU   LV 19", WHITE, 256);
  blit(&ART_LABEL[0], 112 + ART_LABEL[0].ax, 203 + ART_LABEL[0].ay, false, 0, false);
  int bw = 55, hpw = w->hp * bw / MAX_HP, krx = (w->hp - w->kr) * bw / MAX_HP;
  if (hpw < 0) hpw = 0;
  if (krx < 0) krx = 0;
  gfx_fill(128, 200, bw, 11, HP_RED);
  gfx_fill(128, 200, hpw, 11, YELLOW);
  if (w->kr > 0) gfx_fill(128 + krx, 200, hpw - krx > 1 ? hpw - krx : 1, 11, MAGENTA);
  blit(&ART_LABEL[1], 188 + ART_LABEL[1].ax, 203 + ART_LABEL[1].ay, false, 0, false);
  char num[16];
  int hp = w->hp < 0 ? 0 : w->hp, k = 0;
  num[k++] = (char)('0' + hp / 10 % 10), num[k++] = (char)('0' + hp % 10);
  memcpy(num + k, " / 92", 6);
  gfx_text(BODY_FONT, 208, 210, num, w->kr > 0 ? MAGENTA : WHITE, 256);
  for (int i = 0; i < 4; i++) {
    static const int BX[4] = {16, 92, 172, 248};
    bool on = (f->state == FS_MENU || f->state == FS_SUB) && f->sel == i;
    const art_t *a = on ? &ART_BUTTON_ON[i] : &ART_BUTTON[i];
    blit(a, BX[i] + a->ax, 216 + a->ay, false, 0, false);
  }
}

static void draw_box_words(const fight_t *f) {
  const world_t *w = &f->w;
  int x = S(w->zl + 5) + 6, top = S(w->zt + 5);
  if (f->state == FS_SUB) {
    static char buf[48];
    if (f->sub == SUB_ITEM) {
      int page = f->sub_sel / 4;
      for (int i = page * 4; i < f->nitems && i < page * 4 + 4; i++) {
        float hx, hy;
        fight_item_pos(i, &hx, &hy);
        memcpy(buf, "* ", 2);
        strcpy(buf + 2, ITEMS[f->items[i]].name);
        gfx_text(BODY_FONT, S(hx) + 8, S(hy) + 5, buf, WHITE, 256);
      }
      char pg[8] = "PAGE 1";
      pg[5] = (char)('1' + page);
      gfx_text(BODY_FONT, 192, 182, pg, WHITE, 256);
      return;
    }
    const char *item = f->sub == SUB_ENEMY ? "* Sans" : f->sub == SUB_ACT ? "* Check" : "* Spare";
    float hx, hy;
    fight_item_pos(0, &hx, &hy);
    gfx_text(BODY_FONT, S(hx) + 8, S(hy) + 5, item, f->sub == SUB_MERCY ? YELLOW : WHITE, 256);
    return;
  }
  if (f->text_kind == TXT_BOX) lines(BODY_FONT, x, top + 15, 16, f->text, f->text_shown, WHITE);
}

static void draw_bubble(const fight_t *f) {
  const world_t *w = &f->w;
  const art_t *b = ART_BUBBLE;
  float feet_y = (w->ox_speed != 0 ? w->zt : w->tt) - 16;
  int bx = S(w->ox + 64), by = S(feet_y - 128);
  if (bx + b->w > SCREEN_W - 2) bx = SCREEN_W - 2 - b->w;
  if (by < 16) by = 16;
  blit(b, bx + b->ax, by + b->ay, false, 0, false);
  /* only the letters typed so far (the line breaks are where they'll end up), right of the tail */
  static char wrapped[TEXT_MAX + 16];
  wrap(&BT_FONT_BUBBLE, f->text, b->w - 26, wrapped, sizeof wrapped);
  int shown = f->text_shown, k = 0, out = 0;
  for (; wrapped[k] && out < shown; k++)
    if (wrapped[k] != '\n') out++;
  lines(&BT_FONT_BUBBLE, bx + 18, by + 17, 12, wrapped, k, BLACK);
}

static void draw_target(const fight_t *f, const clip_t *k) {
  const world_t *w = &f->w;
  int x0 = k->x0, x1 = k->x1, y0 = k->y0, y1 = k->y1, cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
  int fade = f->state == FS_TARGET ? 32 : f->bar_fade * 32 / 40;
  if (fade <= 0) return;
  /* the target: a lens of lines, its middle the best */
  int hw = (x1 - x0) / 2 - 4, hh = (y1 - y0) / 2 - 4;
  for (int y = y0; y < y1; y++) {
    if (y < gfx_y0 || y >= gfx_y1) continue;
    int dy = y - cy;
    /* x where the lens is this high: |dy| <= hh * (1 - (dx / hw)^2) */
    float r = 1.0f - (float)(dy < 0 ? -dy : dy) / (float)hh;
    if (r < 0) continue;
    float sq = r, g = 0.5f;
    for (int i = 0; i < 8; i++) g = 0.5f * (g + sq / (g > 1e-3f ? g : 1e-3f));
    int span = (int)((float)hw * g);
    color_t edge = gfx_mix(BLACK, HEX(0x1FA84A), fade);
    gfx_fill(cx - span, y, 1, 1, edge), gfx_fill(cx + span, y, 1, 1, edge);
    for (int i = 1; i < 8; i++) {
      int dx = span * i / 8;
      color_t c = gfx_mix(BLACK, i < 2 ? HEX(0xE8E8E8) : HEX(0x2E6E44), fade);
      if (!(y & 1)) gfx_fill(cx - dx, y, 1, 1, c), gfx_fill(cx + dx, y, 1, 1, c);
    }
  }
  gfx_fill(cx, y0 + 2, 1, y1 - y0 - 4, gfx_mix(BLACK, WHITE, fade));
  /* the bar */
  int bx = S(f->bar_x) + ox_;
  bool flash = f->bar_hit && ((w->tick / 2) & 1);
  color_t in = flash ? BLACK : WHITE, out = flash ? WHITE : BLACK;
  if (f->state == FS_TARGET || f->bar_hit) {
    gfx_fill(bx - 3, y0, 7, y1 - y0, gfx_mix(BLACK, out, fade));
    gfx_fill(bx - 2, y0 + 1, 5, y1 - y0 - 2, gfx_mix(BLACK, in, fade));
  }
}

static void draw_slash(const fight_t *f, int cx, int cy) {
  if (f->slash_t <= 0) return;
  int t = 18 - f->slash_t, len = t < 8 ? t * 4 : 32;
  for (int i = 0; i < len; i++) {
    int x = cx - 16 + i, y = cy - 16 + i;
    gfx_fill(x, y, 3, 2, i > len - 4 ? HEX(0xFF8080) : RED);
  }
}

typedef struct {
  const fight_t *f;
  const char *keys, *title;
} fight_ctx_t;

static void fight_scene(void *ctx) {
  const fight_ctx_t *c = ctx;
  const fight_t *f = c->f;
  const world_t *w = &f->w;
  memset(strip, 0, sizeof strip);
  if (!w->black) {
    /* Sans */
    float feet = (w->ox_speed != 0 ? w->zt : w->tt) - 16;
    int fx = S(w->ox) + ox_, fy = S(feet) + oy_;
    draw_sans(w, fx, fy, w->tick);
    draw_slash(f, fx, fy - 30);
    if (f->miss_t > 0) gfx_text_center(&np_font_title, 150, 46, "MISS", HEX(0xC0C0C0), 256);
    /* the zone */
    clip_t k = {S(w->zl + 5) + ox_, S(w->zt + 5) + oy_, S(w->zr - 5) + ox_, S(w->zb - 5) + oy_};
    if (k.x0 < 0) k.x0 = 0;
    if (k.x1 > SCREEN_W) k.x1 = SCREEN_W;
    if (!w->zone_hidden) {
      int zl = S(w->zl) + ox_, zt = S(w->zt) + oy_, zr = S(w->zr) + ox_, zb = S(w->zb) + oy_;
      gfx_fill(zl, zt, zr - zl, k.y0 - zt, WHITE);
      gfx_fill(zl, k.y1, zr - zl, zb - k.y1, WHITE);
      gfx_fill(zl, zt, k.x0 - zl, zb - zt, WHITE);
      gfx_fill(k.x1, zt, zr - k.x1, zb - zt, WHITE);
    }
    if (k.y1 > gfx_y0 && k.y0 < gfx_y1) {
      for (int i = 0; i < w->nplats; i++) {
        const plat_t *p = &w->plats[i];
        int x0 = S(p->x) + ox_, y0 = S(p->y) + oy_, x1 = S(p->x + p->w) + ox_;
        fill_clip(x0, y0, x1 - x0, 1, PLAT_TOP, &k);
        fill_clip(x0, y0 + 1, x1 - x0, 2, PLAT_BODY, &k);
        fill_clip(x0, y0 + 1, 1, 2, PLAT_TOP, &k), fill_clip(x1 - 1, y0 + 1, 1, 2, PLAT_TOP, &k);
      }
      for (int i = 0; i < w->nbones; i++) {
        const bone_t *b = &w->bones[i];
        int y0 = S(b->y) + oy_, y1 = S(b->y + b->h) + oy_;
        if (y1 <= gfx_y0 || y0 >= gfx_y1) continue;
        int x0 = S(b->x) + ox_, x1 = S(b->x + b->w) + ox_;
        if (x1 <= k.x0 || x0 >= k.x1) continue;
        bone(x0, y0, x1, y1, b->vertical, bone_color(b->color), &k);
      }
      for (int i = 0; i < w->nstabs; i++) draw_stab(&w->stabs[i], &k);
      if (f->state == FS_TARGET || f->bar_fade > 0) draw_target(f, &k);
    }
    /* the blasters' beams, then the blasters */
    for (int i = 0; i < w->nblasters; i++) {
      const blaster_t *b = &w->blasters[i];
      if (b->state != BL_LEAVE || b->base <= 0) continue;
      float k3 = b->scale, qx[4], qy[4];
      int a = b->opacity * 32 / 100;
      static const float FROM[3] = {35, 30, 25}, DIV[3] = {1, 1.25f, 2};
      for (int j = 2; j >= 0; j--) {
        float half = (b->base / DIV[j] + b->sine) / 2;
        if (half <= 0) continue;
        blaster_beam(b, FROM[j] * k3, j ? 35 * k3 : 35 * k3 + 1000, half, qx, qy);
        for (int m = 0; m < 4; m++) qx[m] = qx[m] * 0.5f + (float)ox_, qy[m] = qy[m] * 0.5f + (float)oy_;
        quad(qx, qy, WHITE, a, 0, SCREEN_W, 0, SCREEN_H);
      }
    }
    for (int i = 0; i < w->nblasters; i++) {
      const blaster_t *b = &w->blasters[i];
      int k = b->state == BL_FIRE ? 3 - b->timer : b->state == BL_LEAVE ? 3 + b->beam_t : -1;
      const art_t *a = &ART_BLASTER[k < 0 ? 0 : 1 + (k < 5 ? k : 3 + (k - 3) % 2)];
      blit_turned(a, b->x * 0.5f + (float)ox_, b->y * 0.5f + (float)oy_, b->ang, b->sy * 0.5f, b->sx * 0.5f, 32);
    }
    /* the menu's bones */
    for (int i = 0; i < w->nmbones; i++) {
      const mbone_t *m = &w->mbones[i];
      int x0 = S(m->x) + ox_, y0 = S(m->y) + oy_;
      clip_t all = {0, 0, SCREEN_W, SCREEN_H};
      bone(x0, y0, x0 + 7, y0 + 22, true, WHITE, &all);
    }
    draw_ui(f);
    draw_box_words(f);
    if (f->text_kind == TXT_BUBBLE) draw_bubble(f);
  }
  if (w->heart_on) {
    if (f->state == FS_DEATH && f->death_t >= 60) {
      for (int i = 0; i < MAX_SHARDS; i++) {
        const shard_t *s = &f->shards[i];
        int x = S(s->x), y = S(s->y);
        bool t = (f->death_t / 3 + i) & 1;
        gfx_fill(x, y, 2, t ? 2 : 1, RED), gfx_fill(x + (t ? 0 : 1), y + 1, 1, 1, RED);
      }
    } else if (f->state == FS_DEATH && f->death_t >= 20) {
      blit(&ART_HEART[1], S(w->hx), S(w->hy), false, RED, true);
    } else draw_heart(w);
  }
  if (f->banner_t > 0 && f->banner)
    gfx_text(&np_font_title, 8, 46, f->banner == 1 ? "SUCCESS" : "FAILURE", f->banner == 1 ? GREEN : RED, 256);
  if (c->title) gfx_text(SMALL, 6, 24, c->title, GREY, 256);
  key_line(c->keys);
}

void draw_fight(const fight_t *f, const char *title) {
  const world_t *w = &f->w;
  ox_ = oy_ = 0;
  if (w->shake > 0) {
    uint32_t r = w->tick * 2654435761u;
    int n = (w->shake + 1) / 2;
    ox_ = (r >> 7) & 1 ? n : -n, oy_ = (r >> 9) & 1 ? n : -n;
  }
  gfx_buf = strip;
  fight_ctx_t c = {f, fight_keys(f), title};
  gfx_render(fight_scene, &c, 0, SCREEN_H);
}

/* ---------------------------------------------------------------- the other screens */
typedef struct {
  const screen_t *s;
} screen_ctx_t;

static void heart_cursor(int x, int y) {
  for (int yy = 0; yy < ART_HEART[0].h; yy++)
    for (int xx = 0; xx < ART_HEART[0].w; xx++)
      if (art_px(&ART_HEART[0], xx, yy)) gfx_fill(x + xx, y + yy, 1, 1, RED);
}

static void screen_scene(void *ctx) {
  const screen_t *s = ((const screen_ctx_t *)ctx)->s;
  memset(strip, 0, sizeof strip);
  if (s->sans) {
    world_t w;
    memset(&w, 0, sizeof w);
    w.anim = ANIM_IDLE, w.body = BODY_NONE, w.head = s->sans_head, w.anim_t = (int16_t)(s->tick % 3600);
    draw_sans(&w, 160, s->sans_y, s->tick);
  }
  if (s->title) gfx_text_center(&np_font_title, SCREEN_W / 2, s->title_y, s->title, WHITE, 256);
  for (int i = 0; i < s->n; i++) {
    int y = s->list_y + (i - s->first) * s->step;
    if (i < s->first || y > SCREEN_H - 14) continue;
    bool on = i == s->sel;
    gfx_text(BODY_FONT, s->list_x + 14, y, s->items[i], on ? YELLOW : WHITE, 256);
    if (on) heart_cursor(s->list_x, y - 8);
  }
  if (s->more_up) gfx_text_center(SMALL, SCREEN_W / 2, s->list_y - s->step + 2, "^", GREY, 256);
  if (s->more_down) gfx_text_center(SMALL, SCREEN_W / 2, s->list_y + s->rows * s->step - 4, "v", GREY, 256);
  if (s->body) lines(BODY_FONT, s->body_x, s->body_y, 16, s->body, (int)strlen(s->body), WHITE);
  if (s->foot) gfx_text_center(SMALL, SCREEN_W / 2, 232, s->foot, GREY, 256);
  key_line(s->keys);
}

void draw_screen(const screen_t *s) {
  gfx_buf = strip;
  screen_ctx_t c = {s};
  gfx_render(screen_scene, &c, 0, SCREEN_H);
}

/* the pause card over the fight */
typedef struct {
  const fight_t *f;
  const screen_t *s;
} pause_ctx_t;
static void pause_scene(void *ctx) {
  const pause_ctx_t *p = ctx;
  fight_ctx_t c = {p->f, 0, 0};
  fight_scene(&c);
  for (int y = gfx_y0; y < gfx_y1; y++)
    for (int x = 0; x < SCREEN_W; x++) *px(x, y) = gfx_mix(*px(x, y), BLACK, 22);
  const screen_t *s = p->s;
  int bx = 70, by = 60, bw = 180, bh = 112;
  gfx_rrect(bx, by, bw, bh, 6, HEX(0x101014), 256);
  gfx_rrect_outline(bx, by, bw, bh, 6, 2, WHITE, 256);
  gfx_text_center(&np_font_title, SCREEN_W / 2, by + 28, s->title, WHITE, 256);
  for (int i = 0; i < s->n; i++) {
    int y = by + 52 + i * 18;
    bool on = i == s->sel;
    gfx_text(BODY_FONT, bx + 44, y, s->items[i], on ? YELLOW : WHITE, 256);
    if (on) heart_cursor(bx + 30, y - 8);
  }
  key_line(s->keys);
}
void draw_pause(const fight_t *f, const screen_t *s) {
  ox_ = oy_ = 0;
  gfx_buf = strip;
  pause_ctx_t c = {f, s};
  gfx_render(pause_scene, &c, 0, SCREEN_H);
}
