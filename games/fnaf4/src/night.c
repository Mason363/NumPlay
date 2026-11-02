/* Five Nights at Freddy's 4: a night, as the game's own events run it (its "level" frame, 458 events, read out of
 * its iOS package; gamesrc spec level_events.txt), 60 steps a second and in the events' order. The numbers in the
 * comments are the events'.
 *
 * You are in the bedroom. Its two doors open on halls, Bonnie comes up the left one and Chica the right; listen at
 * a door (no light): breathing means one stands there, so hold the door shut until the steps go; no breathing, the
 * light sends one in the far hall away. Foxy runs into the closet when your back is turned; shut its door on him.
 * The Freddles gather on the bed when you don't look; the light makes them go. On the 5th night and after 4 AM on
 * the 6th, Fredbear (Nightmare after it) comes up a hall when you listen at the other door, then to the closet or
 * the bed. Their moves are the game's: a chance every few seconds, out of 20, against each one's AI. */
#include <string.h>
#include "game.h"
#include "plat.h"
#include "data.h"
#include "anims.h"

#define N g.n

/* ------------------------------------------------------------ the game's objects and their animations */
int anim_pic(int an, int k) { return ANIM_PICS[ANIMS[an].first + k]; }
int anim_len(int an) { return ANIMS[an].n; }
int anim_count(void) { return AN_COUNT; }
int anim_frames(int an) {
  int s = ANIMS[an].speed ? ANIMS[an].speed : 1;
  return ANIMS[an].n * (ANIMS[an].plays ? ANIMS[an].plays : 1) * 100 / s;
}

static const uint8_t first_anim[O_COUNT] = {
  AN_ROOM_0, AN_DARKL, AN_DARKR, AN_DARKC, AN_OFF4, AN_ON4, AN_CREAK, AN_CARPET, AN_LSTART, AN_RSTART,
  AN_PEEKL_12, AN_PEEKR_12, AN_CLOSET_0, AN_BED_0, AN_BHIDE, AN_CHIDE, AN_LEAVE3, AN_LEAVE2, AN_LEAVE1, AN_BITE,
  AN_JUMP_0, AN_FREDDYATK, AN_CUPCAKE, AN_BONNIEATK, AN_FREDBEAR};

static void set_anim(int o, int an) {   /* (the same animation goes on) */
  Ob *b = &N.ob[o];
  if (b->an == an) return;
  b->an = (uint8_t)an, b->img = 0, b->cnt = 0, b->plays = ANIMS[an].plays, b->done = 0;
}
static void create(int o) {
  Ob *b = &N.ob[o];
  memset(b, 0, sizeof *b);
  b->alive = b->vis = 1;
  b->z = ++N.zc;
  b->an = 0xFF;
  set_anim(o, first_anim[o]);
}
static void destroy(int o) { N.ob[o].alive = 0; }
static bool alive(int o) { return N.ob[o].alive; }
static bool over(int o, int an) { return N.ob[o].alive && N.ob[o].an == an && N.ob[o].done; }   /* (C-2) */
static bool playing(int o, int an) { return N.ob[o].alive && N.ob[o].an == an && !N.ob[o].done; }

static void animate(void) {
  for (int o = 0; o < O_COUNT; o++) {
    Ob *b = &N.ob[o];
    const AnimDef *d = &ANIMS[b->an];
    if (!b->alive || b->done || !d->speed) continue;
    b->cnt = (uint8_t)(b->cnt + d->speed);
    while (b->cnt >= 100) {
      b->cnt = (uint8_t)(b->cnt - 100);
      if (b->img + 1 < d->n) b->img++;
      else if (b->plays == 1) {
        b->done = 1;
        break;
      } else {
        if (b->plays) b->plays--;
        b->img = 0;
      }
    }
  }
}

int scare_anim(int k) {
  static const int8_t a[7] = {AN_JUMP_14, AN_JUMP_13, AN_JUMP_12, AN_JUMP_0, AN_FREDBEAR, AN_PLUSH_23, -1};
  return a[k];
}

int ob_pic(int o) { return anim_pic(N.ob[o].an, N.ob[o].img); }
bool ob_wide(int o) { return ANIMS[N.ob[o].an].wide; }

int night_top(void) {
  int top = -1;
  for (int o = 0; o < O_COUNT; o++)
    if (N.ob[o].alive && N.ob[o].vis && (top < 0 || N.ob[o].z > N.ob[top].z)) top = o;
  return top;
}

/* ------------------------------------------------------------ the events' conditions */
/* "every N ms": each event its own count, which runs only while the conditions before it hold */
enum { T_SECOND, T_FOXY5, T_BONNIE5, T_CHICA5, T_FRED3, T_FRED2, T_BONNIE10, T_CHICA10, T_BONNIE3, T_CHICA3,
       T_FREDDY4, T_FREDDY50, T_FREDDY3, T_FOXY1, T_DANGER, T_D2, T_D3, T_D4, T_D5, T_CHICA_S6, T_BONNIE_S6,
       T_FRED_L3, T_FRED_R3, T_FRED_S4, T_FRED_S5, T_CLOSET_L, T_CLOSET_R, T_FRED_S7, T_LAUGH, T_FRED_S6B,
       T_FRED_S6C, T_FRED_S12, T_HOUR, T_TURN_B, T_TURN_C, T_FRED_S13, T_FREDDY200, T_FRED_S14, T_FRED30A,
       T_FRED30B, T_FRED_S19L, T_FRED_S19R, T_COUNT };
_Static_assert(T_COUNT <= sizeof(((Night *)0)->every) / sizeof(uint16_t), "timers");
static bool every(int id, int ms) {
  uint16_t p = (uint16_t)(ms * 60 / 1000);
  if (!N.every[id]) N.every[id] = p;
  if (--N.every[id]) return false;
  N.every[id] = p;
  return true;
}
/* "only one action when event loops": true when the event was not true the step before */
static bool edge(int id) {
  bool r = N.last[id] != N.frame;
  N.last[id] = N.frame + 1;
  return r;
}
/* "run this event once" */
static bool once(int id) {
  if (N.once[id >> 3] >> (id & 7) & 1) return false;
  N.once[id >> 3] |= (uint8_t)(1 << (id & 7));
  return true;
}
static bool chance(int out_of, int ai) { return (int)rnd_n((uint32_t)out_of) + 1 <= ai; }   /* Random(n)+1 <= AI */

/* ------------------------------------------------------------ what you'd hear */
static void heard(const char *s) { caption(s); }

/* ------------------------------------------------------------ a night */
void night_begin(int night, int shadow) {
  memset(&N, 0, sizeof N);
  N.night = (uint8_t)night, N.shadow = (uint8_t)shadow;
  N.hour = 12, N.x = 500, N.random = 4, N.what_side = 3;   /* (66, 394) */
  N.warning = 1;
  create(O_ROOM);
}

static void moved(Anim *a, uint8_t to) { a->loc = to, a->s1 = 0; }

