/* Pictures and the song's screen (draw.h). The game's 1280 x 720 screen is drawn at a quarter across and a third
 * down: the notes cross the screen in the same time as in the game. */
#include <math.h>
#include <string.h>
#include "draw.h"
#include "inflate.h"

void plat_push(int x, int y, int w, int h, const uint16_t *px);
void np_push(int x, int y, int w, int h, const color_t *px) { plat_push(x, y, w, h, px); }

/* ------------------------------------------------------------------------------------------------ RAM */
/* the bold letters (kept), then a song's or the menus' pictures */
#define POOL (FNF_BOLD_BYTES + FNF_CAST_MAX + FNF_HUD_MAX + 2 * FNF_ICON_MAX + FNF_TRANSIENT_MAX + 1024)
static uint8_t bg_px[FNF_BG_BYTES];
static uint8_t pool[POOL];
static uint32_t used, base;
static color_t *strip;   /* (on main's stack: draw_strip_buffer) */
void draw_strip_buffer(color_t *p) { strip = p; }

void ram_reset(void) { used = base; }

uint8_t *ram_take(uint32_t n) {
  n = (n + 3) & ~3u;
  if (used + n > POOL) return 0;
  uint8_t *p = pool + used;
  used += n;
  return p;
}

uint8_t *bg_pixels(void) { return bg_px; }

bool pic_load(pic_t *p, unsigned id, uint8_t *dst, uint32_t cap) {
  if (id >= FNF_SPRITES || !dst) return false;
  const fnf_sprite_t *s = &fnf_sprite[id];
  uint32_t stride = (s->w + 1u) / 2, n = stride * s->h;
  if (n > cap || fnf_inflate(fnf_blob + s->at, s->size, dst, cap) != (int32_t)n) return false;
  p->id = (uint16_t)id, p->w = s->w, p->h = s->h, p->stride = (uint16_t)stride, p->x = s->x, p->y = s->y;
  p->pal = fnf_palette[s->pal], p->px = dst;
  return true;
}

bool pic_new(pic_t *p, unsigned id) {
  if (id >= FNF_SPRITES) return false;
  const fnf_sprite_t *s = &fnf_sprite[id];
  uint32_t n = (s->w + 1u) / 2 * s->h;
  uint8_t *d = ram_take(n);
  return d && pic_load(p, id, d, n);
}

static pic_t bg;
bool bg_load(unsigned id) { return pic_load(&bg, id, bg_px, sizeof bg_px) && bg.w == SCREEN_W && bg.h == SCREEN_H; }

/* ------------------------------------------------------------------------------------------------ drawing */
void push_strip(void) { np_push(0, gfx_y0, SCREEN_W, gfx_y1 - gfx_y0, gfx_buf); }

void blit(const pic_t *p, int x, int y, int alpha, bool flip_y) {
  if (!p->px || alpha <= 0) return;
  int y0 = y < gfx_y0 ? gfx_y0 : y, y1 = y + p->h > gfx_y1 ? gfx_y1 : y + p->h;
  int x0 = x < 0 ? 0 : x, x1 = x + p->w > SCREEN_W ? SCREEN_W : x + p->w;
  if (y0 >= y1 || x0 >= x1) return;
  const uint16_t *pal = p->pal;
  for (int r = y0; r < y1; r++) {
    int sy = flip_y ? p->h - 1 - (r - y) : r - y;
    const uint8_t *src = p->px + sy * p->stride;
    color_t *dst = gfx_buf + (r - gfx_y0) * SCREEN_W;
    for (int c = x0; c < x1; c++) {
      int sx = c - x;
      unsigned v = src[sx >> 1];
      if (!v) {   /* two clear pixels */
        if (!(sx & 1)) c++;
        continue;
      }
      v = sx & 1 ? v >> 4 : v & 15;
      if (!v) continue;
      dst[c] = alpha >= 32 ? pal[v] : gfx_mix(dst[c], pal[v], alpha);
    }
  }
}

void blit_at(const pic_t *p, int ax, int ay, int alpha) { blit(p, ax + p->x, ay + p->y, alpha, false); }

