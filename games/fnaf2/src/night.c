/* A night of Five Nights at Freddy's 2, 60 steps a second, as the game plays it.
 *
 * The AI levels are set at given hours (they don't rise each hour as in the first game), and the game caps them
 * at 15 (Withered Foxy 17, Golden Freddy 10). Every 5 seconds each animatronic has a movement opportunity: a
 * number from 1 to 20, and it moves one place along its path if its level is that number or more. From the
 * last place before the office (the hallway, a vent or a vent's blind spot) it comes in the next time the
 * monitor is up, unless the Freddy mask is on at its next opportunity: then it goes back. Once in, it shows
 * when the monitor comes down: the mask at once and it leaves, or it gets you the next time the monitor comes
 * down or the mask comes off. Mangle stays on the ceiling, Balloon Boy takes the lights. Withered Foxy waits in
 * the hallway for the flashlight, Golden Freddy appears, and the Puppet comes for anyone who lets the music box
 * run down. The numbers are in the game's events, as the players who read them wrote them down: see the
 * README's Credits. */
#include <string.h>
#include "game.h"
#include "plat.h"

#define N g.n
#define STUN 400            /* frames a camera's light holds an animatronic (6.66 s) */
#define WINDOW 1.0f         /* seconds to put the mask on, once a visitor shows */
#define MASKED_STAY 4.0f    /* a visitor sent off by the mask stays this long first */
#define WIND_RATE (BOX_FULL / 6.0f)

const uint8_t CUSTOM_ORDER[10] = {WF, WB, WC, FOXY, BB, TF, TB, TC, MANGLE, GF};
const char *const BOT_NAMES[BOTS] = {"Toy Freddy", "Toy Bonnie", "Toy Chica", "Mangle", "Balloon Boy",
                                     "Withered Freddy", "Withered Bonnie", "Withered Chica", "Withered Foxy",
                                     "Golden Freddy", "The Puppet"};

/* the walkers' paths to the office, and where each goes back to when sent away */
static const uint8_t PATH[WALKERS][10] = {
  [TF] = {L_STAGE, L_GAME, L_HALL_FAR, L_HALL},
  [TB] = {L_STAGE, L_PR3, L_PR4, L_PR2, L_RVENT, L_RSPOT},
  [TC] = {L_STAGE, L_MAIN, L_HALL, L_PR1, L_LVENT, L_LSPOT},
  [MANGLE] = {L_KIDS, L_PRIZE, L_GAME, L_MAIN, L_HALL, L_PR1, L_PR2, L_RVENT, L_RSPOT},
  [BB] = {L_GAME, L_UNSEEN, L_LVENT, L_LSPOT},
  [WF] = {L_PARTS, L_MAIN, L_PR3, L_HALL},
  [WB] = {L_PARTS, L_MAIN, L_HALL, L_PR1, L_LVENT},
  [WC] = {L_PARTS, L_PR4, L_PR2, L_RVENT},
};
static const uint8_t PATH_LEN[WALKERS] = {4, 6, 6, 9, 4, 4, 5, 4};
static const uint8_t BACK_TO[WALKERS] = {0, 1, 1, 3, 0, 1, 1, 1};
/* who leaves the Show Stage or Parts/Service only once the other has: Toy Bonnie, Toy Chica, then Toy Freddy;
   Withered Bonnie, Chica, then Freddy */
static const int8_t LEAVES_AFTER[WALKERS] = {[TF] = TC, [TB] = -1, [TC] = TB, [MANGLE] = -1, [BB] = -1,
                                             [WF] = WC, [WB] = -1, [WC] = WB};
/* who comes in first when several are waiting */
static const uint8_t ENTRY_ORDER[WALKERS] = {WF, WB, WC, TF, TB, TC, MANGLE, BB};

/* The AI levels at the hours the game sets them: the night, the hour, then Toy Freddy, Toy Bonnie, Toy Chica,
 * Mangle, Balloon Boy, Withered Freddy, Bonnie, Chica, Foxy, Golden Freddy and the Puppet (-1: as it was; Foxy
 * -2: not there at all) */
