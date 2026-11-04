/* NumBlocks on the TI-84 Plus CE: the screen in 8 bits a pixel (graphx: 0xD40000 shown), the world's window in
 * the other half of the screen memory, the keys (keypadc), the pictures and the worlds in appvars (fileioc).
 *
 * The keys, NumPlay's CE layout (platform/ti84ce/README.md): arrows look (and move in screens); cos , ( ) walk
 * (an upside-down T, like W A S D); y= jumps (shift), alpha sneaks, del sprints (backspace); 2nd mines (OK),
 * clear places (Back); vars opens the inventory; 1 to 9 pick a hotbar slot; x,T,θ,n drops; log picks the block;
 * (-) pauses (ans); enter is EXE; mode (Home) saves and quits. */
#ifdef __ez80__
#include <fileioc.h>
#include <graphx.h>
#include <keypadc.h>
#include <string.h>
#include <sys/lcd.h>
#include <ti/getcsc.h>
#include <ti/screen.h>
#include <time.h>
#include "nb.h"

uint8_t *scr = (uint8_t *)lcd_Ram;
static uint8_t pal_live[256 * 3];
bool vram_lost;          /* the screen memory was cleared (a garbage collection): the window must be made again */

static const uint8_t *appvar(const char *name, unsigned size) {
  uint8_t h = ti_Open(name, "r");
  if (!h) return 0;
  const uint8_t *p = ti_GetSize(h) == size ? ti_GetDataPtr(h) : 0;
  ti_Close(h);
  return p;
}
static void gfx_reload(void) {
  gfx = appvar("NBCEDAT", GFX_SIZE);
  texdat = appvar("NBCETEX", TEXDAT_SIZE);
}
static void gc_before(void) { gfx_End(); }
static void gc_after(void) {
  gfx_Begin();
  plat_palette(0, 256, pal_live);
  gfx_reload();
  vram_lost = true;
}

static void fail(const char *a, const char *b) {
  gfx_End();
  os_ClrHome();
  os_PutStrFull(a);
  os_NewLine();
  os_PutStrFull(b);
  while (!os_GetCSC()) {}
  exit(0);
}

void plat_init(void) {
  gfx_reload();
  if (!gfx || !texdat || gfx[0] != 'N' || gfx[4] != GFX_VERSION) {
    os_ClrHome();
    os_PutStrFull("NumBlocks needs NBCEDAT");
    os_NewLine();
    os_PutStrFull("and NBCETEX too.");
    while (!os_GetCSC()) {}
    exit(0);
  }
  gfx_Begin();
  memcpy(pal_live, pal_rgb, sizeof pal_live);
  plat_palette(0, 256, pal_rgb);
  ti_SetGCBehavior(gc_before, gc_after);
  kb_SetMode(MODE_3_CONTINUOUS);
  (void)fail;
}

void plat_exit(void) {
  ti_Delete("NBCEtmp");
  kb_SetMode(MODE_0_IDLE);
  gfx_End();
  exit(0);
}

void plat_palette(int first, int n, const uint8_t *rgb) {
  volatile uint16_t *p = lcd_Palette + first;
  if (rgb != pal_live + first * 3) memcpy(pal_live + first * 3, rgb, (size_t)n * 3);
  for (int i = 0; i < n; i++, rgb += 3) {
    unsigned r = rgb[0], g = rgb[1], b = rgb[2];
    p[i] = (uint16_t)((r >> 3) << 10 | (g >> 3) << 5 | (b >> 3) | (g & 4) << 13);
  }
}

