/* The Mega Drive for Retro-Go's screens: gwenesis' machine run a frame at a time, as Retro-Go's
 * gwenesis main does (its 68000 and Z80 line by line, the VDP's interrupts, a line drawn
 * when the frame is), the picture handed line by line to rg_line.
 *
 * The calculator hasn't the RAM for gwenesis' frame buffer, nor in one piece for the 68000's
 * 64 KB of RAM besides the 64 KB of video RAM: the video RAM is in Retro-Go's heap, the RAMs in
 * pages of 4 KB (m68k.h, gwenesis_bus.c), and the picture goes out a line at a time.
 *
 * Based on gwenesis by bzhxx (GPL-3.0-or-later, see LICENSE) and Retro-Go's main for it. */
#include <string.h>
#include "md.h"
#include "m68k.h"
#include "gwenesis_bus.h"
#include "gwenesis_io.h"
#include "gwenesis_vdp.h"
#include "gwenesis_savestate.h"
#include "z80inst.h"

extern unsigned char *VRAM;          /* (gwenesis_vdp_mem.c) */
unsigned char *md_ram[32], *md_zram[4];
extern unsigned char gwenesis_vdp_regs[];
extern unsigned short gwenesis_vdp_status;
extern int screen_width, screen_height, hint_pending;
extern unsigned short CRAM565[];
void gwenesis_m68k_save_state(void), gwenesis_m68k_load_state(void);
void gwenesis_io_save_state(void), gwenesis_io_load_state(void);
void gwenesis_bus_save_state(void), gwenesis_bus_load_state(void);
void gwenesis_vdp_mem_save_state(void), gwenesis_vdp_mem_load_state(void);
void power_on(void), reset_emulation(void);

int system_clock, scan_line; /* (the frame's master clock and line, as gwenesis' main keeps them) */
static bool z80_on = true;

/* ---- the state: the parts gwenesis saves by name, here one after another in a block */
#define MISC_SIZE 2304
static uint8_t *misc;
static uint32_t misc_at, misc_size;
struct SaveState { int unused; };
static SaveState the_state;
SaveState *saveGwenesisStateOpenForRead(const char *name) { (void)name; return &the_state; }
SaveState *saveGwenesisStateOpenForWrite(const char *name) { (void)name; return &the_state; }
static void misc_put(const void *p, uint32_t n) {
  if (misc_at + n <= MISC_SIZE) memcpy(misc + misc_at, p, n);
  misc_at += n;
}
static void misc_get(void *p, uint32_t n) {
  if (misc_at + n <= MISC_SIZE) memcpy(p, misc + misc_at, n);
  else memset(p, 0, n);
  misc_at += n;
}
void saveGwenesisStateSet(SaveState *s, const char *tag, int v) { (void)s, (void)tag; misc_put(&v, 4); }
int saveGwenesisStateGet(SaveState *s, const char *tag) {
  int v;
  (void)s, (void)tag;
  misc_get(&v, 4);
  return v;
}
void saveGwenesisStateSetBuffer(SaveState *s, const char *tag, void *p, int n) { (void)s, (void)tag; misc_put(p, (uint32_t)n); }
void saveGwenesisStateGetBuffer(SaveState *s, const char *tag, void *p, int n) { (void)s, (void)tag; misc_get(p, (uint32_t)n); }

static void save_misc(void) {
  misc_at = 0;
  gwenesis_m68k_save_state();
  gwenesis_io_save_state();
  gwenesis_bus_save_state();
  gwenesis_vdp_mem_save_state();
  gwenesis_z80inst_save_state();
  misc_size = misc_at;
}

static void clear_rams(void) {
  for (int i = 0; i < 32; i++) memset(md_ram[i], 0, MD_PAGE);
  for (int i = 0; i < 4; i++) memset(md_zram[i], 0, MD_PAGE);
}

int md_load(const uint8_t *data, uint32_t size) {
  if (size < 0x200 || size > 0x400000) return RG_ERR_ROM;
  rg_alloc_reset();
  VRAM = rg_alloc(0x10000);
  misc = rg_alloc(MISC_SIZE);
  if (!VRAM || !misc) return RG_ERR_MEMORY;
  for (int i = 0; i < 32; i++)
    if (!(md_ram[i] = rg_alloc_page())) return RG_ERR_MEMORY;
  for (int i = 0; i < 4; i++)
    if (!(md_zram[i] = rg_alloc_page())) return RG_ERR_MEMORY;
  clear_rams();
  load_cartridge((unsigned char *)(uintptr_t)data, size);
  power_on();
  reset_emulation();
  save_misc();
  return RG_OK;
}

