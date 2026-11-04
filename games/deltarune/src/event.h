/* Deltarune: events, the game's cutscenes and talks.
 *
 * An event is a C function called once a frame until it says it is done; the macros below make it read
 * like the original's step events (each TALK waits for its text, each WAIT for its frames). They are
 * protothreads on GCC's labels as values (a wait can be anywhere, in a switch too). The function's locals
 * don't last over a wait: what has to is kept in c->v. One event runs at a time; while it does, Kris
 * doesn't move unless it lets him. */
#ifndef DR_EVENT_H
#define DR_EVENT_H
#include <stdbool.h>
#include "res.h"
#include "text.h"
#include "world.h"

typedef struct Ev {
  int id, t;
  void *at;                  /* where it waits (NULL: from the start) */
  bool free;                 /* Kris walks while it runs (FREE()) */
  int v[6];
  Obj *self;
} Ev;

#define EV_CAT2(a, b) a##b
#define EV_CAT(a, b) EV_CAT2(a, b)
#define EV_BEGIN do { if (c->at) goto *c->at; } while (0); {
#define EV_END } return 1
#define EV_WAIT_(cond, n) do { c->at = &&EV_CAT(ev_at_, n); EV_CAT(ev_at_, n): if (!(cond)) return 0; } while (0)
#define WAIT_UNTIL(cond) EV_WAIT_(cond, __COUNTER__)
#define EV_WAITN_(N, n) do { c->t = (N); c->at = &&EV_CAT(ev_at_, n); EV_CAT(ev_at_, n): if (c->t-- > 0) return 0; } while (0)
#define WAIT(N) EV_WAITN_(N, __COUNTER__)
/* the dialogue box with the original's texts (their ids, data.h), a face first or none */
#define TALK(...) do { say_list(0, 0, (const int[]){__VA_ARGS__, -1}); WAIT_UNTIL(!text_active()); } while (0)
#define TALKF(fc, fe, ...) do { say_list(fc, fe, (const int[]){__VA_ARGS__, -1}); WAIT_UNTIL(!text_active()); } while (0)
#define FREE(b) (c->free = (b))
/* a thing walks to (x, y) at sp units a frame; WALK waits for it */
#define WALK(o, X, Y, SP) do { obj_walk_to(o, X, Y, SP); WAIT_UNTIL(!(o)->walking); } while (0)
#define KRIS_WALK(X, Y, SP) do { pl_walk_to(X, Y, SP); WAIT_UNTIL(!pl_walking()); } while (0)
#define BATTLE(id) do { battle_start(id); WAIT_UNTIL(battle_over()); } while (0)
#define FADE_TO(r, e) do { world_fade_to(r, e); WAIT_UNTIL(!world_fading()); } while (0)

/* in a TALK's list: a message that only changes the face (the original's scr_ralface(i, fe)...); who 0 none */
#define F_(who, fe) (0x40000 | (who) << 8 | (fe))
void say_list(int fc, int fe, const int *ids);
void say_unskippable(int id);
void say_add(const int *ids);   /* (more of the same into the box being made: text_clear first) */
void event_start(int id, Obj *self);
bool event_running(void);
bool event_blocking(void);   /* running, and Kris can't walk */
void event_update(void);
void event_cancel(void);
int event_run(Ev *c);        /* rooms.c: the events, by id */
#endif
