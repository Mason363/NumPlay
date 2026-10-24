#include "../src/game.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "reference.inc"
#include "wall-reference.inc"
static unsigned rng=1234567;
static unsigned random32(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static void solve_first(void){
 enum { W=184,H=134,P=72,N=W*H*P };
 uint32_t *parent=malloc(N*sizeof *parent),*queue=malloc(N*sizeof *queue);uint8_t *move=malloc(N);assert(parent&&queue&&move);memset(parent,255,N*sizeof *parent);
 WhgRun r={0};whg_load_level(&r,0);r.phase=WHG_PLAY;unsigned first=(unsigned)((r.y-40)/60)*W+(unsigned)((r.x-10)/60);parent[first]=first;queue[0]=first;unsigned head=0,tail=1,end=UINT_MAX,last=0;
 static const unsigned input[]={0,WHG_LEFT,WHG_RIGHT,WHG_UP,WHG_DOWN,WHG_LEFT|WHG_UP,WHG_RIGHT|WHG_UP,WHG_LEFT|WHG_DOWN,WHG_RIGHT|WHG_DOWN};
 while(head<tail&&end==UINT_MAX){unsigned id=queue[head++],at=id%(W*H);r.x=(int16_t)(10+60*(at%W));r.y=(int16_t)(40+60*(at/W));r.frame=id/(W*H);r.phase=WHG_PLAY;r.level=0;r.checkpoint=0;r.timer=0;
  for(unsigned i=0;i<sizeof input/sizeof *input;i++){
   WhgRun q=r;unsigned event=whg_step(&q,input[i]);if(event&WHG_CLEARED){end=id;last=input[i];break;}if(q.phase!=WHG_PLAY||q.x<10||q.y<40||q.x>=11000||q.y>=8000)continue;
   unsigned nx=(unsigned)(q.x-10)/60,ny=(unsigned)(q.y-40)/60;if(nx>=W||ny>=H)continue;
   unsigned n=(q.frame%P)*(W*H)+ny*W+nx;if(parent[n]!=UINT_MAX)continue;parent[n]=id;move[n]=(uint8_t)input[i];assert(tail<N);queue[tail++]=n;
  }
 }
 assert(end!=UINT_MAX);uint8_t path[2000];unsigned count=0;path[count++]=(uint8_t)last;
 while(end!=first){assert(count<sizeof path);path[count++]=move[end];end=parent[end];}
 FILE *f=fopen("level1-replay.bin","wb");assert(f);r=(WhgRun){0};whg_load_level(&r,0);r.phase=WHG_PLAY;
 for(unsigned i=count;i;i--){fputc(path[i-1],f);whg_step(&r,path[i-1]);assert(r.deaths==0&&r.phase!=WHG_DYING);}fclose(f);assert(r.level==1);
 printf("First level solved through real collisions in %u ticks, zero deaths (%u search states)\n",count,tail);free(parent);free(queue);free(move);
}
int main(void){
 static const unsigned enemies[]={4,12,11,21,16,72,12,7,25,19,56,296,12,42,20,20,12,10,16,13,14,24,41,26,47,26,66,54,23,45};
 static const unsigned coins[]={0,1,1,3,0,4,4,3,1,0,2,1,0,0,0,4,0,67,0,7,3,0,36,0,0,4,1,0,2,4};
 int worst=0;
 for(unsigned i=0;i<sizeof reference/sizeof *reference;i++){const int16_t *f=reference[i];int16_t x,y;whg_enemy((unsigned)f[0],(unsigned)f[2],(unsigned)f[1],&x,&y);int err=WHG_MAX(abs(x-f[3]),abs(y-f[4]));worst=WHG_MAX(worst,err);if(err>2){printf("Bad reference %u: stage %d frame %d enemy %d: %d,%d != %d,%d\n",i,f[0]+1,f[1],f[2],x,y,f[3],f[4]);fflush(stdout);}assert(err<=2);}
 printf("Original Flash trajectories: %u independent samples, maximum deviation %d/20 px\n",(unsigned)(sizeof reference/sizeof *reference),worst);
 for(unsigned i=0;i<sizeof wall_reference/sizeof *wall_reference;i++){const int16_t *f=wall_reference[i];bool hit=whg_wall((unsigned)f[0],f[1],f[2]);if(hit!=f[3]){printf("Wall mismatch level %d at %d,%d: %d != %d\n",f[0]+1,f[1],f[2],hit,f[3]);fflush(stdout);}assert(hit==f[3]);}
 printf("Original wall-render reference: %u points matched\n",(unsigned)(sizeof wall_reference/sizeof *wall_reference));
 WhgProfile p;whg_defaults(&p);whg_new(&p);assert(p.has_run&&p.run.timer==44);for(int i=0;i<44;i++)whg_step(&p.run,0);assert(p.run.phase==WHG_PLAY);
 int x=p.run.x,y=p.run.y;whg_step(&p.run,WHG_RIGHT|WHG_DOWN);assert(p.run.x==x+60&&p.run.y==y+60);
 for(unsigned level=0;level<30;level++){
  const WhgLevel *l=&whg_levels[level];assert(l->enemy_count==enemies[level]&&l->coin_count==coins[level]&&l->enemy_count<=WHG_MAX_ENEMIES);
  WhgRun r={0};whg_load_level(&r,level);assert(!whg_wall(level,r.x,r.y));r.phase=WHG_PLAY;
  if(whg_tips[level]){WhgRun tip={0};whg_load_level(&tip,level);assert(tip.phase==WHG_TIP);for(int t=0;t<100;t++)assert(!whg_step(&tip,WHG_RIGHT));assert(tip.frame==0);}
  assert(whg_wall(level,-1,4000)&&whg_wall(level,11000,4000));
  for(unsigned attempt=0;attempt<8;attempt++){
   for(unsigned t=0;t<2000;t++){unsigned event=whg_step(&r,random32()&15);assert(r.level<30&&r.phase<=WHG_TIP&&r.x>=0&&r.x<=11000&&r.y>=0&&r.y<=8000);assert(r.collected<=whg_levels[r.level].coin_count);if(event&WHG_COMPLETED)break;}
  }
  /* Finish triggers, all coins required, checkpoint persistence and reset rules. */
  whg_load_level(&r,level);r.phase=WHG_PLAY;r.timer=0;const WhgCheck *goal=&whg_checks[l->check_offset+l->goal];r.x=(goal->x0+goal->x1)/2;r.y=(goal->y0+goal->y1)/2;
  if(l->coin_count){unsigned e=whg_step(&r,0);assert(!(e&(WHG_CLEARED|WHG_COMPLETED)));r.phase=WHG_PLAY;r.x=(goal->x0+goal->x1)/2;r.y=(goal->y0+goal->y1)/2;}
  r.collected=(uint8_t)l->coin_count;for(unsigned i=0;i<l->coin_count;i++)r.coins[i>>3]|=1u<<(i&7);
  unsigned e=whg_step(&r,0);assert(e&(level==29?WHG_COMPLETED:WHG_CLEARED));
  whg_load_level(&r,level);r.deaths=0;r.phase=WHG_PLAY;r.timer=0;if(l->goal>1)r.checkpoint=1;unsigned cp=r.checkpoint;r.collected=(uint8_t)l->coin_count;
  for(unsigned i=0;i<l->coin_count;i++)r.coins[i>>3]|=1u<<(i&7);
  unsigned frame=r.frame;whg_restart(&r);for(unsigned i=0;i<25;i++)whg_step(&r,WHG_RIGHT);assert(r.phase==WHG_DYING&&r.deaths==0);assert(whg_step(&r,0)&WHG_DIED);assert(r.deaths==1&&r.collected==0&&r.checkpoint==cp&&r.frame==frame+26);assert(r.x==whg_checks[l->check_offset+cp].x&&r.y==whg_checks[l->check_offset+cp].y);
 }
 whg_new(&p);p.run.phase=WHG_PLAY;for(int i=0;i<7;i++)whg_step(&p.run,WHG_RIGHT);p.run.deaths=42;uint8_t b[WHG_SAVE_BYTES];whg_encode(&p,b);WhgProfile q;assert(whg_decode(&q,b,sizeof b));assert(!memcmp(&p,&q,sizeof p));
 for(unsigned i=0;i<sizeof b;i++){b[i]^=0x20;assert(!whg_decode(&q,b,sizeof b));b[i]^=0x20;}assert(!whg_decode(&q,b,sizeof b-1)&&!whg_decode(&q,NULL,sizeof b));
 p.run.phase=WHG_FINISHED;p.run.level=29;p.furthest=29;whg_record_finish(&p);assert(p.finishes==1&&!p.has_run&&p.best_deaths==42);whg_record_finish(&p);assert(p.finishes==1);whg_encode(&p,b);assert(whg_decode(&q,b,sizeof b));
 solve_first();puts("All 30 stages, 480000 random-input steps, coins, checkpoints, death phases, records and save corruption passed");return 0;
}