uint32_t plat_keys(void) {
  /* (keypadc scans on its own in continuous mode) */
  uint32_t k = 0;
  uint8_t g1 = kb_Data[1], g2 = kb_Data[2], g3 = kb_Data[3], g4 = kb_Data[4], g5 = kb_Data[5], g6 = kb_Data[6],
          g7 = kb_Data[7];
  if (g7 & kb_Left) k |= K_LEFT;
  if (g7 & kb_Right) k |= K_RIGHT;
  if (g7 & kb_Up) k |= K_UP;
  if (g7 & kb_Down) k |= K_DOWN;
  if (g4 & kb_Cos) k |= K_FWD;
  if (g4 & kb_LParen) k |= K_BACK;
  if (g3 & kb_Comma) k |= K_SLEFT;
  if (g5 & kb_RParen) k |= K_SRIGHT;
  if (g1 & kb_Yequ) k |= K_JUMP;
  if (g2 & kb_Alpha) k |= K_SNEAK;
  if (g1 & kb_Del) k |= K_SPRINT;
  if (g1 & kb_2nd) k |= K_ATTACK;
  if (g6 & kb_Clear) k |= K_USE;
  if (g5 & kb_Vars) k |= K_INV;
  if (g5 & kb_Chs) k |= K_PAUSE;
  if (g1 & kb_Mode) k |= K_HOME;
  if (g3 & kb_GraphVar) k |= K_DROP;
  if (g2 & kb_Log) k |= K_PICK;
  if (g6 & kb_Enter) k |= K_ENTER;
  if (g3 & kb_1) k |= K_SLOT(0);
  if (g4 & kb_2) k |= K_SLOT(1);
  if (g5 & kb_3) k |= K_SLOT(2);
  if (g3 & kb_4) k |= K_SLOT(3);
  if (g4 & kb_5) k |= K_SLOT(4);
  if (g5 & kb_6) k |= K_SLOT(5);
  if (g3 & kb_7) k |= K_SLOT(6);
  if (g4 & kb_8) k |= K_SLOT(7);
  if (g5 & kb_9) k |= K_SLOT(8);
  if (kb_On) k |= K_HOME;
  return k;
}

uint32_t plat_ms(void) { return (uint32_t)clock() * 125 >> 12; }   /* (32768 a second) */
void plat_frame(void) {}
uint8_t *plat_spare(void) { return (uint8_t *)lcd_Ram + SCREEN_W * SCREEN_H; }

/* the edit log's memory: a RAM appvar, made once as big as it will be (resizing one loses its data) */
void *plat_grow(void *p, unsigned bytes) {
  uint8_t h = ti_Open("NBCEtmp", "r+");
  if (!h) h = ti_Open("NBCEtmp", "w");
  if (!h) return 0;
  if (ti_GetSize(h) >= bytes) {
    void *d = ti_GetDataPtr(h);
    ti_Close(h);
    return d;
  }
  /* bigger: a new one, the old data copied over */
  unsigned old = ti_GetSize(h);
  ti_Close(h);
  uint8_t n = ti_Open("NBCEtm2", "w");
  if (!n) return 0;
  if (ti_Resize(bytes, n) <= 0) {
    ti_Close(n);
    ti_Delete("NBCEtm2");
    return 0;
  }
  h = ti_Open("NBCEtmp", "r");
  if (h && old && p) memcpy(ti_GetDataPtr(n), ti_GetDataPtr(h), old);
  if (h) ti_Close(h);
  ti_Close(n);
  ti_Delete("NBCEtmp");
  ti_Rename("NBCEtm2", "NBCEtmp");
  h = ti_Open("NBCEtmp", "r");
  void *d = h ? ti_GetDataPtr(h) : 0;
  if (h) ti_Close(h);
  return d;
}

const uint8_t *plat_load(const char *name, unsigned *len) {
  uint8_t h = ti_Open(name, "r");
  if (!h) return 0;
  *len = ti_GetSize(h);
  const uint8_t *p = ti_GetDataPtr(h);
  ti_Close(h);
  return p;
}

bool plat_save(const char *name, const void *a, unsigned na, const void *b, unsigned nb) {
  if (na + nb > 65505) return false;
  uint8_t h = ti_Open(name, "w");
  if (!h) return false;
  bool ok = ti_Write(a, 1, na, h) == na && (!nb || ti_Write(b, 1, nb, h) == nb);
  if (ok) ti_SetArchiveStatus(true, h);
  ti_Close(h);
  return ok;
}

void plat_delete(const char *name) { ti_Delete(name); }

void plat_expand(const uint8_t *src, uint8_t *dst, int rows) {
  for (int y = 0; y < rows; y++, src += RW, dst += 2 * SCREEN_W) {
    uint8_t *d = dst;
    for (int x = 0; x < RW; x++, d += 2) d[0] = d[1] = src[x];
    memcpy(dst + SCREEN_W, dst, SCREEN_W);
  }
}
#endif
