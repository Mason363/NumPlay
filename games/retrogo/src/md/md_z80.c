/* The Mega Drive's Z80: NumPlay's (superzazu's z80, ../sms/z80.c, MIT License), built a second
 * time with the Mega Drive's memory, behind the calls gwenesis makes to its own Z80 (z80inst.h).
 * Its 8 KB of RAM (twice, 0000-3FFF), the sound chips (read as ready: no sound here), the bank
 * register (6000) and a 32 KB window into the 68000's memory (8000-FFFF).
 *
 * The control of the Z80 (bus request, reset) and the timing are gwenesis' z80inst.c's
 * (GPL-3.0-or-later, see LICENSE). */
#include <stdint.h>
#include <string.h>

/* ---- the Z80 itself, under other names */
uint8_t md_z80_read(uint16_t addr);
#define Z80_READ(a) md_z80_read(a)
#define z80_rmap md_z80_rmap
#define z80_bus_write md_z80_bus_write
#define z80_bus_in md_z80_bus_in
#define z80_bus_out md_z80_bus_out
#define z80_init md_z80_init
#define z80_run md_z80_exec
#define z80_gen_nmi md_z80_gen_nmi
#define z80_fix md_z80_fix
#define exec_opcode md_exec_opcode
#define exec_opcode_ddfd md_exec_opcode_ddfd
#define exec_opcode_cb md_exec_opcode_cb
#define exec_opcode_dcb md_exec_opcode_dcb
#define exec_opcode_ed md_exec_opcode_ed
#include "../sms/z80.c"
#undef z80_run
#undef z80_init
#undef z80_fix

#include "m68k.h"
#include "z80inst.h"
#include "gwenesis_bus.h"
#include "gwenesis_savestate.h"

extern unsigned char *md_zram[4];
#define ZRAM_AT(a) (md_zram[((a) >> 11) & 3][(a) & 0x7FF])

/* the machine around it, saved with it */
static struct {
  z80 cpu;
  int32_t bus_ack, reset, reset_once, bank;
} zs;
int zclk;

uint8_t md_z80_read(uint16_t a) {
  if (a < 0x4000) return ZRAM_AT(a);
  if (a < 0x6000) return 0; /* (the YM2612's status: ready) */
  if (a < 0x8000) return 0xFF;
  return (uint8_t)m68k_read_memory_8(((uint32_t)zs.bank << 15) | (a & 0x7FFF));
}
void md_z80_bus_write(uint16_t a, uint8_t v) {
  if (a < 0x4000) ZRAM_AT(a) = v;
  else if (a == 0x6000) zs.bank = ((zs.bank >> 1) | (v & 1) << 8) & 0x1FF;
  else if (a >= 0x8000) m68k_write_memory_8(((uint32_t)zs.bank << 15) | (a & 0x7FFF), v);
  /* (the YM2612 at 4000-5FFF and the PSG at 7F11: no sound) */
}
uint8_t md_z80_bus_in(uint8_t port) { (void)port; return 0xFF; }
void md_z80_bus_out(uint8_t port, uint8_t val) { (void)port, (void)val; }

void z80_start(void) {
  md_z80_init(&zs.cpu);
  zs.reset = 1, zs.reset_once = 0, zs.bus_ack = 0, zclk = 0;
}
void z80_pulse_reset(void) {
  uint8_t line = zs.cpu.irq_line;
  md_z80_init(&zs.cpu);
  zs.cpu.irq_line = line;
}
void z80_set_memory(unsigned char *buffer) { (void)buffer; }

/* runs the Z80 up to the master clock `target` (it runs at a 14th of it) */
void z80_run(int target) {
  if (zclk >= target) return;
  int slice = target - zclk;
  if (zs.reset_once == 1 && zs.bus_ack == 0 && zs.reset == 0) {
    uint32_t n = (uint32_t)(slice / Z80_FREQ_DIVISOR);
    zs.cpu.cyc = 0;
    md_z80_exec(&zs.cpu, n);
    zclk = target + (int)(zs.cpu.cyc - n) * Z80_FREQ_DIVISOR;
  } else {
    zclk = target;
  }
}
static void z80_sync(void) { z80_run(m68k_cycles_master()); }

void z80_write_ctrl(unsigned int address, unsigned int value) {
  z80_sync();
  if (address == 0x1100) { /* BUSREQ */
    zs.bus_ack = value ? 1 : 0;
  } else if (address == 0x1200) { /* RESET */
    if (value == 0) {
      zs.reset = 1;
    } else {
      z80_pulse_reset();
      zs.reset = 0, zs.reset_once = 1;
    }
  }
}
unsigned int z80_read_ctrl(unsigned int address) {
  z80_sync();
  if (address == 0x1100) return zs.bus_ack == 1 ? 0 : 1;
  if (address == 0x1200) return (unsigned)zs.reset;
  if (address == 0x1101 || address == 0x1201) return 0;
  return 0xFF;
}
void z80_irq_line(unsigned int value) {
  if (zs.reset_once == 0) return;
  zs.cpu.irq_line = value ? 1 : 0;
}

void gwenesis_z80inst_save_state(void) {
  SaveState *s = saveGwenesisStateOpenForWrite("z80");
  saveGwenesisStateSetBuffer(s, "z80", &zs, sizeof zs);
  saveGwenesisStateSet(s, "zclk", zclk);
}
void gwenesis_z80inst_load_state(void) {
  SaveState *s = saveGwenesisStateOpenForRead("z80");
  saveGwenesisStateGetBuffer(s, "z80", &zs, sizeof zs);
  zclk = saveGwenesisStateGet(s, "zclk");
  md_z80_fix(&zs.cpu);
  zs.bank &= 0x1FF;
  zs.bus_ack = zs.bus_ack != 0, zs.reset = zs.reset != 0, zs.reset_once = zs.reset_once != 0;
}
