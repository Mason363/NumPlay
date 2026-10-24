#include "football.h"
#include <stdlib.h>
#define Q FC_Q

unsigned fc_timing(unsigned clock){return 100u-(unsigned)abs((int)(clock%64)-32)*100u/32u;}
void fc_free_basis(const FCMatch*m,int*ox,int*oy,int*fx,int*fy){
 static int last_x=INT32_MIN,last_y,last_dir,cached_x,cached_y;
 *ox=m->free_view?m->free_x:m->ball.x;*oy=m->free_view?m->free_y:m->ball.y;
 int dir=fc_attack(m,m->free_team);
 if(*ox!=last_x||*oy!=last_y||dir!=last_dir){int dx=-*ox,dy=(dir>0?FC_H:0)-*oy,d=FC_MAX(Q,fc_distance(dx,dy));cached_x=dx*Q/d;cached_y=dy*Q/d;last_x=*ox;last_y=*oy;last_dir=dir;}
 *fx=cached_x;*fy=cached_y;
}
void fc_foul(FCMatch*m,int offender,int victim,unsigned severity){
 if(m->phase!=FC_LIVE||!fc_active(m,offender)||!fc_active(m,victim)||offender/11==victim/11)return;
 int t=offender/11,awarded=victim/11,dir=fc_attack(m,awarded),x=m->p[victim].x,y=m->p[victim].y;
 bool penalty=abs(x)<20*Q&&(dir>0?y>FC_H-16*Q:y<16*Q);
 m->fouls[t]=FC_MIN(99,m->fouls[t]+1);m->card_kind=0;
 if(severity>=2){m->red[offender]=1;m->card_kind=2;}
 else if(severity){if(m->yellow[offender]){m->yellow[offender]=2;m->red[offender]=1;m->card_kind=3;}else{m->yellow[offender]=1;m->card_kind=1;}}
 if(m->red[offender]){
  m->p[offender].vx=m->p[offender].vy=0;
  if(offender==fc_keeper(m,t))for(int i=t*11+1;i<(t+1)*11;i++)if(fc_active(m,i)){m->keeper_id[t]=(uint8_t)i;m->p[i].x=0;m->p[i].y=(int16_t)((fc_attack(m,t)>0?3:FC_LENGTH-3)*Q);m->p[i].vx=m->p[i].vy=0;break;}
  int active=0;for(int i=t*11;i<(t+1)*11;i++)active+=fc_active(m,i);
  if(active<7){m->forfeit=(uint8_t)(t+1);m->score[1-t]=FC_MAX(m->score[1-t],FC_MAX(3,m->score[t]+1));m->phase=FC_FULL;m->ball.owner=-1;return;}
 }
 if(!fc_active(m,m->selected))m->selected=(uint8_t)fc_nearest(m,0,x,y,false);
 if(penalty){x=0;y=(dir>0?FC_LENGTH-11:11)*Q;}
 fc_restart(m,penalty?FC_PENALTY:FC_FREE,awarded,x,y);
 m->notice=1;m->card_player=(uint8_t)offender;m->card_timer=m->card_kind?120:0;
 if(penalty)fc_start_match_penalty(m,awarded);
}
void fc_wall_setup(FCMatch*m){
 m->wall_count=0;m->wall_clock=0;if(m->phase!=FC_FREE)return;
 int dir=fc_attack(m,m->set_team),dx=-m->ball.x,dy=(dir>0?FC_H:0)-m->ball.y,d=fc_distance(dx,dy);
 if(d<12*Q||d>38*Q||abs(m->ball.x)>30*Q)return;
 int count=d<29*Q?4:3,used=0;
 for(int n=0;n<count;n++){
  int offset=(2*n-count+1)*120;
  int x=m->ball.x+dx*2343/d-dy*offset/d,y=m->ball.y+dy*2343/d+dx*offset/d;
  int best=-1,cost=100000,t=1-m->set_team;
  for(int i=t*11;i<(t+1)*11;i++)if(fc_active(m,i)&&i!=fc_keeper(m,t)&&!(used&(1<<(i%11)))){
   int v=fc_distance(m->p[i].x-x,m->p[i].y-y);if(v<cost){best=i;cost=v;}
  }
  if(best<0)break;used|=1<<(best%11);m->wall[m->wall_count++]=(uint8_t)best;
  m->p[best].x=(int16_t)FC_CLAMP(x,-FC_W/2+Q,FC_W/2-Q);m->p[best].y=(int16_t)FC_CLAMP(y,Q,FC_H-Q);
  m->p[best].vx=m->p[best].vy=0;m->motion[best].face_x=(int16_t)(-dx*Q/d);m->motion[best].face_y=(int16_t)(-dy*Q/d);
 }
}
int fc_wall_lift(const FCMatch*m,int player){
 for(int n=0;n<m->wall_count;n++)if(m->wall[n]==player&&fc_active(m,player)){
  int t=(int)m->wall_clock-8;if(t>0&&t<16)return t*(16-t)*Q/128;
 }return 0;
}
void fc_free_velocity(const FCMatch*m,int*vx,int*vy,int*vz){
 int dir=fc_attack(m,m->set_team),dx=m->aim-m->ball.x,dy=(dir>0?FC_H:0)-m->ball.y;
 int dist=FC_MAX(Q,fc_distance(dx,dy)),speed=(15+(int)m->power/70)*Q;
 *vx=dx*speed/dist;*vy=dy*speed/dist;
 *vz=FC_CLAMP(dist*5*Q/speed+(m->kick_height?m->kick_height:350)*speed/dist,2*Q,14*Q);
}
static void free_shot(FCMatch*m){
 int i=m->taker,t=m->set_team,vx,vy,vz;fc_free_velocity(m,&vx,&vy,&vz);
 int error=(100-(int)m->kick_quality)*6,miss=error?(int)(fc_random(&m->seed)%(2*error+1))-error:0;
 if(m->kick_quality<40){
  int dir=fc_attack(m,t),dist=FC_MAX(Q,abs((dir>0?FC_H:0)-m->ball.y));
  int side=m->aim<0?-1:m->aim>0?1:(fc_random(&m->seed)&1?1:-1);
  vx=(side*(FC_GOAL_HALF+12*Q)-m->ball.x)*abs(vy)/dist;
 }
 m->ball=(FCBall){.x=m->p[i].x,.y=m->p[i].y,.z=Q/5,.vx=vx+miss,.vy=vy,.vz=vz,.owner=-1,.last=(int8_t)i,.receiver=-1,.grace=3};
 m->spin=(int16_t)(m->curve*fc_attack(m,t));m->ball_kind=FC_DRIVE;m->shots[t]=FC_MIN(99,m->shots[t]+1);
 m->p[i].cooldown=12;m->motion[i].action=12;m->pass_cool=14;m->keeper_wait[0]=12;m->keeper_wait[1]=18-m->difficulty*4;
 m->phase=FC_LIVE;m->notice=0;m->charge=0;m->wall_clock=1;m->free_age=1;m->free_view=1;
 if(m->mode==FC_FREEPLAY)m->free_attempts=FC_MIN(9999,m->free_attempts+1);
}
void fc_free_step(FCMatch*m,unsigned in,unsigned hit){
 m->kick_clock++;if(m->timer)m->timer--;
 if(!m->set_team){
  m->aim=(int16_t)FC_CLAMP(m->aim+(!!(in&FC_RIGHT)-!!(in&FC_LEFT))*12*fc_attack(m,0),-FC_GOAL_HALF-384,FC_GOAL_HALF+384);
  m->power=(uint16_t)FC_CLAMP((int)m->power+(!!(in&FC_UP)-!!(in&FC_DOWN))*9,250,1000);
  m->curve=(int8_t)FC_CLAMP(m->curve+(!!(in&FC_LOB)-!!(in&FC_THROUGH))*2,-80,80);
  if(hit&FC_SWITCH)m->curve=0;
  if(hit&FC_PRESS)m->kick_height=m->kick_height<200?350:m->kick_height<450?570:100;
  if(in&FC_SHOOT)m->charge=FC_MIN(1000,m->charge+22);
  if((hit&FC_PASS)&&m->mode!=FC_FREEPLAY){m->ball.owner=(int8_t)m->taker;fc_pass_type(m,m->taker,FC_GROUND,500);m->phase=FC_LIVE;m->wall_count=0;m->notice=0;m->free_view=2;return;}
  if(!(hit&FC_PASS)&&((in&FC_SHOOT)||(!m->charge&&m->timer)))return;
  m->kick_quality=(uint8_t)fc_timing(m->kick_clock);
  /* A tap shoots immediately; holding EXE releases at the chosen rhythm. */
 }else{
  if(m->timer)return;
  int distance=abs(m->ball.y-(fc_attack(m,1)>0?FC_H:0));
  if(distance>36*Q){m->ball.owner=(int8_t)m->taker;fc_pass_type(m,m->taker,FC_GROUND,500);m->phase=FC_LIVE;m->wall_count=0;return;}
  m->aim=(int16_t)((fc_random(&m->seed)&1?1:-1)*(450+fc_random(&m->seed)%350));m->curve=(int8_t)((fc_random(&m->seed)%81)-40);m->kick_quality=(uint8_t)(35+fc_random(&m->seed)%61);
 }
 free_shot(m);
}
void fc_free_reset(FCMatch*m){
 static const int8_t spots[8][2]={{0,22},{-8,24},{9,26},{-12,21},{10,30},{0,28},{-6,19},{7,23}};
 unsigned n=fc_random(&m->seed)%8;int x=spots[n][0]*Q,y=FC_H-spots[n][1]*Q;
 for(int i=0;i<22;i++){m->p[i].x=(int16_t)((i&1?34:-34)*Q);m->p[i].y=5*Q;m->p[i].vx=m->p[i].vy=0;m->p[i].cooldown=0;m->motion[i].touch=m->motion[i].action=0;}
 m->p[9].x=(int16_t)x;m->p[9].y=(int16_t)y;m->motion[9].face_x=(int16_t)(-x/24);m->motion[9].face_y=Q;
 fc_restart(m,FC_FREE,0,x,y);m->p[11].x=0;m->p[11].y=FC_H-3*Q;m->power=400;m->kick_height=570;m->timer=UINT16_MAX;m->free_result=FC_PEN_WAIT;
}
void fc_free_update(FCMatch*m){
 if(m->phase!=FC_LIVE)return;
 if(m->free_age>12&&(m->ball.owner>=0||m->free_age>=160)){
  m->free_result=m->ball.last>=11?FC_PEN_SAVE:FC_PEN_MISS;m->phase=FC_HALF;m->timer=60;
 }
}
