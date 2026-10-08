/* Development helper: render gameplay frames to PPM. usage: shot level tick out.ppm [replay] */
#include "scene.h"
#include "fx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint16_t fb[GFX_W * GFX_H];
static void render(void) {
  for (int s = 0; s < STRIPS; s++) {
    gfx_begin_strip(s);
    scene_draw();
    memcpy(fb + s * STRIP_H * GFX_W, gfx_strip, GFX_W * STRIP_H * sizeof(uint16_t));
  }
}
static void save(const char *path) {
  FILE *f = fopen(path, "wb");
  fprintf(f, "P6\n%d %d\n255\n", GFX_W, GFX_H);
  for (int i = 0; i < GFX_W * GFX_H; i++) { uint8_t c[3] = {c_r(fb[i]), c_g(fb[i]), c_b(fb[i])}; fwrite(c, 1, 3, f); }
  fclose(f);
}
int main(int argc, char **argv) {
  int lvl = atoi(argv[1]), tick = atoi(argv[2]);
  static Level L; static Game g;
  if (!level_load_builtin(&L, (unsigned)lvl)) { puts("load fail"); return 1; }
  int ticks[6000], vals[6000], n = 0;
  if (argc > 4) { FILE *f = fopen(argv[4], "r"); int t, v; while (f && fscanf(f, "input=%d,%d\n", &t, &v) == 2) { ticks[n] = t; vals[n++] = v; } if (f) fclose(f); }
  game_start(&g, &L, argc > 5);
  fx_reset();
  int pos = 0; bool held = false;
  for (int i = 1; i <= tick; i++) {
    while (pos < n && ticks[pos] <= i) held = vals[pos++];
    game_step(&g, held);
    if (i % 6 == 0) fx_update(&g, 6.0f / 240, rgb(125, 255, 0), rgb(0, 255, 255), false);
    if (g.dead) { printf("dead at %d x=%.1f\n", i, g.p.x); break; }
  }
  SceneOpts o = {0};
  o.p1 = rgb(125, 255, 0); o.p2 = rgb(0, 255, 255); o.show_percent = true; o.attempt = 1; o.time = tick / 240.f;
  scene_prepare(&g, &o);
  render();
  save(argv[3]);
  printf("x=%.1f y=%.1f cam=(%.1f,%.1f) mode=%d\n", g.p.x, g.p.y, g.cam_x, g.cam_y, g.p.mode);
  return 0;
}