/* the bedroom, the doors, the closet, the bed: you (events 3 to 101) */
static void you(void) {
  uint32_t held = g.held, pr = g.pressed;
  bool ok = pr & (K_OK | K_EXE), back = pr & (K_DOWN | K_DIGIT(2));
  if (N.x > 400 && (held & (K_LEFT | K_DIGIT(4))) && N.s0 < 7) N.x -= 10;          /* 3 */
  if (N.x < 600 && (held & (K_RIGHT | K_DIGIT(6))) && N.s0 < 7) N.x += 10;         /* 4 */
  if (N.x == 400 && N.s0 == 0) N.s0 = 1, set_anim(O_ROOM, AN_ROOM_12);              /* 5 */
  if (over(O_ROOM, AN_ROOM_12) && N.s0 == 1) set_anim(O_ROOM, AN_ROOM_13), N.s0 = 2;
  if (N.s0 == 2 && N.x > 400) N.s0 = 3, set_anim(O_ROOM, AN_ROOM_14);
  if (over(O_ROOM, AN_ROOM_14) && N.s0 == 3) N.s0 = 0, set_anim(O_ROOM, AN_ROOM_0);
  if (N.s0 == 0 && N.x == 600) N.s0 = 4, set_anim(O_ROOM, AN_ROOM_15);              /* 9 */
  if (over(O_ROOM, AN_ROOM_15) && N.s0 == 4) N.s0 = 5, set_anim(O_ROOM, AN_ROOM_16);
  if (N.s0 == 5 && N.x < 600) N.s0 = 6, set_anim(O_ROOM, AN_ROOM_17);
  if (over(O_ROOM, AN_ROOM_17) && N.s0 == 6) N.s0 = 0, set_anim(O_ROOM, AN_ROOM_0);
  /* to a door (13 to 23) */
  if (ok && N.x == 400 && N.s0 == 2) N.s0 = 7, create(O_DARKL), ok = false;
  if (ok && N.x == 600 && N.s0 == 5) N.s0 = 14, create(O_DARKR), ok = false;
  if (over(O_DARKL, AN_DARKL) && N.s0 == 7)
    N.s0 = 8, N.ob[O_ROOM].vis = 0, create(O_CARPET), N.what_side = 2, destroy(O_DARKL), N.x = 500;
  if (over(O_DARKR, AN_DARKR) && N.s0 == 14)
    N.s0 = 15, N.ob[O_ROOM].vis = 0, create(O_CARPET), N.what_side = 4, destroy(O_DARKR), N.x = 500;
  if (over(O_CARPET, AN_CARPET) && N.s0 == 8)
    destroy(O_CARPET), N.s0 = 9, N.what_side = 1, create(O_LSTART), N.x = 500;
  if (over(O_CARPET, AN_CARPET) && N.s0 == 15)
    destroy(O_CARPET), N.s0 = 16, N.what_side = 5, create(O_RSTART), N.x = 500;
  if (over(O_LSTART, AN_LSTART) && N.s0 == 9) N.s0 = 10, create(O_PEEKL), destroy(O_LSTART);
  /* the left door (24 to 44) */
  if (N.s0 == 10 && !N.flash) set_anim(O_PEEKL, AN_PEEKL_12);
  if (N.s0 == 10 && N.foxy != P_LHF && N.bonnie.loc != P_LHF && N.fred != P_LHF && N.flash)
    set_anim(O_PEEKL, AN_PEEKL_13), N.view_left = 1;
  if (N.s0 == 10 && N.fred == P_LHF && N.kitchen2 <= 1 && !N.shadow && N.flash) set_anim(O_PEEKL, AN_PEEKL_19), N.view_left = 1;
  if (N.s0 == 10 && N.fred == P_LHF && N.kitchen2 > 1 && !N.shadow && N.flash) set_anim(O_PEEKL, AN_PEEKL_20), N.view_left = 1;
  if (N.s0 == 10 && N.fred == P_LHF && N.shadow && N.flash) set_anim(O_PEEKL, AN_PEEKL_21), N.view_left = 1;
  if (N.s0 == 10 && N.foxy == P_LHF && N.bonnie.loc != P_LHF && N.flash)
    set_anim(O_PEEKL, AN_PEEKL_18), N.view_left = 1, N.s0 = 35, N.foxy = P_LRC;
  if (N.s0 == 10 && N.bonnie.loc == P_LHF && N.flash)
    N.view_left = 1, N.s0 = 40, N.bonnie.loc = P_LRC, create(O_BHIDE);
  if (back && N.s0 == 10 && !N.flash && !N.shut) N.s0 = 11, set_anim(O_PEEKL, AN_PEEKL_14), back = false;
  if (over(O_PEEKL, AN_PEEKL_14) && N.s0 == 11) create(O_CARPET), destroy(O_PEEKL), N.s0 = 12, N.what_side = 2;
  for (int side = 0; side < 2; side++) {   /* 36 to 41: back in the bedroom, from the left door then the right */
    uint8_t from = side ? 19 : 12;
    if (!over(O_CARPET, AN_CARPET) || N.s0 != from) continue;
    if (N.foxy_got_you) {
      if (!N.gameover) destroy(O_CARPET), N.foxy_s5 = 2, N.s0 = 39, create(O_JUMP), N.gameover = 1;
    } else if (N.foxy_s5 != 1) {
      N.s0 = 13, destroy(O_CARPET), N.ob[O_ROOM].vis = 1, set_anim(O_ROOM, AN_ROOM_19), N.fredcheck = 1;
    } else {
      N.s0 = 37, destroy(O_CARPET), N.foxy_s5 = 2, create(O_CREAK);
      heard("The closet creaks");
    }
  }
  if (over(O_CARPET, AN_CARPET) && N.s0 == 31) {   /* 42, 43: back from the closet */
    if (N.foxy_got_you) {
      if (!N.gameover) destroy(O_CARPET), create(O_JUMP), N.s0 = 39, N.foxy_s5 = 2, N.gameover = 1;
    } else
      destroy(O_CARPET), N.s0 = 0, N.ob[O_ROOM].vis = 1, set_anim(O_ROOM, AN_ROOM_19), N.fredcheck = 1, N.x = 500;
  }
  if (over(O_ROOM, AN_ROOM_19) && N.s0 == 13) N.s0 = 0, set_anim(O_ROOM, AN_ROOM_0), N.x = 750, N.ob[O_ROOM].vis = 1;
  if (N.x < 400) N.x = 400;                                                         /* 46 */
  if (N.x > 600) N.x = 600;
  if (N.s0 == 12 || N.s0 == 13) N.x = 750;
  if (over(O_CREAK, AN_CREAK) && N.s0 == 37) set_anim(O_ROOM, AN_ROOM_0), N.ob[O_ROOM].vis = 1, N.s0 = 0, destroy(O_CREAK);
  if (N.s0 == 13 && edge(0)) N.s3 = 0;                                              /* 51 */
  if (N.s0 == 13) N.s3++;
  if (N.s0 == 13 && N.s3 > 10) set_anim(O_ROOM, AN_ROOM_0), N.ob[O_ROOM].vis = 1;
  if (playing(O_ROOM, AN_ROOM_19)) N.s3 = 0;
  /* the right door (55 to 77) */
  if (over(O_RSTART, AN_RSTART) && N.s0 == 16) N.s0 = 17, create(O_PEEKR), destroy(O_RSTART);
  if (N.s0 == 17 && !N.flash) set_anim(O_PEEKR, AN_PEEKR_12);
  if (N.s0 == 17 && N.foxy != P_RHF && N.chica.loc != P_RHF && N.fred != P_RHF && N.flash)
    set_anim(O_PEEKR, AN_PEEKR_14), N.view_right = 1;
  if (N.s0 == 17 && N.fred == P_RHF && N.kitchen3 <= 1 && !N.shadow && N.flash) set_anim(O_PEEKR, AN_PEEKR_19), N.view_right = 1;
  if (N.s0 == 17 && N.fred == P_RHF && N.kitchen3 > 1 && !N.shadow && N.flash) set_anim(O_PEEKR, AN_PEEKR_20), N.view_right = 1;
  if (N.s0 == 17 && N.fred == P_RHF && N.shadow && N.flash) set_anim(O_PEEKR, AN_PEEKR_21), N.view_right = 1;
  if (N.s0 == 17 && N.foxy == P_RHF && N.chica.loc != P_RHF && N.flash)
    set_anim(O_PEEKR, AN_PEEKR_18), N.view_right = 1, N.s0 = 36, N.foxy = P_LRC;
  if (N.s0 == 17 && N.chica.loc == P_RHF && N.flash)
    N.view_right = 1, N.s0 = 41, N.chica.loc = P_LRC, create(O_CHIDE);
  if (back && N.s0 == 17 && !N.flash && !N.shut) N.s0 = 18, set_anim(O_PEEKR, AN_PEEKR_13), back = false;
  if (over(O_PEEKR, AN_PEEKR_13) && N.s0 == 18) create(O_CARPET), N.s0 = 19, destroy(O_PEEKR), N.what_side = 4;
  if (N.s0 == 0 && edge(1)) N.what_side = 3;                                        /* 65 */
  if (N.s0 == 10 && N.shut) N.s0 = 20, set_anim(O_PEEKL, AN_PEEKL_15);              /* 70 */
  if (over(O_PEEKL, AN_PEEKL_15) && N.s0 == 20) N.s0 = 21, set_anim(O_PEEKL, AN_PEEKL_16);
  if (N.s0 == 21 && !N.shut) N.s0 = 22, set_anim(O_PEEKL, AN_PEEKL_17);
  if (over(O_PEEKL, AN_PEEKL_17) && N.s0 == 22) N.s0 = 10, set_anim(O_PEEKL, AN_PEEKL_12);
  if (N.s0 == 17 && N.shut) N.s0 = 23, set_anim(O_PEEKR, AN_PEEKR_15);
  if (over(O_PEEKR, AN_PEEKR_15) && N.s0 == 23) N.s0 = 24, set_anim(O_PEEKR, AN_PEEKR_16);
  if (N.s0 == 24 && !N.shut) N.s0 = 25, set_anim(O_PEEKR, AN_PEEKR_17);
  if (over(O_PEEKR, AN_PEEKR_17) && N.s0 == 25) N.s0 = 17, set_anim(O_PEEKR, AN_PEEKR_12);
  /* the closet (78 to 98) */
  if (ok && N.s0 == 0) N.s0 = 26, create(O_DARKC);
  if (over(O_DARKC, AN_DARKC) && N.s0 == 26) create(O_CARPET), N.ob[O_ROOM].vis = 0, N.s0 = 27, destroy(O_DARKC);
  if (over(O_CARPET, AN_CARPET) && N.s0 == 27) N.s0 = 28, destroy(O_CARPET), create(O_CLOSET);
  if (over(O_CLOSET, AN_CLOSET_0) && N.s0 == 28) N.s0 = 29, set_anim(O_CLOSET, AN_CLOSET_12);
  if (N.s0 == 29 && N.foxy != P_CLOSET && N.flash) set_anim(O_CLOSET, AN_CLOSET_13);
  if (N.s0 == 29 && N.foxy == P_CLOSET && N.flash) {
    static const uint8_t by_stage[4] = {AN_CLOSET_21, AN_CLOSET_18, AN_CLOSET_19, AN_CLOSET_20};
    set_anim(O_CLOSET, by_stage[N.foxy_s3 & 3]);
  }
  if (N.s0 == 29 && N.fred == P_CLOSET && N.flash) set_anim(O_CLOSET, N.shadow ? AN_CLOSET_23 : AN_CLOSET_22);
  if (N.s0 == 29 && !N.flash) set_anim(O_CLOSET, AN_CLOSET_12);
  if (back && N.s0 == 29 && !N.flash && !N.shut) N.s0 = 30, set_anim(O_CLOSET, AN_CLOSET_14), back = false;
  if (over(O_CLOSET, AN_CLOSET_14) && N.s0 == 30) destroy(O_CLOSET), create(O_CARPET), N.s0 = 31;
  if (N.s0 == 29 && N.shut && edge(2)) N.s0 = 32, set_anim(O_CLOSET, AN_CLOSET_15);
  if (over(O_CLOSET, AN_CLOSET_15) && N.s0 == 32) N.s0 = 33, set_anim(O_CLOSET, AN_CLOSET_17);
  if (N.s0 == 33 && !N.shut) N.s0 = 34, set_anim(O_CLOSET, AN_CLOSET_16);
  if (over(O_CLOSET, AN_CLOSET_16) && N.s0 == 34) N.s0 = 29, set_anim(O_CLOSET, AN_CLOSET_12);
  /* the bed (198 to 213) */
  if (back && (N.s0 == 0 || N.s0 == 2 || N.s0 == 5) && !N.flash && !N.shut) N.s0 = 41, create(O_OFF4), back = false;
  if (over(O_OFF4, AN_OFF4) && N.s0 == 41) create(O_BED), N.s0 = 42, destroy(O_OFF4);
  if (over(O_BED, AN_BED_0) && N.s0 == 42) N.s0 = 43, set_anim(O_BED, AN_BED_19);
  if (back && N.s0 == 43 && !N.flash && !N.shut) N.s0 = 44, set_anim(O_BED, AN_BED_20);
}

