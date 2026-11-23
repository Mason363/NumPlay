/* The home screen: a carousel of game cards. The selected card is large,
 * its neighbours peek in from the sides, and the card cycles through the
 * game's screenshots. The last card opens the settings. */
#include "ui.h"
#include "live.h"
#include "../../games/common/np_text.h"

#define CARD_W 200
#define CARD_H 152
#define CARD_CX 160
#define CARD_CY 103
#define SPACING 184
#define FRAME 4
#define SHOT_MS 2600
#define SLIDE_MS 520

#define SETTINGS_TOP 0x4A5163
#define SETTINGS_BOTTOM 0x1C1F27
#define SETTINGS_ACCENT 0x6C7A96

typedef struct {
  int n;
  uint8_t theme;
  int8_t item[NP_MAX_GAMES + 1]; /* game index, or -1 for the settings card */
  int sel;
  float pos;            /* scroll position, in cards */
  int shot;             /* screenshot shown on the selected card */
  float slide;          /* 0..1 while sliding to the next screenshot */
  uint32_t shot_time;   /* ms spent on the current screenshot */
  float badge;          /* play badge, 0..1 */
  float zoom;           /* launch animation, 0..1 */
  float intro;
} home_t;

static bool first_open = true;

static uint32_t item_color(int game, int which) {
  if (game < 0) return which == 0 ? SETTINGS_TOP : which == 1 ? SETTINGS_BOTTOM : SETTINGS_ACCENT;
  const np_game_t *g = &np_games[game];
  return which == 0 ? g->top : which == 1 ? g->bottom : g->accent;
}

static const char *item_title(int game) { return game < 0 ? T("Settings") : np_games[game].title; }
/* under the name, the tagline; or, when the calculator's software gives apps too little RAM for the
 * game, how much it lacks */
static const char *item_tagline(int game) {
  if (game < 0) return T("Reset, uninstall, more");
  uint32_t missing = np_game_ram_missing(game);
  if (!missing) return np_games[game].tagline;
  static char line[96];
  char digits[8];
  int n = 0;
  for (uint32_t kb = (missing + 1023) / 1024; kb || !n; kb /= 10) digits[n++] = (char)('0' + kb % 10);
  char *o = line;
  for (const char *s = T("Needs "); *s;) *o++ = *s++;
  while (n) *o++ = digits[--n];
  for (const char *s = T(" KB more memory"); *s;) *o++ = *s++;
  *o = 0;
  return line;
}

static float absf(float v) { return v < 0 ? -v : v; }

/* ---- drawing */
static void draw_settings_art(int x, int y, int w, int h, int dim) {
  uint32_t top = gfx_lerp888(0x59627A, 0, dim), bottom = gfx_lerp888(0x2A2F3B, 0, dim);
  gfx_vgrad(x, y, w, h, top, bottom);
  const np_icon_t *gear = &np_icon_gear;
  if (w > gear->w + 8) gfx_icon(gear, x + (w - gear->w) / 2, y + (h - gear->h) / 2 - 2, 0xFFFF, 200 - dim / 2);
}

