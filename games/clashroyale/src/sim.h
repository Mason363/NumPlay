/* Clash Royale: the battle, as the game plays it (see gamesrc/clashroyale/spec/spec.md).
 *
 * The arena is the game's 18 x 32 tiles turned a quarter: x along it (0 to 32000, 1000 a tile, your King tower
 * at 3000), y across (0 to 18000; the top lane's bridge at 3500). Team 0 is you, on the left; team 1 the bot.
 * One step is 50 ms, 20 a second. Everything is integers, so a battle plays the same from the same start. */
#ifndef CR_SIM_H
#define CR_SIM_H
#include <stdbool.h>
#include <stdint.h>
#include "tables.h"

#define TICK 50
#define ARENA_X 32000
#define ARENA_Y 18000
#define RIVER_X0 15000
#define RIVER_X1 17000
#define MAXE 120
#define MAXP 72
#define MAXA 6
#define MAXFX 40
#define NONE 255

enum { E_ATTACKING = 1, E_CHARGING = 2, E_AWAKE = 4, E_WALKED = 8 };

typedef struct {
  uint8_t used, type, team, lvl;      /* UNITS[type]; lvl: the level index of its stats (m[lvl]) */
  uint8_t flags, face, anim, hidden;   /* face: 0 right, 1 down-right ... 7 up-right; anim: 0 run 1 attack 2 idle */
  int16_t target;                     /* an entity, -1 none */
  uint16_t tgen, gen;                 /* the target's generation, this one's */
  int32_t x, y;
  int32_t hp, maxhp, dmg;
  int32_t deploy;                     /* ms left deploying */
  int32_t t;                          /* attack charge, ms */
  int32_t post;                       /* ms since the last hit (for the picture) */
  int32_t decay;                      /* buildings: the lifetime's remainder */
  int32_t spawn_t, spawn_gap;         /* ms to the next group, to the next of the group */
  int32_t spawn_left;
  int32_t walked;                     /* Prince: units walked since his last hit */
  int32_t lock;                       /* Inferno Tower: ms on this target */
  int32_t rage, freeze, stun;         /* ms left of each */
  int32_t push_x, push_y, push_t;     /* pushed back: units a step, steps left */
  int32_t anim_ms;
  int32_t hide_t;                     /* Tesla: ms into coming up or going down */
  int32_t mana_t;                     /* Elixir Collector */
  int32_t flash;                      /* ms of the hit flash */
  int32_t wake;                       /* King tower: ms until it shoots */
} Ent;

typedef struct {
  uint8_t used, type, team, lvl;
  uint8_t spell, deco, pad1, pad2;
  int16_t target;
  uint16_t tgen;
  int32_t x, y, sx, sy, tx, ty;       /* where it is, where it started, where it goes */
  int32_t z0;                         /* the height it started at (units) */
  int32_t dmg;
  int32_t t, total;                   /* ms flown, of about total */
} Proj;

typedef struct {
  uint8_t used, type, team, lvl;
  int32_t x, y;
  int32_t life, total, hit_t, hits;
  int16_t hit_ids[4];
} Area;

enum { FX_EXPLOSION, FX_PUFF, FX_ZAP, FX_LIGHTNING, FX_SPLASH, FX_TEXT };
typedef struct {
  uint8_t used, kind, team, size;
  int16_t x, y;                       /* pixels */
  int16_t t, len;                     /* ms */
} Fx;

typedef struct {
  uint8_t deck[8];
  uint8_t hand[4];                    /* card ids, NONE while the next one comes */
  int16_t draw_t[4];
  uint8_t queue[8];
  uint8_t nqueue;
  uint8_t crowns;
  int16_t king, princess[2];          /* entities */
  int32_t elixir, elixir_t;           /* whole elixir, ms toward the next */
  int32_t played, spent, leaked;      /* cards played, elixir spent, elixir lost at 10 */
  uint8_t last_card, pad[3];
} Side;

typedef struct {
  int32_t think;                      /* ms to the next look */
  int32_t level;                      /* 0 to 6: the arena */
  int32_t push_lane;                  /* -1 none */
  int32_t push_ms;
  uint32_t rng;
} Bot;

typedef struct {
  uint32_t magic, size;
  uint32_t tick;
  int32_t time_ms;                    /* left in the match, or in overtime */
  uint8_t overtime, over, winner, arena;   /* winner: 0 you, 1 the bot, 2 draw */
  uint32_t rng;
  uint16_t gen;
  uint16_t pad;
  Side side[2];
  Bot bot[2];                         /* team 1's; team 0's only in tests */
  Ent e[MAXE];
  Proj p[MAXP];
  Area a[MAXA];
  Fx fx[MAXFX];
} Battle;

extern const int32_t LEVEL_MULT[16];
int card_level(int card);                          /* the tournament standard's level of a card (1...) */
int card_lvl_index(int card);                      /* its stats' index in LEVEL_MULT */
int32_t lv(int32_t v, int idx);                    /* a stat at a level index */
int32_t king_mult(void), tower_mult(void);         /* the towers' (percent) */

void battle_init(Battle *b, const uint8_t deck0[8], const uint8_t deck1[8], int arena, uint32_t seed);
void battle_step(Battle *b);
/* a card in the hand's slot at (x, y): NULL when it can be, else why not */
const char *battle_check(const Battle *b, int team, int slot, int32_t x, int32_t y);
bool battle_play(Battle *b, int team, int slot, int32_t x, int32_t y);
bool battle_can_place(const Battle *b, int team, int card, int32_t x, int32_t y);
int32_t battle_regen_ms(const Battle *b);          /* ms an elixir takes now */
bool battle_double(const Battle *b);
int battle_spawn(Battle *b, int type, int team, int lvl, int32_t x, int32_t y, int32_t deploy);   /* tests */
int32_t isqrt64(int64_t v);
int32_t dist(int32_t ax, int32_t ay, int32_t bx, int32_t by);
bool ent_alive(const Battle *b, int i);
bool ent_flying(const Ent *e);
uint32_t sim_rand(uint32_t *s);

/* the bot (bot.c): plays team 1 */
void bot_step(Battle *b);
#endif
