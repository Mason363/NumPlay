/* 2 Players on a computer, for tests and pictures (make host, make test).
 *
 *   play test        the rules of every game; bots against bots: player 1 played by the computer's
 *                    own mind (at a player's speed) against the computer, at every level, and a
 *                    quick or slow player in Quick Draw and Tug of War; random sessions through the
 *                    menus and the games, quitting and opening again, that never break a rule; the
 *                    old saves (four games) come back with their tallies, damaged ones reset
 *   play shot DIR    pictures (DIR/NAME.ppm): the menu, each game's page, each game, the cards
 *
 * Built with -ffp-contract=off, and the game uses no libc randomness, sorting or unordered
 * arguments: the same results on any computer. */
#define NP_MODULE 1
#define main tp_main
#include "../src/main.c"
#undef main
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t fb[SCREEN_H][SCREEN_W];
static uint8_t disk[1024];
static uint32_t disk_len;
static int writes, fails, unfinished;
void np_push(int x, int y, int w, int h, const color_t *p) {
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++) fb[y + j][x + i] = p[j * w + i];
}
void *np_alloc(uint32_t size) { return malloc(size); }
const uint8_t *np_file_read(const char *name, uint32_t *len) {
  *len = disk_len;
  return disk_len ? disk : 0;
}
bool np_file_write(const char *name, const void *d, uint32_t len) {
  if (strcmp(name, "twoplayer.sav") || len > sizeof disk) return false;
  memcpy(disk, d, disk_len = len), writes++;
  return true;
}
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

/* ---------------------------------------------------------------- whole sessions, scripted */
static uint32_t clock_ms, frames, fseed = 1;
static int session_frames;
static uint64_t (*script)(uint32_t frame);
static uint32_t frnd(void) { return fseed = fseed * 1664525u + 1013904223u; }
static uint64_t K(int k) { return (uint64_t)1 << k; }
static uint64_t no_keys(uint32_t f) { return 0; }
uint64_t eadk_keyboard_scan(void) {
  clock_ms += 15 + frnd() % 30, frames++;
  if (session_frames && frames >= (uint32_t)session_frames) return K(eadk_key_home);
  if (script) return script(frames);
  /* mostly play keys, sometimes the menus' and Back */
  static const uint8_t KEY[] = {eadk_key_left, eadk_key_up, eadk_key_down, eadk_key_right, eadk_key_ok, eadk_key_exe,
                                eadk_key_three, eadk_key_multiplication, eadk_key_ans, eadk_key_minus, eadk_key_back};
  uint64_t k = 0;
  for (int i = 0; i < 3; i++) {
    uint32_t a = frnd(), b = frnd(), c = frnd(); /* (one at a time: the same order on any compiler) */
    if (a % 3 == 0) k |= K(KEY[b % (sizeof KEY - (c % 8 ? 1 : 0))]);
  }
  return k;
}
uint64_t eadk_timing_millis(void) { return clock_ms; }
uint32_t eadk_random(void) { return frnd(); }
bool eadk_display_wait_for_vblank(void) { return true; }
void eadk_timing_msleep(uint32_t ms) { (void)ms; }

/* the game's state is sane */
static void sane(const char *where) {
  CHECK(game >= 0 && game < NG && cpu >= 0 && cpu <= 3, "%s: game %d cpu %d", where, game, cpu);
  CHECK(score[0] >= 0 && score[1] >= 0 && score[0] <= TARGET[game] && score[1] <= TARGET[game], "%s: score %d-%d in %d", where,
        score[0], score[1], game);
  CHECK(screen >= S_MENU && screen <= S_OVER && top >= 0 && top <= NG - ROWS && game >= top && game < top + ROWS, "%s: screen %d top %d",
        where, screen, top);
  CHECK(pg.rope >= -75 && pg.rope <= 75, "%s: rope %f", where, (double)pg.rope);
}
static void reset_all(void) { /* as a fresh start of the app */
  memset(&sv, 0, sizeof sv), memset(&pg, 0, sizeof pg), memset(&bot, 0, sizeof bot);
  screen = sel = back_to = top = game = cpu = 0, now = 0, phase = 0, score[0] = score[1] = 0;
}

