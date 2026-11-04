/* circloO: drawing into a band of the screen (the screen does not fit the app's RAM), anti-aliased. */
#ifndef CO_GFX_H
#define CO_GFX_H
#include <stdint.h>

#define SCREEN_W 320
#define SCREEN_H 240
#define BAND 24

enum { F_SMALL, F_MEDIUM, F_LARGE, F_BIG };
enum { AL_LEFT, AL_CENTER, AL_RIGHT };

void gfx_init(uint16_t *band);
void gfx_band(int y0);          /* drawing goes to rows y0..y0+BAND-1 */
void gfx_push(void);            /* the band to the screen */
uint16_t rgb(int r, int g, int b);
uint16_t mix(uint16_t a, uint16_t b, int t);   /* a to b, t of 256 */
void gfx_fill(uint16_t c);
void gfx_rect(float x0, float y0, float x1, float y1, uint16_t c, int alpha);
void gfx_disc(float cx, float cy, float r, uint16_t c, int alpha);
void gfx_poly(const float *x, const float *y, int n, uint16_t c, int alpha);   /* convex */
void gfx_line(float x1, float y1, float x2, float y2, float w, uint16_t c, int alpha);
int gfx_text_w(int font, const char *s, int n);
int gfx_font_h(int font);
void gfx_text(int font, float x, float y, const char *s, int n, uint16_t c, int alpha, int align);
/* text broken into lines of at most w pixels (and at '#'); returns the lines' count. y: the top of the first */
int gfx_wrap(int font, float x, float y, const char *s, int w, uint16_t c, int alpha, int align, int draw);
#endif
