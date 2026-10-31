/* Temple Run: the platform (the calculator, or a computer for tests).
 *
 * plat_eadk.c: the calculator's EADK display, keyboard, time and files.
 * plat_host.c: a frame buffer that tests save as images, scripted keys and
 * simulated time (tests/play.c). */
#ifndef TR_PLAT_H
#define TR_PLAT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCREEN_W 320
#define SCREEN_H 240

/* keys held now, as bits */
enum {
  K_LEFT = 1 << 0, K_RIGHT = 1 << 1,   /* the arrows: turn */
  K_UP = 1 << 2, K_DOWN = 1 << 3,      /* Up or 8: jump; Down or 2: slide */
  K_OK = 1 << 4,                       /* OK, EXE, 5: the wings; confirm */
  K_BACK = 1 << 5,                     /* Back, backspace: pause; back */
  K_ONE = 1 << 6,                      /* 1: a head start */
  K_THREE = 1 << 7,                    /* 3: a mega head start */
  K_HOME = 1 << 8,                     /* Home, On/Off: save and quit */
  K_LEAN_L = 1 << 9, K_LEAN_R = 1 << 10,   /* 4 and 6: lean */
  K_SHIFT = 1 << 11,                   /* shift: the arrows lean */
  K_ANY = 1 << 12
};
uint32_t plat_keys(void);
uint32_t plat_millis(void);
void plat_sleep(uint32_t ms);
void plat_push(int x, int y, int w, int h, const uint16_t *px);
bool plat_save(const char *name, const void *data, uint32_t len);
const uint8_t *plat_load(const char *name, uint32_t *len);   /* NULL if absent */
/* a file of len bytes to write in place (the calculator's storage is RAM): plat_reserve gives its
 * bytes, plat_reserved says they are written; NULL if there is no room */
uint8_t *plat_reserve(const char *name, uint32_t len);
void plat_reserved(const char *name, uint32_t len);
bool plat_remove(const char *name);
uint32_t plat_random(void);
void plat_begin(void);
int plat_end(void);
/* an N0110 or N0115 (216 MHz, the N0120 has 550) */
bool plat_slow(void);
#endif
