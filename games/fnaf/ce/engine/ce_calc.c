/* The TI-84 Plus CE (ce.h): graphx's 8-bit mode and two buffers, keypadc, fileioc's appvars. */
#include "ce.h"
#include <fileioc.h>
#include <graphx.h>
#include <keypadc.h>
#include <string.h>
#include <sys/lcd.h>
#include <ti/screen.h>
#include <ti/vars.h>
#include <time.h>

#define LCD_INT_LNBU 4   /* the LCD took the next frame's address (its vertical sync is over) */

uint8_t *scr;
uint16_t ce_pal[256];
bool ce_pal_dirty;
char ce_missing[9];
static uint8_t save_handle;

bool ce_init(void) {
  kb_DisableOnLatch();
  kb_SetMode(MODE_3_CONTINUOUS);   /* (the keys always there: no waiting for a scan) */
  gfx_Begin();
  gfx_SetDrawBuffer();
  memset(gfx_palette, 0, 512);
  memset(gfx_vram, 0, 2 * SCR_W * SCR_H);
  scr = (uint8_t *)gfx_vbuffer;
  return true;
}

void ce_quit(void) {
  if (save_handle) {
    ti_SetArchiveStatus(true, save_handle);
    ti_Close(save_handle);
    save_handle = 0;
  }
  gfx_End();
  kb_Reset();
  /* a key still down would act on the home screen: wait until it is let go (2 seconds at most) */
  for (clock_t t = clock(); ce_keys() && clock() - t < 2 * CLOCKS_PER_SEC;) {}
  os_ClrHomeFull();
}

uint32_t ce_frames;
static bool waiting, keeping;   /* shown, scr not ready yet (ce_wait); to be the shown frame's copy */

void ce_show(bool keep) {
  ce_frames += keep ? 3 : 1;
  gfx_SwapDraw();
  waiting = true, keeping = keep;
  if (ce_pal_dirty) {   /* the palette as the new frame starts: once the LCD has taken its address (gfx_SwapDraw
                         * cleared the flag) */
    while (!(lcd_IntStatus & LCD_INT_LNBU)) {}
    memcpy(gfx_palette, ce_pal, sizeof ce_pal);
    ce_pal_dirty = false;
    ce_wait();
  }
}

void ce_wait(void) {
  if (!waiting) return;
  gfx_Wait();
  waiting = false;
  scr = (uint8_t *)gfx_vbuffer;
  if (keeping) memcpy(scr, scr == gfx_vram ? gfx_vram + SCR_W * SCR_H : gfx_vram, SCR_W * SCR_H);
}

uint32_t ce_keys(void) {
  static const struct {
    uint8_t group, bit;
    uint32_t key;
  } map[] = {
    {7, kb_Left, CK_LEFT}, {7, kb_Right, CK_RIGHT}, {7, kb_Up, CK_UP}, {7, kb_Down, CK_DOWN},
    {1, kb_2nd, CK_2ND}, {6, kb_Enter, CK_ENTER}, {6, kb_Clear, CK_CLEAR}, {1, kb_Mode, CK_MODE},
    {1, kb_Del, CK_DEL}, {2, kb_Alpha, CK_ALPHA},
    {3, kb_0, CK_DIGIT(0)}, {3, kb_1, CK_DIGIT(1)}, {4, kb_2, CK_DIGIT(2)}, {5, kb_3, CK_DIGIT(3)},
    {3, kb_4, CK_DIGIT(4)}, {4, kb_5, CK_DIGIT(5)}, {5, kb_6, CK_DIGIT(6)}, {3, kb_7, CK_DIGIT(7)},
    {4, kb_8, CK_DIGIT(8)}, {5, kb_9, CK_DIGIT(9)},
  };
  uint32_t k = 0;
  for (unsigned i = 0; i < sizeof map / sizeof map[0]; i++)
    if (kb_Data[map[i].group] & map[i].bit) k |= map[i].key;
  if (kb_On) k |= CK_ON;
  return k;
}

uint32_t ce_ticks(void) { return (uint32_t)clock(); }

bool ce_data(const char *prefix, int count, const uint8_t **vars) {
  for (int i = 0; i < count; i++) {
    char name[9];
    size_t n = strlen(prefix);
    memcpy(name, prefix, n);
    if (i >= 10) name[n++] = (char)('0' + i / 10);
    name[n++] = (char)('0' + i % 10);
    name[n] = 0;
    uint8_t h = ti_Open(name, "r");
    if (!h) {
      memcpy(ce_missing, name, sizeof name);
      return false;
    }
    vars[i] = ti_GetDataPtr(h);
    ti_Close(h);
  }
  return true;
}

uint8_t *ce_ram(size_t *len) {
  void *p;
  *len = os_MemChk(&p);
  return p;
}

uint8_t *ce_save_open(const char *name, size_t size) {
  uint8_t h = ti_Open(name, "r+");
  if (h && ti_GetSize(h) != size) {
    ti_Close(h);
    ti_Delete(name);
    h = 0;
  }
  if (h) {
    ti_SetArchiveStatus(false, h);   /* in RAM while the game runs: written in place */
  } else {
    h = ti_Open(name, "w+");
    if (!h) return NULL;
    static const uint8_t zero[64];
    for (size_t n = 0; n < size; n += sizeof zero) ti_Write(zero, 1, size - n < sizeof zero ? size - n : sizeof zero, h);
  }
  save_handle = h;
  return ti_GetDataPtr(h);
}

void ce_save_done(void) {}

/* a message on the home screen (the game can't start), until a key */
void ce_message(const char *a, const char *b) {
  gfx_End();
  os_ClrHomeFull();
  os_SetCursorPos(0, 0);
  os_PutStrFull(a);
  os_SetCursorPos(1, 0);
  os_PutStrFull(b);
  while (ce_keys()) {}
  while (!ce_keys()) {}
  while (ce_keys()) {}
  os_ClrHomeFull();
}
