/* Snow Rider 3D: the platform (the calculator, or a computer for tests).
 *
 * plat_eadk.c: the calculator's EADK display, keyboard, time and files.
 * plat_host.c: a frame buffer that tests save as images, scripted keys and simulated time (tests/play.c). */
#ifndef SR_PLAT_H
#define SR_PLAT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCREEN_W 320
#define SCREEN_H 240

/* keys held now, as bits (SK_: NumPlay's np.h has K_ keys of its own) */
enum {
  SK_LEFT = 1 << 0, SK_RIGHT = 1 << 1, SK_UP = 1 << 2, SK_DOWN = 1 << 3,
  SK_OK = 1 << 4,     /* OK, EXE, 5 */
  SK_BACK = 1 << 5,   /* Back, backspace: pause; back */
  SK_HOME = 1 << 6,   /* Home, On/Off: save and quit */
  SK_ANY = 1 << 7
};
uint32_t plat_keys(void);
uint32_t plat_millis(void);
void plat_sleep(uint32_t ms);
void plat_push(int x, int y, int w, int h, const uint16_t *px);
bool plat_save(const char *name, const void *data, uint32_t len);
const uint8_t *plat_load(const char *name, uint32_t *len);   /* NULL if absent */
uint32_t plat_random(void);
void plat_begin(void);
int plat_end(void);
bool plat_slow(void);   /* an N0110 or N0115 (216 MHz, the N0120 has 550) */
#endif
