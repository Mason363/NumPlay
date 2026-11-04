/* Made by tools/pack.py: the blocks, items, textures and pictures of NumBlocks on the CE. */
#ifndef NBCE_DATA_H
#define NBCE_DATA_H
#include <stdint.h>

#define NTEX 94
#define N_SHADE 32
#define FIRST_STATIC 16
#define N_TINTS 4
#define B_N 56
#define IT_N 33
#define IT0 128
enum {
  B_AIR,
  B_STONE,
  B_GRASS,
  B_DIRT,
  B_COBBLESTONE,
  B_PLANKS_OAK,
  B_PLANKS_SPRUCE,
  B_PLANKS_BIRCH,
  B_SAPLING_OAK,
  B_SAPLING_SPRUCE,
  B_SAPLING_BIRCH,
  B_BEDROCK,
  B_WATER,
  B_LAVA,
  B_SAND,
  B_GRAVEL,
  B_GOLD_ORE,
  B_IRON_ORE,
  B_COAL_ORE,
  B_LOG_OAK,
  B_LOG_SPRUCE,
  B_LOG_BIRCH,
  B_LEAVES_OAK,
  B_LEAVES_SPRUCE,
  B_LEAVES_BIRCH,
  B_GLASS,
  B_LAPIS_ORE,
  B_LAPIS_BLOCK,
  B_SANDSTONE,
  B_TALL_GRASS,
  B_DEAD_BUSH,
  B_WOOL,
  B_DANDELION,
  B_POPPY,
  B_GOLD_BLOCK,
  B_IRON_BLOCK,
  B_BRICKS,
  B_BOOKSHELF,
  B_MOSSY_COBBLESTONE,
  B_OBSIDIAN,
  B_TORCH,
  B_CHEST,
  B_DIAMOND_ORE,
  B_DIAMOND_BLOCK,
  B_CRAFTING_TABLE,
  B_FURNACE,
  B_FURNACE_LIT,
  B_REDSTONE_ORE,
  B_CACTUS,
  B_SUGAR_CANE,
  B_STONE_BRICKS,
  B_COAL_BLOCK,
  B_CLAY,
  B_SNOW,
  B_ICE,
  B_GRASS_SNOWED,
};
enum {
  I_STICK = IT0 + 0,
  I_COAL = IT0 + 1,
  I_CHARCOAL = IT0 + 2,
  I_IRON_INGOT = IT0 + 3,
  I_GOLD_INGOT = IT0 + 4,
  I_DIAMOND = IT0 + 5,
  I_REDSTONE = IT0 + 6,
  I_LAPIS = IT0 + 7,
  I_FLINT = IT0 + 8,
  I_CLAY_BALL = IT0 + 9,
  I_BRICK = IT0 + 10,
  I_REEDS = IT0 + 11,
  I_APPLE = IT0 + 12,
  I_WOODEN_SWORD = IT0 + 13,
  I_WOODEN_SHOVEL = IT0 + 14,
  I_WOODEN_PICKAXE = IT0 + 15,
  I_WOODEN_AXE = IT0 + 16,
  I_STONE_SWORD = IT0 + 17,
  I_STONE_SHOVEL = IT0 + 18,
  I_STONE_PICKAXE = IT0 + 19,
  I_STONE_AXE = IT0 + 20,
  I_IRON_SWORD = IT0 + 21,
  I_IRON_SHOVEL = IT0 + 22,
  I_IRON_PICKAXE = IT0 + 23,
  I_IRON_AXE = IT0 + 24,
  I_GOLDEN_SWORD = IT0 + 25,
  I_GOLDEN_SHOVEL = IT0 + 26,
  I_GOLDEN_PICKAXE = IT0 + 27,
  I_GOLDEN_AXE = IT0 + 28,
  I_DIAMOND_SWORD = IT0 + 29,
  I_DIAMOND_SHOVEL = IT0 + 30,
  I_DIAMOND_PICKAXE = IT0 + 31,
  I_DIAMOND_AXE = IT0 + 32,
};
enum { M_NONE, M_CUBE, M_CROSS, M_LIQUID, M_LEAVES, M_GLASS, M_TORCH };
enum { BF_OPAQUE = 1, BF_SOLID = 2, BF_ALPHA = 4, BF_LIQUID = 8 };
enum { T_NONE, T_PICKAXE, T_AXE, T_SHOVEL, T_SWORD };
enum { BIOME_OCEAN, BIOME_PLAINS, BIOME_DESERT, BIOME_EXTREME_HILLS, BIOME_FOREST, BIOME_TAIGA, BIOME_BEACH, BIOME_BIRCH_FOREST, BIOME_COLD_TAIGA, N_BIOMES };
extern const uint8_t tint_of_biome[N_BIOMES];
extern const uint8_t pal_rgb[256 * 3];   /* 1-15: the sky's (set at run time) */
/* the appvar NBCETEX: the textures (v * 16 + u; 0 see-through), each colour at each level of light
 * (31 full), the nearest colour of a point [r][g][b] of 16 levels each (cube_levels) */