/* ---------------------------------------------------------------- matches, frame by frame */
static void begin(int g, int level, uint32_t s) {
  reset_all();
  game = g, cpu = level, seed = s ? s : 1, now = 1000, ms = 20, dt = ms * .001f, top = clampi(g - 2, 0, NG - ROWS);
  new_match();
}
static void frame(int p1pad, int p1tap, int p2pad, int p2tap) { /* as main's loop in a match */
  ms = 20, dt = .02f, now += 20;
  pad[0] = p1pad, tap[0] = p1tap, pad[1] = p2pad, tap[1] = p2tap;
  if (cpu) pad[1] = tap[1] = 0;
  play();
}

/* Player 1 played by the computer's own mind at a level, at a player's speed: the court mirrored
   so player 1 is player 2, the computer's thinking (with its own memory) run, its keys read. */
static __typeof__(bot) mind;
static int ai_aim1;
static uint32_t ai_t1, ai_shot1;
#define SWAP(a, b) do { __typeof__(a) t_ = a; a = b; b = t_; } while (0)
static void think(int level, int *p, int *t) {
  int keep = cpu;
  __typeof__(bot) b = bot;
  memcpy(&bot, &mind, sizeof bot), cpu = level, *p = *t = 0;
  if (game == PONG) {
    int y = py[1], x = bx, vx = bvx, a = aim; /* (a player aims at the middle) */
    py[1] = py[0], bx = ((SCREEN_W - BS) << 8) - bx, bvx = -bvx, aim = 0;
    int v = pong_cpu();
    py[1] = y, bx = x, bvx = vx, aim = a;
    *p = v > 3 ? B_DOWN : v < -3 ? B_UP : 0;
  } else if (game == HOCKEY) {
    float s[8] = {mx[0], my[0], mvx[0], mvy[0], mx[1], my[1], mvx[1], mvy[1]}, q[2] = {qx, qvx}, tx, ty;
    mx[1] = SCREEN_W - s[0], my[1] = s[1], mvx[1] = -s[2], mvy[1] = s[3];
    mx[0] = SCREEN_W - s[4], my[0] = s[5], mvx[0] = -s[6], mvy[0] = s[7], qx = SCREEN_W - qx, qvx = -qvx;
    hockey_cpu(&tx, &ty);
    mx[0] = s[0], my[0] = s[1], mvx[0] = s[2], mvy[0] = s[3], mx[1] = s[4], my[1] = s[5], mvx[1] = s[6], mvy[1] = s[7];
    qx = q[0], qvx = q[1], tx = -tx;
    *p = (tx > 40 ? B_RIGHT : tx < -40 ? B_LEFT : 0) | (ty > 40 ? B_DOWN : ty < -40 ? B_UP : 0);
  } else if (game == TANKS) {
    int v, turn;
    SWAP(tx_[0], tx_[1]); SWAP(ty_[0], ty_[1]); SWAP(ta[0], ta[1]); SWAP(dead[0], dead[1]); SWAP(dead_t[0], dead_t[1]);
    SWAP(ai_aim, ai_aim1); SWAP(ai_t, ai_t1); SWAP(ai_shot, ai_shot1);
    for (int i = 0; i < SHELLS; i++) sh[i].own = sh[i].own < 0 ? -1 : 1 - sh[i].own;
    tanks_cpu(&v, &turn);
    SWAP(tx_[0], tx_[1]); SWAP(ty_[0], ty_[1]); SWAP(ta[0], ta[1]); SWAP(dead[0], dead[1]); SWAP(dead_t[0], dead_t[1]);
    SWAP(ai_aim, ai_aim1); SWAP(ai_t, ai_t1); SWAP(ai_shot, ai_shot1);
    for (int i = 0; i < SHELLS; i++) sh[i].own = sh[i].own < 0 ? -1 : 1 - sh[i].own;
    /* the arrows point where to go: ahead to drive, an eighth of a turn aside to drive while turning, a quarter
       to turn on the spot */
    static const uint8_t DIR8[8] = {B_RIGHT, B_RIGHT | B_DOWN, B_DOWN, B_DOWN | B_LEFT, B_LEFT, B_LEFT | B_UP, B_UP,
                                    B_UP | B_RIGHT};
    int k = v > 0 ? 8 : 16, a = (ta[0] >> 8) + (turn > 0 ? k : turn < 0 ? -k : 0);
    *p = v > 0 || turn ? DIR8[((a + 4) & 63) / 8] : 0;
  } else if (game == CYCLES) {
    static const uint8_t BIT[4] = {B_RIGHT, B_DOWN, B_LEFT, B_UP};
    uint32_t next = bot.next;
    SWAP(hx[0], hx[1]); SWAP(hy[0], hy[1]); SWAP(hd[0], hd[1]);
    int d = cycles_cpu();
    SWAP(hx[0], hx[1]); SWAP(hy[0], hy[1]); SWAP(hd[0], hd[1]);
    int last = nturns[0] ? turns[0][nturns[0] - 1] : hd[0];
    if (bot.next != next && d != last) *t = BIT[d]; /* (a turn when it decided one) */
  }
  memcpy(&mind, &bot, sizeof bot), bot = b, cpu = keep;
}
/* a match of player 1 (a mind at level l1, or for Quick Draw and Tug of War a player who reacts
   in r ms, or taps every r ms) against the computer at l2; who won */
