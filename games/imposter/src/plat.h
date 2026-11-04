/* Imposter: the platform (the calculator, another target, or a computer for tests).
 *
 * plat_eadk.c: EADK's display, keyboard, time and files (the NumWorks, and every target of platform/).
 * plat_host.c: a frame buffer that tests save as images, scripted keys and simulated time (tests/play.c). */
#ifndef IM_PLAT_H
#define IM_PLAT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* keys held now, as bits */
#define KB_LEFT ((uint32_t)1 << 0)
#define KB_RIGHT ((uint32_t)1 << 1)
#define KB_UP ((uint32_t)1 << 2)
#define KB_DOWN ((uint32_t)1 << 3)
#define KB_OK ((uint32_t)1 << 4)    /* OK */
#define KB_EXE ((uint32_t)1 << 5)   /* EXE: OK, and Done in the name editor */
#define KB_BACK ((uint32_t)1 << 6)  /* Back: menu, back a screen */
#define KB_HOME ((uint32_t)1 << 7)  /* Home, On/Off: save and quit */
#define KB_ERASE ((uint32_t)1 << 8) /* Backspace */
#define KB_ANY ((uint32_t)1 << 9)   /* any key at all */

/* keys held, and in *letters the letters their keys carry (bit 0 A ... bit 25 Z, bit 26 a space) */
uint32_t plat_keys(uint32_t *letters);
/* the keys carry letters (the NumWorks: alpha + a key types its letter, alpha or not) */
bool plat_typing(void);
uint32_t plat_millis(void);
void plat_sleep(uint32_t ms);
void plat_vblank(void);
uint32_t plat_random(void);
void plat_push(int x, int y, int w, int h, const uint16_t *px);
bool plat_save(const char *name, const void *data, uint32_t len);
const uint8_t *plat_load(const char *name, uint32_t *len); /* NULL if absent */
void plat_begin(void);
int plat_end(void);
#endif
