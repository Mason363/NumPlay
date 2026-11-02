/* Rider's physics: the game's own engine, LiquidFun 1.1.0 (Box2D 2.3.0) as Corona SDK built it (src/Box2D), seen from
 * C the way the game's Lua sees it through Corona: content pixels at 30 a metre, degrees turned clockwise, a step of
 * 1/60 s with 8 velocity and 3 position iterations, gravity 9.8 m/s^2 down, bodies allowed to sleep.
 *
 * All of Box2D's memory is one arena (ph_arena), so a whole world can be copied away and back (tests). */
#ifndef PHYS_H
#define PHYS_H
#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct ph_body ph_body;
typedef struct ph_fixture ph_fixture;
typedef struct ph_joint ph_joint;
enum { PH_STATIC, PH_KINEMATIC, PH_DYNAMIC };

typedef struct {
  float density, friction, bounce; /* as Corona's addBody: density <= 0 becomes 0.01 */
  uint16_t cat, mask;
  bool sensor;
  uint16_t tag; /* who it is, given back in the contact callbacks */
} ph_fixdef;

typedef struct {
  void (*begin)(uint16_t tag_a, uint16_t cat_a, uint16_t tag_b, uint16_t cat_b);
  void (*end)(uint16_t tag_a, uint16_t cat_a, uint16_t tag_b, uint16_t cat_b);
  void (*pre)(uint16_t tag_a, uint16_t tag_b);
  void (*post)(uint16_t tag_a, uint16_t cat_a, uint16_t tag_b, uint16_t cat_b, float force);
} ph_listener;

/* the arena: a fresh world, what's in use, and room for copies (tests) */
void ph_world_new(void);
size_t ph_used(void);
size_t ph_free(void);
size_t ph_tail(void); /* the end of the arena never used yet */
/* where to go if the arena runs out (NULL: nowhere); the world is then lost. (GCC's own setjmp on the calculator:
 * the C library's would bring in the C++ unwinder) */
#ifdef HOST
typedef jmp_buf ph_jmp;
#define PH_SETJMP(b) setjmp(b)
#define PH_LONGJMP(b) longjmp(b, 1)
#else
typedef void *ph_jmp[5];
#define PH_SETJMP(b) __builtin_setjmp(b)
#define PH_LONGJMP(b) __builtin_longjmp(b, 1)
#endif
void ph_on_full(ph_jmp *j);
size_t ph_peak(void);
size_t ph_arena_size(void);
size_t ph_top(void); /* the arena in use is [0, ph_top()) */
void *ph_arena(void);
void *ph_scratch(size_t *n); /* the arena's free end, for use till the next step (8-byte aligned) */
size_t ph_device_peak(void); /* (tests: the calculator's arena at its most, from the blocks counted) */
ph_body *ph_ground(void); /* Corona's ground body; the static track lives on it */
int ph_proxies(void);
int ph_contacts(void);
int ph_bodies(void);
void ph_listen(const ph_listener *l); /* NULL: no callbacks (while the world is built or taken apart) */

ph_body *ph_body_new(int type, float x, float y, float rot);
void ph_body_free(ph_body *b);
ph_fixture *ph_poly(ph_body *b, const float *xy, int n, const ph_fixdef *d); /* NULL if Box2D refuses the shape */
ph_fixture *ph_circle(ph_body *b, float x, float y, float r, const ph_fixdef *d);
void ph_fixture_free(ph_body *b, ph_fixture *f);
void ph_free_tag(ph_body *b, uint16_t tag); /* every fixture with that tag */

ph_joint *ph_weld(ph_body *a, ph_body *b, float x, float y);
ph_joint *ph_piston(ph_body *a, ph_body *b, float x, float y, float ax, float ay);
void ph_piston_limits(ph_joint *j, float lo, float hi); /* pixels, and turned on */
ph_joint *ph_pivot(ph_body *a, ph_body *b, float x, float y);
ph_joint *ph_touch(ph_body *b, float x, float y);   /* target, max force 1000 x the body's mass */
void ph_touch_tune(ph_joint *j, float freq, float damping);
void ph_touch_set(ph_joint *j, float x, float y);
void ph_touch_get(const ph_joint *j, float *x, float *y);
void ph_joint_free(ph_joint *j);

void ph_get(const ph_body *b, float *x, float *y, float *rot);
void ph_vel(const ph_body *b, float *vx, float *vy);
float ph_avel(const ph_body *b);
void ph_set_vel(ph_body *b, float vx, float vy);
void ph_set_avel(ph_body *b, float w);
void ph_set_pos(ph_body *b, float x, float y);
void ph_set_rot(ph_body *b, float rot);
void ph_set_fixed_rot(ph_body *b, bool on);
bool ph_fixed_rot(const ph_body *b);
void ph_set_damping(ph_body *b, float d);
void ph_set_gravity(ph_body *b, float scale);
void ph_set_bullet(ph_body *b, bool on);
void ph_set_sensor(ph_body *b, bool on);
void ph_torque(ph_body *b, float t);
float ph_mass(const ph_body *b);
bool ph_awake(const ph_body *b);
bool ph_bounds(const ph_body *b, float *x0, float *y0, float *x1, float *y1); /* its fixtures' boxes, false if none */

void ph_step(void);
bool ph_polys_touch(const float *a, int na, const float *b, int nb);

#ifdef __cplusplus
}
#endif
#endif
