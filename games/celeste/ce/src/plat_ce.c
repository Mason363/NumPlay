/* The TI-84 Plus CE: the LCD at 8 bits a pixel, two buffers (graphx), the keypad, the 32768 Hz timer, appvars.
 * Keys as NumPlay has them on the CE (platform/ti84ce/README.md): 2nd OK (jump), clear Back (dash), math toolbox
 * (grab), del backspace (pause), enter EXE, mode Home (save and quit). */
#include "ce.h"
#include <compression.h>
#include <fileioc.h>
#include <graphx.h>
#include <keypadc.h>
#include <sys/timers.h>
#include <ti/getcsc.h>

uint8_t *g_fb;

void plat_init(void) {
  gfx_Begin();
  gfx_SetDrawBuffer();
  memset(gfx_vram, 0, 320 * 240 * 2);
  g_fb = (uint8_t *)gfx_vbuffer;
  timer_Disable(2);
  timer_Set(2, 0);
  timer_Enable(2, TIMER_32K, TIMER_NOINT, TIMER_UP);
#ifdef CEL_BENCH
  timer_Disable(1);
  timer_Set(1, 0);
  timer_Enable(1, TIMER_CPU, TIMER_NOINT, TIMER_UP);
#endif
  kb_DisableOnLatch();
  kb_SetMode(MODE_3_CONTINUOUS);   /* (the keypad scans by itself: reading it is instant) */
}
/* The RAM the game takes besides its program and static data: an appvar, CELRAM, made last (the save before it, in
 * RAM and at its full size, is only written in place while playing), so nothing moves it; gone at the end. */
#define SAVE_SIZE 512
uint8_t *plat_ram(unsigned n) {
  uint8_t h = ti_Open("CELSAVE", "r");
  if (h) {
    if (ti_IsArchived(h)) ti_SetArchiveStatus(false, h);
    ti_Close(h);
  }
  h = ti_Open("CELSAVE", "r+");
  if (!h) h = ti_Open("CELSAVE", "w");
  if (!h) return NULL;
  if (ti_GetSize(h) < SAVE_SIZE) ti_Resize(SAVE_SIZE, h);
  ti_Close(h);
  ti_Delete("CELRAM");
  h = ti_Open("CELRAM", "w");
  if (!h) return NULL;
  if (ti_Resize(n, h) != (int)n) {
    ti_Close(h);
    ti_Delete("CELRAM");
    return NULL;
  }
  uint8_t *p = ti_GetDataPtr(h);
  ti_Close(h);
  return p;
}

void plat_exit(void) {
  ti_Delete("CELRAM");
  uint8_t h = ti_Open("CELSAVE", "r");   /* the save goes to the archive (it stays in RAM while playing) */
  if (h) ti_SetArchiveStatus(true, h), ti_Close(h);
  gfx_End();
  kb_Reset();
  kb_ClearOnLatch();
}
__attribute__((noinline)) void plat_frame(void) {   /* (not inlined: the emulator counts frames here) */
  gfx_SwapDraw();
  g_fb = (uint8_t *)gfx_vbuffer;
}
void plat_begin_draw(void) { gfx_Wait(); }
void plat_palette(const uint16_t *pal, int first, int n) { memcpy(gfx_palette + first, pal, (size_t)n * 2); }

/* the keys down at any of the looks since the last plat_keys (plat_poll while drawing: a quick press during a long
 * frame still counts) */
static uint8_t seen[4];
void plat_poll(void) {
  seen[0] |= kb_Data[1], seen[1] |= kb_Data[2], seen[2] |= kb_Data[6], seen[3] |= kb_Data[7];
}
unsigned plat_keys(void) {
  unsigned k = 0;
  plat_poll();
  uint8_t g1 = seen[0], g2 = seen[1], g6 = seen[2], g7 = seen[3];
  seen[0] = seen[1] = seen[2] = seen[3] = 0;
  if (g7 & kb_Left) k |= K_LEFT;
  if (g7 & kb_Right) k |= K_RIGHT;
  if (g7 & kb_Up) k |= K_UP;
  if (g7 & kb_Down) k |= K_DOWN;
  if (g1 & kb_2nd) k |= K_JUMP | K_OK;
  if (g6 & kb_Enter) k |= K_OK;
  if (g6 & kb_Clear) k |= K_DASH | K_BACK | K_TALK;
  if (g2 & kb_Math) k |= K_GRAB;
  if (g1 & kb_Del) k |= K_PAUSE;
  if (g1 & kb_Mode) k |= K_QUIT;
  if (kb_On) k |= K_QUIT, kb_ClearOnLatch();
  return k;
}

unsigned plat_ticks(void) { return (unsigned)timer_Get(2); }
void plat_wait(unsigned t) {
  unsigned start = plat_ticks();
  while (((plat_ticks() - start) & 0xFFFFFF) < t) {}
}

const uint8_t *plat_appvar(const char *name) {
  uint8_t h = ti_Open(name, "r");
  if (!h) return NULL;
  const uint8_t *p = ti_GetDataPtr(h);
  ti_Close(h);
  return p;
}
bool plat_save(const char *name, const void *data, unsigned n) {
  uint8_t h = ti_Open(name, "r+");   /* (in place: made at its size by plat_ram) */
  if (!h) return false;
  bool ok = ti_Write(data, n, 1, h) == 1;
  ti_Close(h);
  return ok;
}
bool plat_load(const char *name, void *data, unsigned n) {
  uint8_t h = ti_Open(name, "r");
  if (!h) return false;
  bool ok = ti_Read(data, n, 1, h) == 1;
  ti_Close(h);
  return ok;
}
void plat_unpack(void *dst, const void *src) { zx0_Decompress(dst, src); }

#ifdef CEL_BENCH
unsigned long plat_cycles(void) {   /* its low 24 bits, read twice until the reads agree (a byte may carry mid-read) */
  volatile unsigned *t = (volatile unsigned *)TIMER_COUNT_ADDR(1);
  for (;;) {
    unsigned a = *t, b = *t;
    if (((b - a) & 0xFFFFFF) < 200) return b;
  }
}
#endif
