/* Made by tools/pack.py: the game's animations (AN_*), its own speeds and plays. Included once (night.c). */
#ifndef FNAF4_ANIMS_H
#define FNAF4_ANIMS_H
#include <stdint.h>

typedef struct {
  uint16_t first;   /* in ANIM_PICS */
  uint8_t n, speed, plays, wide;   /* a picture lasts 100 / speed frames; plays 0: for ever; wide: the bedroom's */
} AnimDef;

enum {
  AN_ROOM_0,
  AN_ROOM_12,
  AN_ROOM_13,
  AN_ROOM_14,
  AN_ROOM_15,
  AN_ROOM_16,
  AN_ROOM_17,
  AN_ROOM_19,
  AN_OFF4,
  AN_ON4,
  AN_CREAK,
  AN_DARKL,
  AN_DARKR,
  AN_DARKC,
  AN_CARPET,
  AN_LSTART,
  AN_RSTART,
  AN_BHIDE,
  AN_CHIDE,
  AN_BITE,
  AN_LEAVE3,
  AN_LEAVE2,
  AN_LEAVE1,
  AN_PEEKL_12,
  AN_PEEKR_12,
  AN_PEEKL_13,
  AN_PEEKR_13,
  AN_PEEKL_14,
  AN_PEEKR_14,
  AN_PEEKL_15,
  AN_PEEKR_15,
  AN_PEEKL_16,
  AN_PEEKR_16,
  AN_PEEKL_17,
  AN_PEEKR_17,
  AN_PEEKL_18,
  AN_PEEKR_18,
  AN_PEEKL_19,
  AN_PEEKR_19,
  AN_PEEKL_20,
  AN_PEEKR_20,
  AN_PEEKL_21,
  AN_PEEKR_21,
  AN_CLOSET_0,
  AN_CLOSET_12,
  AN_CLOSET_13,
  AN_CLOSET_14,
  AN_CLOSET_15,
  AN_CLOSET_16,
  AN_CLOSET_17,
  AN_CLOSET_18,
  AN_CLOSET_19,
  AN_CLOSET_20,
  AN_CLOSET_21,
  AN_CLOSET_22,
  AN_CLOSET_23,
  AN_BED_0,
  AN_BED_12,
  AN_BED_14,
  AN_BED_16,
  AN_BED_18,
  AN_BED_19,
  AN_BED_20,
  AN_BED_21,
  AN_BED_22,
  AN_BED_23,
  AN_BED_24,
  AN_BED_25,
  AN_JUMP_0,
  AN_JUMP_12,
  AN_JUMP_13,
  AN_JUMP_14,
  AN_FREDDYATK,
  AN_CUPCAKE,
  AN_BONNIEATK,
  AN_FREDBEAR,
  AN_PLUSH_0,
  AN_PLUSH_12,
  AN_PLUSH_13,
  AN_PLUSH_14,
  AN_PLUSH_15,
  AN_PLUSH_16,
  AN_PLUSH_17,
  AN_PLUSH_18,
  AN_PLUSH_19,
  AN_PLUSH_20,
  AN_PLUSH_21,
  AN_PLUSH_22,
  AN_PLUSH_23,
  AN_COUNT
};

static const uint16_t ANIM_PICS[] = {
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
  16, 17, 18, 19, 20, 21, 8, 22, 23, 24, 25, 26, 27, 28, 29, 30,
  31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46,
  47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62,
  63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78,
  79, 80, 81, 80, 82, 83, 79, 84, 77, 85, 86, 87, 88, 89, 90, 91,
  92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107,
  108, 107, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122,
  123, 124, 125, 126, 127, 118, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137,
  138, 139, 140, 141, 142, 141, 143, 144, 145, 144, 146, 147, 148, 149, 150, 151,
  148, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164, 165, 166,
  167, 168, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179, 175, 180, 181,
  182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194, 195, 196, 197,
  198, 199, 195, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209,
};

