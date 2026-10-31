/* Made by tools/gen.py from Snow Rider 3D's Unity project: do not edit. */
#ifndef SR_DATA_H
#define SR_DATA_H
#include <stdint.h>

/* a mesh to draw: corners (int16 times s metres), triangles, a palette colour and a normal (int8, 127 = 1) each,
 * and a sphere around it */
typedef struct { const int16_t *v; const uint16_t *t; const uint8_t *c; const int8_t *n; uint16_t nv, nt; float s; float ctr[3], rad; } RMesh;
/* a collision mesh: corners, triangles (their front: clockwise seen from outside), its box */
typedef struct { const int16_t *v; const uint16_t *t; uint16_t nv, nt; float s; float lo[3], hi[3]; } CMesh;
/* a mesh of a prefab, near and far, and where it lies in the prefab (3x4, rows) */
typedef struct { uint16_t near, far; float m[12]; } Part;
/* a collider: box (the matrix takes the unit cube to it), sphere (unit sphere), capsule (centre, along axis dir,
 * radius r, height h) or mesh (cmeshes[mesh]); layer: L_ bits */
enum { C_BOX, C_SPHERE, C_CAPSULE, C_MESH };
enum { L_SURFACE = 1, L_OBSTACLE = 2, L_BREAK = 4, L_GIFT = 8 };
typedef struct { uint8_t kind, layer, trigger, dir_or_mesh; float m[12]; float r, h; } Coll;
#define COLL_MESH(c) ((c)->dir_or_mesh)
/* flags: 1 gift, 2 breaks into pieces (chunks[pieces]), 4 a physics body, 8 snowballs meeting it go (SnowballDestroy),
 * 16 a snowball spawner at spawner */
enum { PF_GIFT = 1, PF_BREAK = 2, PF_BODY = 4, PF_BALL_END = 8, PF_SPAWNER = 16 };
typedef struct { const Part *parts; const Coll *colls; uint8_t nparts, ncolls, flags; int16_t pieces, rand; float spawner[3]; } Prefab;
typedef struct { uint16_t prefab; float pos[3], rot[4], scale[3]; } Obj;
typedef struct { const Obj *obj; uint16_t n; } Chunk;
/* modifiers: tunnel (list: tunnels put in chunk 1 and 2), crack start (a ramp chunk, b side ramp chunk), next (the
 * structure after it is next), snowballs (a start chunk, b end chunk, a spawner rolling ball) */
enum { MOD_NONE, MOD_TUNNEL, MOD_CRACKSTART, MOD_NEXT, MOD_SNOWBALLS };
typedef struct { uint16_t prefab; float interval, speed, randomness, min_size, max_size, force; } Balls;
typedef struct { int32_t min_score, max_score; float prob; uint8_t min_len, max_len, mod; int16_t next, a, b;
  const uint16_t *mid; uint8_t nmid; const uint16_t *side; uint8_t nside; const uint16_t *list; uint8_t nlist; Balls balls; } Struct;
typedef struct { uint8_t min_len, max_len; const uint16_t *structs; uint8_t n; } Biome;
typedef struct { uint16_t prefab; float lo[3], hi[3]; const uint16_t *alt; uint8_t nalt; } Rand;
typedef struct { const char *name; uint16_t price, prefab, pieces; float cam[3], box_c[3], box_h[3]; } Skin;

extern const RMesh rmeshes[];
extern const CMesh cmeshes[];
extern const Prefab prefabs[];
extern const Chunk chunks[];
extern const Struct structs[];
extern const Biome biomes[];
extern const Rand rands[];
extern const Skin skins[];
extern const uint16_t palette[];
extern const uint8_t palette888[][3];
#define NRANDS 14
#define NCOLOURS 65
#define NSKINS 11
#define NBIOMES 3
#define CAMP_CHUNK 83

/* GenSettings */
#define VISIBLE_STRUCTS 6
#define STRUCT_W 4
#define CHUNK_X 5.0f
#define CHUNK_Z 10.0f
#define WARMUP_LEN 2
/* BasicSleds */
#define SLED_BASEMOVESPEED 6.5f
#define SLED_SPEEDACCELERATION 4.5f
#define SLED_SPEEDACCELERATIONAMPLITUDE 0.77f
#define SLED_ROTATIONSPEED 95.0f
#define SLED_ROTATIONSMOOTHNESS 0.92f
#define SLED_MODELROTATIONSMOOTHNESS 0.95f
#define SLED_JUMPSPEED 3.5f
/* the scene */
#define GRAVITY_ACC 6.0f
#define PHYS_GRAVITY 3.0f
#define POINT_POS {0, -0.009f, 0.36f}
#define NRAYS 3
#define RAYS {{0, 0.2f, 0.6f}, {0.2f, 0.2f, 0.6f}, {-0.2f, 0.2f, 0.6f}}
#define RAY_LEN 1.0f
#define CAM_ROT_AMPLIFY {-0.2f, 0.15f, 0.25f}
#define CAM_OFFSET {0, 0.5f, -0.8f}
#define CAM_FOV 71.0f
#define CAM_INTRO_SPEED 0.7f
#define CURVATURE 0.004f
#define LIGHT_DIR {0.492404f, 0.766044f, -0.413176f}
#define LIGHT_INTENSITY 0.9f
#define AMBIENT {0.690196f, 0.67451f, 0.67451f}
#define SKY_GROUND {0.788235f, 0.890196f, 0.890196f}
#define BACKGROUND {0.497189f, 0.727285f, 0.889706f}
#define SLEDS_POINT {9.92f, 0.07f, -4.26f}
#define SLEDS_POINT_ROT {4.62692e-05f, 0.00769794f, -0.000874755f, 0.99997f}
#define CAMP_CAM_POINT {9.12f, 0.92f, -8.9f}
#define CAMP_CAM_ROT {-0.0170293f, 0.0803649f, -0.0042851f, -0.996611f}
#endif
