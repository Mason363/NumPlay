#include "../src/football.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define Q FC_Q
static void isolated(FCMatch*m){
 FCProfile p;fc_defaults(&p);fc_new(m,&p,FC_FRIENDLY,1,0x2195);m->phase=FC_LIVE;m->selected=9;
 for(int i=0;i<22;i++){m->p[i].x=(i&1?30:-30)*Q;m->p[i].y=(i<11?10:100)*Q;m->p[i].vx=m->p[i].vy=0;}
 m->p[9].x=0;m->p[9].y=50*Q;m->ball.x=0;m->ball.y=50*Q;fc_possess(m,9);
}
int main(void){FCMatch m,other;
 /* Direction includes backwards passes, and a defender on the lane matters. */
 isolated(&m);m.p[5].x=0;m.p[5].y=40*Q;m.p[7].x=0;m.p[7].y=64*Q;m.p[8].x=-12*Q;m.p[8].y=50*Q;
 m.input_latch=FC_DOWN;assert(fc_pass_target(&m,9,FC_GROUND)==5);
 m.input_latch=FC_LEFT;assert(fc_pass_target(&m,9,FC_GROUND)==8);
 m.input_latch=FC_UP;assert(fc_pass_target(&m,9,FC_GROUND)==7);
 fc_pass_type(&m,9,FC_GROUND,300);int gy=m.receive_y;assert(m.ball.vy>0&&m.ball.z<Q&&m.ball.receiver==7);
 isolated(&other);other.p[7].x=0;other.p[7].y=64*Q;other.input_latch=FC_UP;fc_pass_type(&other,9,FC_RUN_PASS,300);assert(other.receive_y>=gy+3*Q);
 isolated(&other);other.p[7].x=0;other.p[7].y=64*Q;other.input_latch=FC_UP;fc_pass_type(&other,9,FC_HIGH_PASS,600);assert(other.ball.vz>=3*Q&&other.ball_kind==FC_HIGH_PASS);
 /* Releasing both keys together keeps the requested backward direction. */
 isolated(&m);m.p[5].x=0;m.p[5].y=40*Q;m.p[7].x=0;m.p[7].y=64*Q;
 for(int i=0;i<3;i++)fc_step(&m,FC_DOWN|FC_PASS);fc_step(&m,0);assert(m.ball.vy<0&&m.ball.receiver==5);
 /* Physical touches leave a varying foot gap; sprint is faster and looser. */
 isolated(&m);isolated(&other);int normal=0,sprint=0,min=10000,max=0;
 for(int i=0;i<100;i++){fc_step(&m,FC_UP);fc_step(&other,FC_UP|FC_SPRINT);if(m.ball.owner==9){int d=fc_distance(m.ball.x-m.p[9].x,m.ball.y-m.p[9].y);min=FC_MIN(min,d);max=FC_MAX(max,d);}normal=m.p[9].y;sprint=other.p[9].y;}
 assert(sprint>normal+3*Q&&max-min>Q/3&&other.p[9].stamina<m.p[9].stamina);
 /* Ground passes arrive without teleporting and can be trapped cleanly. */
 unsigned completed=0;
 for(int run=0;run<16;run++){
  isolated(&m);m.p[7].x=(run%3-1)*4*Q;m.p[7].y=60*Q;m.input_latch=FC_UP;fc_pass_type(&m,9,FC_GROUND,350);
  for(int tick=0;tick<100&&m.ball.owner<0;tick++)fc_step(&m,0);
  if(m.ball.owner==7)completed++;
 }assert(completed>=12);
 /* A well placed standing tackle is deterministic; a late one can foul. */
 isolated(&m);m.p[11+9].x=0;m.p[11+9].y=51*Q;m.ball.x=0;m.ball.y=51*Q;m.ball.owner=20;m.ball.last=20;m.p[9].x=0;m.p[9].y=51*Q-180;m.motion[20].face_y=-Q;
 fc_tackle(&m,9);assert(m.ball.owner==9);
 isolated(&m);m.p[20].x=0;m.p[20].y=51*Q;m.p[9].y=51*Q+200;m.ball.x=0;m.ball.y=51*Q-200;m.ball.owner=20;m.motion[20].face_y=-Q;fc_tackle(&m,9);assert(m.phase==FC_FREE&&m.fouls[0]==1);
 /* First-touch and interpolation preserve ownership/phase boundaries. */
 isolated(&m);other=m;other.clock++;other.p[9].x+=100;other.ball.x+=100;FCMatch visual;fc_interpolate(&visual,&m,&other,500);assert(visual.p[9].x==m.p[9].x+50&&visual.ball.x==m.ball.x+50);
 other.phase=FC_GOAL;fc_interpolate(&visual,&m,&other,0);assert(visual.p[9].x==other.p[9].x);
 /* Controls generate real shot/pass variety without changing the save format. */
 isolated(&m);m.input_latch=FC_RIGHT|FC_CONTROL;fc_shot(&m,9,600);assert(m.ball_kind==FC_FINESSE&&m.spin!=0);
 unsigned finishing[2]={0};
 for(int style=0;style<2;style++)for(int trial=0;trial<32;trial++){
  isolated(&m);m.seed=trial+1;m.p[9].y=FC_H-(trial%2?20:14)*Q;m.p[9].x=(trial%3-1)*4*Q;m.ball.x=m.p[9].x;m.ball.y=m.p[9].y;
  m.p[11].x=0;m.p[11].y=FC_H-2*Q;m.input_latch=(trial%4?FC_RIGHT:0)|(style?FC_CONTROL:0);fc_shot(&m,9,620);
  for(int tick=0;tick<120&&m.phase==FC_LIVE;tick++)fc_step(&m,0);
  finishing[style]+=m.score[0];
 }
 printf("Finishing fixtures: driven %u/32, finesse %u/32\n",finishing[0],finishing[1]);
 assert(finishing[0]>=8&&finishing[0]<32&&finishing[1]>=4&&finishing[1]<32);
 unsigned goals[2]={0},shots[2]={0},passes[2]={0},done[2]={0},loose=0,held=0;
 FCProfile p;fc_defaults(&p);
 for(unsigned game=0;game<12;game++){
  p.difficulty=game%3;fc_new(&m,&p,FC_FRIENDLY,1,game+88);
  while(m.phase!=FC_FULL){
   unsigned in=0;int owner=m.ball.owner;
   if(owner>=0&&owner<11){int dir=fc_attack(&m,0),goal=dir>0?FC_H:0;in=dir>0?FC_UP:FC_DOWN;
    if(abs(m.p[owner].y-goal)<23*Q){if(m.ticks%36<22)in|=FC_SHOOT;in|=(game&1)?FC_RIGHT:FC_LEFT;}
    else{in|=FC_SPRINT;if(m.p[owner].x>8*Q)in|=FC_LEFT;else if(m.p[owner].x<-8*Q)in|=FC_RIGHT;}
   }else{int dx=m.ball.x-m.p[m.selected].x,dy=m.ball.y-m.p[m.selected].y;if(abs(dx)>Q/3)in|=dx>0?FC_RIGHT:FC_LEFT;if(abs(dy)>Q/3)in|=dy>0?FC_UP:FC_DOWN;in|=FC_SPRINT;if(m.ticks%12==0)in|=FC_SWITCH;if(m.ticks%20==0)in|=FC_PASS;}
   fc_step(&m,in);if(m.phase==FC_LIVE){if(m.ball.owner<0)loose++;else held++;}
  }
  for(int t=0;t<2;t++){goals[t]+=m.score[t];shots[t]+=m.shots[t];passes[t]+=m.passes[t];done[t]+=m.completed[t];}
 }
 printf("Gameplay: %u/16 isolated passes received; 12 matches goals %u-%u, shots %u-%u, passes %u-%u, completed %u-%u, loose %u held %u\n",completed,goals[0],goals[1],shots[0],shots[1],passes[0],passes[1],done[0],done[1],loose,held);
 assert(shots[0]>5&&passes[1]>10&&done[1]>2&&loose>0&&held>0);
 FILE*report=fopen("gameplay-report.json","w");assert(report);fprintf(report,"{\"received\":%u,\"pass_fixtures\":16,\"driven_goals\":%u,\"finesse_goals\":%u,\"match_goals\":[%u,%u],\"shots\":[%u,%u],\"passes\":[%u,%u],\"completed\":[%u,%u]}\n",completed,finishing[0],finishing[1],goals[0],goals[1],shots[0],shots[1],passes[0],passes[1],done[0],done[1]);fclose(report);
 return 0;
}