static int match(int g, int l1, int r, int l2, uint32_t s) {
  begin(g, l2, s);
  memset(&mind, 0, sizeof mind), ai_aim1 = -1, ai_t1 = ai_shot1 = now;
  uint32_t last_tap = 0;
  for (int f = 0; f < 60000 && screen == S_PLAY; f++) { /* (20 minutes: two minds can pin the puck forever) */
    int p = 0, t = 0;
    if (g == QUICK) t = !phase && due(pg.at + r) ? B_FIRE : 0;
    else if (g == TUG) {
      if (!phase && now - last_tap >= (uint32_t)r) t = B_FIRE, last_tap = now;
    } else think(l1, &p, &t);
    frame(p | t, t, 0, 0);
    sane("match");
  }
  if (screen != S_OVER) unfinished++, printf("unfinished: game %d %d-%d screen %d\n", g, score[0], score[1], screen);
  return score[1] >= score[0];
}

/* ---------------------------------------------------------------- the rules */
static void rules(void) {
  /* Quick Draw: too soon loses, after the signal the first wins, together a draw */
  begin(QUICK, 0, 7);
  frame(B_FIRE, B_FIRE, 0, 0);
  CHECK(phase == 1 && winner == 1 && score[1] == 1, "quick: too soon %d %d", phase, winner);
  begin(QUICK, 0, 7), now = pg.at - 20;
  frame(0, 0, B_FIRE, B_FIRE);
  CHECK(winner == 1, "quick: player 2 first %d", winner);
  begin(QUICK, 0, 7), now = pg.at - 20;
  frame(B_FIRE, B_FIRE, B_FIRE, B_FIRE);
  CHECK(winner == 2 && !score[0] && !score[1], "quick: together %d", winner);
  begin(QUICK, 0, 7), now = pg.at - 25;
  frame(0, 0, 0, 0);
  CHECK(!phase, "quick: nothing yet");
  begin(QUICK, 3, 7); /* the computer's keys are its own */
  int f = 0;
  while (!phase && f++ < 1000) frame(0, 0, B_FIRE, B_FIRE);
  CHECK(winner == 1 && now >= pg.next, "quick: the computer drew at %u (%u)", now, pg.next);
  /* Tug of War: a tap pulls 7.5, the rope slides back 3 a second, past 67 wins, 30 s ends it */
  begin(TUG, 0, 7);
  frame(B_FIRE, B_FIRE, 0, 0);
  CHECK(pg.rope < -7.4f && pg.rope > -7.5f, "tug: one tap %f", (double)pg.rope);
  frame(B_FIRE, B_FIRE, B_FIRE, B_FIRE);
  CHECK(pg.rope < -7.3f && pg.rope > -7.5f, "tug: together %f", (double)pg.rope);
  for (int i = 0; i < 50; i++) frame(0, 0, 0, 0);
  CHECK(pg.rope < -4.3f && pg.rope > -4.5f, "tug: slides back %f", (double)pg.rope);
  frame(B_FIRE, 0, 0, 0);
  CHECK(pg.rope > -4.5f, "tug: holding doesn't pull");
  for (int i = 0; i < 9 && !phase; i++) frame(B_FIRE, B_FIRE, 0, 0), frame(0, 0, 0, 0);
  CHECK(phase == 1 && winner == 0, "tug: player 1 pulls it over %d %f", phase, (double)pg.rope);
  begin(TUG, 0, 7);
  frame(0, 0, B_FIRE, B_FIRE);
  while (!phase) frame(0, 0, 0, 0);
  CHECK(winner == 2 && round_ms() > 30000, "tug: time up, the rope back in the middle: a draw %d", winner);
  begin(TUG, 0, 7);
  while (!phase) frame(0, 0, 0, 0), frame(0, 0, B_FIRE, B_FIRE);
  CHECK(winner == 1, "tug: player 2 wins");
  /* a match: first to its points, the tally kept */
  begin(TUG, 0, 7);
  sv.wins[TUG][0][1] = 41;
  for (int r = 0; r < 5; r++) {
    while (!phase) frame(0, 0, 0, 0), frame(0, 0, B_FIRE, B_FIRE);
    for (int i = 0; i < 80 && phase; i++) frame(0, 0, 0, 0);
  }
  CHECK(screen == S_OVER && score[1] == 5 && sv.wins[TUG][0][1] == 42, "tug: the match %d %d %d", screen, score[1], sv.wins[TUG][0][1]);
  /* Pong: a ball past a paddle is the other's point; the serve goes to the one who lost it */
  begin(PONG, 0, 7);
  bx = 2 << 8, bvx = -40, serve_at = 0, by = 120 << 8, bvy = 0, py[0] = 20 << 8;
  for (int i = 0; i < 20 && !phase; i++) frame(0, 0, 0, 0);
  CHECK(phase == 1 && winner == 1, "pong: past player 1");
  for (int i = 0; i < 80 && phase; i++) frame(0, 0, 0, 0);
  CHECK(!phase && bvx < 0, "pong: served to player 1");
  /* Light Cycles: into a wall */
  begin(CYCLES, 0, 7);
  for (int i = 0; i < 400 && !phase; i++) frame(0, 0, 0, 0);
  CHECK(phase == 1 && winner == 2, "cycles: both into the walls together %d", winner);
}

