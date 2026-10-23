#include <eadk.h>
#include <string.h>
#include "render.h"
#include "../../common/epsilon_app.h"
#if !NP_MODULE
#include "../../common/epsilon_files.h"
#define np_file_read ef_read
#define np_file_write ef_write
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name")))="Turbo Rush";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level")))=0;
void np_push(int x,int y,int w,int h,const color_t*p){eadk_display_push_rect((eadk_rect_t){x,y,w,h},p);}
#endif
#define WHITE 0xffff
#define INK HEX(0x101d29)
#define GOLD HEX(0xffd052)
#define MUTED HEX(0xc4d5d6)
#define KEY(k) (UINT64_C(1)<<(k))
enum { MENU,PLAY,PAUSE,GARAGE,HELP,RESULT,SAVE_ERROR,LEAVE,SETUP,RECORDS,CONFIRM };
static TrProfile profile;
static TrRace race,demo,previous_race,visual_race;
static TrView view;
static color_t strip[320*STRIP_H];
static int screen,selected,help_page,error_return,garage_return,help_return,confirm_action;
static bool dirty,quit_after_save;
static unsigned prize,champ_round,points[8];
static bool championship;
static uint32_t ui_frame;
static char garage_notice[54];
uint32_t perf_frames,perf_max,perf_work,perf_race_frames,perf_race_max;
static const char*const MOODS[]={"Day","Sunset","Night"};
static const char*const DIFF[]={"Easy","Normal","Expert"};
static const char*const MODES[]={"Grand Prix","Single race","Free Ride"};
static const char*const UPGRADES[]={"Engine","Tires","Nitro","Armor"};
static char* num(char*s,unsigned n){char a[10];int i=0;do{a[i++]='0'+n%10;n/=10;}while(n);while(i)*s++=a[--i];*s=0;return s;}
static char* txt(char*s,const char*t){while(*t)*s++=*t++;*s=0;return s;}
static void small(int x,int y,const char*s,color_t c){gfx_text(&np_font_small,x,y,s,c,256);}
static void mid(int x,int y,const char*s,color_t c){gfx_text_center(&np_font_body,x,y,s,c,256);}
static void heading(int y,const char*s){gfx_text_center(&np_font_title,160,y,s,WHITE,256);}
static void logo(int offset,color_t color){
 const np_font_t*f=&np_font_title;const char*s="TURBO RUSH";int x=(320-gfx_text_width(f,s)*2)/2+offset,baseline=43+offset;
 while(*s){const np_glyph_t*g=&f->glyphs[(uint8_t)*s++-f->first];const uint8_t*m=f->data+g->offset;
   for(int y=0;y<g->h;y++){int yy=baseline+(g->y+y)*2;if(yy+2<=gfx_y0||yy>=gfx_y1)continue;
     for(int j=0;j<g->w;j++){int n=y*g->w+j,cov=(m[n/2]>>((n&1)*4))&15;if(cov)gfx_fill_alpha(x+(g->x+j)*2,yy,2,2,color,cov*17);}}
   x+=g->advance*2;
 }
}
static void button(int y,const char*s,bool focus){gfx_rrect(34,y,252,25,4,focus?GOLD:INK,250);mid(160,y+18,s,focus?INK:WHITE);}
static bool save(void){if(!dirty)return true;uint8_t b[TR_SAVE_BYTES];tr_encode(&profile,b);if(!np_file_write("turborush.sav",b,sizeof b))return false;dirty=false;return true;}
static void error(bool exit){error_return=screen;quit_after_save=exit;screen=SAVE_ERROR;selected=0;}
static void begin_race(bool fresh){
 if(fresh){championship=profile.mode==0;champ_round=0;memset(points,0,sizeof points);}
 tr_new(&race,&profile,championship?champ_round:profile.track,eadk_random());previous_race=race;screen=PLAY;selected=0;
}
static unsigned champ_rank(void){unsigned rank=1;for(int i=1;i<8;i++)if(points[i]>points[0])rank++;return rank;}
static void finish(void){
 prize=tr_reward(&profile,&race);dirty=true;
 if(championship){
   static const uint8_t scoring[]={15,12,10,8,6,4,2,1};
   for(int i=0;i<8;i++){unsigned rank=tr_position(&race,i);
     if(race.phase==TR_WRECKED){if(!i)continue;if(rank>race.rank)rank--;}
     points[i]+=scoring[rank-1];}
   if(champ_round==5){unsigned bonus=(9-champ_rank())*200;profile.money=NP_MIN(profile.money+bonus,999999u);prize+=bonus;}
 }
 screen=RESULT;selected=0;if(!save())error(false);
}
static void money(void){char s[28];char*p=num(s,profile.money);txt(p," CR");gfx_text_right(&np_font_small,308,15,s,GOLD,256);}
static void time_text(char*s,unsigned t){
 char*p=num(s,t/60000);*p++=':';*p++='0'+t/1000%60/10;*p++='0'+t/1000%10;*p++='.';*p++='0'+t/100%10;*p=0;
}
#include "ui.h"
static void change(int dx){
 if(selected==0)profile.mode=(profile.mode+dx+3)%3;
 else if(selected==1&&profile.mode)profile.track=(profile.track+dx+6)%6;
 else if(selected==2)profile.mood=(profile.mood+dx+3)%3;
 else if(selected==3)profile.manual^=1;
 else if(selected==4)profile.difficulty=(profile.difficulty+dx+3)%3;
 else if(selected==5)profile.effects=profile.effects==2?1:2;
 dirty=true;
}
int main(void){
 np_app_begin();color_t*old=gfx_buf;gfx_buf=strip;tr_defaults(&profile);
 uint32_t size=0;const uint8_t*b=np_file_read("turborush.sav",&size);tr_decode(&profile,b,size);
 screen=MENU;selected=0;dirty=false;championship=false;uint64_t prev=0,stale=eadk_keyboard_scan();bool quit=false,redraw=true;
 uint32_t now=eadk_timing_millis(),last=now,draw_at=now,accum=0,repeat_at=0;uint64_t repeating=0;int pending_shift=0;
 unsigned frame_ms=epsilon_slow_model()?33:20;
 while(!quit){
   now=(uint32_t)eadk_timing_millis();unsigned dt=NP_MIN(now-last,200u);last=now;
   uint64_t raw=eadk_keyboard_scan();stale&=raw;uint64_t keys=raw&~stale,hit=keys&~prev;prev=keys;
   uint64_t arrows=keys&(KEY(eadk_key_left)|KEY(eadk_key_right)|KEY(eadk_key_up)|KEY(eadk_key_down));
   if(arrows!=repeating){repeating=arrows;repeat_at=now+350;}else if(screen!=PLAY&&arrows&&(int32_t)(now-repeat_at)>=0){hit|=arrows;repeat_at=now+120;}
   bool ok=hit&(KEY(eadk_key_ok)|KEY(eadk_key_exe)),back=hit&KEY(eadk_key_back);
   int dx=!!(hit&KEY(eadk_key_right))-!!(hit&KEY(eadk_key_left)),dy=!!(hit&KEY(eadk_key_down))-!!(hit&KEY(eadk_key_up)),before=screen;
   if(screen!=SAVE_ERROR&&(raw&(KEY(eadk_key_home)|KEY(eadk_key_on_off)))){if(save()){quit=true;continue;}error(true);ok=back=false;dx=dy=0;}
   if(screen==MENU){
     if(dy)selected=(selected+dy+4)%4;
     if(back){screen=LEAVE;selected=0;}
     else if(ok){
       if(selected==0){screen=SETUP;selected=6;}
       else if(selected==1){garage_return=MENU;screen=GARAGE;selected=0;garage_notice[0]=0;}
       else if(selected==2){screen=RECORDS;}
       else{help_return=MENU;screen=HELP;help_page=0;}
     }
   }else if(screen==SETUP){
     if(dy){selected=(selected+dy+7)%7;if(selected==1&&!profile.mode)selected+=dy;}
     if(dx&&selected<6)change(dx);
     if(back){screen=MENU;selected=0;}
     else if(ok){if(selected<6)change(1);else begin_race(true);}
   }else if(screen==PLAY){
     if(back){screen=PAUSE;selected=0;}
     else{
       unsigned in=(keys&KEY(eadk_key_up)?TR_THROTTLE:0)|(keys&KEY(eadk_key_down)?TR_BRAKE:0)|(keys&KEY(eadk_key_left)?TR_LEFT:0)|(keys&KEY(eadk_key_right)?TR_RIGHT:0)|(keys&(KEY(eadk_key_ok)|KEY(eadk_key_exe))?TR_BOOST:0);
       int shift=!!(hit&KEY(eadk_key_seven))-!!(hit&KEY(eadk_key_four));if(shift)pending_shift=shift;accum+=dt;
       while(accum>=20){previous_race=race;tr_step(&race,&profile,in,pending_shift);pending_shift=0;accum-=20;if(race.phase>=TR_FINISHED){finish();break;}}
     }
   }else if(screen==PAUSE){
     if(dy)selected=(selected+dy+4)%4;
     if(back||(ok&&selected==0))screen=PLAY;
     else if(ok&&selected==1){help_return=PAUSE;screen=HELP;help_page=0;}
     else if(ok){confirm_action=selected==3;screen=CONFIRM;selected=0;}
   }else if(screen==CONFIRM){
     if(dy)selected^=1;
     if(back||(ok&&!selected)){screen=PLAY;}
     else if(ok){if(confirm_action){screen=MENU;selected=0;}else begin_race(false);}
   }else if(screen==GARAGE){
     if(dy){selected=(selected+dy+5)%5;garage_notice[0]=0;}
     if(selected==4&&(dx||ok)){profile.paint=(profile.paint+(dx?dx:1)+6)%6;dirty=true;}
     else if(ok&&selected<4){
       unsigned price=tr_price(&profile,selected);
       if(tr_buy(&profile,selected)){dirty=true;txt(garage_notice,"Upgrade fitted!");}
       else if(!price)txt(garage_notice,"Already fully upgraded");
       else txt(num(txt(garage_notice,"Need "),price-profile.money)," more credits");
     }
     if(back){screen=garage_return;selected=screen==MENU?1:0;if(!save())error(false);}
   }else if(screen==HELP){
     help_page=(help_page+dx+4)%4;
     if(back||ok){screen=help_return;selected=screen==MENU?3:1;}
   }else if(screen==RECORDS){if(back||ok){screen=MENU;selected=2;}}
   else if(screen==RESULT){
     if(dy)selected=(selected+dy+3)%3;
     if(back||(ok&&selected==2)){screen=MENU;selected=0;}
     else if(ok&&selected==1){garage_return=RESULT;screen=GARAGE;selected=0;garage_notice[0]=0;}
     else if(ok){if(championship&&champ_round<5){champ_round++;begin_race(false);}else begin_race(true);}
   }else if(screen==LEAVE){
     if(dy)selected^=1;
     if(back||(ok&&!selected)){screen=MENU;selected=0;}
     else if(ok){if(save())quit=true;else error(true);}
   }else if(screen==SAVE_ERROR){
     if(dy)selected=(selected+dy+3)%3;
     if(ok){if(selected==2)quit=true;else if(selected==1){screen=error_return==PLAY?PAUSE:error_return;selected=0;}
       else if(save()){if(quit_after_save)quit=true;else{screen=error_return;selected=0;}}}
   }
   if(screen!=PLAY)accum=0,pending_shift=0;
   if(screen!=before){stale|=raw;prev=0;accum=0;redraw=true;last=(uint32_t)eadk_timing_millis();}
   if(hit)redraw=true;
   bool animated=screen==PLAY||screen==MENU;
   if(redraw||(animated&&now-draw_at>=(screen==MENU?80:frame_ms))){
     uint32_t start=(uint32_t)eadk_timing_millis();ui_frame++;
     if(screen==MENU||screen==LEAVE){tr_new(&demo,&profile,profile.track,1);demo.cars[0].distance=(int32_t)((uint64_t)now*45*256/1000%(tr_length(demo.track)*256));demo.cars[0].speed=36*256;for(int i=1;i<8;i++)demo.cars[i].distance=demo.cars[0].distance+(80+i*90)*256;tr_prepare(&view,&demo,&profile,ui_frame);}
     else{if(screen==PLAY)tr_interpolate(&visual_race,&previous_race,&race,accum*50);else visual_race=race;tr_prepare(&view,&visual_race,&profile,ui_frame);}
     eadk_display_wait_for_vblank();gfx_render(scene,0,0,240);
     unsigned spent=(uint32_t)eadk_timing_millis()-start;perf_work+=spent;if(spent>perf_max)perf_max=spent;perf_frames++;
     if(screen==PLAY){perf_race_frames++;if(spent>perf_race_max)perf_race_max=spent;}
     draw_at=now;redraw=false;
   }
   eadk_timing_msleep(2);
 }
 gfx_buf=old;return np_app_end();
}
