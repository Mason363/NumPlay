/* Kingdom Rush: the heroes (the later Flash build's Hero Room and SoldierHero*.as), see hero.c. */
#ifndef KR_HERO_H
#define KR_HERO_H
#include <stdbool.h>
#include <stdint.h>
#include "kr.h"

/* the game's hero ids (GameSettings: 1 Gerald ... 7 Denas) */
enum { HERO_NONE, HERO_GERALD, HERO_MALIK, HERO_ALLERIA, HERO_BOLIN, HERO_MAGNUS, HERO_IGNUS, HERO_DENAS, HERO_COUNT };
#define HERO_ROOM_STARS 15   /* heroRoomStars */

extern uint8_t hero_pick;           /* the hero this battle brings (0: none) */
const char *hero_name(int id);
const char *hero_desc(int id);
const char *hero_special(int id);
void hero_stats(int id, uint8_t out[4]);   /* the Hero Room's bars: health, attack, range, speed (0 to 8) */

/* the battle */
void hero_battle_start(void);
void hero_step(void);               /* each frame: the spawn, the mines, tar, rains, quakes, courage */
int hero_soldier_new(Soldier *so);  /* (soldier_new for SK_HERO, SK_ILLUSION, SK_WILDCAT) */
bool hero_soldier_step(Soldier *so);   /* true: the frame is done */
void hero_soldier_kind(int kind, int16_t v[10]);   /* soldier.c's SoldierKind for these kinds */
void hero_hit(Soldier *so);         /* the blow lands */
void hero_blow_done(Soldier *so);   /* the blow's animation ends (Ignus' surge and frenzy) */
void hero_attack_anim(Soldier *so); /* a blow starts: which animation */
void hero_died(Soldier *so);
bool hero_block(Soldier *so);       /* an enemy's blow at the hero: Gerald's shield takes it */
void hero_bullet(Bullet *b);
void hero_draw_bullet(const Bullet *b, int x, int y);
void hero_draw_decals(void);        /* mines, tar */
void hero_draw_hud(bool selected);  /* the portrait, at the battlefield's top left */

/* the player's orders */
Soldier *hero_soldier(void);        /* the hero on the field (0: none yet) */
bool hero_rally_ok(Pt p);           /* within 30 of a road (§_-J1§.§_-EK§) */
void hero_set_rally(Pt p);
int hero_respawn_left(void);        /* frames until it comes back (0: alive) */
int hero_level(void);               /* 1 to 10 (tests) */
int hero_skill(int k);              /* the skills' levels, 0 to 3 */
#endif