/* Foxy (103 to 138) */
static void foxy(void) {
  if (every(T_SECOND, 1000)) {
    N.foxy_s0 = (uint8_t)(rnd_n(10) + 1), N.bonnie.s0 = (uint8_t)(rnd_n(2) + 1);
    N.chica.s0 = (uint8_t)(rnd_n(2) + 1), N.fred_s0 = (uint8_t)(rnd_n(2) + 1);
  }
  if (N.foxy == P_LRC && N.foxy_s0 <= 5 && N.foxy_s1) N.foxy = P_LRL, N.foxy_s1 = 0, heard("Running, right to left");
  if (N.foxy == P_LRC && N.foxy_s0 > 5 && N.foxy_s1) N.foxy = P_LRR, N.foxy_s1 = 0, heard("Running, left to right");
  if (N.foxy == P_LRR && N.foxy_s0 <= 9 && N.foxy_s1) N.foxy = P_LRL, N.foxy_s1 = 0, heard("Running, right to left");
  if (N.foxy == P_LRR && N.foxy_s0 == 10 && N.foxy_s1 && !N.view_right)
    N.foxy = P_RHF, N.foxy_s1 = 0, heard("Running, to the right hall");
  if (N.foxy == P_LRL && N.foxy_s0 == 10 && N.foxy_s1 && !N.view_left)
    N.foxy = P_LHF, N.foxy_s1 = 0, heard("Running, to the left hall");
  if (N.foxy == P_LRL && N.foxy_s0 <= 9 && N.foxy_s1) N.foxy = P_LRR, N.foxy_s1 = 0, heard("Running, left to right");
  if (every(T_FOXY5, 5000) && chance(10, N.ai_foxy)) N.foxy_s1 = 1;
  if (!N.flash) N.view_left = N.view_right = N.view_closet = 0;                     /* 117 */
  if (over(O_PEEKL, AN_PEEKL_18)) N.s0 = 10, set_anim(O_PEEKL, AN_PEEKL_13);
  if (N.foxy == P_LHF && N.what_side == 5 && edge(3)) heard("Running into the closet"), N.foxy = P_CLOSET;
  if (N.foxy == P_RHF && N.what_side == 1 && edge(4)) heard("Running into the closet"), N.foxy = P_CLOSET;
  if (N.foxy == P_CLOSET && edge(5)) N.foxy_s2 = 0;
  if (N.foxy == P_CLOSET && N.foxy_s1 && !N.view_closet) N.foxy_s2++, N.foxy_s1 = 0;
  if (N.s0 == 29 && N.flash) N.view_closet = 1;
  if (N.s0 == 33 && N.foxy == P_CLOSET && every(T_FOXY1, 1000) && N.foxy_s2) N.foxy_s2--;   /* (129: not below 0) */
  if (N.foxy_s2 > 10) N.foxy_s2 = 10;
  N.foxy_s3 = N.foxy_s2 < 2 ? 0 : N.foxy_s2 < 4 ? 1 : N.foxy_s2 < 6 ? 2 : 3;        /* 131 to 134 */
  if (over(O_PEEKR, AN_PEEKR_18) && N.s0 == 36) N.s0 = 17, set_anim(O_PEEKR, AN_PEEKR_14);
  if (N.foxy == P_CLOSET && once(0)) N.foxy_s5 = 1, N.foxy_s2 = (uint8_t)(3 + rnd_n(5));
  N.foxy_got_you = N.foxy_s2 >= 10;
}

