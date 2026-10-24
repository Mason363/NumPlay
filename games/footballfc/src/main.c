#include <eadk.h>
#include <string.h>
#include "app.h"
#include "../../common/epsilon_app.h"
#include "../../common/epsilon_files.h"
#include "storage.h"
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name")))="Football FC";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level")))=0;
void np_push(int x,int y,int w,int h,const color_t*p){eadk_display_push_rect((eadk_rect_t){x,y,w,h},p);}
static FCApp app;
static color_t strip[320*STRIP_H];
static int save_return;static bool exit_after_save;
uint32_t perf_frames,perf_max,perf_match_max;
#define KEY(k) (UINT64_C(1)<<(k))
static bool save(void){if(!app.dirty)return true;if(!fc_storage_save(&app.profile))return false;app.dirty=false;return true;}
static void save_error(bool exit){save_return=app.screen;exit_after_save=exit;app.screen=UI_SAVE_ERROR;app.selected=0;}
static void message(const char*s){unsigned n=0;while(s[n]&&n<sizeof(app.message)-1){app.message[n]=s[n];n++;}app.message[n]=0;}
static void begin(unsigned mode){
 unsigned other=mode==FC_CAREER?fc_career_opponent(&app.profile):mode==FC_CUP?fc_cup_opponent(&app.profile):app.profile.opponent;
 if(mode==FC_TRAINING){unsigned drill=app.match.drill;fc_training_new(&app.match,&app.profile,drill,eadk_random());}
 else fc_new(&app.match,&app.profile,mode,other,eadk_random());app.previous_match=app.visual_match=app.match;app.screen=UI_MATCH;app.cam_x=0;app.cam_y=app.match.ball.y;app.rewarded=false;app.selected=0;app.message[0]=0;
}
static void setup(unsigned mode){app.mode=(int)mode;app.screen=UI_SETUP;app.selected=0;}
static void confirm(unsigned type,int return_to){app.confirm=type;app.return_to=return_to;app.screen=UI_CONFIRM;app.selected=1;}
static void next_setup(int dy){app.selected=(app.selected+dy+5)%5;if(app.mode==FC_CAREER||app.mode==FC_CUP){if(app.selected==1)app.selected+=dy>0?1:-1;}}
static void change_settings(int dx){FCProfile*p=&app.profile;if(app.selected==0)p->difficulty=(p->difficulty+dx+3)%3;else if(app.selected==1)p->length=(p->length+dx+3)%3;else if(app.selected==2)p->formation=(p->formation+dx+3)%3;else if(app.selected==3)p->kit^=1;else p->venue=(p->venue+dx+3)%3;app.dirty=true;}
static unsigned controls(uint64_t keys){
 unsigned in=0;if(keys&KEY(eadk_key_left))in|=FC_LEFT;if(keys&KEY(eadk_key_right))in|=FC_RIGHT;if(keys&KEY(eadk_key_up))in|=FC_UP;if(keys&KEY(eadk_key_down))in|=FC_DOWN;
 if(keys&KEY(eadk_key_xnt))in|=FC_SPRINT;if(keys&(KEY(eadk_key_exe)|KEY(eadk_key_shift)))in|=FC_SHOOT;if(keys&KEY(eadk_key_ok))in|=FC_PASS;if(keys&KEY(eadk_key_seven))in|=FC_SWITCH;
 if(keys&KEY(eadk_key_eight))in|=FC_THROUGH;if(keys&KEY(eadk_key_nine))in|=FC_LOB;if(keys&KEY(eadk_key_alpha))in|=FC_CONTROL;if(keys&KEY(eadk_key_five))in|=FC_PRESS;if(keys&KEY(eadk_key_six))in|=FC_TACTIC;return fc_match_input(in,&app.match);
}
int main(void){
 np_app_begin();color_t*old=gfx_buf;gfx_buf=strip;memset(&app,0,sizeof app);fc_defaults(&app.profile);fc_storage_load(&app.profile);app.contract_games=5;
 uint64_t stale=eadk_keyboard_scan(),previous=0,repeating=0;uint32_t now=eadk_timing_millis(),last=now,draw_at=now,accum=0,repeat_at=0,message_at=0;
 unsigned frame_ms=epsilon_slow_model()?40:33;bool redraw=true;
 while(!app.quit){
  now=(uint32_t)eadk_timing_millis();unsigned elapsed=FC_MIN(now-last,200u);last=now;app.ui_time=now;
  uint64_t raw=eadk_keyboard_scan();stale&=raw;uint64_t keys=raw&~stale,hit=keys&~previous;previous=keys;
  uint64_t arrows=keys&(KEY(eadk_key_left)|KEY(eadk_key_right)|KEY(eadk_key_up)|KEY(eadk_key_down));
  if(arrows!=repeating){repeating=arrows;repeat_at=now+330;}else if(app.screen!=UI_MATCH&&arrows&&(int32_t)(now-repeat_at)>=0){hit|=arrows;repeat_at=now+115;}
  bool ok=hit&KEY(eadk_key_ok),exe=hit&KEY(eadk_key_exe),back=hit&KEY(eadk_key_back);int dx=!!(hit&KEY(eadk_key_right))-!!(hit&KEY(eadk_key_left)),dy=!!(hit&KEY(eadk_key_down))-!!(hit&KEY(eadk_key_up)),before=app.screen;
  if(app.screen!=UI_SAVE_ERROR&&(raw&(KEY(eadk_key_home)|KEY(eadk_key_on_off)))){if(save()){app.quit=true;continue;}save_error(true);ok=exe=back=false;dx=dy=0;}
  FCProfile*p=&app.profile;
  if(app.screen==UI_HOME){
   if(dy)app.selected=(app.selected+dy+8)%8;if(back)confirm(4,UI_HOME);
   else if(ok){switch(app.selected){case 0:setup(FC_FRIENDLY);break;case 1:if(p->career_active){app.screen=UI_CAREER;app.selected=0;}else setup(FC_CAREER);break;case 2:if(p->cup_active||p->cup_stage||p->cup_round){app.screen=UI_CUP;app.selected=0;}else setup(FC_CUP);break;case 3:setup(FC_SHOOTOUT);break;case 4:setup(FC_FREEPLAY);break;case 5:app.screen=UI_TRAINING;app.selected=0;break;case 6:app.screen=UI_SETTINGS;app.selected=0;break;default:app.screen=UI_HELP;app.page=0;app.return_to=UI_HOME;break;}}
  }else if(app.screen==UI_SETUP){
   if(dy)next_setup(dy);
   if(dx){if(app.selected==0)p->team=(p->team+dx+32)%32;else if(app.selected==1)p->opponent=(p->opponent+dx+32)%32;else if(app.selected==2)p->difficulty=(p->difficulty+dx+3)%3;else if(app.selected==3&&app.mode!=FC_FREEPLAY)p->length=(p->length+dx+3)%3;app.dirty=true;}
   if(back){app.screen=UI_HOME;app.selected=0;}else if(ok||exe){if(app.selected<4)next_setup(1);else{
    if(app.mode==FC_CAREER){fc_career_new(p,p->team);app.screen=UI_CAREER;app.selected=0;app.dirty=true;}
    else if(app.mode==FC_CUP){fc_cup_new(p,p->team);app.screen=UI_CUP;app.selected=0;app.dirty=true;}
    else begin(app.mode);
   }}
  }else if(app.screen==UI_TRAINING){
   if(dy)app.selected=(app.selected+dy+3)%3;if(back){app.screen=UI_HOME;app.selected=5;}
   else if(ok||exe){app.match.drill=(uint8_t)app.selected;begin(FC_TRAINING);}
  }else if(app.screen==UI_SETTINGS){if(dy)app.selected=(app.selected+dy+5)%5;if(dx)change_settings(dx);if(back){app.screen=UI_HOME;app.selected=6;if(!save())save_error(false);}}
  else if(app.screen==UI_CAREER){if(dy)app.selected=(app.selected+dy+4)%4;if(back){app.screen=UI_HOME;app.selected=1;}else if(exe)confirm(1,UI_CAREER);else if(ok){
   if(app.selected==0){if(p->career_round==7){fc_career_next(p);app.dirty=true;}else begin(FC_CAREER);}
   else if(app.selected==1){app.screen=UI_SQUAD;app.selected=0;}else if(app.selected==2){app.screen=UI_TABLE;}else{app.screen=UI_MARKET;app.selected=0;}
  }}else if(app.screen==UI_SQUAD){if(dy)app.selected=(app.selected+dy+11)%11;if(dx)app.contract_games=app.contract_games==3?(dx>0?5:7):app.contract_games==5?(dx>0?7:3):(dx>0?3:5);
   if(back){app.screen=UI_CAREER;app.selected=1;if(!save())save_error(false);}else if(ok||exe){bool bought=ok?fc_train(p,app.selected):fc_contract(p,app.selected,app.contract_games);if(bought){app.dirty=true;message(ok?"Training complete: quality +2":"Contract renewed");}else message("Not enough credits, or already at the limit");message_at=now;}
  }else if(app.screen==UI_TABLE){if(back||ok){app.screen=UI_CAREER;app.selected=2;}}
  else if(app.screen==UI_MARKET){if(dy)app.selected=(app.selected+dy+4)%4;if(back){app.screen=UI_CAREER;app.selected=3;if(!save())save_error(false);}else if(ok){bool bought=app.selected==3?fc_coach(p):fc_recruit(p,app.selected);if(bought){app.dirty=true;message(app.selected==3?"Coach upgraded":"Recruit signed");}else message("Not enough credits, or no upgrade available");message_at=now;}}
  else if(app.screen==UI_CUP){if(back){app.screen=UI_HOME;app.selected=2;}else if(exe)confirm(2,UI_CUP);else if(ok){if(p->cup_active)begin(FC_CUP);else confirm(2,UI_CUP);}}
  else if(app.screen==UI_HELP){if(dx)app.page=(app.page+dx+FC_HELP_PAGES)%FC_HELP_PAGES;if(back){app.screen=app.return_to;app.selected=0;}}
  else if(app.screen==UI_PAUSE){if(dx&&app.match.mode!=FC_TRAINING&&app.match.mode!=FC_FREEPLAY){app.match.tactics[0]=(app.match.tactics[0]+dx+4)%4;app.match.tactic_notice=100;}if(dy)app.selected=(app.selected+dy+3)%3;if(back){app.screen=UI_MATCH;app.match.previous_input=(uint8_t)controls(keys);app.match.input_latch=(uint16_t)controls(keys);}else if(ok){
   if(app.selected==0){app.screen=UI_MATCH;app.match.previous_input=(uint8_t)controls(keys);app.match.input_latch=(uint16_t)controls(keys);}else if(app.selected==1){app.return_to=UI_PAUSE;app.screen=UI_HELP;app.page=0;}else confirm(3,UI_PAUSE);
  }}else if(app.screen==UI_MATCH){
   if(back){app.screen=UI_PAUSE;app.selected=0;accum=0;app.previous_match=app.match;}
   else{accum+=elapsed;while(accum>=25&&app.screen==UI_MATCH){app.previous_match=app.match;fc_step(&app.match,controls(keys));accum-=25;
     if(app.match.phase==FC_FULL&&(!app.match.shoot_state||app.match.shoot_done)){
      if(!app.match.shoot_state&&app.match.score[0]==app.match.score[1]&&app.match.mode==FC_CUP&&p->cup_stage){fc_start_penalties(&app.match);}
      else{if(!app.rewarded){fc_finish(p,&app.match);if(app.match.mode!=FC_TRAINING&&app.match.mode!=FC_FREEPLAY)app.dirty=true;app.rewarded=true;}app.screen=UI_RESULT;app.selected=0;if(!save())save_error(false);}
     }
    }fc_camera(&app,elapsed);
   }
  }else if(app.screen==UI_RESULT){if(dy)app.selected^=1;if(ok||exe||back){if(back||app.selected==1){app.screen=UI_HOME;app.selected=0;}else if(app.match.mode==FC_CAREER){app.screen=UI_CAREER;app.selected=0;}else if(app.match.mode==FC_CUP){app.screen=UI_CUP;app.selected=0;}else begin(app.match.mode);}}
  else if(app.screen==UI_CONFIRM){if(dy)app.selected^=1;if(back||(ok&&app.selected==1)){app.screen=app.return_to;app.selected=0;}else if(ok){
   if(app.confirm==1)setup(FC_CAREER);else if(app.confirm==2)setup(FC_CUP);else if(app.confirm==3){app.screen=app.match.mode==FC_CAREER?UI_CAREER:app.match.mode==FC_CUP?UI_CUP:app.match.mode==FC_TRAINING?UI_TRAINING:UI_HOME;app.selected=app.match.mode==FC_FREEPLAY?4:0;}
   else if(save())app.quit=true;else save_error(true);
  }}else if(app.screen==UI_SAVE_ERROR){if(dy)app.selected=(app.selected+dy+3)%3;if(back||(ok&&app.selected==1)){app.screen=save_return;exit_after_save=false;}else if(ok&&app.selected==2){app.quit=true;}else if(ok&&save()){if(exit_after_save)app.quit=true;else app.screen=save_return;}}
  if(app.message[0]&&(uint32_t)(now-message_at)>2500){app.message[0]=0;redraw=true;}
  if(before!=app.screen){app.message[0]=0;accum=0;redraw=true;stale|=keys;previous=0;}
  if(hit)redraw=true;
  bool animated=app.screen==UI_MATCH;
  if((animated||redraw)&&(int32_t)(now-draw_at)>=0){uint32_t started=eadk_timing_millis();
   app.use_visual=app.screen==UI_MATCH;if(app.use_visual)fc_interpolate(&app.visual_match,&app.previous_match,&app.match,accum*40);
   app.preview_target=app.match.pass_charge&&app.match.ball.owner>=0?fc_pass_target(&app.match,app.match.ball.owner,app.match.pass_kind):-1;
   eadk_display_wait_for_vblank();gfx_render(fc_draw,&app,0,240);app.use_visual=false;uint32_t work=(uint32_t)eadk_timing_millis()-started;
   perf_frames++;perf_max=FC_MAX(perf_max,work);if(app.screen==UI_MATCH)perf_match_max=FC_MAX(perf_match_max,work);draw_at=now+frame_ms;redraw=false;
  }eadk_timing_msleep(2);
 }
 gfx_buf=old;return np_app_end();
}
