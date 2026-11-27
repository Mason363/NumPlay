/* The home screen: a carousel of game cards. The selected card is large,
 * its neighbours peek in from the sides, and the card cycles through the
 * game's screenshots. The last card opens the settings.
 * TwiliPlay look: the colours come from the current theme (theme.c), Up and
 * Down change it. */
#include "ui.h"
#include "live.h"
#include "theme.h"
#include "../../games/common/np_text.h"

#define CARD_W 176
#define CARD_H 128
#define CARD_CX 104
#define CARD_CY 109
#define SPACING 340
#define FRAME 4
#define SHOT_MS 2600
#define SLIDE_MS 520
#define TOAST_MS 1400
#define STAR_COUNT 24

#define SETTINGS_TOP 0x4A5163
#define SETTINGS_BOTTOM 0x1C1F27

typedef struct {
  int n;
  int8_t item[NP_MAX_GAMES + 1]; /* game index, or -1 for the settings card */
  int sel;
  bool library;
  bool motion;
  float pos;            /* scroll position, in cards */
  int shot;             /* screenshot shown on the selected card */
  float slide;          /* 0..1 while sliding to the next screenshot */
  uint32_t shot_time;   /* ms spent on the current screenshot */
  float badge;          /* play badge, 0..1 */
  float zoom;           /* launch animation, 0..1 */
  float intro;
  uint16_t toast;       /* ms left showing the theme's name */
} home_t;

static bool first_open = true;

static const char *item_title(int game) { return game < 0 ? T("Settings") : np_games[game].title; }
/* under the name, the tagline; or, when the calculator's software gives apps too little RAM for the
 * game, how much it lacks */
static const char *item_tagline(int game, char line[96]) {
  if (game < 0) return T("Reset, uninstall, more");
  uint32_t missing = np_game_ram_missing(game);
  if (!missing) return np_games[game].tagline;
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

/* TwiliPlay look: procedural stars and logo, no bitmaps */
static void draw_stars(const np_theme_t *th) {
  color_t ca = gfx_rgb(th->star_a), cb = gfx_rgb(th->star_b);
  for (uint32_t i = 0; i < STAR_COUNT; i++) {
    uint32_t h1 = (i + 1) * 2654435761u, h2 = (i + 17) * 2246822519u;
    int x = (int)((h1 >> 16) % SCREEN_W), y = (int)((h2 >> 16) % SCREEN_H);
    if (y + 2 < gfx_y0 || y - 1 >= gfx_y1) continue;
    int a = 50 + (int)((h1 >> 8) & 63) * 2;
    color_t c = (i & 1) ? ca : cb;
    if (i % 6 == 0) {
      gfx_fill_alpha(x - 1, y, 3, 1, c, a);
      gfx_fill_alpha(x, y - 1, 1, 3, c, a);
    } else {
      int s = 1 + (int)(i & 1);
      gfx_fill_alpha(x, y, s, s, c, a);
    }
  }
}
static void draw_star(int cx, int cy, int r, color_t c, int a) {
  for (int dy = -r; dy <= r; dy++) {
    int t = r - (dy < 0 ? -dy : dy), hw = (t * t + r / 2) / r;
    gfx_fill_alpha(cx - hw, cy + dy, 2 * hw + 1, 1, c, a);
  }
}

static void draw_card(home_t *h, int k, float d) {
  int game = h->item[k];
  float ad = absf(d);
  float s = 1 - 0.26f * NP_MIN(ad, 1.5f);
  float z = h->zoom;
  float cx = CARD_CX + d * SPACING, cy = CARD_CY;
  float w = CARD_W * s, hh = CARD_H * s;
  float frame = FRAME * s, radius = 12 * s;
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
  const np_theme_t *th = np_theme();
  color_t frame_color = gfx_mix(gfx_rgb(ad < 0.5f ? th->accent : th->panel), gfx_rgb(th->bg), dim >> 3);
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
    gfx_circle(bx, by, r + 2, gfx_rgb(th->text), 256);
    gfx_circle(bx, by, r, gfx_rgb(th->accent), 256);
    if (b > 0.8f) gfx_icon(&np_icon_play, bx - 4, by - 6, gfx_rgb(th->bg), (int)(256 * (b - 0.8f) * 5));
  }
}