/* Bonnie and Chica (139 to 197) */
static void steps(int side, bool soft) {
  static const char *const s[4] = {"Footsteps, left", "Footsteps, right", "Soft steps, left", "Soft steps, right"};
  heard(s[side + 2 * soft]);
}

static void bonnie_chica(void) {
  if (every(T_BONNIE5, 5000) && chance(20, N.ai_bonnie) && N.listening != 1 && N.door_lock != 2 && !N.left_shut)
    N.bonnie.s1 = 1;
  if (every(T_CHICA5, 5000) && chance(20, N.ai_chica) && N.listening != 2 && N.door_lock != 2 && !N.right_shut)
    N.chica.s1 = 1;
  bool fred_free = N.fred != P_CLOSET && N.fred != P_BED && N.fred != P_LHF && N.fred != P_RHF;
  if (every(T_FRED3, 3000) && !N.shadow && chance(20, N.ai_fredbear) && fred_free) N.fred_s1 = 1;
  if (every(T_FRED2, 2000) && N.shadow && chance(20, N.ai_fredbear) && fred_free) N.fred_s1 = 1;
  if (N.listening == 1) N.bonnie.s1 = 0;
  if (N.door_lock == 1) N.bonnie.s1 = N.chica.s1 = 0;
  if (N.listening == 2) N.chica.s1 = 0;
  Anim *b = &N.bonnie, *c = &N.chica;
  if (b->s1 && b->loc == P_LRC) moved(b, P_LRL), steps(0, true);
  if (b->loc == P_LRL && b->s1 && b->s0 == 1) moved(b, P_LRC), steps(0, true);
  if (b->loc == P_LRL && b->s1 && b->s0 == 2 && !N.view_left) moved(b, P_LHF), steps(0, false);
  if (b->s1 && b->loc == P_LHF) moved(b, P_LHN), steps(0, false);
  if (every(T_BONNIE10, 10000) && b->s1 && b->loc == P_LHN && N.listening != 1) moved(b, P_LHF), steps(0, false);
  if (c->s1 && c->loc == P_LRC) moved(c, P_LRR), steps(1, true);
  if (c->s1 && c->loc == P_KITCHEN) moved(c, P_LRR), steps(1, true);
  if (c->s1 && c->loc == P_LRR && c->s0 == 1) moved(c, P_KITCHEN), steps(1, true);
  if (c->s1 && c->loc == P_LRR && c->s0 == 2 && !N.view_right) moved(c, P_RHF), steps(1, false);
  if (c->s1 && c->loc == P_RHF) moved(c, P_RHN), steps(1, false);
  if (every(T_CHICA10, 10000) && c->s1 && c->loc == P_RHN && N.listening != 2) moved(c, P_RHF), steps(1, false);
  if (over(O_BHIDE, AN_BHIDE) && N.s0 == 40) N.s0 = 10, set_anim(O_PEEKL, AN_PEEKL_13), destroy(O_BHIDE);
  if (N.s0 == 40 && N.foxy == P_LHF) N.foxy = P_LRC;
  if (over(O_CHIDE, AN_CHIDE) && N.s0 == 41) N.s0 = 17, set_anim(O_PEEKR, AN_PEEKR_14), destroy(O_CHIDE);
  if (N.s0 != 10 && N.s0 != 17) N.listening = 0;                                   /* 174 */
  if (N.flash || N.shut) N.listening = 0;
  if (N.s0 == 10 && !N.flash && !N.shut) N.listening = 1;
  if (N.s0 == 17 && !N.flash && !N.shut) N.listening = 2;
  if (N.listening == 1 && b->loc == P_LHN) g.hold = "Breathing";                    /* 180, 181 */
  if (N.listening == 2 && c->loc == P_RHN) g.hold = "Breathing";
  N.left_shut = N.s0 == 21, N.right_shut = N.s0 == 24;                              /* 182 to 185 */
  if (b->s5 == 1 && N.left_shut && !N.door_lock) b->loc = P_LHN, N.door_lock = 1;
  if (every(T_BONNIE3, 3000) && b->s5 == 2 && N.left_shut && !N.door_lock && b->loc == P_LHN)
    b->loc = P_LRL, steps(0, false), b->s1 = 0, N.door_lock = 2;
  if (c->s5 == 1 && N.right_shut && !N.door_lock) c->loc = P_RHN, N.door_lock = 1;
  if (every(T_CHICA3, 3000) && c->s5 == 2 && N.right_shut && !N.door_lock && c->loc == P_RHN)
    c->loc = P_LRR, steps(1, false), c->s1 = 0, N.door_lock = 2;
  if (b->loc == P_LHN && N.view_left && !N.got_you && !N.gameover)                  /* 190 */
    N.got_you = 1, create(O_JUMP), set_anim(O_JUMP, AN_JUMP_13), N.gameover = 1;
  if (c->loc == P_RHN && N.view_right && !N.got_you && !N.gameover)
    c->loc = P_LRR, N.got_you = 2, create(O_JUMP), set_anim(O_JUMP, AN_JUMP_12), N.gameover = 1;
  if (N.gameover && edge(6)) N.s0 = 99;
  if (!N.right_shut && !N.left_shut) N.door_lock = 0;                               /* 197 */
}

