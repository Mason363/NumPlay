/* Decodes every picture of the CE version (with its bases) and writes it as the screen would show it:
 *   pics OUT_DIR [--data DIR]    (OUT_DIR/pic_<id>.ppm, rows doubled for the half-height ones) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/fnaf.h"

extern const char *host_data_dir;
static const uint8_t *vars[DATA_VARS];

int main(int argc, char **argv) {
  if (argc < 2) return 2;
  if (argc > 3 && !strcmp(argv[2], "--data")) host_data_dir = argv[3];
  if (!ce_data(DATA_VAR, DATA_VARS, vars)) {
    fprintf(stderr, "missing %s\n", ce_missing);
    return 1;
  }
  static const Assets a = {vars, asset_at, pic_group, PIC_FIRST, MAP_FIRST, FONT_FIRST};
  gfx_init(&a, ASSET_TABLES);
  static uint8_t buf[640 * 480];
  for (int p = 0; p < PIC_COUNT; p++) {
    int w = pic_w(p), h = pic_h(p);
    memset(buf, 0, sizeof buf);
    pic_full(p, buf, w);
    const uint16_t *pal = map_palette(pic_group_of(p));
    int dbl = h <= 120 && w > 160 ? 2 : 1;   /* (the half-height ones as shown) */
    char path[512];
    snprintf(path, sizeof path, "%s/pic_%03d.ppm", argv[1], p);
    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    fprintf(f, "P6\n%d %d\n255\n", w, h * dbl);
    for (int y = 0; y < h * dbl; y++)
      for (int x = 0; x < w; x++) {
        uint16_t c = pal[buf[(y / dbl) * w + x]];
        int r = c >> 10 & 31, gg = (c >> 5 & 31) << 1 | c >> 15, b = c & 31;
        uint8_t rgb[3] = {(uint8_t)(r * 255 / 31), (uint8_t)(gg * 255 / 63), (uint8_t)(b * 255 / 31)};
        fwrite(rgb, 1, 3, f);
      }
    fclose(f);
  }
  printf("%d pictures\n", PIC_COUNT);
  return 0;
}