void blit_tinted(const pic_t *p, int x, int y, color_t tint, int t32) {
  if (!p->px) return;
  int y0 = y < gfx_y0 ? gfx_y0 : y, y1 = y + p->h > gfx_y1 ? gfx_y1 : y + p->h;
  int x0 = x < 0 ? 0 : x, x1 = x + p->w > SCREEN_W ? SCREEN_W : x + p->w;
  for (int r = y0; r < y1; r++) {
    const uint8_t *src = p->px + (r - y) * p->stride;
    color_t *dst = gfx_buf + (r - gfx_y0) * SCREEN_W;
    for (int c = x0; c < x1; c++) {
      int sx = c - x;
      unsigned v = src[sx >> 1];
      v = sx & 1 ? v >> 4 : v & 15;
      if (v) dst[c] = gfx_mix(p->pal[v], tint, t32);
    }
  }
}

static void blit_scaled_flip(const pic_t *p, int x, int y, int w, int h, int alpha, bool flip);
void blit_scaled(const pic_t *p, int x, int y, int w, int h, int alpha) { blit_scaled_flip(p, x, y, w, h, alpha, false); }
static void blit_scaled_flip(const pic_t *p, int x, int y, int w, int h, int alpha, bool flip) {
  if (!p->px || w <= 0 || h <= 0 || alpha <= 0) return;
  int y0 = y < gfx_y0 ? gfx_y0 : y, y1 = y + h > gfx_y1 ? gfx_y1 : y + h;
  int x0 = x < 0 ? 0 : x, x1 = x + w > SCREEN_W ? SCREEN_W : x + w;
  for (int r = y0; r < y1; r++) {
    int sy = (r - y) * p->h / h;
    const uint8_t *src = p->px + sy * p->stride;
    color_t *dst = gfx_buf + (r - gfx_y0) * SCREEN_W;
    for (int c = x0; c < x1; c++) {
      int sx = (c - x) * p->w / w;
      if (flip) sx = p->w - 1 - sx;
      unsigned v = src[sx >> 1];
      v = sx & 1 ? v >> 4 : v & 15;
      if (v) dst[c] = alpha >= 32 ? p->pal[v] : gfx_mix(dst[c], p->pal[v], alpha);
    }
  }
}

/* the background, 16 colours (darkened by dark1000 thousandths), zoomed round the screen's middle */
void draw_bg(const uint16_t *pal, int dark1000, float zoom) {
  color_t lut[16];
  int t = dark1000 * 32 / 1000;
  for (int i = 0; i < 16; i++) lut[i] = t > 0 ? gfx_mix(pal[i], 0, t) : pal[i];
  if (zoom <= 1.0005f) {
    for (int r = gfx_y0; r < gfx_y1; r++) {
      const uint8_t *src = bg_px + r * (SCREEN_W / 2);
      uint32_t *dst = (uint32_t *)(gfx_buf + (r - gfx_y0) * SCREEN_W);
      for (int i = 0; i < SCREEN_W / 2; i++) {
        unsigned v = src[i];
        dst[i] = lut[v & 15] | (uint32_t)lut[v >> 4] << 16;
      }
    }
    return;
  }
  int32_t step = (int32_t)(65536.0f / zoom);
  int32_t sx0 = (int32_t)((160.0f - 160.0f / zoom) * 65536.0f);
  for (int r = gfx_y0; r < gfx_y1; r++) {
    int sy = (int)(120.0f + (r - 120) / zoom);
    sy = sy < 0 ? 0 : sy > SCREEN_H - 1 ? SCREEN_H - 1 : sy;
    const uint8_t *src = bg_px + sy * (SCREEN_W / 2);
    color_t *dst = gfx_buf + (r - gfx_y0) * SCREEN_W;
    int32_t sx = sx0;
    for (int c = 0; c < SCREEN_W; c++, sx += step) {
      int x = sx >> 16;
      unsigned v = src[x >> 1];
      dst[c] = lut[x & 1 ? v >> 4 : v & 15];
    }
  }
}

/* ------------------------------------------------------------------------------------------------ the bold font */
static pic_t bold[95];
static bool bold_ready;

