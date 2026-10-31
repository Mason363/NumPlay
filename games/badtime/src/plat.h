/* What the game needs from the calculator (plat_eadk.c), or from a computer for the tests. Keys are bt.h's BK_*. */
#ifndef BT_PLAT_H
#define BT_PLAT_H
#include <stdbool.h>
#include <stdint.h>

uint32_t plat_keys(void);
uint32_t plat_millis(void);
void plat_sleep(uint32_t ms);
void plat_push(int x, int y, int w, int h, const uint16_t *px);
bool plat_save(const char *name, const void *data, uint32_t len);
const uint8_t *plat_load(const char *name, uint32_t *len);
uint32_t plat_random(void);
void plat_begin(void);
int plat_end(void);
#endif
