/* The Final Earth 2 for the NumWorks calculator: the game's loop.
 *
 * Inspired by The Final Earth 2 by Florian van Strien, not affiliated with
 * him. Made by Mason Chen as part of NumPlay. */
#include "bld.h"
#include "bg.h"
#include "data.h"
#include "game.h"
#include "input.h"
#include "save.h"
#include "scratch.h"
#include "story.h"
#include "uii.h"
#include "view.h"

#ifndef HOST
#ifndef NP_PORT /* (another target: platform/port.mk names the app) */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Final Earth 2";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif
#endif

bool game_running = true;
uint32_t game_ticks;
BgEdges city_edges;

static void set_background(int s) {
  BgPlanet p[BG_MAX_PLANETS];
  int n = 0;
  for (int i = 0; i < stories[s].nbackgrounds && n < BG_MAX_PLANETS; i++, n++) {
    const StoryBackground *b = &stories[s].backgrounds[i];
    p[n] = (BgPlanet){b->sprite, (uint16_t)b->width, (uint8_t)b->image, b->x, b->y, b->velocity};
  }
  bg_set_planets(p, n);
}

/* the view where the story wants it: centred on x 225 (or its viewStartX), its bottom at viewYFromBottom */
static void camera_start(int s) {
  view_edges(&city_edges);
  float hw = SCREEN_W / 2.0f / camera.zoom, hh = SCREEN_H / 2.0f / camera.zoom;
  float cx = stories[s].viewStartX ? stories[s].viewStartX : 225, cy = 150;
  camera.x = cx - hw, camera.y = cy - hh;
  camera_clamp_view();
  if (stories[s].viewYFromBottom) camera.y = stories[s].viewYFromBottom - 2 * hh;
}

void game_start(int s, const NewCityOptions *opt) {
  saved_view.valid = false;
  story_new_city(s, opt);
  set_background(s);
  view_edges(&city_edges);
  bg_clouds_start(&city_edges);
  camera.zoom = 1;
  camera_start(s);
}

void game_after_load(void) {
  set_background(story.index);
  view_edges(&city_edges);
  bg_clouds_start(&city_edges);
  camera.zoom = 1;
  camera_start(story.index);
  if (saved_view.valid) {
    /* (City.load: its viewPos and zoomScale) */
    camera.zoom = saved_view.zoom;
    camera.x = saved_view.x, camera.y = saved_view.y;
    camera_clamp_view();
  }
}

void game_init(void) {
  save_restore();   /* (after the app was installed again) */
  bg_init(plat_millis() * 2654435761u + 1);
  rnd_seed(plat_millis() * 2246822519u + 7);
#ifdef START_STORY
  ui_before_city();
  game_start(START_STORY, NULL);   /* test builds: make EXTRA=-DSTART_STORY=3 */
  ui_after_load();
#else
  ui_init();   /* the title screen */
#endif
}

/* how much longer than a tick (33.3 ms) the last frame took: the original's timeMod follows the real time
 * (PIXI's ticker), so the city keeps its pace when frames are slow */
float frame_time_mod = 1;

void game_tick(void) {
  input_update();
  if (key_pressed(K_HOME)) {   /* saves, then quits (a failed save says so, and the game goes on) */
    if (ui_home()) game_running = false;
    return;
  }
  ui_tick();
  sim_tick(frame_time_mod);
  if (game_ticks % 30 == 0) view_edges(&city_edges);
  bg_clouds_update(2 * frame_time_mod * city.simulationSpeed * 0.6666667f * (city.paused ? 0 : 1), &city_edges);
  game_ticks++;
}

void game_draw(void) {
  /* the interface's primitives live on this stack until the frame is drawn (gfx_end) */
  UiFrame f;
  gfx_begin();
  view_draw();
  ui_draw(&f);
  gfx_end();
}

#ifndef HOST
uint32_t perf_frames __attribute__((used));
uint32_t perf_ms __attribute__((used));
uint32_t perf_max __attribute__((used));

int main(void) {
  /* memory only needed while a search or pass runs, on this stack: some calculator software gives apps less RAM */
  Scratch scr;
  scratch = &scr;
  gfx_set_band_memory(scr.band);
  plat_begin();
  game_init();
  uint32_t next = plat_millis(), last = next;
  while (game_running) {
    uint32_t t0 = plat_millis();
    float tm = (float)(t0 - last) * (FPS / 1000.0f);
    last = t0;
    frame_time_mod = tm < 1.1f ? 1 : tm > 2 ? 2 : tm;
    game_tick();
    if (!game_running) break;
    game_draw();
    perf_frames++;
    if (perf_frames > 30 && plat_millis() - t0 > perf_max) perf_max = plat_millis() - t0;
    next += 1000 / FPS + (game_ticks % 3 == 0 ? 1 : 0);   /* 33.3 ms */
    uint32_t now = plat_millis();
    if ((int32_t)(next - now) > 0) plat_sleep(next - now);
    else next = now;   /* late: the next tick's frame_time_mod makes up for it */
  }
  return plat_end();
}
#endif
