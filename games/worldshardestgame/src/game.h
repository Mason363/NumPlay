#ifndef WHG_GAME_H
#define WHG_GAME_H
#include <stdint.h>
#include <stdbool.h>
#define WHG_LEVELS 30
#define WHG_MAX_ENEMIES 296
#define WHG_SAVE_BYTES 72
#define WHG_MIN(a,b) ((a)<(b)?(a):(b))
#define WHG_MAX(a,b) ((a)>(b)?(a):(b))
enum { WHG_LEFT=1,WHG_RIGHT=2,WHG_UP=4,WHG_DOWN=8 };
enum { WHG_INTRO,WHG_PLAY,WHG_DYING,WHG_FINISHED,WHG_TIP };
enum { WHG_CHANGED=1,WHG_DIED=2,WHG_CHECKPOINT=4,WHG_CLEARED=8,WHG_COMPLETED=16 };
typedef struct { int16_t x,y;uint16_t t; } WhgPoint;
typedef struct { uint16_t offset,count,period; } WhgTrack;
typedef struct { int16_t x,y;uint16_t track;uint8_t transform,kind; } WhgEnemy;
typedef struct { int16_t x0,y0,x1,y1;uint8_t f0,f1; } WhgWall;
typedef struct { int16_t x,y; } WhgCoin;
typedef struct { int16_t x0,y0,x1,y1,x,y; } WhgCheck;
typedef struct {
 uint16_t wall_offset,wall_count,enemy_offset,enemy_count,coin_offset,coin_count,check_offset,check_count,goal;
 const char *taunt;
} WhgLevel;
typedef struct {
 uint32_t frame,elapsed,deaths;
 int16_t x,y;
 uint8_t level,checkpoint,phase,timer,collected,coins[9];
} WhgRun;
typedef struct {
 WhgRun run;
 uint32_t best_deaths,best_ticks,last_deaths,finishes;
 uint8_t has_run,furthest;
} WhgProfile;
extern const WhgPoint whg_points[];
extern const WhgTrack whg_tracks[];
extern const WhgEnemy whg_enemies[];
extern const WhgWall whg_walls[];
extern const WhgCoin whg_coins[];
extern const WhgCheck whg_checks[];
extern const WhgLevel whg_levels[30];
extern const char *const whg_tips[30];
extern const uint16_t whg_palette[],whg_row_ids[],whg_title_palette[];
extern const uint32_t whg_row_offsets[];
extern const uint8_t whg_row_data[],whg_title_data[];
extern const unsigned whg_title_size;
void whg_defaults(WhgProfile *p);
void whg_new(WhgProfile *p);
void whg_load_level(WhgRun *r,unsigned level);
unsigned whg_step(WhgRun *r,unsigned input);
void whg_enemy(unsigned level,unsigned enemy,uint32_t frame,int16_t *x,int16_t *y);
bool whg_wall(unsigned level,int x,int y);
bool whg_coin_taken(const WhgRun *r,unsigned coin);
void whg_restart(WhgRun *r);
void whg_record_finish(WhgProfile *p);
void whg_encode(const WhgProfile *p,uint8_t out[WHG_SAVE_BYTES]);
bool whg_decode(WhgProfile *p,const uint8_t *in,unsigned bytes);
#endif
