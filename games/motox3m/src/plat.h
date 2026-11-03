/* Moto X3M: the platform (the calculator, or a computer for tests).
 *
 * plat_eadk.c: the calculator's EADK display, keyboard, time and files.
 * plat_host.c: a frame buffer that tests save as images, scripted keys and
 * simulated time (tests/play.c). */
#ifndef MX_PLAT_H
#define MX_PLAT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCREEN_W 320
#define SCREEN_H 240

/* keys held now, as bits */
enum {
  K_LEFT = 1 << 0,      /* Left, 4: lean back */
  K_RIGHT = 1 << 1,     /* Right, 6: lean forwards */
  K_UP = 1 << 2,        /* Up, 8: gas */
  K_DOWN = 1 << 3,      /* Down, 2: brake */
  K_OK = 1 << 4,        /* OK, EXE, 5: choose, go on after a crash */
  K_BACK = 1 << 5,      /* Back: pause, back a screen */
  K_HOME = 1 << 6,      /* Home, On/Off: save and quit */
  K_ANY = 1 << 7
};
uint32_t plat_keys(void);
uint32_t plat_millis(void);
void plat_sleep(uint32_t ms);
void plat_push(int x, int y, int w, int h, const uint16_t *px);
bool plat_save(const char *name, const void *data, uint32_t len);
const uint8_t *plat_load(const char *name, uint32_t *len);   /* NULL if absent */
/* a file of len bytes to write in place (the calculator's storage is RAM): plat_reserve gives its bytes,
 * plat_reserved says they are written; NULL if there is no room */
uint8_t *plat_reserve(const char *name, uint32_t len);
void plat_reserved(const char *name, uint32_t len);
bool plat_remove(const char *name);
void plat_begin(void);
int plat_end(void);
bool plat_slow(void);   /* an N0110 or N0115 (216 MHz, the N0120 has 550) */
#endif
