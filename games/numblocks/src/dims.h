/* NumBlocks.nwa only (NB_DIMS): what the generators of the Nether and the End share with the game. */
#ifndef NB_DIMS_H
#define NB_DIMS_H
#include <stdbool.h>
#include <stdint.h>
#include "blocks.h"

enum { DIM_OVERWORLD = 0, DIM_NETHER = 1, DIM_END = 2 };

/* The generators write a byte a block: codes 67..82 (wool, which no generator makes) stand for the
 * blocks past 255 (world.c turns them back) */
#define GEN_CODE(k) (B_WOOL_WHITE + (k))
enum { GC_NETHERRACK, GC_SOUL_SAND, GC_QUARTZ_ORE, GC_NETHER_BRICK, GC_FENCE, GC_STAIRS_E, GC_STAIRS_W, GC_STAIRS_S,
       GC_STAIRS_N, GC_WART, GC_END_STONE, GC_FRAME, GC_FRAME_EYE, GC_N };
extern const uint16_t gen_code_block[GC_N];

/* gen.c, past the Overworld (gen_dims.h): the generator of each dimension */
void gen_dim(int d);   /* which one gen_slab, gen_biome and gen_top run (after gen_init) */
int gen_fortress_at(int x, int y, int z);        /* 2: in a piece of a fortress, 1: in its box, 0: neither */
int gen_fortress_find(int x, int z, int *out);   /* (tests) the nearest fortress: x, y, z, pieces */

typedef struct { int16_t x, z; uint8_t y, h, r, pad; } EndSpike;   /* a pillar: its foot, height, radius */
const EndSpike *gen_spikes(int *n);   /* the End's pillars */
int gen_end_block(int x, int y, int z);   /* the End's block there, as made */

/* dims.c: what is kept of the dimensions with the world (save.c) */
typedef struct {
  float ow_x, ow_y, ow_z;      /* where the player was last in the Overworld */
  uint8_t dragon_dead, pad[3];
  float dragon_hp;             /* the dragon's health while it lives (0: not met yet, full) */
  uint32_t crystals_gone[2];   /* the End's crystals destroyed, by pillar */
} DimsState;
extern DimsState dst;
extern float portal_time;   /* 0..1: the portal's swirl over the picture */
extern int wither_ticks;     /* the wither effect on the player (a wither skeleton's hit) */
bool dims_punch(void);       /* (mob.c) the player's swing sends a fireball back */
bool dims_portal_light(int x, int y, int z);   /* fire there lights a portal? (then it is made) */
int dims_can_stay(int b, int x, int y, int z);   /* -1: not one of the dimensions' blocks */
void dims_player_tick(void);                   /* portals */
void dims_respawn(void);                       /* (before the player respawns) */
void player_to_spawn(void);
void dims_go(int to);   /* (tests) there, as through a portal */
void dragon_enter(void);   /* (end.c) */
void end_tick(void);
float dragon_bar(void);
bool dragon_beam(float *from, float *to);
int end_marks(float (*at)[3], uint8_t *kind, int max);   /* (end.c) the dragon and its whole crystals: kind 0, 1 */
bool view_mark(float x, float y, float z, float far, float *sx, float *sy, float *ux, float *uy);   /* (render.c) */
void dragon_offsets(int i, float pt, float *yaw, float *y);
float dragon_anim(float pt);
void throw_eye(int tx, int ty, int tz);   /* (mob.c) an eye of ender towards there */
bool gen_stronghold_near(int x, int z, int *out);   /* (gen.c) where an eye of ender flies to from there */
int gen_stronghold_chest(int x, int y, int z);      /* (gen.c) a stronghold's chest there: its list (0: none) */
int gen_stronghold_find(int s, int *out);           /* (gen.c, tests) stronghold s: where, its portal */
extern uint8_t *gen_hi;                             /* (gen.c) the ninth bits of the slab gen_slab fills */
void pearl_land(float x, float y, float z);
void dims_explode(float x, float y, float z, float power, bool fire);   /* (mob.c) */
void dims_spawners_tick(void);                                         /* (mob.c) the fortresses' blaze spawners */
int gen_fortress_spawners(int x, int z, int *out, int n);              /* (gen.c) theirs near there: x, y, z */        /* (mob.c) the player's ender pearl lands there */
enum { SKIN_CRYSTAL = 16, SKIN_DRAGON = 17 };   /* (the big skins: data.h's skin_crystal_px, skin_dragon_px) */
void dims_credits(void);
void credits_open(void);   /* (gui.c) GuiWinGame */
void player_use_on(int x, int y, int z, int f);   /* (player.c, tests) the held item used on that block */
static inline bool is_iron_door(int b) { return b >= B_IRON_DOOR_LOWER_E && b <= B_IRON_DOOR_LOWER_N_OPEN; }   /* (lower) */
#endif
