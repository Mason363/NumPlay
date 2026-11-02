/* Mr Bullet: the screens (ui.c). */
#ifndef MB_UI_H
#define MB_UI_H
#include <stdbool.h>
#include <stdint.h>

/* mem: the level's bytes are kept there (LEVEL_MAX_BYTES); strip: the screen's strip */
void ui_init(uint8_t *level_mem, uint16_t *strip, void *scratch);
bool ui_frame(uint32_t keys, uint32_t now);   /* false: quit */
bool ui_dirty(void);
void ui_draw(void);
void ui_quit(void);                           /* saves */
#endif
