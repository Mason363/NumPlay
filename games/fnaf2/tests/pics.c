/* Draws every picture of data.bin on a computer (build/pics/<id>.ppm), to check the codec:
 * tests/check.py compares them with what tools/pack.py was given. */
#include <stdio.h>
#include "../src/gfx.h"
#include "../src/res.h"

extern const uint8_t fnaf2_data[];
const uint8_t *fnaf2_bin;
void host_shot(const char *path);

static int cur, offx;
static void draw(void) {
  gfx_fill(0, gfx_y0, SCREEN_W, BAND_H, BLACK);
  pic_draw(cur, -offx, 0);
}

int main(int argc, char **argv) {
  static uint16_t band[SCREEN_W * BAND_H];
  gfx_set_band_memory(band);
  fnaf2_bin = fnaf2_data;
  const char *dir = argc > 1 ? argv[1] : "build/pics";
  for (cur = 0; cur < PIC_COUNT; cur++)
    for (offx = 0; offx < pic_w(cur); offx += SCREEN_W) {
      gfx_frame(draw);
      char path[256];
      snprintf(path, sizeof path, "%s/%d_%d.ppm", dir, cur, offx);
      host_shot(path);
    }
  return 0;
}
