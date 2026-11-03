/* Among Us: drawing. The calculator has no frame buffer for the whole screen, so a frame is drawn a band of rows at
 * a time: the scene is walked once per band and each primitive fills only the band's rows. */
#ifndef AU_GFX_H
#define AU_GFX_H
#include <stdbool.h>
#include <stdint.h>
#include "plat.h"
#include "res.h"

#define BAND 16

static inline uint16_t rgb565(uint32_t rgb) {
  return (uint16_t)(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
}
static inline uint16_t blend(uint16_t d, uint16_t s, int a) {
  /* a: 0-256 */
  uint32_t dd = (d | ((uint32_t)d << 16)) & 0x07E0F81F, ss = (s | ((uint32_t)s << 16)) & 0x07E0F81F;
  uint32_t r = (dd + (((ss - dd) * (uint32_t)(a >> 3)) >> 5)) & 0x07E0F81F;
  return (uint16_t)(r | (r >> 16));
}

extern uint16_t *gfx_band;
extern int gfx_y0;         /* the band's first row */

void gfx_init(uint16_t *band);
/* draws a frame: calls draw() once for each band, then pushes it */
void gfx_frame(void (*draw)(void));
static inline bool gfx_rows(int y0, int y1) { return y1 > gfx_y0 && y0 < gfx_y0 + BAND; }

/* the mix table's alphas (the players' pictures), and a colour's palette for them */
extern const uint8_t *gfx_mix_alpha;
void gfx_mix_palette(uint16_t *pal, uint32_t body, uint32_t shadow, uint32_t visor);

/* a picture with its pivot at (x, y), mirrored, alpha 0-256, inside the clip rectangle; pal: the colours of a P_MIX
 * picture (NULL for the picture's own) */
void gfx_spr(const Pic *p, int x, int y, bool flip, const uint16_t *pal, int alpha);
void gfx_spr_clip(const Pic *p, int x, int y, bool flip, const uint16_t *pal, int alpha, int cx0, int cy0, int cx1,
                  int cy1);
/* a picture scaled (and turned) around its pivot at (x, y) */
void gfx_pic(const Pic *p, float x, float y, float sx, float sy, float angle, const uint16_t *pal, int alpha);
/* a picture tinted by c (alpha kept): the outlines of usable consoles */
void gfx_spr_tint(const Pic *p, int x, int y, uint16_t c, int alpha);
void gfx_fill(int x, int y, int w, int h, uint16_t c, int alpha);
void gfx_rect(int x, int y, int w, int h, int t, uint16_t c);
void gfx_line(float x0, float y0, float x1, float y1, float w, uint16_t c, int alpha);
void gfx_disc(float cx, float cy, float r, uint16_t c, int alpha);
void gfx_poly(const float *xy, int n, uint16_t c, int alpha);
/* the polygon's spans on row y (pairs of x, clamped to the screen); returns how many */
int gfx_poly_spans(const float *xy, int n, int y, int16_t *out, int max);
/* text: y is the baseline; align 0 left, 1 centre, 2 right; outline thickness 0 for none; returns its width */
int gfx_text(int font, int x, int y, const char *s, uint16_t color, uint16_t outline, int thick, int align);
int gfx_text_width(int font, const char *s);
/* text wrapped to w pixels; returns the number of lines */
int gfx_text_wrap(int font, int x, int y, int w, int lh, const char *s, uint16_t color, uint16_t outline, int thick,
                  int align);
#endif