/* ---------------------------------------------------------------- saves */
static void saves(void) {
  /* version 1, four games: its tallies, game and opponent come back, the new games at 0 */
  uint8_t v1[36] = {'P', 1, 3, 2};
  for (int i = 0; i < 16; i++) v1[4 + 2 * i] = (uint8_t)(10 + i), v1[5 + 2 * i] = (uint8_t)i;
  reset_all(), memcpy(disk, v1, disk_len = 36), writes = 0;
  session_frames = 3, frames = 0, script = no_keys, tp_main(), session_frames = 0;
  CHECK(game == CYCLES && cpu == 2, "v1: game %d cpu %d", game, cpu);
  for (int g = 0; g < NG; g++)
    for (int i = 0; i < 4; i++) {
      int want = g < 4 ? (10 + g * 4 + i) | (g * 4 + i) << 8 : 0;
      CHECK(sv.wins[g][i / 2][i % 2] == want, "v1: game %d tally %d: %d", g, i, sv.wins[g][i / 2][i % 2]);
    }
  CHECK(writes && disk_len == sizeof sv && disk[0] == 'P' && disk[1] == 1 && !memcmp(disk + 4, v1 + 4, 32), "v1: saved again, the same");
  /* the new length comes back as it is */
  uint8_t full[sizeof sv];
  memcpy(full, disk, sizeof full);
  reset_all(), session_frames = 3, frames = 0, tp_main(), session_frames = 0;
  CHECK(!memcmp(disk, full, sizeof full), "a save comes back the same");
  /* damaged ones: another magic, a later version, too long: a fresh start */
  for (int k = 0; k < 3; k++) {
    memcpy(disk, full, disk_len = sizeof full);
    if (k == 0) disk[0] = 'Q';
    if (k == 1) disk[1] = 2;
    if (k == 2) disk_len = sizeof full + 2;
    reset_all(), sv.wins[0][0][0] = 0, session_frames = 3, frames = 0, tp_main(), session_frames = 0;
    CHECK(sv.wins[0][0][0] == 0 && sv.wins[1][0][0] == 0, "damaged save %d read", k);
  }
  disk_len = 0, script = 0;
}

