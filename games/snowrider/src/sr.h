/* Snow Rider 3D for the NumWorks calculator: what the parts share.
 *
 * The world is Unity's: metres, x right, y up, z ahead, rotations in degrees, a turn written as a 3x4 matrix of
 * rows (m[0..3] the first row, its last the x of the place). */
#ifndef SR_H
#define SR_H
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include "data.h"
#include "plat.h"

#define DT_REF (1.0f / 60)   /* FrameRateControl.ReferenceFrameDuration: the game steps 60 times a second */
#define DEG 0.017453293f

/* ---------------------------------------------------------------- maths (sr.c) */
typedef struct { float x, y, z, w; } Quat;
Quat q_euler(float x, float y, float z);   /* Unity's Quaternion.Euler: z, then x, then y */
Quat q_mul(Quat a, Quat b);
Quat q_lerp(Quat a, Quat b, float t);      /* Unity's Quaternion.Lerp (normalised, the short way) */
void q_rot(Quat q, const float v[3], float out[3]);
void m_trs(float m[12], const float t[3], Quat q, const float s[3]);
void m_mul(float out[12], const float a[12], const float b[12]);
void m_point(const float m[12], const float p[3], float out[3]);
void m_dir(const float m[12], const float d[3], float out[3]);
bool m_inv(float out[12], const float m[12]);
float m_det(const float m[12]);
float blend(float amount);                 /* FrameRateControl.Blend at the game's 60 steps */
uint32_t rnd(void);
float rnd_range(float a, float b);         /* Random.Range(float, float): a to b */
int rnd_int(int a, int b);                 /* Random.Range(int, int): a to b - 1 (a if b <= a) */
void rnd_seed(uint32_t s);

/* ---------------------------------------------------------------- the world (world.c) */
typedef struct {
  float m[12];
  float lo[3], hi[3];   /* its box in the world (colliders and meshes) */
  uint16_t prefab;
  uint8_t alive, slot;
} Inst;
#define MAXI 400
extern Inst insts[MAXI];
extern int ninsts;
#define MAXC 32
extern Inst camp[MAXC];
extern int ncamp;
extern bool camp_on;

typedef struct {
  float t, p[3];
  const Inst *inst;
  const Coll *coll;
} Hit;
/* Physics.Raycast: the nearest collider on the layers hit by the ray (d a unit vector) within len; rays starting
 * inside a box, sphere or capsule do not see it, and collision meshes are seen from their front only */
bool ray_cast(const float o[3], const float d[3], float len, int layers, Hit *hit);
/* the colliders of the layers that the box (unit cube taken by m) touches: f called for each */
void box_touch(const float m[12], int layers, void (*f)(Inst *inst, const Coll *c));

extern int score_struct, struct_index;
extern float generate_distance;
void world_clear(void);
void world_start(void);                 /* GGen.GenerateStartLevel */
bool world_update(float target_z);      /* GGen.Update: true when a structure was made (OnGenChunk) */
void world_camp(bool on);               /* CampControl */
void world_balls(float dt);             /* the snowballs rolling */
/* snowballs (x, y their middle, r their collider's radius, k the ground's slope under them): drawn like the rest */
typedef struct {
  float x, y, z, r, vx, vy, k, spin;
  int8_t dir;
  uint8_t slot;
  bool alive, ground;
} Ball;
#define MAXB 24
extern Ball balls[MAXB];
extern uint16_t ball_prefab;
extern Inst ball_inst[MAXB];

/* ---------------------------------------------------------------- pieces flying apart (game.c) */
typedef struct {
  float p[3], s[3], v[3], w[3];
  Quat q;
  uint16_t prefab;
  bool alive;
} Piece;
#define MAXP_PIECES 24
extern Piece pieces[MAXP_PIECES];
void pieces_spawn(int chunk, const float at[3], Quat rot, const float push[3]);

/* ---------------------------------------------------------------- the game (game.c) */
enum { M_INTRO, M_MAIN, M_PLAY, M_END };
extern int mode;
extern int score, best, gifts, gifts_game, plays, skin, sel, screen, shop_skin;
extern uint16_t owned;
extern bool best_now;
/* what is over the game: nothing, the pause menu, asking before quitting, the sleds, the end */
enum { S_NONE, S_PAUSE, S_ASK, S_SLEDS, S_END, S_GUIDE };
extern bool guide_read;
extern float score_pop, end_wait;
extern float time_scale;
extern float fade;
typedef struct {
  float x, y, z;              /* the Sledge's place (y stays) */
  float hrot, vrot;           /* where it is steered, and the slope's angle */
  Quat yaw, pitch;            /* its turn (smoothed towards hrot) and its model's (towards vrot) */
  float model_y;              /* the model's height over the Sledge */
  float move_speed, speed;
  float py, vspeed, last_vt;  /* the collision point: its height, speed up, last step */
  bool pgrounded, grounded, already_jumped, alive;
  float jump_start, prev_h, prev_speed;
} Sled;
extern Sled sled;
extern float game_time;
/* the camera: its place and turn (rows: right, up, ahead) */
extern float cam_pos[3], cam_rot[9];
void sled_model_matrix(float m[12]);   /* the sled's model in the world */
void game_init(void);
void game_step(uint32_t keys, uint32_t hit);
extern bool game_running;

/* ---------------------------------------------------------------- drawing (draw.c) */
void draw_frame(void);
void draw_init(void);
#endif