/* the bed and the Freddles (204 to 246) */
static void bed(void) {
  if (over(O_BED, AN_BED_20) && N.s0 == 44) {
    if (!N.chica.s7 && !N.bonnie.s7) N.s0 = 45, destroy(O_BED), create(O_ON4), N.fredcheck = 1;
    else if (N.chica.s7 && !N.gameover) destroy(O_BED), create(O_CUPCAKE), N.gameover = 1;
    else if (N.bonnie.s7 && !N.gameover) destroy(O_BED), N.gameover = 1, create(O_BONNIEATK);
  }
  if (over(O_ON4, AN_ON4) && N.s0 == 45) set_anim(O_ROOM, AN_ROOM_0), N.s0 = 0, N.ob[O_ROOM].vis = 1, destroy(O_ON4);
  if (N.s0 == 43 && !N.flash) set_anim(O_BED, AN_BED_19), N.view_bed = 0;
  if (N.s0 == 43 && N.flash) N.view_bed = 1;
  if (every(T_FREDDY4, 4000) && !N.view_bed) N.freddy += N.ai_freddy;               /* 216 */
  if (N.freddy < 0) N.freddy = 0;
  if (N.s0 != 43) N.view_bed = 0;
  if (N.view_bed && every(T_FREDDY50, 50)) N.freddy--;
  if (N.view_bed) {
    int an = -1;
    if (N.freddy <= 10) an = N.random >= 4 ? AN_BED_18 : N.random == 1 ? AN_BED_23 : N.random == 2 ? AN_BED_24 : AN_BED_25;
    if (N.freddy > 10 && N.freddy <= 20) an = AN_BED_16, N.random = 4;
    if (N.freddy > 20 && N.freddy <= 30) an = AN_BED_14, N.random = 4;
    if (N.freddy > 30) an = AN_BED_12, N.random = 4;
    if (N.fred == P_BED) an = N.shadow ? AN_BED_22 : AN_BED_21;
    set_anim(O_BED, an);
  }
  if (N.view_bed && N.freddy == 30 && edge(7)) create(O_LEAVE3);
  if (N.view_bed && N.freddy == 20 && edge(8)) create(O_LEAVE2);
  if (N.view_bed && N.freddy == 10 && edge(9)) create(O_LEAVE1);
  if (over(O_LEAVE3, AN_LEAVE3)) destroy(O_LEAVE3);
  if (over(O_LEAVE2, AN_LEAVE2)) destroy(O_LEAVE2);
  if (over(O_LEAVE1, AN_LEAVE1)) destroy(O_LEAVE1);
  if (N.s0 == 43 && N.view_bed && N.freddy >= 60 && !N.gameover)
    N.gameover = 1, create(O_JUMP), set_anim(O_JUMP, AN_JUMP_14);
  if (every(T_FREDDY3, 3000) && N.s0 == 43 && !N.view_bed && N.freddy >= 60 && !N.gameover)
    N.gameover = 1, create(O_JUMP), set_anim(O_JUMP, AN_JUMP_14);
  if (N.s0 == 29 && N.foxy == P_CLOSET && N.foxy_s3 == 3 && N.flash && once(1)) create(O_BITE);
  if (over(O_BITE, AN_BITE)) destroy(O_BITE);
  if (over(O_BED, AN_BED_20) && (N.foxy_got_you || N.bed_foxy) && !N.gameover)     /* 245 */
    N.gameover = 1, N.foxy_s5 = 2, N.s0 = 39, create(O_JUMP);
  if (N.s0 == 43 && !N.gameover && N.foxy_got_you) N.force_turn = 1;
}

/* the dark that flickers in as they come closer, and the black before a scare (247 to 278) */
static void attack(int o) {
  static const uint8_t gone[] = {O_OFF4, O_ON4, O_CREAK, O_BHIDE, O_CHIDE, O_DARKC, O_DARKR, O_DARKL, O_LSTART,
                                 O_RSTART, O_CARPET, O_PEEKL, O_PEEKR, O_CLOSET, O_BED};
  N.gameover = 1;
  for (unsigned i = 0; i < sizeof gone; i++) destroy(gone[i]);
  N.flash_vis = 0, N.bf3 = 0;
  create(o);
}

static void danger(void) {
  if (N.bf0 > 0) N.bf1++, N.bf0--;
  if (N.bf1 > 10) N.bf1 = 0;
  if (!N.bf1) N.flash_vis = 0;
  if (N.bf1 > 0) N.flash_vis = 1, N.bf_alpha = (int16_t)(rnd_n(50) + 175 - N.bf1 * 15 - N.bf2);
  if (!N.bf0) N.bf1 = 0;
  uint8_t bd = N.bonnie.loc == P_LHF ? 1 : N.bonnie.loc == P_LHN ? 2 : 0;
  if (!bd) N.bonnie.s6 = N.bonnie.s7 = 0;                                           /* 254 */
  uint8_t cd = N.chica.loc == P_RHF ? 1 : N.chica.loc == P_RHN ? 2 : 0;
  uint8_t fd = N.foxy == P_CLOSET ? 2 : 0;
  uint8_t fr = N.freddy < 20 ? 0 : N.freddy < 30 ? 1 : N.freddy < 50 ? 3 : 5;
  if (N.freddy >= 30 && N.freddy < 50) N.bf2 = 50;
  if (every(T_DANGER, 200)) N.total = (uint8_t)(bd + cd + fd + fr);
  if (N.total == 2 && every(T_D2, 5000) && (int)rnd_n(10) < N.total) N.bf0 = (int16_t)(rnd_n(5) + 5);
  if (N.total == 3 && every(T_D3, 4000) && (int)rnd_n(10) < N.total) N.bf0 = (int16_t)(rnd_n(10) + 10);
  if (N.total == 4 && every(T_D4, 3000) && (int)rnd_n(10) < N.total) N.bf0 = (int16_t)(rnd_n(10) + 10);
  if (N.total >= 5 && every(T_D5, 2000) && (int)rnd_n(10) < N.total) N.bf0 = (int16_t)(rnd_n(30) + 10);
  if (N.gameover) N.flash_vis = 0;                                                  /* 268 */
  if (N.freddy >= 50) N.bf2 = 100;
  if (N.freddy < 30) N.bf2 = 0;
  if (N.freddy >= 80 && edge(10)) N.bf3 = (int16_t)(50 + rnd_n(100));
  if (N.freddy < 80 && !N.ai_fredbear) N.bf3 = 0;
  if (N.gameover) N.bf3 = 0;
  if (N.bf3 > 0) N.bf0 = 10, N.bf3--;
  if (N.bf3 == 1 && !N.gameover && !N.ai_fredbear && edge(11)) attack(O_FREDDYATK);
  if (N.bf3 == 1 && !N.gameover && N.ai_fredbear && edge(12)) attack(O_FREDBEAR);
  if (N.bf3 > 0 && N.bf3 < 30) N.bf_alpha = 0, N.flash_vis = 1;
}

/* who comes on: their AI over 0 puts them in the living room, 0 takes them away (279 to 296) */
static void comings(void) {
  if (N.ai_bonnie && edge(13)) N.bonnie.loc = P_LRC;
  if (!N.ai_bonnie) N.bonnie.loc = P_AWAY;
  if (N.ai_chica && edge(14)) N.chica.loc = P_LRC;
  if (!N.ai_chica) N.chica.loc = P_AWAY;
  if (N.ai_foxy && edge(15)) N.foxy = P_LRC;
  if (!N.ai_foxy) N.foxy = P_AWAY;
  Anim *b = &N.bonnie, *c = &N.chica;
  if (c->loc != P_RHN && c->loc != P_RHF && c->s6 < 20) c->s6 = 0;
  if (c->loc == P_RHN && every(T_CHICA_S6, 1000)) c->s6++;
  if (c->s6 >= 20 - N.night && N.view_bed) c->s7 = 1;
  if (N.view_right) c->s6 = 0;
  if (b->loc == P_LHN && every(T_BONNIE_S6, 1000)) b->s6++;
  if (N.view_left) b->s6 = b->s7 = 0;
  if (b->s6 > 20 - N.night && N.view_bed) b->s7 = 1;
  if (c->loc == P_KITCHEN) g.hold = N.listening == 2 ? "Pots and pans, close" : "Pots and pans";
}

