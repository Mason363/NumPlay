#include "game.h"
#include <string.h>
#include <limits.h>
void whg_defaults(WhgProfile *p){memset(p,0,sizeof *p);p->best_deaths=UINT32_MAX;p->best_ticks=UINT32_MAX;}
void whg_load_level(WhgRun *r,unsigned level){
 uint32_t deaths=r->deaths,elapsed=r->elapsed;memset(r,0,sizeof *r);r->deaths=deaths;r->elapsed=elapsed;
 r->level=(uint8_t)WHG_MIN(level,29u);r->phase=WHG_INTRO;r->timer=44;
 if(whg_tips[r->level]){r->phase=WHG_TIP;r->timer=0;}
 const WhgCheck *c=&whg_checks[whg_levels[r->level].check_offset];r->x=c->x;r->y=c->y;
}
void whg_new(WhgProfile *p){memset(&p->run,0,sizeof p->run);whg_load_level(&p->run,0);p->has_run=1;}
static int lerp(int a,int b,unsigned t,unsigned duration){int n=(b-a)*(int)t;return a+(n>=0?(n+(int)duration/2)/(int)duration:-((-n+(int)duration/2)/(int)duration));}
void whg_enemy(unsigned level,unsigned enemy,uint32_t frame,int16_t *x,int16_t *y){
 const WhgEnemy *e=&whg_enemies[whg_levels[level].enemy_offset+enemy];const WhgTrack *t=&whg_tracks[e->track];unsigned age=frame%t->period;
 const WhgPoint *p=&whg_points[t->offset];unsigned lo=0,hi=t->count-1;
 while(hi-lo>1){unsigned m=(hi+lo)/2;if(p[m].t<=age)lo=m;else hi=m;}
 unsigned dt=p[hi].t-p[lo].t;int dx=dt?lerp(p[lo].x,p[hi].x,age-p[lo].t,dt):p[lo].x;
 int dy=dt?lerp(p[lo].y,p[hi].y,age-p[lo].t,dt):p[lo].y;
 if(e->transform&4){int swap=dx;dx=dy;dy=swap;}if(e->transform&1)dx=-dx;if(e->transform&2)dy=-dy;
 *x=(int16_t)(e->x+dx);*y=(int16_t)(e->y+dy);
}
bool whg_wall(unsigned level,int x,int y){
 if(x<0||x>=11000||y<0||y>=8000)return true;
 const WhgLevel *l=&whg_levels[level];int winding[8]={0};
 for(unsigned i=0;i<l->wall_count;i++){
  const WhgWall *e=&whg_walls[l->wall_offset+i];int dy=e->y1-e->y0;
  if(y<WHG_MIN(e->y0,e->y1)||y>=WHG_MAX(e->y0,e->y1))continue;
  int side=(e->x1-e->x0)*(y-e->y0)-(x-e->x0)*dy;
  if((side>0)==(dy>0)){int sign=dy>0?1:-1;winding[e->f0]+=sign;winding[e->f1]-=sign;}
 }
 for(unsigned i=1;i<8;i++)if(winding[i])return true;return false;
}
bool whg_coin_taken(const WhgRun *r,unsigned coin){return (r->coins[coin>>3]>>(coin&7))&1;}
static bool check_hit(const WhgRun *r,const WhgCheck *c){return r->x+180>=c->x0&&r->x-180<=c->x1&&r->y+180>=c->y0&&r->y-180<=c->y1;}
static bool enemy_hit(unsigned level,unsigned index,uint32_t frame,int x,int y){
 int16_t ex,ey;whg_enemy(level,index,frame,&ex,&ey);
 /* Flash tests the centre and four cardinal edge points against the blue shape. */
 static const int16_t offset[5][2]={{0,0},{180,0},{-180,0},{0,-180},{0,180}};
 for(unsigned p=0;p<5;p++){int dx=x+offset[p][0]-ex,dy=y+offset[p][1]-ey;if(dx>=-130&&dx<=130&&dy>=-130&&dy<=130&&dx*dx+dy*dy<=130*130)return true;}
 return false;
}
void whg_restart(WhgRun *r){if(r->phase==WHG_PLAY){r->phase=WHG_DYING;r->timer=26;}}
unsigned whg_step(WhgRun *r,unsigned in){
 if(r->phase==WHG_FINISHED||r->phase==WHG_TIP)return 0;
 if(r->phase==WHG_INTRO){if(r->timer)r->timer--;if(!r->timer)r->phase=WHG_PLAY;return WHG_CHANGED;}
 const WhgLevel *l=&whg_levels[r->level];unsigned events=WHG_CHANGED;
 r->frame++;if(r->elapsed<UINT32_MAX)r->elapsed++;
 if(r->phase==WHG_DYING){
  if(r->timer)r->timer--;if(!r->timer){
   const WhgCheck *c=&whg_checks[l->check_offset+r->checkpoint];r->x=c->x;r->y=c->y;r->phase=WHG_PLAY;
   memset(r->coins,0,sizeof r->coins);r->collected=0;if(r->deaths<999999)r->deaths++;events|=WHG_DIED;
  }else if(r->timer==1){memset(r->coins,0,sizeof r->coins);r->collected=0;}return events;
 }
 for(unsigned i=1;i<l->goal;i++)if(r->checkpoint<i&&check_hit(r,&whg_checks[l->check_offset+i])){r->checkpoint=(uint8_t)i;events|=WHG_CHECKPOINT;}
 if(r->collected==l->coin_count&&check_hit(r,&whg_checks[l->check_offset+l->goal])){
  if(r->level==29){r->phase=WHG_FINISHED;return events|WHG_COMPLETED;}
  whg_load_level(r,r->level+1);return events|WHG_CLEARED;
 }
 if(in&WHG_RIGHT)r->x+=60;if(in&WHG_LEFT)r->x-=60;if(in&WHG_UP)r->y-=60;if(in&WHG_DOWN)r->y+=60;
 /* Original 3-pixel steps, independent diagonal axes and inset wall probes. */
 if(whg_wall(r->level,r->x+120,r->y))r->x-=60;
 if(whg_wall(r->level,r->x-120,r->y))r->x+=60;
 if(whg_wall(r->level,r->x,r->y-120))r->y+=60;
 if(whg_wall(r->level,r->x,r->y+120))r->y-=60;
 for(unsigned i=0;i<l->enemy_count;i++)if(enemy_hit(r->level,i,r->frame,r->x,r->y)){whg_restart(r);return events;}
 for(unsigned i=0;i<l->coin_count;i++)if(!whg_coin_taken(r,i)){
  const WhgCoin *c=&whg_coins[l->coin_offset+i];int dx=r->x-c->x,dy=r->y-c->y;
  if(dx>=-310&&dx<=310&&dy>=-310&&dy<=310){r->coins[i>>3]|=(uint8_t)(1u<<(i&7));r->collected++;}
 }
 return events;
}
void whg_record_finish(WhgProfile *p){
 if(!p->has_run||p->run.phase!=WHG_FINISHED)return;
 p->has_run=0;p->furthest=29;p->last_deaths=p->run.deaths;if(p->finishes<999999)p->finishes++;
 if(p->run.deaths<p->best_deaths||(p->run.deaths==p->best_deaths&&p->run.elapsed<p->best_ticks)){p->best_deaths=p->run.deaths;p->best_ticks=p->run.elapsed;}
}
