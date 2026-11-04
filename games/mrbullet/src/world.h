/* Mr Bullet: the physics, Box2D 2.3.1 set up as the game sets it up (world.cpp).
 *
 * Everything here is in the game's units (60 a metre) with y pointing down, as its levels are; world.cpp turns
 * them into Box2D's metres with y up. Bodies and joints are numbered; a body carries the game's object number. */
#ifndef MB_WORLD_H
#define MB_WORLD_H
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define W_BODIES 200
#define W_JOINTS 160
#define W_STEP (1.0f / 60.0f)

enum { WB_STATIC, WB_DYNAMIC, WB_KINEMATIC };
/* collision categories, as the game's collision matrix: 3 with 3 and 4 with 4 never touch */
enum { WC_NONE, WC_STATIC, WC_LEVEL, WC_CHARACTER, WC_BULLET, WC_GRENADE, WC_SENSOR = 7 };

typedef struct {
  float density, friction, restitution;
  uint8_t category, sensor;
} WFix;

bool world_begin(void);                       /* a fresh world: gravity 9.81 m/s2 down */
void world_end(void);
/* a body (-1 when the world is full): its object number `obj` comes back in the contacts */
int world_body(int type, float x, float y, float angle, int obj, bool bullet, bool fixed_rotation, float gravity);
bool world_poly(int b, const float *xy, int n, const WFix *f);   /* xy: local, n points */
/* a static outline: a loop of edges (its points: local, n of them, used up) */
bool world_chain(int b, float *xy, int n, const WFix *f);
bool world_box(int b, float cx, float cy, float hw, float hh, const WFix *f);
bool world_circle(int b, float cx, float cy, float r, const WFix *f);
void world_destroy(int b);
bool world_alive(int b);
int world_obj(int b);
/* joints (-1 when full): anchors in world units */
int world_revolute(int a, int b, float x, float y, bool limit, float lo, float hi, bool motor, float speed,
                   float torque);
int world_rope(int a, int b, float ax, float ay, float bx, float by, float length);
void world_destroy_joint(int j);
bool world_joint_alive(int j);
void world_joint_anchors(int j, float *ax, float *ay, float *bx, float *by);
void world_set_limits(int j, bool on, float lo, float hi);

void world_step(void);
/* where it is: position and the sine and cosine of its turn (screen sense) */
void world_xf(int b, float *x, float *y, float *s, float *c);
float world_angle(int b);
void world_velocity(int b, float *vx, float *vy);
void world_set_velocity(int b, float vx, float vy);
void world_set_angular(int b, float w);
float world_angular(int b);
void world_set_transform(int b, float x, float y, float angle);
void world_impulse(int b, float ix, float iy, float px, float py);   /* units: kg m/s, at a world point */
void world_angular_impulse(int b, float i);
float world_mass(int b);
bool world_awake(int b);
bool world_dynamic(int b);
void world_set_type(int b, int type);
void world_wake(int b);
void world_set_fixed_rotation(int b, bool on);
/* Box2D's memory: in use, and the limit past which it makes no new contact (0: none) */
extern long b2_heap_now, b2_heap_peak, b2_heap_limit;
/* the first fixture a ray from (x0, y0) to (x1, y1) meets, of the categories in `mask` (1 << category):
   its body, the point and the normal; -1 for none */
int world_ray(float x0, float y0, float x1, float y1, unsigned mask, float *hx, float *hy, float *nx, float *ny);
/* every body the ray meets (once each), of the categories in `mask`, with where */
int world_ray_all(float x0, float y0, float x1, float y1, unsigned mask, int *out, float *xs, float *ys, int max);
/* the bodies whose boxes meet a box */
int world_query(float x0, float y0, float x1, float y1, int *out, int max);

/* the game's side (game.c): a contact begins (with how fast they meet, units/s), whether one holds, and the
   impulse it took */
void game_contact(int obj_a, int obj_b, int body_a, int body_b, float speed, float px, float py);
bool game_contact_enabled(int obj_a, int obj_b);

/* the nearest point of a body's shapes' boxes to (x, y): how far, and where */
float world_nearest(int b, float x, float y, float *nx, float *ny);
int world_bodies(void);
int world_contacts(void);
#ifdef __cplusplus
}
#endif
#endif
