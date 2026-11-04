/* Balatro on the TI-84 Plus CE: the start (the code put in place, the screen at 8 bits a pixel), the keys
 * (platform/ti84ce/README.md: 2nd OK, enter EXE, clear Back, mode and on Home, del backspace, y= shift, alpha, math
 * toolbox, vars var), the clock and the files (the appvar NPbalato, archived when the game ends). The game itself is
 * the NumWorks version's (../src, its main() as balatro_main()). */
#include <string.h>
#include <eadk.h>
#include "ce.h"
#include "np_port.h"

int balatro_main(void);

/* ------------------------------------------------------------------ files */
/* The records (games/common/epsilon_files.h) live in the appvar itself, in RAM while the game runs (a fixed size:
   it never moves), archived when it ends. */
#define STORAGE_SIZE 5120
static const char SAVE_NAME[] = "NPbalato";
static uint8_t *storage;
uint8_t ce_save_pending;
uint8_t *ce_storage_open(const char *name, size_t size); /* the file's RAM bytes, its old content kept (or zeros) */

uint8_t *np_storage(uint32_t *size) {
  static uint8_t none[4];
  if (!storage) {
    storage = ce_storage_open(SAVE_NAME, STORAGE_SIZE);
    if (storage) {
      uint32_t at = 0;
      while (at + 2 <= STORAGE_SIZE && (storage[at] | storage[at + 1] << 8)) at += storage[at] | storage[at + 1] << 8;
      if (at + 2 > STORAGE_SIZE) memset(storage, 0, STORAGE_SIZE);
    }
  }
  if (!storage) { /* no room in RAM: no saves */
    *size = sizeof none;
    return none;
  }
  *size = STORAGE_SIZE;
  return storage;
}
void ce_storage_keep(void);
void np_storage_changed(void) {
  ce_save_pending = 1;
  ce_storage_keep();
}
bool np_slow(void) { return true; }
const char *np_target(void) { return "ti84ce"; }

/* ------------------------------------------------------------------ random */
static uint32_t rs = 0x9E3779B9u;
uint32_t eadk_random(void) {
  rs ^= rs << 13;
  rs ^= rs >> 17;
  rs ^= rs << 5;
  return rs;
}

#ifndef NP_HOST
/* ================================================================== the calculator */
#include <sys/lcd.h>
#include <sys/rtc.h>
#include <ti/getcsc.h>
#include <ti/screen.h>
#include <ti/vars.h>
#include <time.h>

uint8_t *ce_vram = (uint8_t *)0xD40000;

void ce_set_palette(int first, int n, const uint16_t *c) {
  volatile uint16_t *p = (volatile uint16_t *)0xE30200 + first;
  for (int i = 0; i < n; i++) p[i] = c[i];
}

const uint8_t *ce_appvar(const char *name, size_t *size) {
  var_t *v = os_GetAppVarData(name, 0);
  if (!v) return 0;
  *size = v->size;
  return v->data;
}

/* the clock: the 32768 Hz timer the C library starts for clock() */
uint64_t eadk_timing_millis(void) {
  uint32_t c = (uint32_t)clock();
  return (uint64_t)(c >> 15) * 1000 + (((c & 32767) * 1000) >> 15);
}
uint32_t ce_ms(void) { return (uint32_t)eadk_timing_millis(); }
void eadk_timing_msleep(uint32_t ms) {
  uint64_t until = eadk_timing_millis() + ms;
  while (eadk_timing_millis() < until) {}
}

/* the keys: the keypad scanned once, its groups read */
#define KB_MODE (*(volatile uint8_t *)0xF50000)
#define KB_DATA(g) (*(volatile uint16_t *)(0xF50010 + 2 * (g)))
#define KB_ON (*(volatile uint8_t *)0xF00020 & 1)
static const struct {
  uint8_t group, bit, key;
} KEYS[] = {
    {7, 1 << 1, eadk_key_left}, {7, 1 << 2, eadk_key_right}, {7, 1 << 3, eadk_key_up}, {7, 1 << 0, eadk_key_down},
    {1, 1 << 5, eadk_key_ok},   {6, 1 << 0, eadk_key_exe},   {6, 1 << 6, eadk_key_back}, {1, 1 << 6, eadk_key_home},
    {1, 1 << 7, eadk_key_backspace}, {2, 1 << 7, eadk_key_alpha}, {1, 1 << 4, eadk_key_shift},
    {2, 1 << 6, eadk_key_toolbox}, {5, 1 << 6, eadk_key_var}, {3, 1 << 7, eadk_key_xnt},
};
eadk_keyboard_state_t eadk_keyboard_scan(void) {
  KB_MODE = 2;
  while (KB_MODE & 3) {}
  eadk_keyboard_state_t s = 0;
  for (unsigned i = 0; i < sizeof KEYS / sizeof KEYS[0]; i++)
    if ((uint8_t)KB_DATA(KEYS[i].group) & KEYS[i].bit) s |= (eadk_keyboard_state_t)1 << KEYS[i].key;
  if (KB_ON) s |= (eadk_keyboard_state_t)1 << eadk_key_home;
  return s;
}