static void draw_card(home_t *h, int k, float d) {
  int game = h->item[k];
  float ad = absf(d);
  float s = 1 - 0.26f * NP_MIN(ad, 1.5f);
  float z = h->zoom;
  float cx = CARD_CX + d * SPACING, cy = CARD_CY;
  float w = CARD_W * s, hh = CARD_H * s;
  float frame = FRAME * s, radius = 12 * s;
  if (h->theme == NP_THEME_SWITCH) radius = 15 * s;
  if (h->theme == NP_THEME_PLAYSTATION) radius = 4 * s, frame = 2 * s;
  int dim = (int)(130 * NP_MIN(ad, 1.f));
  if (k == h->sel && z > 0) {  /* launching: grow to the whole screen */
    float e = ui_ease_in_out(z);
    cx += (160 - cx) * e;
    cy += (120 - cy) * e;
    w += (SCREEN_W + 2 * frame - w) * e;
    hh += (SCREEN_H + 2 * frame - hh) * e;
    radius *= 1 - e;
  }
  int x = (int)(cx - w / 2), y = (int)(cy - hh / 2), iw = (int)w, ih = (int)hh;
  if (x >= SCREEN_W || x + iw <= 0) return;
  if (ad < 0.6f && z == 0) gfx_shadow(x, y + 5, iw, ih, (int)radius, 12, 120);
  color_t frame_color = h->theme == NP_THEME_PLAYSTATION ? gfx_rgb(0x8DB8FF) : gfx_mix(0xFFFF, 0, dim >> 3);
  gfx_rrect(x, y, iw, ih, (int)radius, frame_color, 256);
  int fx = (int)frame, ir = NP_MAX((int)(radius - frame + 1), 0);
  int ix = x + fx, iy = y + fx, iiw = iw - 2 * fx, iih = ih - 2 * fx;
  if (game < 0) {
    draw_settings_art(ix, iy, iiw, iih, dim);
  } else {
    const np_game_t *g = &np_games[game];
    int shot = k == h->sel ? h->shot : 0;
    gfx_clip_x0 = NP_MAX(ix, 0);
    gfx_clip_x1 = NP_MIN(ix + iiw, SCREEN_W);
    int off = k == h->sel ? (int)(ui_ease_in_out(h->slide) * iiw) : 0;
    const uint8_t *px = ui_shot(game, shot);
    const np_shot_t *sh = &g->shots[shot];
    if (px) gfx_image(px, sh->palette, sh->ncolors, SHOT_W, SHOT_H, ix - off, iy, iiw, iih, 0, dim);
    else gfx_fill(ix, iy, iiw, iih, gfx_rgb(gfx_lerp888(g->bottom, 0, dim)));
    if (off) {
      int next = (shot + 1) % g->nshots;
      const uint8_t *px2 = ui_shot(game, next);
      const np_shot_t *sh2 = &g->shots[next];
      if (px2) gfx_image(px2, sh2->palette, sh2->ncolors, SHOT_W, SHOT_H, ix - off + iiw, iy, iiw, iih, 0, dim);
    }
    gfx_clip_x0 = 0;
    gfx_clip_x1 = SCREEN_W;
  }
  gfx_corners(ix, iy, iiw, iih, ir, frame_color);
  /* play badge on the selected game (not on one there isn't the RAM for) */
  if (k == h->sel && game >= 0 && h->badge > 0 && z == 0 && !np_game_ram_missing(game)) {
    float b = ui_ease_out(h->badge);
    int r = (int)(15 * b);
    int bx = x + iw - 14, by = y + ih - 14;
    gfx_circle(bx, by + 2, r + 2, 0, 60);
    gfx_circle(bx, by, r + 2, 0xFFFF, 256);
    gfx_circle(bx, by, r, gfx_rgb(item_color(game, 2)), 256);
    if (b > 0.8f) gfx_icon(&np_icon_play, bx - 4, by - 6, 0xFFFF, (int)(256 * (b - 0.8f) * 5));
  }
}

