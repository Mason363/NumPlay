#include "game.h"
#include <string.h>
static uint32_t crc(const uint8_t*p,unsigned n){uint32_t c=~0u;while(n--){c^=*p++;for(unsigned j=0;j<8;j++)c=(c>>1)^(0xedb88320u&-(c&1));}return~c;}
static void put(uint8_t*p,uint32_t v,unsigned n){for(unsigned i=0;i<n;i++)p[i]=(uint8_t)(v>>(8*i));}
static uint32_t get(const uint8_t*p,unsigned n){uint32_t v=0;for(unsigned i=0;i<n;i++)v|=(uint32_t)p[i]<<(i*8);return v;}
void save_encode(const Progress*p,uint8_t*out){
    memset(out,0,SAVE_BYTES);memcpy(out,"ABRD",4);out[4]=2;out[5]=LEVEL_COUNT&255;out[6]=LEVEL_COUNT>>8;unsigned at=8;
    for(unsigned i=0;i<LEVEL_COUNT;i++,at+=4)put(out+at,p->best[i],4);
    memcpy(out+at,p->stars,LEVEL_COUNT);at+=LEVEL_COUNT;memcpy(out+at,p->feathers,sizeof(p->feathers));at+=sizeof(p->feathers);
    for(unsigned i=0;i<GROUP_COUNT;i++,at+=2)put(out+at,p->unlocked[i],2);put(out+at,p->last,2);out[at+2]=p->help;
    put(out+SAVE_BYTES-4,crc(out,SAVE_BYTES-4),4);
}
bool save_decode(Progress*p,const uint8_t*in,unsigned n){
    if(!in||n!=SAVE_BYTES||memcmp(in,"ABRD",4)||(in[4]!=1&&in[4]!=2)||get(in+5,2)!=LEVEL_COUNT||get(in+SAVE_BYTES-4,4)!=crc(in,SAVE_BYTES-4))return false;
    unsigned at=8;for(unsigned i=0;i<LEVEL_COUNT;i++,at+=4){uint32_t v=get(in+at,4);if(in[4]==1?v>100000000:(v>>25)>100)return false;}
    for(unsigned i=0;i<LEVEL_COUNT;i++)if(in[at+i]>3)return false;at+=LEVEL_COUNT+sizeof(p->feathers);
    for(unsigned i=0;i<GROUP_COUNT;i++,at+=2)if(get(in+at,2)<1||get(in+at,2)>groups[i].count)return false;
    if(get(in+at,2)>=LEVEL_COUNT||in[at+2]>1)return false;
    at=8;for(unsigned i=0;i<LEVEL_COUNT;i++,at+=4){uint32_t v=get(in+at,4);p->best[i]=in[4]==1?(v>SCORE_MASK?SCORE_MASK:v):v;}memcpy(p->stars,in+at,LEVEL_COUNT);at+=LEVEL_COUNT;memcpy(p->feathers,in+at,sizeof(p->feathers));at+=sizeof(p->feathers);
    if(in[4]==1)for(unsigned i=0;i<LEVEL_COUNT;i++)if(p->feathers[i>>3]&(1u<<(i&7)))p->best[i]|=100u<<25;
    for(unsigned i=0;i<GROUP_COUNT;i++,at+=2)p->unlocked[i]=(uint16_t)get(in+at,2);p->last=(uint16_t)get(in+at,2);p->help=in[at+2];return true;
}
