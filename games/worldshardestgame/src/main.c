#include <eadk.h>
#include <string.h>
#include "app.h"
#include "../../common/epsilon_app.h"
#include "../../common/epsilon_files.h"
#include "storage.h"
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name")))="World's Hardest Game";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level")))=0;
void np_push(int x,int y,int w,int h,const color_t *pixels){eadk_display_push_rect((eadk_rect_t){x,y,w,h},pixels);}
static color_t strip[320*STRIP_H];
static WhgApp app;
uint32_t perf_frames,perf_max,perf_play_max;
#define KEY(k) (UINT64_C(1)<<(k))
static bool save(void){if(!app.dirty)return true;if(!whg_storage_save(&app.profile))return false;app.dirty=false;return true;}
static void failed_save(bool exit){app.save_return=app.screen;app.exit_after_save=exit;app.screen=UI_SAVE_ERROR;app.selected=0;}
static void confirm(int kind){app.confirm=kind;app.return_to=app.screen;app.screen=UI_CONFIRM;app.selected=1;}
static void begin(void){whg_new(&app.profile);app.dirty=true;app.screen=UI_GAME;app.selected=0;}
int main(void){
 np_app_begin();color_t *old=gfx_buf;gfx_buf=strip;memset(&app,0,sizeof app);whg_defaults(&app.profile);whg_storage_load(&app.profile);
 uint64_t stale=eadk_keyboard_scan(),previous=0,repeating=0;uint32_t now=eadk_timing_millis(),last=now,draw_at=now,accum=0,repeat_at=0;bool redraw=true;
 while(!app.quit){
  now=eadk_timing_millis();unsigned elapsed=WHG_MIN(now-last,200u);last=now;
  uint64_t raw=eadk_keyboard_scan();stale&=raw;uint64_t keys=raw&~stale,hit=keys&~previous;previous=keys;
  uint64_t arrows=keys&(KEY(eadk_key_left)|KEY(eadk_key_right)|KEY(eadk_key_up)|KEY(eadk_key_down));
  if(arrows!=repeating){repeating=arrows;repeat_at=now+330;}else if(app.screen!=UI_GAME&&arrows&&(int32_t)(now-repeat_at)>=0){hit|=arrows;repeat_at=now+120;}
  bool ok=hit&(KEY(eadk_key_ok)|KEY(eadk_key_exe)),exe=hit&KEY(eadk_key_exe),back=hit&KEY(eadk_key_back);
  int dx=!!(hit&KEY(eadk_key_right))-!!(hit&KEY(eadk_key_left)),dy=!!(hit&KEY(eadk_key_down))-!!(hit&KEY(eadk_key_up)),before=app.screen;unsigned phase_before=app.profile.run.phase;
  if(app.screen!=UI_SAVE_ERROR&&(raw&(KEY(eadk_key_home)|KEY(eadk_key_on_off)))){if(save()){app.quit=true;continue;}failed_save(true);ok=exe=back=false;dx=dy=0;}
  if(app.screen==UI_HOME){
   if(dx||dy)app.selected=(app.selected+dx+dy+4)%4;
   if(back)confirm(3);
   else if(exe&&app.profile.has_run)confirm(1);
   else if(ok){if(app.selected==0){if(app.profile.has_run)app.screen=UI_GAME;else begin();}else {app.return_to=UI_HOME;app.screen=app.selected==1?UI_RECORDS:app.selected==2?UI_HELP:UI_CREDITS;}}
  }else if(app.screen==UI_GAME){
   if(back){app.screen=UI_PAUSE;app.selected=0;accum=0;if(!save())failed_save(false);}
   else{
    if(ok&&app.profile.run.phase==WHG_TIP){app.profile.run.phase=WHG_INTRO;app.profile.run.timer=44;accum=0;app.dirty=true;}
    else if(ok&&app.profile.run.phase==WHG_INTRO){app.profile.run.phase=WHG_PLAY;app.profile.run.timer=0;accum=0;app.dirty=true;}
    unsigned in=0;if(keys&KEY(eadk_key_left))in|=WHG_LEFT;if(keys&KEY(eadk_key_right))in|=WHG_RIGHT;if(keys&KEY(eadk_key_up))in|=WHG_UP;if(keys&KEY(eadk_key_down))in|=WHG_DOWN;
    accum+=elapsed*30;while(accum>=1000&&app.screen==UI_GAME){unsigned event=whg_step(&app.profile.run,in);accum-=1000;app.dirty=true;
     if(app.profile.run.level>app.profile.furthest)app.profile.furthest=app.profile.run.level;
     if(event&WHG_COMPLETED){whg_record_finish(&app.profile);app.screen=UI_FINISH;}
     if(event&(WHG_DIED|WHG_CHECKPOINT|WHG_CLEARED|WHG_COMPLETED))if(!save())failed_save(false);
    }
   }
  }else if(app.screen==UI_PAUSE){
   if(dy)app.selected=(app.selected+dy+5)%5;
   if(back||(ok&&app.selected==0))app.screen=UI_GAME;
   else if(ok){if(app.selected==1)confirm(4);else if(app.selected==2){app.return_to=UI_PAUSE;app.screen=UI_HELP;}else confirm(app.selected==3?2:3);}
  }else if(app.screen==UI_HELP||app.screen==UI_RECORDS||app.screen==UI_CREDITS){if(back||ok){app.screen=app.screen==UI_HELP&&app.return_to==UI_PAUSE?UI_PAUSE:UI_HOME;app.return_to=UI_HOME;}}
  else if(app.screen==UI_CONFIRM){
   if(dx||dy)app.selected^=1;
   if(back||(ok&&app.selected==1)){app.screen=app.return_to;app.selected=0;}
   else if(ok){
    if(app.confirm==1)begin();else if(app.confirm==4){whg_restart(&app.profile.run);app.dirty=true;app.screen=UI_GAME;}
    else if(save()){if(app.confirm==3)app.quit=true;else {app.screen=UI_HOME;app.selected=0;}}
    else failed_save(app.confirm==3);
   }
  }else if(app.screen==UI_SAVE_ERROR){
   if(dy)app.selected=(app.selected+dy+3)%3;
   if(back||(ok&&app.selected==1)){app.screen=app.save_return;app.exit_after_save=false;}
   else if(ok&&app.selected==2)app.quit=true;
   else if(ok&&save()){if(app.exit_after_save)app.quit=true;else app.screen=app.save_return;}
  }else if(app.screen==UI_FINISH){if(back||ok){app.screen=UI_HOME;app.selected=0;}}
  if(before!=app.screen){accum=0;redraw=true;stale|=keys;previous=0;}
  if(hit||phase_before!=app.profile.run.phase)redraw=true;
  bool animated=app.screen==UI_GAME&&(app.profile.run.phase==WHG_PLAY||app.profile.run.phase==WHG_DYING);
  if((animated||redraw)&&(int32_t)(now-draw_at)>=0){uint32_t started=eadk_timing_millis();whg_prepare_draw(&app);eadk_display_wait_for_vblank();gfx_render(whg_draw,&app,0,240);
   uint32_t work=eadk_timing_millis()-started;perf_frames++;perf_max=WHG_MAX(perf_max,work);if(animated)perf_play_max=WHG_MAX(perf_play_max,work);draw_at=now+33;redraw=false;
  }
  eadk_timing_msleep(2);
 }
 gfx_buf=old;return np_app_end();
}
