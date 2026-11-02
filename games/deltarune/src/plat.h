/* Deltarune: the platform (the calculator, or a computer for tests).
 *
 * plat_eadk.c: the calculator's EADK display, keyboard, time and files.
 * plat_host.c: a frame buffer that tests save as images, scripted keys and
 * simulated time (tests/play.c). */
#ifndef DR_PLAT_H
#define DR_PLAT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCREEN_W 320
#define SCREEN_H 240

/* keys held now, as bits: the game's Z, X and C are OK, Back and toolbox */
enum {
  K_LEFT = 1 << 0, K_RIGHT = 1 << 1, K_UP = 1 << 2, K_DOWN = 1 << 3,
  K_Z = 1 << 4,         /* OK, EXE: confirm */
  K_X = 1 << 5,         /* Back: cancel */
  K_C = 1 << 6,         /* toolbox, shift: the menu */
  K_HOME = 1 << 7,      /* Home, On/Off: save the moment and quit */
  K_ANY = 1 << 8
};
uint32_t plat_keys(void);
uint32_t plat_millis(void);
void plat_sleep(uint32_t ms);
void plat_push(int x, int y, int w, int h, const uint16_t *px);
void plat_fill(int x, int y, int w, int h, uint16_t c);
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
