#include "game.h"
#include <string.h>
static void put16(uint8_t *p,unsigned x){p[0]=(uint8_t)x;p[1]=(uint8_t)(x>>8);}
static void put32(uint8_t *p,uint32_t x){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(x>>(8*i));}
static unsigned get16(const uint8_t *p){return p[0]|((unsigned)p[1]<<8);}
static uint32_t get32(const uint8_t *p){return (uint32_t)get16(p)|((uint32_t)get16(p+2)<<16);}
static uint32_t crc(const uint8_t *p,unsigned n){uint32_t h=0xffffffff;for(unsigned i=0;i<n;i++){h^=p[i];for(unsigned j=0;j<8;j++)h=(h>>1)^(0xedb88320u&-(h&1));}return ~h;}
void whg_encode(const WhgProfile *p,uint8_t b[WHG_SAVE_BYTES]){
 memset(b,0,WHG_SAVE_BYTES);memcpy(b,"WHG1",4);b[4]=1;b[5]=p->has_run;b[6]=p->furthest;b[7]=p->run.level;
 b[8]=p->run.phase;b[9]=p->run.checkpoint;b[10]=p->run.timer;b[11]=p->run.collected;
 put16(b+12,(unsigned)p->run.x);put16(b+14,(unsigned)p->run.y);put32(b+16,p->run.frame);put32(b+20,p->run.elapsed);put32(b+24,p->run.deaths);
 memcpy(b+28,p->run.coins,9);put32(b+40,p->best_deaths);put32(b+44,p->best_ticks);put32(b+48,p->last_deaths);put32(b+52,p->finishes);
 put32(b+68,crc(b,68));
}
bool whg_decode(WhgProfile *p,const uint8_t *b,unsigned bytes){
 if(!b||bytes!=WHG_SAVE_BYTES||memcmp(b,"WHG1",4)||b[4]!=1||get32(b+68)!=crc(b,68))return false;
 if(b[5]>1||b[6]>=30||b[7]>=30||b[8]>WHG_TIP||b[9]>=whg_levels[b[7]].check_count||b[10]>44)return false;
 WhgProfile q;whg_defaults(&q);q.has_run=b[5];q.furthest=b[6];q.run.level=b[7];q.run.phase=b[8];q.run.checkpoint=b[9];q.run.timer=b[10];q.run.collected=b[11];
 q.run.x=(int16_t)get16(b+12);q.run.y=(int16_t)get16(b+14);q.run.frame=get32(b+16);q.run.elapsed=get32(b+20);q.run.deaths=get32(b+24);memcpy(q.run.coins,b+28,9);
 q.best_deaths=get32(b+40);q.best_ticks=get32(b+44);q.last_deaths=get32(b+48);q.finishes=get32(b+52);
 if(q.run.x<0||q.run.x>11000||q.run.y<0||q.run.y>8000||q.run.deaths>999999||q.finishes>999999||q.last_deaths>999999)return false;
 if(q.run.phase==WHG_DYING&&q.run.timer>26)return false;
 if(q.run.phase==WHG_TIP&&!whg_tips[q.run.level])return false;
 if(q.has_run&&(q.run.phase==WHG_FINISHED||q.furthest<q.run.level))return false;
 unsigned coins=0;for(unsigned i=0;i<72;i++){bool taken=whg_coin_taken(&q.run,i);if(i>=whg_levels[q.run.level].coin_count&&taken)return false;coins+=taken;}
 if(coins!=q.run.collected)return false;*p=q;return true;
}
