/* Among Us: the minigames' shared state (mg.c, mg2.c). */
#ifndef AU_MG_H
#define AU_MG_H
#include "game.h"
#include "gfx.h"

enum {
  MG_CARD, MG_WIRES, MG_ASTEROIDS, MG_ALIGN, MG_SCAN, MG_SAMPLE, MG_FUEL, MG_SIMON, MG_GARBAGE, MG_UPLOAD,
  MG_CALIBRATE, MG_COURSE, MG_LEAVES, MG_MANIFOLDS, MG_STEERING, MG_SHIELDS, MG_DIVERT, MG_ACCEPT, MG_HANDS,
  MG_LIGHTS, MG_COMMS, MG_KEYPAD, MG_CAMS, MG_EMERGENCY, N_MG
};

typedef struct {
  float x, y, vx, vy, a;
  uint8_t kind, on, pad[2];
} Thing;   /* an asteroid, a leaf, a piece of garbage */

typedef struct {
  uint8_t kind, task, cons, done;   /* task: the player's task, or NONE */
  uint8_t sel, phase, n, flags;
  uint8_t stage, held, pad0, pad1;
  float t, done_t, a, b, c;
  uint8_t order[16], state[16];
  float v[8];
  Thing th[12];
  char text[48];
} Mg;
_Static_assert(sizeof(Mg) <= sizeof(((Game *)0)->mg), "the minigame's state");
#define M ((Mg *)(void *)g.mg)

void mg_finish(void);                      /* the step done (or the sabotage's part) */
void mg_status(const char *s, int y, uint16_t c);
void mg_bg(int blob_id);
void mg_keys(const char *s);

/* each minigame: opened, a tick, drawn (mg2.c holds the second half) */
void mg2_open(int kind);
void mg2_tick(uint32_t keys, uint32_t pressed);
void mg2_draw(void);
#endif