void md_reset(bool hard) {
  if (hard) {
    clear_rams();
    memset(VRAM, 0, 0x10000);
    power_on();
    reset_emulation();
  } else {
    m68k_pulse_reset();
    z80_pulse_reset();
  }
}

void md_set_z80(bool on) { z80_on = on; }

void gwenesis_io_get_buttons(void) {} /* (the pad is set before each frame, md_set_pad) */

void md_set_pad(int pad) {
  /* (gwenesis' buttons: up, down, left, right, C, B, A, Start; Retro-Go's A is C, its Select A) */
  static const uint8_t bits[8] = {RG_PAD_UP, RG_PAD_DOWN, RG_PAD_LEFT, RG_PAD_RIGHT,
                                  RG_PAD_A,  RG_PAD_B,    RG_PAD_SELECT, RG_PAD_START};
  for (int i = 0; i < 8; i++) {
    if (pad & bits[i]) gwenesis_io_pad_press_button(0, (unsigned char)i);
    else gwenesis_io_pad_release_button(0, (unsigned char)i);
  }
}

void md_view(int *x, int *y, int *w, int *h) {
  *x = 0, *y = 0;
  *w = REG12_MODE_H40 ? 320 : 256;
  *h = REG1_PAL ? 240 : 224;
}

uint32_t md_frame_us(void) { return REG1_PAL ? 20000 : 16667; }

void md_run_frame(bool draw) {
  int lines_per_frame = REG1_PAL ? LINES_PER_FRAME_PAL : LINES_PER_FRAME_NTSC;
  int hint_counter = gwenesis_vdp_regs[10];
  screen_width = REG12_MODE_H40 ? 320 : 256;
  screen_height = REG1_PAL ? 240 : 224;
  gwenesis_vdp_render_config();
  system_clock = 0;
  zclk = z80_on ? 0 : 0x1000000;
  scan_line = 0;
  while (scan_line < lines_per_frame) {
    m68k_run(system_clock + VDP_CYCLES_PER_LINE);
    z80_run(system_clock + VDP_CYCLES_PER_LINE);
    if (draw && scan_line < screen_height) gwenesis_vdp_render_line(scan_line);
    /* (on these lines the line counter is reloaded) */
    if (scan_line == 0 || scan_line > screen_height) hint_counter = REG10_LINE_COUNTER;
    if (--hint_counter < 0) {
      if (REG0_LINE_INTERRUPT != 0 && scan_line <= screen_height) {
        hint_pending = 1;
        if ((gwenesis_vdp_status & STATUS_VIRQPENDING) == 0) m68k_update_irq(4);
      }
      hint_counter = REG10_LINE_COUNTER;
    }
    scan_line++;
    /* the vertical blank from the end of the last line drawn */
    if (scan_line == screen_height) {
      if (REG1_VBLANK_INTERRUPT != 0) {
        gwenesis_vdp_status |= STATUS_VIRQPENDING;
        m68k_set_irq(6);
      }
      z80_irq_line(1);
    }
    if (scan_line == screen_height + 1) z80_irq_line(0);
    system_clock += VDP_CYCLES_PER_LINE;
  }
  m68k.cycles -= system_clock;
}

/* the picture again from the video RAM as it is now (under the menus) */
void md_redraw(void) {
  screen_width = REG12_MODE_H40 ? 320 : 256;
  screen_height = REG1_PAL ? 240 : 224;
  gwenesis_vdp_render_config();
  for (int y = 0; y < screen_height; y++) gwenesis_vdp_render_line(y);
}

int md_state_chunks(rg_chunk_t *out) {
  save_misc();
  int n = 0;
  out[n++] = (rg_chunk_t){misc, misc_size};
  out[n++] = (rg_chunk_t){VRAM, 0x8000};
  out[n++] = (rg_chunk_t){VRAM + 0x8000, 0x8000};
  for (int i = 0; i < 32; i++) out[n++] = (rg_chunk_t){md_ram[i], MD_PAGE};
  for (int i = 0; i < 4; i++) out[n++] = (rg_chunk_t){md_zram[i], MD_PAGE};
  return n;
}

void md_state_loaded(void) {
  misc_at = 0;
  gwenesis_m68k_load_state();
  gwenesis_io_load_state();
  gwenesis_bus_load_state();
  gwenesis_vdp_mem_load_state();
  gwenesis_z80inst_load_state();
  /* (whatever the state held: an interrupt level the 68000 has, clocks inside a frame) */
  m68k.int_level &= 0x700, m68k.int_mask &= 0x700, m68k.stopped &= 3;
  if (m68k.cycles > 0x100000) m68k.cycles = 0;
  if (m68k.cycle_end > 0x100000) m68k.cycle_end = 0;
  if (zclk < 0 || zclk > 0x1000000) zclk = 0;
}
