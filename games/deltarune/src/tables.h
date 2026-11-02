/* Deltarune: the original's tables (tables.c, made by tools/tables.py from its scripts). Texts are ids
 * (data.h), 0xFFFF none. */
#ifndef DR_TABLES_H
#define DR_TABLES_H
#include <stdint.h>

typedef struct {
  uint16_t name, desc;
  uint8_t target;            /* 0 none, 1 one of the party, 2 all of it */
  int16_t value;             /* what it heals (or sells for) */
  uint8_t usable;
} ItemDef;

typedef struct {
  uint16_t name, desc;
  int8_t at, df, mag;
  uint8_t kris, susie, ralsei;   /* who can use it */
  uint8_t icon;
  int16_t value;
} EquipDef;

typedef struct {
  uint16_t name, desc, longname;
  int16_t cost;              /* TP, out of 250 */
  uint8_t target;            /* 0 none, 1 the party, 2 an enemy */
  uint8_t usable;            /* usable out of battle */
} SpellDef;

typedef struct {
  uint16_t name, desc;
  uint8_t can;
  int16_t cost;              /* TP */
  uint8_t actor;             /* 1 Kris, 2 Susie, 3 Ralsei, 4 all */
} ActDef;

typedef struct {
  uint16_t name;
  int16_t hp, at, df, exp, gold;
  int16_t sparepoint, mercymax, mercymod;
  uint16_t battlemsg;
  ActDef acts[6];
} MonsterDef;

typedef struct {
  int8_t type;               /* MonsterDef */
  int16_t obj;               /* the original's object for it (its ways) */
  int16_t x, y;              /* where it stands, from the view's top-left (640 x 480) */
} EncMon;
typedef struct {
  EncMon m[3];
  uint16_t battlemsg;
} EncounterDef;

extern const ItemDef item_defs[];
extern const EquipDef weapon_defs[], armor_defs[];
extern const SpellDef spell_defs[];
extern const MonsterDef monster_defs[];
extern const EncounterDef encounter_defs[];
extern const int item_count, weapon_count, armor_count, spell_count, monster_count, encounter_count;
#endif
