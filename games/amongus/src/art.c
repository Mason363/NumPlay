/* Among Us: the pictures of the screens around the round: stars, the title, players lined up, the meeting's
 * tablet and plates, the kill and report overlays, the ejection. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "gfx.h"
#include "view.h"

/* the starfield (StarGen): stars drifting by */
void art_stars(float t, float speed) {
  gfx_fill(0, gfx_y0, SCREEN_W, BAND, 0x0000, 256);
  for (int k = 0; k < 90; k++) {
    uint32_t h = (uint32_t)k * 2654435761u;
    h ^= h >> 15;
    int y = (int)(h % SCREEN_H);
    if (y < gfx_y0 || y >= gfx_y0 + BAND) continue;
    float sp = 8 + (h >> 9 & 31);
    int x = (int)fmodf((float)(h >> 3 & 511) + t * sp * speed * 10, 340) - 10;
    if (x < 0 || x >= SCREEN_W) continue;
    uint16_t c = (h >> 20 & 3) ? 0x8C71 : 0xFFFF;
    gfx_band[(y - gfx_y0) * SCREEN_W + x] = c;
  }
}

void art_player(int color, float x, float y, float scale, bool flip, int alpha) {
  const Pic *p = pic(PIC_IDLE);
  float s = scale;
  if (s == 1 && !flip) gfx_spr(p, (int)x, (int)y, false, color_pal[color], alpha);
  else gfx_pic(p, x, y, flip ? -s : s, s, 0, color_pal[color], alpha);
}

void art_title(void) {
  art_stars(g.screen_t, 1);
  gfx_text(FONT_HUGE, 160, 80, "AMONG US", 0xFFFF, 0x0000, 2, 1);
  for (int k = 0; k < 4; k++) art_player(k * 3 % N_COLORS, 70.0f + k * 60, 120, 1.2f, k & 1, 256);
}

/* the team in a row, the player in front (IntroCutscene, EndGameManager) */
void art_lineup(const uint8_t *who, int n, float t) {
  float grow = t < 0.4f ? t / 0.4f : 1;
  /* the light on the floor */
  for (int y = 150; y < 200; y++)
    if (y >= gfx_y0 && y < gfx_y0 + BAND) {
      float w = 140 * grow * (1 - (y - 150) / 80.0f);
      gfx_fill(160 - (int)w, y, (int)(2 * w), 1, 0x2945, 200);
    }
  for (int k = n - 1; k >= 0; k--) {
    int side = (k + 1) / 2 * ((k & 1) ? -1 : 1);
    float sc = 2.6f - 0.25f * (float)abs(side);
    float x = 160 + side * 34.0f * grow, y = 150 - (float)abs(side) * 6;
    art_player(g.p[who[k]].color, x, y, sc, side < 0, 256);
  }
}

/* ---- the meeting's tablet ---- */

void art_meeting_bg(void) {
  gfx_fill(0, gfx_y0, SCREEN_W, BAND, rgb565(0x1B2433), 256);
  gfx_fill(4, 4, 312, 222, rgb565(0x8EA4B8), 256);
  gfx_fill(7, 22, 306, 200, rgb565(0xC6D3DE), 256);
  gfx_text(FONT_BIG, 160, 18, "Who Is The Impostor?", 0xFFFF, 0x0000, 1, 1);
}

void art_plate(int x, int y, int i, bool sel, bool voted, bool dead) {
  const Player *P = &g.p[i];
  uint16_t bg = dead ? rgb565(0x6E7E8C) : rgb565(0xE6EEF4);
  gfx_fill(x, y, 146, 26, sel ? 0xFFE0 : rgb565(0x5A6B7A), 256);
  gfx_fill(x + 2, y + 2, 142, 22, bg, 256);
  art_player(P->color, (float)x + 16, (float)y + 13, 0.7f, false, dead ? 120 : 256);
  bool red = g.p[g.me].imp && P->imp;
  gfx_text(FONT_MID, x + 32, y + 17, COLOR_NAMES[P->color], red ? 0xF800 : 0x0000, 0, 0, 0);
  if (dead) gfx_line((float)x + 6, (float)y + 4, (float)x + 26, (float)y + 22, 3, 0xF800, 256),
            gfx_line((float)x + 26, (float)y + 4, (float)x + 6, (float)y + 22, 3, 0xF800, 256);
  if (voted) {
    gfx_fill(x + 96, y + 6, 44, 14, rgb565(0x2E7D32), 256);
    gfx_text(FONT_SMALL, x + 118, y + 16, "I Voted", 0xFFFF, 0, 0, 1);
  }
}