static const int8_t AI_TABLE[][2 + BOTS] = {
  {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, -2, 0, 1},
  {1, 1, 0, 2, 2, 0, 0, 0, 0, 0, -1, -1, -1},
  {1, 2, 2, 3, 2, 0, 0, 0, 0, 0, -1, -1, -1},
  {2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5},
  {2, 1, 2, 3, 3, 3, 3, 0, 0, 0, 1, -1, -1},
  {3, 0, 0, 0, 0, 0, 1, 0, 1, 1, 2, 0, 8},
  {3, 1, 0, 1, 1, 0, 2, 2, 2, 2, 3, -1, -1},
  {4, 0, 0, 0, 0, 5, 3, 0, 1, 0, 7, 0, 9},
  {4, 2, 0, 1, 0, 5, 3, 3, 4, 4, 7, -1, -1},
  {5, 0, 5, 2, 2, 1, 5, 2, 2, 2, 5, 0, 10},
  {5, 1, 1, 2, 2, 10, 5, 5, 5, 5, 7, -1, -1},
  {6, 0, 0, 0, 0, 3, 5, 5, 5, 5, 10, 0, 15},
  {6, 2, 5, 5, 5, 10, 9, 10, 10, 10, 15, 3, -1},
};
/* Golden Freddy's chance, at 12 AM, to be there at level 1: nights 2 to 6 (1 in this) */
static const uint16_t GOLDEN_ODDS[7] = {0, 0, 1000, 1000, 100, 100, 10};

static bool pressed(uint32_t k) { return (g.pressed & k) != 0; }
static bool held(uint32_t k) { return (g.held & k) != 0; }
static bool monitor_up(void) { return N.flip >= 1; }
static bool mask_on(void) { return N.mask && N.mask_anim >= 1; }
static int roll(void) { return 1 + (int)(rnd() % 20); }

int ai_level(int who) {
  int a = N.ai[who], cap = who == FOXY ? 17 : who == GF ? 10 : 15;
  return a < 0 ? a : a > cap ? cap : a;
}

void apply_hour(int night, int hour) {
  for (unsigned r = 0; r < sizeof AI_TABLE / sizeof AI_TABLE[0]; r++)
    if (AI_TABLE[r][0] == night && AI_TABLE[r][1] == hour)
      for (int k = 0; k < BOTS; k++)
        if (AI_TABLE[r][2 + k] != -1) N.ai[k] = AI_TABLE[r][2 + k] == -2 ? -1 : AI_TABLE[r][2 + k];
}

int battery_frames(int night) {
  static const int16_t frames[8] = {0, 7000, 6000, 5000, 4000, 3000, 3000, 3000};
  return frames[night < 1 ? 1 : night > 7 ? 7 : night];
}

int battery_bars(void) {
  float left = battery_frames(N.night) - N.battery, fifth = battery_frames(N.night) / 5.0f;
  if (left <= 0) return 0;
  int b = (int)(left / fifth + 0.9999f) - 1;
  return b < 0 ? 0 : b > 4 ? 4 : b;
}

/* the music box's drain a second: 20 times the night, none on the first night before 2 AM, the custom night as
 * the 6th */
float box_drain(int night, int hour) {
  if (night == 1) return hour < 2 ? 0 : 40;
  return 20.0f * (night > 6 ? 6 : night);
}

bool box_warning(void) { return N.box <= BOX_FULL / 5 && N.puppet < 3; }

bool lights_work(void) { return !N.bb_in && N.blackout <= 0; }
static bool battery_left(void) { return N.battery < battery_frames(N.night); }

int bot_at(int who) {
  if (who < WALKERS) {
    if ((who == MANGLE && N.mangle_in) || (who == BB && N.bb_in)) return L_OFFICE;
    if (N.occ == who && N.occ_state != OCC_NONE) return L_OFFICE;
    return PATH[who][N.step[who]];
  }
  if (who == FOXY) return N.foxy_hall ? L_HALL : L_PARTS;
  if (who == PUPPET) return N.puppet < 4 ? L_PRIZE : N.puppet_at;
  return N.gf == GF_HALL ? L_HALL : N.gf == GF_SHOWN || N.gf == GF_FADING ? L_OFFICE : -1;
}

void night_begin(int night) {
  memset(&N, 0, sizeof N);
  N.night = (uint8_t)night;
  if (night <= 6) {
    apply_hour(night, 0);
    if (GOLDEN_ODDS[night] && rnd() % GOLDEN_ODDS[night] == 0) N.ai[GF] = 1;
  } else {
    for (int k = 0; k < 10; k++) N.ai[CUSTOM_ORDER[k]] = g.custom[k];
    N.ai[PUPPET] = 15;
  }
  N.box = BOX_FULL;
  N.pan = OFFICE_PAN / 2;
  N.want = -1;
  N.cam = L_STAGE;
  N.occ = 0xFF;
  for (int k = 0; k < BOTS; k++) N.move_t[k] = 5;
  N.move_t[PUPPET] = 1;
  N.foxy_ten = 10;
}

