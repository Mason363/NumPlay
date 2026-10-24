#include "football.h"
#include <stdlib.h>
#define Q FC_Q
static void ready(FCMatch*m){
 m->shoot_state=1;m->shoot_timer=0;m->shoot_keeper=0;m->shoot_target=0;m->charge=0;
 m->shoot_height=Q;m->shoot_start=m->shoot_goal=0;m->shoot_guard_z=Q;m->shoot_flight=26;m->shoot_resolved=0;m->shoot_result=FC_PEN_WAIT;
 m->kick_clock=0;m->kick_quality=70;
 m->ball=(FCBall){.y=11*Q,.z=Q/6,.owner=-1,.last=9,.receiver=-1};
}
void fc_start_penalties(FCMatch*m){
 m->single_penalty=0;
 m->shoot_turn=0;m->shoot_round=0;m->shoot_done=0;m->shoot_score[0]=m->shoot_score[1]=0;m->shoot_taken[0]=m->shoot_taken[1]=0;
 m->shoot_marks[0]=m->shoot_marks[1]=0;m->previous_input=0;m->input_latch=0;ready(m);
}
void fc_start_match_penalty(FCMatch*m,int team){
 m->single_penalty=1;m->shoot_turn=(uint8_t)team;m->shoot_done=0;m->shoot_round=0;
 m->shoot_score[0]=m->shoot_score[1]=0;m->shoot_taken[0]=m->shoot_taken[1]=0;m->shoot_marks[0]=m->shoot_marks[1]=0;
 m->input_latch=m->previous_input=0;
 int dir=fc_attack(m,team),keeper=fc_keeper(m,1-team);m->p[keeper].x=0;m->p[keeper].y=(int16_t)(dir>0?FC_H-Q:Q);
 for(int i=0;i<22;i++)if(fc_active(m,i)&&i!=keeper&&i!=m->taker){m->p[i].vy=m->p[i].vx=0;if(dir>0)m->p[i].y=FC_MIN(m->p[i].y,FC_H-21*Q);else m->p[i].y=FC_MAX(m->p[i].y,21*Q);}
 ready(m);
}
static void strike(FCMatch*m,int direction){
 m->power=m->charge?FC_MAX(280,m->charge):600;
 if(!m->shoot_turn){
  m->kick_quality=(uint8_t)fc_timing(m->kick_clock);
  int spread=20+(100-m->kick_quality)*4+(m->power>900?120:0);m->shoot_target+=(int)(fc_random(&m->seed)%(2*spread+1))-spread;
  if(m->kick_quality<40){
   if(fc_random(&m->seed)&1)m->shoot_target=(int16_t)((m->shoot_target<0?-1:1)*(FC_GOAL_HALF+180+fc_random(&m->seed)%260));
   else m->shoot_height=(int16_t)(FC_GOAL_HEIGHT+180+fc_random(&m->seed)%240);
  }
  if(m->power>900)m->shoot_height+=(m->power-900)*3;
  /* The keeper commits to a side with a limited, difficulty-dependent read.
   * It does not teleport to, or keep tracking, the final shot target. */
  int side=(int)(fc_random(&m->seed)%3)-1;
  if(fc_random(&m->seed)%100<(25u+m->difficulty*12u))side=m->shoot_target>100?1:m->shoot_target<-100?-1:0;
  m->shoot_goal=(int16_t)(side*(600+(int)(fc_random(&m->seed)%230)));
  m->shoot_guard_z=(int16_t)(150+fc_random(&m->seed)%310);
 }else{
  m->shoot_target=(int16_t)((int)(fc_random(&m->seed)%(2*(FC_GOAL_HALF+70)))-(FC_GOAL_HALF+70));
  m->shoot_height=(int16_t)(80+fc_random(&m->seed)%(FC_GOAL_HEIGHT-80));m->power=600;
  int side=direction?direction:(m->shoot_keeper>100?1:m->shoot_keeper<-100?-1:0);
  m->shoot_goal=(int16_t)FC_CLAMP(m->shoot_keeper+side*650,-FC_GOAL_HALF+70,FC_GOAL_HALF-70);
 }
 m->shoot_start=m->shoot_keeper;m->shoot_flight=(uint8_t)(34-m->power/65);m->shoot_state=2;m->shoot_timer=0;
 if(m->single_penalty)m->shots[m->shoot_turn]=FC_MIN(99,m->shots[m->shoot_turn]+1);
}
static void resolve(FCMatch*m){
 bool post=(abs(abs(m->shoot_target)-FC_GOAL_HALF)<34&&m->shoot_height<FC_GOAL_HEIGHT+30)||(abs(m->shoot_target)<FC_GOAL_HALF&&abs(m->shoot_height-FC_GOAL_HEIGHT)<30);
 bool inside=abs(m->shoot_target)<FC_GOAL_HALF-20&&m->shoot_height<FC_GOAL_HEIGHT-20;
 int reach=m->shoot_turn?180:(120+m->difficulty*15);
 bool save=abs(m->shoot_target-m->shoot_keeper)<reach&&abs(m->shoot_height-m->shoot_guard_z)<230;
 m->shoot_result=post?FC_PEN_POST:!inside?FC_PEN_MISS:save?FC_PEN_SAVE:FC_PEN_GOAL;
 m->shoot_resolved=1;if(m->shoot_result==FC_PEN_GOAL&&m->shoot_taken[m->shoot_turn]<16)m->shoot_marks[m->shoot_turn]|=(uint16_t)(1u<<m->shoot_taken[m->shoot_turn]);m->shoot_taken[m->shoot_turn]++;
 if(m->shoot_result==FC_PEN_GOAL){m->shoot_score[m->shoot_turn]++;m->ball.vx=0;m->ball.vy=-4*Q;m->ball.vz=Q;}
 else if(m->shoot_result==FC_PEN_SAVE||post){m->ball.vx=(m->shoot_target<0?-1:1)*6*Q;m->ball.vy=5*Q;m->ball.vz=3*Q;}
}
void fc_penalty_step(FCMatch*m,unsigned input){
 if(m->shoot_done)return;unsigned hit=input&~m->input_latch;m->input_latch=(uint16_t)input;m->previous_input=(uint8_t)input;m->ticks++;if(m->card_timer)m->card_timer--;
 int dx=!!(input&FC_RIGHT)-!!(input&FC_LEFT),dy=!!(input&FC_UP)-!!(input&FC_DOWN);
 if(m->shoot_state==1){
  m->kick_clock++;
  if(!m->shoot_turn){
   m->shoot_target=(int16_t)FC_CLAMP(m->shoot_target+dx*18,-FC_GOAL_HALF-160,FC_GOAL_HALF+160);
   m->shoot_height=(int16_t)FC_CLAMP(m->shoot_height+dy*12,70,FC_GOAL_HEIGHT+160);
   if(input&FC_SHOOT)m->charge=FC_MIN(1000,m->charge+18);
   if((hit&FC_PASS)||(!(input&FC_SHOOT)&&m->charge)||m->kick_clock>=320)strike(m,0);
  }else{
   m->shoot_keeper=(int16_t)FC_CLAMP(m->shoot_keeper+dx*20,-FC_GOAL_HALF+150,FC_GOAL_HALF-150);
   m->shoot_guard_z=(int16_t)FC_CLAMP(m->shoot_guard_z+dy*12,80,FC_GOAL_HEIGHT-80);
   if(++m->shoot_timer>120||(hit&FC_PASS))strike(m,dx);
  }
  return;
 }
 if(m->shoot_state!=2)return;m->shoot_timer++;
 unsigned flight=m->shoot_flight?m->shoot_flight:26,contact=FC_PEN_RUNUP+flight;
 if(!m->shoot_turn&&m->shoot_timer<=FC_PEN_RUNUP){/* The ball remains at the spot during the run-up. */}
 else if(m->shoot_turn&&m->shoot_timer<=FC_PEN_RUNUP&&dx)m->shoot_goal=(int16_t)FC_CLAMP(m->shoot_start+dx*700,-FC_GOAL_HALF+70,FC_GOAL_HALF-70);
 if(m->shoot_timer>FC_PEN_RUNUP){
  unsigned f=m->shoot_timer-FC_PEN_RUNUP;
  int reaction=m->shoot_turn?2:10-m->difficulty*2;
  if((int)f>reaction&&!m->shoot_resolved){int step=m->shoot_turn?48:34+m->difficulty*4;m->shoot_keeper+=(int16_t)FC_CLAMP(m->shoot_goal-m->shoot_keeper,-step,step);}
  if(f<=flight){
   int oldx=m->ball.x,oldy=m->ball.y,oldz=m->ball.z;
   m->ball.x=m->shoot_target*(int)f/(int)flight;m->ball.y=11*Q*(int)(flight-f)/(int)flight;
   m->ball.z=Q/6+(m->shoot_height-Q/6)*(int)f/(int)flight+220*4*(int)f*(int)(flight-f)/(int)(flight*flight);
   m->ball.vx=(m->ball.x-oldx)*40;m->ball.vy=(m->ball.y-oldy)*40;m->ball.vz=(m->ball.z-oldz)*40;
  }else{
   m->ball.x+=m->ball.vx/40;m->ball.y+=m->ball.vy/40;m->ball.z+=m->ball.vz/40;m->ball.vz-=50;
   if(m->ball.z<0){m->ball.z=0;m->ball.vz=-m->ball.vz/3;}
   if(m->shoot_result==FC_PEN_GOAL&&m->ball.y<-2*Q){m->ball.y=-2*Q;m->ball.vy=Q;}
  }
 }
 if(m->shoot_timer>=contact&&!m->shoot_resolved)resolve(m);
 if(m->shoot_timer>=contact+FC_PEN_SETTLE){
  if(m->single_penalty){
   int t=m->shoot_turn,dir=fc_attack(m,t),result=m->shoot_result;m->single_penalty=0;m->shoot_state=0;m->shoot_done=0;
   int keeper=fc_keeper(m,1-t);m->p[keeper].x=m->shoot_keeper;m->p[keeper].cooldown=8;
   if(result==FC_PEN_GOAL){m->score[t]=FC_MIN(99,m->score[t]+1);m->phase=FC_GOAL;m->timer=95;m->set_team=(uint8_t)(1-t);m->ball.x=0;m->ball.y=dir>0?FC_H:0;}
   else if(result==FC_PEN_MISS)fc_restart(m,FC_GOALKICK,1-t,0,(dir>0?FC_LENGTH-6:6)*Q);
   else{
    m->phase=FC_LIVE;m->ball.y=dir>0?FC_H-m->ball.y:m->ball.y;m->ball.y=FC_CLAMP(m->ball.y,Q,FC_H-Q);
    m->ball.x=FC_CLAMP(m->ball.x,-FC_W/2+Q,FC_W/2-Q);m->ball.vy=-dir*abs(m->ball.vy);m->ball.last=(int8_t)fc_keeper(m,1-t);m->ball.receiver=-1;m->ball_kind=FC_DRIVE;m->ball.grace=5;
    m->selected=(uint8_t)fc_nearest(m,0,m->ball.x,m->ball.y,false);
   }return;
  }
  int a=m->shoot_taken[0],b=m->shoot_taken[1],sa=m->shoot_score[0],sb=m->shoot_score[1];
  bool done=(a<=5&&b<=5&&(sa>sb+5-b||sb>sa+5-a))||(a>=5&&a==b&&sa!=sb);
  if(done){m->shoot_done=1;m->phase=FC_FULL;return;}
  m->shoot_turn^=1;m->shoot_round=(uint8_t)FC_MIN(a,b);ready(m);
 }
}
