/* The rules of a night against the numbers the port follows (the game's, as its players read them out of its
 * events): the AI levels by night and hour and their caps, the music box's drain and winding, the flashlight's
 * battery, Withered Foxy's formula, the paths, the mask, a visitor in the office, Balloon Boy, Mangle, Golden
 * Freddy, the Puppet and the camera's light. Built with the sanitizers; prints each failure. */
#include <stdio.h>
#include <string.h>
#include "../src/game.h"
#include "../src/gfx.h"
#include "../src/res.h"

extern const uint8_t fnaf2_data[];
const uint8_t *fnaf2_bin;
static int failures, checks;
#define STUN_FRAMES 400

#define CHECK(c, ...)                                     \
  do {                                                    \
    checks++;                                             \
    if (!(c)) {                                           \
      failures++;                                         \
      printf("FAIL %s:%d: ", __FILE__, __LINE__);         \
      printf(__VA_ARGS__);                                \
      printf("\n");                                       \
    }                                                     \
  } while (0)

/* a night as it starts, everyone at 0 unless set */
static void begin(int night) {
  memset(&g, 0, sizeof g);
  g.running = true;
  night_begin(night);
  g.scene = SC_NIGHT;
  g.n.night = (uint8_t)night;
}
static void quiet(void) {
  for (int k = 0; k < BOTS; k++) g.n.ai[k] = 0;
  g.n.ai[FOXY] = -1;
}
static void step(uint32_t keys) { game_step(keys); }
static void steps(int n, uint32_t keys) {
  for (int i = 0; i < n && g.scene == SC_NIGHT; i++) step(keys);
}
static void tap(uint32_t k) {
  step(k);
  step(0);
}
static void monitor(bool up) {
  if ((g.n.flip >= 1) != up) tap(K_OK);
  steps(20, 0);
}

static void ai_table(void) {
  /* night, hour: Toy Freddy, Toy Bonnie, Toy Chica, Mangle, BB, W.Freddy, W.Bonnie, W.Chica, W.Foxy, Puppet */
  static const int8_t want[][12] = {
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, 1}, {1, 1, 0, 2, 2, 0, 0, 0, 0, 0, -1, 1}, {1, 2, 2, 3, 2, 0, 0, 0, 0, 0, -1, 1},
    {2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5},  {2, 1, 2, 3, 3, 3, 3, 0, 0, 0, 1, 5},  {3, 0, 0, 0, 0, 0, 1, 0, 1, 1, 2, 8},
    {3, 1, 0, 1, 1, 0, 2, 2, 2, 2, 3, 8},  {4, 0, 0, 0, 0, 5, 3, 0, 1, 0, 7, 9},  {4, 2, 0, 1, 0, 5, 3, 3, 4, 4, 7, 9},
    {5, 0, 5, 2, 2, 1, 5, 2, 2, 2, 5, 10}, {5, 1, 1, 2, 2, 10, 5, 5, 5, 5, 7, 10}, {6, 0, 0, 0, 0, 3, 5, 5, 5, 5, 10, 15},
    {6, 2, 5, 5, 5, 10, 9, 10, 10, 10, 15, 15}};
  static const uint8_t order[10] = {TF, TB, TC, MANGLE, BB, WF, WB, WC, FOXY, PUPPET};
  for (unsigned r = 0; r < sizeof want / sizeof want[0]; r++) {
    begin(want[r][0]);
    for (int h = 1; h <= want[r][1]; h++) apply_hour(want[r][0], h);
    for (int k = 0; k < 10; k++)
      CHECK(g.n.ai[order[k]] == want[r][2 + k], "night %d %d AM: %s is %d, not %d", want[r][0], want[r][1],
            BOT_NAMES[order[k]], g.n.ai[order[k]], want[r][2 + k]);
  }
  begin(6);
  apply_hour(6, 2);
  CHECK(g.n.ai[GF] == 3, "night 6, 2 AM: Golden Freddy at 3");
  /* the caps: 20 plays as 15, Foxy 17, Golden Freddy 10 */
  begin(7);
  for (int k = 0; k < BOTS; k++) g.n.ai[k] = 20;
  CHECK(ai_level(TB) == 15 && ai_level(FOXY) == 17 && ai_level(GF) == 10 && ai_level(PUPPET) == 15, "the caps");
  /* the custom night: its ten from the screen, the Puppet at 15 */
  memset(&g, 0, sizeof g);
  for (int k = 0; k < 10; k++) g.custom[k] = (int8_t)(k + 1);
  night_begin(7);
  for (int k = 0; k < 10; k++) CHECK(g.n.ai[CUSTOM_ORDER[k]] == k + 1, "custom night: %s", BOT_NAMES[CUSTOM_ORDER[k]]);
  CHECK(g.n.ai[PUPPET] == 15, "custom night: the Puppet at 15");
  /* the hours: 68 seconds each, the levels change at the hour, 6 AM ends it */
  begin(1);
  quiet();
  g.n.ai[PUPPET] = 1;
  g.n.box = BOX_FULL;
  steps(68 * 60 + 2, 0);
  CHECK(g.n.hour == 1 && g.n.ai[TB] == 2 && g.n.ai[TC] == 2, "1 AM after 68 s: hour %d, Toy Bonnie %d", g.n.hour,
        g.n.ai[TB]);
}