/* Fredbear, and Nightmare (297 to 365) */
static void fredbear(void) {
  if (N.ai_fredbear && edge(16)) N.fred = P_LRC;
  if (N.fred == P_LRC && N.fred_s1 && N.fred_s0 == 1) N.fred_s1 = 0, N.fred = P_LRL, heard("Running, right to left");
  if (N.fred == P_LRC && N.fred_s1 && N.fred_s0 == 2) N.fred_s1 = 0, N.fred = P_LRR, heard("Running, left to right");
  if (N.fred == P_LRL && N.fred_s1 && N.fred_s0 == 1 && !N.view_left) N.fred_s1 = 0, N.fred = P_LHF;
  if (N.fred == P_LRL && N.fred_s1 && N.fred_s0 == 2) N.fred_s1 = 0, N.fred = P_LRR, heard("Running, left to right");
  if (N.fred == P_LRR && N.fred_s1 && N.fred_s0 == 1) N.fred_s1 = 0, N.fred = P_LRL, heard("Running, right to left");
  if (N.fred == P_LRR && N.fred_s1 && N.fred_s0 == 2 && !N.view_right) N.fred_s1 = 0, N.fred = P_RHF;
  if (N.s0 == 10 && N.view_left && !N.kitchen2 && edge(17)) N.kitchen2++, N.kitchen4 = 1;   /* 305 */
  if (N.s0 == 10 && !N.view_left && N.kitchen2 == 1 && N.kitchen4 == 1 && edge(18)) N.kitchen2++;
  if (N.s0 == 17 && N.view_right && !N.kitchen3 && edge(19)) N.kitchen3++, N.kitchen4 = 1;
  if (N.s0 == 17 && !N.view_right && N.kitchen3 == 1 && N.kitchen4 == 1 && edge(20)) N.kitchen3++;
  if (every(T_FRED_L3, 3000) && N.fred == P_LHF && N.left_shut) N.fred = P_LRR, heard("Running, left to right");
  if (every(T_FRED_R3, 3000) && N.fred == P_RHF && N.right_shut) N.fred = P_LRL, heard("Running, right to left");
  if (N.fred != P_LHF && edge(21)) N.fred_s4 = 0;
  if (N.fred != P_RHF && edge(22)) N.fred_s5 = 0;
  if (N.fred == P_LHF && every(T_FRED_S4, 1000)) N.fred_s4++;
  if (N.fred == P_RHF && every(T_FRED_S5, 1000)) N.fred_s5++;
  if (N.fred_s4 >= 10 && N.listening == 2) N.fred = N.fred_s0 == 1 ? P_CLOSET : N.fred_s0 == 2 ? P_BED : N.fred;
  if (N.fred_s5 >= 10 && N.listening == 1) N.fred = N.fred_s0 == 1 ? P_CLOSET : N.fred_s0 == 2 ? P_BED : N.fred;
  if (N.fred == P_LHF && edge(23)) N.fred_s8 = 0;
  if (N.fred == P_RHF && edge(24)) N.fred_s9 = 0;
  if (N.fred == P_LHF && N.view_left && N.fred_s8 < 255) N.fred_s8++;
  if (N.fred == P_RHF && N.view_right && N.fred_s9 < 255) N.fred_s9++;
  int stare = N.night == 5 ? 30 : N.night == 6 ? 25 : N.night >= 7 ? 20 : 255;    /* 328 to 333 */
  if (N.fred_s8 > stare && edge(25)) N.bf3 = 50;
  if (N.fred_s9 > stare && edge(26)) N.bf3 = 50;
  if (every(T_CLOSET_L, 3000) && N.fred == P_CLOSET && N.s0 == 33 && N.fred_s0 == 1)
    N.fred = P_LRL, heard("Running, right to left");
  if (every(T_CLOSET_R, 3000) && N.fred == P_CLOSET && N.s0 == 33 && N.fred_s0 == 2)
    N.fred = P_LRR, heard("Running, left to right");
  if (N.fred == P_BED && edge(27)) N.fred_s7 = 0;
  if (every(T_FRED_S7, 1000) && N.fred == P_BED && N.view_bed) N.fred_s7++;
  if (N.fred == P_BED && N.fred_s7 > 1 && (N.fred_s0 == 1 || N.fred_s0 == 2)) {    /* 338, 339 */
    heard(N.fred_s0 == 1 ? "Running, right to left" : "Running, left to right");
    N.fred = N.fred_s0 == 1 ? P_LRL : P_LRR;
    N.bf6 = 20, N.flash_vis = 1, N.bf_alpha = 0, N.bf18 = 5;
  }
  if (N.bf6 > 0) N.bf_alpha = (int16_t)rnd_n(50), N.bf6--, N.flash_vis = 1;
  if (N.bf18 > 0) N.bf18--, N.flash_vis = 1, N.bf_alpha = 0;
  if (N.ai_fredbear && N.fred != P_BED && every(T_LAUGH, 10000) && rnd_n(10) == 1) heard("Laughing");
  if (N.fred != P_BED && edge(28)) N.fred_s6 = 0;
  if (N.fred != P_CLOSET && edge(29)) N.fred_s6 = 0;
  if (N.fred == P_BED && every(T_FRED_S6B, 1000)) N.fred_s6++;
  if (N.fred == P_CLOSET && every(T_FRED_S6C, 1000)) N.fred_s6++;
  if (N.fredcheck && N.fred_s6 >= 10 && !N.gameover) N.gameover = 1, create(O_FREDBEAR);
  if (N.fredcheck && N.fred_s6 < 10) N.fredcheck = 0;
  if (N.fred_s6 >= 20 - N.shadow * 10 && edge(30)) N.bf3 = 50;
  if (N.fred_s12 >= 25 && edge(31)) N.bf3 = 50;
  if (N.gameover && edge(32)) {   /* 363 */
    static const uint8_t gone[] = {O_CARPET, O_PEEKL, O_PEEKR, O_CLOSET, O_BED, O_OFF4, O_ON4, O_CREAK, O_BHIDE,
                                   O_DARKC, O_CHIDE, O_DARKR, O_DARKL, O_LSTART, O_RSTART, O_LEAVE3, O_LEAVE2,
                                   O_LEAVE1, O_BITE};
    for (unsigned i = 0; i < sizeof gone; i++) destroy(gone[i]);
  }
  if (N.ai_fredbear && every(T_FRED_S12, 1000) && N.fred_s12 < 255) N.fred_s12++;
  if (alive(O_CARPET) && edge(33)) N.fred_s12 = N.fred_s13 = N.fred_s14 = 0;
}

