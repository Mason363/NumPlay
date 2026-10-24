#include "../src/football.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#define Q FC_Q
static void fixture(FCMatch*m,int x,int z,int keeper){
 FCProfile p;fc_defaults(&p);fc_new(m,&p,FC_SHOOTOUT,1,17);
 m->shoot_state=2;m->shoot_timer=0;m->shoot_target=x;m->shoot_height=z;m->shoot_keeper=m->shoot_start=m->shoot_goal=keeper;m->shoot_guard_z=z;m->shoot_flight=26;m->power=600;
}
int main(void){FCMatch m;fixture(&m,700,400,-700);
 for(int i=0;i<FC_PEN_RUNUP;i++){fc_step(&m,0);assert(m.ball.x==0&&m.ball.y==11*Q&&m.ball.z==Q/6);}
 int last=m.ball.y;for(int i=0;i<26;i++){fc_step(&m,0);assert(m.ball.y<last);last=m.ball.y;if(i==12)assert(m.ball.z>300);}
 assert(m.shoot_result==FC_PEN_GOAL&&m.shoot_score[0]==1&&m.shoot_marks[0]==1);
 for(int i=0;i<20;i++)fc_step(&m,0);assert(m.shoot_score[0]==1&&m.shoot_taken[0]==1);
 fixture(&m,0,256,0);for(int i=0;i<FC_PEN_RUNUP+26;i++)fc_step(&m,0);assert(m.shoot_result==FC_PEN_SAVE&&m.ball.vy>0);
 fixture(&m,FC_GOAL_HALF+100,256,0);for(int i=0;i<FC_PEN_RUNUP+26;i++)fc_step(&m,0);assert(m.shoot_result==FC_PEN_MISS);
 fixture(&m,FC_GOAL_HALF,256,0);for(int i=0;i<FC_PEN_RUNUP+26;i++)fc_step(&m,0);assert(m.shoot_result==FC_PEN_POST);
 fixture(&m,500,256,0);m.shoot_goal=800;int old=0;
 for(int i=0;i<FC_PEN_RUNUP+26;i++){fc_step(&m,0);assert(abs(m.shoot_keeper-old)<=42);old=m.shoot_keeper;}assert(old>0&&old<800);
 FCProfile p;fc_defaults(&p);unsigned scored[3]={0};
 for(int diff=0;diff<3;diff++)for(int trial=0;trial<100;trial++){
  p.difficulty=diff;fc_new(&m,&p,FC_SHOOTOUT,1,trial+101);m.kick_clock=31;m.shoot_target=(trial%3-1)*700;m.shoot_height=100+(trial%4)*130;
  fc_step(&m,FC_PASS);assert(m.shoot_state==2);
  for(int i=0;i<80&&!m.shoot_resolved;i++)fc_step(&m,0);
  assert(m.shoot_resolved&&m.shoot_taken[0]==1);scored[diff]+=m.shoot_result==FC_PEN_GOAL;
 }
 printf("Penalties: physical flight, timed strike, progressive dive, goal/save/miss/post, single awards; 100-shot fixtures %u/%u/%u goals\n",scored[0],scored[1],scored[2]);
 assert(scored[0]>=scored[1]&&scored[1]>=scored[2]);
 assert(scored[0]>45&&scored[1]>40&&scored[2]>30&&scored[0]<100&&scored[1]<100&&scored[2]<100);
 return 0;}