/* ---------------------------------------------------------------- random sessions */
static void sessions(int n) {
  disk_len = 0;
  for (int s = 0; s < n; s++) {
    reset_all();
    session_frames = 300 + (int)(frnd() % 1500), frames = 0, script = 0;
    tp_main();
    sane("session");
    CHECK(disk_len == sizeof sv && disk[0] == 'P', "session %d: saved", s);
  }
  session_frames = 0;
}

/* Back pauses, Quit game asks first, Home saves and quits */
static uint64_t walk_script(uint32_t f) {
  /* OK (its page), OK (play), Back (pause), down x3 (Quit game), OK, OK (No), Back (play), Back (pause), down x3, OK,
     down (Yes), OK: out */
  static const uint8_t STEPS[] = {eadk_key_ok, eadk_key_ok, eadk_key_back, eadk_key_down, eadk_key_down, eadk_key_down,
                                  eadk_key_ok, eadk_key_ok, eadk_key_back, eadk_key_back, eadk_key_down, eadk_key_down,
                                  eadk_key_down, eadk_key_ok, eadk_key_down, eadk_key_ok};
  static const uint8_t SCREEN[] = {S_MENU, S_RULES, S_PLAY, S_PAUSE, S_PAUSE, S_PAUSE, S_PAUSE, S_ASK, S_PAUSE, S_PLAY,
                                   S_PAUSE, S_PAUSE, S_PAUSE, S_PAUSE, S_ASK, S_ASK};
  if (f % 4 || f / 4 < 1 || f / 4 > sizeof STEPS) return 0;
  CHECK(screen == SCREEN[f / 4 - 1], "keys: step %u on screen %d", f / 4, screen);
  return K(STEPS[f / 4 - 1]);
}
/* Tug of War against the computer: down to it, the computer, OK, OK, then OK mashed; once the match is
   over, mashing on doesn't pick Rematch (for 600 ms) */