/* ------------------------------------------------------------------ the end of a night */
static void scare(int who) {
  g.scene = SC_SCARE;
  g.scare = (uint8_t)who;
  g.scare_t = 0;
  N.monitor = 0;
  N.flip = 0;
  N.mask = 0;
  N.mask_anim = 0;
}

static void six_am(void) {
  g.scene = SC_SIX;
  g.t = 0;
}

/* ------------------------------------------------------------------ the walkers */
static const char *vent_side(int loc) {
  return loc == L_LVENT || loc == L_LSPOT ? "left" : loc == L_RVENT || loc == L_RSPOT ? "right" : NULL;
}

static void send_back(int who) {
  int from = PATH[who][N.step[who]];
  N.step[who] = BACK_TO[who];
  N.ready[who] = 0;
  N.pending[who] = 0;
  if (vent_side(from)) caption(from == L_LSPOT || from == L_LVENT ? "*clattering: the left vent empties*"
                                                                  : "*clattering: the right vent empties*");
  if (N.flip >= 1 && N.cam == PATH[who][N.step[who]]) N.glitch = 1;
}

static void moved(int who, int from, int to) {
  if (N.flip >= 1 && (N.cam == from || N.cam == to)) N.glitch = 1;   /* the feed breaks up */
  if (to == L_LVENT || to == L_LSPOT) caption("*banging in the left vent*");
  if (to == L_RVENT || to == L_RSPOT) caption("*banging in the right vent*");
  if (who == BB) {
    static const char *const says[3] = {"*a child's voice: \"Hello?\"*", "*a child's voice: \"Hi!\"*",
                                        "*a child laughs*"};
    caption(says[rnd() % 3]);
  }
}

static void advance(int who) {
  int len = PATH_LEN[who];
  if (N.step[who] == len - 1) {   /* at the last place: in, at the next monitor, unless the mask is on */
    if (mask_on()) send_back(who);
    else N.ready[who] = 1;
    return;
  }
  int from = PATH[who][N.step[who]];
  N.step[who]++;
  moved(who, from, PATH[who][N.step[who]]);
}

static bool in_office(int who) {
  return (who == MANGLE && N.mangle_in) || (who == BB && N.bb_in) || (N.occ == who && N.occ_state != OCC_NONE);
}

static void walkers(void) {
  for (int who = 0; who < WALKERS; who++) {
    if (N.stun[who] > 0 && --N.stun[who] == 0 && N.pending[who]) {
      N.pending[who] = 0;
      if (!in_office(who)) advance(who);
    }
    N.move_t[who] -= DT;
    if (N.move_t[who] > 0) continue;
    N.move_t[who] += 5;
    int a = ai_level(who);
    if (a <= 0 || in_office(who) || roll() > a) continue;
    /* the Withereds stay in Parts/Service while its camera is watched */
    if (who >= WF && N.step[who] == 0 && monitor_up() && N.cam == L_PARTS) continue;
    /* the Show Stage and Parts/Service empty in the game's order (the custom night, without their cameras, doesn't
       keep it) */
    if (N.night < 7 && N.step[who] == 0 && LEAVES_AFTER[who] >= 0 && N.step[LEAVES_AFTER[who]] == 0) continue;
    if (N.stun[who]) N.pending[who] = 1;
    else advance(who);
  }
}

/* the monitor up: whoever is waiting comes in */
static void entries(void) {
  if (!monitor_up()) return;
  for (int i = 0; i < WALKERS; i++) {
    int who = ENTRY_ORDER[i];
    if (!N.ready[who]) continue;
    if (who == MANGLE) {
      N.mangle_in = 1, N.ready[who] = 0;
      caption("*radio static, overhead*");
    } else if (who == BB) {
      N.bb_in = 1, N.ready[who] = 0;
      caption("*a child laughs, in the office*");
    } else if (N.occ_state == OCC_NONE && N.gf == GF_NONE) {
      N.occ = (uint8_t)who, N.occ_state = OCC_IN, N.ready[who] = 0;
    }
  }
}

/* ------------------------------------------------------------------ the office's visitor */
static void doomed(void) {
  N.occ_state = OCC_DOOMED;
  N.blackout = 1;
  N.force_t = 2 + (float)(rnd() % 400) / 100;
}

