/* 2 3 4 Players: the platform (the calculator, another target, or a computer for tests).
 *
 * plat_eadk.c: EADK's display, keyboard, time and files (the NumWorks, and every target of platform/).
 * plat_host.c: a frame buffer that tests save as images, scripted keys and simulated time (tests/play.c). */
#ifndef FP_PLAT_H
#define FP_PLAT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* the keys, numbered as EADK numbers them (eadk_key_*) */
enum {
  FK_LEFT = 0, FK_UP = 1, FK_DOWN = 2, FK_RIGHT = 3, FK_OK = 4, FK_BACK = 5, FK_HOME = 6, FK_ONOFF = 8, FK_SHIFT = 12,
  FK_BACKSPACE = 17, FK_SEVEN = 30, FK_RPAREN = 34, FK_ZERO = 48, FK_EXE = 52
};
#define KEY(k) ((uint64_t)1 << (k))

uint64_t plat_keys(void); /* the keys held now, a bit each */
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