bool bold_load(void) {
  used = base = 0;
  bold_ready = true;
  for (int i = 0; i < 95; i++) {
    bold[i].px = 0;
    for (int j = 0; j < i && !bold[i].px; j++)   /* (lower case: the same letters as the capitals) */
      if (fnf_bold[j].sprite == fnf_bold[i].sprite) bold[i] = bold[j];
    if (!bold[i].px && fnf_bold[i].sprite != FNF_NONE && !pic_new(&bold[i], fnf_bold[i].sprite)) bold_ready = false;
  }
  base = used;
  return bold_ready;
}

int bold_width(const char *s, int scale256) {
  int w = 0;
  for (; *s; s++) {
    unsigned c = (unsigned char)*s;
    if (c >= 32 && c < 127) w += fnf_bold[c - 32].advance;
  }
  return w * scale256 / 256;
}

void bold_text(int x, int y, const char *s, int alpha, int scale256) {
  for (; *s; s++) {
    unsigned c = (unsigned char)*s;
    if (c < 32 || c >= 127) continue;
    const pic_t *g = &bold[c - 32];
    if (g->px) {
      if (scale256 == 256) blit(g, x + g->x, y + g->y, alpha, false);
      else blit_scaled(g, x + g->x * scale256 / 256, y + g->y * scale256 / 256, g->w * scale256 / 256,
                       g->h * scale256 / 256, alpha);
    }
    x += fnf_bold[c - 32].advance * scale256 / 256;
  }
}

/* ------------------------------------------------------------------------------------------------ the song */
#define HUD_X(x) ((x) / 4)        /* the game's HUD, 1280 x 720 */
#define HUD_Y(y) ((y) / 3)
#define PX_PER_MS 0.15f           /* Constants.PIXELS_PER_MS (0.45) a third down */

static struct {
  pic_t actor[FNF_ACTORS];        /* each character's frame now */
  uint32_t actor_cap[FNF_ACTORS];
  pic_t front[3];
  pic_t note[4], strum[3][4], hold[4], end[4], rating[4], digit[10];
  pic_t icon[2][3];
  uint8_t icon_id[2];
  pic_t trans;                    /* the countdown's picture, or the four splashes */
  pic_t splash[4];
  uint8_t *trans_px;
  uint16_t splash_id[4];
  bool splashes;
  bool ok;
} S;

static const uint16_t *sprite_pal(unsigned id) { return fnf_palette[fnf_sprite[id].pal]; }

static bool style_pic(pic_t *p, const uint16_t *ids, int i) { return pic_new(p, ids[i]); }

#define STYLE(name) (g->style ? FNF_SPR_PIXEL_##name : FNF_SPR_FUNKIN_##name)

static bool load_icons(const fnf_game_t *g, int who) {
  S.icon_id[who] = g->icon[who];
  for (int f = 0; f < 3; f++) {
    unsigned id = fnf_icon[g->icon[who]][f];
    if (!S.icon[who][f].px) return false;
    uint32_t cap = FNF_ICON_MAX / 3;
    if (!pic_load(&S.icon[who][f], id, S.icon[who][f].px, cap)) return false;
  }
  return true;
}

void play_icons(const fnf_game_t *g) {
  for (int w = 0; w < 2; w++)
    if (S.icon_id[w] != g->icon[w]) load_icons(g, w);
}

