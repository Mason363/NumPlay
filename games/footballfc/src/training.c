#include "football.h"
#include <stdlib.h>
#define Q FC_Q
const char*fc_drill_name(unsigned n){static const char*const names[]={"Finishing","Pass & move","Slalom"};return names[n%3];}
static const int8_t gates[6][2]={{-14,25},{14,38},{-12,51},{12,64},{0,79},{12,90}};
void fc_training_target(const FCMatch*m,int*x,int*y){
 if(m->drill==2){unsigned n=FC_MIN(m->drill_target,5);*x=gates[n][0]*Q;*y=gates[n][1]*Q;}
 else if(m->drill==1){*x=m->p[m->drill_target].x;*y=m->p[m->drill_target].y;}
 else{*x=0;*y=FC_H;}
}
void fc_training_reset(FCMatch*m){
 m->phase=FC_LIVE;m->timer=0;m->charge=m->pass_charge=0;m->charge_owner=-1;m->action_aim=0;m->input_latch=m->previous_input=0;
 m->offside=-1;m->spin=0;m->ball_kind=FC_DRIBBLE;m->pass_cool=0;m->control_lock=0;m->notice=0;m->keeper_wait[0]=m->keeper_wait[1]=m->keeper_dive[0]=m->keeper_dive[1]=0;
 for(int i=0;i<22;i++){m->p[i].x=0;m->p[i].y=0;m->p[i].vx=m->p[i].vy=0;m->p[i].cooldown=0;m->p[i].stamina=1000;m->motion[i].touch=m->motion[i].action=m->motion[i].think=0;}
 int x=0,y=12*Q;
 if(m->drill==0){x=((int)(fc_random(&m->seed)%25)-12)*Q;y=(79+fc_random(&m->seed)%10)*Q;m->p[11].y=102*Q;m->motion[11].face_y=-Q;}
 else if(m->drill==1){y=49*Q;m->p[7].x=-12*Q;m->p[7].y=62*Q;m->p[5].x=14*Q;m->p[5].y=65*Q;m->drill_target=7;}
 m->p[9].x=(int16_t)x;m->p[9].y=(int16_t)y;m->motion[9].face_x=0;m->motion[9].face_y=Q;
 m->ball=(FCBall){.x=x,.y=y,.owner=-1,.last=9,.receiver=-1};fc_possess(m,9);
}
void fc_training_new(FCMatch*m,const FCProfile*p,unsigned drill,uint32_t seed){
 fc_new(m,p,FC_TRAINING,p->opponent,seed);m->drill=(uint8_t)(drill%3);m->duration=60*40;fc_training_reset(m);
}
void fc_training_update(FCMatch*m){
 if(m->drill==0&&m->phase==FC_GOAL)m->drill_hits++;
 if(m->drill==2){int x,y;fc_training_target(m,&x,&y);if(m->ball.owner==9&&fc_distance(m->p[9].x-x,m->p[9].y-y)<3*Q){m->drill_hits++;m->drill_target=FC_MIN(5,m->drill_target+1);}}
 unsigned goal=m->drill==1?10:6;
 if(m->clock>=m->duration||m->drill_hits>=goal){m->phase=FC_FULL;return;}
 if(m->phase!=FC_LIVE){m->timer=28;return;}
 if(m->drill==0){
  /* A parried shot can settle outside the keeper's pickup zone. End that
   * attempt after a bounded flight instead of leaving an idle loose ball. */
  if(m->ball.owner==11||(m->ball.owner<0&&++m->timer>120)){m->phase=FC_GOALKICK;m->timer=28;}
  else if(m->ball.owner==9)m->timer=0;
 }
}
