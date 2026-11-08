/* A bot for the tests and the gallery's recording (built with FD_AUTOPLAY): it tries a few ways of flying to the
 * next hoop, flapping now or not, on copies of the game, and flaps when that does better. Not in the app. */
#include <math.h>
#include "fd.h"

static fd_game_t snap;

/* flaps when the ball is below a line falling towards the next hoop (lo above it at the hoop, hi farther back) */
static float rollout(bool flap_now, float lo, float hi, float reach) {
  G = snap;
  fd_quiet = true;
  uint32_t hoops0 = G.hoops, sw0 = G.swishes;
  if (flap_now) game_flap();
  int i, after = -1;
  for (i = 1; i < 420 && G.state <= ST_PLAY; i++) {
    const hoop_t *h = G.next < G.nh ? &G.h[G.next] : 0;
    if (i % 4 == 0) {
      float dx = h ? h->hx - G.x : 9, ty = h ? h->hy : 0;
      float f = dx / reach;
      if (f < 0) f = 0;
      if (f > 1) f = 1;
      if (G.y < ty + lo + (hi - lo) * f && G.vy < 1) game_flap();
    }
    game_step();
    if (G.hoops > hoops0 && after < 0) after = i;
    if (after >= 0 && i - after > 40) break;
  }
  float v = (float)(G.hoops - hoops0) * 1000 + (float)(G.swishes - sw0) * 400;
  if (G.state == ST_DEAD) v -= 6000 - (float)i;
  if (G.state == ST_WON) v += 3000;
  v -= fabsf(G.y) * 10;
  return v;
}

bool bot_wants_flap(int effort) {
  static const float pol[][3] = {{0.2f, 1.5f, 4}, {0.5f, 2.5f, 6}, {0.0f, 1.0f, 3}, {0.8f, 3.0f, 8},
                                 {0.3f, 0.6f, 2}, {1.0f, 2.0f, 5}, {-0.3f, 1.2f, 3}, {0.4f, 4.0f, 10}};
  snap = G;
  float best[2] = {-1e9f, -1e9f};
  for (int choice = 0; choice < 2; choice++)
    for (int k = 0; k < effort; k++) {
      float v = rollout(choice, pol[k][0], pol[k][1], pol[k][2]);
      if (v > best[choice]) best[choice] = v;
    }
  G = snap;
  fd_quiet = false;
  return best[1] > best[0] + 1;
}