void art_vote_icon(int x, int y, int color, bool anon) {
  gfx_disc((float)x, (float)y, 5, 0x0000, 256);
  gfx_disc((float)x, (float)y, 4, anon ? rgb565(0x8C8C8C) : rgb565(BODY_COLORS[color]), 256);
}

void art_skip(int x, int y, bool sel) {
  gfx_fill(x, y, 90, 18, sel ? 0xFFE0 : rgb565(0x5A6B7A), 256);
  gfx_fill(x + 2, y + 2, 86, 14, rgb565(0xE6EEF4), 256);
  gfx_text(FONT_MID, x + 45, y + 14, "Skip Vote", 0x0000, 0, 0, 1);
}

/* ---- overlays ---- */

void art_kill(float t) {
  /* the killer over the victim on a red band */
  int h = t < 0.2f ? (int)(t / 0.2f * 60) : 60;
  gfx_fill(0, 120 - h, SCREEN_W, 2 * h, 0x0000, 256);
  gfx_fill(0, 120 - h + 6, SCREEN_W, 2 * h - 12, rgb565(0xB01010), 256);
  if (t > 0.2f) {
    art_player(g.p[g.killer].color, 120, 120, 2.4f, false, 256);
    gfx_pic(pic(PIC_DEAD), 200, 140, 2.4f, 2.4f, 0, color_pal[g.p[g.me].color], 256);
  }
}

void art_report(float t) {
  int h = t < 0.2f ? (int)(t / 0.2f * 50) : 50;
  gfx_fill(0, 120 - h, SCREEN_W, 2 * h, rgb565(0x1D2A3A), 240);
  if (t > 0.2f) {
    bool body = g.reported != NONE;
    gfx_text(FONT_HUGE, 160, 130, body ? "Dead Body Reported" : "Emergency Meeting", 0xFFFF, 0xF800, 2, 1);
    art_player(g.p[g.caller].color, 160, 90, 1.2f, false, 256);
  }
}

void art_eject(float t) {
  art_stars(t, 2);
  char b[64];
  int e = g.ejected;
  if (e != NONE) {
    float x = -30 + t * 60, ang = t * 2.2f;
    gfx_pic(pic(PIC_IDLE), x, 120, 1.4f, 1.4f, ang, color_pal[g.p[e].color], 256);
  }
  if (e == NONE) snprintf(b, sizeof b, "No one was ejected. (%s)", g.tie ? "Tie" : "Skipped");
  else if (!g.opt.confirm) snprintf(b, sizeof b, "%s was ejected.", COLOR_NAMES[g.p[e].color]);
  else snprintf(b, sizeof b, "%s was %sThe Impostor.", COLOR_NAMES[g.p[e].color], g.p[e].imp ? "" : "not ");
  int n = (int)((t - 0.6f) * 22);
  if (n < 0) n = 0;
  char shown[64];
  snprintf(shown, sizeof shown, "%.*s", n, b);
  gfx_text(FONT_BIG, 160, 120, shown, 0xFFFF, 0, 0, 1);
  if (g.opt.confirm && t > 3.5f) {
    int k = living(true);
    snprintf(b, sizeof b, "%d Impostor%s remain%s.", k, k == 1 ? "" : "s", k == 1 ? "s" : "");
    gfx_text(FONT_MID, 160, 142, b, 0xFFFF, 0, 0, 1);
  }
}
