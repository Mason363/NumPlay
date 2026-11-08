/* Doodle Jump: the platform (the calculator, or a computer for the tests).
 *
 * plat_eadk.c: the calculator's EADK display, keyboard, time and files.
 * tests/play.c: a frame buffer the tests save as pictures, scripted keys and simulated time. */
#ifndef DJ_PLAT_H
#define DJ_PLAT_H
#include <stdbool.h>
#include <stdint.h>

#define SCREEN_W 320
#define SCREEN_H 240

/* keys held now, as bits */
enum {
  K_LEFT = 1 << 0,  /* Left, 4 */
  K_RIGHT = 1 << 1, /* Right, 6 */
  K_UP = 1 << 2,    /* Up, 8 */
  K_DOWN = 1 << 3,  /* Down, 2 */
  K_OK = 1 << 4,    /* OK, EXE, 5 */
  K_BACK = 1 << 5,  /* Back */
  K_HOME = 1 << 6   /* Home, On/Off: save and quit */
};
uint32_t plat_keys(void);
uint32_t plat_millis(void);
void plat_sleep(uint32_t ms);
void plat_vblank(void);
void plat_push(int x, int y, int w, int h, const uint16_t *px);
bool plat_save(const char *name, const void *data, uint32_t len);
const uint8_t *plat_load(const char *name, uint32_t *len); /* NULL if absent */
uint32_t plat_random(void);
void plat_begin(void);
int plat_end(void);
#endif
