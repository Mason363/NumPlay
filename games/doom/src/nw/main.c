/* Doom on the NumWorks calculator.
 *
 * The game runs until Home or On/Off is pressed (the moment is saved, and comes back on the next start),
 * Quit is chosen in the menu, or an error stops it. */
#include <stdint.h>

#include "doomtype.h"
#include "plat.h"
#include "nw.h"

void D_DoomMain(void);

#ifndef NW_WARP
#define NW_WARP 0
#endif
int nw_warp = NW_WARP;

#if !defined(HOST)
#ifndef NP_PORT /* (another target: platform/port.mk names the app) */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Doom";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif
#else
#define main nw_main /* (tests/play.c has the computer's main) */
int nw_main(void);
#endif

int main(void) {
    plat_begin();
    nw_saves_restore();
    if (!__builtin_setjmp(nw_exit)) {
        D_DoomMain();
        // the main loop ended: Home or On/Off
        if (nw_home) nw_resume_save();
    }
    nw_settings_save();
#ifndef NP_PORT
    while (plat_keys()) plat_sleep(10); /* (the key that quit is not given to the calculator) */
#endif
    return plat_end();
}
