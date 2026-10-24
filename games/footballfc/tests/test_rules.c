#include "../src/football.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#define Q FC_Q
static void match(FCMatch*m,unsigned seed){FCProfile p;fc_defaults(&p);fc_new(m,&p,FC_FRIENDLY,1,seed);m->phase=FC_LIVE;}
static void rear(FCMatch*m,int speed){
 m->phase=FC_LIVE;m->p[9].x=m->p[20].x=0;m->p[20].y=51*Q;m->p[9].y=51*Q+200;
 m->p[9].vx=m->p[20].vx=m->p[20].vy=0;m->p[9].vy=-speed;m->p[9].cooldown=0;
 m->ball.x=0;m->ball.y=51*Q-200;m->ball.owner=20;m->motion[20].face_x=0;m->motion[20].face_y=-Q;
}
int main(void){FCMatch m;unsigned fooled[3]={0};
 for(int diff=0;diff<3;diff++)for(int trial=1;trial<=200;trial++){
  match(&m,trial);m.difficulty=diff;m.p[9].x=0;m.p[9].y=55*Q;m.p[9].vx=0;m.p[9].vy=5*Q;
  m.p[20].x=0;m.p[20].y=58*Q;m.p[20].vx=0;m.p[20].vy=-3*Q;m.ball.x=0;m.ball.y=55*Q;fc_possess(&m,9);
  fc_step(&m,FC_UP);fc_step(&m,FC_RIGHT);fooled[diff]+=m.ai_stumble[20]>0;
  assert(m.feint_cool>0);int time=m.ai_stumble[20];fc_step(&m,FC_DOWN);assert(m.ai_stumble[20]<=time);
 }
 printf("Feint fixtures: easy %u/200, normal %u/200, hard %u/200\n",fooled[0],fooled[1],fooled[2]);
 for(int d=0;d<3;d++)assert(fooled[d]>55&&fooled[d]<185);
 assert(fooled[0]>fooled[1]&&fooled[1]>fooled[2]);
 match(&m,1);rear(&m,0);fc_tackle(&m,9);assert(m.phase==FC_FREE&&!m.yellow[9]&&!m.red[9]);
 match(&m,1);rear(&m,5*Q);fc_tackle(&m,9);assert(m.phase==FC_FREE&&m.yellow[9]==1&&m.card_kind==1);
 rear(&m,5*Q);fc_tackle(&m,9);assert(m.yellow[9]==2&&m.red[9]&&m.card_kind==3&&!fc_active(&m,9)&&m.selected!=9);
 fc_restart(&m,FC_KICKOFF,0,0,FC_H/2);assert(fc_active(&m,m.taker)&&m.taker!=9);fc_step(&m,FC_PASS);assert(m.ball.receiver!=9);
 match(&m,1);rear(&m,9*Q);fc_tackle(&m,9);assert(m.red[9]&&m.card_kind==2);
 match(&m,1);rear(&m,8*Q);m.p[9].y=m.p[20].y+145;m.motion[20].think=100;
 fc_step(&m,FC_DOWN|FC_SPRINT);assert(m.fouls[0]==1&&m.phase==FC_FREE);
 match(&m,1);fc_foul(&m,11,9,2);assert(m.red[11]&&fc_keeper(&m,1)!=11&&fc_active(&m,fc_keeper(&m,1)));
 fc_restart(&m,FC_KICKOFF,0,0,FC_H/2);assert(m.p[fc_keeper(&m,1)].x==0&&m.p[fc_keeper(&m,1)].y>FC_H-4*Q);
 match(&m,1);for(int n=0;n<5;n++){m.phase=FC_LIVE;fc_foul(&m,12+n,9,2);}assert(m.phase==FC_FULL&&m.forfeit==2&&m.score[0]>=3);
 /* Match penalties return to the real match, with one score, not a shootout. */
 for(int team=0;team<2;team++)for(int result=0;result<2;result++){
  match(&m,99);int dir=fc_attack(&m,team),victim=team*11+9,offender=(1-team)*11+5;
  m.p[victim].x=0;m.p[victim].y=(dir>0?FC_LENGTH-10:10)*Q;fc_foul(&m,offender,victim,1);
  assert(m.phase==FC_PENALTY&&m.single_penalty&&m.shoot_state==1&&m.shoot_turn==team);
  m.shoot_state=2;m.shoot_timer=0;m.shoot_flight=26;m.shoot_target=result?FC_GOAL_HALF+200:700;m.shoot_height=Q;
  m.shoot_start=m.shoot_goal=m.shoot_keeper=-700;m.shoot_guard_z=Q;
  for(int i=0;i<100&&m.single_penalty;i++)fc_step(&m,0);
  assert(!m.single_penalty&&!m.shoot_state);assert(m.score[team]==(result?0:1));assert(m.phase==(result?FC_GOALKICK:FC_GOAL));
 }
 /* A wall uses active defenders, stays still before the kick, and can jump. */
 match(&m,1);fc_restart(&m,FC_FREE,0,0,FC_H-25*Q);assert(m.wall_count==4);
 unsigned used=0;for(int w=0;w<m.wall_count;w++){int i=m.wall[w];assert(i>=11&&i!=fc_keeper(&m,1)&&!(used&(1u<<(i%11))));used|=1u<<(i%11);assert(fc_distance(m.p[i].x-m.ball.x,m.p[i].y-m.ball.y)>9*Q);}
 fc_step(&m,FC_LOB|FC_PRESS);assert(m.curve>0&&m.kick_height==570);
 m.kick_clock=31;fc_step(&m,FC_SHOOT);fc_step(&m,0);assert(m.phase==FC_LIVE&&m.shots[0]==1&&m.kick_quality>=90&&m.spin>0);
 m.wall_clock=16;assert(fc_wall_lift(&m,m.wall[0])==Q/2);
 /* Curve changes the physical trajectory, not only the aiming graphic. */
 int vx=m.ball.vx;for(int i=0;i<6&&m.phase==FC_LIVE;i++)fc_step(&m,0);assert(m.ball.vx!=vx);
 for(int high=0;high<2;high++){
  match(&m,1);fc_restart(&m,FC_FREE,0,0,FC_H-25*Q);int w=m.wall[1];m.phase=FC_LIVE;m.wall_clock=16;
  m.ball.x=m.p[w].x;m.ball.y=m.p[w].y-Q;m.ball.z=high?3*Q:2*Q;m.ball.vy=20*Q;m.ball.owner=-1;m.ball.last=9;m.ball.grace=0;m.ball_kind=FC_DRIVE;
  fc_step(&m,0);if(high)assert(m.ball.last==9&&m.ball.vy>0);else assert(m.ball.last>=11&&m.ball.vy<0);
 }
 assert(fc_timing(32)==100&&fc_timing(0)==0&&fc_timing(64)==0);
 FCProfile p;fc_defaults(&p);fc_new(&m,&p,FC_SHOOTOUT,1,17);m.kick_clock=31;fc_step(&m,FC_PASS);assert(m.kick_quality==100);
 fc_new(&m,&p,FC_SHOOTOUT,1,17);fc_step(&m,FC_PASS);assert(m.kick_quality<10);
 unsigned good_error=0,poor_error=0;for(int seed=1;seed<=100;seed++){
  fc_new(&m,&p,FC_SHOOTOUT,1,seed);m.kick_clock=31;m.shoot_target=700;fc_step(&m,FC_PASS);good_error+=abs(m.shoot_target-700);
  fc_new(&m,&p,FC_SHOOTOUT,1,seed);m.shoot_target=700;fc_step(&m,FC_PASS);poor_error+=abs(m.shoot_target-700);
 }assert(good_error*8<poor_error);
 unsigned free_goals=0,blocked=0;
 for(int trial=0;trial<24;trial++){
  match(&m,100+trial);fc_restart(&m,FC_FREE,0,0,FC_H-25*Q);
  m.power=trial%2?350:650;m.kick_height=trial%3?570:350;m.aim=trial&2?700:-700;m.curve=(trial%3-1)*60;m.kick_clock=30;
  fc_step(&m,FC_SHOOT);fc_step(&m,0);
  for(int tick=0;tick<120&&m.phase==FC_LIVE;tick++)fc_step(&m,0);
  free_goals+=m.score[0]>0;blocked+=m.ball.last>=11;
 }printf("Free-kick fixtures: %u/24 goals, %u defensive contacts\n",free_goals,blocked);assert(free_goals>0&&free_goals<24&&blocked>0);
 puts("Rules: contextual feints, carelessness/yellow/second-yellow/red, legal active restarts, replacement keeper, forfeit, match penalties, wall/curve/timing passed");
 FILE*f=fopen("rules-report.json","w");assert(f);fprintf(f,"{\"feint_trials\":200,\"fooled\":[%u,%u,%u],\"cards\":true,\"match_penalties\":true,\"wall_curve_timing\":true}\n",fooled[0],fooled[1],fooled[2]);fclose(f);
 return 0;
}