static void music_box(void) {
  for (int night = 2; night <= 7; night++) {
    begin(night);
    quiet();
    steps(600, 0);
    float want = BOX_FULL - 10 * 20.0f * (night > 6 ? 6 : night);
    CHECK(g.n.box > want - 2 && g.n.box < want + 2, "night %d: the box at %.1f after 10 s, not %.0f", night, g.n.box,
          want);
  }
  begin(1);
  quiet();
  steps(600, 0);
  CHECK(g.n.box == BOX_FULL, "night 1: the box holds until 2 AM");
  CHECK(box_drain(1, 2) == 40, "night 1 from 2 AM: 40 a second");
  /* winding on CAM 11: from empty to full in about 6 s, and it doesn't drain while wound */
  begin(3);
  quiet();
  g.n.box = 0;
  monitor(true);
  tap(K_DIGIT(1));
  tap(K_DIGIT(1));
  CHECK(g.n.cam == L_PRIZE, "1 twice: CAM 11 (cam %d)", g.n.cam);
  steps(5 * 60, K_UP);
  CHECK(g.n.box > 1600 && g.n.box < BOX_FULL, "5 s of winding: %.0f", g.n.box);
  steps(70, K_UP);
  CHECK(g.n.box == BOX_FULL, "6 s of winding: full");
  /* the warning: a fifth or less left */
  g.n.box = 401;
  CHECK(!box_warning(), "no warning above a fifth");
  g.n.box = 399;
  CHECK(box_warning(), "the warning under a fifth");
}

static void flashlight(void) {
  int frames[8] = {0, 7000, 6000, 5000, 4000, 3000, 3000, 3000};
  for (int night = 1; night <= 7; night++) CHECK(battery_frames(night) == frames[night], "night %d's battery", night);
  begin(1);
  quiet();
  CHECK(battery_bars() == 4, "4 bars at first");
  g.n.battery = 1400;
  CHECK(battery_bars() == 3, "a fifth used: 3 bars (%d)", battery_bars());
  g.n.battery = 5601;
  CHECK(battery_bars() == 0, "past four fifths: no bar shown (%d)", battery_bars());
  /* the hallway light uses it a frame at a time, until it's empty */
  g.n.battery = 0;
  steps(100, K_DIGIT(5));
  CHECK(g.n.battery == 100 && g.n.hall_light, "100 frames of light: %.0f", g.n.battery);
  g.n.battery = 7000;
  steps(10, K_DIGIT(5));
  CHECK(!g.n.hall_light && g.n.battery == 7000, "an empty battery: no light");
  /* the vent lights are free */
  g.n.battery = 0;
  g.n.pan = 0;
  steps(60, K_DIGIT(7));
  CHECK(g.n.vent_light == 1 && g.n.battery == 0, "the left vent light, free");
  /* pressed while looking right: turns left first */
  steps(2, 0);
  g.n.pan = OFFICE_PAN;
  steps(90, K_DIGIT(7));
  CHECK(g.n.vent_light == 1 && g.n.pan <= 30, "the vent light turns the view first (pan %.0f)", g.n.pan);
}

