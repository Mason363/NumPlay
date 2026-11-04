/* 2 3 4 Players: what the minigames share (kit.c, app.c), and the minigames (g_*.c).
 *
 * A minigame plays rounds: start() sets one up, step() moves it on by TICK milliseconds with each player's
 * key, draw() paints it (the launcher's strips: called once a strip, every shape clips itself). It ends a round
 * with win() or tie(), or, in a last-one-standing game, with out() until one side is left. */
#ifndef FP_H
#define FP_H
#include <stdbool.h>
#include <stdint.h>
#include "../../../launcher/src/gfx.h"

#define NMAX 6
#define TICK 10    /* ms, a step of the games */
#define DT 0.01f   /* the same in seconds */
#define TOP 22     /* the score bar: the games below it */
#define PI 3.14159265f
#define WHITE 0xFFFF
#define INK HEX(0x1B1733)  /* the menus' night violet */
#define GOLD HEX(0xFFC93C)

/* ---- the round, for the minigame playing it */
extern int np;                                    /* players: 2 to 6 */
extern bool teams;                                /* teams: players 0, 2, 4 are the red team, 1, 3, 5 the blue */
extern uint8_t hold[NMAX], tap[NMAX], rel[NMAX];  /* each one's key this step: held, just pressed, just let go */
extern uint8_t gone[NMAX];                        /* out of the round */
extern int32_t rt;                                /* the round's time in ms, from GO */
extern color_t col[NMAX], lite[NMAX], dark[NMAX]; /* each player's colours */
int side(int p);    /* p's side: their team, or themselves */
void out(int p);    /* p is out (in a last-one-standing game, the last side left takes the round) */
void win(int p);    /* p's side takes the round */
void tie(void);     /* nobody does: the round is played again */
bool over(void);    /* the round is decided: the game plays on a moment, keys off */
int alive(void);    /* players still in */
/* where p's key is on the keypad, seen from its middle (radians, 0 right, y down): where p starts */
float seat(int p);
/* p's key, as printed on it ("shift", "0"...; backspace is drawn: keyname(p) gives "" for it) */
const char *keyname(int p);

/* ---- chance and numbers */
uint32_t rnd(void);
int rn(int n);            /* 0 to n - 1 */
float rf(void);           /* 0 to 1 */
float rr(float a, float b);
float fsin(float a);
float fcos(float a);
float fsqrt(float v);
float fabs_(float v);
float clampf(float v, float a, float b);
int clampi(int v, int a, int b);
float wrap_pi(float a);   /* to -pi..pi */
float fatan2(float y, float x);
typedef struct {
  float x, y, vx, vy;
} body_t;
/* two discs closer than r: pushed apart, their speeds changed as masses ma and mb would (e: the bounce) */
bool collide(body_t *a, body_t *b, float r, float ma, float mb, float e);
#ifdef HOST
int bot_tap(int p, bool want); /* a computer player's tap: pressed for one frame, then let go */
bool bot_fresh(int p);         /* a new round for this computer player (once) */
#endif

/* ---- drawing (on the strip being drawn) */
void disc(float x, float y, float r, color_t c);
void disc_a(float x, float y, float r, color_t c, int a256);
void box(int x, int y, int w, int h, int r, color_t c);
/* a rectangle turned to angle a around (x, y): from l0 to l1 along a, w to each side */
void quad(float x, float y, float a, float l0, float l1, float w, color_t c);
void poly(const float *xy, int n, color_t c); /* a convex polygon */
void line(float x0, float y0, float x1, float y1, float w, color_t c);
void say(const np_font_t *f, int cx, int base, const char *s, color_t c); /* centred, with a shadow */
/* a player: a round body looking towards look (radians), r its radius */
void blob(float x, float y, float r, int p, float look);
void eyes(float x, float y, float r, float look);
/* p's key label at (cx, base), centred, in c (the backspace key's sign is drawn) */
void keytag(int cx, int base, int p, color_t c);
int keytag_w(int p);
/* where p is drawn (the top of them): their key's label shows there during the countdown */
void tag_at(int p, float x, float y);
void tag_by(int p, float x, float y, int side); /* (by its side: -1 to its left, 1 to its right) */
char *num(char *o, uint32_t v);

/* ---- the minigames */
typedef struct {
  const char *name, *shortname, *rules;
  uint8_t last;                  /* the last side standing takes the round */
  void (*start)(void);
  void (*step)(void);
  void (*draw)(void);
  void (*icon)(int x, int y);    /* its picture in the menu, 56 x 34 */
#ifdef HOST
  int (*bot)(int p);             /* tests: whether a computer player holds its key now */
#endif
} game_t;

#ifdef HOST
#define BOT(f) , f
#else
#define BOT(f)
#endif

enum { G_SUMO, G_RACE, G_TANKS, G_SNAKES, G_SOCCER, G_FISH, G_SKATE, G_ROPE, G_PAINT, G_BALLOON, G_BIRDS, G_GRAVITY,
       G_FROG, G_CLOCK, NG };
extern const game_t g_sumo, g_race, g_tanks, g_snakes, g_soccer, g_fish, g_skate, g_rope, g_paint, g_balloon, g_birds,
    g_gravity, g_frog, g_clock;
extern const game_t *const GAMES[NG];
#endif
