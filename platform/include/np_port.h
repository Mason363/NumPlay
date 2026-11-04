/* What a NumPlay target gives the games besides EADK (eadk.h): their files, the RAM after their own, and the app's
 * start and end. Built for any target but the NumWorks (NP_PORT defined), games/common/epsilon_app.h and
 * epsilon_files.h use these instead of the NumWorks's own.
 *
 * Files. A game's files are records in one buffer laid out as the NumWorks keeps them (a 16-bit size, the name,
 * the content; a zero size ends the list), so every game's saves work unchanged. The target loads the buffer when
 * the app starts and keeps it (a file, an appvar...) whenever np_storage_changed() is called. */
#ifndef NP_PORT_H
#define NP_PORT_H
#include <stdbool.h>
#include <stdint.h>

/* the files' buffer and its size (the records, then their end) */
uint8_t *np_storage(uint32_t *size);
/* a record was written or removed: keep the buffer */
void np_storage_changed(void);

/* a slow target: games draw less (the NumWorks N0110 and N0115 answer true) */
bool np_slow(void);

/* the target's name: "sdl", "web", "nspire", "casio-cg", "ti84ce"... */
const char *np_target(void);

/* the app starts; it ends (`return np_end();` from main): the files are kept */
void np_begin(void);
int np_end(void);

#endif
