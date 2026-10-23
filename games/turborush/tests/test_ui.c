#define NP_MODULE 1
#define main turbo_main
#include "../src/main.c"
#undef main
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <math.h>
#undef gfx_text
#undef gfx_text_center
#undef gfx_text_right
static void text_bounds(const np_font_t*f,int x,int y,const char*s){
 const char*original=s;int left=x;
 while(*s){unsigned c=(uint8_t)*s++;assert(c>=f->first&&c<f->first+f->count);const np_glyph_t*g=&f->glyphs[c-f->first];
   if(g->w&&(x+g->x<0||x+g->x+g->w>320||y+g->y<0||y+g->y+g->h>240)){fprintf(stderr,"Clipped text: %s at %d,%d\n",original,left,y);abort();}x+=g->advance;
 }
}
int tr_checked_text(const np_font_t*f,int x,int y,const char*s,color_t c,int alpha){text_bounds(f,x,y,s);return gfx_text(f,x,y,s,c,alpha);}
void tr_checked_center(const np_font_t*f,int x,int y,const char*s,color_t c,int alpha){tr_checked_text(f,x-gfx_text_width(f,s)/2,y,s,c,alpha);}
void tr_checked_right(const np_font_t*f,int x,int y,const char*s,color_t c,int alpha){tr_checked_text(f,x-gfx_text_width(f,s),y,s,c,alpha);}
static color_t framebuffer[320*240];
uint64_t eadk_timing_millis(void){return 1234;}
uint32_t eadk_random(void){return 1;}
uint64_t eadk_keyboard_scan(void){return KEY(eadk_key_home);}
void eadk_timing_msleep(uint32_t n){(void)n;}
void eadk_display_wait_for_vblank(void){}
const uint8_t*np_file_read(const char*n,uint32_t*len){(void)n;*len=0;return NULL;}
bool np_file_write(const char*n,const void*b,uint32_t len){(void)n;(void)b;(void)len;return true;}
void np_push(int x,int y,int w,int h,const color_t*p){for(int j=0;j<h;j++)memcpy(framebuffer+(y+j)*320+x,p+j*w,w*2);}
void *np_alloc(uint32_t n){(void)n;return strip;}
static void shot(const char*name){
 tr_prepare(&view,&race,&profile,ui_frame);gfx_buf=strip;gfx_render(scene,0,0,240);
 char path[120];snprintf(path,sizeof path,"%s.ppm",name);FILE*f=fopen(path,"wb");assert(f);fprintf(f,"P6\n320 240\n255\n");
 for(unsigned i=0;i<320*240;i++){unsigned c=framebuffer[i];uint8_t rgb[]={((c>>11)&31)*255/31,((c>>5)&63)*255/63,(c&31)*255/31};fwrite(rgb,1,3,f);}fclose(f);
}
int main(void){
 tr_defaults(&profile);tr_new(&race,&profile,0,1);screen=PLAY;shot("starting-grid");
 tr_defaults(&profile);tr_new(&race,&profile,0,1);race.cars[0].distance=320*256;race.cars[0].speed=40*256;race.rank=7;race.gear=3;race.phase=TR_RACING;race.time=8700;race.ticks=600;
 for(int i=1;i<8;i++)race.cars[i].distance=race.cars[0].distance+(i*40+10)*256;
 selected=0;screen=MENU;shot("menu");
 screen=SETUP;selected=6;shot("setup");screen=RECORDS;shot("records");
 screen=PLAY;for(int i=0;i<6;i++){race.track=i;race.mood=i%3;char name[50];snprintf(name,sizeof name,"circuit-%d",i+1);shot(name);}
 profile.money=1450;profile.upgrades[0]=2;profile.upgrades[1]=1;screen=GARAGE;selected=0;shot("garage");
 screen=HELP;for(int i=0;i<4;i++){help_page=i;char name[30];snprintf(name,sizeof name,"help-%d",i);shot(name);}
 screen=PAUSE;selected=0;shot("pause");screen=RESULT;championship=false;race.rank=1;race.phase=TR_FINISHED;prize=500;shot("result");
 screen=SAVE_ERROR;shot("save-error");
 for(int state=0;state<=CONFIRM;state++){
   screen=state;for(int sel=0;sel<(state==MENU?4:state==GARAGE?5:state==SETUP?7:state==RESULT?3:state==PAUSE?4:1);sel++){
     selected=sel;profile.money=999999;profile.races=999999;profile.wins=999999;race.cars[0].speed=90*256;race.nitro=1000;race.damage=1000;
     tr_prepare(&view,&race,&profile,0);gfx_render(scene,0,0,240);
   }
 }
 race.cars[1].distance=race.cars[0].distance+10*256;tr_prepare(&view,&race,&profile,0);bool visible=false;
 for(int i=0;i<view.count;i++)if(view.objects[i].kind==8&&view.objects[i].id==1){visible=true;assert(view.objects[i].w>25&&view.objects[i].w<50&&view.objects[i].y>120);}
 assert(visible);
 profile.mode=TR_FREE_RIDE;tr_new(&race,&profile,0,1);race.cars[0].speed=40*256;race.cars[0].distance=280*256;race.overtakes=7;
 screen=PLAY;shot("free-ride");screen=SETUP;selected=0;shot("free-ride-setup");
 screen=PAUSE;selected=0;shot("free-ride-pause");screen=CONFIRM;shot("free-ride-confirm");
 race.cars[0].x=600;race.camera_x=240;tr_prepare(&view,&race,&profile,0);assert(view.player_x>220);
 screen=PLAY;shot("camera-right");
 race.overtakes=9999999;race.distance_base=4000000000u;tr_prepare(&view,&race,&profile,0);gfx_render(scene,0,0,240);
 /* Render edge positions and all effects with a guard around the strip. */
 struct{uint32_t before[8];color_t pixels[320*16];uint32_t after[8];} guard;
 for(int i=0;i<8;i++)guard.before[i]=guard.after[i]=0x12345678;
 gfx_buf=guard.pixels;screen=PLAY;
 for(int t=0;t<6;t++)for(int mood=0;mood<3;mood++)for(int effect=0;effect<3;effect++)for(int x=-1600;x<=1600;x+=800){
   race.track=t;race.mood=mood;profile.effects=effect;race.cars[0].x=x;race.cars[0].distance=(900+x/4)*256;
   tr_prepare(&view,&race,&profile,ui_frame++);gfx_render(scene,0,0,240);
   for(int i=0;i<8;i++)assert(guard.before[i]==0x12345678&&guard.after[i]==0x12345678);
 }
 /* Perspective must be continuous across the world's 8 m cache boundary,
  * and fixed landmarks must grow and approach the tire contact plane. */
 tr_defaults(&profile);profile.mode=TR_FREE_RIDE;tr_new(&race,&profile,0,1);float x0,y0,s0,x1,y1,s1;
 for(int track=0;track<6;track++){
   race.track=track;race.cars[0].distance=320*256-1;tr_prepare(&view,&race,&profile,0);assert(tr_project(&view,400,10,&x0,&y0,&s0));
   int32_t ground[3]={view.rows[140].world,view.rows[170].world,view.rows[200].world};
   race.cars[0].distance+=2;tr_prepare(&view,&race,&profile,1);assert(tr_project(&view,400,10,&x1,&y1,&s1));
   assert(fabsf(x1-x0)<.05f&&fabsf(y1-y0)<.05f&&y1>y0&&s1>s0);
   for(int i=0;i<3;i++)assert(abs(view.rows[140+i*30].world-ground[i])<16);
   assert(fabsf(view.player_y-199.2f)<.02f&&fabsf(view.player_w-71.68f)<.02f);
   race.cars[0].distance=340*256;tr_prepare(&view,&race,&profile,2);assert(tr_project(&view,400,10,&x1,&y1,&s1));assert(y1>y0&&s1>s0);
   for(int at=0;at<2000;at+=17){race.cars[0].distance=at*256;tr_prepare(&view,&race,&profile,at);TrRow row=view.rows[(int)view.player_y];assert(row.left<view.player_x&&row.right>view.player_x);}
 }
 /* Export actual render frames: the preview exercises steering, acceleration,
  * bends and interpolation rather than presenting a still image as motion QA. */
 tr_new(&race,&profile,0,42);race.cars[0].distance=240*256;race.cars[0].speed=42*256;
 for(int i=1;i<8;i++)race.cars[i].distance=race.cars[0].distance+i*75*256;
 FILE*motion=fopen("motion.raw","wb");assert(motion);TrRace previous=race,draw;unsigned accum=0;float last_x=160;
 gfx_buf=strip;screen=PLAY;
 for(int frame=0;frame<240;frame++){
   accum+=33;int target=frame<80?-450:frame<160?450:0;
   while(accum>=20){int predict=race.cars[0].x+race.cars[0].lateral*6;unsigned input=TR_THROTTLE;if(predict>target+20)input|=TR_LEFT;if(predict<target-20)input|=TR_RIGHT;
     previous=race;tr_step(&race,&profile,input,0);accum-=20;}
   tr_interpolate(&draw,&previous,&race,accum*50);tr_prepare(&view,&draw,&profile,frame);assert(fabsf(view.player_x-last_x)<8);last_x=view.player_x;
   gfx_render(scene,0,0,240);assert(fwrite(framebuffer,2,320*240,motion)==320*240);
 }
 fclose(motion);puts("270 guarded renders, text bounds, world continuity and 240 motion frames passed");return 0;
}
