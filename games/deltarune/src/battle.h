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
/* what a member does this turn (battle_action) */
enum { A_NONE, A_FIGHT, A_ACT, A_SPELL, A_ITEM, A_SPARE, A_DEFEND };

void battle_start(int encounter);
bool battle_over(void);
int battle_result(void);
void battle_update(uint32_t pressed, uint32_t held);
void battle_draw(void);
/* (for the tests) what the battle waits for */
enum { BUI_NONE, BUI_BUTTONS, BUI_TARGET, BUI_ACTS, BUI_ITEMS, BUI_SPELLS, BUI_PARTY, BUI_BARS, BUI_DODGE, BUI_TEXT };
int battle_ui(void);
Writer *scene_writer(void);   /* a writer for a cutscene's own text (the bubble's, free outside battles) */
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
  void (*draw_front)(struct Enemy *e); /* (drawn over the box and the bullets: a bike...) */
  int16_t blx, bly;                    /* its bubble (scr_enemyblcon) from it, units: 0, 0 the usual -160, 0 */
  uint16_t blspr;                      /* the bubble's sprite, 0 spr_battleblcon_long */
  /* a battle of its own (the Dummy's lessons): asked BS_* things, NULL none */
  int (*script)(struct Enemy *e, int ev, int arg);
  uint8_t flags;                       /* EF_* */
} EnemyDef;
enum { EF_AMBUSH = 1,                  /* its turn first (scr_ambush) */
       EF_STAYS = 2 };                 /* SPARE, PACIFY and FIGHT don't take it out: its script hears it (BS_*) */
enum { BS_CANMOVE,                     /* may member arg (CH_*) choose this turn? (global.charmove) */
       BS_STEP,                        /* every frame (arg 0), or asked (arg 1); nonzero: busy, the actions wait */
       BS_DRAW,                        /* over the party (a hug...) */
       BS_SPARED, BS_PACIFIED };       /* (EF_STAYS) what would have taken it out */

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
  uint16_t pic;                        /* or a picture in the bubble (obj_heartblcon), 0 none */
  bool rest;                           /* no attack this turn (said in its talk) */
  int dmg_show, dmg_t, dmg_kind;       /* the number over it */
  bool hidden;                         /* not drawn (its attack is itself: K. Round's leap...) */
  bool automiss;                       /* warned: the party's FIGHT misses it (global.automiss) */
  uint8_t outcome;                     /* global.flag[51 + i]: 1 beaten, 2 spared, 3 pacified, 4 a boss's */
  float f[6];                          /* its own (in floats) */
} Enemy;

typedef struct Bullet_ {
  uint16_t spr;
  int32_t x, y, vx, vy, ax, ay;        /* 1/16 unit */
  int16_t r;                           /* hit radius (units), or a box */
  int16_t w, h;                        /* (w: a box, half w by half h, turned by angle, not r) */
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
  uint8_t gp, timep;                   /* grazepoints, timepoints (5, 5) */
  bool keep;                           /* not gone when it hits (the bike...): off the screen too */
  void (*upd)(struct Bullet_ *b);      /* its own step, before it moves (NULL none) */
  float fx, fy, fvx, fvy, fric, grav, gdir;   /* (an upd's own: GameMaker's speed, friction, gravity) */
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
void enemy_leave(Enemy *e);                      /* gone, no spoils (scr_monsterdefeat by its own script) */
int battle_action(int c, int *sub);              /* what member c (CH_*) does this turn: A_*, its ACT/spell/item */
void battle_box_talk(int fc, int fe, const char *const *m, int n);   /* in its talk: the box speaks (typer 45),
                                                 the bubbles and the attack wait for it */
void battle_lesson(int fc, int fe, const char *const *m, int n, uint16_t after);   /* at the turn's end: a text to
                                                 read, then the turn begins with `after` in the box (0xFFFF none) */
bool battle_text_on(void);                       /* the box's text is showing */
void battle_hide_hero(int c, bool hide);
void battle_hero_pos(int c, int *x, int *y);     /* its place, units */
int battle_hp_lost(int c);                       /* HP member c lost in this battle */
void bullet_gm(Bullet *b);                       /* an upd: GameMaker's motion (fvx, fvy, friction, gravity) */
void bullets_fade_out(void);                     /* with (obj_regularbullet) active = 0, fading */
void hero_auto_attack(int c, int points);        /* member c (CH_*) hits the first enemy (a cutscene's FIGHT) */
void battle_set_battlemsg(uint16_t id);          /* global.battlemsg[0]: the flavour text */
void battle_text_hold(bool on);
void battle_end_text(void);                      /* (and gone) */                  /* the box's text stays, OK doesn't go on (a mini-game's) */
/* a cutscene's guest (the Hathy fight's Lancer): at a moment (BC_*) it slides in from the right, its words go in
 * the box (ids: texts and F_ face changes, -1 after), then it slides out and the battle goes on */
enum { BC_CLEAN_TURN,                            /* the first enemies' turn, if Kris and the next lost no HP */
       BC_VICTORY };                             /* won: after the victory text */
void battle_cameo(int when, uint16_t spr, int y, int fc, int fe, const int *ids);
void battle_victory_text(uint16_t id, int fc, int fe);   /* the victory's text instead (~1 EXP, ~2 D$) */
void battle_say_ids(int fc, int fe, const int *ids);     /* the box's text: texts and F_ face changes */
#endif
