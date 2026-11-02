/* Kingdom Rush: the battle's state and rules, as the Flash game (2011) runs them.
 *
 * Everything is in the game's own units: its 700 x 600 battlefield, a step a frame at 30 frames a second, its
 * numbers (stats.c, from its GameSettings). Each part follows the game's class of the same name: level.c
 * (Level, Wave, WaveSpawn), enemy.c (Enemy, EnemyCommon and each enemy), tower.c (Tower and each tower),
 * soldier.c (Soldier and each soldier), bullet.c (Arrow, Bolt, Bomb, the rays...), power.c (the spells). */
#ifndef KR_KR_H
#define KR_KR_H
#include <stdbool.h>
#include <stdint.h>
#include "data.h"

#define FPS 30
#define RANGE_RATIO 0.7f
#define MAX_ENEMIES 160
#define MAX_SOLDIERS 80
#define MAX_BULLETS 96
#define MAX_FX 96
#define MAX_SPOTS 20
#define MAX_PATHS 6
#define MAX_LANES 3
#define MAX_ACTIVE_WAVES 8

/* ------------------------------------------------------------------ randomness: Math.random() */
float rnd(void);                     /* [0, 1) */
void rnd_seed(uint32_t s);
static inline int rnd_dmg(int lo, int hi) { int d = hi - lo; return lo + (d > 0 ? (int)__builtin_ceilf(rnd() * (float)d) : 0); }
int rnd_round(int lo, int hi);       /* Math.round(Math.random() * (hi - lo)) + lo */

/* ------------------------------------------------------------------ the numbers (stats.c) */
typedef struct {
  int16_t health, armor, magic_armor, dodge, gold, cost, min_dmg, max_dmg, reload;
  float speed;
  int8_t size, flying, boss, blockable, charge, x_soldier, x_adj, y_adj, bar_y, bar_w;   /* bar_w: 0 small 1 medium 2 big */
  int16_t hit_w, hit_h;    /* the hit box: x +-hit_w, y - hit_h to y + 4 */
  int16_t dead_time;
  uint8_t blood;           /* 0 red, 1 green, 2 grey */
} EnemyStat;
extern const EnemyStat enemy_stats[EN_COUNT];
const char *enemy_name(int type);

enum { SZ_SMALL, SZ_MEDIUM, SZ_BIG };
enum { ARM_P, ARM_M, ARM_E, ARM_I };   /* physical, magic, explosive, true damage */

/* ------------------------------------------------------------------ towers */
enum {
  TW_NONE, TW_ARCHER, TW_BARRACKS, TW_MAGE, TW_ENGINEER,
  TW_RANGER, TW_MUSKETEER, TW_PALADIN, TW_BARBARIAN, TW_ARCANE, TW_SORCERER, TW_BFG, TW_TESLA
};
typedef struct {
  int16_t cost, range, min_dmg, max_dmg, reload, area;
} TowerStat;
/* [kind][level 1..3] for the four basic towers; the specials at level 4 */
TowerStat tower_stat(int kind, int level);
typedef struct {
  int16_t cost, range, rally, health, armor, min_dmg, max_dmg, reload, respawn, regen, regen_reload;
} BarrackStat;
BarrackStat barrack_stat(int kind, int level);

/* ------------------------------------------------------------------ entities */
typedef struct {
  float x, y;
} Pt;

typedef struct Enemy {
  uint8_t alive, type, path, lane;
  uint8_t active, dead, blocked, fighting, charging, flying, boss, left;   /* left: facing left (scaleX -1) */
  uint8_t held;            /* §_-9R§: can't move (thorns, frozen...) */
  uint8_t fpos[2];         /* fighterPosition: soldiers around it */
  int8_t soldier;          /* who blocks it */
  int16_t node;            /* currentNode */
  float x, y, xs, ys, speed, margin;
  int32_t health, init_health;
  int16_t armor, magic_armor, dodge, gold, cost, min_dmg, max_dmg;
  int16_t reload, reload_t, charge, charge_t;
  int16_t dead_t, dead_time;
  uint16_t anim, anim_t;
  uint8_t dir;             /* 0 none, 1 right, 2 left, 3 up, 4 down (§_-LW§) */
  /* debuffs */
  int16_t poison_t, poison_lvl, poison_tick;
  int16_t curse_t, curse_tick, curse_amt, slow_t, net_t;
  float slow_speed;        /* the speed before a slow or a net */
  float base_speed;
  /* the specials: timers and counts, by enemy */
  int16_t sp_t, sp_t2, sp_n, sp_n2, sp_state, sp_c;
  int16_t regen_t, rage_t, respawn_t;
  int16_t teleport_t, teleports, thorn_t, thorn_tick, thorns, thorn_dmg, thorn_len, thorn_every;
  uint8_t teleporting, thorned;
  uint8_t no_teleport, no_thorn, no_poly;
  uint8_t froze;           /* bosses: a phase */
} Enemy;

