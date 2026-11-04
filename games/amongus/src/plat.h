/* Among Us: the platform (the calculator, or a computer for tests).
 *
 * plat_eadk.c: the calculator's EADK display, keyboard, time and files.
 * plat_host.c: a frame buffer that tests save as images, scripted keys and simulated time (tests/play.c). */
#ifndef AU_PLAT_H
#define AU_PLAT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCREEN_W 320
#define SCREEN_H 240

/* keys held now, as bits */
enum {
  K_LEFT = 1 << 0,
  K_RIGHT = 1 << 1,
  K_UP = 1 << 2,
  K_DOWN = 1 << 3,
  K_OK = 1 << 4,     /* OK: use, report, vent (the action button) */
  K_EXE = 1 << 5,    /* EXE: kill */
  K_BACK = 1 << 6,   /* Back: pause, close */
  K_HOME = 1 << 7,   /* Home, On/Off: save and quit */
  K_TOOL = 1 << 8,   /* Toolbox: the map (the sabotage map for impostors) */
  K_D0 = 1 << 9,     /* digits 0-9: K_D0 << n */
  K_ANY = 1u << 31
};
#define K_DIGITS (0x3FFu << 9)
uint32_t plat_keys(void);
uint32_t plat_millis(void);
void plat_sleep(uint32_t ms);
void plat_push(int x, int y, int w, int h, const uint16_t *px);
bool plat_save(const char *name, const void *data, uint32_t len);
const uint8_t *plat_load(const char *name, uint32_t *len);   /* NULL if absent */
bool plat_remove(const char *name);
void plat_begin(void);
int plat_end(void);
#endif