static void visitor(void) {
  switch (N.occ_state) {
  case OCC_SHOWN:
    N.occ_t += DT;
    N.flicker = 0.2f;
    if (N.mask) N.occ_state = OCC_MASKED, N.occ_t = 0;
    else if (N.occ_t >= WINDOW) doomed();
    break;
  case OCC_MASKED:
    N.occ_t += DT;
    N.flicker = 0.2f;
    if (!N.mask) scare(N.occ);
    else if (N.occ_t >= MASKED_STAY) N.occ_state = OCC_LEAVING, N.blackout = 1;
    break;
  case OCC_LEAVING:
    if (N.blackout <= 0) {
      int who = N.occ;
      N.occ_state = OCC_NONE, N.occ = 0xFF;
      send_back(who);
    }
    break;
  case OCC_DOOMED:
    if (monitor_up() || N.mask) {   /* left alone, it pulls the monitor down or the mask off */
      N.force_t -= DT;
      if (N.force_t <= 0) {
        if (N.mask) scare(N.occ);
        else N.monitor = 0;
      }
    }
    break;
  }
}

/* ------------------------------------------------------------------ Withered Foxy */
static void foxy(bool flash_on, bool flash_new) {
  if (N.ai[FOXY] < 0) return;
  bool threats = N.occ_state != OCC_NONE || N.gf != GF_NONE;
  for (int k = 0; k < WALKERS; k++) threats |= N.ready[k] != 0;
  if (N.blackout <= 0) N.foxy_d += DT * (mask_on() && !threats ? 2 : 1);
  if (flash_on) {
    if (N.foxy_hall) {
      if (N.foxy_got) {
        scare(FOXY);
        return;
      }
      N.foxy_d = 0;
      if (flash_new) N.foxy_flashes++;
      N.foxy_hold += DT;
      if (N.foxy_hold >= 1) N.foxy_hold -= 1, N.foxy_flashes++;
      if (N.foxy_flashes >= 5) N.foxy_off = 1;
    }
  } else
    N.foxy_hold = 0;
  /* every half second: the light down the hall while he is in Parts/Service takes 1 off D */
  N.foxy_dim += DT;
  if (N.foxy_dim >= 0.5f) {
    N.foxy_dim -= 0.5f;
    if (flash_on && !N.foxy_hall) N.foxy_d = N.foxy_d > 1 ? N.foxy_d - 1 : 0;
  }
  N.move_t[FOXY] -= DT;
  if (N.move_t[FOXY] <= 0) {
    N.move_t[FOXY] += 5;
    if (!N.foxy_off && 21 + (int)(rnd() % 5) - (int)N.foxy_d < ai_level(FOXY)) {
      if (!N.foxy_hall) {
        N.foxy_hall = 1, N.foxy_flashes = 0, N.foxy_hold = 0;
        if (monitor_up() && N.cam == L_PARTS) N.glitch = 1;
      } else
        N.foxy_got = 1;
    }
  }
  N.foxy_ten -= DT;
  if (N.foxy_ten <= 0) {
    N.foxy_ten += 10;
    if (N.foxy_got && N.blackout <= 0) scare(FOXY);
  }
}

/* ------------------------------------------------------------------ the music box and the Puppet */
static void puppet(bool wind) {
  N.winding = wind;
  if (wind) N.box = N.box + WIND_RATE * DT > BOX_FULL ? BOX_FULL : N.box + WIND_RATE * DT;
  else {
    N.box -= box_drain(N.night, N.hour) * DT;
    if (N.box < 0) N.box = 0;
  }
  if (!N.warned && box_warning()) N.warned = 1;
  if (N.box > BOX_FULL / 5) N.warned = 0;
  if (N.puppet < 3 && N.box >= BOX_FULL) N.puppet = 0;
  if (N.stun[PUPPET] > 0) N.stun[PUPPET]--;
  N.move_t[PUPPET] -= DT;
  if (N.move_t[PUPPET] > 0) return;
  N.move_t[PUPPET] += 1;
  int a = ai_level(PUPPET);
  if (N.puppet < 3 && N.box > 0) return;   /* wound in time: he stays */
  if (N.stun[PUPPET] || (int)(rnd() % 20) > a) return;
  if (N.puppet < 4) {
    if (++N.puppet == 4) {
      N.puppet_at = L_MAIN;
      caption("*a jack-in-the-box tune starts*");
      if (monitor_up() && (N.cam == L_PRIZE || N.cam == L_MAIN)) N.glitch = 1;
    }
    return;
  }
  /* on his way: the Main Hall, a party room, Party Room 2, the hallway, the office */
  int from = N.puppet_at, to;
  static const uint8_t rooms[3] = {L_PR3, L_PR1, L_PR4};
  switch (from) {
  case L_MAIN: to = rooms[rnd() % 3]; break;
  case L_PR1: case L_PR3: case L_PR4: to = L_PR2; break;
  case L_PR2: to = L_UNSEEN; break;
  default: to = L_OFFICE; break;
  }
  N.puppet_at = (uint8_t)to;
  if (monitor_up() && (N.cam == from || N.cam == to)) N.glitch = 1;
  if (to == L_OFFICE) scare(PUPPET);
}