static void scene_grid(home_t *h) {
  uint32_t top = 0x101820, bottom = 0x05090E, accent = 0x107C10;
  uint32_t ink = 0xFFFF;
  if (h->theme == NP_THEME_WII) top = 0x245A78, bottom = 0x102B40, accent = 0x55C9EE;
  if (h->theme == NP_THEME_WII_U) top = 0x174C7B, bottom = 0x071A31, accent = 0x00A8E0;
  gfx_vgrad(0, 0, SCREEN_W, SCREEN_H, top, bottom);
  const char *heading = h->theme == NP_THEME_XBOX ? "Home" : h->theme == NP_THEME_WII ? "Channels" : "Menu";
  gfx_text(&np_font_title, 12, 24, heading, ink, 256);
  gfx_fill(0, 31, SCREEN_W, 2, gfx_rgb(accent));
  int page = h->sel / 8, first = page * 8;
  for (int slot = 0; slot < 8; slot++) {
    int k = first + slot;
    if (k >= h->n) break;
    int game = h->item[k], col = slot % 4, row = slot / 4;
    int x = 8 + col * 78, y = 43 + row * 86;
    int w = h->theme == NP_THEME_XBOX ? 70 : 62;
    int hh = h->theme == NP_THEME_XBOX ? 48 : 54;
    bool selected = k == h->sel;
    float z = selected ? ui_ease_in_out(h->zoom) : 0;
    float pop = selected && z == 0 ? 1.f + 0.045f * h->badge : 1.f;
    float cx = x + w / 2.f, cy = y + hh / 2.f;
    float ww = w * pop, hhh = hh * pop;
    if (z > 0) {
      cx += (160 - cx) * z;
      cy += (120 - cy) * z;
      ww += (SCREEN_W + 8 - ww) * z;
      hhh += (SCREEN_H + 8 - hhh) * z;
    }
    int tx = (int)(cx - ww / 2), ty = (int)(cy - hhh / 2);
    int tw = (int)ww, th = (int)hhh;
    int radius = h->theme == NP_THEME_WII ? 9 : h->theme == NP_THEME_WII_U ? 4 : 2;
    if (selected) gfx_rrect(tx - 3, ty - 3, tw + 6, th + 6, radius + 2, gfx_rgb(accent), 256);
    else gfx_rrect(tx - 1, ty - 1, tw + 2, th + 2, radius + 1, 0xFFFF, 70);
    if (game >= 0) {
      const np_game_t *g = &np_games[game];
      const uint8_t *px = ui_shot(game, 0);
      if (px) gfx_image(px, g->shots[0].palette, g->shots[0].ncolors, SHOT_W, SHOT_H,
                        tx, ty, tw, th, radius, selected ? 0 : 35);
      else gfx_fill(tx, ty, tw, th, gfx_rgb(g->bottom));
    } else {
      draw_settings_art(tx, ty, tw, th, 0);
    }
    if (z < 0.55f) {
      gfx_text_center(&np_font_small, x + w / 2, y + hh + 13, item_title(game), ink,
                      selected ? 256 : 190);
    }
  }
  if (h->n > 8) {
    char page_text[16];
    int n = 0, p = page + 1, total = (h->n + 7) / 8;
    page_text[n++] = (char)('0' + p / 10);
    if (page_text[0] == '0') n = 0;
    page_text[n++] = (char)('0' + p % 10);
    page_text[n++] = '/';
    page_text[n++] = (char)('0' + total / 10);
    if (page_text[n - 1] == '0') n--;
    page_text[n++] = (char)('0' + total % 10);
    page_text[n] = 0;
    gfx_text_right(&np_font_small, SCREEN_W - 10, 24, page_text, ink, 180);
  }
}

