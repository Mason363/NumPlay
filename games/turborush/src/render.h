#ifndef TURBO_RENDER_H
#define TURBO_RENDER_H
#include "race.h"
#include "road.h"
#include "../../../launcher/src/gfx.h"
#ifdef TR_UI_TEST
int tr_checked_text(const np_font_t*,int,int,const char*,color_t,int);
void tr_checked_center(const np_font_t*,int,int,const char*,color_t,int);
void tr_checked_right(const np_font_t*,int,int,const char*,color_t,int);
#define gfx_text tr_checked_text
#define gfx_text_center tr_checked_center
#define gfx_text_right tr_checked_right
#endif
typedef struct { int16_t center,width,left,right; uint16_t depth; int32_t world; } TrRow;
typedef struct { int16_t x,y,w; uint8_t kind,id; int32_t at; } TrObject;
typedef struct {float x,z,fx,fz,rx,rz;} TrCamera;
typedef struct {
 TrRow rows[240];TrObject objects[56];uint8_t count,horizon,mood,land,rain;
 uint32_t frame;int meters;const TrRace *race;const TrProfile *profile;
 color_t sky_palette[64],scene_palette[8][64];int16_t focal,camera,pan;
 TrRoad road;TrCamera eye;float progress,player_x,player_y,player_w;
 uint8_t palette_ready,palette_key;
} TrView;
extern const color_t tr_paints[6];
void tr_prepare(TrView*v,const TrRace*r,const TrProfile*p,uint32_t frame);
bool tr_project(const TrView*,float meters,float lateral,float*x,float*y,float*scale);
void tr_world(const TrView*v);
void tr_car(int x,int bottom,int width,color_t color,int steer,bool lights,bool boost);
void tr_hud(const TrView*v);
void tr_map(int x,int y,int w,int h,unsigned track,const TrRace*r);
#endif