static void scene(void *ctx) {
  home_t *h = ctx;
  const np_theme_t *th = np_theme();
  bool live = h->motion && live_active();
  color_t tx = live ? 0xFFFF : gfx_rgb(th->text);
  if (live) {
    live_strip();
  } else {
    gfx_vgrad(0, 0, SCREEN_W, SCREEN_H, th->bg, th->bg2);
    draw_stars(th);
  }
  float z = ui_ease_in_out(h->zoom);
  int ui_alpha = (int)(256 * (1 - z));
  if (gfx_y0 < 28 && ui_alpha > 0) {
    draw_star(21, 14, 9, gfx_rgb(th->accent), ui_alpha);
    draw_star(32, 6, 3, gfx_rgb(th->star_b), ui_alpha);
    gfx_text(&np_font_body, 38, 14, "TwiliPlay", tx, ui_alpha);
    gfx_text(&np_font_small, 39, 25, "STARLIGHT EDITION", tx, ui_alpha * 170 / 256);
    gfx_rrect(230, 5, 80, 24, 7, gfx_rgb(th->panel), ui_alpha);
    gfx_text_center(&np_font_small, 270, 20, h->toast ? th->name : h->library ? "LIBRARY" : "HOME", tx, ui_alpha);
  }
  if (h->library) {
    int count = h->n - 1;
    int top = NP_CLAMP(h->sel - 3, 0, NP_MAX(0, count - 7));
    color_t panel = gfx_rgb(th->panel), accent = gfx_rgb(th->accent);
    gfx_text(&np_font_small, 12, 43, "GAME LIBRARY", tx, 256);
    if (!count) gfx_text_center(&np_font_body, 160, 120, "No games installed", tx, 256);
    for (int row = 0; row < 7 && top + row < count; row++) {
      int k = top + row, y = 49 + row * 24;
      bool chosen = k == h->sel;
      gfx_rrect(10, y, 290, 21, 5, chosen ? accent : panel, 256);
      gfx_text(&np_font_body, 20, y + 15, item_title(h->item[k]), chosen ? gfx_rgb(th->bg) : tx, 256);
      if (chosen) gfx_text_right(&np_font_small, 290, y + 15, "OK", gfx_rgb(th->bg), 256);
    }
    if (count > 7) {
      gfx_rrect(305, 49, 5, 165, 2, panel, 256);
      int thumb = 165 * 7 / count;
      gfx_rrect(305, 49 + (165 - thumb) * top / (count - 7), 5, thumb, 2, accent, 256);
    }
    gfx_rrect(10, 221, 300, 17, 5, panel, 256);
    gfx_text_center(&np_font_small, 160, 233, "Up/Down: Browse   Back: Home", tx, 256);
    return;
  }
  int lo = NP_MAX((int)h->pos - 2, 0), hi = NP_MIN((int)h->pos + 2, h->n - 1);
  for (int pass = 3; pass >= 0; pass--)
    for (int k = lo; k <= hi; k++) {
      float d = k - h->pos;
      int ring = absf(d) < 0.5f ? 0 : absf(d) < 1.5f ? 1 : absf(d) < 2.5f ? 2 : 3;
      if (ring == pass && (k == h->sel || z < 1)) draw_card(h, k, d);
    }
  if (ui_alpha <= 0) return;
  int front = NP_CLAMP((int)(h->pos + 0.5f), 0, h->n - 1);
  float away = absf(h->pos - front);
  int text_alpha = (int)(ui_alpha * NP_CLAMP(1 - away * 2.2f, 0.f, 1.f));
  int game = h->item[front];
  if (text_alpha > 0) {
    color_t panel = gfx_rgb(th->panel), accent = gfx_rgb(th->accent);
    gfx_rrect(200, 45, 110, 128, 9, panel, text_alpha);
    gfx_text_center(&np_font_small, 255, 62, game < 0 ? "SYSTEM" : "SELECTED GAME", tx, text_alpha);
    char index[] = "00 / 00";
    index[0] = '0' + (front + 1) / 10;
    index[1] = '0' + (front + 1) % 10;
    int count = h->n - 1;
    index[5] = '0' + count / 10;
    index[6] = '0' + count % 10;
    gfx_text_center(&np_font_body, 255, 87, game < 0 ? "Settings" : index, tx, text_alpha);
    gfx_rrect(212, 100, 86, 4, 2, tx, text_alpha / 4);
    gfx_rrect(212, 100, game < 0 ? 86 : NP_MAX(4, 86 * (front + 1) / NP_MAX(1, count)), 4, 2, accent, text_alpha);
    gfx_rrect(210, 122, 90, 35, 7, accent, text_alpha);
    gfx_text_center(&np_font_body, 255, 144, game < 0 ? "OK: Open" : "OK: Play", gfx_rgb(th->bg), text_alpha);
    gfx_text_center(&np_font_title, 160, 198, item_title(game), tx, text_alpha);
    char tagline[96];
    gfx_text_center(&np_font_small, 160, 214, item_tagline(game, tagline), tx, text_alpha);
    gfx_rrect(10, 221, 300, 17, 5, panel, ui_alpha);
    gfx_text_center(&np_font_small, 160, 233, "Up: Library   Down: Theme   L/R: Browse", tx, ui_alpha);
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

static void splash(void *ctx) {
  (void)ctx;
  const np_theme_t *t = np_theme();
  gfx_fill(0, 0, SCREEN_W, SCREEN_H, gfx_rgb(t->bg));
  gfx_fill(159, 66, 2, 18, gfx_rgb(t->accent));
  gfx_fill(151, 74, 18, 2, gfx_rgb(t->accent));
  gfx_text_center(&np_font_body, 160, 117, "TwiliPlay", gfx_rgb(t->text), 256);
  gfx_text_center(&np_font_small, 160, 139, "STARLIGHT EDITION", gfx_rgb(t->accent), 256);
}
int np_home(int *selected, bool returning) {
  if (!bg_loaded) { /* once per run: the background chosen last time (before ui_init, which makes its tables) */
    bg_loaded = true;
    int m;
    if (np_bg_load(&m)) live_set(m);
  }
  ui_init();
  home_t h = {0};
  h.motion = np_motion_load();
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
    if (h.motion) {
      ui_frame(splash, NULL);
      uint32_t start = np_millis();
      while (np_millis() - start < 360) {
        uint32_t key = np_keys();
        if (key & K_HOME) return -2;
        if (key) { np_wait_release(); break; }
        np_sleep(8);
      }
      h.pos = h.sel + 1.2f;
    }
    first_open = false;
  }
  bool unzooming = h.motion && returning && h.item[h.sel] == *selected && *selected >= 0;
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
    if (h.library) {
      int before = h.sel;
      if (pressed & K_HOME) return -2;
      if (pressed & K_BACK) {
        h.library = false;
        h.pos = h.sel;
        h.shot = 0; h.slide = 0; h.shot_time = 0; h.badge = 0;
        dirty = true;
      } else {
        if (pressed & K_UP) h.sel = NP_MAX(0, h.sel - 1);
        if (pressed & K_DOWN) h.sel = NP_MIN(h.sel + 1, NP_MAX(0, h.n - 2));
        if ((pressed & K_OK) && h.item[h.sel] >= 0 && !np_game_ram_missing(h.item[h.sel])) {
          *selected = h.item[h.sel];
          return *selected;
        }
        if (h.sel != before) dirty = true;
        if (dirty) { if (h.motion) live_frame(now); ui_frame(scene, &h); dirty = false; }
        else np_sleep(16);
        continue;
      }
      pressed = 0;
    }
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
      if (pressed & K_UP) {
        h.library = true;
        h.sel = NP_MIN(h.sel, NP_MAX(0, h.n - 2));
        h.zoom = 0;
        unzooming = false;
        dirty = true;
        continue;
      }
      if (pressed & K_DOWN) {
        np_theme_step(-1);
        h.toast = TOAST_MS;
        dirty = true;
      }
      if (pressed & K_BACKSPACE) { /* the next background, then none */
        live_next();
        np_bg_save(live_mode());
        if (!live_ready()) ui_init(); /* Tunnel's and Fire's bigger tables */
        dirty = true;
      }
      if (pressed & (K_HOME | K_BACK)) return -2;
    }
    /* motion */
    float pos = h.motion ? ui_approach(h.pos, (float)h.sel, dt, 0.075f) : h.sel;
    bool moving = pos != h.pos;
    if (moving) dirty = true;
    h.pos = pos;
    int game = h.item[h.sel];
    bool settled = !moving;
    if (settled && game >= 0 && result == -3) {
      if (h.badge < 1) {
        h.badge = h.motion ? NP_MIN(h.badge + dt / 0.22f, 1.f) : 1.f;
        dirty = true;
      }
      if (h.motion && np_games[game].nshots > 1) {
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
        h.zoom = h.motion ? NP_MIN(h.zoom + dt / 0.24f, 1.f) : 1.f;
        dirty = true;
        if (h.zoom >= 1) {
          ui_frame(scene, &h);
          *selected = result;
          return result;
        }
      }
    }
    if (h.toast) {
      h.toast = elapsed >= h.toast ? 0 : (uint16_t)(h.toast - elapsed);
      if (!h.toast) dirty = true;
    }
    if (h.motion && live_active()) dirty = true; /* the background moves: every frame */
    if (dirty) {
      if (h.motion) live_frame(now);
      ui_frame(scene, &h);
      dirty = bar_dirty = false;
    } else if (bar_dirty) {
      /* No screenshot progress bar in the Stage 2 layout. */
      bar_dirty = false;
    } else {
      np_sleep(16);
    }
  }
}
