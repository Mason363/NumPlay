/* Sonic the Hedgehog: the frame (VBlank's work: the joypad, the colours, the planes drawn as the
 * camera moves, the animated art and the HUD's numbers; then the picture and real time), the game
 * modes one after another as the original's main loop runs them, and Home, which saves and quits
 * from anywhere. */
#include "s1.h"
#include "game.h"
#include "plat.h"

#ifdef HOST /* (tests/play.c has the computer's main) */
#define main game_main
void host_frame(void);
#endif

Vars V;
Obj objs[NOBJ];

/* ------------------------------------------------------------------ data access */
const uint8_t *map_get(int id) { return DATA(((const uint32_t *)DATA(D_MAPS))[id]); }
const uint8_t *map_frame(const uint8_t *map, int f) { return map + ((const uint16_t *)(const void *)map)[1 + f]; }
const uint8_t *ani_get(int id) { return DATA(((const uint32_t *)DATA(D_ANIS))[id]); }

/* ------------------------------------------------------------------ numbers */
uint32_t RandomNumber(void) {
  uint32_t seed = V.random ? V.random : 0x2A6D365A;
  uint32_t m = seed * 41;
  uint16_t lo = (uint16_t)(m + (m >> 16));
  V.random = (uint32_t)lo << 16 | (m & 0xFFFF);
  /* (the original's d0: the old seed's high word and the new number) */
  return (seed & 0xFFFF0000u) | lo;
}

int16_t CalcSine(uint8_t angle, int16_t *cos) {
  const int16_t *t = (const int16_t *)DATA(D_TAB_SINE_DATA);
  if (cos) *cos = t[angle + 0x40];
  return t[angle];
}

uint8_t CalcAngle(int16_t x, int16_t y) {
  if (!x && !y) return 0x40;
  const uint8_t *t = DATA(D_TAB_ANGLE_DATA);
  uint16_t ax = (uint16_t)(x < 0 ? -x : x), ay = (uint16_t)(y < 0 ? -y : y);
  int a;
  if (ay < ax) a = t[(uint16_t)(((uint32_t)ay << 8) / ax)];
  else a = 0x40 - t[(uint16_t)(((uint32_t)ax << 8) / ay)];
  if (x < 0) a = 0x80 - a;
  if (y < 0) a = 0x100 - a;
  return (uint8_t)a;
}

/* ------------------------------------------------------------------ the frame */
/* (the compiler's own jumps: the C library's would bring its unwinder along) */
#if (defined(HOST) && defined(__APPLE__)) || defined(NP_PORT) /* (no __builtin_setjmp on arm64) */
#include <setjmp.h>
static jmp_buf quit_jump;
#define __builtin_setjmp setjmp
#define __builtin_longjmp longjmp
#else
static void *quit_jump[5];
#endif
static uint32_t next_frame;    /* when the next frame is due (ms * 60, to keep fractions) */
static uint32_t frame_ms60;
static int skipped;
uint32_t sonic_gfx_pending[23]; /* (the patterns Sonic_LoadGfx asked for, for VBlank) */
int sonic_gfx_count;

static void ReadJoypads(void) {
  uint32_t k = plat_keys();
  if (k & K_HOME) game_quit();
  uint8_t held = (uint8_t)(k & 0xFF);
  V.jpadpress1 = (uint8_t)(held & ~V.jpadhold1);
  V.jpadhold1 = held;
}

static void transfers(void) {
  memcpy(cram, V.f_wtr_state ? V.palette_water : V.palette, sizeof cram);
  extern uint16_t vdp_water_cram[64];
  memcpy(vdp_water_cram, V.palette_water, sizeof vdp_water_cram);
  vdp_vscroll_a = V.scrposy_vdp;
  vdp_vscroll_b = V.bgscrposy_vdp;
}

void sonic_gfx_apply(void) {
  if (!V.f_sonframechg) return;
  extern void sonic_dplc_apply(void);
  sonic_dplc_apply();
  V.f_sonframechg = 0;
}