bool play_load(const fnf_game_t *g) {
  const fnf_var_t *v = &fnf_var[g->var];
  const fnf_stage_t *st = &fnf_stage[v->stage];
  memset(&S, 0, sizeof S);
  ram_reset();
  bool ok = bg_load(st->bg);
  for (int i = 0; i < FNF_ACTORS; i++) {
    const fnf_actor_t *a = &g->actor[i];
    if (a->chr == 255) continue;
    S.actor_cap[i] = fnf_char[a->chr].frame_max;
    S.actor[i].px = ram_take(S.actor_cap[i]);
    S.actor[i].id = FNF_NONE;
    ok &= S.actor[i].px != 0;
  }
  for (int k = 0; k < 3; k++)
    if (st->front[k] != FNF_NONE) ok &= pic_new(&S.front[k], st->front[k]);
  static const uint16_t funkin[] = {
      FNF_SPR_FUNKIN_NOTE0, FNF_SPR_FUNKIN_NOTE1, FNF_SPR_FUNKIN_NOTE2, FNF_SPR_FUNKIN_NOTE3,
      FNF_SPR_FUNKIN_STATIC0, FNF_SPR_FUNKIN_STATIC1, FNF_SPR_FUNKIN_STATIC2, FNF_SPR_FUNKIN_STATIC3,
      FNF_SPR_FUNKIN_PRESS0, FNF_SPR_FUNKIN_PRESS1, FNF_SPR_FUNKIN_PRESS2, FNF_SPR_FUNKIN_PRESS3,
      FNF_SPR_FUNKIN_CONFIRM0, FNF_SPR_FUNKIN_CONFIRM1, FNF_SPR_FUNKIN_CONFIRM2, FNF_SPR_FUNKIN_CONFIRM3,
      FNF_SPR_FUNKIN_HOLD0, FNF_SPR_FUNKIN_HOLD1, FNF_SPR_FUNKIN_HOLD2, FNF_SPR_FUNKIN_HOLD3,
      FNF_SPR_FUNKIN_END0, FNF_SPR_FUNKIN_END1, FNF_SPR_FUNKIN_END2, FNF_SPR_FUNKIN_END3,
      FNF_SPR_FUNKIN_SICK, FNF_SPR_FUNKIN_GOOD, FNF_SPR_FUNKIN_BAD, FNF_SPR_FUNKIN_SHIT,
      FNF_SPR_FUNKIN_NUM0, FNF_SPR_FUNKIN_NUM1, FNF_SPR_FUNKIN_NUM2, FNF_SPR_FUNKIN_NUM3, FNF_SPR_FUNKIN_NUM4,
      FNF_SPR_FUNKIN_NUM5, FNF_SPR_FUNKIN_NUM6, FNF_SPR_FUNKIN_NUM7, FNF_SPR_FUNKIN_NUM8, FNF_SPR_FUNKIN_NUM9,
      FNF_SPR_FUNKIN_SPLASH0_0, FNF_SPR_FUNKIN_SPLASH1_0, FNF_SPR_FUNKIN_SPLASH2_0, FNF_SPR_FUNKIN_SPLASH3_0};
  static const uint16_t pixel[] = {
      FNF_SPR_PIXEL_NOTE0, FNF_SPR_PIXEL_NOTE1, FNF_SPR_PIXEL_NOTE2, FNF_SPR_PIXEL_NOTE3,
      FNF_SPR_PIXEL_STATIC0, FNF_SPR_PIXEL_STATIC1, FNF_SPR_PIXEL_STATIC2, FNF_SPR_PIXEL_STATIC3,
      FNF_SPR_PIXEL_PRESS0, FNF_SPR_PIXEL_PRESS1, FNF_SPR_PIXEL_PRESS2, FNF_SPR_PIXEL_PRESS3,
      FNF_SPR_PIXEL_CONFIRM0, FNF_SPR_PIXEL_CONFIRM1, FNF_SPR_PIXEL_CONFIRM2, FNF_SPR_PIXEL_CONFIRM3,
      FNF_SPR_PIXEL_HOLD0, FNF_SPR_PIXEL_HOLD1, FNF_SPR_PIXEL_HOLD2, FNF_SPR_PIXEL_HOLD3,
      FNF_SPR_PIXEL_END0, FNF_SPR_PIXEL_END1, FNF_SPR_PIXEL_END2, FNF_SPR_PIXEL_END3,
      FNF_SPR_PIXEL_SICK, FNF_SPR_PIXEL_GOOD, FNF_SPR_PIXEL_BAD, FNF_SPR_PIXEL_SHIT,
      FNF_SPR_PIXEL_NUM0, FNF_SPR_PIXEL_NUM1, FNF_SPR_PIXEL_NUM2, FNF_SPR_PIXEL_NUM3, FNF_SPR_PIXEL_NUM4,
      FNF_SPR_PIXEL_NUM5, FNF_SPR_PIXEL_NUM6, FNF_SPR_PIXEL_NUM7, FNF_SPR_PIXEL_NUM8, FNF_SPR_PIXEL_NUM9,
      FNF_SPR_PIXEL_SPLASH0_0, FNF_SPR_PIXEL_SPLASH1_0, FNF_SPR_PIXEL_SPLASH2_0, FNF_SPR_PIXEL_SPLASH3_0};
  const uint16_t *ids = g->style ? pixel : funkin;
  for (int d = 0; d < 4; d++) {
    ok &= style_pic(&S.note[d], ids, d);
    for (int s = 0; s < 3; s++) ok &= style_pic(&S.strum[s][d], ids, 4 + 4 * s + d);
    ok &= style_pic(&S.hold[d], ids, 16 + d);
    ok &= style_pic(&S.end[d], ids, 20 + d);
    ok &= style_pic(&S.rating[d], ids, 24 + d);
    S.splash_id[d] = ids[38 + d];
  }
  for (int n = 0; n < 10; n++) ok &= style_pic(&S.digit[n], ids, 28 + n);
  for (int w = 0; w < 2; w++)
    for (int f = 0; f < 3; f++) {
      S.icon[w][f].px = ram_take(FNF_ICON_MAX / 3);
      ok &= S.icon[w][f].px != 0;
    }
  ok &= load_icons(g, 0) && load_icons(g, 1);
  S.trans_px = ram_take(FNF_TRANSIENT_MAX);
  S.trans.id = FNF_NONE;
  ok &= S.trans_px != 0;
  S.ok = ok;
  (void)sprite_pal;
  return ok;
}

