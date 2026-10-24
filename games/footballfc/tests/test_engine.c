#include "../src/football.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void invariant(const FCMatch*m){
 assert(m->selected<11&&m->phase<=FC_PENALTY&&m->ball.owner>=-1&&m->ball.owner<22&&m->teams[0]<32&&m->teams[1]<32);
 assert(m->ball.last>=0&&m->ball.last<22&&m->half<=1&&m->charge<=1000&&m->clock<=m->duration);
 for(int i=0;i<22;i++){assert(abs(m->p[i].x)<=FC_W/2&&m->p[i].y>=0&&m->p[i].y<=FC_H);assert(m->p[i].stamina<=1000);}
}
static void save_roundtrip(const FCProfile*p){uint8_t b[FC_SAVE_BYTES];FCProfile q;fc_encode(p,b);assert(fc_decode(&q,b,sizeof b));assert(!memcmp(p,&q,sizeof q));}
int main(void){
 FCProfile p;FCMatch m;fc_defaults(&p);save_roundtrip(&p);
 unsigned goals=0,steps=0;
 for(unsigned team=0;team<32;team++)for(unsigned diff=0;diff<3;diff++){
  p.team=(uint8_t)team;p.difficulty=(uint8_t)diff;fc_new(&m,&p,FC_FRIENDLY,(team+7)%32,team*11+diff+1);
  while(m.phase!=FC_FULL&&steps<2000000){unsigned in=0;if(m.phase==FC_LIVE){
    int owner=m.ball.owner;if(owner>=0&&owner<11){int dx=m.p[owner].x;in=FC_UP|FC_SPRINT;if(m.half)in=FC_DOWN|FC_SPRINT;
     if(abs(m.p[owner].y-(m.half?0:FC_H))<27*256){in|=FC_SHOOT;if(m.ticks%40>24)in&=~FC_SHOOT;in|=dx>0?FC_LEFT:FC_RIGHT;}
    }else in=FC_SWITCH|FC_UP|((m.ticks%16)==0?FC_PASS:0);
   }fc_step(&m,in);invariant(&m);steps++;}
  assert(m.phase==FC_FULL);assert(m.half==1);goals+=m.score[0]+m.score[1];
 }
 assert(goals>10);
 uint32_t seed=0x2ab451;
 for(unsigned trial=0;trial<256;trial++){
  fc_new(&m,&p,FC_FRIENDLY,trial%32,trial+100);for(unsigned i=0;i<1800;i++){fc_step(&m,fc_random(&seed)&8191);invariant(&m);}
 }
 /* Swept goal-plane test: a fast low shot crosses in a single tick, once. */
 fc_defaults(&p);fc_new(&m,&p,FC_FRIENDLY,1,8);m.phase=FC_LIVE;
 m.ball.owner=-1;m.ball.grace=10;m.ball.x=2*256;m.ball.y=FC_H-40;m.ball.vy=30*256;m.ball.z=100;m.ball.vz=0;
 fc_step(&m,0);assert(m.phase==FC_GOAL&&m.score[0]==1);fc_step(&m,0);assert(m.score[0]==1);
 fc_new(&m,&p,FC_FRIENDLY,1,9);m.phase=FC_LIVE;m.ball.owner=-1;m.ball.grace=10;m.ball.x=8*256;m.ball.y=FC_H-40;m.ball.vy=30*256;
 fc_step(&m,0);assert(m.score[0]==0&&m.phase==FC_GOALKICK);
 fc_new(&m,&p,FC_FRIENDLY,1,9);m.phase=FC_LIVE;m.ball.owner=-1;m.ball.grace=10;m.ball.x=FC_W/2-4;m.ball.y=50*256;m.ball.vx=10*256;m.ball.last=2;
 fc_step(&m,0);assert(m.phase==FC_THROW&&m.set_team==1);
 /* Shoot button release must kick; directional input must match the screen. */
 fc_new(&m,&p,FC_FRIENDLY,1,2);m.phase=FC_LIVE;m.ball.owner=9;m.selected=9;m.p[9].y=70*256;
 m.ball.x=m.p[9].x;m.ball.y=m.p[9].y;
 for(int j=11;j<22;j++){m.p[j].x=30*256;m.p[j].y=100*256;}
 int y=m.p[9].y;for(int i=0;i<20;i++)fc_step(&m,FC_UP|FC_SHOOT);assert(m.p[9].y>y&&m.charge>0);fc_step(&m,0);assert(m.ball.owner==-1&&m.charge==0&&m.shots[0]);
 /* Every pair occurs once in an eight-team season. */
 unsigned seen[8][8]={{0}};for(unsigned r=0;r<7;r++){unsigned mask=0;for(unsigned j=0;j<4;j++){unsigned a,b;fc_league_pair(r,j,&a,&b);assert(a!=b&&a<8&&b<8);assert(!(mask&(1u<<a))&&!(mask&(1u<<b)));mask|=(1u<<a)|(1u<<b);seen[FC_MIN(a,b)][FC_MAX(a,b)]++;}assert(mask==255);}
 for(int a=0;a<8;a++)for(int b=a+1;b<8;b++)assert(seen[a][b]==1);
 fc_career_new(&p,4);unsigned oldmoney=p.money;for(int i=0;i<7;i++){fc_new(&m,&p,FC_CAREER,fc_career_opponent(&p),1);m.phase=FC_FULL;m.score[0]=3;m.score[1]=0;fc_finish(&p,&m);save_roundtrip(&p);}
 assert(p.pts[0]==21&&fc_rank(&p,0)==1&&p.trophies==1&&p.money>oldmoney&&p.career_round==7);fc_career_next(&p);assert(p.season==2&&p.career_round==0&&p.pts[0]==0);
 p.money=99999;assert(fc_train(&p,0));assert(fc_contract(&p,0,7));assert(fc_coach(&p));assert(fc_recruit(&p,2));save_roundtrip(&p);
 p.money=0;assert(!fc_train(&p,0)&&!fc_contract(&p,0,3)&&!fc_coach(&p)&&!fc_recruit(&p,1));
 for(unsigned t=0;t<32;t++){
  fc_defaults(&p);fc_cup_new(&p,t);int games=0;
  while(p.cup_active){assert(fc_cup_opponent(&p)!=t);fc_new(&m,&p,FC_CUP,fc_cup_opponent(&p),1);m.score[0]=5;m.score[1]=0;fc_finish(&p,&m);save_roundtrip(&p);assert(++games<=7);}
  assert(games==7&&p.cup_alive&&p.cup_stage==5&&p.trophies==1);
 }
 /* Penalty shootout: five winners, early clinch, and sudden death. */
 fc_new(&m,&p,FC_SHOOTOUT,1,1);for(int i=0;i<20&&!m.shoot_done;i++){
  m.shoot_state=2;m.shoot_timer=FC_PEN_RUNUP+25;m.shoot_flight=26;m.shoot_resolved=0;m.shoot_height=256;m.shoot_guard_z=256;m.shoot_target=-800;m.shoot_keeper=m.shoot_turn?-800:800;m.shoot_goal=m.shoot_keeper;m.power=700;
  for(int j=0;j<46&&!m.shoot_done;j++)fc_penalty_step(&m,0);
 }assert(m.shoot_done&&m.shoot_score[0]>m.shoot_score[1]);
 uint8_t bytes[FC_SAVE_BYTES];fc_encode(&p,bytes);FCProfile q;
 for(unsigned i=0;i<FC_SAVE_BYTES;i++){bytes[i]^=128;assert(!fc_decode(&q,bytes,sizeof bytes));bytes[i]^=128;}
 assert(!fc_decode(&q,NULL,0));assert(!fc_decode(&q,bytes,sizeof bytes-1));
 printf("96 complete matches, %u steps, %u goals; 256 random-input runs; 32 full World Cups; league, contracts, saves and penalties passed\n",steps,goals);
 FILE*report=fopen("engine-report.json","w");assert(report);fprintf(report,"{\"matches\":96,\"steps\":%u,\"goals\":%u,\"random_runs\":256,\"cups\":32}\n",steps,goals);fclose(report);
 return 0;
}
