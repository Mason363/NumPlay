/* What NumPico needs from the calculator (plat_eadk.c), or from a computer for the tests (tests/host.c). */
#ifndef NP8_PLAT_H
#define NP8_PLAT_H
#include <stdbool.h>
#include <stdint.h>

enum {
  PK_LEFT = 1 << 0, PK_RIGHT = 1 << 1, PK_UP = 1 << 2, PK_DOWN = 1 << 3,
  PK_O = 1 << 4,     /* OK, EXE */
  PK_X = 1 << 5,     /* Back, Ans */
  PK_MENU = 1 << 6,  /* Backspace */
  PK_HOME = 1 << 7,  /* Home, On/Off */
  PK_OK = 1 << 8,    /* OK or EXE alone, Back alone: for the menus */
  PK_BACK = 1 << 9,
};
uint32_t plat_keys(void);
uint32_t plat_millis(void);
void plat_sleep(uint32_t ms);
void plat_push(int x, int y, int w, int h, const uint16_t *px);
bool plat_save(const char *name, const void *data, uint32_t len);
const uint8_t *plat_load(const char *name, uint32_t *len);
uint32_t plat_random(void);
/* the RAM after the app's own, for Lua */
void plat_heap(uint8_t **start, uint8_t **end);
void plat_begin(void);
int plat_end(void);
#endif
