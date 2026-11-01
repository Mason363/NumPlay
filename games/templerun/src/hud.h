/* Temple Run: the game's interface pictures and its golden lettering (hud.c, made by tools/hud.py). */
#ifndef TR_HUD_H
#define TR_HUD_H
#include <stdint.h>

/* a picture: its pixels hud_px[off ..], w a row, colours hud_pal (see-through amounts hud_alpha, index 0 none);
 * a letter also has where it sits from the pen (ox, oy below the line's top) and how far the pen goes */
typedef struct {
  uint8_t w, h;
  int8_t ox, oy;
  uint8_t advance;
  uint32_t off : 24;
} HudPic;

typedef struct {
  uint8_t first, count, line;   /* its letters, and the height of a line */
  uint16_t at;   /* hud_pics[at ..] */
} HudFont;

enum {
  HP_TOTEM,
  HP_TOTEM_BAR,
  HP_TOTEM_CAP,
  HP_EMBLEM1,
  HP_EMBLEM2,
  HP_EMBLEM3,
  HP_EMBLEM4,
  HP_JUNGLE_A,
  HP_JUNGLE_B,
  HP_JUNGLE_C,
  HP_SCOREBOARD,
  HP_SCORE_PLATE,
  HP_COINS_PLATE,
  HP_WINGS_PLATE,
  HP_COIN,
  HP_COIN_RED,
  HP_COIN_BLUE,
  HP_IDOL,
  HP_PAUSE,
  HP_BONUS100,
  HP_BONUS250,
  HP_BONUS500,
  HP_BONUS1000,
  HP_BOARD,
  HP_HIGH_SCORE,
  HP_NICE_JOB,
  HP_TRY_AGAIN,
  HP_ARROW_LEFT,
  HP_ARROW_RIGHT,
  HP_ARROW_UP,
  HP_ARROW_DOWN,
  HP_HEADSTART,
  HP_HEADSTART_MEGA,
  HP_LOGO,
  HP_PLAY_IDOL,
  HP_M_STORE,
  HP_M_OBJECTIVES,
  HP_PARCHMENT,
  HP_B_RUN_AGAIN,
  HP_B_MAIN_MENU,
  HP_B_RESUME,
  HP_B_NEW_GAME,
  HP_B_BACK,
  HP_H_SCORE,
  HP_H_PAUSED,
  HP_H_STORE,
  HP_H_OBJECTIVES,
  HP_B_STORE,
  HP_I_MAGNET,
  HP_I_INVIS,
  HP_I_COIN_BONUS,
  HP_I_COIN_VALUE,
  HP_I_BOOST,
  HP_I_WINGS,
  HP_I_HEADSTART,
  HP_I_HEADSTART_MEGA,
  HP_BAR_EMPTY,
  HP_BAR_FULL1,
  HP_BAR_FULL2,
  HP_BAR_FULL3,
  HP_BAR_FULL4,
  HP_BAR_FULL5,
  HP_SEPARATOR,
  HP_CHECK_EMPTY,
  HP_CHECK_FULL,
};

extern const HudPic hud_pics[];
extern const uint16_t hud_pal[256];
extern const uint8_t hud_alpha[256], hud_px[];
extern const HudFont hud_gold, hud_gold_small;
#endif
