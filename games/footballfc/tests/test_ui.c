#include "../src/app.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static struct {uint32_t before[16];color_t pixels[320*STRIP_H];uint32_t after[16];} strip;
static color_t framebuffer[320*240];
void*np_alloc(uint32_t n){assert(n<=sizeof strip.pixels);return strip.pixels;}
void np_push(int x,int y,int w,int h,const color_t*p){assert(x>=0&&y>=0&&x+w<=320&&y+h<=240);for(int j=0;j<h;j++)memcpy(framebuffer+(y+j)*320+x,p+j*w,w*2);}
static void bounds(const np_font_t*f,int x,int y,const char*s){const char*start=s;int left=x;
 while(*s){unsigned c=(uint8_t)*s++;assert(c>=f->first&&c<f->first+f->count);const np_glyph_t*g=&f->glyphs[c-f->first];if(g->w&&(x+g->x<0||x+g->x+g->w>320||y+g->y<0||y+g->y+g->h>240)){fprintf(stderr,"Clipped: %s at %d,%d\n",start,left,y);abort();}x+=g->advance;}
}
int fc_checked_text(const np_font_t*f,int x,int y,const char*s,color_t c,int alpha){bounds(f,x,y,s);return gfx_text(f,x,y,s,c,alpha);}
void fc_checked_center(const np_font_t*f,int x,int y,const char*s,color_t c,int alpha){fc_checked_text(f,x-gfx_text_width(f,s)/2,y,s,c,alpha);}
void fc_checked_right(const np_font_t*f,int x,int y,const char*s,color_t c,int alpha){fc_checked_text(f,x-gfx_text_width(f,s),y,s,c,alpha);}
static void draw(FCApp*a,const char*name){for(int i=0;i<16;i++)strip.before[i]=strip.after[i]=0x513dd9a2;gfx_buf=strip.pixels;gfx_render(fc_draw,a,0,240);for(int i=0;i<16;i++)assert(strip.before[i]==0x513dd9a2&&strip.after[i]==0x513dd9a2);
 if(name){char path[100];snprintf(path,sizeof path,"%s.ppm",name);FILE*f=fopen(path,"wb");assert(f);fprintf(f,"P6\n320 240\n255\n");for(unsigned i=0;i<320*240;i++){unsigned c=framebuffer[i];uint8_t rgb[]={((c>>11)&31)*255/31,((c>>5)&63)*255/63,(c&31)*255/31};fwrite(rgb,1,3,f);}fclose(f);}
}
int main(void){FCApp a={0};fc_defaults(&a.profile);a.contract_games=5;fc_career_new(&a.profile,0);fc_cup_new(&a.profile,0);fc_new(&a.match,&a.profile,FC_FRIENDLY,1,42);a.cam_y=52*256;a.screen=UI_HOME;draw(&a,"menu");a.screen=UI_SETUP;draw(&a,"setup");a.screen=UI_MATCH;draw(&a,"kickoff");
 /* Side view: length maps rightward, width maps upward, height stays upward. */
 int cx,cy,x,y;assert(fc_project(&a,0,52*256,0,&cx,&cy));
 assert(fc_project(&a,0,53*256,0,&x,&y)&&x>cx&&abs(y-cy)<=1);
 assert(fc_project(&a,256,52*256,0,&x,&y)&&y<cy&&abs(x-cx)<=1);
 assert(fc_project(&a,0,52*256,2*256,&x,&y)&&y<cy);
 /* Reference proportions: a 9 m circle radius spans about 84 pixels, while
  * a 1.75 m player is about 16 pixels tall in the centre of the close view. */
 assert(fc_project(&a,0,61*256,0,&x,&y)&&x-cx>=80&&x-cx<=90);
 assert(fc_project(&a,0,52*256,7*256/4,&x,&y)&&cy-y>=15&&cy-y<=18);
 a.cam_x=8*256;assert(fc_project(&a,0,52*256,0,&x,&y)&&y>cy+30);a.cam_x=0;
 assert(fc_screen_input(FC_RIGHT|FC_SPRINT,false)==(FC_UP|FC_SPRINT));
 assert(fc_screen_input(FC_UP|FC_PASS,false)==(FC_RIGHT|FC_PASS));
 assert(fc_screen_input(FC_LEFT|FC_DOWN,false)==(FC_DOWN|FC_LEFT));
 assert(fc_screen_input(FC_LEFT|FC_SHOOT,true)==(FC_LEFT|FC_SHOOT));
 FCMatch horizontal=a.match,vertical=a.match;horizontal.phase=vertical.phase=FC_LIVE;
 int startx=horizontal.p[9].x,starty=horizontal.p[9].y;
 for(int i=0;i<20;i++){fc_step(&horizontal,fc_screen_input(FC_RIGHT,false));fc_step(&vertical,fc_screen_input(FC_UP,false));}
 assert(horizontal.p[9].y>starty&&abs(horizontal.p[9].x-startx)<128);
 assert(vertical.p[9].x>startx&&abs(vertical.p[9].y-starty)<128);
 a.match.phase=FC_LIVE;for(int i=0;i<170;i++){fc_step(&a.match,(i<100?FC_UP:FC_RIGHT)|FC_SPRINT);fc_camera(&a,25);}draw(&a,"gameplay");
 a.screen=UI_CAREER;draw(&a,"career");a.screen=UI_SQUAD;draw(&a,"squad");a.screen=UI_TABLE;draw(&a,"table");a.screen=UI_MARKET;draw(&a,"market");a.screen=UI_CUP;draw(&a,"cup");a.screen=UI_RESULT;draw(&a,"result");a.screen=UI_MATCH;fc_start_penalties(&a.match);draw(&a,"penalties");
 a.match.shoot_state=0;
 a.screen=UI_PAUSE;draw(&a,"pause");a.screen=UI_HELP;a.page=0;draw(&a,"help");
 a.screen=UI_SETTINGS;draw(&a,"settings");
 a.screen=UI_MATCH;a.match.phase=FC_LIVE;a.match.charge=750;draw(&a,"power");a.match.charge=0;
 a.match.phase=FC_FREE;a.match.set_team=0;a.match.notice=1;draw(&a,"restart");
 for(int screen=0;screen<=UI_TRAINING;screen++)for(int s=0;s<11;s++){
  a.screen=screen;a.selected=s;a.page=s%FC_HELP_PAGES;a.confirm=1+s%4;a.profile.money=999999;a.profile.season=999;a.profile.team=(s*3)%32;a.profile.opponent=(s*3+1)%32;a.mode=s%4;draw(&a,NULL);
 }
 for(int team=0;team<32;team++)for(int selected=0;selected<5;selected++){
  a.screen=UI_SETUP;a.selected=selected;a.profile.team=team;a.profile.opponent=(team+1)%32;draw(&a,NULL);
 }
 for(int phase=0;phase<=FC_PENALTY;phase++)for(int x=-30;x<=30;x+=15){a.screen=UI_MATCH;a.match.phase=phase;a.match.set_team=0;a.match.notice=phase%3;a.cam_x=x*256;a.cam_y=(x+50)*256;draw(&a,NULL);}
 for(int end=0;end<2;end++)for(int edge=-1;edge<=1;edge++){
  fc_new(&a.match,&a.profile,FC_FRIENDLY,1,42);a.screen=UI_MATCH;a.match.phase=FC_LIVE;
  a.match.ball.x=edge*(FC_W/2-2*256);a.match.ball.y=(end?FC_LENGTH-6:6)*256;
  for(int frame=0;frame<50;frame++)fc_camera(&a,33);
  assert(fc_project(&a,a.match.ball.x,a.match.ball.y,0,&x,&y)&&x>25&&x<295&&y>40&&y<206);
  char name[32];snprintf(name,sizeof name,"side-%d-%d",end,edge+1);draw(&a,name);
 }
 for(int venue=0;venue<3;venue++)for(int drill=0;drill<3;drill++){
  a.profile.venue=venue;fc_training_new(&a.match,&a.profile,drill,123);a.screen=UI_MATCH;a.cam_x=0;a.cam_y=a.match.ball.y;
  char name[32];snprintf(name,sizeof name,"training-%d-%d",venue,drill);draw(&a,name);
  a.screen=UI_RESULT;draw(&a,NULL);a.screen=UI_TRAINING;a.selected=drill;draw(&a,"training-menu");
 }
 a.profile.venue=2;fc_new(&a.match,&a.profile,FC_FRIENDLY,1,42);a.match.phase=FC_LIVE;a.cam_x=0;a.cam_y=52*256;a.screen=UI_MATCH;a.match.keeper_dive[1]=16;draw(&a,"floodlights");
 a.profile.venue=0;
 fc_new(&a.match,&a.profile,FC_FRIENDLY,1,42);a.match.phase=FC_LIVE;a.screen=UI_MATCH;
 a.match.p[9].x=34*256;a.match.p[9].y=55*256;a.match.ball.x=34*256;a.match.ball.y=55*256;fc_possess(&a.match,9);
 a.cam_x=26*256;a.cam_y=61*256;draw(&a,"stadium-touchline");
 fc_start_penalties(&a.match);int keepx=a.cam_x,keepy=a.cam_y;fc_camera(&a,33);assert(a.cam_x==keepx&&a.cam_y==keepy);
 a.match.kick_clock=32;draw(&a,"penalty-timing-good");a.match.kick_clock=0;draw(&a,"penalty-timing-poor");
 fc_new(&a.match,&a.profile,FC_FRIENDLY,1,42);fc_restart(&a.match,FC_FREE,0,0,FC_H-25*256);
 a.cam_x=0;a.cam_y=a.match.ball.y+6*256;a.match.kick_clock=32;a.match.curve=35;draw(&a,"free-kick-wall");
 a.match.card_timer=100;a.match.card_player=15;a.match.card_kind=1;draw(&a,"yellow-card");a.match.card_kind=3;draw(&a,"red-card");
 /* Native close-view fixture corresponding to the supplied reference's
  * proportions: carrier left of centre, midfield circle to the right. */
 a.profile.team=0;fc_new(&a.match,&a.profile,FC_FRIENDLY,1,42);a.match.phase=FC_LIVE;
 a.match.p[9].x=0;a.match.p[9].y=40*256;a.match.p[6].x=-8*256;a.match.p[6].y=46*256;
 a.match.ball.x=0;a.match.ball.y=40*256;fc_possess(&a.match,9);
 a.cam_x=0;a.cam_y=46*256;a.screen=UI_MATCH;
 for(int tick=0;tick<20;tick++){fc_step(&a.match,0);fc_camera(&a,25);}draw(&a,"close-view");
 FILE*motion=fopen("motion.raw","wb");assert(motion);a.profile.team=0;fc_new(&a.match,&a.profile,FC_FRIENDLY,1,42);a.match.phase=FC_LIVE;a.match.ball.x=a.match.p[9].x;a.match.ball.y=a.match.p[9].y;fc_possess(&a.match,9);a.cam_x=0;a.cam_y=60*256;a.screen=UI_MATCH;
 a.previous_match=a.match;unsigned accumulator=0;
 for(int frame=0;frame<240;frame++){accumulator+=33;while(accumulator>=25){a.previous_match=a.match;unsigned in=FC_UP|FC_SPRINT;int owner=a.match.ball.owner;
   if(owner>=0&&owner<11){if(frame<12)in=FC_UP|FC_PASS;else if(abs(a.match.p[owner].y-FC_H)<24*256){if(a.match.ticks%48<28)in|=FC_SHOOT;in|=FC_RIGHT;}else if(a.match.p[owner].x>5*256)in|=FC_LEFT;else if(a.match.p[owner].x<-5*256)in|=FC_RIGHT;}
   else{FCPlayer*p=&a.match.p[a.match.selected];in=(a.match.ball.x>p->x?FC_RIGHT:FC_LEFT)|(a.match.ball.y>p->y?FC_UP:FC_DOWN)|(frame%10==0?FC_SWITCH|FC_PASS:0);}
   fc_step(&a.match,in);accumulator-=25;}fc_camera(&a,33);fc_interpolate(&a.visual_match,&a.previous_match,&a.match,accumulator*40);a.use_visual=true;a.preview_target=a.match.pass_charge&&a.match.ball.owner>=0?fc_pass_target(&a.match,a.match.ball.owner,a.match.pass_kind):-1;draw(&a,NULL);a.use_visual=false;fwrite(framebuffer,sizeof framebuffer,1,motion);}fclose(motion);
 a.profile.team=0;a.profile.opponent=1;a.profile.venue=2;a.screen=UI_MATCH;
 fc_new(&a.match,&a.profile,FC_SHOOTOUT,1,5);FILE*pen=fopen("penalty-motion.raw","wb");assert(pen);accumulator=0;
 unsigned captured=0;
 for(int frame=0;frame<210;frame++){
  accumulator+=33;while(accumulator>=25){unsigned in=0;
   if(a.match.shoot_state==1&&!a.match.shoot_turn){if(frame<18)in=FC_RIGHT|FC_UP;else if(frame<44)in=FC_SHOOT;}
   else if(a.match.shoot_state==1&&a.match.shoot_turn){in=FC_LEFT|FC_UP;if(a.match.shoot_timer>20)in|=FC_PASS;}
   else if(a.match.shoot_turn&&a.match.shoot_timer<FC_PEN_RUNUP)in=FC_LEFT;
   fc_step(&a.match,in);accumulator-=25;
  }
  a.ui_time=frame*33;const char*name=NULL;unsigned bit=0;
  if(a.match.shoot_state==2){unsigned timer=a.match.shoot_timer;
   if(timer<FC_PEN_RUNUP){bit=1;name="penalty-runup";}
   else if(timer<FC_PEN_RUNUP+7){bit=2;name="penalty-strike";}
   else if(!a.match.shoot_resolved){bit=4;name="penalty-flight";}
   else{bit=8;name="penalty-result";}
  }
  if(a.match.shoot_state==2&&!a.match.shoot_resolved&&a.match.shoot_timer>FC_PEN_RUNUP+12&&abs(a.match.shoot_goal-a.match.shoot_start)>150&&!(captured&16)){bit=16;name="penalty-dive";}
  draw(&a,bit&&!(captured&bit)?name:NULL);captured|=bit;fwrite(framebuffer,sizeof framebuffer,1,pen);
 }
 fclose(pen);assert(captured==31);
 for(int half=0;half<2;half++)for(int side=-1;side<=1;side++){
  fc_new(&a.match,&a.profile,FC_FRIENDLY,1,42);a.match.half=half;
  fc_restart(&a.match,FC_FREE,0,side*12*256,(half?24:110-24)*256);int bx,by,gx,gy,hx,hy;
  assert(fc_project(&a,a.match.free_x,a.match.free_y,0,&bx,&by)&&abs(bx-160)<2&&by>208&&by<214);
  assert(fc_project(&a,0,half?0:FC_H,0,&gx,&gy)&&abs(gx-160)<2&&gy>75&&gy<125);
  assert(fc_project(&a,a.match.free_x,a.match.free_y,7*256/4,&hx,&hy)&&by-hy>=58&&by-hy<=61);
  char name[32];snprintf(name,sizeof name,"free-front-%d-%d",half,side+1);draw(&a,name);
 }
 fc_new(&a.match,&a.profile,FC_FREEPLAY,1,42);a.match.kick_clock=32;draw(&a,"free-practice");
 fc_step(&a.match,FC_PASS);for(int t=0;t<200&&a.match.phase==FC_LIVE;t++)fc_step(&a.match,0);draw(&a,"free-practice-result");
 a.profile.team=0;fc_new(&a.match,&a.profile,FC_FRIENDLY,1,42);fc_restart(&a.match,FC_FREE,0,0,FC_H-25*256);
 a.match.aim=700;a.match.curve=-45;a.match.power=350;a.match.kick_height=570;a.cam_x=0;a.cam_y=a.match.ball.y+6*256;
 FILE*free_motion=fopen("free-motion.raw","wb");assert(free_motion);accumulator=0;
 for(int frame=0;frame<96;frame++){
  accumulator+=33;while(accumulator>=25){fc_step(&a.match,frame<24?FC_SHOOT:0);accumulator-=25;}
  fc_camera(&a,33);a.ui_time=frame*33;draw(&a,NULL);fwrite(framebuffer,sizeof framebuffer,1,free_motion);
 }fclose(free_motion);
 for(int page=0;page<FC_HELP_PAGES;page++){a.screen=UI_HELP;a.page=page;char name[24];snprintf(name,sizeof name,"help-%d",page);draw(&a,name);}
 puts("Guarded renders, all screens, native glyph bounds and motion frames passed");return 0;}
