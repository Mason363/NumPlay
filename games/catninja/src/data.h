/* Made by tools/data.py from the game's own files: do not edit. */
#ifndef CN_DATA_H
#define CN_DATA_H
#include <stdint.h>
#include "font.h"
#define CN_ALPHA 8
extern const uint32_t cn_pal[256]; /* 0xAARRGGBB, AA: 0 to CN_ALPHA */
#define CN_TILE_PX 100 /* 10 x 10 */
extern const uint8_t cn_tiles[7000];
extern const uint8_t cn_fg_tile[92]; /* the picture (cn_tiles, 100 each) of a tile type, or 255 */
extern const uint8_t cn_blood_tile[92]; /* the picture (cn_tiles, 100 each) of a tile type, or 255 */
extern const uint8_t cn_grid_tile[92]; /* the picture (cn_tiles, 100 each) of a tile type, or 255 */
typedef struct {
  uint8_t w, h;
  uint32_t at;
} cn_sprite_t;
enum {
  SPR_CAT_IDLE,
  SPR_CAT_IDLE2,
  SPR_CAT_WALK1,
  SPR_CAT_WALK2,
  SPR_CAT_JUMP,
  SPR_CAT_WALLSLIDE,
  SPR_CAT_DUCK,
  SPR_CAT_ROLL,
  SPR_JETPACKADDON,
  SPR_MINEBLINKOFF_0,
  SPR_MINEBLINKON_0,
  SPR_MINEOFF_0,
  SPR_MINEBLINKOFF_1,
  SPR_MINEBLINKON_1,
  SPR_MINEOFF_1,
  SPR_MINEBLINKOFF_2,
  SPR_MINEBLINKON_2,
  SPR_MINEOFF_2,
  SPR_MINEBLINKOFF_3,
  SPR_MINEBLINKON_3,
  SPR_MINEOFF_3,
  SPR_BUTTON_0,
  SPR_BUTTON_1,
  SPR_BUTTON_2,
  SPR_BUTTON_3,
  SPR_DIRT_0,
  SPR_DIRT_1,
  SPR_DIRT_2,
  SPR_CRATE,
  SPR_SHIELD_0_0,
  SPR_SHIELD_0_1,
  SPR_SHIELD_0_2,
  SPR_SHIELD_0_3,
  SPR_SHIELD_1_0,
  SPR_SHIELD_1_1,
  SPR_SHIELD_1_2,
  SPR_SHIELD_1_3,
  SPR_SHIELD_2_0,
  SPR_SHIELD_2_1,
  SPR_SHIELD_2_2,
  SPR_SHIELD_2_3,
  SPR_SHIELD_3_0,
  SPR_SHIELD_3_1,
  SPR_SHIELD_3_2,
  SPR_SHIELD_3_3,
  SPR_HOMING_0_0,
  SPR_HOMING_0_1,
  SPR_HOMING_0_2,
  SPR_HOMING_0_3,
  SPR_HOMING_1_0,
  SPR_HOMING_1_1,
  SPR_HOMING_1_2,
  SPR_HOMING_1_3,
  SPR_HOMING_2_0,
  SPR_HOMING_2_1,
  SPR_HOMING_2_2,
  SPR_HOMING_2_3,
  SPR_HOMING_3_0,
  SPR_HOMING_3_1,
  SPR_HOMING_3_2,
  SPR_HOMING_3_3,
  SPR_FALLSPIKE_0,
  SPR_FALLSPIKE_1,
  SPR_FALLSPIKE_2,
  SPR_FALLSPIKE_3,
  SPR_SUNSPIKE,
  SPR_BOMB,
  SPR_SPHERE_0,
  SPR_SPHERE_1,
  SPR_SPHERE_2,
  SPR_SPHERE_3,
  SPR_SPHERE_4,
  SPR_SPHERE_5,
  SPR_SPHERE_6,
  SPR_SPHERE_7,
  SPR_SPHERE_8,
  SPR_SPHERE_9,
  SPR_SPHERE_10,
  SPR_BIGSPHERE_0,
  SPR_BIGSPHERE_1,
  SPR_BIGSPHERE_2,
  SPR_BIGSPHERE_3,
  SPR_BIGSPHERE_4,
  SPR_BIGSPHERE_5,
  SPR_TV,
  SPR_TRACKMILL,
  SPR_COUNT
};
extern const cn_sprite_t cn_sprites[SPR_COUNT];
extern const uint8_t cn_sprite_px[32854];
#define CN_MAPS 31 /* the menu, then the 30 levels */
typedef struct {
  uint8_t w, h;
  int16_t marks[12]; /* [1] to [4]: x, y, the level that edge leads to */
  uint8_t has[4], sx, sy; /* which marks; the spawn (s) */
  uint32_t at, len; /* the tiles, DEFLATE */
  uint16_t script; /* into cn_scripts: pages, then each page's length and steps */
} cn_map_t;
extern const cn_map_t cn_maps[CN_MAPS];
#define CN_MAP_MAX 14850 /* the most tiles a map has */
extern const uint8_t cn_map_z[8619];
enum {
  OP_NOP,
  OP_PAUSECAM,
  OP_STOPMOVE,
  OP_PAUSE,
  OP_SETSPAWN,
  OP_DIE,
  OP_SETROOM,
  OP_FUNCTION,
  OP_GOTO,
  OP_SETTYPEPATH,
  OP_SETTYPE,
  OP_CREATELASER,
  OP_MOVECAM,
  OP_RESUMECAM,
  OP_RESUMEMOVE,
  OP_WALK,
  OP_SHAKE,
  OP_UPDATESCREEN,
  OP_RESETLEVEL,
  OP_TELEPORT,
  OP_CREATETEXT,
  OP_RUNSCRIPT,
  OP_IF,
  OP_ELSE,
  OP_ENDIF,
  OP_EXIT,
  OP_STARTBOSS,
  OP_REMOVEBOSS,
  OP_LOOP,
  OP_ENDLOOP,
  OP_JUMP,
  OP_SCALE,
};
enum { LB_ENDFUNCTION = 1, LB_ELSEIF = 2, LB_ENDIF = 4, LB_UNLOCKLASER = 8, LB_RESPAWN = 16 };
extern const int16_t cn_scripts[1125];
#define CN_CRYSTALS 49
extern const uint8_t cn_crystals[CN_CRYSTALS][3]; /* level, x, y */
#define CN_TEXTS 90
extern const uint16_t cn_text_at[CN_TEXTS]; /* runs: 1 font, 2 colour, \n, letters */
extern const char cn_text_data[3711];
#define TX_SPEC1 0
#define TX_SPEC2 1
#define TX_SPEC3 2
#define TX_SPEC4 3
#define TX_DIE0 4
#define TX_IDLE0 13
#define TX_BOSS 17
#define TX_ENDGOOD 18
#define TX_ENDBAD 19
#define TX_DIES 9
#define TX_IDLES 4
#define CN_COLOURS 33
extern const uint32_t cn_colours[CN_COLOURS];
#define CN_FONTS 14
#define FONT_HUD 9
#define COLOUR_WHITE 1
extern const np_font_t cn_fonts[CN_FONTS];
#endif
