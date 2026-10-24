#include "../src/football.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define Q FC_Q
static void alone(FCMatch*m){
 FCProfile p;fc_defaults(&p);fc_training_new(m,&p,2,100);m->p[9].x=0;m->p[9].y=55*Q;
 m->ball.x=0;m->ball.y=55*Q;m->duration=10000;
}
int main(void){
 FCMatch m,a,b;FCProfile p;fc_defaults(&p);
 /* Vector acceleration and sprint speed do not benefit diagonal input. */
 alone(&a);b=a;
 for(int n=0;n<32;n++){
  fc_move_to(&a,9,0,90*Q,8*Q);fc_move_to(&b,9,30*Q,85*Q,8*Q);
  int v0=a.p[9].vx*a.p[9].vx+a.p[9].vy*a.p[9].vy;
  int v1=b.p[9].vx*b.p[9].vx+b.p[9].vy*b.p[9].vy;
  assert(abs(v0-v1)<80000);
 }
 /* Frequent steering and sharp sprint turns retain a controllable ball. */
 alone(&m);int retained=0,maxgap=0;
 unsigned directions[]={FC_UP,FC_RIGHT,FC_DOWN,FC_LEFT};
 for(int tick=0;tick<320;tick++){
  fc_step(&m,directions[(tick/20)%4]|FC_SPRINT);
  if(m.ball.owner==9)retained++;
  maxgap=FC_MAX(maxgap,fc_distance(m.ball.x-m.p[9].x,m.ball.y-m.p[9].y));
 }
 printf("Sprint turns: retained %d/320, max gap %d/256 m\n",retained,maxgap);
 assert(retained==320&&maxgap<3*Q);
 /* Receptions remain assisted when idle, but manual steering is immediate. */
 alone(&m);m.mode=FC_TRAINING;m.drill=1;m.p[7].x=8*Q;m.p[7].y=55*Q;
 m.input_latch=FC_RIGHT;fc_pass_type(&m,9,FC_GROUND,500);assert(m.selected==7);
 fc_step(&m,FC_RIGHT|FC_SPRINT);assert(m.p[7].vx>0);
 /* OK on a free ball must not start an empty tackle and block its capture. */
 alone(&m);m.ball.owner=-1;m.ball.x=Q;m.ball.y=m.p[9].y;m.ball.grace=20;
 fc_step(&m,FC_PASS);assert(!m.p[9].cooldown);
 /* The carrier owns both movement and actions, including after switching. */
 fc_new(&m,&p,FC_FRIENDLY,1,123);m.phase=FC_LIVE;m.p[9].x=0;m.p[9].y=55*Q;m.ball.x=0;m.ball.y=55*Q;fc_possess(&m,9);m.selected=2;
 fc_step(&m,FC_SWITCH|FC_UP|FC_SHOOT);assert(m.selected==9&&m.charge_owner==9&&m.p[9].vy>0);
 /* Any teammate completing a real pass counts, not just the named target. */
 alone(&m);m.ball.owner=-1;m.ball.last=5;m.ball.receiver=7;m.ball_kind=FC_GROUND;fc_possess(&m,9);assert(m.completed[0]==1);
 /* Pass power is clamped before speed/lead arithmetic, even via direct APIs. */
 alone(&a);b=a;fc_pass_type(&a,9,FC_RUN_PASS,1000);fc_pass_type(&b,9,FC_RUN_PASS,65000);assert(a.ball.vx==b.ball.vx&&a.ball.vy==b.ball.vy);
 /* Every kickoff offers a short, legal reception even while moving forward. */
 for(int half=0;half<2;half++){
  fc_new(&m,&p,FC_FRIENDLY,1,123);m.half=half;fc_restart(&m,FC_KICKOFF,0,0,FC_H/2);
  fc_step(&m,FC_UP|FC_PASS);assert(m.phase==FC_LIVE&&m.ball.receiver==7&&m.ball.vy*fc_attack(&m,0)<0);
  for(int tick=0;tick<100&&m.ball.owner<0;tick++)fc_step(&m,0);assert(m.ball.owner==7);
 }
 /* OK at a distant free kick makes a pass; EXE still requests a shot. */
 fc_new(&m,&p,FC_FRIENDLY,1,123);fc_restart(&m,FC_FREE,0,0,45*Q);fc_step(&m,FC_PASS);assert(m.phase==FC_LIVE&&m.passes[0]==1&&!m.shots[0]);
 fc_new(&m,&p,FC_FRIENDLY,1,123);fc_restart(&m,FC_FREE,0,0,45*Q);fc_step(&m,FC_SHOOT);fc_step(&m,0);assert(m.shots[0]==1);
 /* A real ground-level keeper contact stops a dribble, not just free shots. */
 fc_new(&m,&p,FC_FRIENDLY,1,123);m.phase=FC_LIVE;m.p[9].x=0;m.p[9].y=FC_H-3*Q;
 m.p[11].x=0;m.p[11].y=FC_H-3*Q;m.ball.x=0;m.ball.y=m.p[9].y;fc_possess(&m,9);fc_step(&m,0);assert(m.ball.owner==11&&!m.score[0]);
 /* Ground, through and high passes must all be physically receivable. */
 unsigned received[3]={0};
 for(int kind=FC_GROUND;kind<=FC_HIGH_PASS;kind++)for(int trial=0;trial<12;trial++){
  fc_training_new(&m,&p,1,100+trial);m.input_latch=FC_LEFT|FC_UP;
  fc_pass_type(&m,9,kind,350+trial*35);
  int target=m.ball.receiver;assert(target==7);
  for(int tick=0;tick<180&&m.ball.owner<0;tick++)fc_step(&m,0);
  if(m.ball.owner==target)received[kind-1]++;
 }printf("Receptions: ground %u/12, through %u/12, high %u/12\n",received[0],received[1],received[2]);
 assert(received[0]>=10&&received[1]>=9&&received[2]>=9);
 /* A settled possession has separate lines, width and nearby support. The
  * wide midfielder in 4-4-2 and wingback in 3-4-3 retain their real roles. */
 for(int form=0;form<3;form++)for(int half=0;half<2;half++){
  p.formation=form;fc_new(&m,&p,FC_FRIENDLY,1,123);m.phase=FC_LIVE;m.half=half;int dir=fc_attack(&m,0);
  m.p[9].x=-10*Q;m.p[9].y=FC_H/2;fc_possess(&m,9);m.ball.x=m.p[9].x;m.ball.y=m.p[9].y;
  for(int tick=0;tick<500;tick++){
   fc_ai(&m,0);
   for(int i=0;i<11;i++)if(i!=9){m.p[i].x+=m.p[i].vx/40;m.p[i].y+=m.p[i].vy/40;}
  }
  int options=0;for(int i=1;i<11;i++)if(i!=9&&fc_distance(m.p[i].x-m.p[9].x,m.p[i].y-m.p[9].y)<23*Q)options++;
  assert(options>=2);assert(abs(m.p[1].x-m.p[form==2?3:4].x)>25*Q);
  assert((m.p[6].y-m.ball.y)*dir<-7*Q);
  if(form==1)assert((m.p[8].y-m.ball.y)*dir<6*Q);
  if(form==2)assert((m.p[4].y-m.ball.y)*dir>-8*Q);
 }
 puts("Refinement: radial acceleration, turns, manual reception, carrier ownership, pass stats/power, kickoffs, free kicks, keeper dribble contacts passed");
 FILE*f=fopen("refinement-report.json","w");assert(f);
 fprintf(f,"{\"sprint_turns_retained\":%d,\"turn_ticks\":320,\"max_dribble_gap_q\":%d,\"ground_received\":%u,\"through_received\":%u,\"high_received\":%u,\"pass_trials_per_kind\":12,\"formation_cases\":6}\n",retained,maxgap,received[0],received[1],received[2]);fclose(f);
 return 0;
}
