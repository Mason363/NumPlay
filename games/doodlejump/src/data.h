/* Doodle Jump's pictures and font, made by tools/pack.py from the game's own files. */
#ifndef DJ_DATA_H
#define DJ_DATA_H
#include <stdint.h>

typedef struct {
  uint8_t w, h;
  int8_t x, y;     /* its top left, from its anchor */
  uint16_t pal;    /* its first colour in dj_pal */
  uint32_t pix;    /* its rows in dj_pix */
} dj_sprite_t;

enum {
  S_P_GREEN,
  S_P_BLUE,
  S_P_WHITE,
  S_P_BROWN0,
  S_P_BROWN1,
  S_P_BROWN2,
  S_P_BROWN3,
  S_P_BOMB0,
  S_P_BOMB1,
  S_P_BOMB2,
  S_P_BOMB3,
  S_P_BOMB4,
  S_P_BOOM0,
  S_P_BOOM1,
  S_P_BOOM2,
  S_SPRING0,
  S_SPRING1,
  S_TRAMP0,
  S_TRAMP1,
  S_TRAMP2,
  S_PROP_ITEM,
  S_JET_ITEM,
  S_M_FLY0,
  S_M_FLY1,
  S_M_FLY2,
  S_M_FLY3,
  S_M_FLY4,
  S_M_BAT0,
  S_M_BAT1,
  S_M_BAT2,
  S_M_BAT3,
  S_M_BAT4,
  S_M_EYE0,
  S_M_EYE1,
  S_M_BLOB0,
  S_M_BLOB1,
  S_M_CROC0,
  S_M_CROC1,
  S_M_HAIRY,
  S_M_BUG0,
  S_M_BUG1,
  S_M_RED,
  S_M_PURPLE,
  S_M_HOLE,
  S_M_UFO0,
  S_M_UFO1,
  S_M_BEAM,
  S_D_LEFT,
  S_D_LEFTODSKOK,
  S_D_RIGHT,
  S_D_RIGHTODSKOK,
  S_D_PUCA,
  S_D_PUCAODSKOK,
  S_D_NOSE,
  S_D_TURNL1,
  S_D_TURNL2,
  S_D_TURNL3,
  S_D_TURNL4,
  S_D_TURNL5,
  S_D_TURNL6,
  S_D_TURNL7,
  S_D_TURNR1,
  S_D_TURNR2,
  S_D_TURNR3,
  S_D_TURNR4,
  S_D_TURNR5,
  S_D_TURNR6,
  S_D_TURNR7,
  S_HAT0,
  S_HAT1,
  S_HAT2,
  S_HAT3,
  S_JET0,
  S_JET1,
  S_JET2,
  S_JET3,
  S_JET4,
  S_JET5,
  S_JET6,
  S_JET7,
  S_JET8,
  S_JET9,
  S_STARS0,
  S_STARS1,
  S_STARS2,
  S_BULLET,
  S_TOPBAR,
  S_DIGIT1,
  S_DIGIT2,
  S_DIGIT3,
  S_DIGIT4,
  S_DIGIT5,
  S_DIGIT6,
  S_DIGIT7,
  S_DIGIT8,
  S_DIGIT9,
  S_DIGIT0,
  S_PAUSE,
  S_LOGO,
  S_T_UFO,
  S_T_HOLE,
  S_T_GREEN,
  S_T_DOOD,
  S_T_DOODJ,
  S_T_EYE,
  S_BUTTON,
  S_BUTTON_ON,
  S_GAMEOVER,
  S_COUNT
};

typedef struct {
  uint16_t sprite;  /* its picture */
  uint8_t advance;
} dj_glyph_t;

extern const dj_sprite_t dj_spr[];
extern const uint16_t dj_pal[];
extern const uint8_t dj_alpha[];
extern const uint8_t dj_pix[];
extern const dj_glyph_t dj_font_big[95]; /* ASCII 32 to 126, 28 pixels a line */
extern const dj_glyph_t dj_font_small[95]; /* ASCII 32 to 126, 21 pixels a line */
#endif
