/* What is drawn over the lawn: the seed bank, the shovel, the progress meter, the cursor and the messages
 * (draw.h). */
#include <string.h>
#include "draw.h"
#include "game.h"
#include "gfx.h"
#include "res.h"
#include "save.h"

Hud hud;

static const uint16_t C_DARK = 0x0000;

/* ---------------------------------------------------------------- seed packets */

void packet_draw(int kind, int x, int y, int cooldown, int full, bool dim, bool selected, int key) {
  gfx_image(I_PACKET, x, y);
  /* (the explode-o-nut: the wall-nut's picture in red, as the game colours it) */
  if (kind == P_EXPLODENUT) gfx_anim(I_PACKET_WALLNUT, 0, x + 10, y + 12, 0, TINT_RED, 32);
  else gfx_image(I_PACKET_PEASHOOTER + kind, x + 10, y + 12);
  if (full >= 0 && kind < N_SEEDS && plant_defs[kind].cost)
    gfx_text(F_SMALL, x + 10, y + 26, gfx_fmt("%d", plant_defs[kind].cost), C_DARK, GFX_CENTER);
  if (full > 0 && cooldown > 0) {
    int h = 28 * cooldown / full;
    gfx_rect(x, y, 20, h, C_DARK, 14);
  }
  if (dim) gfx_rect(x, y, 20, 28, C_DARK, 12);
  if (selected) gfx_frame_rect(x - 1, y - 1, 22, 30, 1, rgb565(0xFFE14A), 32);
  if (key) gfx_text(F_SMALL, x + 1, y + 6, gfx_fmt("%d", key % 10), C_WHITE, GFX_OUTLINE);
}

int bank_width(void) { return B.nseeds > 6 ? 178 + (B.nseeds - 6) * 21 : 178; }

/* the seed bank: the sun, then the packets */
void hud_bank_draw(int x0, int y0, bool costs) {
  int w = bank_width();
  if (w > 178) {
    /* longer for more slots: its middle stretched */
    gfx_clip(x0, y0, 160, 40);
    gfx_image(I_SEEDBANK, x0, y0);
    for (int x = 160; x < w - 10; x += 10) {
      gfx_clip(x0 + x, y0, 10, 40);
      gfx_image(I_SEEDBANK, x0 + x - 150, y0);
    }
    gfx_clip(x0 + w - 10, y0, 10, 40);
    gfx_image(I_SEEDBANK, x0 + w - 178, y0);
    gfx_clip_all();
  } else
    gfx_image(I_SEEDBANK, x0, y0);
  if (costs) gfx_text(F_SMALL, x0 + 16, y0 + 33, gfx_fmt("%d", (int)B.sun), C_DARK, GFX_CENTER);
}

static void conveyor_draw(void) {
  gfx_image(I_CONVEYOR, 0, 0);
  /* the belt's stripes, moving */
  int off = (int)(B.ticks / 4) % 6;
  for (int x = 6 - off; x < 202; x += 6) gfx_rect(x, 9, 1, 22, rgb565(0x3A3A3A), 32);
  for (int i = 0; i < B.npackets; i++) {
    int x = 6 + (int)(B.packets[i].x * 0.4f);
    if (x > 190) continue;
    packet_draw(B.packets[i].kind, x, 4, 0, -1, false, hud.selected == i, i + 1 <= 10 ? i + 1 : 0);
  }
}

static void meter_draw(void) {
  if (B.mode == MODE_VASE || B.has_boss) return;
  int x = 252, y = 228, w = 63;
  gfx_image(I_METER_EMPTY, x, y);
  int n = B.nwaves ? B.nwaves : 1;
  int fill = w * B.wave / n;
  if (fill > 0) {
    gfx_clip(x + w - fill, y, fill, 12);
    gfx_image(I_METER_FULL, x, y);
    gfx_clip_all();
  }
  int per = waves_flag_every();
  for (int k = 0; per && B.level != 1 && k < n; k++)
    if (k % per == per - 1) {
      int fx = x + w - (k + 1) * w / n;
      gfx_image(I_METER_FLAG, fx + 2, y - (B.wave > k ? 4 : 1));
    }
  gfx_image(I_METER_HEAD, x + w - fill, y + 5);
}

void draw_money(int x, int y, uint32_t money) {
  gfx_image(I_COIN_GOLD, x + 6, y);
  gfx_text(F_HEAD, x + 14, y + 4, gfx_fmt("$%u", (unsigned)money), rgb565(0xFFE14A), GFX_OUTLINE);
}

/* ---------------------------------------------------------------- messages */

void draw_message_band(const char *s) {
  gfx_rect(0, 196, SCREEN_W, 22, C_DARK, 20);
  gfx_paragraph(F_BODY, 160, 206, 300, s, rgb565(0xFFE14A), GFX_OUTLINE);
}

