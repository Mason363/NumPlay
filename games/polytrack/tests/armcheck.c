/* The official tracks' runs (routes.h) on the calculator's processor, for tools/armcheck.py: each track driven to
 * its finish, the frame and the car's hash kept in check_out for the emulator to read. They must be the ones the
 * computers get. */
#include <eadk.h>
#include "../src/car.h"
#include "routes.h"

#ifndef NP_PORT /* (another target: platform/port.mk names the app) */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "PTCheck";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif

__attribute__((used, externally_visible)) uint32_t check_out[2 * PT_NTRACKS], check_done;
/* (the emulator sets how many tracks, then go, once the app waits) */
__attribute__((used, externally_visible)) volatile uint32_t check_tracks, check_go;

static car_t c;
int main(void) {
  while (!check_go) {}
  for (int t = 0; t < (int)check_tracks && t < PT_NTRACKS; t++) {
    world_load(t);
    car_reset(&c);
    const char *s = pt_routes[t];
    uint32_t f = 0;
    while (*s && !c.finish) {
      char ch = *s++;
      unsigned k = ch >= 'a' && ch <= 'p' ? (unsigned)(ch - 'a') : ch >= 'A' && ch <= 'P' ? (unsigned)(ch - 'A') | IN_RESET : 99;
      if (k == 99) continue;
      uint32_t r = 0;
      while (*s >= '0' && *s <= '9') r = r * 10 + (uint32_t)(*s++ - '0');
      if (!r) r = 1;
      for (uint32_t i = 0; i < r * 10 && !c.finish; i++, f++) car_step(&c, k);
    }
    for (; f < 600000 && !c.finish; f++) car_step(&c, 0);
    check_out[2 * t] = c.finish;
    check_out[2 * t + 1] = car_hash(&c);
  }
  check_done = 1;
  return 0;
}
