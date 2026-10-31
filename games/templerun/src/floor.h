/* Temple Run: the path pieces' ground and obstacles (floor.c, made by tools/floor.py). */
#ifndef TR_FLOOR_H
#define TR_FLOOR_H
#include <stddef.h>
#include <stdint.h>

#define FLOOR_PIECES 88   /* (data.h's first meshes, in the same order) */
#define FLOOR_CELLS 30
enum { FO_JUMP = 1, FO_SLIDE = 2, FO_KILL = 4 };   /* jumped over, slid under; kills */
typedef struct {
  uint8_t piece, flags, n, pad;
  float height;
  int16_t p[8][2];
} FloorObstacle;
/* a piece's ground cells (NULL: none) */
extern const uint32_t *const floor_cells[FLOOR_PIECES];
extern const FloorObstacle floor_obstacles[];
extern const int floor_nobstacles;
#endif
