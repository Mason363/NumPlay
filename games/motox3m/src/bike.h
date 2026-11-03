/* Moto X3M: the bike and its rider (motox3m4.js: CarBuilder, CarObject, BikeObject, FrameObject, WheelObject,
 * DriverObject, DriverPartObject, BoostEffector, BodyEffector). */
#ifndef MX_BIKE_H
#define MX_BIKE_H
#include <stdbool.h>
#include "nape.h"

/* the rider's parts: head, corpse, hip, shin, arm, forearm, then the other hip, shin, arm and forearm */
enum { P_HEAD, P_CORPSE, P_HIP, P_SHIN, P_ARM, P_FOREARM, P_HIP2, P_SHIN2, P_ARM2, P_FOREARM2, NPARTS };

typedef struct {
  Con pivot, soft, stiff;   /* to its parent part (or the frame) */
  float base, delta;
  bool has;
} PartJoint;

typedef struct {
  Body frame, wheel[2], driver, part[NPARTS];
  Shape frame_shapes[2], wheel_shape[2], driver_shape, part_shape[NPARTS];
  float frame_loc[2][8], driver_loc[8], part_loc[NPARTS][8];
  float frame_glob[2][20], driver_glob[20], part_glob[NPARTS][20];
  Con line[2], dist[2], brake_joint, weld;
  PartJoint pj[NPARTS];     /* each part to its parent */
  PartJoint arm, leg;       /* forearm and shin to the frame */
  uint8_t mat_wheel[2], mat_brake;   /* material slots */
  int skin;
  /* state */
  bool dead, finished, drive, accell, brake, exploded, braked, spiked, boosting;
  int tilt;
  float speed, x, y, rotation;
  float fly_time, fall_time, start_fly, min_fly, max_fly;
  float accel_brake_timer, max_vel_timer;
  bool frame_ground, wheel_ground[2];
  float frame_end_delay, wheel_end_delay[2], wheel_fly[2], spin_delay[2];
  float review_x, review_y;
  int safe_id;
  float smoke_timer;
  float boost_left, boost_force;
  float eff_vx[NPARTS + 4], eff_vy[NPARTS + 4];   /* geysers: frame, wheels, driver body, parts */
  V2 wheel_contact[2];
  float wheel_normal[2];
  bool wheel_contacted[2];
} Bike;

extern Bike bike;

void bike_build(float x, float y, int skin);
void bike_review(void);       /* to the last safe point (or the start) */
void bike_restart(void);      /* to the start */
/* a game tick: keys K_UP gas, K_DOWN brake, K_LEFT / K_RIGHT lean */
void bike_update(float dt, uint32_t keys);
void bike_driver_ground(void);   /* processDriverGround */
void bike_explode(void);
void bike_spikes(Body *part);
void bike_finish(void);
void bike_add_boost(float time, float force);
bool bike_owns(const Body *b);
int bike_body_slot(const Body *b);   /* geyser effect slot, or -1 */
/* flips landed: the game takes 0.5 s off the time for each */
extern void game_flip(int n);
extern void game_died(void);
extern void game_finished(void);
#endif