/* ------------------------------------------------------------------ Golden Freddy */
static void golden(bool flash_on) {
  switch (N.gf) {
  case GF_NONE:
    N.move_t[GF] -= DT;
    if (N.move_t[GF] > 0) break;
    N.move_t[GF] += 5;
    if (ai_level(GF) <= 0 || N.occ_state != OCC_NONE || N.blackout > 0 || roll() > ai_level(GF)) break;
    if (N.flip <= 0) N.gf = GF_HALL, N.gf_t = N.gf_lit = 0;
    else if (rnd() % 100 < 60) N.gf = GF_COMING;
    break;
  case GF_HALL:
    N.gf_t += DT;
    if (flash_on) N.gf_lit += DT;
    if (N.gf_lit >= 0.5f) scare(GF);
    else if (N.gf_t >= 10) N.gf = GF_NONE;
    break;
  case GF_SHOWN:
    N.gf_t += DT;
    N.flicker = 0.2f;
    if (N.mask) N.gf = GF_FADING, N.gf_t = 0;
    else if (N.gf_t >= WINDOW) scare(GF);
    break;
  case GF_FADING:
    N.gf_t += DT;
    if (N.gf_t >= 1.5f) N.gf = GF_NONE;
    break;
  }
}

/* ------------------------------------------------------------------ the monitor and the mask */
static void monitor_moves(void) {
  bool was_up = N.flip >= 1, was_down = N.flip <= 0;
  N.flip += (N.monitor ? 1 : -1) * DT / 0.25f;
  N.flip = N.flip < 0 ? 0 : N.flip > 1 ? 1 : N.flip;
  if (!was_up && N.flip >= 1) {   /* up */
    g.blip = 0.12f;
    if (N.foxy_off) N.foxy_hall = N.foxy_off = N.foxy_got = 0, N.foxy_d = 0, N.foxy_flashes = 0;
    if (N.gf == GF_HALL) N.gf = GF_NONE;
  }
  if (!was_down && N.flip <= 0) {   /* down: who is in the office shows */
    N.glitch = 0;
    if (N.occ_state == OCC_IN) N.occ_state = OCC_SHOWN, N.occ_t = 0;
    else if (N.occ_state == OCC_DOOMED) {
      scare(N.occ);
      return;
    }
    if (N.gf == GF_COMING) N.gf = GF_SHOWN, N.gf_t = 0;
    if (N.mangle_in && rnd() % 3 == 0) scare(MANGLE);
  }
}

static void mask_moves(void) {
  N.mask_anim += (N.mask ? 1 : -1) * DT / 0.25f;
  N.mask_anim = N.mask_anim < 0 ? 0 : N.mask_anim > 1 ? 1 : N.mask_anim;
}

/* ------------------------------------------------------------------ keys */
static bool side_in_view(int side) { return side == 0 ? N.pan <= 30 : N.pan >= OFFICE_PAN - 30; }

/* returns the lights held this step: bit 0 the hallway, bit 1 the left vent, bit 2 the right vent, bit 3 the
 * camera's light, bit 4 winding */