static const AnimDef ANIMS[AN_COUNT] = {
  {0, 1, 50, 0, 1},   /* ROOM_0 */
  {1, 1, 50, 1, 1},   /* ROOM_12 */
  {2, 1, 50, 0, 1},   /* ROOM_13 */
  {3, 1, 50, 1, 1},   /* ROOM_14 */
  {4, 1, 50, 1, 1},   /* ROOM_15 */
  {5, 1, 50, 0, 1},   /* ROOM_16 */
  {6, 1, 50, 1, 1},   /* ROOM_17 */
  {7, 2, 50, 1, 1},   /* ROOM_19 */
  {9, 2, 20, 1, 1},   /* OFF4 */
  {11, 2, 20, 1, 1},   /* ON4 */
  {13, 2, 20, 1, 1},   /* CREAK */
  {15, 3, 20, 1, 1},   /* DARKL */
  {18, 3, 20, 1, 1},   /* DARKR */
  {21, 2, 20, 1, 1},   /* DARKC */
  {23, 8, 20, 1, 0},   /* CARPET */
  {31, 7, 20, 1, 0},   /* LSTART */
  {38, 5, 20, 1, 0},   /* RSTART */
  {43, 4, 20, 1, 0},   /* BHIDE */
  {47, 4, 20, 1, 0},   /* CHIDE */
  {51, 6, 20, 1, 0},   /* BITE */
  {57, 2, 50, 1, 0},   /* LEAVE3 */
  {59, 2, 50, 1, 0},   /* LEAVE2 */
  {61, 2, 50, 1, 0},   /* LEAVE1 */
  {63, 1, 50, 0, 0},   /* PEEKL_12 */
  {64, 1, 20, 0, 0},   /* PEEKR_12 */
  {65, 1, 50, 0, 0},   /* PEEKL_13 */
  {66, 5, 20, 1, 0},   /* PEEKR_13 */
  {71, 3, 20, 1, 0},   /* PEEKL_14 */
  {74, 1, 20, 0, 0},   /* PEEKR_14 */
  {75, 3, 20, 1, 0},   /* PEEKL_15 */
  {78, 3, 20, 1, 0},   /* PEEKR_15 */
  {81, 1, 50, 0, 0},   /* PEEKL_16 */
  {82, 1, 50, 0, 0},   /* PEEKR_16 */
  {83, 3, 20, 1, 0},   /* PEEKL_17 */
  {86, 3, 20, 1, 0},   /* PEEKR_17 */
  {89, 3, 25, 1, 0},   /* PEEKL_18 */
  {92, 3, 25, 1, 0},   /* PEEKR_18 */
  {95, 1, 50, 0, 0},   /* PEEKL_19 */
  {96, 1, 50, 0, 0},   /* PEEKR_19 */
  {97, 1, 50, 0, 0},   /* PEEKL_20 */
  {98, 1, 50, 1, 0},   /* PEEKR_20 */
  {99, 1, 50, 0, 0},   /* PEEKL_21 */
  {100, 1, 50, 0, 0},   /* PEEKR_21 */
  {101, 5, 20, 1, 0},   /* CLOSET_0 */
  {106, 1, 50, 0, 0},   /* CLOSET_12 */
  {107, 1, 50, 0, 0},   /* CLOSET_13 */
  {108, 3, 20, 1, 0},   /* CLOSET_14 */
  {111, 2, 20, 1, 0},   /* CLOSET_15 */
  {113, 1, 20, 1, 0},   /* CLOSET_16 */
  {114, 1, 50, 0, 0},   /* CLOSET_17 */
  {115, 1, 50, 0, 0},   /* CLOSET_18 */
  {116, 1, 50, 0, 0},   /* CLOSET_19 */
  {117, 1, 50, 0, 0},   /* CLOSET_20 */
  {118, 1, 50, 1, 0},   /* CLOSET_21 */
  {119, 1, 50, 0, 0},   /* CLOSET_22 */
  {120, 1, 50, 0, 0},   /* CLOSET_23 */
  {121, 3, 20, 1, 0},   /* BED_0 */
  {124, 3, 90, 0, 0},   /* BED_12 */
  {127, 2, 90, 0, 0},   /* BED_14 */
  {129, 2, 90, 0, 0},   /* BED_16 */
  {131, 1, 50, 0, 0},   /* BED_18 */
  {132, 1, 50, 0, 0},   /* BED_19 */
  {133, 2, 20, 1, 0},   /* BED_20 */
  {135, 1, 50, 0, 0},   /* BED_21 */
  {136, 1, 50, 0, 0},   /* BED_22 */
  {137, 1, 50, 0, 0},   /* BED_23 */
  {138, 1, 50, 0, 0},   /* BED_24 */
  {139, 1, 50, 0, 0},   /* BED_25 */
  {140, 6, 25, 3, 0},   /* JUMP_0 */
  {146, 4, 25, 3, 0},   /* JUMP_12 */
  {150, 4, 25, 3, 0},   /* JUMP_13 */
  {154, 7, 25, 2, 0},   /* JUMP_14 */
  {161, 5, 25, 3, 0},   /* FREDDYATK */
  {166, 6, 25, 4, 0},   /* CUPCAKE */
  {172, 5, 25, 5, 0},   /* BONNIEATK */
  {177, 6, 25, 3, 0},   /* FREDBEAR */
  {183, 1, 50, 1, 0},   /* PLUSH_0 */
  {184, 1, 50, 0, 0},   /* PLUSH_12 */
  {185, 1, 50, 0, 0},   /* PLUSH_13 */
  {186, 4, 50, 1, 0},   /* PLUSH_14 */
  {190, 3, 50, 1, 0},   /* PLUSH_15 */
  {193, 3, 50, 1, 0},   /* PLUSH_16 */
  {196, 3, 50, 1, 0},   /* PLUSH_17 */
  {199, 3, 50, 1, 0},   /* PLUSH_18 */
  {202, 4, 50, 1, 0},   /* PLUSH_19 */
  {206, 4, 50, 1, 0},   /* PLUSH_20 */
  {210, 1, 50, 0, 0},   /* PLUSH_21 */
  {211, 1, 50, 0, 0},   /* PLUSH_22 */
  {212, 10, 25, 1, 0},   /* PLUSH_23 */
};

#endif