static uint32_t over_at;
static uint64_t mash_script(uint32_t f) {
  if (f < 30) return f % 2 ? 0 : K(f < 12 ? eadk_key_down : f < 14 ? eadk_key_right : eadk_key_ok);
  if (screen == S_OVER && !over_at) over_at = clock_ms;
  if (over_at && clock_ms - over_at > 600) {
    CHECK(screen == S_OVER, "a mashed key picked from the end card");
    return K(eadk_key_home);
  }
  return f % 2 ? K(eadk_key_ok) : 0;
}
/* alone, EXE is OK: Tug of War against the computer, OK and EXE pressed in turn, one always held: a single pull */
static float rope_was;
static int pulls;
static uint64_t both_script(uint32_t f) {
  if (f < 30) return f % 2 ? 0 : K(f < 12 ? eadk_key_down : f < 14 ? eadk_key_right : f < 18 ? eadk_key_exe : 0);
  if (f == 30) CHECK(screen == S_PLAY && game == TUG && cpu == 1 && round_ms() < 600, "exe: in the match %d %d", screen, game);
  pulls += pg.rope < rope_was - 5, rope_was = pg.rope;
  if (f < 90) return f % 4 == 0 ? K(eadk_key_ok) : f % 4 == 2 ? K(eadk_key_exe) : K(eadk_key_ok) | K(eadk_key_exe);
  CHECK(pulls == 1, "exe: %d pulls", pulls);
  return K(eadk_key_home);
}
static void keys(void) {
  reset_all(), frames = 0, script = both_script, pulls = 0, rope_was = 0;
  tp_main();
  reset_all(), disk_len = 0, frames = 0, script = walk_script, session_frames = 0;
  tp_main();
  CHECK(frames == 64 && disk_len == sizeof sv, "quit game asks first, then quits and saves (frame %u)", frames);
  reset_all(), frames = 0, script = mash_script, over_at = 0;
  tp_main();
  CHECK(over_at && game == TUG && cpu == 1, "tug: over %u game %d cpu %d", over_at, game, cpu);
  script = 0;
}

/* ---------------------------------------------------------------- bots against bots */
static void bots(void) {
  static const char *const GAME[] = {"Pong", "Air Hockey", "Tanks", "Light Cycles"};
  int n = 12;
  for (int g = PONG; g <= CYCLES; g++) {
    printf("%-12s player 1 at easy/normal/hard against the computer at easy/normal/hard, of %d:\n", GAME[g], n);
    int won[4][4] = {{0}};
    for (int l1 = 1; l1 <= 3; l1++) {
      printf("  ");
      for (int l2 = 1; l2 <= 3; l2++) {
        for (int i = 0; i < n; i++) won[l1][l2] += !match(g, l1, 0, l2, 1000u * g + 100u * l1 + 10u * l2 + i);
        printf(" %2d", won[l1][l2]);
      }
      printf("\n");
    }
    /* the levels: a harder computer wins more; a good player beats easy, and can beat hard; hard beats a
       weak player */
    CHECK(won[2][1] >= won[2][3] && won[3][1] > n / 2 && won[3][3] > 0 && won[1][3] < n, "%s: the levels", GAME[g]);
  }
  /* Quick Draw: a quick player (250 ms) and a slow one (400 ms); Tug of War: 7 and 5 taps a second */
  int quick[4][2] = {{0}}, tug[4][2] = {{0}};
  for (int l2 = 1; l2 <= 3; l2++)
    for (int i = 0; i < n; i++) {
      quick[l2][0] += !match(QUICK, 0, 250, l2, 50u + 10u * l2 + i), quick[l2][1] += !match(QUICK, 0, 400, l2, 90u + 10u * l2 + i);
      tug[l2][0] += !match(TUG, 0, 143, l2, 50u + 10u * l2 + i), tug[l2][1] += !match(TUG, 0, 200, l2, 90u + 10u * l2 + i);
    }
  printf("Quick Draw   a 250 ms / 400 ms player against easy, normal, hard, of %d: %d/%d %d/%d %d/%d\n", n, quick[1][0], quick[1][1],
         quick[2][0], quick[2][1], quick[3][0], quick[3][1]);
  printf("Tug of War   7 / 5 taps a second against easy, normal, hard, of %d: %d/%d %d/%d %d/%d\n", n, tug[1][0], tug[1][1], tug[2][0],
         tug[2][1], tug[3][0], tug[3][1]);
  printf("unfinished (the puck pinned by two minds): %d\n", unfinished);
  CHECK(unfinished <= 4, "unfinished matches");
  CHECK(quick[1][1] > n / 2 && quick[3][0] > 0 && quick[3][1] < n, "quick draw: levels");
  CHECK(tug[1][1] > n / 2 && tug[3][0] > 0 && tug[3][1] < n, "tug of war: levels");
}

