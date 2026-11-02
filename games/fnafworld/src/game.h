/* FNAF World: the game's state and its parts.
 *
 * game.c: the scenes around the map (the title, the files, the party, the pause menu, chips and bytes, the shops,
 * game over) and the keys. world.c: the overworld (walking, the areas, encounters, chests, switches, bosses, jumps).
 * battle.c: a battle as the game's events run it (rules.c: its numbers). save.c: the files. The game's names for its
 * objects and counters are kept in the comments, with its event numbers (gamesrc spec/overworld_events.txt). */
#ifndef FNAFW_GAME_H
#define FNAFW_GAME_H
#include <stdbool.h>
#include <stdint.h>

#define NCHAR 48    /* the characters, 1 to 48 */
#define NCHIP 21
#define NBYTE 21
#define NSWITCH 9
#define NCHEST 21

/* a file (the game's Ini "fnafw1" to "fnafw3") */
typedef struct {
  uint8_t started, mode, diff;   /* mode 1 Adventure, 2 Fixed Party; diff 1 Normal, 2 Hard */
  uint8_t locked;                /* the party is fixed ('locked') */
  uint8_t have[NCHAR + 1];       /* 'Nhave' */
  uint8_t lv[NCHAR + 1];         /* 'Nlv' (shown plus one) */
  uint32_t next[NCHAR + 1];      /* 'Nnext': exp toward the next level */
  uint8_t slot[8];               /* 's1' to 's8': the party, two rows of four */
  uint32_t tokens;
  uint8_t chest[NCHEST + 1];     /* 'cN': chest N opened, its chip found */
  uint8_t chip[4];               /* 'active1' to 'active4': the chips worn */
  uint8_t byte_have[NBYTE + 1];  /* 'pN' */
  uint8_t bytes[4];              /* 'active1b' to 'active4b' */
  uint8_t armor;                 /* 'armor': 0, 1, 2 or 10 */
  uint8_t armor_have[4];         /* 'ar1' to 'ar3' */
  uint8_t sw[NSWITCH + 1];       /* 'sw1' to 'sw9' */
  uint8_t key, w3;               /* the key; the third jump ('w3') */
  uint8_t resetpos;              /* lost: start again at the start */
  int16_t x, y;                  /* 'x', 'y': where on the map (0: the start) */
  uint8_t area;
  uint8_t hours, minutes, seconds;
  uint8_t beat[8];               /* 'beatgame1' to 'beatgame7' */
} File;

/* the overworld */
typedef struct {
  int16_t x, y;            /* "8bit hitbox", the game's coordinates */
  uint8_t dir;             /* 0 right, 1 up, 2 left, 3 down */
  uint8_t walking;         /* "8bit hitbox" A */
  uint16_t img;            /* the walker's picture counter */
  uint16_t grace;          /* "8bit hitbox" B: frames without encounters */
  uint8_t area;            /* "area" */
  uint8_t boss_won[24];    /* the bosses beaten since the map was loaded (their objects' A) */
  uint8_t note;            /* a message shown: N_* */
  uint16_t note_t;
  uint8_t note_n;          /* (its chip, ...) */
  uint32_t enc_t, sec_t;   /* the encounter's every 2000 ms; the play clock */
  uint8_t warp_menu, warp_sel;
} World;

/* an enemy ("target") */
typedef struct {
  uint8_t alive;
  uint8_t an;              /* its animation (AN_*) */
  uint8_t img, cnt;        /* the picture, the speed counter */
  int16_t x, y;            /* the game's 800 x 480 battle */
  int16_t px, py;          /* (its path: node, progress) */
  uint8_t path, node;
  int16_t node_t;
  int32_t hurt;            /* B... "target" L (s11): the damage taken, below 0 */
  int32_t dmg;             /* "target" A (s0): damage to take this frame */
  int16_t speed;           /* "target" C (s2) */
  uint8_t s3;              /* poisoned look */
  int16_t s4;              /* attacking: frames */
  uint8_t s5;              /* hit: blinks */
  uint8_t s9;              /* its hit is coming */
  uint8_t s12;             /* munchies to come */
  int16_t s15;             /* poison ticks */
  int16_t s18;             /* haunted: seconds */
  uint8_t s19;             /* bitten */
  int16_t s20, s21;        /* unscrew, unscrew 2 */
  int16_t s25;             /* the roll for an instant end */
  uint8_t s7, s8, s17, s24;
  uint8_t s23;             /* status arrows to show */
} Enemy;

