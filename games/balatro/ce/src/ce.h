/* What the TI-84 Plus CE's own files share: the screen (8 bits a pixel through a palette of 256 colours), the
 * appvars, the colours' tables. On a computer (NP_HOST) tools/host.c stands in for the calculator. */
#ifndef CE_H
#define CE_H
#include <stddef.h>
#include <stdint.h>
#include "ce_art.h"
#include "gfx.h"

#define SCR_W 320
#define SCR_H 240

/* ---- the calculator, or tools/host.c */
extern uint8_t *ce_vram;                                   /* the screen: 320 x 240 palette indices */
void ce_set_palette(int first, int n, const uint16_t *c);  /* colours as the CE's 1555: 0rrrrrgg gggbbbbb */
const uint8_t *ce_appvar(const char *name, size_t *size);  /* an appvar's content, where it is (flash), or NULL */
uint32_t ce_ms(void);                                      /* milliseconds */

/* ---- the busiest loops (blit.S; C on a computer, in gfx_ce.c) */
void ce_shade(uint8_t *d, const uint8_t *tab, unsigned n);  /* d[i] = tab[d[i]]: tab 256-byte aligned, n > 0 */
void ce_unpack(uint8_t *d, const uint8_t *s, unsigned n);   /* n bytes of two levels into 2n indices from 192 */

/* ---- colours (gfx_ce.c) */
uint8_t ce_pal(C c);                    /* the palette index showing c: a fixed colour, or one taken for it */
const uint8_t *ce_shade_tab(int alpha); /* every index darkened by alpha/32 */
static inline uint16_t ce_1555(C c) { return (uint16_t)((c >> 11) << 10 | ((c >> 6) & 31) << 5 | (c & 31)); }

/* ---- the art (art_ce.c) */
int ce_art_init(void);                  /* 0 when an appvar is missing */
const uint8_t *ce_sprite(int id);       /* its rows (see tools/mkart.py), unpacked if it was packed */
extern const uint8_t *ce_lut;           /* BALLUT: shading, editions' and fire's colours, texts, fonts */
extern const uint8_t *ce_fire;          /* BALFIRE: the fire's smoke */
extern const uint8_t *ce_edpat;         /* BALED: the editions' sheen */

/* ---- the background (bg_ce.c) */
void ce_bg_row(uint8_t *dst, int y, int x0); /* row y's background, from x0 to the right edge */
extern uint8_t ce_bg_ver;                    /* changes when the background's picture does (not its colours) */
int ce_bg_init(void);

/* ---- the fire (flame_ce.c) */
void ce_flame_rows(int k, int x, int y, int w, int h, int top, uint8_t *strip, int y0, int ya, int yb, int cx0, int cx1);
int ce_flame_frame(void);               /* changes when the fire does */

/* ---- the files (ce_main.c): kept when the game ends */
extern uint8_t ce_save_pending;
#endif
