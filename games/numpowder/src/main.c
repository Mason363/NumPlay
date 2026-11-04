/* NumPowder: The Powder Toy on the NumWorks calculator, the game's loop.
 *
 * Inspired by The Powder Toy, not affiliated with its developers. Elements, font and icons ported from The
 * Powder Toy's source code (GPL-3.0) by Stanislaw K Skowronek, Simon Robertshaw and its contributors. Made by
 * Mason Chen as part of NumPlay. */
#include "ui.h"

#ifndef HOST
#ifndef NP_PORT /* (another target: platform/port.mk names the app) */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "NumPowder";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif
#endif

/* the frame time: as fast as it goes, up to the original's 60 frames a second */
#define FRAME_MS 16

static uint32_t fps_last, fps_frames;

/* a frame: the keys, the simulation, the stickmen, the drawing */
bool game_frame(void) {
  input_update();
  if (!ui_tick()) return false;
  bool run = !sys_pause || ui.step;
  ui.step = false;
  bool modal = ui.mode == MODE_CONFIRM || ui.mode == MODE_SAVES || ui.mode == MODE_TEXT;
  if (modal) run = false;
  sim_frame(run);
  int right = tool_is_element(ui.tool[1]) ? TYP(ui.tool[1]) : 0;
  stkm_frame(right);
  /* the interface's overlay lives on this stack until the frame is drawn */
  Overlay o;
  ov_reset(&o);
  bool panel = false;
  int push_w = ui_draw(&o, &panel);
  render_frame(&o, panel, push_w);
  /* FPS, as the original's: frames over the last half second */
  fps_frames++;
  uint32_t now = plat_millis();
  if (now - fps_last >= 500) {
    ui.fps100 = fps_frames * 100000u / (now - fps_last);
    fps_frames = 0, fps_last = now;
  }
  return true;
}

#ifdef TEST_SCENE
/* speed tests (make EXTRA=-DTEST_SCENE=n): a busy simulation from the start */
static void test_scene(void) {
  /* 1: still elements (about 4800 particles); 2: with fire, plasma and lava (glowing). (Void edges, as the first
   * measures were made with: what falls out goes) */
  settings.edge_mode = EDGE_VOID;
  sim_settings_changed();
  static const uint8_t still[] = {PT_STNE, PT_GLAS, PT_METL, PT_WOOD, PT_BRCK, PT_DMND, PT_IRON, PT_CNCT};
  static const uint8_t hot[] = {PT_SAND, PT_WATR, PT_STNE, PT_OIL, PT_GLAS, PT_LAVA, PT_FIRE, PT_PLSM, PT_METL, PT_SALT};
  for (int y = 4; y < YRES - 4; y++)
    for (int x = 2; x < XRES - 2; x++) {
      int k = x / 10 + y / 16 * 3;
      create_part(-1, x, y, TEST_SCENE == 1 ? still[k % sizeof still] : hot[k % sizeof hot], -1);
    }
  ui.intro = 0;
}
#endif

void game_init(void) {
  sim_init();
  render_init();
  ui_init();
#ifdef TEST_SCENE
  test_scene();
#endif
#ifdef PERF_HASH
  ui.debug = false;
#endif
  fps_last = plat_millis();
}

#ifndef HOST
uint32_t perf_frames __attribute__((used));
uint32_t perf_ms __attribute__((used));
uint32_t perf_max __attribute__((used));
uint32_t perf_render __attribute__((used));   /* the drawing's ms, all frames (render_frame) */
#ifdef PERF_HASH
/* the same simulation before and after a change (make EXTRA="-DSIM_SCENE=n -DPERF_HASH=frames", with tools/emu.py):
 * after that many frames of the simulation, a hash of its state, then the app waits on the frame drawn (the HUD and
 * its FPS hidden, so the screen can be compared too) */
uint32_t perf_hash __attribute__((used));
static void hash_mix(uint32_t *h, const void *p, size_t n) {
  for (size_t k = 0; k < n; k++) *h = (*h ^ ((const uint8_t *)p)[k]) * 16777619u;
}
static uint32_t state_hash(void) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < parts_active; i++)
    if (parts[i].type) hash_mix(&h, &i, sizeof i), hash_mix(&h, &parts[i], sizeof(Particle));
  hash_mix(&h, pv, sizeof pv), hash_mix(&h, vx, sizeof vx), hash_mix(&h, vy, sizeof vy), hash_mix(&h, hv, sizeof hv);
  hash_mix(&h, gravx, sizeof gravx), hash_mix(&h, gravy, sizeof gravy), hash_mix(&h, bmap, sizeof bmap);
  hash_mix(&h, emap, sizeof emap), hash_mix(&h, rng_state, sizeof rng_state), hash_mix(&h, &temp_dither, sizeof temp_dither);
  return h;
}
#endif

int main(void) {
  /* the particle map lives on this stack (12.8 KB): the heap is the particles' */
  uint16_t pmap_mem[YRES][XRES] __attribute__((aligned(8)));   /* (cleared a word at a time) */
  pmap_id = pmap_mem;
  plat_begin();
  game_init();
  uint32_t next = plat_millis();
  for (;;) {
    uint32_t t0 = plat_millis();
    if (!game_frame()) break;
#ifdef PERF_HASH
    if (frameCount >= PERF_HASH) {
      perf_hash = state_hash();
      for (;;) plat_sleep(1000);   /* (read by the emulator: quitting would clear the RAM) */
    }
#endif
    perf_frames++;
    perf_render += render_ms_total;
    uint32_t dt = plat_millis() - t0;
    perf_ms += dt;
    if (perf_frames > 30 && dt > perf_max) perf_max = dt;
    next += FRAME_MS;
    uint32_t now = plat_millis();
    if ((int32_t)(next - now) > 0) plat_sleep(next - now);
    else next = now;
  }
  return plat_end();
}
#endif