static void level_updates(void) {
  memcpy(V.cam_dup, V.cam, sizeof V.cam);
  memcpy(V.scroll_flags_dup, V.scroll_flags, sizeof V.scroll_flags);
  LoadTilesAsYouMove();
  AnimateLevelGfx();
  HUD_Update();
}

static void timer(void) {
  if (V.generictimer) V.generictimer--;
}

static void vblank(uint8_t r) {
  switch (r) {
  case id_VBlank_Sega: ReadJoypads(); transfers(); timer(); break;
  case id_VBlank_SegaPCM: timer(); break;
  case id_VBlank_Title: ReadJoypads(); transfers(); LoadTilesAsYouMove_BGOnly(); timer(); break;
  case id_VBlank_Paused:
    if (V.gamemode == id_Special) goto special;
    /* fall through */
  case id_VBlank_Levels:
    ReadJoypads(), transfers(), sonic_gfx_apply(), level_updates(), timer();
    break;
  case id_VBlank_SpecialStage:
  special:
    ReadJoypads(), transfers(), PalCycle_SS(), sonic_gfx_apply(), timer();
    break;
  case id_VBlank_TitleCards: case id_VBlank_Ending:
    ReadJoypads(), transfers(), sonic_gfx_apply(), level_updates();
    break;
  case id_VBlank_PaletteFade: ReadJoypads(); transfers(); break;
  case id_VBlank_Continue: ReadJoypads(); transfers(); sonic_gfx_apply(); timer(); break;
  default: /* a lag frame (never here: the game always has the time) */
    break;
  }
}

/* whether the picture can be left out this frame to keep up (the N0110 and N0115) */
static bool keep_up(void) {
  uint32_t now = plat_millis() * 60;
  if ((int32_t)(now - next_frame) > 1000 * 4) next_frame = now; /* far behind (a long load): start again */
  return (int32_t)(now - next_frame) > (int32_t)frame_ms60 && skipped < 3;
}

uint32_t perf_frames __attribute__((used));   /* read by tools/emu.py */
uint32_t perf_updates __attribute__((used));

void WaitForVBlank(void) {
  uint8_t r = V.vblank_routine;
  perf_updates++;
  V.vblank_routine = 0;
  V.f_hblank_pal = 1;
  vblank(r);
  V.vblank_count++;
  vdp_hint_line = V.zone == id_LZ && (V.gamemode & 0x7F) == id_Level && V.hblank_line < 224 ? V.hblank_line : -1;
  if (keep_up()) skipped++;
  else {
    skipped = 0;
    perf_frames++;
    vdp_render();
    pause_overlay_draw();
  }
#ifdef HOST
  host_frame();
#endif
  next_frame += frame_ms60;
  for (;;) {
    int32_t wait = (int32_t)(next_frame - plat_millis() * 60);
    if (wait <= 0) break;
    plat_sleep((uint32_t)(wait / 60 > 0 ? wait / 60 : 1));
    if (wait < 60) break;
  }
}

void game_quit(void) { __builtin_longjmp(quit_jump, 1); }

/* ------------------------------------------------------------------ the main loop */
int main(void) {
  plat_begin();
  plat_fill(0, 0, SCREEN_W, SCREEN_H, 0);
  frame_ms60 = 1000;   /* 1/60 s, in ms * 60 */
  next_frame = plat_millis() * 60;
  vdp_clear_planes();
  save_restore_copy();
  if (!__builtin_setjmp(quit_jump)) {
    game_init();
    for (;;) {
      switch (V.gamemode & 0x1C) {
      case id_Sega: GM_Sega(); break;
      case id_Title: GM_Title(); break;
      case id_Demo: case id_Level: GM_Level(); break;
      case id_Special: GM_Special(); break;
      case id_Continue: GM_Continue(); break;
      case id_Ending: GM_Ending(); break;
      case id_Credits: GM_Credits(); break;
      }
    }
  }
  game_save_on_quit();
  plat_fill(0, 0, SCREEN_W, SCREEN_H, 0);
  return plat_end();
}
