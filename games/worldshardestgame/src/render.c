#include "app.h"
#include <string.h>
#define BLACK HEX(0x000000)
#define WHITE HEX(0xffffff)
#define RED HEX(0xf02525)
#define BLUE HEX(0x3030ff)
static void number(char *s,uint32_t v){char t[12];unsigned n=0;do{t[n++]=(char)('0'+v%10);v/=10;}while(v);for(unsigned i=0;i<n;i++)s[i]=t[n-i-1];s[n]=0;}
static void body(int x,int y,const char *s,color_t c){gfx_text(&np_font_body,x,y,s,c,256);}
static void small(int x,int y,const char *s,color_t c){gfx_text(&np_font_small,x,y,s,c,256);}
static void center(int x,int y,const char *s,color_t c){gfx_text_center(&np_font_body,x,y,s,c,256);}
static void title(int y,const char *s){gfx_text_center(&np_font_title,160,y,s,BLACK,256);}
static int sx(int x){return (x*8+137)/275;}
static int sy(int y){return 3+(y*233+4000)/8000;}
void whg_prepare_draw(WhgApp *a){unsigned level=a->profile.run.level;for(unsigned i=0;i<whg_levels[level].enemy_count;i++)whg_enemy(level,i,a->profile.run.frame,&a->enemy_x[i],&a->enemy_y[i]);}
static void paper(void){gfx_vgrad(0,0,320,222,0xd8d8ff,0xffffff);gfx_fill(0,222,320,18,BLACK);}
static void footer(const char *left,const char *right){gfx_fill(0,222,320,18,BLACK);small(7,235,left,WHITE);gfx_text_right(&np_font_small,313,235,right,WHITE,256);}
static void title_art(void){
 if(gfx_y0>=97||gfx_y1<=19)return;
 unsigned p=0,at=0;
 while(p<whg_title_size){unsigned n=whg_title_data[p++];color_t color=whg_title_palette[whg_title_data[p++]];
  while(n){unsigned x=at%300,take=WHG_MIN(n,300-x);if(whg_title_data[p-1])gfx_fill(10+(int)x,19+(int)(at/300),(int)take,1,color);at+=take;n-=take;}
 }
}
static void outline_center(int x,int y,const char *s,color_t c){
 int left=x-gfx_text_width(&np_font_body,s)/2;
 for(int yy=-1;yy<=1;yy++)for(int xx=-1;xx<=1;xx++)if(xx||yy)body(left+xx,y+yy,s,BLACK);
 body(left,y,s,c);
}
static void home(const WhgApp *a){
 paper();title_art();gfx_text_right(&np_font_small,307,112,"VERSION 1.0",BLACK,256);
 static const char *const top[]={"PLAY","RECORDS","HOW TO","CREDITS"},*const bottom[]={"GAME","","PLAY",""};
 static const color_t colors[]={HEX(0xff3030),HEX(0x5e63ff),HEX(0x5edb65),HEX(0xe5d32d)};
 for(unsigned i=0;i<4;i++){
  int x=41+(int)i*79;if(a->selected==(int)i){gfx_fill(7+(int)i*79,133,69,55,WHITE);gfx_fill(7+(int)i*79,187,69,3,colors[i]);}
  outline_center(x,153,i==0&&a->profile.has_run?"CONTINUE":top[i],colors[i]);
  outline_center(x,175,i==0&&a->profile.has_run?"RUN":bottom[i],colors[i]);
 }
 gfx_text_center(&np_font_small,160,214,a->profile.has_run?"Arrows / OK    EXE: new game":"Arrows to select / OK to start",BLACK,256);
 footer("By Bobydeluxe","SNUBBY LAND");
}
static void dots(int x,int y,color_t fill,int alpha){gfx_circle(x,y,4,BLACK,alpha);gfx_circle(x,y,2,fill,alpha);}
static void field(const WhgApp *a){
 const WhgRun *r=&a->profile.run;const WhgLevel *l=&whg_levels[r->level];gfx_fill(0,0,320,240,whg_palette[0]);
 int y=WHG_MAX(3,gfx_y0),last=WHG_MIN(236,gfx_y1);
 while(y<last){unsigned row=whg_row_ids[r->level*233+(unsigned)(y-3)];int end=y+1;
  while(end<last&&whg_row_ids[r->level*233+(unsigned)(end-3)]==row)end++;
  unsigned at=whg_row_offsets[row];int x=0;
  while(x<320){unsigned n=whg_row_data[at++];color_t c=whg_palette[whg_row_data[at++]];gfx_fill(x,y,(int)n,end-y,c);x+=(int)n;}
  y=end;
 }
 for(unsigned i=0;i<l->coin_count;i++)if(!whg_coin_taken(r,i)){
  const WhgCoin *c=&whg_coins[l->coin_offset+i];int y0=sy(c->y);if(y0+5>=gfx_y0&&y0-5<gfx_y1)dots(sx(c->x),y0,HEX(0xffff00),256);
 }
 static const color_t material[]={HEX(0x0000ff),HEX(0x642bbb),HEX(0xc91d1d)};
 for(unsigned i=0;i<l->enemy_count;i++){
  int y0=sy(a->enemy_y[i]);if(y0+5>=gfx_y0&&y0-5<gfx_y1)dots(sx(a->enemy_x[i]),y0,material[whg_enemies[l->enemy_offset+i].kind],256);
 }
 int alpha=r->phase==WHG_DYING?WHG_MAX(0,WHG_MIN(256,((int)r->timer-1)*256/25)):256;
 int px=sx(r->x),py=sy(r->y);gfx_fill_alpha(px-5,py-5,11,11,BLACK,alpha);gfx_fill_alpha(px-4,py-4,9,9,HEX(0xff0000),alpha);
 char text[16];gfx_fill(0,0,320,19,BLACK);small(6,14,"BACK: MENU",WHITE);number(text,r->level+1);strcat(text,"/30");gfx_text_center(&np_font_small,157,14,text,WHITE,256);
 number(text,r->deaths);gfx_text_right(&np_font_small,314,14,text,WHITE,256);gfx_text_right(&np_font_small,314-gfx_text_width(&np_font_small,text)-4,14,"DEATHS",WHITE,256);
 char tally[20],n[12];number(tally,r->collected);strcat(tally,"/");number(n,l->coin_count);strcat(tally,n);strcat(tally," COINS");footer(tally,"ARROWS: MOVE");
}
static void header(const char *s){paper();title(39,s);gfx_fill(15,49,290,2,BLACK);}
static void help(void){
 header("HOW TO PLAY");
 gfx_fill(20,65,12,12,BLACK);gfx_fill(21,66,10,10,HEX(0xff0000));body(44,77,"You are the red square.",BLACK);
 dots(26,99,BLUE,256);body(44,104,"Avoid the enemy circles.",BLACK);
 dots(26,126,HEX(0xffff00),256);body(44,131,"Collect all the yellow coins.",BLACK);
 gfx_fill(20,148,13,13,HEX(0xb5feb4));body(44,159,"Reach the green finish zone.",BLACK);
 small(18,181,"Other green zones are checkpoints.",BLACK);small(18,198,"A death resets coins, but keeps your checkpoint.",BLACK);
 footer("ARROWS: MOVE","BACK: RETURN");
}
static void records(const WhgApp *a){
 header("RECORDS");char s[20];const WhgProfile *p=&a->profile;
 body(20,81,"Fewest deaths",BLACK);if(p->finishes)number(s,p->best_deaths);else strcpy(s,"--");gfx_text_right(&np_font_title,300,83,s,BLUE,256);
 body(20,116,"Last completed run",BLACK);if(p->finishes)number(s,p->last_deaths);else strcpy(s,"--");gfx_text_right(&np_font_body,300,116,s,BLACK,256);
 body(20,151,"Runs completed",BLACK);number(s,p->finishes);gfx_text_right(&np_font_body,300,151,s,BLACK,256);
 body(20,186,"Furthest level",BLACK);number(s,p->furthest+1);strcat(s,"/30");gfx_text_right(&np_font_body,300,186,s,BLACK,256);
 footer("Local records","BACK: RETURN");
}
static void credits(void){
 header("CREDITS");center(160,78,"The World's Hardest Game",BLUE);center(160,104,"Stephen Critoph / Snubby Land",BLACK);
 center(160,130,"Published by Armor Games",BLACK);center(160,164,"NumWorks adaptation",BLACK);center(160,188,"By Bobydeluxe",RED);
 footer("Original version 1.0","BACK: RETURN");
}
static void pause_menu(const WhgApp *a){
 field(a);gfx_fill_alpha(0,19,320,203,WHITE,215);gfx_fill(47,39,226,172,BLACK);gfx_fill(49,41,222,168,WHITE);title(68,"PAUSED");
 static const char *const options[]={"Resume","Retry checkpoint","How to play","Main menu","Quit game"};
 for(int i=0;i<5;i++){int y=94+i*24;if(a->selected==i){gfx_fill(61,y-16,198,21,HEX(0xe5e5ff));body(67,y,">",RED);}center(160,y,options[i],BLACK);}
}
static void confirmation(const WhgApp *a){
 paper();title(63,a->confirm==1?"NEW GAME?":a->confirm==2?"MAIN MENU?":a->confirm==4?"RETRY CHECKPOINT?":"QUIT GAME?");
 gfx_paragraph(&np_font_body,160,105,280,21,a->confirm==1?"Replace this run? Your completed records will be kept.":a->confirm==4?"Return to the last checkpoint and add one death?":"Your current run will be saved.",BLACK,256);
 center(102,183,"YES",a->selected==0?RED:BLACK);center(218,183,"NO",a->selected==1?BLUE:BLACK);gfx_fill(a->selected==0?78:199,191,45,2,a->selected==0?RED:BLUE);
 footer("ARROWS / OK","BACK: CANCEL");
}
static void save_error(const WhgApp *a){
 header("SAVE FAILED");gfx_paragraph(&np_font_body,160,81,278,20,"There is not enough free storage. Your other files are unchanged.",BLACK,256);
 static const char *const opt[]={"Retry save","Return to game","Exit without saving"};
 for(int i=0;i<3;i++){int y=149+i*26;if(i==a->selected)gfx_fill(28,y-17,264,23,HEX(0xe0e0ff));center(160,y,opt[i],i==a->selected?RED:BLACK);}footer("ARROWS / OK","BACK: RETURN");
}
void whg_draw(void *ctx){
 WhgApp *a=ctx;
 switch(a->screen){
 case UI_HOME:home(a);break;
 case UI_GAME:
  if(a->profile.run.phase==WHG_TIP){paper();title(43,"TIP");gfx_paragraph(&np_font_small,160,78,282,16,whg_tips[a->profile.run.level],BLACK,256);footer("Take your time.","OK: CONTINUE");}
  else if(a->profile.run.phase==WHG_INTRO){paper();char s[16];number(s,a->profile.run.level+1);strcat(s," / 30");center(160,39,s,BLACK);gfx_paragraph(&np_font_title,160,109,296,27,whg_levels[a->profile.run.level].taunt,BLACK,256);footer("","OK: START");}
  else field(a);break;
 case UI_PAUSE:pause_menu(a);break;case UI_HELP:help();break;case UI_RECORDS:records(a);break;case UI_CREDITS:credits();break;case UI_CONFIRM:confirmation(a);break;case UI_SAVE_ERROR:save_error(a);break;
 case UI_FINISH:{paper();title(63,"YOU BEAT THE GAME!");center(160,99,"All 30 levels complete.",BLACK);char s[16];number(s,a->profile.run.deaths);gfx_text_center(&np_font_title,160,142,s,RED,256);center(160,163,"TOTAL DEATHS",BLACK);footer("Well done!","OK: MAIN MENU");break;}
 }
}
