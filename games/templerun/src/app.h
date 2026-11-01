/* Temple Run: the screens and the way between them - the title, the run, pause, Save me, the results,
 * the shop, the missions, how to play. */
#ifndef SS_APP_H
#define SS_APP_H
#include <stdbool.h>
#include <stdint.h>

extern bool app_running;
void app_init(void);
void app_step(uint32_t keys, uint32_t pressed);   /* a 60th of a second */
void app_draw(void);
bool app_in_run(void);   /* (the run is on: tests and the timing build) */
#endif
