#include "football.h"
#include <stdlib.h>
#define Q FC_Q
const char*fc_tactic_name(unsigned n){static const char*const names[]={"Balanced","Possession","Counter attack","High press"};return names[n%4];}
static const int8_t home[3][11][2]={
 {{0,3},{-23,23},{-8,20},{8,20},{23,23},{-16,45},{0,40},{16,45},{-22,70},{0,77},{22,70}},
 {{0,3},{-23,23},{-8,20},{8,20},{23,23},{-23,48},{-8,44},{8,44},{23,48},{-11,76},{11,76}},
 {{0,3},{-20,22},{0,19},{20,22},{-24,44},{-10,41},{10,41},{24,44},{-22,70},{0,77},{22,70}}
};
static int pressure(const FCMatch*m,int t,int x,int y){int near=30*Q;for(int j=(1-t)*11;j<(2-t)*11;j++)if(fc_active(m,j))near=FC_MIN(near,fc_distance(m->p[j].x-x,m->p[j].y-y));return near;}
static int pressing_player(const FCMatch*m,int t,bool extra){
 int best=-1,score=100000;for(int i=t*11+1;i<(t+1)*11;i++){
  if(!fc_active(m,i)||i==fc_keeper(m,t)||(!t&&extra&&i==m->selected))continue;
  int value=fc_distance(m->p[i].x-m->ball.x-m->ball.vx/4,m->p[i].y-m->ball.y-m->ball.vy/4)+m->p[i].cooldown*8;
  if(value<score){score=value;best=i;}
 }return best;
}
static int line(const FCMatch*m,int t){int first=-100000,second=first,dir=fc_attack(m,t);for(int j=(1-t)*11;j<(2-t)*11;j++)if(fc_active(m,j)){int v=m->p[j].y*dir;if(v>first){second=first;first=v;}else if(v>second)second=v;}return FC_MAX(second,m->ball.y*dir)-Q;}
static void feints(FCMatch*m,unsigned in){
 int owner=m->ball.owner,dx=!!(in&FC_RIGHT)-!!(in&FC_LEFT),dy=!!(in&FC_UP)-!!(in&FC_DOWN);
 if(owner<0||owner>=11){m->tracked_owner=-1;return;}
 if(owner==m->tracked_owner&&(dx||dy)&&(m->feint_x||m->feint_y)&&dx*m->feint_x+dy*m->feint_y<=0&&!m->feint_cool&&fc_distance(m->p[owner].vx,m->p[owner].vy)>2*Q){
  for(int i=11;i<22;i++)if(fc_active(m,i)&&i!=fc_keeper(m,1)&&!m->ai_stumble[i]&&fc_distance(m->p[i].x-m->p[owner].x,m->p[i].y-m->p[owner].y)<5*Q){
   int chance=FC_CLAMP(72-(int)m->difficulty*12+(85-m->motion[i].skill)/3+(int)(1000-m->p[i].stamina)/80,30,85);
   if(fc_random(&m->seed)%100<(unsigned)chance){m->ai_stumble[i]=(uint8_t)(13+fc_random(&m->seed)%10);m->ai_delay[i]=m->ai_stumble[i];m->motion[i].think=m->ai_stumble[i]+6;
    m->ai_x[i]=(int16_t)FC_CLAMP(m->p[i].x+m->p[i].vx/2,-FC_W/2+Q,FC_W/2-Q);m->ai_y[i]=(int16_t)FC_CLAMP(m->p[i].y+m->p[i].vy/2,Q,FC_H-Q);
   }
  }m->feint_cool=36;
 }
 m->tracked_owner=(int8_t)owner;if(dx||dy){m->feint_x=(int8_t)dx;m->feint_y=(int8_t)dy;}
}
static void keeper(FCMatch*m,int i){
 int t=i/11,dir=fc_attack(m,t),own=dir>0?0:FC_H;FCPlayer*p=&m->p[i];FCBall*b=&m->ball;
 int x=FC_CLAMP(b->x/3,-3*Q,3*Q),y=own+dir*3*Q,speed=(t?3*Q+m->difficulty*Q/4:4*Q);
 if(b->owner==i){
  if(m->mode==FC_TRAINING)return;
  if(!m->pass_cool&&!m->motion[i].think){fc_pass_type(m,i,pressure(m,t,p->x,p->y)<12*Q?FC_HIGH_PASS:FC_GROUND,600);m->motion[i].think=25;}
 }else if(b->owner<0&&b->vy*dir<0&&m->ball_kind>=FC_DRIVE){
  if(!m->keeper_wait[t]){
   int eta=abs(y-b->y)*40/FC_MAX(1,abs(b->vy));eta=FC_CLAMP(eta,0,60);
   x=FC_CLAMP(b->x+b->vx*eta/40,-(4*Q),4*Q);
   if(eta<22&&abs(x-p->x)>Q/2&&!m->keeper_dive[t]){m->keeper_dive[t]=16;m->motion[i].action=16;}
   if(m->keeper_dive[t]>6)speed=t?(6*Q+m->difficulty*Q/2):8*Q;
   else if(m->keeper_dive[t])speed=2*Q;
  }
 }else if(b->owner<0&&abs(b->y-own)<12*Q&&abs(b->x)<9*Q&&b->z<2*Q){x=b->x;y=FC_CLAMP(b->y,2*Q,(FC_H-2*Q));speed=5*Q;}
 else if(b->owner>=0&&b->owner/11!=t&&abs(b->y-own)<19*Q){y=own+dir*FC_CLAMP(abs(b->y-own)/3,3*Q,7*Q);x=FC_CLAMP(b->x/2,-4*Q,4*Q);}
 fc_move_to(m,i,x,y,speed);
}
void fc_ai(FCMatch*m,unsigned input){
 if(m->mode==FC_FREEPLAY){keeper(m,11);return;}
 if(m->mode==FC_TRAINING){if(m->drill==0)keeper(m,11);for(int i=1;i<11;i++)if(fc_active(m,i)&&i!=m->selected){
   if(m->drill==1)fc_move_to(m,i,(i==7?-12:i==5?14:0)*Q,(i==7?62:i==5?65:49)*Q,3*Q);
   else fc_move_to(m,i,m->p[i].x,m->p[i].y,0);
  }return;}
 feints(m,input);
 if(m->clock>m->duration*3/4&&m->score[0]!=m->score[1])m->tactics[1]=m->score[1]<m->score[0]?3:2;
 FCBall*b=&m->ball;int owner=b->owner,team=owner<0?(b->receiver>=0?b->last/11:-1):owner/11;
 int chase[2]={pressing_player(m,0,(input&FC_PRESS)!=0),pressing_player(m,1,false)};
 /* Human defending already supplies a presser. A second bot only joins when
  * explicitly requested, or when the selected player is too far to engage. */
 if(!(input&FC_PRESS)&&fc_distance(m->p[m->selected].x-b->x,m->p[m->selected].y-b->y)<8*Q)chase[0]=m->selected;
 int limits[2]={line(m,0),line(m,1)},marks[22];
 for(int i=0;i<22;i++)marks[i]=-1;
 /* Assign each defender a different threat, instead of dragging two backs
  * onto the same forward while another attacker runs through unmarked. */
 for(int t=0;t<2;t++)if(team!=t){unsigned used=0;
  for(int n=1;n<=(!t&&m->formation==2?3:4);n++){int i=t*11+n,best=-1,score=100000;if(!fc_active(m,i))continue;
   for(int j=(1-t)*11+5;j<(2-t)*11;j++)if(fc_active(m,j)&&!(used&(1u<<(j%11)))){
    int progress=fc_attack(m,t)>0?m->p[j].y:FC_H-m->p[j].y;
    if(progress>65*Q)continue;
    int value=fc_distance(m->p[i].x-m->p[j].x,m->p[i].y-m->p[j].y)+progress/5;
    if(j==owner)value-=3*Q;if(value<score){score=value;best=j;}
   }marks[i]=best;if(best>=0)used|=1u<<(best%11);
  }
 }
 for(int i=0;i<22;i++){
  if(i==m->selected||!fc_active(m,i))continue;
  /* A pass or tackle earlier in this loop may have changed possession. */
  owner=b->owner;team=owner<0?(b->receiver>=0?b->last/11:-1):owner/11;
  FCPlayer*p=&m->p[i];FCMotion*f=&m->motion[i];int t=i/11,n=i%11,dir=fc_attack(m,t),x,y,tactic=m->tactics[t];
  int speed=(t?1000+m->difficulty*90:1280)+(int)(f->skill-80)*3;
  if(i==fc_keeper(m,t)){keeper(m,i);continue;}
  bool wall=false;for(int w=0;w<m->wall_count;w++)if(m->wall[w]==i&&m->wall_clock<29)wall=true;
  if(wall){fc_move_to(m,i,p->x,p->y,0);continue;}
  if(m->ai_stumble[i]){fc_move_to(m,i,m->ai_x[i],m->ai_y[i],speed*3/4);continue;}
  int ball_progress=dir>0?b->y:FC_H-b->y;
  if(i==owner){
   int ownpressure=pressure(m,t,p->x,p->y),goal=dir>0?FC_H:0;
   int left=pressure(m,t,p->x-4*Q,p->y+dir*5*Q),right=pressure(m,t,p->x+4*Q,p->y+dir*5*Q);
   x=FC_CLAMP(p->x,-20*Q,20*Q);y=goal;
   if(ownpressure<5*Q)x=p->x+(left>right?-5:5)*Q;
   if(abs(p->x)>22*Q)x=p->x*3/4;
   if(tactic==1)speed-=Q/3;else if(tactic==2&&ownpressure>3*Q)speed+=Q/2;
   if(!f->think&&!m->pass_cool){
    int target=fc_pass_target(m,i,FC_GROUND),forward=target<0?0:(m->p[target].y-p->y)*dir;
    if(abs(p->y-goal)<(abs(p->x)<9*Q?24:18)*Q&&abs(p->x)<19*Q&&ownpressure>Q/2){fc_shot(m,i,460+fc_random(&m->seed)%280);}
    else if(target>=0&&(ownpressure<4*Q||forward>8*Q||((fc_random(&m->seed)%100)<(tactic==1?42u:12u)))){
     unsigned kind=forward>(tactic==2?6:10)*Q&&target%11>=8?FC_RUN_PASS:FC_GROUND;
     if(ownpressure<2*Q&&forward>15*Q)kind=FC_HIGH_PASS;
     fc_pass_type(m,i,kind,380+fc_random(&m->seed)%220);
    }
    f->think=(uint8_t)((t?44-m->difficulty*8:22)+fc_random(&m->seed)%9);
   }
  }else if(b->owner<0&&b->receiver==i){
   fc_receive_target(m,i,&x,&y);speed+=Q;
  }else if(i==chase[t]&&team!=t){
   if(!m->ai_delay[i]){m->ai_x[i]=(int16_t)FC_CLAMP(b->x+b->vx/5,-FC_W/2+Q,FC_W/2-Q);m->ai_y[i]=(int16_t)FC_CLAMP(b->y+b->vy/5,Q,FC_H-Q);m->ai_delay[i]=(uint8_t)(t?7-m->difficulty*2+fc_random(&m->seed)%5:4);}
   x=m->ai_x[i];y=m->ai_y[i];
   if(owner>=0){int side=dir*Q;/* Approach from the goal side, then time the tackle. */
    if(fc_distance(p->x-b->x,p->y-b->y)>3*Q)y-=side;
    if(fc_distance(p->x-b->x,p->y-b->y)<Q+Q/4&&!p->cooldown&&!f->think){
     int front=(p->x-m->p[owner].x)*m->motion[owner].face_x+(p->y-m->p[owner].y)*m->motion[owner].face_y;
     if((front>-Q*Q/3||fc_random(&m->seed)%100<6)&&(!t||(fc_random(&m->seed)%100)<(40u+m->difficulty*18u)))fc_tackle(m,i);f->think=(uint8_t)(t?32-m->difficulty*7:14);
    }
   }speed+=Q/2;
  }else{
   int form=t?0:m->formation,progress;int width=home[form][n][0]*Q;
   bool back=n<=(form==2?3:4),mid=!back&&n<=(form==1?8:7);
   if(team==t){
    if(back){progress=ball_progress-23*Q+(home[form][n][1]-21)*Q;x=width*11/10+b->x/8;}
    else if(mid){
     progress=ball_progress+(n==6?-11:n==5?-3:2)*Q;
     x=width+b->x/5;
     /* Near-side midfielder offers a short diagonal; the far one retains
      * width. The holding midfielder remains a separate backward option. */
     if(n!=6&&abs(width-b->x)<9*Q)x=b->x+(width<0?7:-7)*Q;
    }else{progress=ball_progress+(n==9?17:n==8?9:12)*Q+(tactic==2?4:0)*Q-(tactic==1?3:0)*Q;x=width*6/5+b->x/10;}
   }else{
    progress=ball_progress+(back?-16:mid?-6:15)*Q+(home[form][n][1]-(back?21:mid?44:72))*Q/3;
    x=width*9/10+b->x/4;
    if(tactic==3)progress+=3*Q;else if(tactic==2)progress-=3*Q;
   }
   progress=FC_CLAMP(progress,back?8*Q:mid?13*Q:28*Q,back?76*Q:mid?94*Q:FC_H-8*Q);
   y=dir>0?progress:FC_H-progress;
   if(team==t&&!back){int limit=FC_MAX(limits[t],FC_H/2*dir)-((n%3)*Q/2);if(y*dir>limit)y=limit*dir;}
   if(team!=t&&back&&owner>=0){
    int mark=marks[i];
    if(mark>=0&&abs(m->p[mark].x-x)<14*Q&&fc_distance(m->p[mark].x-b->x,m->p[mark].y-b->y)<25*Q){x=(x+m->p[mark].x)/2;y=(y+m->p[mark].y-dir*3*Q)/2;}
   }
  }
  /* Keep passing support spread out before a collision occurs. The carrier
   * and intended receiver retain their exact dribble/interception targets. */
  if(i!=owner&&i!=b->receiver&&i!=chase[t])for(int j=t*11+1;j<(t+1)*11;j++)if(j!=i&&fc_active(m,j)){
   int dx=x-m->p[j].x,dy=y-m->p[j].y,d=fc_distance(dx,dy);
   if(d<4*Q){if(d<Q/4){dx=(i&1)?Q:-Q;dy=n<5?-dir*Q:dir*Q;d=Q;}int push=(4*Q-d)/2;x+=dx*push/d;y+=dy*push/d;}
  }
  if(p->stamina<160)speed=speed*4/5;
  fc_move_to(m,i,FC_CLAMP(x,-FC_W/2+3*Q,FC_W/2-3*Q),FC_CLAMP(y,Q,(FC_H-Q)),speed);
  if(m->phase!=FC_LIVE)return;
 }
}