typedef struct Soldier {
  uint8_t alive, kind, active, dead, respawning, walking, fighting, blocking, idle, charging, door_queued, left;
  uint8_t hidden;          /* in its barracks, not drawn */
  uint8_t look;            /* the reinforcements' three faces (A, B, C) */
  int8_t spot;             /* its barracks' spot, -1: reinforcements or a sorcerer's elemental */
  int8_t enemy;
  uint8_t fpos;
  uint8_t level;           /* reinforcements: their level; elemental: its upgrade */
  float x, y, rx, ry, dx, dy;   /* where it is, its rally point, where it goes */
  float speed;
  int16_t health, init_health, armor, min_dmg, max_dmg, range_w, range_h;
  int16_t reload, reload_t, charge, charge_dmg, charge_t;
  int16_t idle_time, idle_t, targeted_t, regen, regen_reload, regen_t;
  int16_t dead_time, dead_t, door_wait, door_t;
  int16_t life_time, life_t, lifes;
  uint16_t anim, anim_t;
  int16_t sp_t, sp_t2;     /* specials' cooldowns */
  uint8_t sp_state;        /* the special under way: 1 healing, 2 holy strike, 3 a net, 4 an axe */
  int16_t sp_c;
  int8_t sp_target;
  Pt sp_at;
  float home_x, home_y;    /* reinforcements: where they were called (§_-Fm§) */
} Soldier;

typedef struct Spot {
  int16_t x, y;            /* the tower's place (the build spot is drawn 36 higher) */
  Pt rally;                /* the soldiers' rally point */
  uint8_t kind, level;     /* TW_*, 1 to 3 (4: a special) */
  uint8_t building;        /* building: the frames left */
  uint8_t charging, ready;
  int16_t reload_t, reload, charge_t, charge_len, shoot_at;
  int16_t spent;           /* §_-1Y§: what it cost, for selling */
  int16_t shots;
  int8_t target;
  uint8_t shooter_flip[2], shooter_up[2], arrows;
  uint16_t anim_t, shooter_t[2];
  int16_t idle_t;
  Pt dest;                 /* a bomb's or an arrow's aim */
  /* barracks */
  int8_t soldiers[3];
  uint8_t door;            /* 0 closed, 1 opening, 2 open, 3 closing */
  int16_t door_t;
  /* specials: each ability's level and cooldown, the one under way (sp_on: 1 + its index) and its count */
  uint8_t sp_lvl[3];
  int16_t sp_t[3];
  uint8_t sp_flag, sp_on;
  int16_t sp_c, poison_t;
  int8_t sp_target;
  Pt sp_at;
  int8_t elemental;
  uint8_t premade;         /* built by the level (iron mode), not bought */
  uint8_t disabled;        /* frozen, or held by Vez'nan */
  uint8_t ice, ice_hits;   /* J.T.'s ice: 1 forming, 2 holding (3 hits break it), 3 melting; 4 to 6: Vez'nan's hold */
  int16_t ice_t;
} Spot;