static void foxy(void) {
  /* night 1: not there */
  begin(1);
  CHECK(g.n.ai[FOXY] < 0, "night 1: no Withered Foxy");
  /* 21 + random(0..4) - D < AI: at AI 17 he never moves while D is 0, and always once D passes 9 */
  begin(4);
  quiet();
  g.n.ai[FOXY] = 17;
  for (int i = 0; i < 40 * 60; i++) {
    g.n.foxy_d = 0;
    step(0);
  }
  CHECK(!g.n.foxy_hall, "D 0: Foxy stays in Parts/Service");
  g.n.foxy_d = 10;
  steps(5 * 60 + 2, 0);
  CHECK(g.n.foxy_hall, "D 10: Foxy in the hallway");
  /* left alone in the hallway: he gets you */
  steps(30 * 60, 0);
  CHECK(g.scene == SC_SCARE && g.scare == FOXY, "Foxy unflashed: the jumpscare (scene %d)", g.scene);
  /* the light: D back to 0; five flashes and he leaves at the next monitor */
  begin(4);
  quiet();
  g.n.ai[FOXY] = 17;
  g.n.foxy_hall = 1;
  g.n.foxy_d = 8;
  for (int k = 0; k < 5; k++) {
    steps(6, K_DIGIT(5));
    steps(6, 0);
  }
  CHECK(g.n.foxy_off && g.n.foxy_d < 1, "five flashes: Foxy off (flashes %d)", g.n.foxy_flashes);
  monitor(true);
  CHECK(!g.n.foxy_hall, "the monitor up: Foxy back in Parts/Service");
  /* a flash once he has you: the jumpscare */
  begin(4);
  quiet();
  g.n.ai[FOXY] = 17;
  g.n.foxy_hall = 1;
  g.n.foxy_got = 1;
  g.n.foxy_ten = 9;
  steps(3, K_DIGIT(5));
  CHECK(g.scene == SC_SCARE && g.scare == FOXY, "a flash at Foxy who has you");
  /* the mask on, no one about: D twice as fast */
  begin(4);
  quiet();
  g.n.ai[FOXY] = 0;
  tap(K_DOWN);
  steps(30, 0);
  float d0 = g.n.foxy_d;
  steps(120, 0);
  CHECK(g.n.foxy_d - d0 > 3.9f && g.n.foxy_d - d0 < 4.1f, "the mask: D +2 a second (%.2f)", g.n.foxy_d - d0);
}

/* steps until who moves (at most 30 s: at 20, capped at 15, 3 opportunities in 4 succeed); where it is then */
static int next_place(int who) {
  int was = bot_at(who), ready = who < WALKERS ? g.n.ready[who] : 0;
  for (int i = 0; i < 30 * 60 && g.scene == SC_NIGHT; i++) {
    step(0);
    if (bot_at(who) != was || (who < WALKERS && g.n.ready[who] != ready)) break;
  }
  return bot_at(who);
}

