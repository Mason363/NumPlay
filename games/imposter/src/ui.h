/* Imposter: the screens and their keys (ui.c). */
#ifndef IM_UI_H
#define IM_UI_H
#include <stdbool.h>
#include <stdint.h>

void app_init(uint32_t now);
/* the keys held now and the letters on them (plat_keys); false: the game ends (saved) */
bool app_step(uint32_t keys, uint32_t letters, uint32_t now);
bool app_dirty(void);
void app_draw(void);
#endif