enum {
  BU_ARROW, BU_BOLT, BU_BOMB, BU_FIREBALL, BU_ARROW_ENEMY, BU_AXE, BU_AXE_ENEMY, BU_MISSILE, BU_MISSILE_ENEMY,
  BU_RAY_ARCANE, BU_RAY_DEATH, BU_RAY_POLY, BU_RAY_TESLA, BU_BOLT_SORCERER, BU_BOLT_NECRO, BU_SPEAR, BU_SHOT,
  BU_FIELD, BU_NET
};
typedef struct Bullet {
  uint8_t alive, kind, level, active, crit, first;
  int8_t target;           /* an enemy (or a soldier for the enemies' bullets) */
  int16_t t0, t1, fade_t, fade;
  float x, y, x0, y0, tx, ty, xs, ys, acc, acc_max, g, rot;
  int16_t min_dmg, max_dmg, area, pierce, bombs;
  uint16_t anim, anim_t;
  int16_t dmg;             /* a ray's whole damage, spread over its frames */
  uint8_t jump, jump_max, shielded;
  int8_t hitlist[6];       /* the tesla's arcs: whom they already hit */
} Bullet;

typedef struct Fx {
  uint8_t alive, flip, decal;
  uint16_t anim, t, life;
  float x, y;
  int16_t value;           /* gold from selling, from calling a wave */
} Fx;

/* ------------------------------------------------------------------ the battle */
enum { LS_STARTING, LS_NORMAL, LS_PAUSED, LS_PRE_WIN, LS_WIN, LS_OVER };
enum { MODE_CAMPAIGN, MODE_HEROIC, MODE_IRON };

typedef struct {
  int16_t wave;            /* index in the level's waves */
  int16_t spawn;           /* indexSpawn */
  uint8_t between;         /* §_-4l§: waiting after a spawn finished */
  int16_t between_t;
  int16_t ival_t, index_cant, index_swap;
  uint8_t cur_creep;
  uint8_t note_sent;       /* notificationSent: the wave's second notification went out */
} ActiveWave;

typedef struct {
  uint8_t index, mode, state;
  int16_t lives, init_lives, cash, max_upgrade;
  uint8_t unlock[4], unlock_lvl[4];   /* archers, barracks, engineers, mages (as the level gives them) */
  uint8_t power_fire, power_reinf;    /* unlocked at the first call */
  uint8_t npaths, nlanes[MAX_PATHS];
  const uint8_t *lane[MAX_PATHS][MAX_LANES];
  uint16_t lane_n[MAX_PATHS][MAX_LANES];
  const uint8_t *waves;               /* this mode's waves */
  uint8_t nwaves, max_waves, current_wave, index_waves;
  int16_t interval_t;
  uint8_t waiting_first;              /* §switch§: the first wave waits for its call */
  uint8_t flags_up;                   /* the next wave's flags show */
  uint8_t nactive;
  ActiveWave active[MAX_ACTIVE_WAVES];
  uint8_t nflags;
  Pt flag[MAX_PATHS], flag_dir[MAX_PATHS];
  uint8_t flag_path[MAX_PATHS];       /* paths whose flags show */
  int16_t start_t, prewin_t, ready_win_t;
  uint8_t ready_win;
  uint8_t fearless;
  Pt defend[2];
  uint8_t ndefend;
  /* spells */
  int16_t fire_cd, fire_reload, reinf_cd, reinf_reload;
  uint8_t fire_on, reinf_on;          /* unlocked */
  int16_t fire_open_t, reinf_open_t;
  uint8_t pending_note;               /* a notification to show (NOTE_*) */
  uint8_t pending_more;               /* and one more after it */
  /* the field */
  uint8_t nspots;
  Spot spot[MAX_SPOTS];
  Enemy en[MAX_ENEMIES];
  Soldier so[MAX_SOLDIERS];
  Bullet bu[MAX_BULLETS];
  Fx fx[MAX_FX];
  /* fireballs falling: the rain of fire's controllers */
  struct { uint8_t on; int16_t t, dur; Pt at; } rain[2];
  /* spider eggs (SpiderEgg): where, on which road, when they hatch */
  struct { uint8_t on, path, n; int16_t node, t; float x, y; } egg[12];
  uint8_t grave, grave_lane, grave_k;  /* Graveyard: 1 stage 10's, 2 stage 12's; the next lane and grave */
  /* the rain of fire's burning ground (ScorchedEarth) */
  struct { uint8_t on; int16_t t, dur; Pt at; } burn[8];
  int8_t veznan;                      /* Vez'nan, in the last stage's campaign */
  struct { uint8_t on, k, n, group, max; int16_t t; } vportal[6];
  /* the arcane wizards' portals (TeleportPortal) */
  struct { uint8_t on; int16_t t, left, nodes; Pt at; } portal[4];
  uint32_t frame;
  int32_t gold_won;
  uint16_t kills;
} Battle;
extern Battle B;

