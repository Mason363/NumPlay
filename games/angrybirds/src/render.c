#include "render.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../../launcher/src/font.h"
#ifdef ANGRY_TEST
#include <assert.h>
#endif
/* Only an eight-line RGB565 strip is in RAM, never a screen-sized image. */
static uint16_t strip[320*8];
static uint32_t hashes[30];static int top;static bool invalid=true;
typedef struct {int32_t u,v,du,dv;int16_t x0,y0,x1,y1;uint16_t id;} DrawItem;
static DrawItem items[BODY_CAP+2];static unsigned item_count;
#define C(r,g,b) (((r)>>3)<<11|((g)>>2)<<5|((b)>>3))
enum { WHITE=65535,BLACK=0,INK=C(55,34,20),YELLOW=C(255,214,44),CREAM=C(255,239,164),GREY=C(113,119,118),GREEN=C(91,165,50) };
static void pixel(int x,int y,uint16_t colour){if((unsigned)x<320&&(unsigned)(y-top)<8)strip[(y-top)*320+x]=colour;}
static void rect(int x,int y,int w,int h,uint16_t colour){
    int left=x<0?0:x,right=x+w>320?320:x+w,start=y<top?top:y,end=y+h>top+8?top+8:y+h;
    for(int yy=start;yy<end;yy++)for(int xx=left;xx<right;xx++)strip[(yy-top)*320+xx]=colour;
}
static void box(int x,int y,int w,int h,uint16_t colour){rect(x+2,y,w-4,h,INK);rect(x,y+2,w,h-4,INK);rect(x+3,y+2,w-6,h-4,colour);rect(x+2,y+3,w-4,h-6,colour);}
static bool clip_edge(float p,float q,float*first,float*last){if(p==0)return q>=0;float t=q/p;if(p<0){if(t>*last)return false;if(t>*first)*first=t;}else{if(t<*first)return false;if(t<*last)*last=t;}return true;}
static void line(int x0,int y0,int x1,int y1,uint16_t c){
    /* Clip before walking: distant sling/explosion endpoints must not stall input. */
    float dx=(float)x1-x0,dy=(float)y1-y0,first=0,last=1;
    if(!clip_edge(-dx,(float)x0,&first,&last)||!clip_edge(dx,319.0f-x0,&first,&last)||!clip_edge(-dy,(float)y0-top,&first,&last)||!clip_edge(dy,top+7.0f-y0,&first,&last))return;
    int endx=(int)lroundf(x0+last*dx),endy=(int)lroundf(y0+last*dy);x0=(int)lroundf(x0+first*dx);y0=(int)lroundf(y0+first*dy);x1=endx;y1=endy;
    int ax=abs(x1-x0),sx=x0<x1?1:-1,ay=-abs(y1-y0),sy=y0<y1?1:-1,error=ax+ay;
    for(unsigned steps=0;steps<321;steps++){pixel(x0,y0,c);if(x0==x1&&y0==y1)break;int e=2*error;if(e>=ay){error+=ay;x0+=sx;}if(e<=ax){error+=ax;y0+=sy;}}
}
static uint16_t sample(unsigned id,int x,int y){const Sprite*s=&sprites[id];if(x<0||y<0||x>=s->w||y>=s->h)return 0;unsigned at=(unsigned)y*s->w+x;uint8_t byte=art_pixels[s->offset+at/2];return(byte>>((at&1)*4))&15;}
static void image(unsigned id,int x,int y,int w,int h){
    if(id>=SPRITE_COUNT||w<=0||h<=0||y+h<=top||y>=top+8)return;const Sprite*s=&sprites[id];int yy=y<top?top:y,end=y+h<top+8?y+h:top+8;
    for(;yy<end;yy++){int py=(yy-y)*s->h/h;for(int xx=x<0?0:x;xx<x+w&&xx<320;xx++){unsigned index=sample(id,(xx-x)*s->w/w,py);if(index)pixel(xx,yy,s->palette[index]);}}
}
static uint16_t text_ink=WHITE;
static const np_glyph_t*glyph(unsigned c){if(c<32||c>126)c='?';return &np_font_body.glyphs[c-32];}
static int text_width(const char*t){int w=0;for(;*t;t++)w+=glyph((unsigned char)*t)->advance;return w;}
static uint16_t blend(uint16_t bg,uint16_t fg,unsigned a){unsigned r=(((bg>>11)*(15-a)+(fg>>11)*a)+7)/15,g=((((bg>>5)&63)*(15-a)+((fg>>5)&63)*a)+7)/15,b=(((bg&31)*(15-a)+(fg&31)*a)+7)/15;return(uint16_t)((r<<11)|(g<<5)|b);}
static void text(int x,int y,const char*t){
#ifdef ANGRY_TEST
    assert(x>=0&&x+text_width(t)<=320&&y>=0&&y+13<=240);
#endif
    if(y+13<=top||y>=top+8)return;
    for(;*t;t++){const np_glyph_t*g=glyph((unsigned char)*t);int xx=x+g->x,yy=y+np_font_body.ascent+g->y;
        for(unsigned j=0;j<g->h;j++){int row=yy+(int)j;if(row<top||row>=top+8)continue;for(unsigned i=0;i<g->w;i++){int col=xx+(int)i;if((unsigned)col>=320)continue;unsigned at=j*g->w+i,a=(np_font_body.data[g->offset+at/2]>>((at&1)*4))&15;if(a){unsigned p=(unsigned)(row-top)*320+col;strip[p]=a==15?text_ink:blend(strip[p],text_ink,a);}}}x+=g->advance;
    }
}
static void center(int y,const char*t){text((320-text_width(t))/2,y,t);}
static void star(int x,int y,int r,bool bright){
    if(y+r<top||y-r>=top+8)return;
    int px[10],py[10];for(int i=0;i<10;i++){float a=i*PI/5-PI/2;float d=i&1?r*.43f:r;px[i]=x+(int)lroundf(cosf(a)*d);py[i]=y+(int)lroundf(sinf(a)*d);}
    for(int yy=y-r;yy<=y+r;yy++)for(int xx=x-r;xx<=x+r;xx++){bool in=false;for(int i=0,j=9;i<10;j=i++)if((py[i]>yy)!=(py[j]>yy)&&xx<(px[j]-px[i])*(yy-py[i])/(float)(py[j]-py[i])+px[i])in=!in;if(in)pixel(xx,yy,bright?YELLOW:GREY);}
    for(int i=0;i<10;i++)line(px[i],py[i],px[(i+1)%10],py[(i+1)%10],INK);
}
static int sx(const Game*g,float x){return(int)lroundf(160+(x-g->camera_x)*g->zoom);}
static int sy(const Game*g,float y){return(int)lroundf(120+(y-g->camera_y)*g->zoom);}
static void prepare_body(const Game*g,const Body*b){
    const ObjectDef*d=&object_defs[b->type];unsigned frame=0;float health=d->health>0?b->hp/d->health:1;
    if(d->kind==BIRD){if(b->flags&FIRED)frame=b->type==OBJ_BIRD_REDBIG?1:2;if(frame>=d->frames)frame=0;}
    else if(health<1&&d->frames)frame=(unsigned)fminf(d->frames-1,floorf((1-health)*d->frames));
    unsigned id=d->sprite[frame];if(id>=SPRITE_COUNT)return;const Sprite*s=&sprites[id];const Shape*q=&shapes[d->shape];float ox=b->x-q->cx*b->c+q->cy*b->s,oy=b->y-q->cx*b->s-q->cy*b->c;
    float scale=g->zoom/5,c=b->c,ss=b->s,px=s->px/16.0f,py=s->py/16.0f;float xx=160+(ox-g->camera_x)*g->zoom,yy=120+(oy-g->camera_y)*g->zoom;
    float centerx=xx+((s->w*.5f-px)*c-(s->h*.5f-py)*ss)*scale,centery=yy+((s->w*.5f-px)*ss+(s->h*.5f-py)*c)*scale;
    float ex=(fabsf(c)*s->w+fabsf(ss)*s->h)*scale*.5f,ey=(fabsf(ss)*s->w+fabsf(c)*s->h)*scale*.5f;
    int x0=(int)floorf(centerx-ex),x1=(int)ceilf(centerx+ex),y0=(int)floorf(centery-ey),y1=(int)ceilf(centery+ey);
    if(y1<=0||y0>=240||x1<0||x0>319)return;if(x0<0)x0=0;if(x1>320)x1=320;if(y0<0)y0=0;if(y1>240)y1=240;
    if(scale<.01f||item_count>=BODY_CAP+2)return;float inv=1/scale,dx=(x0+.5f-xx)*inv,dy=(y0+.5f-yy)*inv;
    items[item_count++]=(DrawItem){(int32_t)lroundf((dx*c+dy*ss+px)*65536),(int32_t)lroundf((-dx*ss+dy*c+py)*65536),(int32_t)lroundf(c*inv*65536),(int32_t)lroundf(-ss*inv*65536),(int16_t)x0,(int16_t)y0,(int16_t)x1,(int16_t)y1,(uint16_t)id};
}
static void paint_items(void){
    for(unsigned i=0;i<item_count;i++){const DrawItem*d=&items[i];if(d->y1<=top||d->y0>=top+8)continue;const Sprite*s=&sprites[d->id];int start=d->y0<top?top:d->y0,end=d->y1<top+8?d->y1:top+8;
        for(int y=start;y<end;y++){int32_t u=d->u-d->dv*(y-d->y0),v=d->v+d->du*(y-d->y0);uint16_t*row=strip+(y-top)*320;
            for(int x=d->x0;x<d->x1;x++,u+=d->du,v+=d->dv){unsigned px=(uint32_t)u>>16,py=(uint32_t)v>>16;if(px<s->w&&py<s->h){unsigned at=py*s->w+px,index=(art_pixels[s->offset+at/2]>>((at&1)*4))&15;if(index)row[x]=s->palette[index];}}
        }
    }
}
static void prepare_items(const Game*g){
    item_count=0;bool aiming=!g->active&&(g->launched<g->birds||g->eagle_armed)&&!g->won&&!g->lost;
    if(aiming){Body ready=g->bodies[g->launched+1];if(g->eagle_armed){ready.type=OBJ_BIRD_SARDINE;ready.hp=object_defs[ready.type].health;}const Shape*q=&shapes[object_defs[ready.type].shape];ready.x=g->slingx-cosf(g->angle)*5*g->power+q->cx;ready.y=g->slingy+sinf(g->angle)*5*g->power+q->cy;ready.c=1;ready.s=0;ready.flags=ALIVE|WAITING;prepare_body(g,&ready);}
    for(unsigned layer=0;layer<2;layer++)for(unsigned i=1;i<g->count;i++){const Body*b=&g->bodies[i];const ObjectDef*d=&object_defs[b->type];if(!(b->flags&ALIVE)||((d->kind==DECOR)!=(layer==1)))continue;
        if(b->flags&WAITING){if(i<=(unsigned)g->launched+1)continue;Body queue=*b;const Shape*q=&shapes[d->shape];queue.x=g->slingx-6-(i-g->launched-2)*2.5f+q->cx;queue.y=g->slingy+8.5f-q->radius+q->cy;queue.c=1;queue.s=0;prepare_body(g,&queue);}else prepare_body(g,b);
    }
    if(g->eagle_timer){float t=(180-g->eagle_timer)/180.0f;Body eagle={0};eagle.type=OBJ_BIRD_ME;eagle.flags=ALIVE;eagle.hp=2;eagle.x=g->eagle_x-90+t*180;eagle.y=g->eagle_y-30*sinf(t*PI);body_refresh(&eagle);prepare_body(g,&eagle);}
}
static void background(const Game*g,bool menu){
    unsigned id=menu?SPR_MENU_BG:background_ids[levels[g->level].theme];const Sprite*s=&sprites[id];int ground=sy(g,0);
    int previous=-1,phase=(int)(g->camera_x*.15f)%160;if(phase<0)phase+=160;
    for(int y=top;y<top+8;y++){
        uint16_t*dst=strip+(y-top)*320;
        if(menu){const uint8_t*row=art_pixels+s->offset+y*160;for(int x=0;x<320;x+=2){uint8_t p=row[x/2];dst[x]=s->palette[p&15];dst[x+1]=s->palette[p>>4];}}
        else{
            int v=(y-ground+210)/2;if(v<0)v=0;if(v>=120)v=119;
            if(previous==v){memcpy(dst,dst-320,640);continue;}previous=v;const uint8_t*row=art_pixels+s->offset+v*80;
            for(int x=0;x<320;x+=2){unsigned u=x/2+phase;if(u>=160)u-=160;uint8_t p=row[u/2];uint16_t colour=s->palette[(p>>((u&1)*4))&15];dst[x]=dst[x+1]=colour;}
        }
    }
}
static void play(const Game*g){
    background(g,false);
    int slx=sx(g,g->slingx),sly=sy(g,g->slingy);int width=(int)(g->zoom*1.1f),height=(int)(g->zoom*8.8f);
    rect(slx-width/2,sly+2,width,height,C(92,45,21));line(slx-width/2,sly+2,slx-width,sly-3,C(68,32,16));line(slx+width/2,sly+2,slx+width,sly-2,C(68,32,16));
    bool aiming=!g->active&&(g->launched<g->birds||g->eagle_armed)&&!g->won&&!g->lost;
    if(aiming){float dx=cosf(g->angle)*5*g->power,dy=-sinf(g->angle)*5*g->power;int bx=sx(g,g->slingx-dx),by=sy(g,g->slingy-dy);line(slx-width,sly-3,bx,by,C(45,23,14));
        line(bx,by,slx+width,sly-2,C(72,33,18));
        float speed=!g->eagle_armed&&g->bodies[g->launched+1].type==OBJ_BIRD_REDBIG?50:46.25f;
        for(unsigned i=1;i<=14;i++){float t=i*.075f,x=g->slingx-dx+speed*g->power*cosf(g->angle)*t,y=g->slingy-dy-speed*g->power*sinf(g->angle)*t+10*t*t;if(y>0)break;rect(sx(g,x),sy(g,y),2,2,WHITE);}
    }
    paint_items();
    if(g->explosion_age){int r=(int)((26-g->explosion_age%26)*g->zoom*.25f)+2;int cx=sx(g,g->explosion_x),cy=sy(g,g->explosion_y);for(int a=0;a<36;a++){float t=a*PI/18;line(cx+(int)(cosf(t)*r),cy+(int)(sinf(t)*r),cx+(int)(cosf(t)*r*1.4f),cy+(int)(sinf(t)*r*1.4f),YELLOW);}}
    rect(0,0,320,24,C(81,57,37));text_ink=WHITE;image(SPR_LEVELPLAY_BUTTON_PAUSE,3,2,20,20);char t[60];snprintf(t,sizeof(t),"LEVEL %u-%u",g->group+1,g->level-groups[g->group].first+1);text(30,5,t);snprintf(t,sizeof(t),"SCORE %u",(unsigned)g->score);text(312-text_width(t),5,t);
    rect(0,217,320,23,C(81,57,37));
    if(aiming){snprintf(t,sizeof(t),"AIM %d  POWER %d%%  OK: FIRE",(int)lroundf(g->angle*180/PI),(int)lroundf(g->power*100));center(218,t);}
    else center(218,"OK: POWER   1: EAGLE   2: VIEW");
}
static void button(int y,const char*t,bool selected){box(72,y,176,27,selected?YELLOW:CREAM);text_ink=INK;text(160-text_width(t)/2,y+6,t);text_ink=WHITE;}
static void episode_name(int x,int y,const char*name){
    /* Split by measured pixels rather than character count. */
    char first[40]={0};unsigned n=0,last_space=0;while(name[n]&&n<sizeof(first)-1){first[n]=name[n];first[n+1]=0;if(text_width(first)>94){first[n]=0;break;}if(name[n]==' ')last_space=n;n++;}
    if(!name[n]){text(x,y,first);return;}if(last_space){first[last_space]=0;n=last_space+1;}text(x,y,first);while(name[n]==' ')n++;text(x,y+14,name+n);
}
void render_invalidate(void){invalid=true;}
void render(const Game*g,unsigned view,unsigned selected,bool failed,DisplayPush push){
    bool in_game=view==PLAY||view==PAUSE||view==RESULT||(view==QUIT&&g->count>0);if(in_game)prepare_items(g);
    for(top=0;top<240;top+=8){
        if(in_game)play(g);else background(g,true);
        char t[80];text_ink=WHITE;
        if(view==TITLE){
            const Sprite*logo=&sprites[SPR_CLASSIC_AB_LOGO];image(SPR_CLASSIC_AB_LOGO,(320-logo->w)/2,14,logo->w,logo->h);image(SPR_MAINMENU_BUTTON_PLAY_NORMAL,121,99,78,48);box(37,174,246,25,INK);center(180,"1: CONTINUE   0: HOW TO PLAY");rect(0,217,320,23,INK);text(10,222,"By Bobydeluxe");
        }else if(view==EPISODES){
            rect(0,0,320,25,INK);center(6,"SELECT EPISODE");unsigned page=selected/6;
            for(unsigned k=0;k<6;k++){unsigned i=page*6+k;if(i>=GROUP_COUNT)break;int x=8+(k%2)*156,y=29+(k/2)*60;box(x,y,148,56,i==selected?YELLOW:CREAM);image(groups[i].image,x+3,y+5,45,43);text_ink=INK;snprintf(t,sizeof(t),"%u levels",groups[i].count);text(x+51,y+38,t);episode_name(x+50,y+5,groups[i].name);
            }text_ink=WHITE;rect(0,217,320,23,INK);snprintf(t,sizeof(t),"%u / %u   ARROWS: SELECT   OK",page+1,(GROUP_COUNT+5)/6);center(222,t);
        }else if(view==STAGES){
            rect(0,0,320,25,INK);center(6,groups[g->group].name);unsigned page=selected/15;
            for(unsigned k=0;k<15;k++){unsigned n=page*15+k;if(n>=groups[g->group].count)break;unsigned l=groups[g->group].first+n;int x=15+(k%5)*60,y=32+(k/5)*55;bool open=n<g->p.unlocked[g->group];box(x,y,51,47,n==selected?YELLOW:open?CREAM:GREY);text_ink=INK;snprintf(t,sizeof(t),"%u",n+1);text(x+25-text_width(t)/2,y+5,t);for(unsigned j=0;j<3;j++)star(x+13+j*12,y+31,5,g->p.stars[l]>j);if(g->p.feathers[l>>3]&(1u<<(l&7)))rect(x+42,y+3,4,8,WHITE);}
            text_ink=WHITE;rect(0,194,320,46,INK);
            unsigned l=groups[g->group].first+selected;snprintf(t,sizeof(t),"BEST %u   EAGLE %u%%",(unsigned)BEST_SCORE(&g->p,l),(unsigned)BEST_EAGLE(&g->p,l));center(200,t);
            snprintf(t,sizeof(t),"PAGE %u / %u   OK: PLAY   BACK",page+1,(groups[g->group].count+14)/15);center(222,t);
        }else if(view==PAUSE){rect(0,0,320,240,C(83,111,124));center(25,"PAUSED");button(58,"RESUME",selected==0);button(91,"RETRY",selected==1);button(124,"LEVELS",selected==2);button(157,"QUIT GAME",selected==3);center(211,"UP/DOWN: SELECT   OK");
        }else if(view==RESULT){
            rect(41,28,238,178,INK);rect(44,31,232,172,CREAM);text_ink=INK;center(40,g->won?g->feather?"MIGHTY EAGLE!":"LEVEL CLEARED!":"LEVEL FAILED");
            unsigned stars=g->score>=levels[g->level].gold?3:g->score>=levels[g->level].silver?2:1;
            if(g->feather){snprintf(t,sizeof(t),"%u%% DESTRUCTION",g->eagle_percent);center(80,t);}else for(unsigned i=0;i<3;i++)star(118+i*42,83,18,g->won&&i<stars);snprintf(t,sizeof(t),"SCORE %u",(unsigned)g->score);center(112,t);snprintf(t,sizeof(t),"BEST %u",(unsigned)BEST_SCORE(&g->p,g->level));center(133,t);
            image(SPR_LEVELOVERWIN_BUTTON_RESTART,76,158,40,36);image(SPR_LEVELOVERWIN_BUTTON_MENU,140,158,40,36);image(SPR_LEVELOVERWIN_BUTTON_NEXTLEVEL,204,158,40,36);rect(74+(int)selected*64,197,44,3,INK);rect(0,217,320,23,INK);text_ink=WHITE;center(221,"LEFT/RIGHT: SELECT   OK");
        }else if(view==HELP){
            rect(10,13,300,210,INK);center(22,"HOW TO PLAY");text(24,48,"UP/DOWN: AIM THE SLINGSHOT");text(24,67,"LEFT/RIGHT: CHANGE POWER");text(24,86,"OK / EXE: LAUNCH");text(24,105,"OK IN FLIGHT: BIRD POWER");text(24,124,"2: ZOOM OUT / FOLLOW");text(24,143,"1: AIM THE MIGHTY EAGLE");text(24,162,"BACK: PAUSE   HOME: SAVE & EXIT");text(24,181,"CLEAR PIGS. SAVE BIRDS FOR POINTS.");center(204,"OK / BACK: RETURN");
        }else if(view==QUIT){rect(30,44,260,150,INK);center(61,"SAVE AND QUIT?");button(91,"KEEP PLAYING",selected==0);button(128,"QUIT GAME",selected==1);
        }else if(view==SAVE_ERROR){rect(18,35,284,167,INK);center(53,"STORAGE IS FULL");center(87,"OK: RETRY SAVE AND EXIT");center(115,"1: RETURN TO THE MENU");center(143,"BACK: EXIT WITHOUT SAVING");}
        else if(view==STORY){rect(0,0,320,240,INK);if(selected<SPRITE_COUNT){const Sprite*s=&sprites[selected];image(selected,(320-s->w)/2,(210-s->h)/2,s->w,s->h);}center(221,"OK / BACK: CONTINUE");}
        else if(view==LOADING){rect(25,75,270,90,INK);center(92,"LOADING LEVEL");center(121,"BACK: CANCEL   HOME: EXIT");}
        if(failed&&view!=SAVE_ERROR){rect(310,29,6,6,YELLOW);}
        uint32_t hash=2166136261u;for(unsigned i=0;i<320*8;i++)hash=(hash^strip[i])*16777619u;unsigned band=(unsigned)top/8;if(invalid||hash!=hashes[band]){push(0,top,320,8,strip);hashes[band]=hash;}
    }invalid=false;
}
