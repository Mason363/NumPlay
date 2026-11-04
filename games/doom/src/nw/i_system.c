/* Doom on the calculator: the system (memory, time, errors, quitting). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doomtype.h"
#include "i_system.h"
#include "i_timer.h"
#include "i_video.h"
#include "m_misc.h"
#include "z_zone.h"
#include "plat.h"
#include "nw.h"

/* ------------------------------------------------------------------ memory
 * The zone (Doom's heap) is all the RAM the app has after its variables: from _heap_start to _heap_end
 * on the calculator, a fixed block on a computer (as much as the calculator gives on its own software). */
#if NW_SHORTPTR
extern char _data_section_start_ram[], _heap_start[], _heap_end[];
uintptr_t nw_ram_base;
#endif

byte *I_ZoneBase(int *size) {
#if NW_SHORTPTR
    nw_ram_base = ((uintptr_t)_data_section_start_ram & ~3u) - 4;
    uintptr_t start = ((uintptr_t)_heap_start + 3) & ~3u, end = (uintptr_t)_heap_end & ~3u;
    *size = (int)(end - start);
    return (byte *)start;
#else
    static uint32_t zone[NW_HOST_ZONE / 4];
    *size = sizeof zone;
    return (byte *)zone;
#endif
}

#if NW_SHORTPTR
/* (the few mallocs left, from the zone) */
void *malloc(size_t n) { return Z_Malloc((int)n, PU_STATIC, NULL); }
void *calloc(size_t a, size_t b) {
    void *p = malloc(a * b);
    memset(p, 0, a * b);
    return p;
}
void free(void *p) {
    if (p) Z_Free(p);
}
void *realloc(void *p, size_t n) {
    I_Error("realloc");
}
/* (and the C library's own, so that nothing else takes from the heap) */
struct _reent;
void *_malloc_r(struct _reent *r, size_t n) { return malloc(n); }
void _free_r(struct _reent *r, void *p) { free(p); }
void *_realloc_r(struct _reent *r, void *p, size_t n) { return realloc(p, n); }
void *_calloc_r(struct _reent *r, size_t a, size_t b) { return calloc(a, b); }
/* (no signals to raise: a division by zero gives 0, an abort is an error) */
int __aeabi_idiv0(int r) { return 0; }
long long __aeabi_ldiv0(long long r) { return 0; }
void abort(void) { I_Error("abort"); }
char *strdup(const char *s) {
    size_t n = strlen(s) + 1;
    return memcpy(malloc(n), s, n);
}
#endif

boolean drone = false; /* (a network game's watcher: never) */

void I_AtExit(atexit_func_t func, boolean run_on_error) {}
void I_Tactile(int on, int off, int total) {}
void I_PrintBanner(const char *msg) {}
void I_PrintDivider(void) {}
void I_PrintStartupBanner(const char *gamedescription) {}
boolean I_ConsoleStdout(void) { return false; }
boolean I_GetMemoryValue(unsigned int offset, void *value, int size) { return false; }
void I_Init(void) {}

/* ------------------------------------------------------------------ time: Doom's tics are 1/35 s */
static uint32_t time_base;
void I_InitTimer(void) { time_base = plat_millis(); }
int I_GetTimeMS(void) { return (int)(plat_millis() - time_base); }
int I_GetTime(void) { return (int)((uint64_t)(plat_millis() - time_base) * TICRATE / 1000); }
void I_Sleep(int ms) { plat_sleep(ms > 0 ? (uint32_t)ms : 0); }
void I_WaitVBL(int count) { I_Sleep(count * 1000 / 70); }
uint64_t time_us_64(void) { return (uint64_t)plat_millis() * 1000; }

/* ------------------------------------------------------------------ the end */
boolean nw_quit;
#if (defined(HOST) && defined(__APPLE__)) || defined(NP_PORT)
jmp_buf nw_exit;
#else
void *nw_exit[5];
#endif

void I_Quit(void) {
    nw_quit = true;
    __builtin_longjmp(nw_exit, 1);
}

/* An error the game cannot go on from: said on the screen until a key is pressed, then the app quits. */
void I_Error(const char *error, ...) {
    static boolean in_error;
    char msg[160];
    va_list ap;
    va_start(ap, error);
    vsnprintf(msg, sizeof msg, error, ap);
    va_end(ap);
    if (!in_error) {
        in_error = true;
        nw_show_error(msg);
    }
    nw_quit = true;
    __builtin_longjmp(nw_exit, 1);
}
