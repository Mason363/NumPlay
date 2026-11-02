/* Bounded RFC 1951 decoder. A level's output is its own history window. */
#include "inflate.h"
#include <string.h>
typedef struct {const uint8_t *p,*end; uint32_t bits; unsigned count; bool bad;} Bits;
typedef struct {uint16_t counts[16],symbols[288];} Tree;
static unsigned bits(Bits *b,unsigned n){
    while(b->count<n){if(b->p==b->end){b->bad=true;return 0;}b->bits|=(uint32_t)*b->p++<<b->count;b->count+=8;}
    unsigned v=b->bits&((1u<<n)-1);b->bits>>=n;b->count-=n;return v;
}
static bool tree(Tree *t,const uint8_t *lengths,unsigned n){
    memset(t->counts,0,sizeof(t->counts));
    for(unsigned i=0;i<n;i++){if(lengths[i]>15)return false;t->counts[lengths[i]]++;}
    int left=1;for(unsigned i=1;i<=15;i++){left=(left<<1)-t->counts[i];if(left<0)return false;}
    uint16_t offsets[16]={0};for(unsigned i=1;i<15;i++)offsets[i+1]=offsets[i]+t->counts[i];
    for(unsigned i=0;i<n;i++)if(lengths[i])t->symbols[offsets[lengths[i]]++]=(uint16_t)i;
    return t->counts[0]!=n;
}
static int symbol(Bits *b,const Tree *t){
    unsigned code=0,first=0,index=0;
    for(unsigned n=1;n<=15;n++){code|=bits(b,1);unsigned count=t->counts[n];if(code>=first&&code-first<count)return t->symbols[index+code-first];index+=count;first=(first+count)<<1;code<<=1;}
    b->bad=true;return -1;
}
bool inflate_raw(const uint8_t *src,size_t length,uint8_t *out,size_t capacity){
    static const uint16_t lb[]={3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static const uint8_t le[]={0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    static const uint16_t db[]={1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    static const uint8_t de[]={0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
    Bits b={src,src+length,0,0,false};size_t used=0;bool final=false;Tree lit,dist;
    while(!final&&!b.bad){
        final=bits(&b,1);unsigned type=bits(&b,2);
        if(type==0){b.bits=0;b.count=0;if((size_t)(b.end-b.p)<4)return false;unsigned n=b.p[0]|b.p[1]<<8,inv=b.p[2]|b.p[3]<<8;b.p+=4;if((n^65535)!=inv||n>capacity-used||(size_t)(b.end-b.p)<n)return false;memcpy(out+used,b.p,n);b.p+=n;used+=n;continue;}
        uint8_t lens[320]={0};unsigned nl=288,nd=32;
        if(type==1){for(unsigned i=0;i<288;i++)lens[i]=i<144?8:i<256?9:i<280?7:8;for(unsigned i=288;i<320;i++)lens[i]=5;}
        else if(type==2){
            static const uint8_t order[]={16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
            nl=257+bits(&b,5);nd=1+bits(&b,5);unsigned nc=4+bits(&b,4);uint8_t cl[19]={0};Tree codes;
            if(nl>286||nd>32)return false;
            for(unsigned i=0;i<nc;i++)cl[order[i]]=bits(&b,3);
            if(!tree(&codes,cl,19))return false;
            unsigned n=0;while(n<nl+nd&&!b.bad){int s=symbol(&b,&codes);if(s<0)return false;if(s<16)lens[n++]=(uint8_t)s;else{unsigned repeat=s==16?3+bits(&b,2):s==17?3+bits(&b,3):11+bits(&b,7);if(s==16&&!n)return false;uint8_t value=s==16?lens[n-1]:0;if(n+repeat>nl+nd)return false;while(repeat--)lens[n++]=value;}}
        }else return false;
        if(!lens[256]||!tree(&lit,lens,nl)||!tree(&dist,lens+nl,nd))return false;
        for(;;){int s=symbol(&b,&lit);if(b.bad)return false;if(s<256){if(used==capacity)return false;out[used++]=(uint8_t)s;}else if(s==256)break;else{if(s>285)return false;unsigned n=lb[s-257]+bits(&b,le[s-257]);int d=symbol(&b,&dist);if(d<0||d>=30)return false;unsigned back=db[d]+bits(&b,de[d]);if(b.bad||back>used||n>capacity-used)return false;while(n--){out[used]=out[used-back];used++;}}}
    }
    return !b.bad&&used==capacity;
}