void draw_ready(int t) {
  /* "Ready... Set... PLANT!", a word at a time, each one jumping in */
  const char *w = t < 60 ? "Ready..." : t < 120 ? "Set..." : "PLANT!";
  int k = t < 60 ? t : t < 120 ? t - 60 : t - 120;
  uint16_t c = t < 120 ? rgb565(0xE02020) : rgb565(0xFF2828);
  int dy = k < 8 ? (8 - k) * 3 : 0;
  gfx_text(F_BIG, 160, 128 - dy, w, c, GFX_CENTER | GFX_OUTLINE);
}

static const char *tutorial_hint(void) {
  if (B.level == 1 && B.tutorial == 1) {
    int peas = 0;
    for (int i = 0; i < B.nplants; i++) peas += B.plants[i].kind == P_PEASHOOTER;
    if (hud.selected >= 0) return "Move to the middle row and press OK to plant your seed!";
    if (peas == 0) return "Press 1 to pick up the peashooter's seed packet!";
    if (B.sun < 100) return "Collect the falling sun: move the cursor onto it!";
    if (B.cooldown[0] > 0) return "Nice! Wait for the seed packet to recharge.";
    return "You have enough sun: plant another peashooter!";
  }
  if ((B.level == 2 || B.tutorial == 3) && B.tutorial) {
    if (B.sun < 50) return "Collect sun until you have 50, then plant a sunflower (2).";
    return "Sunflowers are very important! Plant at least three of them.";
  }
  if (B.level == 4 && B.ticks < 900 && B.started) return "Plant wall-nuts (4) in front of your peashooters to protect them.";
  if (B.mode == MODE_BOWLING && B.ticks < 800) return "Pick a wall-nut (1) and roll it from left of the red line: OK.";
  if (B.mode == MODE_CONVEYOR && B.ticks < 500) return "Pick plants from the conveyor belt with 1, 2, 3...";
  if (B.mode == MODE_WHACK && B.ticks < 700) return "Whack the zombies with OK before they get through!";
  if (B.mode == MODE_VASE && B.ticks < 700) return "Break the vases with OK. Pick up the seeds they hold with OK.";
  return NULL;
}

void hud_draw(bool cursor) {
  if (has_conveyor()) {
    if (B.tutorial != 4) conveyor_draw();   /* (1-5's belt comes after the shovel lesson) */
  } else if (B.mode != MODE_VASE && B.mode != MODE_WHACK) {
    hud_bank_draw(0, 0, true);
    for (int i = 0; i < B.nseeds; i++) {
      int kind = B.seeds[i];
      bool poor = B.sun < plant_cost(kind);
      packet_draw(kind, 34 + i * 21, 3, B.cooldown[i], plant_defs[kind].cooldown, poor || B.cooldown[i] > 0,
                  hud.selected == i, i + 1);
    }
  } else if (B.mode == MODE_WHACK) {
    hud_bank_draw(0, 0, true);
    for (int i = 0; i < B.nseeds; i++) {
      int kind = B.seeds[i];
      packet_draw(kind, 34 + i * 21, 3, B.cooldown[i], plant_defs[kind].cooldown,
                  B.sun < plant_cost(kind) || B.cooldown[i] > 0, hud.selected == i, i + 1);
    }
  }
  /* the shovel */
  if ((progress.items & ITEM_SHOVEL) && B.mode != MODE_VASE && B.mode != MODE_WHACK) {
    int x = has_conveyor() ? 208 : bank_width() + 2;
    gfx_image(I_SHOVELBANK, x, 3);
    if (!hud.shovel) gfx_image(I_SHOVEL, x + 14, 17);
  }
  meter_draw();
  gfx_text(F_SMALL, B.has_boss ? 240 : B.mode == MODE_VASE ? 316 : 248, 236, gfx_fmt("Level %d-%d", level_world(B.level), level_stage(B.level)), C_WHITE,
           GFX_RIGHT | GFX_OUTLINE);
  if (B.has_boss) {
    /* Dr. Zomboss's health */
    int w = 70 * boss.health / BOSS_HEALTH;
    gfx_rect(244, 228, 72, 8, C_DARK, 24);
    gfx_rect(245, 229, w, 6, rgb565(0xD83A2E), 32);
  }
  if (B.money || B.ncoins || (B.level >= 11 && progress.money)) draw_money(2, 228, progress.money);
  /* sun and coins flying home */
  for (int i = 0; i < B.nfx; i++) {
    const Fx *f = &B.fx[i];
    if (f->kind != FX_SUNFLY && f->kind != FX_COINFLY) continue;
    float t = (float)f->t / f->life, sx = (float)screen_x(f->x), sy = (float)screen_y(f->y);
    float tx = f->kind == FX_SUNFLY ? 16 : 8, ty = f->kind == FX_SUNFLY ? 14 : 228;
    float k = t * t * (3 - 2 * t);
    int x = (int)(sx + (tx - sx) * k), y = (int)(sy + (ty - sy) * k);
    if (f->kind == FX_SUNFLY) gfx_anim(A_SUN, (int)(vis_ticks / 8), x, y, 0, TINT_NONE, 32 - 16 * f->t / f->life);
    else gfx_image(f->v >= 1000 ? I_COIN_DIAMOND : f->v >= 50 ? I_COIN_GOLD : I_COIN_SILVER, x, y);
  }
  if (cursor) hud_cursor_draw();
  /* the big messages */
  for (int i = 0; i < B.nfx; i++) {
    const Fx *f = &B.fx[i];
    if (f->kind != FX_TEXT) continue;
    uint16_t red = rgb565(0xE82020);
    if (f->v == 0) {
      gfx_text(F_BIG, 160, 112, "A Huge Wave of Zombies", red, GFX_CENTER | GFX_OUTLINE);
      gfx_text(F_BIG, 160, 132, "is Approaching!", red, GFX_CENTER | GFX_OUTLINE);
    } else if (f->t < 200) {
      int s = f->t < 20 ? 256 + (20 - f->t) * 20 : 256;
      (void)s;
      gfx_text(F_BIG, 160, 128, "FINAL WAVE", red, GFX_CENTER | GFX_OUTLINE);
    }
  }
  const char *hint = B.phase == PH_PLAY ? tutorial_hint() : NULL;
  if (hint) draw_message_band(hint);
  if (hud.issue && vis_ticks - (uint32_t)hud.issue_t < 150)
    gfx_text(F_BODY, 160, 190, issue_text[hud.issue], C_WHITE, GFX_CENTER | GFX_OUTLINE);
}

