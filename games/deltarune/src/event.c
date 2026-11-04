/* The event that runs now (event.h). */
#include <string.h>
#include "event.h"
#include "game.h"

static Ev cur;
static bool running;

/* the face changes a list asks for: "\\F0\\Ex\\Fw%" (made once each, they last) */
const char *face_msg(int code) {
  static char made[48][12];
  static int codes[48], n;
  for (int i = 0; i < n; i++)
    if (codes[i] == code) return made[i];
  int k = n < 48 ? n++ : 47;
  char *m = made[k], who = (char)((code >> 8) & 0xFF);
  int fe = code & 0xFF, j = 0;
  m[j++] = '\\', m[j++] = 'F', m[j++] = '0';
  if (who) m[j++] = '\\', m[j++] = 'E', m[j++] = (char)(fe < 10 ? '0' + fe : 'A' + fe - 10), m[j++] = '\\', m[j++] = 'F', m[j++] = who;
  m[j++] = '%', m[j] = 0;
  codes[k] = code;
  return m;
}
void say_add(const int *ids) {
  for (int i = 0; ids[i] >= 0; i++) text_add(ids[i] & 0x40000 ? face_msg(ids[i]) : str((uint16_t)ids[i]));
}
/* a text the player can't hurry (the original's skippable = 0) */
void say_unskippable(int id) {
  static char buf[256];
  const char *s = str((uint16_t)id);
  buf[0] = '\\', buf[1] = 's', buf[2] = '0';
  strncpy(buf + 3, s, sizeof buf - 4);
  buf[sizeof buf - 1] = 0;
  text_clear();
  text_add(buf);
  text_open();
}
void say_list(int fc, int fe, const int *ids) {
  text_clear();
  text_face(fc, fe);
  say_add(ids);
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
