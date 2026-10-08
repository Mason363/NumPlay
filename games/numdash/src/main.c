/* Main loop: fixed 240 Hz physics, one rendered frame per loop iteration,
 * drawn in horizontal strips straight to the LCD. */
#include "app.h"
#if PLATFORM_DEVICE
#include <eadk.h>
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "NumDash";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif

/* Frames drawn so far, read by tools/emu.py to measure frame rates. */
__attribute__((used)) volatile uint32_t perf_frames;

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
#if PLATFORM_DEVICE
  uint16_t strip[GFX_W * STRIP_H] __attribute__((aligned(8)));
  gfx_strip = strip;
#endif
  if (!platform_init()) return 1;
#if PLATFORM_DEVICE
  platform_begin();
#endif
  app_init();
  uint32_t prev = platform_millis(), acc = 0, fps_start = prev, frames = 0, pending = 0, render_ms = 30;
  while (app.running) {
    uint32_t now = platform_millis(), elapsed = now - prev;
    prev = now;
    /* never advance further than the player could have seen */
    if (elapsed > 50) elapsed = 50;
    acc += elapsed * ND_HZ;
    uint32_t keys = platform_keys();
    if (acc < 1000) {
      pending |= keys;
    } else {
      keys |= pending;
      pending = 0;
      while (acc >= 1000 && app.running) {
        app_tick(keys);
        acc -= 1000;
      }
    }
    if (!app.running) break;
    app_frame(elapsed / 1000.0f);
    /* Sync to the LCD refresh when a frame fits comfortably in it. */
    platform_frame_begin(render_ms < 22);
    uint32_t t0 = platform_millis();
    for (int s = 0; s < STRIPS; s++) {
      gfx_begin_strip(s);
      app_draw();
      platform_strip(s * STRIP_H, STRIP_H, gfx_strip);
    }
    platform_frame_end();
    render_ms = (render_ms * 3 + (platform_millis() - t0)) / 4;
    frames++;
    perf_frames++;
    if (now - fps_start >= 1000) {
      app.fps = frames * 1000 / (now - fps_start);
      frames = 0;
      fps_start = now;
    }
#if !PLATFORM_DEVICE
    if (elapsed < 8) platform_sleep(8 - elapsed);
#endif
  }
  platform_close();
#if PLATFORM_DEVICE
  return platform_end();
#else
  return 0;
#endif
}
