/* src/blit.S's loops in C, for the computer build. */
#include "../src/ce.h"

static void tile_rows(const Cell *c, uint8_t *d, unsigned a, unsigned b, unsigned rows) {
  const uint8_t *s = c->tile;
  for (int r = 0; r < 8; r++, s += 8, d += 320) {
    if (!(rows >> r & 1)) continue;
    if (c->opaque >> r & 1) memcpy(d + a, s + a, b - a);
    else if (c->drawn >> r & 1)
      for (unsigned i = a; i < b; i++)
        if (s[i]) d[i] = s[i];
  }
}
void blit_row(const Cell *cells, unsigned n, uint8_t *dst, unsigned rows) {
  for (unsigned i = 0; i < n; i++, dst += 8)
    if (cells[i].drawn & rows) tile_rows(&cells[i], dst, 0, 8, rows);
}
void blit_clip(const Cell *cell, uint8_t *dst, unsigned a, unsigned b, unsigned rows) {
  if (cell->drawn & rows) tile_rows(cell, dst, a, b, rows);
}
void blit_copy(uint8_t *dst, const uint8_t *src, unsigned n) { memcpy(dst, src, n); }
void blit_rle(uint8_t *dst, const uint8_t *rows, unsigned nrows) {
  for (unsigned r = 0; r < nrows; r++, dst += 320) {
    uint8_t *d = dst;
    int n = *rows++;
    while (n--) {
      d += rows[0];
      memcpy(d, rows + 2, rows[1]);
      d += rows[1];
      rows += 2 + rows[1];
    }
  }
}
void build_cells(const uint8_t *fg, const uint8_t *bg, const TEnt *table, Cell *ofg, Cell *obg, uint8_t *vis,
                 unsigned n) {
  for (unsigned i = 0; i < n; i++) {
    ofg[i] = table[fg[i]].c, obg[i] = table[bg[i]].c;
    if (ofg[i].opaque == 0xFF) obg[i].opaque = obg[i].drawn = 0, vis[i] = 0;
    else vis[i] = obg[i].opaque != 0xFF;
  }
}
void blit_rect(uint8_t *dst, const uint8_t *src, unsigned w, unsigned h, unsigned src_step) {
  for (unsigned r = 0; r < h; r++, dst += 320, src += src_step) memcpy(dst, src, w);
}
