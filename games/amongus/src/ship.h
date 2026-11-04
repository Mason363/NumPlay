/* Among Us: the Skeld. Its walls (moving), light walls (seeing), doors, rooms, consoles, vents and cameras come
 * from world.h; positions are in the game's units (y up), as in its ShipStatus. */
#ifndef AU_SHIP_H
#define AU_SHIP_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  float x, y;
} V2;
typedef struct {
  float x0, y0, x1, y1;
} Seg;
typedef struct {
  float x, y;
  uint8_t room, front;   /* front: a door across a hallway seen from the front (else from the side) */
  Seg seg[4];
} DoorRec;
typedef struct {
  uint8_t room, first, n;
} AreaRec;
typedef struct {
  uint8_t type, lo, hi;  /* the console serves this task type at steps lo..hi */
} ValidRec;
/* kind: 0 a task console, 1 the admin map, 2 the emergency button, 3 the cameras */
typedef struct {
  float x, y, dist, sx, sy;   /* sx, sy: where a bot stands to use it */
  uint32_t types;             /* the task types it serves at any step, as bits */
  uint8_t kind, id, room, below, imp;
  ValidRec valid[2];
} ConsoleRec;
typedef struct {
  float x, y, sx, sy;
  int8_t link[3];
} VentRec;
typedef struct {
  float x, y, size, aspect;
} CamRec;

/* the rooms (the game's SystemTypes) */
enum {
  R_HALLWAY, R_STORAGE, R_CAFETERIA, R_REACTOR, R_UPPER_ENGINE, R_NAV, R_ADMIN, R_ELECTRICAL, R_O2, R_SHIELDS,
  R_MEDBAY, R_SECURITY, R_WEAPONS, R_LOWER_ENGINE, R_COMMS, N_ROOMS
};
extern const char *const ROOM_NAMES[N_ROOMS];

#define PX_PER_UNIT 40
#define FEET_Y 0.2545f   /* the player's collider is this far below its picture's middle (0.3636 x 0.7) */
#define PLAYER_R 0.1564f /* the collider's radius (0.2234 x 0.7) */

void ship_init(void);
/* moves a circle of radius r from p by d, sliding along walls and closed doors */
V2 ship_move(V2 p, V2 d, float r);
/* is anything between a and b: walls (and closed doors) for moving, or light walls for seeing */
bool ship_wall_between(V2 a, V2 b);
bool ship_light_between(V2 a, V2 b);
int ship_room_at(V2 p);   /* the room (R_*) the point is in, R_HALLWAY if none */
bool door_closed(int d);
/* the vision polygon around p out to radius r: points into out (x, y pairs), returns how many */
int ship_vision(V2 p, float r, float *out, int max);
#endif