/* an effect on screen: the game's objects made by attacks (FX_*) */
typedef struct {
  uint8_t kind, an, img, cnt, plays, done;
  int16_t x, y, vx, vy;    /* (x, y in 1/16 pixels of the game's battle) */
  int16_t t, a, b;         /* its counters (the object's A, B...) */
  int8_t target;           /* an enemy, -1 none */
} Fx;

#define NFX 40

typedef struct {
  uint8_t on;              /* "battle" */
  uint8_t boss;            /* "boss": 0 or the boss's number */
  uint8_t chal;            /* "Active 26" B: 2 in a challenger's battle; chal_c its character */
  uint8_t chal_c, chal_roll, chal_banner, chal_won, chal_have;
  uint8_t etype;           /* "enemy type" */
  uint8_t count;           /* "make character" S: how many enemies came */
  uint8_t row;             /* "load row" 1 or 2 */
  uint8_t stage;           /* "victory stage" */
  int32_t life[8], life_max[8];   /* "life p 1" to 8 */
  uint8_t charge[8];
  uint8_t ready;           /* the slot (1 to 4) whose attacks show, 0 none */
  uint8_t sel;             /* the attack picked (0 to 2) */
  uint8_t ready_turn;      /* "ready for turn" */
  uint8_t moving;          /* "movement?" */
  /* the party on the field */
  struct { int16_t x, y; uint8_t an, img, cnt, state; int16_t t; uint8_t node; } pm[4];
  uint8_t att_slot;        /* "what character attacked" (its slot 1-4), 0 none */
  uint8_t used;            /* "used attack" */
  uint8_t att_img, att_cnt, att_plays, att_an;   /* the "attack" object */
  uint8_t att_frame_done;  /* (the picture whose effect was made) */
  int16_t cur_level;       /* "current level" */
  int16_t multiplier;
  int16_t atk_boost, def_boost, spd_boost;      /* seconds */
  int16_t shield, shield2, gift[4];
  int16_t pwr_down, spd_down, spd_down2, pwr_cloud, pwr_cloud_t;   /* "pwr down icon" A, "spd down icon" A... */
  int16_t stunned;
  int32_t heal[4], healed;
  int16_t regen, regen_n;   /* "value" R, S */
  uint8_t party_flash;
  int32_t enemy_max, boss_life, boss_speed, boss_strength, strength, special_strength;
  uint8_t special, special_chance;   /* "enemy attack" A (its kind) */
  int16_t special_t;                 /* "enemy attack" B */
  uint8_t special_go;
  int16_t poison_all, confuse, hose, screw_t, screw_who, cheese, wall_t, special18;
  uint8_t attacked_who;
  uint8_t hit_slot;                  /* (the member the last hit landed on, for its effect) */
  int32_t exp, tokens;               /* "exp earned", "tokens" */
  int16_t vt0, vt1, vt2;             /* "victory" A, B, C */
  uint8_t leveled[8];
  uint8_t team_ok[2];
  uint8_t running, run_luck;
  uint8_t chip_flags;                /* the chips' "chip settings" switches */
  int16_t quick_t;                   /* Quickstart: frames the enemies wait */
  uint8_t comet;                     /* Evercomet: chance in 10 */
  uint8_t counter_bite, pizza_fury, freddle_fury, block_confuse, block_unscrew;
  uint8_t bomb_used;
  int16_t shake, flash;              /* "follow 1" A, "flashhit" A */
  uint8_t mimic, mimic_attack;
  int16_t mimic_t;
  int16_t endo_t;
  int16_t hj_t, hj_kind, hj_level;   /* a happy jam playing */
  int16_t cup_t, cup_level;          /* Cupcake's "value" A */
  int16_t favors_t, favors_level;
  int16_t cheese_t, cheese_pow;      /* "value" D, E (hot cheese) */
  int16_t popper_n, popper_pow, poppers_t, poppers_on, poppers_wait;
  int16_t stars, stars2, star_pow;   /* "switch parties" S, T, U */
  int16_t pizzas, pizzas2, freddles, pizza_pow, pizza2_pow, freddle_pow;
  int16_t newnums0, newnums1, newnums2;
  uint8_t dark, dark_kind, mystery, mystery_kind, mystery_t;
  uint8_t lost, won;
  uint8_t slots[8];                  /* the party as it was ("slot 1" to 8 come back from the file: 1167) */
  uint32_t step;                     /* frames since it began */
  uint32_t timers[40];               /* the "every" timers: when each fires next (1/3 ms) */
  int16_t leave_t;                   /* the row leaving, then the other comes */
  Enemy en[4];
  Fx fx[NFX];
} Battle;

