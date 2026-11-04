/* The Five Nights at Freddy's games on the TI-84 Plus CE: the calculator (ce_calc.c: graphx, keypadc, fileioc)
 * or a computer for tests (ce_host.c), the same to the games.
 *
 * The screen is 320 x 240 palette indices, two buffers: a frame is drawn into scr, then ce_show shows it and scr
 * is the other buffer. The palette changes with the frame that shows it (ce_pal, ce_pal_dirty). */
#ifndef CE_H
#define CE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCR_W 320
#define SCR_H 240

extern uint8_t *scr;            /* the frame being drawn */
extern uint16_t ce_pal[256];    /* the palette (1555 words: the CE's), shown with the next frame */
extern bool ce_pal_dirty;       /* ce_pal changed */

bool ce_init(void);             /* false: the data is missing (ce_missing names it) */
void ce_quit(void);
/* shows the frame drawn; keep: the next frame starts as a copy of it */
void ce_show(bool keep);

/* the keys held: the CE's (CK_*) */
enum {
  CK_LEFT = 1 << 0, CK_RIGHT = 1 << 1, CK_UP = 1 << 2, CK_DOWN = 1 << 3,
  CK_2ND = 1 << 4, CK_ENTER = 1 << 5, CK_CLEAR = 1 << 6, CK_MODE = 1 << 7, CK_DEL = 1 << 8,
  CK_0 = 1 << 9,                /* the digits: CK_0 << n */
  CK_ON = 1 << 19, CK_ALPHA = 1 << 20
};
#define CK_DIGIT(n) ((uint32_t)CK_0 << (n))
uint32_t ce_keys(void);
uint32_t ce_ticks(void);        /* 32768 a second */

/* the data: the appvars <prefix>0, <prefix>1... (archived, read where they are) */
bool ce_data(const char *prefix, int count, const uint8_t **vars);
extern char ce_missing[9];      /* the first one missing */
/* the RAM nobody uses (after the program), for the games' big buffers */
uint8_t *ce_ram(size_t *len);
/* the save: an appvar of a fixed size, in RAM while the game runs (written in place), archived when it ends.
 * ce_save_open makes it (zeros when new) before ce_ram is taken; ce_save_data gives its bytes, ce_save_done
 * writes them back (on the calculator they are written where they are) */
uint8_t *ce_save_open(const char *name, size_t size);
void ce_save_done(void);
#endif
