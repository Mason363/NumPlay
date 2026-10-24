#include "../src/app.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static color_t picture[320*240];
void *np_alloc(uint32_t n){(void)n;assert(!"Unexpected heap allocation");return NULL;}
static struct { uint32_t pre[8];color_t pixels[320*STRIP_H];uint32_t post[8]; } guarded;
void np_push(int x,int y,int w,int h,const color_t *p){assert(x==0&&w==320&&y>=0&&y+h<=240);memcpy(picture+y*320,p,(size_t)w*h*sizeof *p);for(unsigned i=0;i<8;i++)assert(guarded.pre[i]==0x1234abcd&&guarded.post[i]==0x1234abcd);}
static void bounds(const np_font_t *f,int x,int y,const char *s){for(unsigned j=0;s[j];j++){unsigned c=(unsigned char)s[j];assert(c>=f->first&&c<f->first+f->count);const np_glyph_t *g=&f->glyphs[c-f->first];if(g->w){assert(x+g->x>=0&&x+g->x+g->w<=320&&y+g->y>=0&&y+g->y+g->h<=240);}x+=g->advance;}}
int whg_checked_text(const np_font_t *f,int x,int y,const char *s,color_t c,int a){bounds(f,x,y,s);return gfx_text(f,x,y,s,c,a);}
void whg_checked_center(const np_font_t *f,int x,int y,const char *s,color_t c,int a){bounds(f,x-gfx_text_width(f,s)/2,y,s);gfx_text_center(f,x,y,s,c,a);}
void whg_checked_right(const np_font_t *f,int x,int y,const char *s,color_t c,int a){bounds(f,x-gfx_text_width(f,s),y,s);gfx_text_right(f,x,y,s,c,a);}
static void draw(WhgApp *a,const char *name){whg_prepare_draw(a);gfx_render(whg_draw,a,0,240);if(name){FILE *f=fopen(name,"wb");assert(f);fprintf(f,"P6\n320 240\n255\n");for(unsigned i=0;i<320*240;i++){unsigned c=picture[i];fputc(((c>>11)&31)*255/31,f);fputc(((c>>5)&63)*255/63,f);fputc((c&31)*255/31,f);}fclose(f);}}
int main(void){
 for(unsigned i=0;i<8;i++)guarded.pre[i]=guarded.post[i]=0x1234abcd;gfx_buf=guarded.pixels;
 WhgApp a={0};whg_defaults(&a.profile);draw(&a,"menu.ppm");
 for(int screen=0;screen<=UI_FINISH;screen++){a.screen=screen;for(int selected=0;selected<5;selected++){a.selected=selected;for(int cf=1;cf<=4;cf++){a.confirm=cf;draw(&a,NULL);}}}
 a.screen=UI_HELP;draw(&a,"help.ppm");a.screen=UI_CREDITS;draw(&a,"credits.ppm");
 for(unsigned level=0;level<30;level++){
  whg_load_level(&a.profile.run,level);a.profile.run.deaths=0;a.screen=UI_GAME;draw(&a,NULL);a.profile.run.phase=WHG_PLAY;
  for(unsigned frame=0;frame<12;frame++){a.profile.run.frame=frame*19;draw(&a,NULL);}char name[32];snprintf(name,sizeof name,"level-%02u.ppm",level+1);draw(&a,name);
  a.profile.run.deaths=999999;a.profile.run.phase=WHG_DYING;a.profile.run.timer=1;draw(&a,NULL);a.profile.run.phase=WHG_PLAY;
 }
 a.screen=UI_GAME;memset(&a.profile.run,0,sizeof a.profile.run);whg_load_level(&a.profile.run,0);a.profile.run.phase=WHG_PLAY;FILE *replay=fopen("level1-replay.bin","rb"),*motion=fopen("motion.raw","wb");assert(replay&&motion);int in;
 while((in=fgetc(replay))!=EOF){draw(&a,NULL);fwrite(picture,sizeof picture,1,motion);whg_step(&a.profile.run,(unsigned)in);}draw(&a,NULL);for(int i=0;i<12;i++)fwrite(picture,sizeof picture,1,motion);fclose(replay);fclose(motion);
 a.screen=UI_PAUSE;draw(&a,"pause.ppm");a.screen=UI_FINISH;draw(&a,"finish.ppm");puts("All native screens and 30 levels rendered with glyph/strip bounds guards; real level-one replay captured");return 0;
}
