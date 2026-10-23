#include "../src/race.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static unsigned drive(TrRace*r){
 int target=0;for(int i=1;i<8;i++){int dz=(r->cars[i].distance-r->cars[0].distance)/256;
   if(dz>0&&dz<45&&abs(r->cars[i].x)<350){target=r->cars[i].x<=0?720:-720;break;}}
 unsigned k=TR_THROTTLE;
 if(r->cars[0].x>target+15)k|=TR_LEFT;if(r->cars[0].x<target-15)k|=TR_RIGHT;
 if(abs(r->cars[0].x)<900&&abs(tr_curve(r->track,r->cars[0].distance/256+60))<45)k|=TR_BOOST;
 return k;
}
int main(void){
 TrProfile p,q,unchanged;TrRace r;tr_defaults(&p);uint8_t bytes[TR_SAVE_BYTES];
 p.money=5000;p.best[2]=150000;p.upgrades[0]=2;tr_encode(&p,bytes);tr_defaults(&q);assert(tr_decode(&q,bytes,sizeof bytes));assert(!memcmp(&p,&q,sizeof p));
 unchanged=q;for(int i=0;i<TR_SAVE_BYTES;i++){bytes[i]^=0x80;assert(!tr_decode(&q,bytes,sizeof bytes));assert(!memcmp(&q,&unchanged,sizeof q));bytes[i]^=0x80;}
 assert(!tr_decode(&q,bytes,sizeof bytes-1));assert(!tr_decode(&q,NULL,0));
 for(int i=0;i<4;i++)while(tr_price(&p,i)){unsigned old=p.money,price=tr_price(&p,i);p.money=999999;old=p.money;assert(tr_buy(&p,i));assert(p.money==old-price);}assert(!tr_buy(&p,0));
 tr_defaults(&p);assert(!tr_buy(&p,0));
 tr_new(&r,&p,0,1);for(int i=1;i<8;i++)for(int j=i+1;j<8;j++){
   int dz=abs(r.cars[i].distance-r.cars[j].distance)/256;
   assert(dz>=15);if(abs(r.cars[i].x-r.cars[j].x)<400)assert(dz>=70);
 }
 unsigned runs=0,wins=0,wrecks=0;unsigned long steps=0;
 for(int t=0;t<6;t++)for(int diff=0;diff<3;diff++)for(int upgrades=0;upgrades<2;upgrades++)for(int seed=1;seed<=8;seed++){
   tr_defaults(&p);p.difficulty=diff;for(int j=0;j<4;j++)p.upgrades[j]=upgrades*4;
   tr_new(&r,&p,t,seed);assert(r.rank==8);for(int i=0;i<149;i++)tr_step(&r,&p,TR_THROTTLE,0);assert(r.phase==TR_READY&&r.cars[0].distance==0);tr_step(&r,&p,0,0);assert(r.phase==TR_RACING);
   for(unsigned tick=0;tick<30001&&r.phase==TR_RACING;tick++){
     tr_step(&r,&p,drive(&r),0);steps++;
     assert(r.rank>=1&&r.rank<=8&&r.lap>=1&&r.lap<=3);assert(r.nitro<=1000&&r.damage<=1000);assert(r.gear>=1&&r.gear<=5);
     assert(r.cars[0].speed>=0&&r.cars[0].speed<=(tr_top(&p)+15)*256);assert(abs(r.cars[0].x)<1800);
   }
   assert(r.phase>=TR_FINISHED);runs++;if(r.phase==TR_WRECKED)wrecks++;else if(r.rank==1)wins++;
   if(r.phase==TR_FINISHED){assert(r.cars[0].distance==tr_length(t)*3*256);assert(tr_position(&r,0)==r.rank);assert(r.best_lap>0);}
   unsigned cash=tr_reward(&p,&r);assert(cash>0&&p.races==1);assert(p.best[t]==(r.phase==TR_FINISHED?r.time:0));
 }
 assert(wins>100&&wrecks==0);
 /* Finishing behind a previously finished rival must never become first. */
 tr_defaults(&p);tr_new(&r,&p,0,1);r.phase=TR_RACING;r.time=200000;r.cars[0].distance=tr_length(0)*3*256-1;r.cars[0].speed=10000;r.cars[1].distance=tr_length(0)*3*256;r.cars[1].finished=100000;
 tr_step(&r,&p,TR_THROTTLE,0);assert(r.phase==TR_FINISHED&&r.rank==2);
 /* Manual gearbox, boost charge, braking and collision cooldown. */
 p.manual=1;tr_new(&r,&p,0,1);r.phase=TR_RACING;for(int i=0;i<1000;i++)tr_step(&r,&p,TR_THROTTLE,0);assert(r.gear==1&&tr_speed_kmh(&r)<90);
 for(int i=0;i<4;i++)tr_step(&r,&p,TR_THROTTLE,1);assert(r.gear==5);r.cars[0].x=0;r.cars[0].speed=40*256;unsigned n=r.nitro;tr_step(&r,&p,TR_THROTTLE|TR_BOOST,0);assert(r.nitro<n&&r.boost);
 int v=r.cars[0].speed;tr_step(&r,&p,TR_BRAKE,0);assert(r.cars[0].speed<v);
 tr_new(&r,&p,0,1);r.phase=TR_RACING;r.cars[1]=r.cars[0];tr_step(&r,&p,0,0);assert(r.damage>0&&r.invulnerable);unsigned damage=r.damage;tr_step(&r,&p,0,0);assert(r.damage==damage);
 /* Regressions: no sideways motion at rest, progressive direction changes,
  * no boost while braking, depletion hysteresis, physical contact width. */
 tr_new(&r,&p,0,1);r.phase=TR_RACING;
 for(int i=0;i<100;i++)tr_step(&r,&p,TR_LEFT,0);assert(r.cars[0].x==0&&r.cars[0].lateral==0);
 r.cars[0].speed=45*256;r.steering=0;tr_step(&r,&p,TR_RIGHT,0);assert(r.steering>0&&r.steering<1000&&r.cars[0].lateral<20);
 r.nitro=500;tr_step(&r,&p,TR_THROTTLE|TR_BRAKE|TR_BOOST,0);assert(!r.boost&&r.nitro>=500);
 r.nitro=6;r.cars[0].speed=40*256;r.cars[0].x=0;tr_step(&r,&p,TR_THROTTLE|TR_BOOST,0);assert(r.boost_empty);
 for(int i=0;i<100;i++){tr_step(&r,&p,TR_THROTTLE|TR_BOOST,0);assert(!r.boost);}
 tr_new(&r,&p,0,1);r.phase=TR_RACING;r.cars[0].speed=40*256;
 for(int j=2;j<8;j++)r.cars[j].distance=(500+j*50)*256;
 r.cars[1]=r.cars[0];r.cars[1].x=600;tr_step(&r,&p,TR_THROTTLE,0);assert(r.damage==0);
 r.cars[1].x=300;r.cars[1].distance=r.cars[0].distance;tr_step(&r,&p,TR_THROTTLE,0);assert(r.damage>0);
 damage=r.damage;for(int i=0;i<20;i++)tr_step(&r,&p,TR_THROTTLE,0);assert(r.damage==damage);
 assert(tr_gap(&r,5*256,(tr_length(0)-5)*256)==10*256);
 tr_new(&r,&p,0,1);r.phase=TR_RACING;r.cars[0].x=1499;r.cars[0].speed=50*256;r.steering=1000;r.cars[0].lateral=25;tr_step(&r,&p,TR_THROTTLE|TR_RIGHT,0);
 damage=r.damage;assert(damage>0&&r.cars[0].x<=1490);for(int i=0;i<15;i++)tr_step(&r,&p,TR_THROTTLE|TR_RIGHT,0);assert(r.damage==damage);
 /* Free Ride loops indefinitely, recycles sparse traffic and never banks
  * race rewards or records. Distance stays monotonic across rebasing. */
 tr_defaults(&p);p.mode=TR_FREE_RIDE;tr_encode(&p,bytes);assert(tr_decode(&q,bytes,sizeof bytes)&&q.mode==TR_FREE_RIDE);
 tr_new(&r,&p,0,1);assert(r.phase==TR_RACING&&r.countdown==0);unsigned active=0;for(int i=1;i<8;i++)active+=tr_car_active(&r,i);assert(active==4);
 uint32_t last_distance=0;
 for(int i=0;i<160000;i++){
   tr_step(&r,&p,drive(&r),0);assert(r.phase==TR_RACING&&r.damage==0&&r.cars[0].finished==0);
   assert(tr_distance(&r)>=last_distance);last_distance=tr_distance(&r);assert(abs(r.cars[0].x-r.camera_x)<=600);
 }
 assert(r.time>600000&&r.distance_base>0&&r.overtakes>10);unchanged=p;r.phase=TR_FINISHED;assert(tr_reward(&p,&r)==0&&!memcmp(&p,&unchanged,sizeof p));
 for(int seed=1;seed<=128;seed++){
   tr_new(&r,&p,0,seed);r.cars[0].distance=1000*256;r.cars[1].distance=850*256;
   for(int i=2;i<=4;i++)r.cars[i].distance=(1890+(i-2)*110)*256;
   tr_step(&r,&p,0,0);for(int i=2;i<=4;i++)assert(abs(r.cars[1].distance-r.cars[i].distance)>95*256);
 }
 uint32_t fuzz=0x12345678;for(int run=0;run<1500;run++){
   tr_defaults(&p);p.manual=run&1;p.difficulty=run%3;tr_new(&r,&p,run%6,run+1);r.phase=TR_RACING;
   for(int i=0;i<400&&r.phase==TR_RACING;i++){
     fuzz^=fuzz<<13;fuzz^=fuzz>>17;fuzz^=fuzz<<5;tr_step(&r,&p,fuzz&31,(int)(fuzz%3)-1);
     assert(r.damage<=1000&&r.nitro<=1000&&abs(r.cars[0].x)<=1500&&r.cars[0].speed>=0&&r.gear>=1&&r.gear<=5);
     for(int j=1;j<8;j++)assert(r.cars[j].speed>=0&&abs(r.cars[j].x)<900);
   }
 }
 TrRace before,after,draw;tr_new(&before,&p,0,1);before.phase=TR_RACING;after=before;after.cars[0].distance+=256;after.cars[0].x=100;after.heading=100;
 tr_interpolate(&draw,&before,&after,250);assert(draw.cars[0].distance==64&&draw.cars[0].x==25&&draw.heading==25);
 after.cars[1].distance+=800*256;tr_interpolate(&draw,&before,&after,500);assert(draw.cars[1].distance==after.cars[1].distance);
 printf("%u complete races, %u wins, %u wrecks, %lu physics steps; 1500 random-input sessions; save corruption, shop, ranking, manual gears and collisions passed\n",runs,wins,wrecks,steps);
 return 0;
}
