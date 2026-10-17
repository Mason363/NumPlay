/* Host gameplay and rendering test of the ACTUAL native source. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define main dino_app_main
#include "../src/main.c"
#undef main
static uint16_t screen[W*H];
static uint64_t pixels_uploaded, pushes;

uint64_t eadk_keyboard_scan(void) {return 0;}
uint32_t eadk_timing_millis(void) {return 0;}
void eadk_timing_msleep(uint32_t ms) {(void)ms;}
void eadk_display_push_rect(eadk_rect_t r,const uint16_t *px) {
  assert(r.width>0 && r.height>0 && r.x+r.width<=W && r.y+r.height<=H);
  pixels_uploaded+=(uint64_t)r.width*r.height;
  pushes++;
  for(int y=0;y<r.height;y++) for(int x=0;x<r.width;x++)
    screen[(y+r.y)*W+x+r.x]=px[y*r.width+x];
}
/* Validate what ACTUALLY appears on screen, not just object coordinates.
 * Below the HUD and above the floor, with no other objects spawned, only
 * the bird can supply dark pixels in x=100..300. */
static int bird_screen_left(void) {
  for(int x=100;x<300;x++)for(int y=145;y<180;y++)
    if(screen[y*W+x]==RGB(0x535353))return x;
  return -1;
}
/* A dirty-only frame MUST leave exactly the same framebuffer as full
 * repaint. This catches leftover pixels when sprites move or disappear. */
static uint16_t screen_copy[W*H];
static void verify_dirty_matches_full(void) {
  memcpy(screen_copy,screen,sizeof screen);
  display_cache_valid=false; /* Force the following draw to upload all tiles. */
  frame();
  assert(memcmp(screen_copy,screen,sizeof screen)==0);
}
/* Bird and cactus visible silhouettes are measured in nonoverlapping
 * scanlines. Their pixels must travel left together throughout frames. */
static int cactus_screen_left(void) {
  for(int x=100;x<300;x++)for(int y=181;y<195;y++)
    if(screen[y*W+x]==RGB(0x535353))return x;
  return -1;
}
/* Every bird/cactus pair must stay at least one second apart, even while
 * multi-cactus groups are allowed to sit 20px apart. */
