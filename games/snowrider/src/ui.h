/* Snow Rider 3D's own lettering and interface pictures (ui.c, made by tools/ui.py). */
#ifndef SR_UI_H
#define SR_UI_H
#include <stdint.h>
#include "../../../launcher/src/font.h"

extern const np_font_t sr_font_small, sr_font_mid, sr_font_big;

/* a picture: w x h pixels from ui_px[off], colours ui_pal and their see-through amounts ui_alpha (0: none) */
typedef struct {
  uint8_t w, h;
  uint16_t off;
} UiPic;
enum {
  UI_GIFT,
  UI_SIGN,
  UI_SLED,
  UI_ARROW_BUTTON,
  UI_KEYS,
  UI_COUNT
};
extern const UiPic ui_pics[UI_COUNT];
extern const uint16_t ui_pal[256];
extern const uint8_t ui_alpha[256], ui_px[];
#endif
