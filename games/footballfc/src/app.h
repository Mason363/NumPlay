#ifndef FOOTBALL_APP_H
#define FOOTBALL_APP_H
#include "football.h"
#include "../../../launcher/src/gfx.h"
enum { UI_HOME,UI_SETUP,UI_MATCH,UI_PAUSE,UI_RESULT,UI_CAREER,UI_SQUAD,UI_TABLE,UI_MARKET,UI_CUP,UI_SETTINGS,UI_HELP,UI_CONFIRM,UI_SAVE_ERROR,UI_TRAINING };
typedef struct {
 FCProfile profile;FCMatch match;
 int screen,selected,page,mode,return_to,confirm,contract_games;
 int32_t cam_x,cam_y;uint32_t ui_time;
 bool dirty,rewarded,quit;char message[80];
 bool use_visual;int preview_target;FCMatch previous_match,visual_match;
} FCApp;
static inline const FCMatch*fc_view(const FCApp*a){return a->use_visual?&a->visual_match:&a->match;}
void fc_draw(void*ctx);
void fc_camera(FCApp*a,unsigned elapsed);
int fc_project(const FCApp*a,int x,int y,int height,int*sx,int*sy);
/* The simulation keeps its pitch axes. The sideline view rotates only input
 * and rendering; front-on shootouts retain their original controls. */
static inline unsigned fc_screen_input(unsigned in,bool shootout){
 if(shootout)return in;
 return (in&~15u)|((in&FC_RIGHT)?FC_UP:0)|((in&FC_LEFT)?FC_DOWN:0)|
  ((in&FC_UP)?FC_RIGHT:0)|((in&FC_DOWN)?FC_LEFT:0);
}
static inline unsigned fc_match_input(unsigned in,const FCMatch*m){
 if(m->shoot_state)return in;
 if(fc_free_view(m)){
  if(m->phase==FC_FREE||fc_attack(m,m->free_team)>0)return in;
  return (in&~15u)|((in&FC_LEFT)?FC_RIGHT:0)|((in&FC_RIGHT)?FC_LEFT:0)|((in&FC_UP)?FC_DOWN:0)|((in&FC_DOWN)?FC_UP:0);
 }
 return fc_screen_input(in,false);
}
const char *fc_player_name(unsigned team,unsigned player);
#endif
