/* What NumBlocks needs of the machine: the CE (plat_ce.c) or a computer, for tests and pictures (host/plat_host.c).
 * The screen is 320 x 240, a byte a pixel through a palette of 256 colours. */
#ifndef NBCE_PLAT_H
#define NBCE_PLAT_H
#include <stdbool.h>
#include <stdint.h>

#define SCREEN_W 320
#define SCREEN_H 240
extern uint8_t *scr;                  /* the screen shown */
void plat_init(void);
void plat_exit(void);
void plat_palette(int first, int n, const uint8_t *rgb);   /* entries first.., 3 bytes each */
uint32_t plat_keys(void);             /* the keys held (K_*), and those pressed since the last call */
void plat_poll(void);                 /* (the keys looked at while a picture is made: plat_keys has them) */
uint32_t plat_ms(void);
void plat_frame(void);                /* a picture is done (the computer shows or keeps it) */
uint8_t *plat_spare(void);            /* 76800 bytes of the machine's own (the CE: the second half of its screen memory) */
void *plat_grow(void *p, unsigned bytes);   /* the edit log's memory, made bytes long (NULL: no room) */
/* files (the CE: appvars; data read in place) */
const uint8_t *plat_load(const char *name, unsigned *len);
bool plat_save(const char *name, const void *a, unsigned na, const void *b, unsigned nb);
void plat_delete(const char *name);
/* the renderer's hot loops (render.c has them in C, the CE in assembly) */
void plat_expand(const uint8_t *src, uint8_t *dst, int rows);   /* 160-wide rows to 320 wide, doubled */
#endif