/* ---------------------------------------------------------------- pictures */
static void shot(const char *dir, const char *name) {
  char path[256];
  snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
  FILE *f = fopen(path, "wb");
  if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
  gfx_render(scene, 0, 0, SCREEN_H);
  for (int y = 0; y < SCREEN_H; y++)
    for (int x = 0; x < SCREEN_W; x++) {
      unsigned c = fb[y][x];
      unsigned char p[3] = {(unsigned char)((c >> 11) * 255 / 31), (unsigned char)((c >> 5 & 63) * 255 / 63), (unsigned char)((c & 31) * 255 / 31)};
      fwrite(p, 1, 3, f);
    }
  fclose(f);
}
static void shots(const char *dir) {
  char name[32];
  reset_all();
  sv.wins[QUICK][1][0] = 3, sv.wins[QUICK][1][1] = 1, sv.wins[PONG][1][0] = 2, sv.wins[TUG][1][1] = 1;
  static const uint16_t AT[NG] = {170, 120, 400, 160, 20, 90}; /* (frames into a round: the README's pictures) */
  for (int g = 0; g < NG; g++) {
    begin(g, 3, 11u + g);
    memset(&mind, 0, sizeof mind), ai_aim1 = -1, ai_t1 = ai_shot1 = now;
    for (int i = 0; i < AT[g] && screen == S_PLAY; i++) {
      int p = 0, t = 0;
      if (g <= CYCLES) think(3, &p, &t);
      else if (g == TUG) t = i % 6 == 0 ? B_FIRE : 0;
      frame(p | t, t, 0, 0);
    }
    if (g == QUICK) now = pg.at + 150; /* (the signal) */
    snprintf(name, sizeof name, "game_%02d", g), shot(dir, name);
    screen = S_RULES, top = clampi(top, g - ROWS + 1, g), shot(dir, (snprintf(name, sizeof name, "rules_%02d", g), name));
    if (g == 0 || g == QUICK) screen = S_MENU, shot(dir, (snprintf(name, sizeof name, "menu_%02d", g), name));
  }
  begin(PONG, 0, 3), shot(dir, "pong_2p");
  begin(TUG, 0, 3), screen = S_RULES, shot(dir, "rules_tug_2p");
  begin(HOCKEY, 1, 5);
  for (int i = 0; i < 60; i++) frame(0, 0, 0, 0);
  screen = S_PAUSE, sel = 1, shot(dir, "pause_hockey");
  begin(TUG, 1, 5), screen = S_PAUSE, shot(dir, "pause_tug");
  score[0] = 5, score[1] = 3, screen = S_OVER, sel = 0, shot(dir, "over_tug");
  screen = S_ASK, back_to = S_PAUSE, sel = 0, shot(dir, "ask_tug");
  begin(TUG, 1, 5);
  for (int i = 0; i < 100; i++) frame(i % 6 ? 0 : B_FIRE, i % 6 ? 0 : B_FIRE, 0, 0);
  point(0), now += 300, shot(dir, "point_tug");
}

int main(int argc, char **argv) {
  gfx_buf = strip;
  static const uint32_t TEAM[2] = {0xEF4136, 0x2E7CF6};
  for (int p = 0; p < 2; p++)
    team[p] = gfx_rgb(TEAM[p]), light[p] = gfx_rgb(gfx_lerp888(TEAM[p], 0xFFFFFF, 120)), dark[p] = gfx_rgb(gfx_lerp888(TEAM[p], 0, 110));
  if (argc > 2 && !strcmp(argv[1], "shot")) {
    shots(argv[2]);
    return 0;
  }
  rules();
  saves();
  keys();
  sessions(argc > 2 ? atoi(argv[2]) : 300);
  bots();
  printf(fails ? "%d FAILED\n" : "all passed\n", fails);
  return fails != 0;
}
