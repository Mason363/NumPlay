#include <eadk.h>
#include "render.h"
#include "../../common/epsilon_app.h"
#include "../../common/epsilon_files.h"
#include <math.h>
#include <string.h>
#ifdef __ELF__
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name")))="Angry Birds";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level")))=0;
#endif
#define KEY(k) (UINT64_C(1)<<(k))
static Game game;
static uint8_t save_bytes[SAVE_BYTES];
static uint32_t last_crc;
static bool saved,failed;
uint32_t current_view,perf_frames,perf_max,perf_step_max,perf_slow;
static void push(int x,int y,int w,int h,const uint16_t*p){eadk_display_push_rect((eadk_rect_t){x,y,w,h},p);}
static bool save(void){
    save_encode(&game.p,save_bytes);uint32_t crc;memcpy(&crc,save_bytes+SAVE_BYTES-4,4);
    if(saved&&!failed&&crc==last_crc)return true;failed=!ef_write("angrybirds.sav",save_bytes,SAVE_BYTES);
    if(!failed){last_crc=crc;saved=true;game.dirty=0;}return !failed;
}
int main(void){
    np_app_begin();game_new(&game,(uint32_t)eadk_random());uint32_t size=0;const uint8_t*data=ef_read("angrybirds.sav",&size);saved=save_decode(&game.p,data,size);
    unsigned view=TITLE,selected=0,return_view=TITLE,quit_view=TITLE,story_return=PLAY,story_id=65535,story_selection=0,loaded_view=PLAY;bool exiting=false;
    uint64_t keys=eadk_keyboard_scan(),stale=keys,previous=0;uint32_t now=(uint32_t)eadk_timing_millis(),last=now,draw_at=now,accum=0,repeat=now;
    unsigned painted=999;render_invalidate();
    while(!exiting){
        now=(uint32_t)eadk_timing_millis();uint32_t elapsed=now-last;last=now;keys=eadk_keyboard_scan();stale&=keys;uint64_t active=keys&~stale,hit=active&~previous;previous=active;
        bool ok=hit&(KEY(eadk_key_ok)|KEY(eadk_key_exe)),back=hit&KEY(eadk_key_back),left=hit&KEY(eadk_key_left),right=hit&KEY(eadk_key_right),up=hit&KEY(eadk_key_up),down=hit&KEY(eadk_key_down);
        if(view!=SAVE_ERROR&&(keys&(KEY(eadk_key_home)|KEY(eadk_key_on_off)))){if(save())break;view=SAVE_ERROR;stale=keys;ok=back=left=right=up=down=false;}
        if(view==PLAY){
            if(back){view=PAUSE;selected=0;save();}
            else{
                if(hit&KEY(eadk_key_two))game.overview^=1;
                if(hit&KEY(eadk_key_one))game_eagle(&game);
                if(!game.active&&(game.launched<game.birds||game.eagle_armed)&&(hit||now-repeat>=40)){
                    if(active&KEY(eadk_key_up))game.angle=fminf(PI*.49f,game.angle+PI/180);
                    if(active&KEY(eadk_key_down))game.angle=fmaxf(-PI*.2f,game.angle-PI/180);
                    if(active&KEY(eadk_key_left))game.power=fmaxf(.15f,game.power-.025f);
                    if(active&KEY(eadk_key_right))game.power=fminf(1,game.power+.025f);repeat=now;
                }
                if(ok){if(game.active)game_power(&game);else game_launch(&game);}
                /* Bound catch-up work so collapse scenes cannot starve the keypad. */
                if(elapsed>67)elapsed=67;accum+=elapsed*60;unsigned steps=0;uint32_t step_start=(uint32_t)eadk_timing_millis();
                while(accum>=1000&&steps<4){uint32_t at=(uint32_t)eadk_timing_millis();game_step(&game);uint32_t used=(uint32_t)eadk_timing_millis()-at;if(used>perf_step_max)perf_step_max=used;accum-=1000;steps++;if((uint32_t)eadk_timing_millis()-step_start>=12)break;}
                if(accum>=1000)accum%=1000;
                if(game.won||game.lost){view=RESULT;selected=game.won&&game.level+1<groups[game.group].first+groups[game.group].count?2:0;save();stale|=keys;if(game.won&&story_end[game.level]!=65535){story_return=RESULT;story_selection=selected;story_id=story_end[game.level];selected=story_id;view=STORY;}}
            }
        }else{
            accum=0;
            if(view==TITLE){
                if(hit&KEY(eadk_key_zero)){return_view=TITLE;view=HELP;}
                else if(hit&KEY(eadk_key_one)){if(game_load(&game,game.p.last)){loaded_view=PLAY;view=LOADING;stale|=keys;}}
                else if(ok){view=EPISODES;selected=0;}
                else if(back){view=QUIT;quit_view=TITLE;selected=0;}
            }else if(view==EPISODES){
                if(left&&selected)selected--;if(right&&selected+1<GROUP_COUNT)selected++;if(up&&selected>=2)selected-=2;if(down&&selected+2<GROUP_COUNT)selected+=2;
                if(back)view=TITLE;else if(ok){game.group=(uint16_t)selected;view=STAGES;selected=game.p.unlocked[game.group]-1;}
            }else if(view==STAGES){
                if(left&&selected)selected--;if(right&&selected+1<groups[game.group].count)selected++;if(up)selected=selected>=5?selected-5:0;if(down)selected=selected+5<groups[game.group].count?selected+5:(unsigned)groups[game.group].count-1;
                if(back){view=EPISODES;selected=game.group;}else if(ok&&selected<game.p.unlocked[game.group]){
                    if(game_load(&game,groups[game.group].first+selected)){loaded_view=PLAY;stale|=keys;
                        if(story_intro[game.level]!=65535&&!game.p.stars[game.level]){story_id=story_intro[game.level];story_return=PLAY;story_selection=0;selected=story_id;loaded_view=STORY;}
                        if(!game.p.help){game.p.help=1;return_view=loaded_view;loaded_view=HELP;}view=LOADING;
                    }
                }
            }else if(view==PAUSE){
                if(up)selected=(selected+3)%4;if(down)selected=(selected+1)%4;
                if(back||(ok&&selected==0)){view=PLAY;stale|=keys;}
                else if(ok&&selected==1){if(game_load(&game,game.level)){loaded_view=PLAY;view=LOADING;stale|=keys;}}
                else if(ok&&selected==2){view=STAGES;selected=game.level-groups[game.group].first;}
                else if(ok){view=QUIT;quit_view=PAUSE;selected=0;}
            }else if(view==RESULT){
                if(left)selected=(selected+2)%3;if(right)selected=(selected+1)%3;
                if(back||(ok&&selected==1)){view=STAGES;selected=game.level-groups[game.group].first;}
                else if(ok){unsigned level=selected==0?game.level:game.level+1;if(level<groups[game.group].first+groups[game.group].count&&game_load(&game,level)){loaded_view=PLAY;view=LOADING;stale|=keys;}else{view=STAGES;selected=0;}}
            }else if(view==LOADING){if(back){view=STAGES;selected=game.level-groups[game.group].first;stale|=keys;}else if(game_prepare(&game)){view=loaded_view;stale|=keys;}}
            else if(view==HELP){if(ok||back){view=return_view;stale|=keys;}}
            else if(view==STORY){if(ok||back){view=story_return;selected=story_selection;stale|=keys;}}
            else if(view==QUIT){if(up||down||left||right)selected^=1;if(back||(ok&&selected==0)){view=quit_view;selected=0;}else if(ok){if(save())exiting=true;else view=SAVE_ERROR;}}
            else if(view==SAVE_ERROR){if(ok&&save())exiting=true;if(back)exiting=true;if(hit&KEY(eadk_key_one))view=TITLE;}
        }
        current_view=view;
        if(painted!=view||hit||!perf_frames||(view==PLAY&&now-draw_at>=33)){
            if(painted!=view)render_invalidate();uint32_t at=(uint32_t)eadk_timing_millis();render(&game,view,selected,failed,push);uint32_t used=(uint32_t)eadk_timing_millis()-at;
            if(used>perf_max)perf_max=used;if(used>33)perf_slow++;perf_frames++;draw_at=now;painted=view;
        }
        eadk_timing_msleep(2);
    }
    uint32_t release=(uint32_t)eadk_timing_millis();while(eadk_keyboard_scan()&&(uint32_t)eadk_timing_millis()-release<2000)eadk_timing_msleep(10);
    return np_app_end();
}
