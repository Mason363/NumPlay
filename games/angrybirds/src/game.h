#ifndef ANGRY_GAME_H
#define ANGRY_GAME_H
#include "data.h"
#include <stdbool.h>
#define BODY_CAP (MAX_ORIGINAL_OBJECTS+9)
#define JOINT_CAP MAX_ORIGINAL_JOINTS
#define CONTACT_CAP 768
#define LEVEL_BUFFER 14000
#define SAVE_BYTES 2336
#define PI 3.14159265358979323846f
#define SCORE_MASK UINT32_C(0x1ffffff)
#define BEST_SCORE(p,l) ((p)->best[l]&SCORE_MASK)
#define BEST_EAGLE(p,l) ((p)->best[l]>>25)
enum { ALIVE=1,DYNAMIC=2,WAITING=4,FIRED=8,USED=16,EGG=32,LINKED=64,SLEEPING=128,HIT=256 };
enum { TITLE,EPISODES,STAGES,PLAY,PAUSE,RESULT,HELP,QUIT,SAVE_ERROR,STORY,LOADING };
typedef struct {float x,y,vx,vy,a,w,c,s,im,ii,hp,minx,miny,maxx,maxy,damage;uint16_t type,flags,age,quiet,link;} Body;
typedef struct {float ax,ay,bx,by,length,angle,speed,torque,low,high;uint16_t a,b,flags,timer,next_a,next_b;uint8_t type;} Joint;
typedef struct {float nx,ny,x,y,depth,normal,tangent,closing,momentum;uint16_t a,b;} Contact;
typedef struct {uint32_t best[LEVEL_COUNT];uint8_t stars[LEVEL_COUNT],feathers[(LEVEL_COUNT+7)/8];uint16_t unlocked[GROUP_COUNT],last;uint8_t help;} Progress;
typedef struct {
    Progress p;Body bodies[BODY_CAP];Joint joints[JOINT_CAP];
    /* Loading finishes before simulation begins: both share the same RAM. */
    union {Contact contacts[CONTACT_CAP];uint8_t scratch[LEVEL_BUFFER];};
    uint16_t sorted[BODY_CAP];
    uint16_t count,joint_count,contact_count,level,group,birds,launched,active,pigs,settle,wait,tick,max_contacts,overflows;
    uint32_t score,rng;float slingx,slingy,angle,power,camera_x,camera_y,zoom,explosion_x,explosion_y;
    float eagle_x,eagle_y;uint16_t explosion_age,eagle_timer;uint8_t won,lost,dirty,overview,eagle_used,eagle_armed,feather,paused,eagle_percent,preparing;
} Game;
void game_new(Game*,uint32_t);
bool game_start(Game*,unsigned);
bool game_load(Game*,unsigned);
bool game_prepare(Game*);
void game_step(Game*);
bool game_launch(Game*);
bool game_power(Game*);
void game_eagle(Game*);
void game_explode(Game*,float,float,float,float);
void game_finish(Game*);
void body_refresh(Body*);
void physics_step(Game*,float);
bool game_validate(const Game*);
void save_encode(const Progress*,uint8_t*);
bool save_decode(Progress*,const uint8_t*,unsigned);
#endif