/* the countdown's picture and the splashes take turns in the same RAM */
static void trans_show(unsigned id) {
  if (S.trans.id == id && S.trans.px) return;
  S.splashes = false;
  if (!pic_load(&S.trans, id, S.trans_px, FNF_TRANSIENT_MAX)) S.trans.px = 0, S.trans.id = FNF_NONE;
}

static bool splashes_show(void) {
  if (S.splashes) return true;
  S.trans.id = FNF_NONE;
  uint32_t at = 0;
  for (int d = 0; d < 4; d++) {
    const fnf_sprite_t *s = &fnf_sprite[S.splash_id[d]];
    uint32_t n = (s->w + 1u) / 2 * s->h;
    if (!pic_load(&S.splash[d], S.splash_id[d], S.trans_px + at, FNF_TRANSIENT_MAX - at)) return false;
    at += n;
  }
  return S.splashes = true;
}

static float ease_cube_in_out(float x) { return x < 0.5f ? 4 * x * x * x : 1 - powf(-2 * x + 2, 3) / 2; }
static float ease_circ_out(float x) { return sqrtf(1 - (x - 1) * (x - 1)); }

/* a camera bop's leftover: lerp(1, m, 0.95^(dt x 60)) (PlayState.update) */
static float decay(int32_t dt) { return dt <= 0 ? 1.0f : powf(0.95f, dt * 60e-6f); }

static int hud_tx(int x, float z) { return (int)lroundf(160 + (x - 160) * z); }
static int hud_ty(int y, float z) { return (int)lroundf(120 + (y - 120) * z); }

static color_t RED, GREEN, BLACK;

typedef struct {
  const fnf_game_t *g;
  bool down;
  int32_t since;
  float cam, hud;
} scene_t;

