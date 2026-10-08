/* Development helper: renders the home screen icon (55 x 56) with the game's
 * own artwork. usage: icon out.ppm  (then convert to assets/icon.png) */
#include "scene.h"
#include <stdio.h>
#include <string.h>
static uint16_t fb[GFX_W * GFX_H];
int main(int argc, char **argv) {
  if (argc < 2) return 1;
  const int x0 = 132, y0 = 92, w = 55, h = 56;
  color_t bg = rgb(40, 125, 255), g1 = rgb(0, 102, 255);
  for (int s = 0; s < STRIPS; s++) {
    gfx_begin_strip(s);
    scene_background(bg, 1180, 0);
    scene_ground(g1, 0xffff, 40, y0 + 42, false, false);
    float cx = x0 + 27.5f, cy = y0 + 42 - 14;
    gfx_sprite_ex(SPR_CUBE1_S, (int)(cx * 16), (int)(cy * 16), 0, 360, 0, rgb(0, 255, 255), 256, BLEND_NORMAL);
    gfx_sprite_ex(SPR_CUBE1_P, (int)(cx * 16), (int)(cy * 16), 0, 360, 0, rgb(125, 255, 0), 256, BLEND_NORMAL);
    memcpy(fb + s * STRIP_H * GFX_W, gfx_strip, GFX_W * STRIP_H * sizeof(uint16_t));
  }
  FILE *f = fopen(argv[1], "wb");
  if (!f) return 1;
  fprintf(f, "P6\n%d %d\n255\n", w, h);
  for (int y = y0; y < y0 + h; y++)
    for (int x = x0; x < x0 + w; x++) {
      uint16_t c = fb[y * GFX_W + x];
      uint8_t p[3] = {(uint8_t)c_r(c), (uint8_t)c_g(c), (uint8_t)c_b(c)};
      fwrite(p, 1, 3, f);
    }
  fclose(f);
  return 0;
}
