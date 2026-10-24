#include "football.h"
#include <string.h>
#include <stdlib.h>
#define Q FC_Q

static const int8_t shape[3][11][2]={
 {{0,3},{-23,23},{-8,20},{8,20},{23,23},{-16,45},{0,40},{16,45},{-22,70},{0,77},{22,70}},
 {{0,3},{-23,23},{-8,20},{8,20},{23,23},{-23,48},{-8,44},{8,44},{23,48},{-11,76},{11,76}},
 {{0,3},{-20,22},{0,19},{20,22},{-24,44},{-10,41},{10,41},{24,44},{-22,70},{0,77},{22,70}}
};
uint32_t fc_random(uint32_t*s){if(!*s)*s=0x8ac3f921;*s^=*s<<13;*s^=*s>>17;return *s^=*s<<5;}
int fc_distance(int x,int y){x=abs(x);y=abs(y);return FC_MAX(x,y)+FC_MIN(x,y)*3/8;}
static int length(int x,int y){
 uint32_t n=(uint32_t)(x*x+y*y),r=0,b=1u<<30;
 while(b>n)b>>=2;while(b){if(n>=r+b){n-=r+b;r=(r>>1)+b;}else r>>=1;b>>=2;}return (int)r;
}
int fc_attack(const FCMatch*m,int t){return ((t==0)^(m->half!=0))?1:-1;}
static unsigned input(const FCMatch*m){unsigned v=m->input_latch?m->input_latch:m->previous_input;if(!(v&15))v|=m->action_aim&15;if(m->action_aim&FC_CONTROL)v|=FC_CONTROL;return v;}
static void face(FCMatch*m,int i,int x,int y){
 int d=length(x,y);if(d<32)return;
 FCMotion*f=&m->motion[i];int tx=x*Q/d,ty=y*Q/d;
 f->face_x+=(tx-f->face_x)/3;f->face_y+=(ty-f->face_y)/3;
 d=length(f->face_x,f->face_y);if(d>16){f->face_x=f->face_x*Q/d;f->face_y=f->face_y*Q/d;}
 else{f->face_x=(int16_t)tx;f->face_y=(int16_t)ty;}
}
void fc_move_to(FCMatch*m,int i,int x,int y,int speed){
 FCPlayer*p=&m->p[i];int dx=x-p->x,dy=y-p->y,d=length(dx,dy);
 int vx=0,vy=0;if(d>Q/5){speed=FC_MIN(speed,d*3);vx=dx*speed/d;vy=dy*speed/d;}
 /* Friction and acceleration are bounded; turning while sprinting has weight. */
 int accel=90+(int)m->motion[i].skill/2;if(p->cooldown>20)accel/=2;
 if(i<11&&m->ball.receiver==i)accel+=45;
 int ax=vx-p->vx,ay=vy-p->vy,change=length(ax,ay);
 if(change>accel){ax=ax*accel/change;ay=ay*accel/change;}
 p->vx+=(int16_t)ax;p->vy+=(int16_t)ay;
 face(m,i,p->vx,p->vy);
}
static void formation(FCMatch*m,bool fresh){
 for(int t=0;t<2;t++)for(int n=0;n<11;n++){
  int i=t*11+n,f=t?0:m->formation,dir=fc_attack(m,t);FCPlayer*p=&m->p[i];
  if(!fc_active(m,i)){p->vx=p->vy=0;continue;}p->x=shape[f][n][0]*Q;p->y=(dir>0?shape[f][n][1]:FC_LENGTH-shape[f][n][1])*Q;
  if(i==fc_keeper(m,t)){p->x=0;p->y=(int16_t)((dir>0?3:FC_LENGTH-3)*Q);}p->vx=p->vy=0;p->cooldown=0;
  if(fresh)p->stamina=1000;m->motion[i].face_x=0;m->motion[i].face_y=(int16_t)(dir*Q);m->motion[i].touch=0;m->motion[i].action=0;
 }
}
int fc_nearest(const FCMatch*m,int t,int x,int y,bool keeper){
 int best=fc_keeper(m,t),value=100000;
 for(int i=t*11+(keeper?0:1);i<(t+1)*11;i++)if(fc_active(m,i)&&(keeper||i!=fc_keeper(m,t))){
  int d=fc_distance(m->p[i].x-x,m->p[i].y-y)+m->p[i].cooldown*3;
  if(d<value){best=i;value=d;}
 }return best;
}
void fc_restart(FCMatch*m,int phase,int t,int x,int y){
 if(m->mode==FC_FREEPLAY&&phase!=FC_FREE){m->phase=(uint8_t)phase;m->timer=60;m->free_result=FC_PEN_MISS;m->ball.owner=-1;return;}
 m->free_view=phase==FC_FREE;m->free_age=0;m->free_x=(int16_t)x;m->free_y=(int16_t)y;m->free_team=(uint8_t)t;
 m->wall_count=m->wall_clock=0;m->kick_clock=0;m->kick_quality=70;m->kick_height=350;m->curve=0;
 m->phase=(uint8_t)phase;m->set_team=(uint8_t)t;m->timer=phase==FC_KICKOFF?60:phase==FC_FREE&&!t?480:100;m->charge=m->pass_charge=0;m->charge_owner=-1;m->action_aim=0;m->offside=-1;m->spin=0;m->ball_kind=FC_DRIBBLE;m->control_lock=0;
 m->ball=(FCBall){.x=FC_CLAMP(x,-(FC_W/2),(FC_W/2)),.y=FC_CLAMP(y,0,FC_H),.owner=-1,.last=(int8_t)(t*11),.receiver=-1};
 if(phase==FC_KICKOFF){formation(m,false);m->ball.x=0;m->ball.y=FC_H/2;
  for(int j=0;j<22;j++)if(fc_active(m,j)&&j%11>=8){int dir=fc_attack(m,j/11);m->p[j].y=(int16_t)(FC_H/2-dir*(j%11==9?5:7)*Q);}
  int partner=fc_active(m,t*11+7)?t*11+7:fc_nearest(m,t,5*Q,FC_H/2,false);m->p[partner].x=5*Q;m->p[partner].y=(int16_t)(FC_H/2-fc_attack(m,t)*4*Q);
 }
 int i=phase==FC_KICKOFF?t*11+9:phase==FC_GOALKICK?fc_keeper(m,t):fc_nearest(m,t,x,y,false);
 if(!fc_active(m,i))i=fc_nearest(m,t,x,y,false);m->taker=(uint8_t)i;m->p[i].x=(int16_t)m->ball.x;m->p[i].y=(int16_t)m->ball.y;m->p[i].vx=m->p[i].vy=0;
 if(t==0)m->selected=(uint8_t)i;m->aim=0;m->power=500;
 for(int j=0;j<22;j++)if(j!=i&&fc_active(m,j)){
  m->p[j].vx=m->p[j].vy=0;
  int dx=m->p[j].x-m->ball.x,dy=m->p[j].y-m->ball.y,d=fc_distance(dx,dy);
  int space=j/11==t?2*Q:phase==FC_KICKOFF?9*Q:phase==FC_FREE?2343:5*Q;
  if(phase==FC_KICKOFF){int dir=fc_attack(m,j/11);m->p[j].y=(int16_t)(dir>0?FC_MIN(m->p[j].y,FC_H/2-Q):FC_MAX(m->p[j].y,FC_H/2+Q));dx=m->p[j].x-m->ball.x;dy=m->p[j].y-m->ball.y;d=fc_distance(dx,dy);}
  if(d<space){if(!d){dx=(j&1)?Q:-Q;dy=Q/2;d=Q;}m->p[j].x=(int16_t)FC_CLAMP(m->ball.x+dx*space/d,-(FC_W/2-Q),(FC_W/2-Q));m->p[j].y=(int16_t)FC_CLAMP(m->ball.y+dy*space/d,Q,(FC_H-Q));}
 }
 fc_wall_setup(m);if(phase==FC_FREE){int ox,oy,fx,fy;fc_free_basis(m,&ox,&oy,&fx,&fy);m->motion[i].face_x=(int16_t)fx;m->motion[i].face_y=(int16_t)fy;}
}
void fc_new(FCMatch*m,const FCProfile*p,unsigned mode,unsigned opponent,uint32_t seed){
 memset(m,0,sizeof *m);m->keeper_id[1]=11;m->tracked_owner=-1;m->seed=seed;m->teams[0]=mode==FC_CAREER?p->career_team:mode==FC_CUP?p->cup_team:p->team;m->teams[1]=opponent%32;
 m->difficulty=p->difficulty;m->formation=p->formation;m->mode=(uint8_t)mode;m->duration=(2+p->length*2)*60*40;
 for(int t=0;t<2;t++){const FCTeam*a=&fc_teams[m->teams[t]];m->quality[t]=(a->attack+a->midfield+a->defense)/3;
  for(int n=0;n<11;n++)m->motion[t*11+n].skill=(uint8_t)FC_CLAMP(m->quality[t]+n%5-2,55,99);
 }
 if(mode==FC_CAREER){unsigned total=0;for(int i=0;i<11;i++){int v=p->roster[i]-(p->contracts[i]?0:12)+p->coach*2;m->motion[i].skill=(uint8_t)FC_CLAMP(v,55,99);total+=m->motion[i].skill;}m->quality[0]=(uint8_t)(total/11);}
 const FCTeam*op=&fc_teams[m->teams[1]];
 m->tactics[1]=(uint8_t)(op->defense>op->attack?2:op->midfield>op->attack?1:op->attack>op->defense+2?3:0);
 formation(m,true);fc_restart(m,FC_KICKOFF,0,0,FC_H/2);m->selected=9;m->charge_owner=-1;
 if(mode==FC_SHOOTOUT)fc_start_penalties(m);
 if(mode==FC_FREEPLAY){m->duration=UINT32_MAX;fc_free_reset(m);}
}
static int offside_line(const FCMatch*m,int t){
 int dir=fc_attack(m,t),first=-100000,second=-100000;
 for(int j=(1-t)*11;j<(2-t)*11;j++)if(fc_active(m,j)){int v=m->p[j].y*dir;if(v>first){second=first;first=v;}else if(v>second)second=v;}return second;
}
static bool offside_player(const FCMatch*m,int i,int j){if(m->mode==FC_TRAINING)return false;int dir=fc_attack(m,i/11),y=m->p[j].y*dir;return (m->p[j].y-FC_H/2)*dir>0&&y>m->p[i].y*dir+Q/3&&y>offside_line(m,i/11)+Q/3;}
/* Clearance to the segment, not just to its endpoints. Used only for decisions. */
static int lane(const FCMatch*m,int t,int ax,int ay,int bx,int by){
 int dx=(bx-ax)/16,dy=(by-ay)/16,den=dx*dx+dy*dy,clear=12*Q;if(!den)return 0;
 for(int k=(1-t)*11;k<(2-t)*11;k++)if(fc_active(m,k)){
  int ox=(m->p[k].x-ax)/16,oy=(m->p[k].y-ay)/16,dot=ox*dx+oy*dy;if(dot<=0||dot>=den)continue;
  int f=(int)((int64_t)dot*1024/den);int d=fc_distance(ox*16-dx*16*f/1024,oy*16-dy*16*f/1024);clear=FC_MIN(clear,d);
 }return clear;
}
int fc_pass_target(const FCMatch*m,int i,unsigned kind){
 int t=i/11,best=-1,value=-100000,dx=0,dy=0;const FCPlayer*p=&m->p[i];
 if(!t){unsigned in=input(m);dx=((in&FC_RIGHT)!=0)-((in&FC_LEFT)!=0);dy=((in&FC_UP)!=0)-((in&FC_DOWN)!=0);}
 int ax=dx*Q,ay=dy*Q,d=length(ax,ay);bool manual=d!=0;
 if(d){ax=ax*Q/d;ay=ay*Q/d;}else{ax=m->motion[i].face_x;ay=m->motion[i].face_y;if(!ax&&!ay)ay=fc_attack(m,t)*Q;}
 for(int j=t*11;j<(t+1)*11;j++)if(j!=i&&fc_active(m,j)){
  int x=m->p[j].x-p->x,y=m->p[j].y-p->y,dist=length(x,y);if(dist<2*Q||dist>(kind==FC_HIGH_PASS?55:kind==FC_RUN_PASS?42:38)*Q)continue;
  int alignment=(ax*x+ay*y)/FC_MAX(dist,1);if(manual&&alignment<70)continue;
  int clear=lane(m,t,p->x,p->y,m->p[j].x,m->p[j].y);
  int score=alignment*12-dist/5+FC_MIN(clear,5*Q)*(kind==FC_HIGH_PASS?1:2);
  int desired=(8+(m->pass_charge?m->pass_charge:320)*28/1000)*Q;score-=abs(dist-desired)/3;
  if(!manual)score+=y*fc_attack(m,t)/5;
  if(kind==FC_RUN_PASS){score+=y*fc_attack(m,t)/3;if(j%11<5)score-=Q*4;}
  if(offside_player(m,i,j))score-=12*Q;
  if(score>value){best=j;value=score;}
 }return best;
}
static void launch(FCMatch*m,int i,int tx,int ty,int speed,int loft){
 FCBall*b=&m->ball;FCPlayer*p=&m->p[i];int dx=tx-b->x,dy=ty-b->y;
 if(fc_distance(b->x-p->x,b->y-p->y)>4*Q){b->x=p->x;b->y=p->y;dx=tx-p->x;dy=ty-p->y;}
 int d=FC_MAX(length(dx,dy),Q/4);b->z=FC_MAX(b->z,Q/5);b->vx=dx*speed/d;b->vy=dy*speed/d;b->vz=loft;b->owner=-1;b->last=(int8_t)i;b->grace=3;
 p->cooldown=12;m->motion[i].action=12;m->pass_cool=14;m->offside=-1;m->spin=0;m->charge=m->pass_charge=0;m->charge_owner=-1;m->action_aim=0;
 m->keeper_wait[0]=(uint8_t)(10+2-m->difficulty*2);m->keeper_wait[1]=(uint8_t)(18-m->difficulty*4);
}
void fc_pass_type(FCMatch*m,int i,unsigned kind,unsigned power){
 power=FC_CLAMP(power,220u,1000u);m->pass_charge=(uint16_t)power;
 int t=i/11,best=fc_pass_target(m,i,kind);FCPlayer*p=&m->p[i];int tx,ty;
 int speed=(kind==FC_HIGH_PASS?15:kind==FC_RUN_PASS?18:14)*Q+(int)power*Q/85;
 if(best<0){int ax=m->motion[i].face_x,ay=m->motion[i].face_y;if(!t){unsigned in=input(m);int dx=!!(in&FC_RIGHT)-!!(in&FC_LEFT),dy=!!(in&FC_UP)-!!(in&FC_DOWN);if(dx||dy){int d=length(dx*Q,dy*Q);ax=dx*Q*Q/d;ay=dy*Q*Q/d;}}
  tx=p->x+ax*(12+power/80);ty=p->y+ay*(12+power/80);
 }else{
  int distance=length(m->p[best].x-p->x,m->p[best].y-p->y),time=FC_CLAMP(distance*Q/speed,40,360);
  tx=m->p[best].x+m->p[best].vx*time/Q;ty=m->p[best].y+m->p[best].vy*time/Q;
  if(kind==FC_RUN_PASS){int dir=fc_attack(m,t);ty+=dir*(4+(int)power/140)*Q;tx+=m->p[best].vx/3;}
 }
 tx=FC_CLAMP(tx,-(FC_W/2-2*Q),(FC_W/2-2*Q));ty=FC_CLAMP(ty,2*Q,(FC_H-2*Q));
 if(t){int near=20*Q;for(int j=0;j<11;j++)if(fc_active(m,j))near=FC_MIN(near,fc_distance(m->p[j].x-p->x,m->p[j].y-p->y));
  int error=50+(100-m->motion[i].skill)*4+(near<3*Q?150:0)+(1000-p->stamina)/5;
  tx+=(int)(fc_random(&m->seed)%(error*2+1))-error;ty+=(int)(fc_random(&m->seed)%(error*2+1))-error;
 }
 int dist=length(tx-p->x,ty-p->y),loft=kind==FC_HIGH_PASS?FC_CLAMP(dist*5*Q/speed,3*Q,9*Q):0;
 launch(m,i,tx,ty,speed,loft);m->ball_kind=(uint8_t)kind;m->ball.receiver=(int8_t)best;m->receive_x=(int16_t)tx;m->receive_y=(int16_t)ty;m->passes[t]=FC_MIN(65535,m->passes[t]+1);
 if(best>=0){if(offside_player(m,i,best))m->offside=(int8_t)best;if(!t){m->selected=(uint8_t)best;m->control_lock=8;}}
}
void fc_pass(FCMatch*m,int i){fc_pass_type(m,i,FC_GROUND,320);}
void fc_receive_target(const FCMatch*m,int i,int*x,int*y){
 /* Search the actual decelerating flight for a reachable interception. The
  * old fixed destination could leave a receiver waiting behind a slowed pass
  * or running under a lob before it was low enough to control. */
 const FCBall*b=&m->ball;const FCPlayer*p=&m->p[i];int bx=b->x,by=b->y,bz=b->z,vx=b->vx,vy=b->vy,vz=b->vz,best=100000;
 *x=b->x;*y=b->y;
 for(int tick=4;tick<=80;tick+=4){
  bx+=vx/10;by+=vy/10;bz+=vz/10;vz-=Q;
  if(bz<0){bz=0;vz=vz<-3*Q?-vz/3:0;}
  int drag=bz?996:961;vx=vx*drag/1000;vy=vy*drag/1000;
  if(abs(bx)>FC_W/2||by<0||by>FC_H)break;if(bz>Q)continue;
  int cost=fc_distance(bx-p->x,by-p->y)-6*Q*tick/40;
  if(cost<best){best=cost;*x=bx;*y=by;}if(cost<Q/2)break;
 }
}
void fc_shot(FCMatch*m,int i,unsigned power){
 int t=i/11,dir=fc_attack(m,t);FCPlayer*p=&m->p[i];bool finesse=!t&&(input(m)&FC_CONTROL);
 int pressure=0;for(int j=(1-t)*11;j<(2-t)*11;j++)if(fc_active(m,j)&&fc_distance(m->p[j].x-p->x,m->p[j].y-p->y)<3*Q)pressure++;
 int spread=FC_MAX(24,200-m->motion[i].skill)+pressure*45+(fc_distance(p->vx,p->vy)>6*Q?70:0)+(power>900?160:0);
 int aim=(int)(fc_random(&m->seed)%(spread*2+1))-spread;
 if(!t){unsigned in=input(m);if(in&FC_LEFT)aim-=700;if(in&FC_RIGHT)aim+=700;
  if(!(in&(FC_LEFT|FC_RIGHT)))aim+=FC_CLAMP(p->x/9,-430,430);
 }else aim+=(fc_random(&m->seed)&1?1:-1)*(450+(int)(fc_random(&m->seed)%300));
 if(m->phase==FC_FREE||m->phase==FC_PENALTY)aim=m->aim;
 int speed=(finesse?18:21)*Q+(int)power*Q/(finesse?100:60);
 int loft=power>900?5*Q:finesse?2*Q:Q/2+(int)power/3;
 launch(m,i,aim,dir>0?FC_H:0,speed,loft);m->ball_kind=finesse?FC_FINESSE:FC_DRIVE;m->ball.receiver=-1;
 if(finesse)m->spin=(int16_t)((aim-p->x/3)>0?18:-18);
 m->shots[t]=(uint8_t)FC_MIN(99,m->shots[t]+1);
}
void fc_possess(FCMatch*m,int i){
 FCBall*b=&m->ball;int old=b->owner,team=i/11;bool pass=b->receiver>=0&&b->last>=0&&b->last!=i&&b->last/11==team&&m->ball_kind>=FC_GROUND&&m->ball_kind<=FC_HIGH_PASS;
 if(pass)m->completed[team]=FC_MIN(65535,m->completed[team]+1);
 if(pass&&m->mode==FC_TRAINING&&m->drill==1&&i==m->drill_target){m->drill_hits++;m->drill_target=(uint8_t)(i==7?5:i==5?9:7);}
 b->owner=(int8_t)i;b->last=(int8_t)i;b->receiver=-1;b->grace=5;b->z=0;b->vz=0;m->offside=-1;m->ball_kind=FC_DRIBBLE;m->spin=0;m->control_lock=pass&&!team?10:0;
 m->motion[i].touch=0;m->motion[i].action=6;m->motion[i].think=6;
 if(!team)m->selected=(uint8_t)i;
 else if(old>=0&&old<11)m->selected=(uint8_t)fc_nearest(m,0,b->x,b->y,false);
 if(m->charge_owner!=i){m->charge=m->pass_charge=0;m->charge_owner=-1;}
}
void fc_set_target(const FCMatch*m,int*x,int*y){
 int dir=fc_attack(m,m->set_team);*x=m->aim;*y=dir>0?FC_H:0;
 if(m->phase==FC_THROW){*x=m->ball.x/3+m->aim*3;*y=m->ball.y+dir*(6+m->power/100)*Q;}
 else if(m->phase==FC_CORNER){*x=m->aim;*y=(dir>0?FC_LENGTH-11:11)*Q;}
 else if(m->phase==FC_GOALKICK){*x=m->aim*4;*y=m->ball.y+dir*(15+m->power/40)*Q;}
 *x=FC_CLAMP(*x,-(FC_W/2-2*Q),(FC_W/2-2*Q));*y=FC_CLAMP(*y,Q,FC_H-Q);
}
void fc_tackle(FCMatch*m,int i){
 FCPlayer*p=&m->p[i];if(p->cooldown||!fc_active(m,i))return;p->cooldown=30;m->motion[i].action=18;
 int j=m->ball.owner;if(j<0||j/11==i/11)return;
 if(j<11&&m->control_lock&&m->ball.grace)return;
 int ball=fc_distance(p->x-m->ball.x,p->y-m->ball.y),body=fc_distance(p->x-m->p[j].x,p->y-m->p[j].y);
 if(body>2*Q||ball>2*Q)return;
 int side=(p->x-m->p[j].x)*m->motion[j].face_x+(p->y-m->p[j].y)*m->motion[j].face_y;
 bool behind=side<-Q*Q/3;int impact=fc_distance(p->vx-m->p[j].vx,p->vy-m->p[j].vy);
 if(body<Q+Q/3&&((behind&&ball>Q/2)||(ball>Q&&impact>6*Q))){
  unsigned severity=impact>8*Q?2:impact>4*Q||(behind&&impact>2*Q)?1:0;
  fc_foul(m,i,j,severity);
 }else if(ball<Q+Q/3){
  bool shielding=j<11&&(input(m)&FC_CONTROL);if(shielding&&behind)return;
  m->p[j].cooldown=14;p->cooldown=8;fc_possess(m,i);
 }
}
static void goal(FCMatch*m,int t){m->score[t]=(uint8_t)FC_MIN(99,m->score[t]+1);m->phase=FC_GOAL;m->timer=95;m->set_team=(uint8_t)(1-t);m->ball.owner=-1;m->charge=m->pass_charge=0;if(m->mode==FC_FREEPLAY){m->free_goals=FC_MIN(9999,m->free_goals+1);m->free_result=FC_PEN_GOAL;m->timer=60;}}
static void dribble(FCMatch*m){
 FCBall*b=&m->ball;int i=b->owner;if(i<0)return;FCPlayer*p=&m->p[i];FCMotion*f=&m->motion[i];int d=fc_distance(b->x-p->x,b->y-p->y),speed=length(p->vx,p->vy);
 if(d>3*Q){b->owner=-1;b->last=(int8_t)i;return;}
 if(!f->face_x&&!f->face_y)f->face_y=(int16_t)(fc_attack(m,i/11)*Q);
 bool sprint=speed>6*Q,shield=i<11&&(input(m)&FC_CONTROL);
 /* A change of direction needs a new touch; waiting for the old sprint
  * interval made the ball continue straight while its carrier turned. */
 if(speed>Q&&(p->vx*b->vx+p->vy*b->vy<0||abs(p->vx-b->vx)+abs(p->vy-b->vy)>7*Q))f->touch=0;
 if(!f->touch){
  int reach=shield?100:sprint?300:150;
  int tx=p->x+f->face_x*reach/Q,ty=p->y+f->face_y*reach/Q;
  b->vx=p->vx+(tx-b->x)*7;b->vy=p->vy+(ty-b->y)*7;int v=length(b->vx,b->vy);if(v>11*Q){b->vx=b->vx*11*Q/v;b->vy=b->vy*11*Q/v;}
  f->touch=(uint8_t)(shield?3:sprint?9:5);
 }
 if(m->ticks%8==0){m->possession[i/11]=(uint8_t)FC_MIN(250,m->possession[i/11]+1);if(m->possession[0]+m->possession[1]>240){m->possession[0]/=2;m->possession[1]/=2;}}
}
static void ball_step(FCMatch*m){
 FCBall*b=&m->ball;if(b->grace)b->grace--;dribble(m);
 int oldx=b->x,oldy=b->y,oldz=b->z;
 b->x+=b->vx/40;b->y+=b->vy/40;b->z+=b->vz/40;
 if(m->spin){int vx=b->vx;b->vx+=b->vy*m->spin/24000;b->vy-=vx*m->spin/24000;if(m->ticks%4==0)m->spin-=m->spin>0?1:-1;}
 if(b->owner<0){b->vz-=64;if(b->z<0){b->z=0;b->vz=b->vz<-3*Q?-b->vz/3:0;}int drag=b->z?999:990;b->vx=b->vx*drag/1000;b->vy=b->vy*drag/1000;}
 else{b->z=0;b->vz=0;}
 /* A goalkeeper can collect an exposed dribble at his feet as well as a
  * free shot. Previously an owned ball could run straight through him. */
 if(b->owner>=0){int i=fc_keeper(m,1-b->owner/11),dir=fc_attack(m,i/11),own=dir>0?0:FC_H;
  if(fc_active(m,i)&&!m->p[i].cooldown&&abs(m->p[i].x)<20*Q&&abs(m->p[i].y-own)<16*Q&&fc_distance(m->p[i].x-b->x,m->p[i].y-b->y)<180){
   int carrier=b->owner;m->p[carrier].cooldown=14;fc_possess(m,i);b->x=m->p[i].x;b->y=m->p[i].y;b->vx=b->vy=0;
  }
 }
 /* Resolve swept player contact before the goal plane. An airborne player
  * who cannot reach the ball must not hide a reachable goalkeeper behind him. */
 if(b->owner<0&&!b->grace){
  int best=-1,first=257,dist=100000;
  int vx=b->x-oldx,vy=b->y-oldy,den=vx*vx+vy*vy;
  for(int i=0;i<22;i++)if(fc_active(m,i)&&!m->p[i].cooldown){
   int dir=fc_attack(m,i/11),own=dir>0?0:FC_H;
   bool hands=i==fc_keeper(m,i/11)&&abs(m->p[i].x)<20*Q&&abs(m->p[i].y-own)<16*Q;
   bool receiver=i<11&&i==b->receiver&&m->ball_kind>=FC_GROUND&&m->ball_kind<=FC_HIGH_PASS;
   int radius=hands?(m->keeper_dive[i/11]>6?(i>=11?245:290):(i>=11?180:210)):receiver?340:230;
   int f=den?FC_CLAMP((int)((int64_t)((m->p[i].x-oldx)*vx+(m->p[i].y-oldy)*vy)*256/den),0,256):256;
   int cx=oldx+vx*f/256,cy=oldy+vy*f/256,cz=oldz+(b->z-oldz)*f/256;
   int reach=hands?2*Q:receiver?7*Q/4:Q+Q/3;for(int w=0;w<m->wall_count;w++)if(m->wall[w]==i)reach=7*Q/4+fc_wall_lift(m,i);if(cz>=reach)continue;
   int d=fc_distance(m->p[i].x-cx,m->p[i].y-cy);
   if(d<radius&&(f<first||(f==first&&d<dist))){best=i;first=f;dist=d;}
  }
  if(best>=0){
   b->x=oldx+vx*first/256;b->y=oldy+vy*first/256;b->z=FC_MAX(0,oldz+(b->z-oldz)*first/256);
   if(m->offside==best){fc_restart(m,FC_FREE,1-best/11,b->x,b->y);m->notice=2;return;}
   int speed=length(b->vx,b->vy),dir=fc_attack(m,best/11),own=dir>0?0:FC_H;
   bool keeper=best==fc_keeper(m,best/11)&&abs(m->p[best].x)<20*Q&&abs(m->p[best].y-own)<16*Q;
   if(keeper){
    if(speed>(best>=11?18:21)*Q){int side=b->x>=0?1:-1;b->vx=side*(7*Q+abs(b->vx)/4);b->vy=-b->vy/4;b->vz=3*Q;b->owner=-1;b->last=(int8_t)best;b->receiver=-1;b->grace=9;m->control_lock=0;m->p[best].cooldown=16;m->motion[best].action=14;m->offside=-1;}
    else{fc_possess(m,best);b->x=m->p[best].x;b->y=m->p[best].y;}
   }else if((speed>22*Q||m->wall_clock)&&m->ball_kind>=FC_DRIVE){b->vx=-b->vx/3+(best&1?3*Q:-3*Q);b->vy=-b->vy/3;b->vz=Q;b->last=(int8_t)best;b->receiver=-1;b->grace=5;m->control_lock=0;m->p[best].cooldown=14;m->offside=-1;}
   else{fc_possess(m,best);b->vx=m->p[best].vx;b->vy=m->p[best].vy;m->motion[best].touch=0;}
  }
 }
 if((oldy>=0&&b->y<0)||(oldy<=FC_H&&b->y>FC_H)){
  int gy=b->y<0?0:FC_H,den=b->y-oldy,gx=oldx+(b->x-oldx)*(gy-oldy)/den,gz=oldz+(b->z-oldz)*(gy-oldy)/den;
  if((abs(abs(gx)-FC_GOAL_HALF)<64&&gz<FC_GOAL_HEIGHT+65)||(abs(gx)<FC_GOAL_HALF&&abs(gz-FC_GOAL_HEIGHT)<55)){
   b->y=FC_CLAMP(b->y,32,FC_H-32);b->vy=-b->vy*3/4;b->owner=-1;return;
  }
  if(abs(gx)<FC_GOAL_HALF&&gz<FC_GOAL_HEIGHT){goal(m,fc_attack(m,0)==(gy?1:-1)?0:1);return;}
 }
 if(abs(b->x)>(FC_W/2)){fc_restart(m,FC_THROW,1-b->last/11,b->x,b->y);return;}
 if(b->y<0||b->y>FC_H){int dir=b->y>FC_H?1:-1,att=fc_attack(m,0)==dir?0:1,def=1-att;
  if(b->last/11==def){m->corners[att]=(uint8_t)FC_MIN(99,m->corners[att]+1);fc_restart(m,FC_CORNER,att,b->x<0?-(FC_W/2-Q):(FC_W/2-Q),dir>0?(FC_H-Q):Q);}
  else fc_restart(m,FC_GOALKICK,def,0,(dir>0?FC_LENGTH-6:6)*Q);
 }
}
static void human(FCMatch*m,unsigned in,unsigned hit){
 /* Possession always controls the carrier. A switch must not move one player
  * while charging or kicking remotely with a different teammate. */
 if(m->ball.owner>=0&&m->ball.owner<11)m->selected=(uint8_t)m->ball.owner;
 int i=m->selected;FCPlayer*p=&m->p[i];int dx=!!(in&FC_RIGHT)-!!(in&FC_LEFT),dy=!!(in&FC_UP)-!!(in&FC_DOWN);
 bool sprint=(in&FC_SPRINT)&&p->stamina>60&&(dx||dy)&&!(in&FC_CONTROL);
 int speed=(sprint?8:5)*Q+((int)m->motion[i].skill-80)*4;
 if(in&FC_CONTROL){speed=3*Q;if(m->ball.owner!=i)face(m,i,m->ball.x-p->x,m->ball.y-p->y);}
 if(sprint)p->stamina-=FC_MIN(p->stamina,4);else p->stamina=FC_MIN(1000,p->stamina+2);
 if(m->ball.receiver==i&&m->ball.owner<0&&!(in&(FC_CONTROL|FC_SPRINT))){
  int tx,ty;fc_receive_target(m,i,&tx,&ty);
  tx+=dx*Q*3/4;ty+=dy*Q*3/4;
  fc_move_to(m,i,tx,ty,6*Q);
 }
 else fc_move_to(m,i,p->x+dx*8*Q,p->y+dy*8*Q,speed);
 int owner=m->ball.owner;
 if(owner<0||owner>=11){if(owner>=11&&(hit&FC_PASS))fc_tackle(m,i);m->charge=m->pass_charge=0;m->charge_owner=-1;m->action_aim=0;return;}
 if(m->charge_owner!=owner){m->charge=m->pass_charge=0;m->charge_owner=(int8_t)owner;}
 unsigned passes=in&(FC_PASS|FC_THROUGH|FC_LOB);
 if((passes||(in&FC_SHOOT))&&(in&15))m->action_aim=(uint16_t)(in&(15|FC_CONTROL));
 if((in&FC_CONTROL)&&(passes||(in&FC_SHOOT)))m->action_aim|=FC_CONTROL;
 if(in&FC_SHOOT){m->charge=FC_MIN(1000,m->charge+25);m->pass_charge=0;}
 else if(m->charge){fc_shot(m,owner,FC_MAX(250,m->charge));return;}
 else if(passes){m->pass_charge=FC_MIN(1000,m->pass_charge+27);m->pass_kind=(uint8_t)(passes&FC_LOB?FC_HIGH_PASS:passes&FC_THROUGH?FC_RUN_PASS:FC_GROUND);}
 else if(m->pass_charge){fc_pass_type(m,owner,m->pass_kind,FC_MAX(220,m->pass_charge));}
}
void fc_step(FCMatch*m,unsigned in){
 if(m->shoot_state){fc_penalty_step(m,in);return;}if(m->phase==FC_FULL)return;
 if(m->mode==FC_FREEPLAY&&m->phase!=FC_FREE&&m->phase!=FC_LIVE){
  unsigned hit=in&~m->input_latch;m->input_latch=(uint16_t)in;m->ticks++;if(m->timer)m->timer--;
  if(!m->timer||(hit&FC_PASS)){fc_free_reset(m);m->input_latch=(uint16_t)in;}return;
 }
 if(m->mode==FC_TRAINING&&m->phase!=FC_LIVE){
  m->ticks++;if(++m->clock>=m->duration){m->phase=FC_FULL;return;}
  if(m->timer)m->timer--;else fc_training_reset(m);return;
 }
 unsigned hit=in&~m->input_latch;m->ticks++;m->input_latch=(uint16_t)in;m->previous_input=(uint8_t)in;
 if(m->mode==FC_FREEPLAY&&(hit&FC_TACTIC)){fc_free_reset(m);m->input_latch=(uint16_t)in;return;}
 if(m->free_view==1&&m->phase==FC_LIVE&&++m->free_age>160&&m->mode!=FC_FREEPLAY)m->free_view=2;
 if(m->tactic_notice)m->tactic_notice--;if(m->card_timer)m->card_timer--;if(m->feint_cool)m->feint_cool--;if(m->wall_clock&&++m->wall_clock>40){m->wall_clock=m->wall_count=0;}
 for(int i=0;i<22;i++){if(m->ai_delay[i])m->ai_delay[i]--;if(m->ai_stumble[i])m->ai_stumble[i]--;}
 if((hit&FC_TACTIC)&&m->mode!=FC_TRAINING){m->tactics[0]=(m->tactics[0]+1)%4;m->tactic_notice=100;}
 if(m->pass_cool)m->pass_cool--;if(m->control_lock)m->control_lock--;
 for(int i=0;i<22;i++){if(m->motion[i].touch)m->motion[i].touch--;if(m->motion[i].action)m->motion[i].action--;if(m->motion[i].think)m->motion[i].think--;}
 for(int t=0;t<2;t++){if(m->keeper_wait[t])m->keeper_wait[t]--;if(m->keeper_dive[t])m->keeper_dive[t]--;}
 if(m->phase==FC_GOAL){if(m->timer)m->timer--;else fc_restart(m,FC_KICKOFF,m->set_team,0,FC_H/2);return;}
 if(m->phase==FC_HALF){if(m->timer)m->timer--;else{m->half=1;for(int i=0;i<22;i++)m->p[i].stamina=FC_MIN(1000,m->p[i].stamina+250);fc_restart(m,FC_KICKOFF,1,0,FC_H/2);}return;}
 if(m->phase==FC_FREE){fc_free_step(m,in,hit);if(m->phase!=FC_LIVE)return;}
 if(m->phase!=FC_LIVE){
  if(m->timer)m->timer--;if(m->set_team==0){m->aim=FC_CLAMP(m->aim+(!!(in&FC_RIGHT)-!!(in&FC_LEFT))*12,-4*Q,4*Q);m->power=FC_CLAMP((int)m->power+(!!(in&FC_UP)-!!(in&FC_DOWN))*10,250,1000);}
  if(m->timer&&!(m->set_team==0&&(hit&(FC_PASS|FC_SHOOT))))return;
  int phase=m->phase;m->ball.owner=(int8_t)m->taker;
  if(phase==FC_PENALTY||(phase==FC_FREE&&((hit&FC_SHOOT)||(m->set_team&&abs(m->ball.y-(fc_attack(m,m->set_team)>0?FC_H:0))<30*Q))))fc_shot(m,m->taker,m->power);
  else if(phase==FC_FREE)fc_pass_type(m,m->taker,FC_GROUND,m->power);
  else if(phase==FC_KICKOFF){
   /* A short legal layoff is reliable even if the direction key is already
    * held when starting play; it must not turn into a blind forward punt. */
   int partner=m->set_team*11+7;if(!fc_active(m,partner)||partner==m->taker){partner=fc_nearest(m,m->set_team,5*Q,FC_H/2,false);if(partner==m->taker)for(int j=m->set_team*11+1;j<(m->set_team+1)*11;j++)if(fc_active(m,j)&&j!=m->taker){partner=j;break;}}launch(m,m->taker,m->p[partner].x,m->p[partner].y,14*Q,0);
   m->ball_kind=FC_GROUND;m->ball.receiver=(int8_t)partner;m->receive_x=m->p[partner].x;m->receive_y=m->p[partner].y;
   m->passes[m->set_team]++;if(!m->set_team){m->selected=(uint8_t)partner;m->control_lock=8;}
  }
  else{int x,y;fc_set_target(m,&x,&y);launch(m,m->taker,x,y,(14+m->power/70)*Q,phase==FC_CORNER?5*Q:phase==FC_GOALKICK?6*Q:2*Q);m->ball.receiver=-1;m->ball_kind=FC_HIGH_PASS;}
  m->phase=FC_LIVE;m->notice=0;
 }
 if(++m->clock>=m->duration&&m->mode!=FC_FREEPLAY){m->phase=FC_FULL;m->charge=m->pass_charge=0;return;}
 if(m->mode!=FC_TRAINING&&m->mode!=FC_FREEPLAY&&!m->half&&m->clock>=m->duration/2){m->phase=FC_HALF;m->timer=80;m->charge=m->pass_charge=0;return;}
 if((hit&FC_SWITCH)&&m->mode!=FC_TRAINING&&!m->control_lock){int best=m->ball.owner>=0&&m->ball.owner<11?m->ball.owner:fc_nearest(m,0,m->ball.x+m->ball.vx/3,m->ball.y+m->ball.vy/3,false);
  if(best==m->selected&&m->ball.owner>=11){int second=-1,dist=100000;for(int j=1;j<11;j++)if(j!=best&&fc_active(m,j)){int d=fc_distance(m->p[j].x-m->ball.x,m->p[j].y-m->ball.y);if(d<dist){dist=d;second=j;}}if(second>=0)best=second;}
  m->selected=(uint8_t)best;m->control_lock=0;
 }
 if(m->mode!=FC_FREEPLAY)human(m,in,hit);if(m->phase!=FC_LIVE)return;fc_ai(m,in);if(m->phase!=FC_LIVE)return;
 for(int i=0;i<22;i++)if(fc_active(m,i)){
  FCPlayer*p=&m->p[i];if(p->cooldown)p->cooldown--;p->x=(int16_t)FC_CLAMP(p->x+p->vx/40,-(FC_W/2)+20,(FC_W/2)-20);p->y=(int16_t)FC_CLAMP(p->y+p->vy/40,20,FC_H-20);
  p->pose=(uint8_t)((p->pose+(fc_distance(p->vx,p->vy)>Q?1:0))%24);if(i!=m->selected){bool effort=m->tactics[i/11]==3&&fc_distance(p->vx,p->vy)>5*Q;
   if(effort)p->stamina-=FC_MIN(p->stamina,1);else p->stamina=FC_MIN(1000,p->stamina+1);}
 }
 for(int i=0;i<22;i++)for(int j=i+1;j<22;j++)if(fc_active(m,i)&&fc_active(m,j)){
  int x=m->p[i].x-m->p[j].x,y=m->p[i].y-m->p[j].y,d=fc_distance(x,y);
  if(d<150){
   int owner=m->ball.owner,other=owner==i?j:owner==j?i:-1;
   if(other>=0&&i/11!=j/11&&other!=fc_keeper(m,other/11)){
    int rear=(m->p[other].x-m->p[owner].x)*m->motion[owner].face_x+(m->p[other].y-m->p[owner].y)*m->motion[owner].face_y;
    int impact=fc_distance(m->p[other].vx-m->p[owner].vx,m->p[other].vy-m->p[owner].vy);
    if(rear<-Q*Q/3&&impact>7*Q&&!m->p[other].cooldown){fc_foul(m,other,owner,impact>9*Q?2:1);return;}
   }if(!d){x=i&1?100:-100;y=80;d=128;}int push=(150-d)/2;
   m->p[i].x=(int16_t)FC_CLAMP(m->p[i].x+x*push/d,-(FC_W/2)+20,(FC_W/2)-20);m->p[j].x=(int16_t)FC_CLAMP(m->p[j].x-x*push/d,-(FC_W/2)+20,(FC_W/2)-20);
   m->p[i].y=(int16_t)FC_CLAMP(m->p[i].y+y*push/d,20,FC_H-20);m->p[j].y=(int16_t)FC_CLAMP(m->p[j].y-y*push/d,20,FC_H-20);
  }
 }
 if(m->phase==FC_LIVE)ball_step(m);
 if(m->mode==FC_FREEPLAY)fc_free_update(m);
 else if(m->free_view==1&&m->phase==FC_LIVE&&m->free_age>20&&m->ball.owner>=0)m->free_view=2;
 if(m->mode==FC_TRAINING)fc_training_update(m);
}
void fc_interpolate(FCMatch*out,const FCMatch*old,const FCMatch*now,unsigned alpha){
 *out=*now;alpha=FC_MIN(alpha,1000u);if(old->phase!=now->phase||old->half!=now->half||old->shoot_state||now->clock-old->clock>1)return;
 for(int i=0;i<22;i++){out->p[i].x=(int16_t)(old->p[i].x+(now->p[i].x-old->p[i].x)*(int)alpha/1000);out->p[i].y=(int16_t)(old->p[i].y+(now->p[i].y-old->p[i].y)*(int)alpha/1000);}
 if(old->ball.owner==now->ball.owner&&fc_distance(now->ball.x-old->ball.x,now->ball.y-old->ball.y)<4*Q){out->ball.x=old->ball.x+(now->ball.x-old->ball.x)*(int)alpha/1000;out->ball.y=old->ball.y+(now->ball.y-old->ball.y)*(int)alpha/1000;out->ball.z=old->ball.z+(now->ball.z-old->ball.z)*(int)alpha/1000;}
}