static void paths(void) {
  /* Toy Bonnie: Party Room 3, 4, 2, the right vent, its blind spot, then waiting to come in */
  begin(1);
  quiet();
  g.n.ai[TB] = 20;
  static const int8_t at[] = {L_PR3, L_PR4, L_PR2, L_RVENT, L_RSPOT};
  for (int k = 0; k < 5; k++) {
    int now = next_place(TB);
    CHECK(now == at[k], "Toy Bonnie's move %d: %d, not %d", k + 1, now, at[k]);
  }
  next_place(TB);
  CHECK(g.n.ready[TB], "Toy Bonnie waits in the blind spot");
  /* the mask at his next won opportunity: back to Party Room 3 */
  tap(K_DOWN);
  next_place(TB);
  CHECK(bot_at(TB) == L_PR3 && !g.n.ready[TB], "the mask sends Toy Bonnie back (%d)", bot_at(TB));
  /* the others' first steps */
  static const struct { uint8_t who; int8_t at[3]; } first[] = {
    {TF, {L_GAME, L_HALL_FAR, L_HALL}}, {TC, {L_MAIN, L_HALL, L_PR1}}, {MANGLE, {L_PRIZE, L_GAME, L_MAIN}},
    {BB, {L_UNSEEN, L_LVENT, L_LSPOT}}, {WF, {L_MAIN, L_PR3, L_HALL}}, {WB, {L_MAIN, L_HALL, L_PR1}},
    {WC, {L_PR4, L_PR2, L_RVENT}}};
  for (unsigned i = 0; i < sizeof first / sizeof first[0]; i++) {
    begin(3);
    quiet();
    /* (the ones that leave the Show Stage or Parts/Service before them are gone) */
    g.n.step[TB] = 1;
    if (first[i].who != WB) g.n.step[WB] = 1;
    if (first[i].who == TF) g.n.step[TC] = 1;
    if (first[i].who == WF) g.n.step[WC] = 1;
    g.n.ai[first[i].who] = 20;
    for (int k = 0; k < 3; k++) {
      int now = next_place(first[i].who);
      CHECK(now == first[i].at[k], "%s's move %d: %d", BOT_NAMES[first[i].who], k + 1, now);
    }
  }
  /* the Show Stage and Parts/Service empty in the game's order: Toy Chica not before Toy Bonnie, Withered Freddy not
     before Withered Chica, whatever their levels; on the custom night they leave as they like */
  for (int custom = 0; custom < 2; custom++) {
    begin(custom ? 7 : 3);
    quiet();
    g.n.ai[TC] = g.n.ai[WF] = 20;
    steps(60 * 60, 0);
    CHECK((bot_at(TC) == L_STAGE && bot_at(WF) == L_PARTS) == !custom,
          "night %d: Toy Chica at %d, Withered Freddy at %d", custom ? 7 : 3, bot_at(TC), bot_at(WF));
  }
  /* a move a 5 s opportunity, no sooner */
  begin(3);
  quiet();
  g.n.ai[WC] = 20;
  int moves = 0, was = bot_at(WC);
  for (int i = 0; i < 15 * 60 - 2; i++) {
    step(0);
    if (bot_at(WC) != was) moves++, was = bot_at(WC);
  }
  CHECK(moves <= 2, "two opportunities in 15 s (%d moves)", moves);
  /* AI 0: no one moves */
  begin(1);
  quiet();
  steps(60 * 60, 0);
  for (int k = 0; k < WALKERS; k++) CHECK(g.n.step[k] == 0, "%s moved at 0", BOT_NAMES[k]);
  /* the Withereds stay in Parts/Service while its camera is watched */
  begin(3);
  quiet();
  g.n.ai[WB] = 20;
  monitor(true);
  tap(K_DIGIT(8));
  steps(30 * 60, 0);
  CHECK(bot_at(WB) == L_PARTS, "Withered Bonnie held in Parts/Service by its camera");
}

static void visitor(void) {
  /* Withered Chica comes in with the monitor up, shows when it comes down: no mask in time, she gets you the
     next time it comes down */
  begin(3);
  quiet();
  g.n.step[WC] = 3;
  g.n.ready[WC] = 1;
  monitor(true);
  CHECK(g.n.occ == WC && g.n.occ_state == OCC_IN, "in with the monitor up");
  monitor(false);
  CHECK(g.n.occ_state == OCC_SHOWN, "she shows (%d)", g.n.occ_state);
  steps(60, 0);
  CHECK(g.n.occ_state == OCC_DOOMED, "no mask within the second: doomed (%d)", g.n.occ_state);
  CHECK(g.scene == SC_NIGHT, "not yet");
  monitor(true);
  steps(20, 0);
  if (g.scene == SC_NIGHT) monitor(false);
  CHECK(g.scene == SC_SCARE && g.scare == WC, "the monitor up and down: Withered Chica's jumpscare");
  /* the mask within the second: she stays, the lights go out, she's back in Party Room 4 */
  begin(3);
  quiet();
  g.n.step[WC] = 3;
  g.n.ready[WC] = 1;
  monitor(true);
  tap(K_OK);
  steps(16, 0);
  tap(K_DOWN);
  CHECK(g.n.occ_state == OCC_MASKED, "masked in time (%d)", g.n.occ_state);
  steps(6 * 60, 0);
  CHECK(g.n.occ_state == OCC_NONE && bot_at(WC) == L_PR4 && g.scene == SC_NIGHT, "she left (%d, at %d)",
        g.n.occ_state, bot_at(WC));
  /* the mask off too soon */
  begin(3);
  quiet();
  g.n.step[WC] = 3;
  g.n.ready[WC] = 1;
  monitor(true);
  tap(K_OK);
  steps(16, 0);
  tap(K_DOWN);
  steps(60, 0);
  tap(K_DOWN);
  CHECK(g.scene == SC_SCARE && g.scare == WC, "the mask off too soon");
  /* doomed, the mask on: pulled off after a while */
  begin(3);
  quiet();
  g.n.step[WF] = 3;
  g.n.ready[WF] = 1;
  monitor(true);
  monitor(false);
  steps(70, 0);
  tap(K_DOWN);
  steps(8 * 60, 0);
  CHECK(g.scene == SC_SCARE && g.scare == WF, "doomed with the mask on: it comes off");
}

