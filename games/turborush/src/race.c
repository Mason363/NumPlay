/* Fixed 50 Hz physics. All distances and speeds use 24.8 fixed point.
 * No heap, image decompression, sound buffers or per-frame allocations. */
#include "race.h"
#include <string.h>
#define CLAMP(v,a,b) ((v)<(a)?(a):(v)>(b)?(b):(v))
#define ABS(v) ((v)<0?-(v):(v))
const TrTrack tr_tracks[TR_TRACKS]={
 {"Sunset Coast",90,0,{0,0,12,32,48,48,32,0,-20,-40,-40,-20,0,0,32,65,65,32,0,-30,-55,-55,-30,0,18,40,40,18,-30,-30,0,0}},
 {"Pine Valley",95,1,{0,0,-18,-45,-65,-45,0,25,50,65,50,15,0,-25,-60,-75,-60,-25,0,0,40,55,40,0,-40,-65,-40,0,25,25,0,0}},
 {"Red Rock Run",85,2,{0,0,30,60,80,60,0,-35,-65,-35,0,45,70,45,0,-40,-80,-80,-40,0,0,55,80,55,0,-45,-70,-45,20,20,0,0}},
 {"Harbor Sprint",100,3,{0,0,0,0,40,75,75,40,0,0,-35,-70,-70,-35,0,0,0,40,80,80,40,0,0,-45,-80,-80,-45,0,0,0,0,0}},
 {"Alpine Pass",90,1,{0,0,25,55,80,80,45,0,-45,-80,-80,-45,0,35,70,90,70,35,0,-40,-75,-90,-75,-40,0,30,65,65,30,-25,0,0}},
 {"Metro Circuit",90,3,{0,0,-30,-65,-80,-65,-30,0,0,40,80,80,40,0,-35,-70,-35,0,40,65,40,0,-40,-80,-80,-40,0,0,30,30,0,0}}
};
static uint32_t rng(TrRace*r){uint32_t x=r->seed;x^=x<<13;x^=x>>17;return r->seed=x^(x<<5);}
int tr_length(unsigned t){return tr_tracks[t%TR_TRACKS].segment*32;}
int tr_curve(unsigned t,int32_t m){
 const TrTrack*q=&tr_tracks[t%TR_TRACKS];int len=q->segment*32;m%=len;if(m<0)m+=len;
 int i=m/q->segment,f=m%q->segment;int a=q->curves[i],b=q->curves[(i+1)&31];return a+(b-a)*f/q->segment;
}
void tr_defaults(TrProfile*p){memset(p,0,sizeof *p);p->mood=1;p->difficulty=1;p->effects=1;}
int tr_top(const TrProfile*p){return 63+p->upgrades[0]*3;}
void tr_new(TrRace*r,const TrProfile*p,unsigned track,uint32_t seed){
 memset(r,0,sizeof *r);r->seed=seed?seed:1;r->track=track%TR_TRACKS;r->mood=p->mood;r->manual=p->manual;r->difficulty=p->difficulty;
 r->mode=p->mode;r->phase=r->mode==TR_FREE_RIDE?TR_RACING:TR_READY;r->countdown=r->mode==TR_FREE_RIDE?0:150;r->nitro=1000;r->gear=1;r->rank=8;r->lap=1;
 for(int i=1;i<8;i++){
   int order=7-i;r->cars[i].distance=(40+(order/2)*70+(order%2)*15)*256;r->cars[i].x=(order&1)?520:-520;
   if(r->mode==TR_FREE_RIDE){r->cars[i].distance=(90+(i-1)*100+(int)(rng(r)%20))*256;r->cars[i].x=((int)(rng(r)%3)-1)*620;r->cars[i].speed=(27+rng(r)%15)*256;}
   r->cars[i].lane=r->cars[i].x;r->cars[i].style=i%2;r->cars[i].skill=rng(r)%5;
 }
}
bool tr_car_active(const TrRace*r,unsigned i){return i<8&&(r->mode!=TR_FREE_RIDE||i<=3u+r->difficulty);}
uint32_t tr_distance(const TrRace*r){return r->distance_base+(uint32_t)(r->cars[0].distance/256);}
int tr_speed_kmh(const TrRace*r){return r->cars[0].speed*36/(256*10);}
int tr_gap(const TrRace*r,int32_t a,int32_t b){if(r->mode==TR_FREE_RIDE)return a-b;int length=tr_length(r->track)*256,d=(a-b)%length;if(d>length/2)d-=length;if(d< -length/2)d+=length;return d;}
unsigned tr_position(const TrRace*r,unsigned car){
 unsigned rank=1;const TrCar*a=&r->cars[car];
 for(unsigned j=0;j<8;j++)if(j!=car){const TrCar*b=&r->cars[j];
   if((b->finished&&(!a->finished||b->finished<a->finished))||
      (b->finished&&a->finished&&b->finished==a->finished&&j<car)||
      (!b->finished&&!a->finished&&(b->distance>a->distance||(b->distance==a->distance&&j<car))))rank++;
 }return rank;
}
unsigned tr_prize(unsigned rank){static const uint16_t money[8]={500,350,250,190,140,100,75,50};return money[CLAMP(rank,1,8)-1];}
void tr_step(TrRace*r,const TrProfile*p,unsigned input,int shift){
 if(r->phase>=TR_FINISHED)return;r->ticks++;
 if(r->phase==TR_READY){if(--r->countdown==0)r->phase=TR_RACING;return;}
 TrCar*c=&r->cars[0];int top=tr_top(p),curve=tr_curve(r->track,c->distance/256),v=c->speed/256;
 if(r->invulnerable)r->invulnerable--;
 if(r->manual){if(shift>0&&r->gear<5)r->gear++;if(shift<0&&r->gear>1)r->gear--;}
 else{r->gear=1+(v>=19)+(v>=32)+(v>=44)+(v>=56);}
 int cap=(int[]){0,24,37,49,60,top}[r->gear]*256;
 int accel=48+p->upgrades[0]*4;
 if(r->manual&&r->gear>1&&v<(r->gear-1)*12)accel=12;
 int speedcap=top*256-r->damage*(8-p->upgrades[3])/5;
 if(cap<speedcap)speedcap=cap;
 if(input&TR_BRAKE)c->speed-=155;
 else if(input&TR_THROTTLE)c->speed+=accel;
 else c->speed-=16+v/5;
 if(!(input&TR_BOOST)||r->nitro>=200)r->boost_empty=0;
 r->boost=(input&TR_BOOST)&&(input&TR_THROTTLE)&&!(input&TR_BRAKE)&&!r->boost_empty&&r->nitro>=6&&v>12&&ABS(c->x)<1040;
 if(r->boost){r->nitro-=6;speedcap+=15*256;c->speed+=100;}
 else if(r->nitro<1000){r->nitro+=1+p->upgrades[2];if(r->nitro>1000)r->nitro=1000;}
 if(r->nitro<6)r->boost_empty=1;
 if(c->speed>speedcap){c->speed-=110;if(c->speed<speedcap)c->speed=speedcap;}
 c->speed=CLAMP(c->speed,0,(top+15)*256);
 int steer=((input&TR_RIGHT)!=0)-((input&TR_LEFT)!=0),goal=steer*1000;
 r->steering+=(goal-r->steering)/4;if(ABS(goal-r->steering)<4)r->steering=goal;
 int target_heading=v<2?0:r->steering*(170+p->upgrades[1]*8)/1000-curve*v*350/(32000+p->upgrades[1]*6000);
 r->heading+=(target_heading-r->heading)/5;if(ABS(target_heading-r->heading)<5)r->heading=target_heading;
 c->lateral=r->heading*c->speed/(350*256);c->x+=c->lateral;
 if(r->impact)r->impact--;
 if(r->offroad_ticks)r->offroad_ticks--;
 if(ABS(c->x)>1050&&c->speed>28*256)c->speed-=190;
 if(ABS(c->x)>1500){
   if(v>10&&!r->offroad_ticks){r->damage+=55-p->upgrades[3]*8;c->speed=c->speed*70/100;r->impact=12;r->invulnerable=12;r->offroad_ticks=40;}
   c->x=CLAMP(c->x,-1490,1490);c->lateral=c->x>0?-8:8;
 }
 int oldlap=c->distance/(tr_length(r->track)*256);
 int forward=c->speed-(int)((int64_t)c->speed*r->heading*r->heading/2000000);
 c->distance+=forward/50;r->time+=20;r->lap_time+=20;
 int lap=c->distance/(tr_length(r->track)*256);
 if(lap!=oldlap){if(!r->best_lap||r->lap_time<r->best_lap)r->best_lap=r->lap_time;r->lap_time=0;}
 r->lap=CLAMP(lap+1,1,3);r->rank=1;
 int total=tr_length(r->track)*TR_LAPS*256;
 if(r->mode!=TR_FREE_RIDE&&c->distance>=total){c->distance=total;c->finished=r->time;}
 for(int i=1;i<8;i++){
   if(!tr_car_active(r,i))continue;
   TrCar*b=&r->cars[i];int bend=tr_curve(r->track,b->distance/256+70),cv=ABS(bend);
   int target=(r->mode==TR_FREE_RIDE?28+b->skill*3:49+r->difficulty*6+b->skill-cv/14)*256;
   if(r->mode!=TR_FREE_RIDE&&(r->ticks+i*7)%25==0){
     int best=-100000,lane=b->lane;
     for(int candidate=-650;candidate<=650;candidate+=650){int score=120-ABS(candidate-b->x)/10+candidate*bend/1500;
       for(int j=0;j<8;j++)if(j!=i&&tr_car_active(r,j)){int dz=tr_gap(r,r->cars[j].distance,b->distance)/256;
         if(dz> -8&&dz<65&&ABS(r->cars[j].x-candidate)<430)score-=(75-dz)*3;}
       if(score>best){best=score;lane=candidate;}
     }b->lane=lane;
   }
   int desired=CLAMP((b->lane-b->x)/18,-6,6);b->lateral+=(desired-b->lateral)/2;if(ABS(desired-b->lateral)<2)b->lateral=desired;
   b->x=CLAMP(b->x+b->lateral,-850,850);
   for(int j=0;j<8;j++)if(j!=i&&tr_car_active(r,j)){int dz=tr_gap(r,r->cars[j].distance,b->distance)/256;
     if(dz>0&&dz<12&&ABS(r->cars[j].x-b->x)<380&&r->cars[j].speed<target)target=r->cars[j].speed;}
   if(b->finished&&r->mode!=TR_FREE_RIDE){target=28*256;b->x+=b->x>=0?3:-3;}
   if(b->speed<target)b->speed+=34+i;else if(b->speed>target)b->speed-=95;
   if(b->speed<0)b->speed=0;b->distance+=b->speed/50;
   if(r->mode==TR_FREE_RIDE){
     int gap=b->distance-c->distance;
     if(gap< -8*256&&!b->finished){b->finished=1;if(r->overtakes<9999999)r->overtakes++;}
     if(gap< -120*256||(gap>1750*256&&c->speed>25*256)){
       int far=700+(int)(rng(r)%400);
       /* Recheck earlier slots after moving the spawn point: a single pass
        * can otherwise place a new car beside a previously checked one. */
       for(int attempt=0;attempt<8;attempt++){
         bool clear=true;
         for(int j=1;j<8;j++)if(j!=i&&tr_car_active(r,j)){
           int d=(r->cars[j].distance-c->distance)/256;
           if(ABS(d-far)<100){far=d+120;clear=false;break;}
         }
         if(clear)break;
       }
       b->distance=c->distance+far*256;b->finished=0;b->skill=rng(r)%5;b->style=rng(r)%2;
       b->x=((int)(rng(r)%3)-1)*620;b->lane=b->x;b->lateral=0;r->contacts[i-1]=0;
     }
   }else if(b->distance>=total&&!b->finished)b->finished=r->time;
   int dz=ABS(tr_gap(r,b->distance,c->distance));if(r->contacts[i-1])r->contacts[i-1]--;
   if((!b->finished||r->mode==TR_FREE_RIDE)&&!c->finished&&dz<5*256&&ABS(b->x-c->x)<380&&!r->contacts[i-1]){
     int closing=ABS(b->speed-c->speed)/256;
     r->damage+=(8+CLAMP(closing*2,0,65))*(10-p->upgrades[3])/10;
     if(c->speed>b->speed)c->speed=b->speed*90/100;else b->speed=c->speed*90/100;
     int side=c->x>=b->x?1:-1;c->lateral=side*10;r->heading=side*70;b->lateral=-side*6;c->x+=side*45;
     c->x=CLAMP(c->x,-1490,1490);r->contacts[i-1]=55;r->impact=10;r->invulnerable=10;
   }
 }
 int camera_goal=c->x*60/100;
 r->camera_x+=(camera_goal-r->camera_x)/8;
 r->camera_x=CLAMP(r->camera_x,c->x-600,c->x+600);
 r->rank=r->mode==TR_FREE_RIDE?0:tr_position(r,0);
 if(r->mode==TR_FREE_RIDE){
   r->damage=0;
   int chunk=tr_length(r->track)*8*256;
   if(c->distance>chunk*2){for(int i=0;i<8;i++){if(tr_car_active(r,i))r->cars[i].distance-=chunk;else r->cars[i].distance=0;}r->distance_base+=chunk/256;}
   return;
 }
 if(r->damage>=1000){r->damage=1000;r->phase=TR_WRECKED;}
 else if(c->distance>=total){c->distance=total;r->phase=TR_FINISHED;}
 if(r->time>=600000)r->phase=TR_WRECKED;
 /* Advance only once per simulation step; racing never depends on LCD rate. */
 if((r->ticks&255)==0)rng(r);
}
void tr_interpolate(TrRace*out,const TrRace*a,const TrRace*b,unsigned t){
 *out=*b;if(t>1000)t=1000;
 if(a->phase!=b->phase||a->track!=b->track||a->distance_base!=b->distance_base)return;
 for(int i=0;i<8;i++){
   int32_t delta=b->cars[i].distance-a->cars[i].distance;
   if(ABS(delta)>5*256)continue; /* Traffic respawns must never sweep through the scene. */
   out->cars[i].distance=a->cars[i].distance+delta*(int)t/1000;
   out->cars[i].x=a->cars[i].x+(b->cars[i].x-a->cars[i].x)*(int)t/1000;
   out->cars[i].lateral=a->cars[i].lateral+(b->cars[i].lateral-a->cars[i].lateral)*(int)t/1000;
 }
 out->camera_x=a->camera_x+(b->camera_x-a->camera_x)*(int)t/1000;
 out->heading=a->heading+(b->heading-a->heading)*(int)t/1000;
 out->steering=a->steering+(b->steering-a->steering)*(int)t/1000;
}
unsigned tr_reward(TrProfile*p,const TrRace*r){
 if(r->phase<TR_FINISHED||r->mode==TR_FREE_RIDE)return 0;unsigned prize=r->phase==TR_FINISHED?tr_prize(r->rank):25;
 p->money=p->money+prize>999999?999999:p->money+prize;if(p->races<999999)p->races++;
 if(r->phase==TR_FINISHED){if(r->rank==1&&p->wins<999999)p->wins++;uint32_t*t=&p->best[r->track];if(!*t||r->time<*t)*t=r->time;}
 return prize;
}
unsigned tr_price(const TrProfile*p,unsigned item){return item<4&&p->upgrades[item]<4?400+250*p->upgrades[item]+item*75:0;}
bool tr_buy(TrProfile*p,unsigned item){unsigned price=tr_price(p,item);if(!price||p->money<price)return false;p->money-=price;p->upgrades[item]++;return true;}
static uint32_t crc(const uint8_t*p,unsigned n){uint32_t h=~0u;while(n--){h^=*p++;for(int i=0;i<8;i++)h=(h>>1)^(0xedb88320u&-(h&1));}return ~h;}
static void put(uint8_t*p,uint32_t x){for(int i=0;i<4;i++)p[i]=(uint8_t)(x>>(8*i));}
static uint32_t get(const uint8_t*p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
void tr_encode(const TrProfile*p,uint8_t b[TR_SAVE_BYTES]){
 memset(b,0,TR_SAVE_BYTES);memcpy(b,"TRSH",4);b[4]=1;put(b+8,p->money);put(b+12,p->wins);put(b+16,p->races);
 for(int i=0;i<6;i++)put(b+20+i*4,p->best[i]);memcpy(b+44,p->upgrades,4);
 b[48]=p->paint;b[49]=p->mode;b[50]=p->track;b[51]=p->mood;b[52]=p->manual;b[53]=p->difficulty;b[54]=p->effects;
 put(b+76,crc(b,76));
}
bool tr_decode(TrProfile*p,const uint8_t*b,unsigned n){
 if(!b||n!=TR_SAVE_BYTES||memcmp(b,"TRSH",4)||b[4]!=1||get(b+76)!=crc(b,76))return false;
 TrProfile q;tr_defaults(&q);q.money=get(b+8);q.wins=get(b+12);q.races=get(b+16);
 if(q.money>999999||q.wins>q.races||q.races>999999)return false;
 for(int i=0;i<6;i++){q.best[i]=get(b+20+i*4);if(q.best[i]&&(q.best[i]<10000||q.best[i]>600000))return false;}
 for(int i=0;i<4;i++){q.upgrades[i]=b[44+i];if(q.upgrades[i]>4)return false;}
 q.paint=b[48];q.mode=b[49];q.track=b[50];q.mood=b[51];q.manual=b[52];q.difficulty=b[53];q.effects=b[54];
 if(q.paint>=6||q.mode>TR_FREE_RIDE||q.track>=6||q.mood>2||q.manual>1||q.difficulty>2||q.effects>2)return false;
 *p=q;return true;
}