static void strip_scene(const scene_t *sc) {
  const fnf_game_t *g = sc->g;
  const fnf_var_t *v = &fnf_var[g->var];
  const fnf_stage_t *st = &fnf_stage[v->stage];
  int32_t now = g->now;
  /* LE SSERAFIM's dimming */
  int dark = 0;
  if (g->dark_t1 > g->dark_t0 || g->dark_to) {
    float x = g->dark_t1 > g->dark_t0 ? (float)(now - g->dark_t0) / (float)(g->dark_t1 - g->dark_t0) : 1;
    x = x < 0 ? 0 : x > 1 ? 1 : x;
    x = -(cosf(3.14159265f * x) - 1) / 2;   /* sineInOut */
    dark = (int)(g->dark_from + (g->dark_to - g->dark_from) * x);
  }
  float z = sc->cam;
  draw_bg(bg.pal, dark, z);
  /* the characters, in the stage's order, and what stands in front of them */
  for (int k = 0; k < 3; k++) {
    int i = st->order[k];
    const fnf_actor_t *a = &g->actor[i];
    if (a->chr != 255 && a->visible && S.actor[i].px && S.actor[i].id != FNF_NONE) {
      int fx = hud_tx(a->x, z), fy = hud_ty(a->y, z);
      if (dark) blit_tinted(&S.actor[i], fx + S.actor[i].x, fy + S.actor[i].y, 0, dark * 32 / 1000);
      else blit(&S.actor[i], fx + S.actor[i].x, fy + S.actor[i].y, 32, false);
    }
    if (S.front[k].px) blit(&S.front[k], hud_tx(S.front[k].x, z), hud_ty(S.front[k].y, z), 32, false);
  }
  for (int i = 3; i < FNF_ACTORS; i++) {   /* LE SSERAFIM's other members, in front */
    const fnf_actor_t *a = &g->actor[i];
    if (a->chr == 255 || !a->visible || !S.actor[i].px || S.actor[i].id == FNF_NONE) continue;
    int fx = hud_tx(a->x, z), fy = hud_ty(a->y, z);
    if (dark) blit_tinted(&S.actor[i], fx + S.actor[i].x, fy + S.actor[i].y, 0, dark * 32 / 1000);
    else blit(&S.actor[i], fx + S.actor[i].x, fy + S.actor[i].y, 32, false);
  }
  /* ---- the HUD */
  float h = sc->hud;
  bool down = sc->down;
  int strum_y = down ? 219 : 21;
  float speed = g->speed / 1000.0f;
  bool solo = (v->flags & FNF_VF_SOLO) != 0;
  int px0 = solo ? 118 : 185;   /* your arrows' first (centred when alone) */
  /* the arrows fade in, one after the other, as the song comes up (Strumline.fadeInArrow) */
  for (int lane = 0; lane < 8; lane++) {
    int d = lane & 3;
    if (solo && lane >= 4) continue;
    int cx = lane < 4 ? px0 + 28 * d : 25 + 28 * d;
    int alpha = 32, dy = 0;
    float ft = (sc->since - 500000 - 200000 * d) / 1e6f;
    if (ft < 1) {
      float e = ft <= 0 ? 0 : ease_circ_out(ft);
      alpha = (int)(e * 32), dy = (int)lroundf((1 - e) * -3);
    }
    const pic_t *p = &S.strum[g->strum[lane]][d];
    blit_at(p, hud_tx(cx, h), hud_ty(strum_y + dy, h), alpha);
  }
  /* holds, then notes */
  for (int pass = 0; pass < 2; pass++)
    for (int i = 0; i < FNF_ACTIVE; i++) {
      const fnf_note_t *n = &g->note[i];
      if (n->state == NOTE_GONE && n->hold != HOLD_ON) continue;
      if (solo && n->lane >= 4) continue;
      int d = n->lane & 3;
      int cx = hud_tx(n->lane < 4 ? px0 + 28 * d : 25 + 28 * d, h);
      float dist = (n->t - now) / 1000.0f * PX_PER_MS * speed;
      int y = down ? strum_y - (int)lroundf(dist) : strum_y + (int)lroundf(dist);
      if (pass == 0) {
        if (n->len <= 0 || n->hold == HOLD_DONE || n->hold == HOLD_DROPPED) continue;
        float tail = (n->t + n->len - now) / 1000.0f * PX_PER_MS * speed;
        int ye = down ? strum_y - (int)lroundf(tail) : strum_y + (int)lroundf(tail);
        int ys = n->hold == HOLD_ON ? strum_y : y;   /* held: from the arrow on */
        if (n->hold == HOLD_ON && (down ? ye > ys : ye < ys)) continue;
        ys = hud_ty(ys, h), ye = hud_ty(ye, h);
        const pic_t *body = &S.hold[d], *end = &S.end[d];
        int top = down ? ye : ys, bottom = down ? ys : ye;
        int x0 = cx + body->x;
        /* the body down to the end's start, then the end */
        int body_top = down ? top + end->h : top, body_bottom = down ? bottom : bottom - end->h;
        for (int r = body_top < gfx_y0 ? gfx_y0 : body_top; r < body_bottom && r < gfx_y1; r++) {
          pic_t row = *body;
          row.h = 1;
          blit(&row, x0, r, 32, false);
        }
        if (down) blit(end, cx + end->x, top, 32, true);
        else blit(end, cx + end->x, bottom - end->h, 32, false);
      } else if (n->state != NOTE_GONE) {
        if (y < -20 || y > 260) continue;
        blit_at(&S.note[d], cx, hud_ty(y, h), n->state == NOTE_FADED ? 16 : 32);
      }
    }
  /* splashes on Sick hits (167 ms, at 80%) */
  int32_t cd_age;
  bool counting = fnf_countdown(g, &cd_age) != 0;
  for (int d = 0; d < 4 && !counting; d++) {
    if (!g->splash_at[d] || now - g->splash_at[d] > 167000 || now < g->splash_at[d]) continue;
    if (splashes_show()) blit_at(&S.splash[d], hud_tx(px0 + 28 * d, h), hud_ty(strum_y, h), 26);
  }
  /* the health bar: red for the opponent on the left, green for you on the right */
  int bar_y = down ? 24 : 216;
  int bx = hud_tx(85, h), bw = (int)lroundf(150 * h), by = hud_ty(bar_y - 3, h), bh = 6;
  gfx_fill(bx - 1, by - 1, bw + 2, bh + 2, BLACK);
  int split = bw - (int)((int64_t)g->health_shown * bw / FNF_HEALTH_MAX);
  gfx_fill(bx, by, split, bh, RED);
  gfx_fill(bx + split, by, bw - split, bh, GREEN);
  /* the icons, bopping on the beat: 1.2 times wider, back over min(a step x 0.002 s, 0.175 s) */
  int32_t bop_len = fnf_step_len(g, now) * 2 / 1000;
  if (bop_len > 175000) bop_len = 175000;
  float grow = 1;
  if (g->icon_at && now >= g->icon_at && now - g->icon_at < bop_len) grow = 1.2f - 0.2f * (now - g->icon_at) / bop_len;
  for (int w = 0; w < 2; w++) {
    if (w == 1 && g->icon_hidden) continue;
    int32_t hp = w == 0 ? g->health_shown : FNF_HEALTH_MAX - g->health_shown;
    int frame = hp < 400000 ? 1 : hp > 1600000 && S.icon[w][2].id != S.icon[w][0].id ? 2 : 0;
    const pic_t *ic = &S.icon[w][frame];
    int size = (int)lroundf(37.5f * grow);
    int cx = bx + split;
    /* (iconP1 from 26 left of the split, iconP2 ending 26 right of it, both on the bar's middle) */
    int x = w == 0 ? cx - 6 : cx + 6 - size;
    int y = by + bh / 2 - size / 2;
    int sx = ic->w * size / 38, sy = ic->h * size / 38;
    /* (mirrored: the picture's place in its square mirrors too) */
    int left = g->icon_flip[w] ? 38 - (ic->x + 19) - ic->w : ic->x + 19;
    blit_scaled_flip(ic, x + left * size / 38, y + (ic->y + 19) * size / 38, sx, sy, 32, g->icon_flip[w]);
  }
  /* Score: 12,345 */
  char buf[32], *p = buf + sizeof buf - 1;
  int32_t sc_ = fnf_score(g);
  bool neg = sc_ < 0;
  uint32_t u = (uint32_t)(neg ? -sc_ : sc_);
  *p = 0;
  int digits = 0;
  do {
    if (digits && digits % 3 == 0) *--p = ',';
    *--p = (char)('0' + u % 10), u /= 10, digits++;
  } while (u);
  if (neg) *--p = '-';
  char line[48] = "Score: ";
  strcat(line, p);
  int tx = hud_tx(85 + 150 - 48, h), ty = hud_ty(bar_y + 11, h);
  for (int oy = -1; oy <= 1; oy++)
    for (int ox = -1; ox <= 1; ox++)
      if (ox || oy) gfx_text(&np_font_small, tx + ox, ty + oy, line, BLACK, 256);
  gfx_text(&np_font_small, tx, ty, line, 0xFFFF, 256);
  /* the ratings and combo popping up */
  for (int i = 0; i < FNF_POPS; i++) {
    const fnf_pop_t *pp = &g->pop[i];
    if (pp->what == 255 || !pp->at || now < pp->at || now > pp->fade + 200000) continue;
    float t = (now - pp->at) / 1e6f;
    int x = (int)lroundf(pp->x + pp->vx * t), y = (int)lroundf(pp->y + pp->vy * t + pp->ay * t * t / 2);
    int alpha = now < pp->fade ? 32 : (int)(32 - 32 * (now - pp->fade) / 200000);
    if (g->style && alpha < 32) alpha = alpha >= 16 ? 32 : 0;   /* (pixel: stepped(2)) */
    const pic_t *pic = pp->what < 4 ? &S.rating[pp->what] : &S.digit[pp->what - 10];
    blit_at(pic, hud_tx(x, h), hud_ty(y, h), alpha);
  }
  /* the countdown: Ready, Set, Go, each fading over its beat */
  int32_t age;
  int cd = fnf_countdown(g, &age);
  if (cd) {
    static const uint16_t cd_spr[2][3] = {{FNF_SPR_FUNKIN_COUNTTWO, FNF_SPR_FUNKIN_COUNTONE, FNF_SPR_FUNKIN_COUNTGO},
                                          {FNF_SPR_PIXEL_COUNTTWO, FNF_SPR_PIXEL_COUNTONE, FNF_SPR_PIXEL_COUNTGO}};
    trans_show(cd_spr[g->style != 0][cd - 1]);
    float x = age / (float)(fnf_step_len(g, now) * 4);
    x = x < 0 ? 0 : x > 1 ? 1 : x;
    float a = g->style ? 1 - floorf(x * 8) / 8 : 1 - ease_cube_in_out(x);
    if (S.trans.px) blit_at(&S.trans, hud_tx(160, h), hud_ty(120, h), (int)(a * 32));
  }
}