static void others(void) {
  /* Balloon Boy in the office: no flashlight, no vent lights, no jumpscare */
  begin(2);
  quiet();
  g.n.step[BB] = 3;
  g.n.ready[BB] = 1;
  monitor(true);
  monitor(false);
  CHECK(g.n.bb_in && !lights_work(), "Balloon Boy in: the lights don't work");
  steps(30, K_DIGIT(5));
  CHECK(!g.n.hall_light, "no hallway light with Balloon Boy in");
  steps(60 * 30, 0);
  CHECK(g.scene == SC_NIGHT, "Balloon Boy doesn't attack");
  /* Mangle in the office: the mask does nothing; sooner or later the monitor coming down ends it */
  begin(2);
  quiet();
  g.n.step[MANGLE] = 8;
  g.n.ready[MANGLE] = 1;
  monitor(true);
  CHECK(g.n.mangle_in, "Mangle in");
  for (int i = 0; i < 40 && g.scene == SC_NIGHT; i++) {
    monitor(false);
    if (g.scene == SC_NIGHT) monitor(true);
  }
  CHECK(g.scene == SC_SCARE && g.scare == MANGLE, "Mangle attacks, the monitor going up and down");
  /* Golden Freddy's head in the hallway: lit half a second, the jumpscare */
  begin(6);
  quiet();
  g.n.gf = GF_HALL;
  steps(40, K_DIGIT(5));
  CHECK(g.scene == SC_SCARE && g.scare == GF, "Golden Freddy in the light");
  /* ... in the office: the mask in time and he fades */
  begin(6);
  quiet();
  g.n.gf = GF_COMING;
  monitor(true);
  tap(K_OK);
  steps(16, 0);
  tap(K_DOWN);
  steps(120, 0);
  CHECK(g.scene == SC_NIGHT && g.n.gf == GF_NONE, "Golden Freddy masked: gone (%d)", g.n.gf);
  /* the Puppet: the box run down, he comes out and gets you, mask or not */
  begin(7);
  quiet();
  g.n.ai[PUPPET] = 15;
  g.n.box = 0;
  tap(K_DOWN);
  steps(60 * 60, 0);
  CHECK(g.scene == SC_SCARE && g.scare == PUPPET, "the Puppet, through the mask (%d %d)", g.n.puppet, g.n.puppet_at);
  /* wound before he is out: he stays */
  begin(7);
  quiet();
  g.n.ai[PUPPET] = 19;
  g.n.box = 0;
  while (g.n.puppet < 1) step(0);
  g.n.box = 500;
  steps(3 * 60, 0);
  CHECK(g.n.puppet == 1 && g.scene == SC_NIGHT, "wound in time: the Puppet stays (%d)", g.n.puppet);
  /* the camera's light holds Toy Bonnie 6.66 s */
  begin(1);
  quiet();
  g.n.ai[TB] = 20;
  g.n.step[TB] = 1;
  monitor(true);
  tap(K_DIGIT(3));
  steps(2, K_UP);
  CHECK(g.n.stun[TB] >= 398, "the light holds Toy Bonnie (%d)", g.n.stun[TB]);
  g.n.battery = -100000;   /* (light enough for the test) */
  steps(30 * 60, K_UP);
  CHECK(bot_at(TB) == L_PR3 && g.n.pending[TB], "held: his move waits");
  steps(30, 0);
  steps(STUN_FRAMES - 30, 0);
  CHECK(bot_at(TB) == L_PR4, "6.66 s after the light, he moves (%d)", bot_at(TB));
  /* ... but not the Toys on the Show Stage */
  begin(1);
  quiet();
  monitor(true);
  tap(K_DIGIT(9));
  steps(2, K_UP);
  CHECK(g.n.stun[TB] == 0, "the Show Stage: no hold");
}

int main(void) {
  static uint16_t band[SCREEN_W * BAND_H];
  gfx_set_band_memory(band);
  fnaf2_bin = fnaf2_data;
  ai_table();
  music_box();
  flashlight();
  foxy();
  paths();
  visitor();
  others();
  printf("%d of %d rules checks passed\n", checks - failures, checks);
  return failures ? 1 : 0;
}