static void scene(void *ctx) {
  home_t *h = ctx;
  if (h->theme >= NP_THEME_XBOX) { scene_grid(h); return; }
  /* background: the theme of the card in front, blended while scrolling */
  int a = (int)h->pos;
  if (a < 0) a = 0;
  if (a > h->n - 1) a = h->n - 1;
  int b = NP_MIN(a + 1, h->n - 1);
  int t = (int)((h->pos - a) * 256);
  t = NP_CLAMP(t, 0, 256);
  uint32_t top = gfx_lerp888(item_color(h->item[a], 0), item_color(h->item[b], 0), t);
  uint32_t bottom = gfx_lerp888(item_color(h->item[a], 1), item_color(h->item[b], 1), t);
  if (h->theme == NP_THEME_SWITCH) {
    gfx_vgrad(0, 0, SCREEN_W, SCREEN_H, 0x242424, 0x080808);
    gfx_fill(0, 29, SCREEN_W / 2, 2, gfx_rgb(0xE60012));
    gfx_fill(SCREEN_W / 2, 29, SCREEN_W / 2, 2, gfx_rgb(0x00A7E1));
  } else if (h->theme == NP_THEME_PLAYSTATION) {
    gfx_vgrad(0, 0, SCREEN_W, SCREEN_H, 0x24365F, 0x080E1D);
    gfx_fill(0, 29, SCREEN_W, 2, gfx_rgb(0x4B8DFF));
  } else if (live_active()) { /* the NumPlay live background */
    live_strip();
  } else {
    gfx_vgrad(0, 0, SCREEN_W, SCREEN_H, top, bottom);
    for (int y = (gfx_y0 + 15) / 16 * 16 + 8 - 16; y < gfx_y1; y += 16) {
      if (y < gfx_y0) continue;
      for (int x = 8; x < SCREEN_W; x += 16) gfx_fill_alpha(x, y, 2, 1, 0xFFFF, 22);
    }
  }

  float z = ui_ease_in_out(h->zoom);
  int ui_alpha = (int)(256 * (1 - z));
  /* header */
  if (gfx_y0 < 26 && ui_alpha > 0) {
    if (h->theme == NP_THEME_NUMPLAY) gfx_icon(&np_icon_logo, 12, 6, 0xFFFF, ui_alpha);
    const char *label = h->theme == NP_THEME_SWITCH ? "HOME" : h->theme == NP_THEME_PLAYSTATION ? "Games" : "NumPlay";
    gfx_text(&np_font_body, h->theme == NP_THEME_NUMPLAY ? 40 : 12, 18, label, 0xFFFF, ui_alpha);
    /* where we are in the carousel; with many cards the dots get closer */
    int pitch = NP_MIN(10, 190 / (h->n + 1));
    int dx = SCREEN_W - 10 - (h->n + 1) * pitch;
    for (int i = 0; i < h->n; i++) {
      float near = 1 - NP_MIN(absf(i - h->pos), 1.f);
      int w = pitch - 4 + (int)(pitch * near);
      gfx_rrect(dx, 11, w, 6, NP_MIN(3, w / 2), 0xFFFF, (int)((90 + 166 * near) * ui_alpha / 256));
      dx += w + 4;
    }
  }
  /* cards, far ones first */
  int lo = NP_MAX((int)h->pos - 2, 0), hi = NP_MIN((int)h->pos + 2, h->n - 1);
  for (int pass = 3; pass >= 0; pass--)
    for (int k = lo; k <= hi; k++) {
      float d = k - h->pos;
      int ring = absf(d) < 0.5f ? 0 : absf(d) < 1.5f ? 1 : absf(d) < 2.5f ? 2 : 3;
      if (ring == pass && (k == h->sel || z < 1)) draw_card(h, k, d);
    }
  if (ui_alpha <= 0) return;
  /* screenshot progress under the card */
  int front = (int)(h->pos + 0.5f);
  front = NP_CLAMP(front, 0, h->n - 1);
  float away = absf(h->pos - front);
  int text_alpha = (int)(ui_alpha * NP_CLAMP(1 - away * 2.2f, 0.f, 1.f));
  int game = h->item[front];
  if (gfx_y1 > 180 && text_alpha > 0) {
    if (game >= 0 && np_games[game].nshots > 1) {
      int n = np_games[game].nshots, seg = 22, gap = 5;
      int x0 = CARD_CX - (n * seg + (n - 1) * gap) / 2;
      float progress = front == h->sel ? (h->slide > 0 ? 1.f : NP_MIN(h->shot_time / (float)SHOT_MS, 1.f)) : 0;
      for (int i = 0; i < n; i++) {
        int x = x0 + i * (seg + gap);
        gfx_rrect(x, 185, seg, 4, 2, 0xFFFF, text_alpha * 70 / 256);
        int fill = i < h->shot ? seg : i == h->shot ? (int)(seg * progress) : 0;
        if (fill >= 4) gfx_rrect(x, 185, fill, 4, 2, 0xFFFF, text_alpha * 230 / 256);
      }
    }
    /* who made the game, when it isn't ours: under the card */
    if (game >= 0 && np_games[game].credit && np_games[game].nshots <= 1)
      gfx_text_center(&np_font_small, CARD_CX, 193, np_games[game].credit, 0xFFFF, text_alpha * 150 / 256);
    gfx_text_center(&np_font_title, CARD_CX, 213, item_title(game), 0xFFFF, text_alpha);
    gfx_text_center(&np_font_body, CARD_CX, 231, item_tagline(game), 0xFFFF, text_alpha * 190 / 256);
  }
}

/* ---- behaviour */
static int build_items(home_t *h) {
  h->n = 0;
  for (int i = 0; i < np_game_count && h->n < NP_MAX_GAMES; i++)
    if (np_game_installed(i)) h->item[h->n++] = (int8_t)i;
  h->item[h->n++] = -1;
  return h->n;
}

static bool bg_loaded;

