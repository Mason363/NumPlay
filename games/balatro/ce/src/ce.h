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
unsigned ce_hash(unsigned h, const void *p, unsigned n);    /* h * 33 ^ each byte, 0 < n < 256 */
unsigned ce_hash_str(unsigned h, const char *s);            /* the same over a string */
unsigned ce_hash_words(unsigned h, const void *p, unsigned n); /* h * 33 + each 3 bytes (n of them), 0 < n < 256 */
/* n rows (1..255) of a glyph, a byte a row (the left column its top bit), d its left on the first row (the strip's
   rows are 320 bytes): in colour col, or the pixels under it darkened through tab (256-byte aligned) */
void ce_glyph(uint8_t *d, const uint8_t *rows, unsigned n, unsigned col);
void ce_glyph_shade(uint8_t *d, const uint8_t *rows, unsigned n, const uint8_t *tab);
/* a sprite's rows of runs into the strip, the sprite wholly between the clip's columns */
typedef struct {
  uint8_t *d;         /* the strip at the sprite's left, on the first row drawn */
  const uint8_t *rt;  /* that row's entry in the sprite's table of rows */
  const uint8_t *spr; /* the sprite */
  uint8_t n, sw;      /* rows (1..255), the sprite's width (1..255) */
  const uint8_t *tab; /* NULL: the pixels copied; or a table (256-byte aligned) darkening what is under them */
} ce_rle_t;
void ce_rle(ce_rle_t *b);
/* n pixels (1..255) of a sprite's row at another width, pixel i from row[(x8 + i * step) / 256] (row 256-byte aligned,
   255 where nothing is): copied (tab NULL), or what is under it darkened through tab */
void ce_scale_row(uint8_t *d, const uint8_t *row, unsigned n, unsigned x8, unsigned step, const uint8_t *tab);

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
/* the background's 64 colours (1555, the low byte first) at phase (0..511) from lut's 33 colours (blit.S) */
void ce_bg_pal(uint8_t *out, const uint8_t *lut, unsigned phase, const uint8_t *keep);
extern uint8_t ce_bg_ver;                    /* changes when the background's picture does (not its colours) */
int ce_bg_init(void);

/* ---- the fire (flame_ce.c) */
void ce_flame_rows(int k, int x, int y, int w, int h, int top, uint8_t *strip, int y0, int ya, int yb, int cx0, int cx1);
int ce_flame_frame(void);               /* changes when the fire does */

/* ---- the files (ce_main.c): kept when the game ends */
extern uint8_t ce_save_pending;
#endif
