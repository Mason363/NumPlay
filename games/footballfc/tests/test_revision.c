#include "../src/football.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define Q FC_Q
int main(void){FCProfile p,before;FCMatch m;fc_defaults(&p);unsigned red=0,received[3]={0};
 const unsigned phases[]={0,4,57,62};
 for(int seed=1;seed<=100;seed++)for(int phase=0;phase<4;phase++){
  fc_new(&m,&p,FC_SHOOTOUT,1,seed);m.shoot_target=(seed%3-1)*700;m.shoot_height=100+seed%500;m.kick_clock=phases[phase];
  fc_step(&m,FC_PASS);assert(m.kick_quality<40);
  assert(abs(m.shoot_target)>FC_GOAL_HALF+100||m.shoot_height>FC_GOAL_HEIGHT+100);
  for(int tick=0;tick<80&&!m.shoot_resolved;tick++)fc_step(&m,0);
  assert(m.shoot_result==FC_PEN_MISS&&m.shoot_score[0]==0);red++;
 }
 for(int kind=FC_GROUND;kind<=FC_HIGH_PASS;kind++)for(int trial=0;trial<12;trial++){
  fc_training_new(&m,&p,1,100+trial);m.input_latch=FC_LEFT|FC_UP;fc_pass_type(&m,9,kind,350+trial*35);
  int target=m.ball.receiver;assert(target==7);
  for(int tick=0;tick<180&&m.ball.owner<0;tick++)fc_step(&m,FC_UP);
  if(m.ball.owner==target){received[kind-1]++;for(int tick=0;tick<8;tick++){fc_step(&m,FC_RIGHT);assert(m.ball.owner==target);}}
 }
 printf("Held-direction receptions: %u/12 ground, %u/12 through, %u/12 high\n",received[0],received[1],received[2]);
 assert(received[0]>=10&&received[1]>=9&&received[2]>=9);
 /* Assisted receptions still allow a defender to intercept the actual path. */
 fc_new(&m,&p,FC_FRIENDLY,1,10);m.phase=FC_LIVE;
 for(int i=0;i<22;i++){m.p[i].x=(i&1?30:-30)*Q;m.p[i].y=(i<11?8:100)*Q;m.p[i].vx=m.p[i].vy=0;}
 m.p[9].x=m.p[7].x=m.p[20].x=0;m.p[9].y=50*Q;m.p[7].y=64*Q;m.p[20].y=57*Q;m.motion[20].think=100;
 m.ball.x=0;m.ball.y=50*Q;fc_possess(&m,9);m.input_latch=FC_UP;fc_pass_type(&m,9,FC_GROUND,500);
 for(int tick=0;tick<80&&m.ball.owner<0;tick++)fc_step(&m,0);assert(m.ball.owner>=11&&m.completed[0]==0);
 unsigned practice_goals=0;
 for(int diff=0;diff<3;diff++){
  p.difficulty=diff;fc_new(&m,&p,FC_FREEPLAY,1,42);assert(m.phase==FC_FREE&&m.wall_count>=3&&m.taker==9);
  for(int trial=0;trial<16;trial++){
   m.aim=trial&1?650:-650;m.power=350;m.kick_height=570;m.curve=(trial%3-1)*35;m.kick_clock=30;
   fc_step(&m,FC_SHOOT);fc_step(&m,0);assert(m.phase==FC_LIVE&&m.free_attempts==trial+1);
   for(int tick=0;tick<260&&m.phase!=FC_FREE;tick++)fc_step(&m,0);
   assert(m.phase==FC_FREE&&m.free_attempts==trial+1&&m.free_goals<=m.free_attempts&&m.wall_count>=3);
  }
  practice_goals+=m.free_goals;before=p;fc_finish(&p,&m);assert(!memcmp(&p,&before,sizeof p));
  unsigned attempts=m.free_attempts;fc_step(&m,FC_TACTIC);int x=m.free_x,y=m.free_y;
  for(int tick=0;tick<10;tick++)fc_step(&m,FC_TACTIC);assert(m.free_x==x&&m.free_y==y&&m.free_attempts==attempts);
 }
 assert(practice_goals>0);printf("Revision: %u/400 red timings missed physically; 48 repeated practice kicks, %u goals; interception/retention/no rewards passed\n",red,practice_goals);
 FILE*f=fopen("revision-report.json","w");assert(f);fprintf(f,"{\"red_penalties\":%u,\"red_goals\":0,\"held_ground_received\":%u,\"held_through_received\":%u,\"held_high_received\":%u,\"practice_shots\":48,\"practice_goals\":%u}\n",red,received[0],received[1],received[2],practice_goals);fclose(f);
 return 0;
}
