#include "ui.h"
#include "inflate.h"
#include "live.h"

/* ---- screenshots: decoded on demand into a few slots of the arena */
#define MAX_SLOTS 6
typedef struct {
  int16_t game, shot;
  uint32_t used;
  uint8_t *px;
} slot_t;
static slot_t *slots; /* menu-only metadata belongs in the shared arena */
static int nslots;
static uint32_t lru_clock;
static np_inflate_tables_t *tables;

void ui_init(void) {
  np_alloc_reset();
  gfx_init();
  tables = np_alloc(np_inflate_tables_size());
  live_init(); /* its small tables first: the screenshot slots below take all that is left */
  nslots = 0;
  slots = np_alloc(MAX_SLOTS * sizeof(*slots));
  lru_clock = 0;
  if (!slots || !tables) return;
  while (nslots < MAX_SLOTS) {
    uint8_t *p = np_alloc(SHOT_W * SHOT_H);
    if (!p) break;
    slots[nslots++] = (slot_t){-1, -1, 0, p};
  }
}

const uint8_t *ui_shot(int game, int shot) {
  if (game < 0 || !np_game_installed(game) || !nslots) return 0;
  const np_game_t *g = &np_games[game];
  if (shot < 0 || shot >= g->nshots) return 0;
  slot_t *best = &slots[0];
  for (int i = 0; i < nslots; i++) {
    if (slots[i].game == game && slots[i].shot == shot) {
      slots[i].used = ++lru_clock;
      return slots[i].px;
    }
    if (slots[i].used < best->used) best = &slots[i];
  }
  const np_shot_t *s = &g->shots[shot];
  best->game = -1;
  if (s->w != SHOT_W || s->h != SHOT_H) return 0;
  if (np_inflate(best->px, SHOT_W * SHOT_H, s->z, s->zsize, tables) != SHOT_W * SHOT_H) return 0;
  best->game = (int16_t)game;
  best->shot = (int16_t)shot;
  best->used = ++lru_clock;
  return best->px;
}

/* ---- input */
uint32_t ui_poll(ui_keys_t *k) {
  uint32_t now = np_millis(), keys = np_keys();
  uint32_t arrows = K_LEFT | K_RIGHT | K_UP | K_DOWN;
  uint32_t pressed = keys & ~k->held;
  if (pressed) {
    k->since = now;
    k->next = now + 380;
  } else if ((keys & arrows) && (int32_t)(now - k->next) >= 0) {
    pressed = keys & arrows;
    k->next = now + 120;
  }
  k->held = keys;
  return pressed;
}

/* ---- motion */
float ui_ease_out(float t) {
  t = NP_CLAMP(t, 0.f, 1.f);
  float u = 1 - t;
  return 1 - u * u * u;
}

float ui_ease_in_out(float t) {
  t = NP_CLAMP(t, 0.f, 1.f);
  return t < 0.5f ? 4 * t * t * t : 1 - (-2 * t + 2) * (-2 * t + 2) * (-2 * t + 2) / 2;
}

/* Exponential approach, independent of the frame rate. */
float ui_approach(float v, float target, float dt, float tau) {
  float k = dt / (tau + dt);
  float out = v + (target - v) * k;
  float d = out - target;
  return (d < 0.001f && d > -0.001f) ? target : out;
}

void ui_frame(gfx_scene_t scene, void *ctx) {
  np_vblank();
  gfx_render(scene, ctx, 0, SCREEN_H);
}

/* ---- pieces */
void ui_button(int x, int y, int w, int h, const char *label, bool selected, uint32_t accent, int a) {
  if (selected) {
    gfx_rrect(x, y + 2, w, h, h / 2, 0, a / 4);
    gfx_rrect(x, y, w, h, h / 2, gfx_rgb(accent), a);
    gfx_text_center(&np_font_body, x + w / 2, y + h / 2 + 4, label, 0xFFFF, a);
  } else {
    gfx_rrect(x, y, w, h, h / 2, 0xFFFF, a * 40 / 256);
    gfx_text_center(&np_font_body, x + w / 2, y + h / 2 + 4, label, 0xFFFF, a * 200 / 256);
  }
}

void ui_key_hint(int x, int baseline, const char *key, const char *label, color_t c) {
  int kw = gfx_text_width(&np_font_small, key) + 8;
  gfx_rrect(x, baseline - 10, kw, 13, 4, c, 70);
  gfx_text(&np_font_small, x + 4, baseline, key, c, 256);
  gfx_text(&np_font_small, x + kw + 4, baseline, label, c, 200);
}
