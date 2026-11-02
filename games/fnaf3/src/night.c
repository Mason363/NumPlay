/* Five Nights at Freddy's 3: a night, as the game's own events run it (gamesrc spec fnaf3_spec.md, from the
 * Android build's Clickteam events), 60 steps a second. Within a step things happen in the events' order, so each
 * reads what the ones before it just changed. The comments name the game's own counters and objects. */
#include <string.h>
#include "game.h"
#include "data.h"
#include "plat.h"

#define N g.n
static bool held(uint32_t k) { return (g.held & k) != 0; }
static bool pressed(uint32_t k) { return (g.pressed & k) != 0; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* Clickteam's "every N ms": its clock started with the night, so the ones with the same period fire together */
static bool every(int ms) {
  uint32_t t1 = N.frame * 50 / 3, t0 = (N.frame - 1) * 50 / 3;
  return t1 / (uint32_t)ms != t0 / (uint32_t)ms;
}

int screen_up(void) { return N.viewing >= 2 || N.panel >= 2; }
int spring_cam(void) { return N.loc >= L_CAM1 && N.loc <= L_VENT15 ? N.loc : 0; }
static int you_in(void) { return N.vents_map ? N.vent : N.cam; }
static bool err(int8_t h) { return h <= -10; }

/* ------------------------------------------------------------ moving him */
static void go(int loc) { N.loc = (uint8_t)loc, N.turns = 0, N.action = 0; }

static void quiet_vent(void) { caption("A quiet sound in the vents"); }
static void loud_vent(void) { caption("A loud bang in the vents"); }

/* the room graph (fnaf3_spec 2.4): a move for the action chosen, if this place has one now */
static void room_graph(void) {
  int a = N.action;
  if (a < 2) return;
  bool up = screen_up();
  switch (N.loc) {
  case 10: if (a <= 3) go(9); else go(14), quiet_vent(); break;
  case 9: if (a == 2) go(10); else if (a == 3) go(8); else go(11), quiet_vent(); break;
  case 8: if (a == 2) go(9); else if (a == 3) go(7); else go(5); break;
  case 7: if (a == 2) go(8); else if (a == 3) go(6); else go(12), quiet_vent(); break;
  case 6: if (a == 2) go(7); else go(5); break;
  case 5:
    if (a == 2) go(6);
    else if (a == 3) go(2);
    else if (!N.pic_random) go(4);
    else go(13), quiet_vent();
    break;
  case 2:
    if (a == 2) go(5);
    else if (a == 3) go(4);
    else if (!N.pic_random) go(L_ST1);
    else go(15), quiet_vent();
    break;
  case 4: if (a == 2) go(2); else go(3); break;
  case 3: if (a == 2) go(4); else go(L_ST1); break;
  case L_ST1: if (a >= 3) N.turns = 0, N.action = 0, N.latch = 1; break;
  case L_ST2: if (a >= 3) go(L_ST3); break;
  case L_ST3: if (up) go(a == 2 ? 1 : L_ST4); break;
  case 1: if (up) go(a == 2 ? L_ST3 : L_ST4); break;
  case L_ST4:   /* the attack: from the door if the panel is up, from the right if the monitor is */
    if (N.panel >= 2) go(L_GOT);
    else if (N.viewing >= 2) go(L_GOT2);
    break;
  }
}

/* the vents (fnaf3_spec 2.4): out the way he came if it's sealed, else on to the office */
static void vent_moves(void) {
  static const uint8_t back[5] = {9, 7, 5, 10, 2}, on[5] = {L_ST3, L_ST3, L_ST1, L_GOT2, L_GOT2};
  if (N.loc < L_VENT11 || N.loc > L_VENT15 || N.action < 2) return;
  int i = N.loc - L_VENT11;
  if (N.closed == N.loc) go(back[i]), quiet_vent();
  else go(on[i]), loud_vent();
}

/* a step at the blackout's darkest (fnaf3_spec 2.7): he comes one place closer */
static void blackout_peak(void) {
  bool moved = true;
  if (N.loc == L_ST1) go(L_ST2);
  else if (N.loc == L_ST2) go(L_ST3);
  else if (N.loc == L_ST3 || N.loc == 1) go(L_ST4);
  else if (N.loc == L_ST4) go(L_GOT);
  else if (N.loc == L_GOT2 && !screen_up() && !N.got_you) N.got_you = 2, N.x = XMAX;
  else moved = false;
  if (moved) N.black = 249, N.black_up = 1;
  if (N.head_out && N.head_t <= 0) N.head_out = 0;   /* (the Puppet's head, sliding off, gone at a peak) */
}

/* ------------------------------------------------------------ the screens */
static void monitor_down(void) {
  if (N.viewing == 0 && !N.mon_down) return;
  N.viewing = 0, N.mon_s0 = 0, N.mon_down = 1, N.mon_anim = 0;
  N.sealing = 0, N.sdfg = 0;   /* (lowering it cancels the sealing) */
}
static void panel_down(void) {
  if (N.panel && !N.panel_dropping) N.panel_dropping = 1, N.panel_anim = 0;
}

static void select_cam(int id) {
  if (id >= L_VENT11) N.vent = (uint8_t)id;
  else N.cam = (uint8_t)id;
  N.static_s4 = 250, N.bb_shown = 0, N.bb_stare = 0;
}

/* the map's buttons, for the arrows: the nearest one that way */
static const int16_t BTN[16][2] = {{0, 0},
  {BTN01_X, BTN01_Y}, {BTN02_X, BTN02_Y}, {BTN03_X, BTN03_Y}, {BTN04_X, BTN04_Y}, {BTN05_X, BTN05_Y},
  {BTN06_X, BTN06_Y}, {BTN07_X, BTN07_Y}, {BTN08_X, BTN08_Y}, {BTN09_X, BTN09_Y}, {BTN10_X, BTN10_Y},
  {BTN11_X, BTN11_Y}, {BTN12_X, BTN12_Y}, {BTN13_X, BTN13_Y}, {BTN14_X, BTN14_Y}, {BTN15_X, BTN15_Y}};
static int nearest(int from, int dx, int dy) {
  int lo = N.vents_map ? 11 : 1, hi = N.vents_map ? 15 : 10, best = from, bd = 1 << 30;
  for (int i = lo; i <= hi; i++) {
    int ex = BTN[i][0] - BTN[from][0], ey = BTN[i][1] - BTN[from][1];
    int along = ex * dx + ey * dy, side = ex * dy - ey * dx;
    if (i == from || along <= 0) continue;
    int d = along * along + 3 * side * side;
    if (d < bd) bd = d, best = i;
  }
  return best;
}

/* the lure's adjacency (fnaf3_spec 3): a lure at camera c pulls him from these places */
static bool lure_pulls(int c, int loc) {
  static const uint8_t from[11][4] = {{0}, {L_ST4}, {L_ST1, 3, 4, 5}, {2, 4}, {2, 3}, {2, 6, 7, 8}, {5, 7}, {6, 8},
                                      {7, 5, 9}, {8, 10}, {9}};
  for (int i = 0; i < 4; i++)
    if (from[c][i] && from[c][i] == loc) return true;
  return false;
}

static void play_audio(void) {
  /* [214]: the monitor up, no audio error, the counter full, the camera map */
  if (N.viewing < 2 || err(N.audio) || N.play < 7 || N.vents_map) return;
  N.play = 1;   /* (0, held at its minimum 1) */
  N.audio = (int8_t)clampi(N.audio - N.ai, -10, 0);
  N.sdfg = 133, N.lure_cam = N.cam;
  caption("BB laughs on the camera");
  if (rnd_n(7) + 1 == 1) return;   /* [220-221]: one time in 7 he doesn't come */
  if (lure_pulls(N.cam, N.loc) && !N.lure_to) N.lure_to = N.cam, N.lure_t = (int16_t)rnd_n(100), N.static_s4 = 200;
}

static void input(void) {
  bool locked = N.got_you || N.frozen;
  /* the view: 20 px a step while looking around [7-12] */
  if (!locked && N.viewing == 0 && !N.mon_down && N.panel == 0) {
    int dir = held(K_LEFT | K_DIGIT(4)) ? -1 : held(K_RIGHT | K_DIGIT(6)) ? 1 : 0;
    if (dir) N.want = 0;
    if (pressed(K_OK | K_EXE | K_UP | K_DIGIT(8))) N.want = 1;
    if (pressed(K_DOWN | K_DIGIT(2))) N.want = 2;
    if (N.want == 1 && N.x < 800) dir = 1;
    if (N.want == 2 && N.x > 400) dir = -1;
    if (dir < 0 && N.x > 400) N.x = (int16_t)(N.x - 20);
    if (dir > 0 && N.x < 800) N.x = (int16_t)(N.x + 20);
    /* the monitor's button at the right end [14], the panel's at the left [32] */
    if (N.want == 1 && N.x >= 800 && N.mon_s0 == 5 && !N.black2)
      N.viewing = 1, N.mon_anim = 0, N.want = 0;
    if (N.want == 2 && N.x <= 400 && !N.black2) N.panel = 1, N.panel_anim = 0, N.want = 0;
    return;
  }
  if (N.viewing >= 2 && N.mon_s0 == 4) {
    if (pressed(K_EXE)) {
      monitor_down();
      return;
    }
    if (pressed(K_DEL) && !N.toggle_cd) {   /* Map Toggle [373-424] */
      N.vents_map ^= 1, N.toggle_cd = 20, N.sdfg = 0, N.sealing = 0;
      N.static_s4 = 250, N.bb_shown = 0, N.bb_stare = 0;
    }
    int now = you_in(), to = 0;
    if (pressed(K_LEFT)) to = nearest(now, -1, 0);
    if (pressed(K_RIGHT)) to = nearest(now, 1, 0);
    if (pressed(K_UP)) to = nearest(now, 0, -1);
    if (pressed(K_DOWN)) to = nearest(now, 0, 1);
    for (int d = 0; d <= 9; d++)
      if (pressed(K_DIGIT(d))) {
        int id = d == 0 ? 10 : d;
        to = N.vents_map ? (id <= 5 ? 10 + id : 0) : id;
      }
    if (to && to != now && !(N.vents_map && N.sealing)) select_cam(to);
    if (pressed(K_OK)) {
      if (!N.vents_map) play_audio();
      else N.sealing = N.vent, N.seal_t = (int16_t)(50 + rnd_n(50));   /* (the double click on a vent camera) */
    }
    return;
  }
  if (N.panel == 3 && !N.panel_dropping) {
    if (!N.rebooting) {
      if (pressed(K_UP | K_DIGIT(8))) N.cursor = (uint8_t)(N.cursor == 1 ? 5 : N.cursor - 1);
      if (pressed(K_DOWN | K_DIGIT(2))) N.cursor = (uint8_t)(N.cursor == 5 ? 1 : N.cursor + 1);
      if (pressed(K_OK)) {
        if (N.cursor == 5) panel_down();
        else N.rebooting = N.cursor, N.reboot_p = 0;
      }
      if (pressed(K_EXE)) panel_down();
    }
  }
}

/* the monitor's and the panel's animations (flip out 6 pictures, flip in 4, the panel up 5 and down 4, at 50) */
static void screens(void) {
  if (N.viewing == 1 && ++N.mon_anim >= 12) N.viewing = 2, N.mon_s0 = 4, N.mon_anim = 0;
  if (N.mon_down && ++N.mon_anim >= 8) N.mon_down = 0, N.mon_s0 = 5, N.mon_anim = 0;
  if (N.panel == 1 && ++N.panel_anim >= 10) N.panel = 3, N.panel_anim = 0;
  if (N.panel_dropping && ++N.panel_anim >= 8) N.panel = 0, N.panel_dropping = 0, N.panel_anim = 0;
  if (N.toggle_cd) N.toggle_cd--;
}

/* ------------------------------------------------------------ the camera feed (fnaf3_spec 7.5) */
int feed_anim(int c) {
  static const uint8_t empty[16] = {0, 17, 14, 15, 16, 21, 12, 13, 23, 24, 25, 45, 46, 47, 48, 49};
  static const uint8_t him[16] = {0, 30, 43, 28, 29, 27, 26, 31, 32, 33, 34, 50, 51, 52, 53, 54};
  bool here = spring_cam() == c, h = c <= 10 && (N.halluc >> (c - 1) & 1);
  int a = empty[c];
  if (c == 2 && here) a = N.pic_random ? 43 : 35;
  else if (c == 2 && h) a = 43;
  else if (here || h) a = him[c];
  if (c == 4 && N.mangle == 2) a = 55;
  if (c == 8 && N.puppet == 2) a = 56;
  if (c == 7 && N.chica == 2) a = 57;
  return a;
}

/* ------------------------------------------------------------ a night */
void night_begin(int night) {
  memset(&N, 0, sizeof N);
  N.night = (uint8_t)night;
  N.x = 512, N.mon_s0 = 5, N.cursor = 1;
  N.cam = 2, N.vent = 11, N.play = 1;
  N.pic_random = (uint8_t)rnd_n(2);
  static const uint8_t start[5] = {10, 9, 8, 7, 6};
  N.loc = night == 1 ? L_OFF : start[rnd_n(5)];
  if (night == 1) {   /* [612]: the systems start a little worn */
    N.audio = (int8_t)(-3 - (int)rnd_n(4));
    N.camera = (int8_t)(-3 - (int)rnd_n(4));
    N.vent_h = (int8_t)(-2 - (int)rnd_n(4));
  }
  /* [537-539]: the phantoms that can come this night */
  N.bb = N.mangle = night >= 2, N.freddy = N.chica = night >= 3, N.puppet = night >= 4;
}

static void white_flash(void) {   /* [547-550] */
  N.vent_h = -10, N.vent_err = 2000, N.black_up = 1, N.black = 255;
  N.flash = 51, N.breath = 500, N.scare = PH_NONE, N.scare_t = 0;
}

static void start_scare(int who) {
  N.scare = (uint8_t)who, N.scare_t = 0, N.frozen = 1, N.cooldown = 10;
  monitor_down(), panel_down();
}

static void phantoms(void) {
  bool noon = N.hour == 0;
  int cam = you_in();
  bool mon = N.viewing >= 2;
  /* forced appearances: once, and only while that phantom is still to come [630-634] */
  if (!noon) {
    if (!N.bb) N.forced |= 1;
    if (N.mangle != 1) N.forced |= 2;
    if (N.puppet != 1) N.forced |= 4;
    if (N.chica != 1) N.forced |= 8;
    if (N.freddy != 1) N.forced |= 16;
  }
  /* BB [511-512, 521-529, 552-553] */
  if (every(20000) && N.bb == 1 && N.viewing <= 1 && rnd_n(10) + 1 <= N.ai && !noon) N.bb_pending = 1;
  if (!(N.forced & 1) && N.bb == 1 && N.viewing <= 1 && N.hour == 3) N.bb_pending = 1, N.forced |= 1;
  if (N.bb_pending && mon && (cam == 1 || cam == 7 || cam == 9 || cam == 10)) N.bb_pending = 0, N.bb_shown = 1;
  if (!mon) N.bb_shown = 0, N.bb_stare = 0;
  if (N.bb_shown && ++N.bb_stare > N.tlimit) {
    N.bb_shown = 0, N.bb = 0;
    monitor_down();
    start_scare(PH_BB);
    caption("BB screams");
  }
  /* Mangle [513-514, 557, 560-568] */
  if (every(20000) && cam != 4 && N.mangle == 1 && rnd_n(7) + 1 <= N.ai && !noon) N.mangle = 2;
  if (!(N.forced & 2) && N.mangle == 1 && N.hour == 5 && cam != 4 && mon) N.mangle = 2, N.forced |= 2;
  if (N.mangle == 2 && mon && cam == 4) N.mangle_stare++;
  if (!mon) {
    N.mangle_stare = 0;
    if (N.mangle == 2) N.mangle = 1;
  }
  if (N.mangle == 2 && N.mangle_stare >= N.tlimit) {
    N.mangle = 0, N.mangle_win = 1;
    monitor_down();
    N.audio = -10;
    caption("Mangle's radio garbles");
  }
  if (N.mangle_win) {   /* up 100 px at 5 (160 frames), half a second, down at 50 (16 frames) */
    if (++N.mangle_win > 160 + 30 + 16) N.mangle_win = 0, N.breath = 500;
    else if (N.mangle_win % 60 == 0) caption("Mangle's radio garbles");
  }
  /* the Puppet [515-516, 558, 563, 569, 571-584] */
  if (every(20000) && cam != 8 && N.puppet == 1 && rnd_n(10) + 1 <= N.ai && !noon) N.puppet = 2;
  if (!(N.forced & 4) && N.puppet == 1 && N.hour == 5 && cam != 8) N.puppet = 2, N.forced |= 4;
  if (N.puppet == 2 && mon && cam == 8) N.puppet_stare++;
  if (!mon) {
    N.puppet_stare = 0;
    if (N.puppet == 2) N.puppet = 1;
  }
  if (N.puppet == 2 && N.puppet_stare >= N.tlimit && !N.cooldown) {
    N.puppet = 0, N.head_t = 500, N.head_out = 1, N.head_x = 160;
    monitor_down(), panel_down();
    caption("A mask moves over you");
  }
  if (N.head_t > 0) {
    N.head_t--, N.black2 = 100;
    if (N.black > 210) N.head_x += N.head_x < 160 ? (160 - N.head_x < 13 ? 160 - N.head_x : 13) : 0;
    if (every(250)) N.head_anim = (uint8_t)rnd_n(3);
  } else if (N.head_out) {
    N.head_x = (int16_t)(N.head_x - 8);
    if (N.head_x <= -230) N.head_out = 0;
  }
  if (!N.head_t && N.black2 > 0) N.black2--;
  /* Chica [517-518, 559, 570, 585-590, 592] */
  if (every(20000) && N.chica == 1 && cam != 7 && rnd_n(10) + 1 <= N.ai && !noon) N.chica = 2;
  if (!(N.forced & 8) && N.chica == 1 && N.hour == 5 && cam != 7) N.chica = 2, N.forced |= 8;
  if (N.chica == 2 && mon && cam == 7) N.chica_stare++;
  if (!mon && N.chica == 2) N.chica = 1;
  if (N.chica == 1 && N.chica_stare >= N.tlimit && !N.cooldown && !N.chica_in) N.chica_in = 1, N.chica = 3;
  if (N.chica_in == 1) {
    if (N.x > 512) N.x = (int16_t)(N.x - 30 < 512 ? 512 : N.x - 30);
    if (N.x < 600) N.chica_in = 2, start_scare(PH_CHICA), caption("Chica screams");
  }
  /* Phantom Freddy [519-520, 541-546, 551, 530, 554, 556] */
  if (every(60000) && N.freddy == 1 && rnd_n(12) + 1 <= N.ai && !noon) N.freddy_pending = 1;
  if (!(N.forced & 16) && N.freddy == 1 && N.hour == 4) N.freddy_pending = 1, N.forced |= 16;
  if (N.freddy_pending && !N.fwalk && !N.flunge && N.freddy == 1 && every(1000) && rnd_n(2)) N.fwalk = 1, N.fwalk_t = 0;
  if (N.fwalk) {
    if (++N.fwalk_t >= 1955) N.fwalk = 0, N.freddy_pending = 0;   /* (733 px at 3/8 px a frame: past, safe) */
    if (screen_up()) N.fwalk_stare = 0;
    else N.fwalk_stare++;
    if (N.fwalk && N.fwalk_stare >= 3 * N.tlimit && !N.cooldown) N.flunge = 1;
  }
  if (N.flunge && ++N.flunge > 28) N.flunge = 0, N.fwalk = 0, N.fscare_ready = 1;
  if (N.fscare_ready && every(1000) && rnd_n(2) && !N.cooldown) {
    N.fscare_ready = 0, N.freddy_pending = 0;
    start_scare(PH_FREDDY);
    caption("Phantom Freddy screams");
  }
}

static void foxy_step(void) {   /* [709-713, 588, 602, 606] */
  static const uint16_t chance[7] = {0, 0, 1000, 100, 50, 10, 10};
  if (N.night >= 2 && N.panel == 1 && N.panel_anim == 1 && N.frame > 600) N.foxy = rnd_n(chance[N.night]) == 0;
  if (N.foxy && !N.foxy_done && N.x < 568 && !N.cooldown && !screen_up() && N.panel == 0) {
    N.foxy_done = 1, N.foxy = 0;
    start_scare(PH_FOXY);
    caption("Foxy screams");
  }
}

/* the phantoms' scares, played: their pictures, then the white flash */
static void scare_step(void) {
  if (!N.scare) return;
  int len = N.scare == PH_BB ? 30 + 17 : N.scare == PH_CHICA ? 20 : N.scare == PH_FOXY ? 23 : 17;
  if (++N.scare_t >= len) {
    if (N.scare == PH_CHICA) N.chica = 0, N.chica_in = 0;
    if (N.scare == PH_FREDDY) N.freddy = 0;
    white_flash();
    N.frozen = 0;
  }
}

static void ventilation(void) {
  if (err(N.vent_h)) N.vent_err++;
  else N.vent_err = 0;
  int ai = N.ai;
  bool was = N.hallucinating;
  if (N.vent_err > 1000 - 100 * ai) N.halluc_t = (int16_t)(200 + rnd_n(200));
  if (N.halluc_t > 0) {
    N.red = 50, N.hallucinating = 1;
    if (every(250)) {
      N.red_on ^= 1;
      if (N.red_on && N.alarm > 0) caption("The ventilation alarm");
    }
    N.halluc_t = (int16_t)(N.halluc_t - 2);
  } else {
    N.hallucinating = 0;
    if (N.red) N.red--;
    else N.red_on = 0;
  }
  if (N.halluc_t > 0 && N.alarm == 0 && !was) N.alarm = 400;
  if (N.alarm > 0) N.alarm--;
  if (N.hallucinating) {
    N.halluc = 0;
    for (int c = 0; c < 10; c++)
      if (rnd_n(3) == 1) N.halluc |= (uint16_t)(1 << c);
  } else {
    N.halluc = 0;
  }
  if (N.hallucinating != was) N.static_s4 = (int16_t)(was ? 200 + rnd_n(100) : 200);
  /* the blackout [344-353] */
  bool dark = N.vent_err > 2000 - 200 * ai;
  if (dark) {
    if (!N.black_up) {
      if (++N.black > 255) N.black_up = 1;
    } else if (--N.black <= 100) N.black_up = 0;
  }
  if (N.black2 > 0) {
    if (!N.black_up) {
      if ((N.black += 5) > 255) N.black_up = 1;
    } else if ((N.black -= 5) <= 150) N.black_up = 0;
  }
  if (!dark && !N.black2 && N.black > 0) N.black_up = 0, N.black--;
  if (N.black > 250) blackout_peak();
}

static void wear(void) {
  int ai = N.ai;
  if (N.viewing >= 2 && every(1000) && ++N.cam_wear >= 12) N.cam_wear = 0, N.camera = (int8_t)clampi(N.camera - ai, -10, 0);
  static const uint8_t vent_s[8] = {0, 0, 12, 10, 9, 8, 0, 0};
  if (ai < 8 && vent_s[ai] && every(vent_s[ai] * 1000)) N.vent_h = (int8_t)clampi(N.vent_h - 1, -10, 0);
  if (every(1000)) {
    if (screen_up()) N.idle = 0;
    else if (N.idle < 255) N.idle++;
    if (N.idle > 10 && N.night != 1) N.vent_h = (int8_t)clampi(N.vent_h - 1, -10, 0);
  }
  if (screen_up()) N.idle = 0;
  if (g.cheats & CH_NOERRORS) N.audio = N.camera = N.vent_h = 0;
}

static void reboot(void) {
  if (!N.rebooting) return;
  if (every(N.rebooting == 4 ? 2000 : 1000)) N.reboot_p = (uint8_t)(N.reboot_p + 1 + rnd_n(2));
  if (N.reboot_p >= 10) {
    if (N.rebooting == 1 || N.rebooting == 4) N.audio = 0;
    if (N.rebooting == 2 || N.rebooting == 4) N.camera = 0;
    if (N.rebooting == 3 || N.rebooting == 4) N.vent_h = 0;
    if (N.rebooting == 4) N.cam_wear = 0;
    N.rebooting = 0, N.reboot_p = 0;
  }
}

static void statics(void) {
  if (every(500)) N.static_s1 = (uint8_t)(rnd_n(2) * 5);
  if (every(360) && rnd_n(5) == 0) N.static_s4 = 100;
  if (every(1000) && rnd_n(10) == 0) N.static_s4 = 200;
  if (N.static_s5 > 0) {
    N.static_s5--;
    if (you_in() == spring_cam()) N.static_s4 = 200;
    for (int i = 0; i < N.trail_n; i++)
      if (N.trail_at[i] == you_in()) N.static_s4 = 250;
  }
  N.static_s4 = (int16_t)(N.static_s4 > 10 ? N.static_s4 - 12 : N.static_s4 - 2);
  if (N.static_s4 < 0) N.static_s4 = 0;
  /* the trail [182-184]: where he was, for a second or two */
  if (every(3000) && N.trail_n < 3 && spring_cam()) N.trail_at[N.trail_n] = (uint8_t)spring_cam(), N.trail_age[N.trail_n++] = 0;
  if (every(1000)) {
    for (int i = 0; i < N.trail_n;) {
      if (++N.trail_age[i] >= 2) {
        N.trail_n--;
        N.trail_at[i] = N.trail_at[N.trail_n], N.trail_age[i] = N.trail_age[N.trail_n];
      } else {
        i++;
      }
    }
  }
}

static void office_visuals(void) {
  if (N.loc == L_ST1 && screen_up()) N.window = 1;
  if (N.loc != L_ST1) N.window = 0;
  if (N.loc == L_ST2 && !N.ran_past) N.ran_past = 1;
  else if (N.ran_past && N.ran_past < 255 && ++N.ran_past > 31) N.ran_past = 255;
  if (N.loc == L_ST3 && N.panel_dropping && !N.hid) N.hid = 1;
  else if (N.hid && N.hid < 255 && ++N.hid > 27) N.hid = 255;
  if (N.loc == L_ST4 && screen_up()) N.head = 1;
  if (N.loc != L_ST4) N.head = 0;
  if (N.latch && screen_up()) N.latch = 0, go(L_ST2);   /* [190] */
}

static void nearby(void) {
  int l = N.loc, v = 0;
  if (l == 1 || l == 2 || (l >= L_ST1 && l <= L_GOT2)) v = 2;
  else if (l == 3 || l == 4 || l == 5 || l == 13) v = 1;
  if ((N.fwalk || N.chica_in) && v < 1) v = 1;
  N.nearby = (uint8_t)v;
}

static void attack(void) {
  if (N.loc == L_GOT && !N.got_you) N.got_you = 1;
  if (N.loc == L_GOT2 && !N.got_you) {
    if (N.panel >= 2) go(L_GOT);
    else if (N.viewing >= 2 && every(1000) && rnd_n(2)) N.got_you = 2;
  }
  if (!N.got_you) return;
  if (N.got_you == 2) monitor_down();
  panel_down();
  N.bb_shown = 0, N.chica_in = 0, N.fwalk = 0, N.foxy = 0, N.head_t = 0, N.head_out = 0, N.mangle_win = 0;
  if (N.got_you == 1 && N.x > 512) N.x = (int16_t)(N.x - 20 < 512 ? 512 : N.x - 20);
  if (++N.death_t == 1) caption("Springtrap screams");
  if (N.death_t >= 37 + 4) {   /* the lunge (11 pictures at 30), then 5 frames: the static */
    g.scene = SC_STATIC, g.t = 0;
  }
}

void night_step(void) {
  N.frame++;
  input();
  screens();
  /* Springtrap's move [135-140] */
  if (N.night > 1) {
    if (every(15000)) {
      N.aggressive = 0;
      if (rnd_n(5) < N.ai) N.aggressive = 1;   /* [505-506] */
    }
    if (every(1000)) {
      N.moves = (uint8_t)(N.moves + ((g.cheats & CH_AGGRESSIVE) ? 2 : 1));
      if ((int)N.moves > (10 - N.ai - N.aggressive) + (int)rnd_n(15) - N.turns) {
        N.moves = 0;
        N.action = (uint8_t)(rnd_n(3 + N.aggressive) + 1);
        if (N.action == 1) N.action = 0, N.turns++;
      }
    }
    /* the walk cue [141]: on the step the action turns > 1 */
    bool walk = N.action > 1;
    if (walk && !N.walk_was && !N.window && !N.head)
      N.static_s5 = (int16_t)(50 + rnd_n(100)), N.static_s4 = (int16_t)(N.static_s4 + 100), caption("Footsteps");
    N.walk_was = walk;
    room_graph();
    office_visuals();
    /* the lure's move [245-255] */
    if (N.lure_to && --N.lure_t <= 0) {
      N.loc = N.lure_to, N.lure_to = 0, N.static_s5 = (int16_t)(50 + rnd_n(100)), N.moves = 0;
    }
  }
  if (N.sdfg) N.sdfg--;
  if (every(1500) && N.play < 7) N.play++;
  reboot();
  wear();
  ventilation();
  vent_moves();
  /* sealing [436-439] */
  if (N.sealing && !N.vents_map) N.sealing = 0;
  if (N.sealing && --N.seal_t <= 0) N.closed = N.sealing, N.sealing = 0;
  /* the hours [485-491] */
  int len = (N.night == 1 ? 30000 : 60000) / ((g.cheats & CH_FAST) ? 2 : 1);
  if (every(len)) N.hour++;
  if (N.hour >= 6) {
    g.scene = SC_SIX, g.t = 0;
    return;
  }
  static const uint8_t AI[7] = {0, 0, 2, 3, 4, 5, 7}, TL[7] = {0, 100, 50, 40, 30, 20, 20};
  N.ai = AI[N.night > 6 ? 6 : N.night], N.tlimit = TL[N.night > 6 ? 6 : N.night];
  attack();
  if (g.scene != SC_NIGHT) return;
  phantoms();
  foxy_step();
  scare_step();
  if (every(1000) && N.cooldown) N.cooldown--;
  if (N.flash > 0) N.flash--;
  if (N.breath > 0 && every(30)) N.breath--;
  /* the forced move after a phantom (nights 3 on) [719-726, 753-755] */
  if (N.frozen && !N.frozen_was && N.night >= 3) N.force_move = 1, N.target = (uint8_t)(rnd_n(3) + 1);
  N.frozen_was = N.frozen;
  if (N.force_move == 1) {
    if (N.loc >= 6 && N.loc <= 10) N.force_move = 2;
    else if (N.loc == 5) N.loc = 2, N.force_move = 0;
    else if (N.loc == 2) N.loc = L_ST1, N.force_move = 0;
  }
  if (N.force_move == 2) {
    N.loc = N.target == 1 ? 5 : N.target == 2 ? 10 : 4;
    if (N.target != 1) N.moves = 0;
    N.force_move = 0;
  }
  /* pic random [330]: rolled again every 10 s while you look elsewhere */
  if (every(10000) && you_in() != spring_cam()) N.pic_random = (uint8_t)rnd_n(2);
  /* aggressive [288, 595-596, 730, 735, 743] */
  if (err(N.vent_h) || N.frozen || N.breath > 0 || N.idle > 10 || N.hour >= 3) N.aggressive = 1;
  if (N.hour == 0) N.aggressive = 0;
  statics();
  nearby();
  if (N.night == 1) N.loc = L_OFF;
}
