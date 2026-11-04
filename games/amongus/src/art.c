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

/* the main menu: the logo over space, crewmates drifting by */
void art_title(void) {
  art_stars(g.screen_t, 1);
  float t = g.screen_t;
  for (int k = 0; k < 3; k++) {
    float x = fmodf(t * (9 + 4 * k) + k * 140, 420) - 50, y = 126 + 6 * sinf(t * 0.5f + k * 2.1f) + (k - 1) * 3;
    gfx_pic(pic(PIC_IDLE), x, y, 0.9f - 0.12f * k, 0.9f - 0.12f * k, t * (0.6f + 0.25f * k) + k, color_pal[(k * 5 + 1) % N_COLORS],
            256);
  }
  gfx_spr(pic(PIC_UI_BANNERLOGO_AMONGUS), 160, 62, false, NULL, 256);
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

/* ---- the meeting's tablet (MeetingHud): its glass from x 15 to 285, y 20 to 207 ---- */

void art_meeting_bg(void) {
  gfx_fill(0, gfx_y0, SCREEN_W, BAND, rgb565(0x10141C), 256);
  gfx_tiles((const TilePic *)unpacked(BLOB_MEETING, 0), 160, 114);
  gfx_text(FONT_BIG, 150, 35, "Who Is The Impostor?", 0xFFFF, 0x0000, 1, 1);
}

/* a player's plate (PlayerVoteArea): the head, the name, I Voted, the megaphone of who called it, the X of the dead */
void art_plate(int x, int y, int i, bool sel, bool voted, bool dead) {
  const Player *P = &g.p[i];
  if (sel) gfx_fill(x - 2, y - 2, 134, 28, 0xFFE0, 256);
  gfx_spr(pic(PIC_UI_VOTEPLAYERBASE), x + 65, y + 12, false, NULL, 256);
  if (dead) gfx_fill(x, y, 128, 22, rgb565(0x7A8794), 150);
  gfx_spr_clip(pic(PIC_PLATE_PLAYER), x + 15, y + 10, false, color_pal[P->color], dead ? 140 : 256, x, y, x + 130, y + 22);
  bool red = g.p[g.me].imp && P->imp;
  gfx_text(FONT_MID, x + 31, y + 16, COLOR_NAMES[P->color], red ? 0xF800 : 0xFFFF, 0x0000, 1, 0);
  if (dead) gfx_spr(pic(PIC_UI_DEADX_BORDER), x + 15, y + 11, false, NULL, 256);
  if (voted) gfx_spr(pic(PIC_UI_I_VOTED), x + 6, y + 6, false, NULL, 256);
  if (i == g.caller) gfx_spr(pic(PIC_UI_REPORTED), x + 114, y + 11, false, NULL, 256);
}

void art_vote_icon(int x, int y, int color, bool anon) {
  static uint16_t gray[256];
  if (anon && !gray[1]) gfx_mix_palette(gray, 0x8C8C8C, 0x505050, 0x95CADC);
  gfx_spr(pic(PIC_VOTE_ICON), x, y, false, anon ? gray : color_pal[color], 256);
}

void art_skip(int x, int y, bool sel) {
  if (sel) gfx_fill(x - 2, y - 2, 70, 19, 0xFFE0, 256);
  gfx_spr(pic(PIC_UI_BUTTON_SKIPVOTE_EDIT), x + 33, y + 7, false, NULL, 256);
}

/* the vote's buttons by a plate: the tick and the cross (the one chosen framed) */
void art_confirm(int x, int y, bool yes) {
  gfx_fill(x - 1 + (yes ? 0 : 22), y - 1, 21, 21, 0xFFE0, 256);
  gfx_spr(pic(PIC_UI_VOTECONFIRM), x + 9, y + 9, false, NULL, 256);
  gfx_spr(pic(PIC_UI_VOTECANCEL), x + 31, y + 9, false, NULL, 256);
}

/* ---- overlays (KillOverlay) ---- */

/* the flames' band, opening from the middle and closing at the end */
static void flames(float t, float len) {
  float k = t < 0.2f ? t / 0.2f : t > len - 0.25f ? (len - t) / 0.25f : 1;
  if (k < 0) k = 0;
  int h = (int)(73 * k);
  gfx_fill(0, 120 - h, SCREEN_W, 2 * h, 0x0000, 256);
  const TilePic *bg = (const TilePic *)unpacked(BLOB_KILLBG, 0);
  if (h > 2) {
    /* (the band's middle rows, as many as it is open) */
    int y0 = 120 - h > gfx_y0 ? 120 - h : gfx_y0, y1 = 120 + h < gfx_y0 + BAND ? 120 + h : gfx_y0 + BAND;
    if (y0 < y1) {
      int save = gfx_y0;
      (void)save;
      for (int y = y0; y < y1; y++) {
        int sy = 73 + (int)((y - 120) / k);
        uint16_t *row = gfx_band + (y - gfx_y0) * SCREEN_W;
        for (int x = 0; x < SCREEN_W; x++) {
          int c = tile_px(bg, x, sy);
          if (c >= 0) row[x] = (uint16_t)c;
        }
      }
    }
  }
}

/* the tongue kill (KillAnimation_Tongue): the killer's and the victim's frames over the flames */
void art_kill(float t) {
  flames(t, 2.6f);
  if (t < 0.2f || t > 2.35f) return;
  static uint16_t kp[2][N_KMIX];
  static uint8_t kc[2] = {0xFF, 0xFF};
  int col[2] = {g.p[g.killer].color, g.p[g.me].color};
  for (int k = 0; k < 2; k++)
    if (kc[k] != col[k]) {
      gfx_mix_palette_of(BLOB_KMIX, N_KMIX, kp[k], BODY_COLORS[col[k]], SHADOW_COLORS[col[k]], 0x95CADC);
      kc[k] = (uint8_t)col[k];
    }
  int f = (int)((t - 0.2f) * 15);
  if (f > 23) f = 23;
  const uint8_t *keep = gfx_mix_alpha;
  gfx_mix_alpha = (const uint8_t *)blob(BLOB_KMIX) + 3;
  gfx_spr((const Pic *)unpacked(PIC_KILL_VICTIM + f, 2), 178, 128, false, kp[1], 256);
  gfx_spr((const Pic *)unpacked(PIC_KILL_IMP + f, 1), 178, 128, false, kp[0], 256);
  gfx_mix_alpha = keep;
}

/* "Dead Body Reported" with the body, or "Emergency Meeting" with who called it */
void art_report(float t) {
  flames(t, 2.5f);
  if (t < 0.2f || t > 2.3f) return;
  bool body = g.reported != NONE;
  float s = t < 0.35f ? (t - 0.2f) / 0.15f : 1;
  if (body) {
    gfx_pic(pic(PIC_UI_BEFOREVOTING_DEADBODYTEXT), 104, 120, s, s, 0, NULL, 256);
    gfx_spr(pic(PIC_DEADBODY_BIG), 236, 132, false, color_pal[g.p[g.reported].color], 256);
  } else {
    gfx_pic(pic(PIC_UI_BEFOREVOTING_EMTEXT), 104, 120, s, s, 0, NULL, 256);
    gfx_spr(pic(PIC_IDLE_BIG), 232, 124, true, color_pal[g.p[g.caller].color], 256);
    gfx_spr(pic(PIC_UI_REPORTED), 206, 116, true, NULL, 256);
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