/* the save in a RAM appvar of a fixed size while the game runs (archiving it could move the art in the archive: only
   when the game ends); an archived one, or one of another size, is copied into a new one (through the strip buffer) */
uint8_t *ce_storage_open(const char *name, size_t size) {
  int arch = 0, scr;
  var_t *v = os_GetAppVarData(name, &arch);
  size_t keep = 0;
  uint8_t *tmp = g_scratch(&scr);
  if (v && (arch || v->size != size)) {
    keep = v->size < size ? v->size : size;
    if (keep > (size_t)scr) keep = (size_t)scr;
    memcpy(tmp, v->data, keep);
    void *entry, *data;
    if (os_ChkFindSym(OS_TYPE_APPVAR, name, &entry, &data)) os_DelSymEntry(entry);
    v = 0;
  } else if (v)
    return v->data;
  v = os_CreateAppVar(name, (uint16_t)size);
  if (!v) return 0;
  memcpy(v->data, tmp, keep);
  memset(v->data + keep, 0, size - keep);
  return v->data;
}
void ce_storage_keep(void) {}

void np_begin(void) { rs ^= (uint32_t)rtc_Time() * 2654435761u ^ (uint32_t)clock(); }
int np_end(void) { return 0; }

/* ------------------------------------------------------------------ the start */
#define BOOT __attribute__((section(".text.boot")))
#define BOOTDATA __attribute__((section(".rodata.boot")))
extern uint8_t ce_vram_lo[], ce_vram_hi[], ce_pshadow_lo[], ce_pshadow_hi[], ce_bssx_lo[], ce_bssx_hi[];
void ce_archive(const char *op1);
void ce_on_stack(void (*f)(void), void *top);
static uint8_t stack[6144]; /* the game's (see archive.S) */
static void game(void) { balatro_main(); }

/* the code's appvars (where each goes), then the others */
#define NCODE 4
static const char BOOTDATA var_names[][9] = {"BALCV0",  "BALCV1",  "BALCP",   "BALCB",   "BALLUT",  "BALFIRE",
                                             "BALBG0",  "BALBG1",  "BALBG2",  "BALED",   "BALART0", "BALART1",
                                             "BALART2", "BALART3", "BALART4", "BALART5"};
#define NVARS (NCODE + 6 + CE_ART_NVARS)
static const char BOOTDATA save_name[9] = "NPbalato";
static const char BOOTDATA msg_missing[] = "Balatro: a file is missing";
static const char BOOTDATA msg_send[] = "Send every Balatro file:";
static const char BOOTDATA msg_files[] = "BALATRO.8xp and BAL*.8xv";

BOOT static void boot_archive(const char *name) {
  char op1[9];
  op1[0] = OS_TYPE_APPVAR;
  for (int i = 0; i < 8; i++) op1[1 + i] = name[i];
  ce_archive(op1);
}

BOOT static void boot_fail(void) {
  os_ClrHomeFull();
  os_PutStrFull(msg_missing);
  os_NewLine();
  os_PutStrFull(msg_send);
  os_NewLine();
  os_PutStrFull(msg_files);
  while (!os_GetCSC()) {}
}

BOOT int main(void) {
  /* the appvars in the archive (where the art is read), then the code put in place */
  const uint8_t *src[NCODE];
  size_t len[NCODE];
  for (unsigned i = 0; i < NVARS; i++) boot_archive(var_names[i]);
  for (unsigned i = 0; i < NVARS; i++) {
    var_t *v = os_GetAppVarData(var_names[i], 0);
    if (!v) {
      boot_fail();
      return 0;
    }
    if (i < NCODE) src[i] = v->data, len[i] = v->size;
  }
  if (len[0] + len[1] != (size_t)(ce_vram_hi - ce_vram_lo) || len[2] != (size_t)(ce_pshadow_hi - ce_pshadow_lo) ||
      len[3] != (size_t)(ce_bssx_hi - ce_bssx_lo)) {
    boot_fail();
    return 0;
  }
  /* the screen at 8 bits a pixel, black while the code fills the half it no longer shows */
  volatile uint16_t *pal = (volatile uint16_t *)0xE30200;
  for (int i = 0; i < 256; i++) pal[i] = 0;
  memset((void *)0xD40000, 0, 320 * 240);
  lcd_UpBase = 0xD40000;
  lcd_Control = 0x927;
  memcpy(ce_vram_lo, src[0], len[0]);
  memcpy(ce_vram_lo + len[0], src[1], len[1]);
  memcpy(ce_pshadow_lo, src[2], len[2]);
  memcpy(ce_bssx_lo, src[3], len[3]);
  int ok = ce_art_init() && ce_bg_init();
  if (ok) {
    ce_set_palette(0, CE_NSTATIC, ce_palette);
    ce_on_stack(game, stack + sizeof stack);
  }
  /* the OS's screen, its pixel shadow cleared */
  memset((void *)0xD40000, 255, 320 * 240 * 2);
  memset((void *)0xD031F6, 0, 8400);
  lcd_Control = 0x92D;
  if (!ok) {
    boot_fail();
    return 0;
  }
  os_ClrHomeFull();
  if (ce_save_pending) boot_archive(save_name); /* (the screen's half with code is gone: names from here) */
  return 0;
}
#endif
