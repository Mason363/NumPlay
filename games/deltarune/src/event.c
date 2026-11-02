/* The event that runs now (event.h). */
#include <string.h>
#include "event.h"
#include "game.h"

static Ev cur;
static bool running;

void say_list(int fc, int fe, const int *ids) {
  text_clear();
  text_face(fc, fe);
  for (int i = 0; ids[i] >= 0; i++) text_add(str((uint16_t)ids[i]));
  text_open();
}

void event_start(int id, Obj *self) {
  if (running || !id) return;
  memset(&cur, 0, sizeof cur);
  cur.id = id, cur.self = self;
  running = true;
  event_update();   /* (it starts this frame: a talk opens at once) */
}

bool event_running(void) { return running; }
bool event_blocking(void) { return running && !cur.free; }
void event_cancel(void) { running = false; }

void event_update(void) {
  if (!running) return;
  if (event_run(&cur)) running = false;
}