/* the hours and who is on (366 to 392) */
static void hours(void) {
  if (every(T_HOUR, 60000)) N.hour++;
  if (N.hour > 12) N.hour = 1;
  if (N.fade < 255) N.fade = (int16_t)(N.fade + 5);
  int n = N.night, h = N.hour;
  if (n == 1 && h == 2 && edge(34)) N.ai_bonnie = 1, N.ai_chica = 1, N.ai_freddy = 1;
  if (n == 1 && h == 3 && edge(35)) N.ai_bonnie += 2, N.ai_chica += 1, N.ai_freddy += 1;
  if (n == 2 && edge(36)) N.ai_bonnie = 5, N.ai_chica = 5, N.ai_freddy = 2, N.ai_foxy = 1;
  if (n == 2 && h == 3 && edge(37)) N.ai_freddy += 1, N.ai_bonnie += 2, N.ai_chica += 2, N.ai_foxy += 3;
  if (n == 3 && edge(38)) N.ai_foxy = 10, N.ai_freddy = 3, N.ai_bonnie = 7, N.ai_chica = 7;
  if (n == 3 && h == 3 && edge(39)) N.ai_bonnie += 3, N.ai_chica += 3;
  if (n == 4 && edge(40)) N.ai_freddy = 4, N.ai_bonnie = 10, N.ai_foxy = 5, N.ai_chica = 10;
  if (n == 4 && h == 3 && edge(41)) N.ai_chica += 2, N.ai_bonnie += 2, N.ai_foxy += 5;
  /* 379 (only at the bed: in the game a turn forced while you are elsewhere leaves you stuck) */
  if (N.force_turn && (N.s0 == 43 || N.s0 == 44)) set_anim(O_BED, AN_BED_20), N.s0 = 44;
  N.force_turn = 0;
  if (every(T_TURN_B, 4000) && N.s0 == 43 && N.bonnie.s7) N.force_turn = 1;
  if (every(T_TURN_C, 4000) && N.s0 == 43 && N.chica.s7) N.force_turn = 1;
  if (every(T_FRED_S13, 1000) && N.fred_s13 < 255) N.fred_s13++;
  if (N.fred_s13 >= 30 && !N.view_bed && N.night > 1 && every(T_FREDDY200, 200)) N.freddy++;
  if (every(T_FRED_S14, 1000) && N.view_bed && N.fred_s14 < 255) N.fred_s14++;
  if (N.fred_s14 >= 15) N.foxy = P_CLOSET, N.foxy_got_you = 1, N.bed_foxy = 1, N.force_turn = 1;   /* 385 */
  if (g.reward && n < 6 && edge(42)) g.reward = 0, N.hour = 2;
  if (n == 6 && edge(43)) N.ai_bonnie = 12, N.ai_foxy = 10, N.ai_freddy = 5, N.ai_chica = 12;
  if (n == 6 && h == 4 && edge(44)) N.ai_bonnie = N.ai_chica = N.ai_foxy = N.ai_freddy = 0, N.ai_fredbear = 15;
  if (N.shadow == 1 && edge(45))
    N.ai_bonnie = 15, N.ai_fredbear = 0, N.night = 7, N.ai_chica = 15, N.ai_foxy = 15, N.ai_freddy = 6;
  if (N.shadow == 1 && h == 4 && edge(46)) N.ai_freddy = N.ai_foxy = N.ai_chica = N.ai_bonnie = 0, N.ai_fredbear = 20;
  if (N.shadow == 2 && edge(45))
    N.ai_bonnie = 20, N.ai_fredbear = 0, N.night = 8, N.ai_chica = 20, N.ai_foxy = 20, N.ai_freddy = 6;
  if (N.shadow == 2 && h == 4 && edge(46)) N.ai_freddy = N.ai_foxy = N.ai_chica = N.ai_bonnie = 0, N.ai_fredbear = 20;
  if (N.s0 == 0 && edge(47)) N.random = (uint8_t)(rnd_n(100) + 1);
}

static void late(void) {
  if (N.ai_fredbear) N.freddy = 0;                                                  /* 414 */
  int n = N.night;
  if (n >= 2 && n <= 4 && once(2)) N.force_bonnie = (uint8_t)(rnd_n(4) + 2), N.force_chica = (uint8_t)(rnd_n(3) + 3);
  uint8_t h = N.hour == 12 ? 0 : N.hour;
  if (N.s0 == 0 && N.force_bonnie && N.force_bonnie <= h && !N.got_you && N.hour != 12 && once(3))
    N.bonnie.loc = P_LHN, N.bonnie.s1 = 0;
  if (N.s0 == 0 && N.force_chica && N.force_chica <= h && !N.got_you && N.hour != 12 && once(4))
    N.chica.loc = P_RHN, N.chica.s1 = 0;
  if (N.bonnie.loc == P_LHF) N.bonnie.s5 = 1;                                       /* 420 to 423 */
  if (N.bonnie.loc == P_LHN) N.bonnie.s5 = 2;
  if (N.chica.loc == P_RHF) N.chica.s5 = 1;
  if (N.chica.loc == P_RHN) N.chica.s5 = 2;
  if (alive(O_CARPET) && edge(49)) N.kitchen2 = N.kitchen4 = N.kitchen3 = 0;
  if (n == 5 && once(5)) N.fred_s18 = 4;
  if ((n == 6 || n == 7) && once(6)) N.fred_s18 = 2;
  bool free = N.fred != P_CLOSET && N.fred != P_BED;
  if (N.ai_fredbear && N.fred_s18 && free && N.fred_s0 == 1 && every(T_FRED30A, 30000) && rnd_n(2) == 1)
    N.fred_s18--, N.fred = P_CLOSET, heard("Laughing");
  if (N.ai_fredbear && N.fred_s18 && free && N.fred_s0 == 2 && every(T_FRED30B, 30000) && rnd_n(2) == 1)
    N.fred_s18--, N.fred = P_BED, heard("Laughing");
  if (N.fred != P_LHF && N.fred != P_RHF) N.fred_s19 = 0;                           /* 430 to 433 */
  if (N.fred == P_LHF && every(T_FRED_S19L, 1000)) N.fred_s19++;
  if (N.fred == P_RHF && every(T_FRED_S19R, 1000)) N.fred_s19++;
  if (N.fred_s19 >= 20 - n) N.fred_s19 = 0, N.bf3 = 50;
  if (N.night > 1 || (N.hour > 2 && N.hour != 12)) N.warning = 0;                   /* 441, 442 */
}

/* the keys, read at the step's end as the game reads them (443 to 456) */
static void keys(void) {
  bool light = N.s0 == 10 || N.s0 == 17 || N.s0 == 29 || N.s0 == 43;
  bool door = N.s0 == 10 || N.s0 == 17 || N.s0 == 29 || N.s0 == 20 || N.s0 == 21 || N.s0 == 23 || N.s0 == 24 ||
              N.s0 == 32 || N.s0 == 33;
  bool up = g.held & (K_UP | K_DIGIT(8));
  N.shut = door && up;
  N.flash = light && !up && (g.held & (K_OK | K_EXE | K_DIGIT(5)));
  if (N.s0 == 0 && edge(50)) N.x = 500;   /* 457 */
}

void night_step(void) {
  N.frame++;
  g.hold = NULL;
  you();
  if (N.night == 5 && once(7)) N.ai_fredbear = 12;   /* 102 */
  foxy();
  bonnie_chica();
  bed();
  danger();
  comings();
  fredbear();
  hours();
  late();
  keys();
  animate();
}