static void assert_safe_mixed_pairs(void) {
  for(int i=0;i<count;i++)for(int j=i+1;j<count;j++) {
    if((obstacles[i].kind==BIRD)==(obstacles[j].kind==BIRD))continue;
    int d=obstacles[i].x-obstacles[j].x;
    if(d<0)d=-d;
    assert(d>=MIN_MIXED_GAP);
  }
}
static void screenshot(const char *f) {
  FILE *fp=fopen(f,"wb"); assert(fp);
  fprintf(fp,"P6\n%d %d\n255\n",W,H);
  for(int i=0;i<W*H;i++) {
    uint16_t c=screen[i];
    unsigned char rgb[3]={(unsigned char)(((c>>11)&31)*255/31),
                          (unsigned char)(((c>>5)&63)*255/63),
                          (unsigned char)((c&31)*255/31)};
    fwrite(rgb,1,3,fp);
  }
  fclose(fp);
}
int main(int argc,char **argv) {
  scene=TITLE; dino_y=DY*256;score=0;
  frame();
  assert(pixels_uploaded==(uint64_t)W*H); /* First frame covers entire screen. */
  if(argc>=2) screenshot(argv[1]);
  pixels_uploaded=pushes=0;
  frame();
  assert(pixels_uploaded==0 && pushes==0); /* Do NOT resend the same pixels. */

  /* A single moving bird changes just a few tiles, not 320x240 pixels.
   * This is the hardware-performance regression the old tests missed. */
  start_game();
  frame(); /* Draw the otherwise empty game screen. */
  pixels_uploaded=pushes=0;
  frame();
  assert(pixels_uploaded==0 && pushes==0);
  obstacles[0]=(obstacle){220,165,21,18,BIRD};count=1;
  pixels_uploaded=pushes=0;
  frame();
  assert(pixels_uploaded>0 && pixels_uploaded<(uint64_t)W*H/4);
  assert(pushes>0);
  uint64_t first_bird_pixels=pixels_uploaded;
  verify_dirty_matches_full();
  render_ms=70;
  pixels_uploaded=pushes=0;
  frame();
  assert(pixels_uploaded>0 && pixels_uploaded<(uint64_t)W*H/4);
  uint64_t next_bird_pixels=pixels_uploaded;
  verify_dirty_matches_full();
  printf("Screen upload check: %llu pixels for bird appearance, %llu for bird movement, vs %d previously every frame.\n",
         (unsigned long long)first_bird_pixels,(unsigned long long)next_bird_pixels,W*H);
  obstacles[0].x=100;render_ms=24;
  frame();verify_dirty_matches_full();
  count=0;frame();verify_dirty_matches_full(); /* Object removal clears pixels. */
  /* Dark-to-light transition legitimately repaints the full background. */
  score=300;
  pixels_uploaded=pushes=0;
  frame();
  assert(pixels_uploaded>=(uint64_t)W*H*3/4);
  verify_dirty_matches_full();
  score=0;
  frame();


  /* Reference: original Python H=-13, gravity=+2, 49 px at frame 7. */
  start_game();assert(scene==PLAY && score==0 && count==0);
  int y=dino_y;
  cactus_meter=bird_meter=-100000;
  update(true,false);assert(dino_y==y-13*256);
  for(int t=0;t<6;t++) update(false,false);
  assert(dino_y==y-49*256);
  for(int t=0;t<7;t++) update(false,false);
  assert(dino_y==DY*256 && score==7 && speed==10);
  assert(GAME_FRAME_MS==50 && DRAW_FRAME_MS==33); /* 14 x 50ms = 700ms in the air. */

  /* R4 scroll is deliberately faster; physics jump stays 700ms. */
  start_game();ticks=359;cactus_meter=bird_meter=-100000;
  update(false,false);assert(speed==10 && cloud_speed==5 && score==0);
  update(false,false);assert(speed==11 && cloud_speed==6 && score==1);
  ticks=1440;update(false,false);assert(speed==14 && cloud_speed==9);
  /* Exactly 9px left / 50ms for BOTH cactus and bird, from tick zero. */
  start_game();cactus_meter=bird_meter=-100000;
  obstacles[0]=(obstacle){280,DY,14,23,CACTUS};
  obstacles[1]=(obstacle){270,165,21,18,BIRD};count=2;
  update(false,false);
  assert(obstacles[0].x==271 && obstacles[1].x==261);
  update(false,false);
  assert(obstacles[0].x==262 && obstacles[1].x==252);

  /* R4: enforce the same screen direction and step for both obstacle types. */
  assert(scroll_step()==9);
  for(int i=0;i<16;i++) {
    assert(obstacles[0].x-obstacles[1].x==10); /* Relative gap NEVER changes. */
    update(false,false);
    assert(obstacles[0].x-obstacles[1].x==10);
  }

  /* R5: reject birds beside a cactus AND reject cacti beside a bird.
   * Check boundaries and BOTH real spawn paths, not just the helper. */
  assert(MIN_MIXED_GAP==300);
  start_game();count=1;
  obstacles[0]=(obstacle){400,DY,14,23,CACTUS};
  assert(!safe_mixed_spawn(400,BIRD));
  assert(!safe_mixed_spawn(699,BIRD));
  assert(safe_mixed_spawn(700,BIRD));
  assert(safe_mixed_spawn(100,BIRD));
  /* Force repeated bird attempts with a cactus dangerously close. */
  score=450;bird_meter=100000;cactus_meter=-100000;
  for(int i=0;i<500;i++) {spawn();assert(count==1);}
  obstacles[0].x=100; /* now the queued bird at 400 is safe */
  for(int i=0;i<500 && count==1;i++)spawn();
  assert(count>1);assert_safe_mixed_pairs();

  start_game();count=1;
  obstacles[0]=(obstacle){350,165,21,18,BIRD};
  assert(!safe_mixed_spawn(350,CACTUS));
  assert(!safe_mixed_spawn(649,CACTUS));
  assert(safe_mixed_spawn(650,CACTUS));
  bird_meter=-100000;cactus_meter=100000;
  for(int i=0;i<500;i++) {spawn();assert(count==1);}
  obstacles[0].x=20; /* a new group at 350+ is now safe */
  for(int i=0;i<500 && count==1;i++)spawn();
  assert(count>1);assert_safe_mixed_pairs();

  /* Check a real bird + its separately queued follower cactus. */
  start_game();score=450;bird_meter=100000;cactus_meter=-100000;
  for(int i=0;i<500 && count==0;i++)spawn();
  assert(count>=1);assert_safe_mixed_pairs();
  for(int i=0;i<count;i++)if(obstacles[i].kind==CACTUS)
    assert(obstacles[i].x>=700);

  /* A pair of adjacent original cacti is actually jumpable with normal
   * timing. Before R4 they moved too slowly for the 700ms jump window. */
  start_game();cactus_meter=bird_meter=-100000;
  count=2;
  obstacles[0]=(obstacle){120,DY,14,23,CACTUS};
  obstacles[1]=(obstacle){140,DY,14,23,CACTUS};
  for(int i=0;i<14;i++) {
    update(i==0,false);
    assert(scene==PLAY); /* No unavoidable collision midair or on landing. */
  }
  assert(dino_y==DY*256 && obstacles[0].x==-6 && obstacles[1].x==14);

  /* On-screen parity: visible bird AND cactus must scroll LEFT at the
   * exact same rate. Compare visible sprite pixels, not just x coordinates. */
  start_game();cactus_meter=bird_meter=-100000;last_step_ms=0;
  obstacles[0]=(obstacle){240,DY,14,23,CACTUS};
  obstacles[1]=(obstacle){200,150,21,18,BIRD};count=2;
  const uint32_t shared_times[]={0,20,49,50,70,99,100,130,149,150,180,199,200,250,300};
  int previous_bird=999,previous_cactus=999;
  for(unsigned i=0;i<sizeof(shared_times)/sizeof(shared_times[0]);i++) {
    assert(!timed_input(shared_times[i],0,0));
    frame();
    int bx=bird_screen_left();
    int cx=cactus_screen_left();
    assert(bx>=0 && cx>=0);
    assert(bx<=previous_bird && cx<=previous_cactus);
    assert(cx-bx==20); /* The silhouettes keep exactly the same spacing. */
    previous_bird=bx;previous_cactus=cx;
  }

  /* Regression: the original sprite's beak faces LEFT. Mirroring it is wrong. */
  bg=RGB(0xFFFFFF);ink=RGB(0x535353);band_y=0;
  for(int i=0;i<W*BAND;i++) pixels[i]=bg;
  SPRITE(BIRD_WING_DOWN,80,4,ink);
  assert(pixels[4*W+84]==ink); /* Source first rect x=4..6: left-facing beak. */
  assert(pixels[4*W+95]==bg); /* Old mirrored version wrongly filled this spot. */

  /* Regression: past code visibly moved the bird RIGHT at each physics tick.
   * Test the ACTUAL rendered pixels across multiple logical tick boundaries.
   * The bird must never appear to move back to the right. */
  start_game();cactus_meter=bird_meter=-100000;
  obstacles[0]=(obstacle){220,165,21,18,BIRD};count=1;
  last_step_ms=0;
  const uint32_t times[]={0,25,49,50,75,99,100,125,149,150,175,199,200,225,250,275,300};
  int previous_x=999;
  for(unsigned i=0;i<sizeof(times)/sizeof(times[0]);i++) {
    assert(!timed_input(times[i],0,0));
    frame();
    int visual_x=bird_screen_left();
    assert(visual_x>=0);
    assert(visual_x<=previous_x); /* Bird never moves to the right. */
    previous_x=visual_x;
  }
  assert(score==3 && ticks==6);
  assert(obstacles[0].x==166); /* 54px LEFT after 300ms. */

  /* Even if hardware frame pushes are slow, simulation catches up. */
  start_game();cactus_meter=bird_meter=-100000;
  obstacles[0]=(obstacle){250,165,21,18,BIRD};count=1;
  last_step_ms=0;
  assert(!timed_input(340,0,0));
  assert(score==3 && ticks==6 && obstacles[0].x==196);
  assert(render_ms==40);
  assert(!timed_input(875,0,0));
  assert(score==9 && ticks==17 && obstacles[0].x==97);
  assert(render_ms==25);
  /* Input is still checked on draw-only frames. */
  assert(!timed_input(885,PAUSE,PAUSE) && scene==PAUSED);
  uint32_t frozen=ticks;
  assert(!timed_input(1200,0,0) && ticks==frozen);
  assert(!timed_input(1201,PAUSE,PAUSE) && scene==PLAY);
  assert(!timed_input(1230,0,0) && ticks==frozen);
  assert(!timed_input(1251,0,0) && ticks==frozen+1);

  /* Python ducking changes gravitational acceleration, even in the air. */
  start_game();cactus_meter=bird_meter=-100000;
  update(true,false);update(false,true);
  assert(duck && gravity_units==9 && dino_y<DY*256);
  update(false,false);assert(!duck && gravity_units==2);
  /* Cloud motion follows bird/cloud speed, not cactus speed. */
  start_game();cactus_meter=bird_meter=-100000;
  clouds[0]=(decoration){200,80};cloud_count=1;cloud_clock=-99999;
  update(false,false);assert(clouds[0].x==198);
  update(false,false);assert(clouds[0].x==195);

  /* Controls: Clear pauses and resumes; Back exits immediately. */
  start_game();
  assert(!handle_input(PAUSE,PAUSE) && scene==PAUSED);
  uint32_t before=ticks;
  assert(!handle_input(0,0) && ticks==before);
  assert(!handle_input(PAUSE,PAUSE) && scene==PLAY && ticks==before);
  assert(handle_input(KEY(eadk_key_back),KEY(eadk_key_back)) && scene==PLAY);
  scene=TITLE;
  assert(handle_input(KEY(eadk_key_back),KEY(eadk_key_back)));
  scene=PAUSED;
  assert(handle_input(KEY(eadk_key_home),KEY(eadk_key_home)));

  /* Original Python cactus collision: d[] x anchor and y position. */
  start_game();high_score=0;
  count=1; obstacles[0]=(obstacle){DX+22,DY,14,23,CACTUS};
  assert(collision());
  update(false,false);assert(scene==DEAD && high_score==score);
  start_game();assert(scene==PLAY && score==0 && count==0);

  /* Simulate a long random run and verify all bounded game arrays stay safe.
   * This also checks that scores, difficulty and every spawn type co-exist. */
  start_game();
  for(int i=0;i<2500;i++) {
    if(scene==DEAD) start_game();
    update((i%17)<4, (i%29)>24);
    assert(count>=0 && count<=12);
    assert(cloud_count>=0 && cloud_count<=3);
    assert(dust_count>=0 && dust_count<=5);
    assert(pebble_count>=0 && pebble_count<=2);
    assert_safe_mixed_pairs();
  }

  /* Both sprite variants and the sky can render safely in all 240 rows. */
  start_game();score=328;ticks=1312;
  count=3;
  obstacles[0]=(obstacle){244,DY,14,23,CACTUS};
  obstacles[1]=(obstacle){289,DY,30,23,LARGE_CACTUS};
  obstacles[2]=(obstacle){152,GROUND-28,21,18,BIRD};
  clouds[0]=(decoration){170,70};cloud_count=1;
  frame();
  if(argc>=3)screenshot(argv[2]);
  printf("R5 PASSED: R4 speed/jump/bird motion unchanged, safe bird-cactus spawn order, no mixed overlaps, double cactus feasible.\n");
  return 0;
}
