/* Temple Run: flat drawing over the band being drawn (r3d.h): boxes, round boxes, discs,
 * polygons, text with an outline. Everything clips itself to the band's rows. */
#ifndef SS_DRAW_H
#define SS_DRAW_H
#include <stdint.h>
#include "assets.h"
#include "r3d.h"

#define C(rgb) r_565(rgb)
void d_fill(int x, int y, int w, int h, uint16_t c);
void d_fill_a(int x, int y, int w, int h, uint16_t c, int a256);
void d_vgrad(int x, int y, int w, int h, uint32_t top, uint32_t bottom);
void d_rrect(int x, int y, int w, int h, int r, uint16_t c, int a256);
/* a round box with a border t pixels wide, filled with a gradient */
void d_panel(int x, int y, int w, int h, int r, int t, uint32_t border, uint32_t top, uint32_t bottom);
void d_disc(float cx, float cy, float r, uint16_t c, int a256);
void d_ring(float cx, float cy, float r, float t, uint16_t c, int a256);
/* a convex polygon, its corners as x, y pairs (in any order around it) */
void d_poly(const float *xy, int n, uint16_t c, int a256);
void d_line(float x0, float y0, float x1, float y1, float t, uint16_t c, int a256);
void d_dim(int a256);   /* darkens the whole band */

/* text: its pen starts at x, on the baseline; returns the x after it */
int d_text(const Font *f, int x, int baseline, const char *s, uint16_t c, int a256);
int d_text_width(const Font *f, const char *s);
/* white-ish text with a dark outline t pixels thick, the game's lettering; align -1 left, 0 centre,
 * 1 right of x */
void d_label(const Font *f, int x, int baseline, const char *s, uint16_t fill, uint16_t outline, int t, int align);
#endif
