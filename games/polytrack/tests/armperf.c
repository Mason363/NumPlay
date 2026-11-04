/* The frame rate on the calculator's processor, for tools/armperf.py: a track's recorded run (routes.h) driven
 * with its exact keys, drawn as the game draws a race (the picture's detail going down when a frame is slow and up
 * when there is time, as in main.c), each frame's work timed. tools/emu.py counts time by instructions, as fast as
 * an N0110: the slowest calculators. */
#include <eadk.h>
#include <string.h>
#include "../src/car.h"
#include "../src/draw.h"
#include "../src/render.h"
#include "../src/save.h"
#include "routes.h"

#ifndef NP_PORT /* (another target: platform/port.mk names the app) */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "PTPerf";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif

/* in: the track, then go (set by the emulator once the app waits); out: frames, work (ms) in all, the slowest,
   frames over 33 ms, frames at qualities 2..5, the finish, at 1, the triangles and the chunks of the slow frames in all, at 0 */
__attribute__((used, externally_visible)) volatile uint32_t perf_track, perf_go;
__attribute__((used, externally_visible)) uint32_t perf_out[16], perf_done;
/* (for a profile of the slow frames: the frame going on, and each frame's work in ms) */
__attribute__((used, externally_visible)) volatile uint32_t perf_frame;
__attribute__((used, externally_visible)) uint8_t perf_ms[4096];

void plat_push(int x, int y, int w, int h, const uint16_t *px) {
  eadk_display_push_rect((eadk_rect_t){(uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h}, (const eadk_color_t *)px);
}

static car_t car;
static pt_file sv;
static pt_ui ui;
static chase_t chase;
static camera_t cam;
/* the run's keys, read as the frames go (10 ms a letter) */
static const char *rs;
static unsigned rkey;
static uint32_t rleft;
static unsigned next_keys(void) {
  while (!rleft && *rs) {
    char ch = *rs++;
    if (ch < 'a' || ch > 'p') continue;
    uint32_t r = 0;
    while (*rs >= '0' && *rs <= '9') r = r * 10 + (uint32_t)(*rs++ - '0');
    rkey = (unsigned)(ch - 'a'), rleft = (r ? r : 1) * 10;
  }
  if (!rleft) return 0;
  rleft--;
  return rkey;
}

int main(void) {
  while (!perf_go) {}
  int t = (int)perf_track;
  rs = pt_routes[t];
  memset(&sv, 0, sizeof sv);
  sv.p.track = (uint8_t)t;
  world_load(t);
  car_reset(&car);
  chase_reset(&chase, car.pos, car.rot);
  memset(&ui, 0, sizeof ui);
  ui.screen = V_RACE, ui.started = 1;
  rd_detail = RD_LEVELS - 1;
  uint32_t dt = 33, f = 0;
  /* (the race waits at the start for a driving key: the picture settles meanwhile) */
  for (int i = 0; i < 30; i++) {
    uint32_t t0 = (uint32_t)eadk_timing_millis();
    car_look look = car_look_of(&car);
    chase_update(&chase, &cam, 0.033f, look.pos, look.rot, car.speed);
    pt_render(&sv, &car, &cam, &look, NULL, &ui);
    rd_adapt((uint32_t)eadk_timing_millis() - t0);
  }
  while (!car.finish && f < 600000) {
    uint32_t t0 = (uint32_t)eadk_timing_millis();
    for (uint32_t i = 0; i < dt && !car.finish; i++, f++) car_step(&car, next_keys());
    uint32_t t1 = (uint32_t)eadk_timing_millis();
    rd_ease_for(t1 - t0);
    car_look look = car_look_of(&car);
    chase_update(&chase, &cam, (float)dt / 1000.0f, look.pos, look.rot, car.speed);
    ui.clock += dt;
    pt_render(&sv, &car, &cam, &look, NULL, &ui);
    uint32_t spent = (uint32_t)eadk_timing_millis() - t0;
    if (perf_frame < sizeof perf_ms) perf_ms[perf_frame] = (uint8_t)(spent < 255 ? spent : 255);
    perf_frame++;
    perf_out[0]++, perf_out[1] += spent;
    if (spent > perf_out[2]) perf_out[2] = spent;
    if (spent > 33) perf_out[3]++, perf_out[10] += (uint32_t)rd_tris, perf_out[11] += (uint32_t)rd_chunks, perf_out[14] += t1 - t0, perf_out[15] += (uint32_t)car.ncache;
    if (rd_detail < 3) perf_out[rd_detail == 2 ? 9 : rd_detail == 1 ? 12 : 13]++;
    else perf_out[4 + rd_detail - 3]++;
    rd_adapt(spent);
    rd_ease = 0;
    dt = spent > 33 ? spent : 33;     /* (the game's frames: 33 ms at least on these calculators) */
  }
  perf_out[8] = car.finish;
  perf_done = 1;
  return 0;
}