const char *night_keys(void) {
  switch (N.s0) {
  case 0: return "Left Right look   OK closet   Down bed";
  case 2: case 5: return "OK the door   Down bed";
  case 10: case 17: case 20: case 21: case 22: case 23: case 24: case 25: return "OK light   Up hold the door   Down back";
  case 29: case 32: case 33: case 34: return "OK light   Up hold the closet   Down back";
  case 43: return "OK light   Down turn around";
  }
  return NULL;
}

/* ------------------------------------------------------------ Fun with Plushtrap (its frame's 62 events) */
/* He walks from his chair down the hall to the X, through the rooms on either side; the light stops him, and
 * sends him back to the chair from the hall's start. Catch him on the X with the light and the next night
 * starts at 2 AM. */
#define P g.p
enum { PL_CHAIR, PL_HALL1, PL_FARL, PL_FARR, PL_HALL2, PL_CLOSEL, PL_CLOSER, PL_HALL3, PL_GOT };

static void plush_anim(int an) {
  if (P.an == an) return;
  P.an = (uint8_t)an, P.img = 0, P.cnt = 0, P.plays = ANIMS[an].plays, P.done = 0;
}
static bool plush_over(int an) { return P.an == an && P.done; }
static bool plush_every(int id, int ms) {
  uint16_t p = (uint16_t)(ms * 60 / 1000);
  if (!P.every[id]) P.every[id] = p;
  if (--P.every[id]) return false;
  P.every[id] = p;
  return true;
}

void plush_begin(bool from_extras) {
  memset(&P, 0, sizeof P);
  P.an = 0xFF;
  plush_anim(AN_PLUSH_0);
  P.from_extras = from_extras;
  P.random = (uint8_t)(rnd_n(2) + 1);                                              /* 6 */
  P.aggressive = (uint8_t)rnd_n(3);                                                /* 60 */
  g.reward = 0;                                                                     /* 61 */
  int n = g.n.night;                                                                /* 52 to 57 */
  P.counter = from_extras ? 30 : n <= 1 ? 90 : n == 2 ? 60 : n == 3 ? 45 : 30;
}

/* returns 1 when caught on the X (GREAT!, then the next night), 2 when lost (TOO BAD.) */
static int plush_events(void) {
  bool held = g.held & (K_OK | K_EXE | K_DIGIT(5));
  if (!held && P.s0 != 99 && P.s0 != 98 && !(P.an == AN_PLUSH_20 && !P.done)) P.light = 0, P.s0 = 0;   /* 0 */
  if (held) P.light = 1;
  if (plush_every(0, 1000) && !g.reward) P.counter--;
  if (plush_every(1, 1000)) P.random = (uint8_t)(rnd_n(2) + 1);
  if (!P.light) P.active = (int16_t)(P.active + 1 + P.aggressive);                 /* 7 */
  if (plush_every(2, 2000) && P.active >= 400 + (int)rnd_n(100))
    P.active = 200, P.move = 1, P.aggressive = (uint8_t)rnd_n(3);
  static const char *const quick = "Quick steps", *const rl = "Running, right to left", *const lr = "Running, left to right";
  if (P.loc == PL_CHAIR && P.move) P.move = 0, P.loc = PL_HALL1, caption(quick);
  if (P.loc == PL_HALL1 && P.move && P.random == 1) P.loc = PL_FARL, P.move = 0, caption(rl);
  if (P.loc == PL_HALL1 && P.move && P.random == 2) P.loc = PL_FARR, P.move = 0, caption(lr);
  if (P.loc == PL_FARR && P.move) P.loc = PL_HALL2, P.move = 0, caption(quick);
  if (P.loc == PL_FARL && P.move) P.loc = PL_HALL2, P.move = 0, caption(quick);
  if (P.loc == PL_HALL2 && P.move && P.random == 1) P.loc = PL_CLOSEL, P.move = 0, caption(rl);
  if (P.loc == PL_HALL2 && P.move && P.random == 2) P.loc = PL_CLOSER, P.move = 0, caption(lr);
  if (P.loc == PL_CLOSEL && P.move) P.loc = PL_HALL3, P.move = 0, caption(lr);
  if (P.loc == PL_CLOSER && P.move) P.move = 0, P.loc = PL_HALL3, caption(lr);
  if (P.loc == PL_HALL3 && P.move) P.move = 0, P.loc = PL_GOT, caption(lr);
  if (P.loc == PL_HALL1 && P.light) P.loc = PL_CHAIR, P.s0 = 2, plush_anim(AN_PLUSH_14), caption(quick);   /* 19 */
  if (P.s0 == 0) plush_anim(AN_PLUSH_0);
  if (P.s0 == 1 && P.loc == PL_CHAIR) plush_anim(AN_PLUSH_12);
  if (P.s0 == 1 && P.loc == PL_HALL2) plush_anim(AN_PLUSH_21);
  if (P.s0 == 1 && P.loc == PL_HALL3) plush_anim(AN_PLUSH_22);
  if (P.s0 == 1 && P.loc != PL_CHAIR && P.loc != PL_HALL2 && P.loc != PL_HALL3) plush_anim(AN_PLUSH_13);
  if (plush_over(AN_PLUSH_14) && P.s0 == 2) P.s0 = 1;
  if (held && P.s0 == 0 && P.active > 200) {                                        /* 26 to 31 */
    static const uint8_t caught[9] = {0, 0, AN_PLUSH_15, AN_PLUSH_16, AN_PLUSH_19, AN_PLUSH_18, AN_PLUSH_17, AN_PLUSH_20, 0};
    if (caught[P.loc]) P.s0 = 3, plush_anim(caught[P.loc]);
    if (P.loc == PL_HALL2 || P.loc == PL_HALL3) caption("A thud");
  }
  for (int an = AN_PLUSH_15; an <= AN_PLUSH_19; an++)
    if (plush_over(an) && P.s0 == 3) P.s0 = 1;
  if (plush_over(AN_PLUSH_20) && P.s0 == 3) P.s0 = 98, g.reward = 1;               /* 37 */
  if (P.light) P.active = 0;                                                        /* 39 */
  if (held && P.s0 == 0) P.s0 = 1;
  if (P.loc == PL_HALL3 && !P.light) P.active = (int16_t)(P.active + 5);           /* 43 */
  if (P.loc == PL_GOT && P.light && P.s0 != 99) P.s0 = 99, plush_anim(AN_PLUSH_23);
  if (plush_over(AN_PLUSH_23)) return 2;
  if (P.s0 == 98) plush_anim(AN_PLUSH_22), P.s2++;
  if (P.s2 > 150) return 1;
  if (P.counter <= 0 && P.s0 != 98) return 2;
  if (P.loc == PL_HALL3) P.aggressive = 2;                                          /* 59 */
  return 0;
}

static int plush_result;
void plush_step(void) {
  P.frame++;
  int r = plush_events();
  const AnimDef *d = &ANIMS[P.an];
  if (!P.done && d->speed) {
    P.cnt = (uint8_t)(P.cnt + d->speed);
    while (P.cnt >= 100) {
      P.cnt = (uint8_t)(P.cnt - 100);
      if (P.img + 1 < d->n) P.img++;
      else if (P.plays == 1) {
        P.done = 1;
        break;
      } else {
        if (P.plays) P.plays--;
        P.img = 0;
      }
    }
  }
  plush_result = r;
}
int plush_done(void) { return plush_result; }
int plush_pic(void) { return anim_pic(P.an, P.img); }
