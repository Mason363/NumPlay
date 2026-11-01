/* Five Nights at Freddy's 2: the platform (the calculator, or a computer for tests).
 *
 * plat_eadk.c: the calculator's EADK display, keyboard, time and files.
 * plat_host.c: a frame buffer that tests save as images, scripted keys and
 * simulated time (tests/play.c). */
#ifndef FNAF2_PLAT_H
#define FNAF2_PLAT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCREEN_W 320
#define SCREEN_H 240

/* keys held now, as bits */
enum {
  K_LEFT = 1 << 0, K_RIGHT = 1 << 1, K_UP = 1 << 2, K_DOWN = 1 << 3,
  K_OK = 1 << 4,        /* OK, EXE */
  K_BACK = 1 << 5,      /* Back: pause */
  K_HOME = 1 << 6,      /* Home, On/Off: keep the moment and quit */
  K_DEL = 1 << 7,       /* backspace: mute the call */
  K_0 = 1 << 8,         /* the digits: K_0 << n */
  K_ANY = 1 << 18
};
#define K_DIGIT(n) (K_0 << (n))
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
void plat_begin(void);
int plat_end(void);
/* an N0110 or N0115 (216 MHz, the N0120 has 550) */
bool plat_slow(void);
#endif
