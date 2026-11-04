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
  kb_DisableOnLatch();
}
void plat_exit(void) {
  uint8_t h = ti_Open("CELSAVE", "r+");   /* the save goes to the archive (it stays in RAM while playing) */
  if (h) ti_SetArchiveStatus(true, h), ti_Close(h);
  gfx_End();
  kb_ClearOnLatch();
}
void plat_frame(void) {
  gfx_SwapDraw();
  g_fb = (uint8_t *)gfx_vbuffer;
}
void plat_begin_draw(void) { gfx_Wait(); }
void plat_palette(const uint16_t *pal, int first, int n) { memcpy(gfx_palette + first, pal, (size_t)n * 2); }

unsigned plat_keys(void) {
  kb_Scan();
  unsigned k = 0;
  uint8_t g1 = kb_Data[1], g2 = kb_Data[2], g6 = kb_Data[6], g7 = kb_Data[7];
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
  uint8_t h = ti_Open(name, "w");
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