static scene_t scene;

static void actor_frames(const fnf_game_t *g) {
  for (int i = 0; i < FNF_ACTORS; i++) {
    if (g->actor[i].chr == 255 || !S.actor[i].px) continue;
    unsigned id = fnf_actor_sprite(g, i);
    if (id == FNF_NONE || id == S.actor[i].id) continue;
    uint8_t *px = S.actor[i].px;
    if (!pic_load(&S.actor[i], id, px, S.actor_cap[i])) S.actor[i].px = px, S.actor[i].id = FNF_NONE;
  }
}

void (*draw_between)(void);   /* (main.c: the keyboard, read between strips) */

void draw_frame(void (*strip_fn)(void *), void *ctx) {
  gfx_buf = strip;
  for (int y = 0; y < SCREEN_H; y += STRIP_H) {
    gfx_y0 = y, gfx_y1 = y + STRIP_H;
    strip_fn(ctx);
    push_strip();
    if (draw_between) draw_between();
  }
}

void play_prepare(const fnf_game_t *g, bool downscroll, int32_t since_start) {
  RED = HEX(0xFF0000), GREEN = HEX(0x66FF33), BLACK = 0;
  play_icons(g);
  actor_frames(g);
  scene.g = g, scene.down = downscroll, scene.since = since_start;
  float cd = decay(g->now - g->bop_at);
  scene.cam = g->bop_at ? 1 + g->bop_cam / 1e6f * cd : 1;
  scene.hud = g->bop_at ? 1 + g->bop_hud / 1e6f * cd : 1;
}

void play_strip(void) { strip_scene(&scene); }

/* one character alone, on black (the game over) */
void play_actor_strip(const fnf_game_t *g, int i) {
  gfx_fill(0, gfx_y0, SCREEN_W, gfx_y1 - gfx_y0, 0);
  const fnf_actor_t *a = &g->actor[i];
  if (a->chr != 255 && S.actor[i].px && S.actor[i].id != FNF_NONE)
    blit(&S.actor[i], a->x + S.actor[i].x, a->y + S.actor[i].y, 32, false);
}