/* ---------------------------------------------------------------- the cursor */

void hud_cursor_draw(void) {
  int col = hud.col, row = hud.row;
  float gx = BOARD_LEFT + col * CELL_W, cx = cell_x(col);
  float top = row_y(row, cx) - (B.env == ENV_ROOF ? 58 : is_pool() ? 68 : 78);
  int x = screen_x(gx), y = screen_y(top), w = 32, h = (int)(cell_h() * 0.4f + 0.5f);
  if (B.mode == MODE_WHACK && hud.selected < 0) {
    gfx_anim(I_HAMMER, 0, x + 18, y + 10 + (B.hammer_t > 10 ? 4 : 0), 0, TINT_NONE, 32);
    return;
  }
  int kind = hud.selected >= 0 ? (B.mode == MODE_VASE ? B.drops[hud.selected].kind : seed_kind(hud.selected)) : -1;
  int issue = kind >= 0 ? plant_issue(kind, col, row) : 0;
  if (kind >= 0 && B.mode != MODE_BOWLING) {
    float py = row_y(row, cx);
    if (B.env == ENV_ROOF && kind != P_FLOWERPOT) py -= 12;
    if (!issue) draw_plant_at(kind, screen_x(cx), screen_y(py), 16);
  } else if (kind >= 0) {
    int a = kind == P_EXPLODENUT ? A_EXPLODENUT : A_WALLNUT;
    gfx_anim(a, 0, screen_x(cx), screen_y(row_y(row, cx)), 0, issue ? TINT_GRAY : TINT_NONE, 16);
  }
  uint16_t c = issue ? rgb565(0xFF5040) : hud.shovel ? rgb565(0xFFD36B) : C_WHITE;
  gfx_rect(x + 1, y + 1, w - 2, h - 2, C_WHITE, 4);
  /* its corners */
  int k = 6;
  gfx_rect(x, y, k, 2, c, 32), gfx_rect(x, y, 2, k, c, 32);
  gfx_rect(x + w - k, y, k, 2, c, 32), gfx_rect(x + w - 2, y, 2, k, c, 32);
  gfx_rect(x, y + h - 2, k, 2, c, 32), gfx_rect(x, y + h - k, 2, k, c, 32);
  gfx_rect(x + w - k, y + h - 2, k, 2, c, 32), gfx_rect(x + w - 2, y + h - k, 2, k, c, 32);
  if (hud.shovel) gfx_image(I_SHOVEL, x + w - 4, y + 6);
}

/* ---------------------------------------------------------------- menus' parts */

void draw_dialog(int x, int y, int w, int h) {
  /* a stone tablet, like the game's menus */
  gfx_round_rect(x + 3, y + 4, w, h, 8, C_DARK, 12);
  gfx_round_rect(x, y, w, h, 8, rgb565(0x5A5E78), 32);
  gfx_round_rect(x + 3, y + 3, w - 6, h - 6, 6, rgb565(0x8A8FA8), 32);
  gfx_round_rect(x + 6, y + 6, w - 12, h - 12, 5, rgb565(0x6C7190), 32);
}

void draw_button(int x, int y, int w, const char *label, bool focus) {
  gfx_round_rect(x, y + 2, w, 20, 5, C_DARK, 14);
  gfx_round_rect(x, y, w, 20, 5, focus ? rgb565(0x3F7F2A) : rgb565(0x5A3A1E), 32);
  gfx_round_rect(x + 2, y + 2, w - 4, 16, 4, focus ? rgb565(0x6CBF3A) : rgb565(0x8A5A2E), 32);
  gfx_text(F_HEAD, x + w / 2, y + 14, label, focus ? rgb565(0xFFF5B0) : rgb565(0xF0D8A8), GFX_CENTER | GFX_OUTLINE);
}