/* the upgrades bought with stars (as the game's GameUpgrades: each line's level, 0 to 5) */
enum { UP_ARCHERS, UP_BARRACKS, UP_MAGES, UP_ENGINEERS, UP_RAIN, UP_REINF };
extern uint8_t upgrades[6];
/* is upgrade n (1 to 5) of a line on in this battle (heroic and iron cap them)? */
bool up_on(int line, int n);

/* level.c */
extern uint32_t battle_seed;         /* mixed into each battle's randomness */
void battle_start(int level, int mode);
void battle_step(void);              /* a frame (1/30 s) */
bool battle_call_wave(void);         /* the next wave now (true if there was one to call) */
int battle_stars(void);
Pt path_node(int path, int lane, int node);
int path_len(int path, int lane);
int battle_wave_seconds_left(void);  /* until the next wave comes by itself, -1 if none waits */
bool tower_locked(int kind, int level);   /* isTowerLocked */
void battle_cash(int v);
void battle_lose_lives(int n);

/* enemy.c */
int enemy_spawn(int type, int path, int lane, int node);
void enemy_step(Enemy *e);
void enemy_damage(Enemy *e, int dmg, int armor_type, int armor_pierce);
void enemy_kill(Enemy *e, int how);  /* how: 0 normal, 1 explode, 2 desintegrate */
void enemy_block(Enemy *e, int soldier);
void enemy_unblock(Enemy *e);
void enemy_start_fighting(Enemy *e);
int enemy_fighter_position(Enemy *e);
void enemy_free_position(Enemy *e, int pos);
bool enemy_dodge(Enemy *e);
Pt enemy_road(const Enemy *e, int ahead);   /* road[currentNode + ahead], kept on the road */
int enemy_nodes_speed(const Enemy *e);
static inline Enemy *EN(int i) { return i >= 0 && i < MAX_ENEMIES && B.en[i].alive ? &B.en[i] : 0; }
float ellipse_contains(float cx, float cy, float w, float h, float px, float py);

/* tower.c */
void spot_build(int s, int kind);
void spot_make(int s, int kind, int level);   /* a tower the level built (iron mode), free */
void spot_upgrade(int s, int choice);   /* 0 next level, 1 / 2 the specials */
void spot_sell(int s);
int spot_sell_value(int s);
int spot_cost(int s, int choice);        /* -1 if it can't */
void tower_step(int s);
void spot_rally(int s, Pt p);
bool spot_rally_ok(int s, Pt p);
int tower_range(int kind, int level);
int spot_ability_cost(int s, int k);     /* -1 at its top */
void spot_buy_ability(int s, int k);

/* soldier.c */
int soldier_new(int kind, int spot, Pt at, Pt rally);
void soldier_step(Soldier *so);
void soldier_damage(Soldier *so, int dmg, bool true_dmg);
void soldier_set_rally(Soldier *so, Pt p);
void soldier_free(Soldier *so);
static inline Soldier *SO(int i) { return i >= 0 && i < MAX_SOLDIERS && B.so[i].alive ? &B.so[i] : 0; }
enum {
  SK_L1, SK_L2, SK_L3, SK_PALADIN, SK_BARBARIAN, SK_FARMER, SK_REINF2, SK_REINF3, SK_REINF4, SK_ELEMENTAL,
  SK_IMPERIAL
};

/* bullet.c */
Bullet *bullet_new(int kind);
void bullet_step(Bullet *b);
void fx_add(int anim, float x, float y, bool flip, bool decal);
void fx_value(float x, float y, int value);   /* "+15" floating up */
void fx_step(void);

/* power.c */
void power_init(void);
void power_step(void);
bool power_fire(Pt at);
bool power_reinforce(Pt at);
bool power_reinforce_ok(Pt at);
int power_fire_cooldown(void);
int power_reinf_cooldown(void);
void power_unlock_fire(void);
void power_unlock_reinf(void);
void power_shorten(int frames);
#endif
