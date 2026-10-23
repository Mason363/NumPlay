#include "game.h"
#include "inflate.h"
#include "render.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
static Game game;
static Game incremental;
static unsigned strips;
static void display(int x,int y,int w,int h,const uint16_t*p){assert(x==0&&y>=0&&y<240&&w==320&&h==8&&p);strips++;}
static void layouts(void){
    game_new(&game,1);assert(game_start(&game,0));
    for(unsigned i=0;i<GROUP_COUNT;i++){
        render_invalidate();render(&game,EPISODES,i,false,display);game.group=(uint16_t)i;
        for(unsigned n=0;n<groups[i].count;n++){render_invalidate();render(&game,STAGES,n,false,display);}
    }
    assert(game_start(&game,0));game.score=UINT32_MAX;
    for(unsigned view=0;view<=SAVE_ERROR;view++){render_invalidate();render(&game,view,0,false,display);}
    render_invalidate();render(&game,STORY,story_intro[0],false,display);
    render_invalidate();render(&game,LOADING,0,false,display);
    /* Extreme off-screen coordinates used to walk long lines in every strip. */
    for(int sign=-1;sign<=1;sign+=2){game.camera_x=sign*100000;game.camera_y=sign*100000;game.zoom=5;game.explosion_age=12;render_invalidate();render(&game,PLAY,0,false,display);}
    assert(strips>30);printf("PASS: all menu pages and text bounds, clipped off-screen rendering\n");
}
static void preparation(void){
    game_new(&game,1);game_new(&incremental,1);assert(game_start(&game,0));assert(game_load(&incremental,0));unsigned n=1;while(!game_prepare(&incremental))n++;assert(n==12);assert(memcmp(&game,&incremental,sizeof(game))==0);
    printf("PASS: incremental preparation preserves the original starting state\n");
}
static void eagle_progress(void){
    game_new(&game,1);assert(game_start(&game,0));game.won=1;game.score=levels[0].gold;game_finish(&game);uint32_t best=BEST_SCORE(&game.p,0);unsigned stars=game.p.stars[0];
    game.feather=1;game.score=0;game_finish(&game);assert(game.eagle_percent==0&&BEST_SCORE(&game.p,0)==best&&game.p.stars[0]==stars);
    game.score=10000000;game_finish(&game);assert(game.eagle_percent==100&&BEST_EAGLE(&game.p,0)==100&&(game.p.feathers[0]&1));
    uint8_t record[SAVE_BYTES];Progress copy={0};save_encode(&game.p,record);assert(save_decode(&copy,record,sizeof(record)));assert(memcmp(&copy,&game.p,sizeof(copy))==0);
    game.feather=0;game.score=best+100;game_finish(&game);assert(BEST_EAGLE(&game.p,0)==100&&BEST_SCORE(&game.p,0)==best+100);
    /* A version-1 record has plain scores and a separate earned-feather bit. */
    game.p.best[0]=12345;save_encode(&game.p,record);record[4]=1;uint32_t c=~0u;for(unsigned i=0;i<SAVE_BYTES-4;i++){c^=record[i];for(unsigned j=0;j<8;j++)c=(c>>1)^(0xedb88320u&-(c&1));}c=~c;for(unsigned i=0;i<4;i++)record[SAVE_BYTES-4+i]=(uint8_t)(c>>(i*8));
    assert(save_decode(&copy,record,sizeof(record)));assert(BEST_SCORE(&copy,0)==12345&&BEST_EAGLE(&copy,0)==100);
}
static void powers(void){
    const unsigned kinds[]={OBJ_BIRD_RED,OBJ_BIRD_YELLOW,OBJ_BIRD_BLUE,OBJ_BIRD_BLACK,OBJ_BIRD_WHITE,OBJ_BIRD_GREEN,OBJ_BIRD_REDBIG};
    for(unsigned k=0;k<7;k++){
        assert(game_start(&game,0));game.bodies[1].type=(uint16_t)kinds[k];assert(game_launch(&game));unsigned count=game.count;float vx=game.bodies[1].vx;bool used=game_power(&game);
        if(k==0||k==6)assert(!used);else assert(used);
        if(k==1)assert(game.bodies[1].vx>vx*2);
        if(k==2)assert(game.count==count+2);
        if(k==3)assert(!game.active&&!(game.bodies[1].flags&ALIVE));
        if(k==4)assert(game.count==count+1&&(game.bodies[count].flags&EGG));
        if(k==5)assert(game.bodies[1].vx<0);
        for(unsigned i=0;i<300;i++){game_step(&game);assert(game_validate(&game));}
    }
    game_start(&game,0);unsigned pigs=game.pigs;game_eagle(&game);assert(game.eagle_armed&&game.pigs==pigs);assert(game_launch(&game));assert(game.bodies[game.active].type==OBJ_BIRD_SARDINE);bool swoop=false;
    for(unsigned i=0;i<500;i++){game_step(&game);if(game.eagle_timer)swoop=true;assert(game_validate(&game));}assert(swoop);
}
int main(int argc,char**argv){
    game_new(&game,1);uint8_t record[SAVE_BYTES];Progress p=game.p;save_encode(&p,record);assert(save_decode(&game.p,record,sizeof(record)));assert(!save_decode(&game.p,NULL,0));
    for(unsigned i=0;i<SAVE_BYTES;i++){record[i]^=1;assert(!save_decode(&game.p,record,sizeof(record)));record[i]^=1;}
    preparation();layouts();eagle_progress();powers();unsigned ticks=0,contacts=0;uint32_t rng=0x12345678;for(unsigned l=0;l<LEVEL_COUNT;l++){
        incremental.p=game.p;assert(game_start(&game,l));assert(game_load(&incremental,l));while(!game_prepare(&incremental)){}assert(memcmp(&game,&incremental,sizeof(game))==0);
        assert(game_validate(&game));assert(game.birds&&game.pigs);assert(game.count<=BODY_CAP);assert(!game.overflows);
        for(unsigned i=0;i<60;i++){game_step(&game);assert(game_validate(&game));ticks++;}
        rng=rng*1664525u+1013904223u;game.angle=((int)(rng%90)-10)*PI/180;game.power=.4f+(rng%61)*.01f;
        assert(!game.won);assert(game_launch(&game));assert(!game_launch(&game));
        for(unsigned i=0;i<240;i++){if(i==40)game_power(&game);game_step(&game);assert(game_validate(&game));ticks++;}
        if(game.max_contacts>contacts)contacts=game.max_contacts;
        if(game.overflows){fprintf(stderr,"Contact overflow in %s: %u\n",levels[l].name,game.overflows);return 1;}
    }
    printf("PASS: %u stages, %u simulation ticks; max %u contacts. Game=%zu Progress=%zu\n",LEVEL_COUNT,ticks,contacts,sizeof(Game),sizeof(Progress));fflush(stdout);
    FILE*report=fopen("host-report.json","w");assert(report);fprintf(report,"{\"game_bytes\":%zu,\"progress_bytes\":%zu,\"active\":%zu,\"angle\":%zu,\"score\":%zu,\"won\":%zu,\"launched\":%zu,\"level\":%zu,\"tick\":%zu,\"overflows\":%zu,\"ticks\":%u,\"max_contacts\":%u}\n",sizeof(Game),sizeof(Progress),offsetof(Game,active),offsetof(Game,angle),offsetof(Game,score),offsetof(Game,won),offsetof(Game,launched),offsetof(Game,level),offsetof(Game,tick),offsetof(Game,overflows),ticks,contacts);fclose(report);
    for(unsigned i=0;i<GROUP_COUNT;i++)p.unlocked[i]=groups[i].count;
    for(unsigned i=0;i<LEVEL_COUNT;i++)if(strcmp(levels[i].name,"easterspecial-3")==0)p.last=(uint16_t)i;
    save_encode(&p,record);report=fopen("large-stage.sav","wb");assert(report);assert(fwrite(record,1,SAVE_BYTES,report)==SAVE_BYTES);fclose(report);
    if(argc>1&&strcmp(argv[1],"solve")==0){
        unsigned best=0,best_angle=0,best_birds=0;for(unsigned degrees=5;degrees<=80;degrees++){
            game_start(&game,0);game.angle=degrees*PI/180;game.power=1;game_launch(&game);
            for(unsigned i=0;i<900&&!game.won&&!game.lost;i++){game_step(&game);if(!game.active&&!game.wait&&game.launched<game.birds)game_launch(&game);}
            if(game.won&&game.score>best){best=game.score;best_angle=degrees;best_birds=game.launched;}
        }
        if(!best){fprintf(stderr,"No stage 1 solution found\n");return 1;}
        printf("Stage 1 clear: angle %u, score %u, birds %u\n",best_angle,best,best_birds);report=fopen("stage1-solution.json","w");assert(report);fprintf(report,"{\"angle\":%u,\"score\":%u,\"birds\":%u}\n",best_angle,best,best_birds);fclose(report);
    }
    return 0;
}
