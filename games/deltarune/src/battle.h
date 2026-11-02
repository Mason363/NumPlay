/* Deltarune: battles (the original's obj_battlecontroller and its enemies).
 *
 * The original's battle is 640 x 480, halved here as the Dark World is: positions stay the original's
 * units (1/16 of them for what moves), drawn at half. A battle is an encounter (scr_encountersetup's
 * number: who is fought and where); the party (up to three) each picks FIGHT, ACT (Kris) or MAGIC, ITEM,
 * SPARE or DEFEND; then the enemies talk and attack the SOUL in the box, grazing their bullets builds TP. */
#ifndef DR_BATTLE_H
#define DR_BATTLE_H
#include <stdbool.h>
#include <stdint.h>
#include "text.h"

enum { BR_WON, BR_SPARED, BR_FLED, BR_LOST, BR_SCRIPTED };

void battle_start(int encounter);
bool battle_over(void);
int battle_result(void);
void battle_update(uint32_t pressed, uint32_t held);
void battle_draw(void);
/* (for the tests) what the battle waits for */
enum { BUI_NONE, BUI_BUTTONS, BUI_TARGET, BUI_ACTS, BUI_ITEMS, BUI_SPELLS, BUI_PARTY, BUI_BARS, BUI_DODGE, BUI_TEXT };
int battle_ui(void);
extern int g_last_encounter;

/* ---------------------------------------------------------------- for the enemies (enemies.c) */
#define BFX 16                /* 1/16 unit */
#define MAX_EN 3
#define MAX_BUL 80
#define MAXTP 250

struct Enemy;
typedef struct EnemyDef {
  uint16_t obj;                        /* the original's object (obj_diamondenemy...) */
  uint16_t spr_idle, spr_hurt, spr_spared;
  uint8_t idle_div;                    /* frames per idle frame (siner / n) */
  /* its turn: what it says (the bubble), its attack (bullets, each frame), the flavour text after */
  void (*talk)(struct Enemy *e);
  void (*attack)(struct Enemy *e, int t);
  void (*flavor)(struct Enemy *e);
  /* an ACT (1 Check, 2...): its text into the battle box, mercy and such; returns true if handled */
  bool (*act)(struct Enemy *e, int act);
  void (*draw)(struct Enemy *e, int x, int y);   /* (its own drawing, NULL the usual) */
  int turntimer;                       /* frames its attack lasts */
} EnemyDef;

typedef struct Enemy {
  const EnemyDef *def;
  int type;                            /* monster_defs[] */
  bool alive;                          /* still in the battle */
  bool spared, killed;
  int16_t hp, maxhp, at, df;
  int16_t mercy;                       /* mercymod (0..100 and more) */
  bool tired;                          /* monsterstatus 1 */
  const char *comment;                 /* "(Tired)" by its name */
  int32_t x, y;                        /* the sprite's place, units */
  int shake, hurt_t, flash_t, fade;
  int siner;
  int turns;
  int acting;                          /* the ACT chosen this turn (1 Check...), 0 none */
  int16_t v[8];                        /* its own */
  uint16_t say[4];                     /* this turn's bubble (text ids), 0xFFFF none */
  int nsay;
  int dmg_show, dmg_t, dmg_kind;       /* the number over it */
} Enemy;

typedef struct {
  uint16_t spr;
  int32_t x, y, vx, vy, ax, ay;        /* 1/16 unit */
  int16_t r;                           /* hit radius (units), or a box */
  int16_t w, h;
  int16_t t, life;
  int16_t a, b, c, d;                  /* the attack's own */
  int16_t img, ispd;                   /* image_index, image_speed (1/16) */
  int16_t angle;                       /* drawn turned (1/256 turn) */
  int16_t scale;                       /* 256 = as drawn (the original's 2) */
  uint8_t alpha;
  uint8_t grazed;
  bool on;
  bool harmless;
  int16_t damage;
  int8_t target;                       /* party member hit (0-2), 3 all */
} Bullet;

extern Enemy en[MAX_EN];
extern Bullet bul[MAX_BUL];
extern int g_tension;                  /* TP, 0..250 */

Bullet *bullet_new(uint16_t spr, int x, int y);   /* units */
void bullets_clear(void);
void battle_say(const uint16_t *ids, int n);      /* the battle box's text (scr_battletext) */
void battle_sayf(uint16_t id, const char *a, const char *b);
void battle_face(int fc, int fe);                 /* a face for the next battle_say */
void battle_say_strs(const char *const *m, int n);  /* (messages as they are: a face change...) */
void battle_end_attack(void);                    /* the enemies' attack is over */
void enemy_say(Enemy *e, const uint16_t *ids, int n);   /* its bubble */
void mercy_add(Enemy *e, int n);
void tension_add(int n);
int heart_x(void), heart_y(void);                /* the SOUL's middle, units */
int box_cx(void), box_cy(void);                  /* the box's middle, units */
int box_half(void);                              /* half its side */
int party_target(void);                          /* scr_randomtarget: a member still up */
bool party_has(int c);
int enemies_alive(void);
const EnemyDef *enemy_def(int obj);
void enemy_defeat_run(Enemy *e);                 /* it runs off (killed by FIGHT: it flees) */
void enemy_spare(Enemy *e);
#endif