extern const uint8_t *texdat;
#define TEXDAT_RECIP (NTEX * 256 + N_SHADE * 256 + 4096)
#define TEXDAT_RATIO (TEXDAT_RECIP + 2545 * 3)
#define TEXDAT_SIZE (TEXDAT_RATIO + 256)
#define tex_px ((const uint8_t (*)[256])texdat)
#define shade_cm ((const uint8_t (*)[256])(texdat + NTEX * 256))
#define pal_cube (texdat + NTEX * 256 + N_SHADE * 256)
/* faces -Y +Y -Z +Z -X +X; blk_tint: the faces whose texture has N_TINTS copies, one per biome tint */
extern const uint8_t blk_tex[B_N][6], blk_tint[B_N], blk_model[B_N], blk_flags[B_N], blk_light[B_N];
extern const uint8_t blk_opacity[B_N], blk_hard[B_N], blk_tool[B_N], blk_drop[B_N];
/* blk_hard: hardness x 4 (255: unbreakable); blk_tool: T_* | the level needed << 4; blk_drop: 255 none */
extern const uint16_t it_fuel[B_N + IT_N];
extern const uint8_t it_kind[IT_N], it_speed[IT_N], it_dmg[IT_N];   /* it_kind: T_* | tier << 4 */
extern const uint16_t it_dur[IT_N];
extern const char *const it_label[IT_N], *const blk_label[B_N];
extern const uint8_t recipes[], smelt[];
extern const uint8_t cube_levels[16];
#include "gfx.h"
/* the pictures, in the appvar NBCEDAT */
#define font_bits (gfx + GFX_FONT)
#define font_w (gfx + GFX_FONT_W)
#define icon_of (gfx + GFX_ICON_OF)
#define TX_STONE 0
#define TX_DIRT 1
#define TX_GRASS_TOP_GRASS 2
#define TX_GRASS_SIDE 6
#define TX_COBBLESTONE 10
#define TX_PLANKS_OAK 11
#define TX_PLANKS_SPRUCE 12
#define TX_PLANKS_BIRCH 13
#define TX_SAPLING_OAK 14
#define TX_SAPLING_SPRUCE 15
#define TX_SAPLING_BIRCH 16
#define TX_BEDROCK 17
#define TX_WATER_STILL 18
#define TX_LAVA_STILL 19
#define TX_SAND 20
#define TX_GRAVEL 21
#define TX_GOLD_ORE 22
#define TX_IRON_ORE 23
#define TX_COAL_ORE 24
#define TX_LOG_OAK_TOP 25
#define TX_LOG_OAK 26
#define TX_LOG_SPRUCE_TOP 27
#define TX_LOG_SPRUCE 28
#define TX_LOG_BIRCH_TOP 29
#define TX_LOG_BIRCH 30
#define TX_LEAVES_OAK_FOLIAGE 31
#define TX_LEAVES_SPRUCE_SPRUCE 35
#define TX_LEAVES_BIRCH_BIRCH 36
#define TX_GLASS 37
#define TX_LAPIS_ORE 38
#define TX_LAPIS_BLOCK 39
#define TX_SANDSTONE_BOTTOM 40
#define TX_SANDSTONE_TOP 41
#define TX_SANDSTONE_NORMAL 42
#define TX_TALLGRASS_GRASS 43
#define TX_DEADBUSH 47
#define TX_WOOL_COLORED_WHITE 48
#define TX_FLOWER_DANDELION 49
#define TX_FLOWER_ROSE 50
#define TX_GOLD_BLOCK 51
#define TX_IRON_BLOCK 52
#define TX_BRICK 53
#define TX_BOOKSHELF 54
#define TX_COBBLESTONE_MOSSY 55
#define TX_OBSIDIAN 56
#define TX_TORCH_ON 57
#define TX_CHEST_TOP 58
#define TX_CHEST_SIDE 59
#define TX_CHEST_FRONT 60
#define TX_DIAMOND_ORE 61
#define TX_DIAMOND_BLOCK 62
#define TX_CRAFTING_TABLE_TOP 63
#define TX_CRAFTING_TABLE_SIDE 64
#define TX_CRAFTING_TABLE_FRONT 65
#define TX_FURNACE_TOP 66
#define TX_FURNACE_SIDE 67
#define TX_FURNACE_FRONT_OFF 68
#define TX_FURNACE_FRONT_ON 69
#define TX_REDSTONE_ORE 70
#define TX_CACTUS_BOTTOM 71
#define TX_CACTUS_TOP 72
#define TX_CACTUS_SIDE 73
#define TX_REEDS_GRASS 74
#define TX_STONEBRICK 78
#define TX_COAL_BLOCK 79
#define TX_CLAY 80
#define TX_SNOW 81
#define TX_ICE 82
#define TX_GRASS_SIDE_SNOWED 83
#define TX_DESTROY_STAGE_0 84
#define TX_DESTROY_STAGE_1 85
#define TX_DESTROY_STAGE_2 86
#define TX_DESTROY_STAGE_3 87
#define TX_DESTROY_STAGE_4 88
#define TX_DESTROY_STAGE_5 89
#define TX_DESTROY_STAGE_6 90
#define TX_DESTROY_STAGE_7 91
#define TX_DESTROY_STAGE_8 92
#define TX_DESTROY_STAGE_9 93
#define SP_HOTBAR 0
#define SP_HOTBAR_SEL 1
#define SP_BUTTON 2
#define SP_BUTTON_HOVER 3
#define SP_BUTTON_OFF 4
#define SP_HEART_BG 5
#define SP_HEART 6
#define SP_HEART_HALF 7
#define SP_FOOD_BG 8
#define SP_FOOD 9
#define SP_FOOD_HALF 10
#define SP_BUBBLE 11
#define SP_BUBBLE_POP 12
#define SP_XP_BG 13
#define SP_XP 14
#define SP_CROSSHAIR 15
#define SP_INVENTORY 16
#define SP_CRAFTING_TABLE 17
#define SP_FURNACE 18
#define SP_INV_BOTTOM 19
#define SP_FLAME 20
#define SP_ARROW 21
#define SP_CREATIVE 22
#define SP_SCROLLER 23
#define SP_DIRT 24
#define SP_SUN 25
#define SP_MOON_0 26
#define SP_MOON_1 27
#define SP_MOON_2 28
#define SP_MOON_3 29
#define SP_MOON_4 30
#define SP_MOON_5 31
#define SP_MOON_6 32
#define SP_MOON_7 33
#define SP_CHEST 34
#define SP_STEVE 35

#endif