enum {
  SC_TITLE, SC_FILES, SC_PARTY, SC_WORLD, SC_BATTLE, SC_PAUSE, SC_CHIPS, SC_BYTES, SC_SHOP, SC_ARMOR,
  SC_GAMEOVER, SC_HOWTO, SC_WARP
};

typedef struct {
  bool running;
  uint8_t scene, back;     /* back: the scene a menu returns to */
  uint32_t t;              /* frames in the scene */
  uint32_t keys, pressed, held_t;
  uint8_t menu, sub, menu2;
  uint8_t slot;            /* the file (1 to 3) */
  uint8_t dirty;
  File f;
  World w;
  Battle b;
  uint8_t pick;            /* the party select's cursor (0..47), the slot being set (0..7) */
  uint8_t pick_slot, pick_mode;
  uint8_t shop_kind;       /* "what shop" */
  uint8_t confirm;
  uint32_t now;            /* the game's clock: thirds of a millisecond, 50 a frame */
} Game;

extern Game g;

uint32_t rnd(void);
int rnd_n(int n);          /* Clickteam's Random(n): 0 to n - 1 */

void game_init(void);
void game_step(uint32_t keys);
void game_draw(void);

/* saves (save.c) */
void save_file(void);
bool load_file(int slot, File *f);
void save_new(int slot);
void save_restore_copy(void);
void save_summary(int slot, uint8_t *mode, uint8_t *diff, uint8_t *hours, uint8_t *minutes, bool *started);

/* the overworld (world.c) */
void world_enter(void);
void world_step(void);
void world_draw(void);
void world_save_pos(void);
void world_band_dim(void);      /* (a band of the map, under menus) */
void world_prepare_view(void);
void world_after_battle(void);
const char *area_name(int area);

/* battles (battle.c) */
void battle_start(int boss);
void battle_challenger(int c);   /* "A new challenger has appeared!" (1198-1202) */
void battle_step(void);
void battle_draw(void);
void battle_keys(uint32_t pressed, uint32_t held);
void battle_draw_band_dim(void);
void battle_prepare_view(void);

/* the numbers (rules.c) */
extern const char *const CHAR_NAME[NCHAR + 1];
extern const uint8_t CHAR_ATTACKS[NCHAR + 1][3];
extern const char *const ATTACK_NAME[61];
extern const uint8_t ATTACK_COLOUR[61];
extern const char *const ENEMY_NAME[54];
extern const char *const BOSS_NAME[20];
extern const char *const CHIP_NAME[NCHIP + 1];
extern const char *const BYTE_NAME[NBYTE + 1];
extern const uint16_t BYTE_PRICE[NBYTE + 1];
int32_t life_max(int c, int lv);
int32_t exp_needed(int c, int lv);
int32_t enemy_life(int type, int diff);
int32_t challenger_life(int c, int diff);
int challenger_pick(int area);
int32_t victory_exp(int count, int type, int boss, int area, int diff);
int32_t victory_tokens(int count, int type, int boss, int area, int diff);
int exp_tick(int32_t exp);
int divisor(bool def_boost, bool shield, bool pwr_down);
int32_t enemy_strength(int type, int diff, int div, int block, int armor);
int boss_info(int boss, int diff, int32_t *life, int32_t *speed, int32_t *strength, int *special, int *an);
int enemy_special(int type);
int enemy_hit_effect(int type);
#endif