static int keys(void) {
  int lights = 0;
  bool office = N.flip <= 0 && !N.monitor;
  if (office) {
    if (pressed(K_DOWN | K_DIGIT(2)) && (N.mask_anim <= 0 || N.mask_anim >= 1)) N.mask ^= 1;
    if (pressed(K_OK) && !N.mask && N.mask_anim <= 0) {
      N.monitor = 1;
      N.want = -1;
      return 0;
    }
    float target = 0;
    if (held(K_LEFT | K_DIGIT(4))) target = -380, N.want = -1;
    else if (held(K_RIGHT | K_DIGIT(6))) target = 380, N.want = -1;
    if (!N.mask && N.mask_anim <= 0) {
      if (held(K_UP | K_DIGIT(5))) lights |= 1;
      for (int side = 0; side < 2; side++) {
        uint32_t k = side ? K_DIGIT(9) | K_DIGIT(3) : K_DIGIT(7) | K_DIGIT(1);
        if (pressed(k)) N.want = (int8_t)side;
        if (held(k)) {
          if (side_in_view(side)) lights |= 2 << side, N.want = -1;
          else if (N.want == side) target = side ? 720 : -720;
        }
      }
    }
    float dv = target - N.pan_v, step = 4200 * DT;
    N.pan_v += dv > step ? step : dv < -step ? -step : dv;
    N.pan += N.pan_v * DT;
    if (N.pan < 0) N.pan = 0, N.pan_v = 0;
    if (N.pan > OFFICE_PAN) N.pan = OFFICE_PAN, N.pan_v = 0;
    return lights;
  }
  if (N.flip < 1) return 0;
  if (pressed(K_OK | K_DOWN)) {
    N.monitor = 0;
    return 0;
  }
  int to = -1;
  if (pressed(K_LEFT)) to = (N.cam + CAM_COUNT - 1) % CAM_COUNT;
  if (pressed(K_RIGHT)) to = (N.cam + 1) % CAM_COUNT;
  for (int d = 2; d <= 9; d++)
    if (pressed(K_DIGIT(d))) to = d - 1;
  if (pressed(K_DIGIT(0))) to = L_GAME;
  if (pressed(K_DIGIT(1))) to = N.cam == L_PR1 ? L_PRIZE : N.cam == L_PRIZE ? L_KIDS : L_PR1;
  if (to >= 0 && to != N.cam) {
    N.cam = (uint8_t)to;
    N.glitch = 0;
    g.blip = 0.12f;
  }
  if (held(K_UP)) lights |= N.cam == L_PRIZE ? 16 : 8;
  return lights;
}

/* ------------------------------------------------------------------ time */
static void time_passes(void) {
  N.t += DT;
  N.hour_t += DT;
  if (N.hour_t < HOUR) return;
  N.hour_t -= HOUR;
  N.hour++;
  if (N.night <= 6) apply_hour(N.night, N.hour);
  if (N.hour >= 6) six_am();
}

static void sounds(void) {
  N.laugh_t += DT;
  if (N.laugh_t < 4) return;
  N.laugh_t -= 4;
  if (N.bb_in) caption("*Balloon Boy laughs*");
  int m = bot_at(MANGLE);
  if (m == L_HALL || m == L_RVENT || m == L_RSPOT || m == L_PR1 || m == L_PR2 || m == L_OFFICE)
    caption(m == L_OFFICE ? "*radio static, overhead*" : "*radio static, close by*");
}

void night_step(void) {
  time_passes();
  if (g.scene != SC_NIGHT) return;
  int lights = keys();
  monitor_moves();
  if (g.scene != SC_NIGHT) return;
  mask_moves();
  if (N.blackout > 0) N.blackout -= DT;
  if (N.flicker > 0) N.flicker -= DT;
  if (N.glitch > 0) N.glitch -= DT;
  /* the lights: the flashlight (the hallway, the camera's) uses the battery, the vent lights don't */
  bool can = lights_work(), batt = battery_left();
  N.hall_light = (lights & 1) && can && batt;
  N.vent_light = (lights & 2) && can ? 1 : (lights & 4) && can ? 2 : 0;
  N.cam_light = (lights & 8) && batt && N.blackout <= 0;
  if (N.hall_light || N.cam_light) N.battery += 1;
  bool flash_new = N.hall_light && !N.hall_was;
  N.hall_was = N.hall_light;
  if (N.cam_light) {   /* the camera's light holds who it falls on */
    for (int who = 0; who < WALKERS; who++) {
      int at = bot_at(who);
      if (at != N.cam || who == BB) continue;
      if (who >= WF && at == L_PARTS) continue;
      if (who <= TC && at == L_STAGE) continue;
      if (who == MANGLE && at == L_PRIZE) continue;
      N.stun[who] = STUN;
    }
    if (N.puppet >= 4 && N.puppet_at == N.cam) N.stun[PUPPET] = STUN;
  }
  puppet((lights & 16) != 0);
  if (g.scene != SC_NIGHT) return;
  walkers();
  entries();
  visitor();
  if (g.scene != SC_NIGHT) return;
  foxy(N.hall_light, flash_new);
  if (g.scene != SC_NIGHT) return;
  golden(N.hall_light);
  if (g.scene != SC_NIGHT) return;
  sounds();
}
