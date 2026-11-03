/* Moto X3M: drawing. The calculator has no frame buffer for the whole screen, so a frame is drawn a band of rows
 * at a time: the scene is walked once per band and each primitive fills only the band's rows. */
#ifndef MX_GFX_H
#define MX_GFX_H
#include <stdbool.h>
#include <stdint.h>
#include "plat.h"
#include "res.h"

#define BAND 12

static inline uint16_t rgb565(uint32_t rgb) {
  return (uint16_t)(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
}

extern uint16_t *gfx_band;
extern int gfx_y0;         /* the band's first row */

void gfx_init(uint16_t *band);
/* draws a frame: calls draw() once for each band, then pushes it */
void gfx_frame(void (*draw)(void));
static inline bool gfx_rows(float y0, float y1) { return y1 >= gfx_y0 && y0 < gfx_y0 + BAND; }

/* an affine map from the screen to a picture's pixels: u = ux*x + uy*y + u0, v = vx*x + vy*y + v0 */
typedef struct {
  float ux, uy, u0, vx, vy, v0;
} Map;

/* a polygon (screen points, even-odd) filled with a picture: wrapped (tiles) or clipped to it */
void gfx_poly_pic(const float *xy, int n, const Pic *p, const Map *m, bool wrap, int alpha);
void gfx_poly_color(const float *xy, int n, uint16_t c, int alpha);
/* a picture turned and scaled around its pivot, the pivot at (x, y); alpha 0-256 */
void gfx_pic(int id, float x, float y, float sx, float sy, float angle, int alpha);
/* a picture upright at its top-left (no pivot), scaled */
void gfx_pic_at(int id, float x, float y, float scale, int alpha);
void gfx_fill(int x, int y, int w, int h, uint16_t c, int alpha);
void gfx_line(float x0, float y0, float x1, float y1, float w, uint16_t c, int alpha);
/* text: y is the baseline; align 0 left, 1 centre, 2 right; outline 0 none; its width */
int gfx_text(int font, float x, float y, const char *s, uint16_t color, uint16_t outline, int thick, int align);
int gfx_text_width(int font, const char *s);
#endif
