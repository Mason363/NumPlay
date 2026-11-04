/* Saves: the appvar NPfnaf (NumPlay's name for the game's saves), of a fixed size: the progress (the night
 * reached, the stars, the custom night's levels), then the night as Home left it (read once on the next start).
 * It stays in RAM while the game runs (written where it is) and is archived when the game ends. The same
 * functions as the NumWorks version's save.c, for game.c. */
#include <string.h>
#include "fnaf.h"

#define MAGIC 0x46414E46u   /* "FNAF" */
#define VERSION 1

typedef struct {
  uint32_t magic;
  uint8_t version, reached, stars, keys_seen;
  int8_t custom[4];
} Progress;

typedef struct {
  uint32_t magic;
  uint32_t size;
  Night n;
} Resume;

typedef struct {
  Progress p;
  Resume r;
} Save;

static Save *save;
static Save fallback;   /* (no room for the appvar: the game still runs, without saving) */

void save_open(void) {
  save = (Save *)(void *)ce_save_open("NPfnaf", sizeof(Save));
  if (!save) save = &fallback;
}

void save_restore_copy(void) {}

void save_progress(void) {
  Progress p;
  memset(&p, 0, sizeof p);
  p.magic = MAGIC, p.version = VERSION;
  p.reached = g.reached, p.stars = g.stars, p.keys_seen = g.keys_seen;
  memcpy(p.custom, g.custom, sizeof p.custom);
  memcpy(&save->p, &p, sizeof p);
  ce_save_done();
}

void save_load(void) {
  Progress p;
  memcpy(&p, &save->p, sizeof p);
  g.reached = 1;
  if (p.magic != MAGIC || p.version != VERSION) return;
  g.reached = p.reached >= 1 && p.reached <= 5 ? p.reached : 1;
  g.stars = p.stars & 7;
  g.keys_seen = p.keys_seen;
  for (int k = 0; k < 4; k++) g.custom[k] = p.custom[k] >= 0 && p.custom[k] <= 20 ? p.custom[k] : 0;
}

void save_resume(void) {
  Resume r;
  memset(&r, 0, sizeof r);
  r.magic = MAGIC, r.size = sizeof(Night), r.n = g.n;
  memcpy(&save->r, &r, sizeof r);
  ce_save_done();
}

bool save_resume_load(void) {
  Resume r;
  memcpy(&r, &save->r, sizeof r);
  const Night *n = &r.n;
  bool ok = r.magic == MAGIC && r.size == sizeof(Night) && n->night >= 1 && n->night <= 7 && n->hour < 6 &&
            n->cam < CAM_COUNT && n->light <= 2 && n->foxy <= 3 && n->gold <= 4 && n->dark <= 3 &&
            n->pos[0] <= IN_OFFICE && n->pos[1] <= IN_OFFICE && n->pos[2] <= IN_OFFICE;
  if (ok) g.n = r.n;
  memset(&save->r, 0, sizeof save->r);
  ce_save_done();
  return ok;
}
