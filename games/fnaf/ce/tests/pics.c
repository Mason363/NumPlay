/* Decodes every picture of the CE version (with its bases), checks it against what tools/pack.py made
 * (tests/pics.crc: each picture's palette indices, their CRC-32) and writes it as the screen would show it:
 *   pics [OUT_DIR] [--data DIR] [--crc FILE] [--sums FILE]   (OUT_DIR/pic_<id>.ppm, rows doubled for the
 *   half-height ones; FILE: each picture's two 16-bit sums, tests/ce_pics's) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/fnaf.h"

extern const char *host_data_dir;
static const uint8_t *vars[DATA_VARS];

static uint32_t crc32(const uint8_t *p, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  while (n--) {
    c ^= *p++;
    for (int k = 0; k < 8; k++) c = c >> 1 ^ (0xEDB88320u & -(c & 1));
  }
  return ~c;
}

int main(int argc, char **argv) {
  const char *out = NULL, *crcfile = "tests/pics.crc", *sumfile = NULL, *rawfile = NULL;
  int raw = -1;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--data")) host_data_dir = argv[++i];
    else if (!strcmp(argv[i], "--crc")) crcfile = argv[++i];
    else if (!strcmp(argv[i], "--sums")) sumfile = argv[++i];
    else if (!strcmp(argv[i], "--raw")) raw = atoi(argv[i + 1]), rawfile = argv[i + 2], i += 2;
    else out = argv[i];
  }
  if (!ce_data(DATA_VAR, DATA_VARS, vars)) {
    fprintf(stderr, "missing %s\n", ce_missing);
    return 1;
  }
  static const Assets a = {vars, asset_at, pic_group, PIC_FIRST, MAP_FIRST, FONT_FIRST};
  static uint8_t temp[160 * 120];
  gfx_init(&a, temp, sizeof temp);
  FILE *cf = fopen(crcfile, "r"), *sf = sumfile ? fopen(sumfile, "wb") : NULL;
  static uint8_t buf[640 * 480];
  int bad = 0;
  for (int p = 0; p < PIC_COUNT; p++) {
    int w = pic_w(p), h = pic_h(p);
    memset(buf, 0, sizeof buf);
    pic_full(p, buf, w);
    unsigned want = 0;
    if (cf && fscanf(cf, "%x", &want) == 1 && want != crc32(buf, (size_t)(w * h))) {
      fprintf(stderr, "picture %d: not what tools/pack.py made\n", p);
      bad++;
    }
    if (p == raw) {   /* (its bytes as they are) */
      FILE *rf = fopen(rawfile, "wb");
      if (rf) fwrite(buf, 1, (size_t)(w * h), rf), fclose(rf);
    }
    if (sf) {   /* (tests/ce_pics's sums: two 16-bit ones) */
      uint16_t s1 = 0, s2 = 0;
      for (int i = 0; i < w * h; i++) s1 = (uint16_t)(s1 + buf[i]), s2 = (uint16_t)(s2 + s1);
      uint8_t b[4] = {(uint8_t)s1, (uint8_t)(s1 >> 8), (uint8_t)s2, (uint8_t)(s2 >> 8)};
      fwrite(b, 1, 4, sf);
    }
    if (!out) continue;
    const uint16_t *pal = map_palette(pic_group_of(p));
    int dbl = h <= 120 && w > 160 ? 2 : 1;   /* (the half-height ones as shown) */
    char path[512];
    snprintf(path, sizeof path, "%s/pic_%03d.ppm", out, p);
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
  if (sf) fclose(sf);
  printf("%d pictures, %d not as made\n", PIC_COUNT, bad);
  return bad != 0;
}