int np_home(int *selected, bool returning) {
  ui_init();
  if (!bg_loaded) { /* once per run: the background chosen last time */
    bg_loaded = true;
    int m;
    if (np_bg_load(&m)) live_set(m);
  }
  home_t h = {0};
  np_config_t cfg;
  np_config_load(&cfg);
  h.theme = cfg.theme < NP_THEME_COUNT ? cfg.theme : NP_THEME_NUMPLAY;
  build_items(&h);
  /* the game played last, or else the first game there is: never Settings,
   * unless it was just open or no game is left */
  h.sel = 0;
  for (int k = 0; k < h.n; k++)
    if (h.item[k] == *selected) h.sel = k;
  if (*selected == -1) h.sel = h.n - 1;
  if (h.n == 1) h.sel = 0;
  h.pos = h.sel;
  if (first_open) {
    h.pos = h.sel + 1.2f;
    first_open = false;
  }
  bool unzooming = returning && h.item[h.sel] == *selected && *selected >= 0;
  if (unzooming) h.zoom = 1;
  ui_keys_t keys = {np_keys(), 0, 0};  /* ignore keys still held from before */
  uint32_t last = np_millis();
  bool dirty = true, bar_dirty = false;
  int result = -3;
  for (;;) {
    uint32_t now = np_millis();
    float dt = NP_MIN((now - last) / 1000.f, 0.05f);
    uint32_t elapsed = now - last;
    last = now;
    uint32_t pressed = ui_poll(&keys);
    if (result == -3) {
      int before = h.sel;
      if (pressed & K_LEFT) h.sel = NP_MAX(h.sel - 1, 0);
      if (pressed & K_RIGHT) h.sel = NP_MIN(h.sel + 1, h.n - 1);
      if (h.sel != before) {
        h.shot = 0;
        h.slide = 0;
        h.shot_time = 0;
        h.badge = 0;
      }
      if ((pressed & K_OK) && (h.item[h.sel] < 0 || !np_game_ram_missing(h.item[h.sel]))) result = h.item[h.sel];
      if (pressed & K_BACKSPACE) { /* the next background, then none */
        live_next();
        np_bg_save(live_mode());
        dirty = true;
      }
      if (pressed & (K_HOME | K_BACK)) return -2;
    }
    /* motion */
    float pos = ui_approach(h.pos, (float)h.sel, dt, 0.075f);
    bool moving = pos != h.pos;
    if (moving) dirty = true;
    h.pos = pos;
    int game = h.item[h.sel];
    bool settled = !moving;
    if (settled && game >= 0 && result == -3) {
      if (h.badge < 1) {
        h.badge = NP_MIN(h.badge + dt / 0.22f, 1.f);
        dirty = true;
      }
      if (np_games[game].nshots > 1) {
        if (h.slide > 0) {
          h.slide += elapsed / (float)SLIDE_MS;
          if (h.slide >= 1) {
            h.slide = 0;
            h.shot = (h.shot + 1) % np_games[game].nshots;
            h.shot_time = 0;
          }
          dirty = true;
        } else {
          uint32_t before = h.shot_time * 22 / SHOT_MS;
          h.shot_time += elapsed;
          if (h.shot_time >= SHOT_MS) h.slide = 0.0001f, dirty = true;
          if (h.shot_time * 22 / SHOT_MS != before) bar_dirty = true;  /* only the progress bar moves */
        }
      }
    }
    if (unzooming) {
      h.zoom = NP_MAX(h.zoom - dt / 0.26f, 0.f);
      unzooming = h.zoom > 0;
      dirty = true;
    }
    if (result != -3) {
      if (result == -1 || !settled) {
        if (result == -1) {
          *selected = -1;
          return -1;
        }
      } else {
        h.zoom = NP_MIN(h.zoom + dt / 0.24f, 1.f);
        dirty = true;
        if (h.zoom >= 1) {
          ui_frame(scene, &h);
          *selected = result;
          return result;
        }
      }
    }
    if (live_active()) dirty = true; /* the background moves: every frame */
    if (dirty) {
      live_frame(now);
      ui_frame(scene, &h);
      dirty = bar_dirty = false;
    } else if (bar_dirty) {
      gfx_render(scene, &h, 176, 192);  /* the strip holding the progress bar */
      bar_dirty = false;
    } else {
      np_sleep(16);
    }
  }
}
