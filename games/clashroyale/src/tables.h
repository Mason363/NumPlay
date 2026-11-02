/* Clash Royale: the game's numbers, as tools/pack.py reads them from its assets/csv_logic (tables.c).
 * Stats are at level 1 of the card's rarity; times in ms, distances in the game's units (1000 a tile),
 * speeds in tiles a minute (60: one tile a second). */
#ifndef CR_TABLES_H
#define CR_TABLES_H
#include <stdint.h>
#include "ids.h"

/* an animation: pictures img + dir * nf + frame; act: the frame that hits (255 none); len: the game's frames */
typedef struct {
  int16_t img;
  uint8_t nf, ndir, act, len;
} AnimDef;
typedef struct {
  AnimDef run, attack, idle;
} SetDef;

enum {
  U_GROUND = 1 << 0,     /* AttacksGround */
  U_AIR = 1 << 1,        /* AttacksAir */
  U_BUILDINGS = 1 << 2,  /* TargetOnlyBuildings */
  U_FLY = 1 << 3,        /* FlyingHeight */
  U_NOPUSH = 1 << 4,     /* IgnorePushback (and buildings) */
  U_SELFAOE = 1 << 5,    /* SelfAsAoeCenter: splash around itself (Valkyrie) */
  U_JUMP = 1 << 6,       /* JumpEnabled: over the river (Hog Rider) */
  U_BUILDING = 1 << 7,
  U_HIDES = 1 << 8,      /* HidesWhenNotAttacking (Tesla) */
  U_TOWER = 1 << 9,      /* a crown tower */
  U_KING = 1 << 10,
  U_BOMB = 1 << 11       /* no hitpoints: explodes when deployed */
};

typedef struct {
  const char *name;
  int32_t hp, dmg, hit_ms, load_ms, range, min_range, sight, speed, deploy_ms, radius, mass, proj, area, life_ms;
  uint32_t flags;
  int32_t death_dmg, death_radius, death_push, death_unit, death_count;
  int32_t spawn_unit, spawn_number, spawn_level, spawn_interval, spawn_pause, spawn_start;
  int32_t charge_range, dmg_special, charge_mul;
  int32_t dmg2, t1, dmg3, t2;               /* Inferno Tower: VariableDamage2, its time, VariableDamage3 */
  int32_t mana_ms, mana_amount;             /* Elixir Collector */
  int32_t random_deploy, fly_height, hide_ms, up_ms;
  int32_t set;                               /* SETS[] */
  int32_t shadow_sx, shadow_sy, shadow_x, shadow_y;
  int32_t attached, attached_h, top_set;     /* the Princess on her tower, the Bomber on his; the tower's top */
} UnitDef;

enum { P_HOMING = 1, P_AOE_GROUND = 2, P_AOE_AIR = 4, P_REDUCED = 8, P_ARROW = 16, P_SPEAR = 32 };
typedef struct {
  int32_t speed, dmg, radius, push;
  uint32_t flags;
  int32_t spawn_unit, spawn_count, spawn_level, spawn_deploy, gravity, img;
} ProjDef;

enum { A_ENEMIES = 1, A_OWN = 2, A_BIGGEST = 4, A_REDUCED = 8, A_GROUND = 16, A_AIR = 32 };
typedef struct {
  int32_t radius, life_ms, life_per_level, hit_ms, dmg;
  uint32_t flags;
  int32_t buff_ms, buff_per_level, proj, speed_mul, hit_mul, spawn_mul;
} AreaDef;

enum { K_TROOP, K_BUILDING, K_SPELL };
enum { R_COMMON, R_RARE, R_EPIC, R_LEGENDARY };
typedef struct {
  const char *name, *info;
  int32_t rarity, cost, kind, count, unit, proj, area, radius, arrows, icon;
} CardDef;

extern const SetDef SETS[NSETS];
extern const UnitDef UNITS[NUNITS];
extern const ProjDef PROJS[NPROJS];
extern const AreaDef AREAS[NAREAS];
extern const CardDef CARDS[NCARDS];
extern const int32_t GLOBALS[NGLOBALS];
extern const uint8_t BOT_DECKS[NBOTDECKS][8];   /* the training camp's eight-card decks (predefined_decks.csv) */
#define GL(name) (GLOBALS[G_##name])
#endif
