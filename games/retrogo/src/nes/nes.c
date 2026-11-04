/*
** Nofrendo (c) 1998-2000 Matthew Conte (matt@conte.com)
**
** This program is free software; you can redistribute it and/or modify it under the terms of
** the GNU General Public License as published by the Free Software Foundation; either version 3
** of the License, or (at your option) any later version. (Nofrendo was released under version
** 2 of the GNU Library General Public License; as its section 3 allows, this copy, changed for
** Retro-Go on NumPlay, is under the GNU General Public License instead.)
**
** This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
** without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
** See the GNU General Public License for more details.
**
** Any permitted reproduction of these routines, in whole or in part, must bear this legend.
**
** nes.c: the NES machine, from nofrendo's nes.c (its address handlers, frame IRQ and frame of
** 262 scanlines) and nes_rom.c (the iNES header). Changed for Retro-Go on NumPlay: one static
** machine, the cartridge read in place from the calculator's flash, RAM from Retro-Go's block,
** the pads, the palettes, the battery RAM and save states.
*/

#include "noftypes.h"
#include "nes.h"
#include "mmclist.h"
#include "nes_core.h"

#define  NES_CLOCK_DIVIDER    12
#define  NES_MASTER_CLOCK     (236250000 / 11)
#define  NES_SCANLINE_CYCLES  (1364.0f / NES_CLOCK_DIVIDER)
#define  NES_FIQ_PERIOD       (NES_MASTER_CLOCK / NES_CLOCK_DIVIDER / 60)

#define  NES_RAMSIZE          0x800

nes_t nes;
uint8 *nes_rom_lo, *nes_rom_hi;
static uint8 *ram;
static bool sram_dirty;

nes_t *nes_getcontextptr(void)
{
   return &nes;
}

static uint8 ram_read(uint32 address)
{
   return ram[address & (NES_RAMSIZE - 1)];
}

static void ram_write(uint32 address, uint8 value)
{
   ram[address & (NES_RAMSIZE - 1)] = value;
}

/* $6000-$7FFF: the cartridge's RAM, unless its mapper put ROM there */
static void wram_write(uint32 address, uint8 value)
{
   uint8 *p = nes6502_pages()[(address >> 12) & 15] + (address & 0xFFF);
   if (p >= nes_rom_lo && p < nes_rom_hi)
      return;
   if (*p != value)
   {
      *p = value;
      if (p >= nes.rominfo.sram && p < nes.rominfo.sram + 0x2000)
         sram_dirty = true;
   }
}

static void write_protect(uint32 address, uint8 value)
{
   /* don't allow write to go through */
   UNUSED(address);
   UNUSED(value);
}

static uint8 read_protect(uint32 address)
{
   /* don't allow read to go through */
   UNUSED(address);

   return 0xFF;
}

#define  LAST_MEMORY_HANDLER  { -1, -1, NULL }
/* read/write handlers for standard NES */
static const nes6502_memread default_readhandler[] =
{
   { 0x0800, 0x1FFF, ram_read },
   { 0x2000, 0x3FFF, ppu_read },
   { 0x4000, 0x4015, apu_read },
   { 0x4016, 0x4017, ppu_readhigh },
   LAST_MEMORY_HANDLER
};

static const nes6502_memwrite default_writehandler[] =
{
   { 0x0800, 0x1FFF, ram_write },
   { 0x2000, 0x3FFF, ppu_write },
   { 0x4000, 0x4013, apu_write },
   { 0x4015, 0x4015, apu_write },
   { 0x4014, 0x4017, ppu_writehigh },
   LAST_MEMORY_HANDLER
};

/* this big nasty boy sets up the address handlers that the CPU uses */
static void build_address_handlers(void)
{
   int count, num_handlers = 0;
   mapintf_t *intf = mmc.intf;

   memset(nes.readhandler, 0, sizeof(nes.readhandler));
   memset(nes.writehandler, 0, sizeof(nes.writehandler));

   for (count = 0; default_readhandler[count].read_func; count++)
      nes.readhandler[num_handlers++] = default_readhandler[count];
   if (intf->mem_read)
      for (count = 0; intf->mem_read[count].read_func && num_handlers < MAX_MEM_HANDLERS - 2; count++)
      {
         nes.readhandler[num_handlers].min_range = intf->mem_read[count].min_range;
         nes.readhandler[num_handlers].max_range = intf->mem_read[count].max_range;
         nes.readhandler[num_handlers++].read_func = intf->mem_read[count].read_func;
      }
   nes.readhandler[num_handlers++] = (nes6502_memread){ 0x4018, 0x5FFF, read_protect };
   nes.readhandler[num_handlers++] = (nes6502_memread)LAST_MEMORY_HANDLER;

   num_handlers = 0;
   for (count = 0; default_writehandler[count].write_func; count++)
      nes.writehandler[num_handlers++] = default_writehandler[count];
   if (intf->mem_write)
      for (count = 0; intf->mem_write[count].write_func && num_handlers < MAX_MEM_HANDLERS - 4; count++)
      {
         nes.writehandler[num_handlers].min_range = intf->mem_write[count].min_range;
         nes.writehandler[num_handlers].max_range = intf->mem_write[count].max_range;
         nes.writehandler[num_handlers++].write_func = intf->mem_write[count].write_func;
      }
   /* catch-all for bad writes */
   nes.writehandler[num_handlers++] = (nes6502_memwrite){ 0x4018, 0x5FFF, write_protect };
   nes.writehandler[num_handlers++] = (nes6502_memwrite){ 0x6000, 0x7FFF, wram_write };
   nes.writehandler[num_handlers++] = (nes6502_memwrite){ 0x8000, 0xFFFF, write_protect };
   nes.writehandler[num_handlers++] = (nes6502_memwrite)LAST_MEMORY_HANDLER;
}

/* raise an IRQ */
void nes_irq(void)
{
   nes6502_irq();
}

uint8 nes_clearfiq(void)
{
   if (nes.fiq_occurred)
   {
      nes.fiq_occurred = false;
      return 0x40;
   }

   return 0;
}

void nes_setfiq(uint8 value)
{
   nes.fiq_state = value;
   nes.fiq_cycles = (int) NES_FIQ_PERIOD;
}

static void nes_checkfiq(int cycles)
{
   nes.fiq_cycles -= cycles;
   if (nes.fiq_cycles <= 0)
   {
      nes.fiq_cycles += (int) NES_FIQ_PERIOD;
      if (0 == (nes.fiq_state & 0xC0))
      {
         nes.fiq_occurred = true;
         nes6502_irq();
      }
   }
}

void nes_nmi(void)
{
   nes6502_nmi();
}

static void nes_renderframe(bool draw_flag)
{
   int elapsed_cycles;
   mapintf_t *mapintf = mmc.intf;
   int in_vblank = 0;

   while (262 != nes.scanline)
   {
      ppu_scanline(nes.scanline, draw_flag);

      if (241 == nes.scanline)
      {
         /* 7-9 cycle delay between when VINT flag goes up and NMI is taken */
         elapsed_cycles = nes6502_execute(7);
         nes.scanline_cycles -= elapsed_cycles;
         nes_checkfiq(elapsed_cycles);

         ppu_checknmi();

         if (mapintf->vblank)
            mapintf->vblank();
         in_vblank = 1;
      }

      if (mapintf->hblank)
         mapintf->hblank(in_vblank);

      nes.scanline_cycles += (float) NES_SCANLINE_CYCLES;
      elapsed_cycles = nes6502_execute((int) nes.scanline_cycles);
      nes.scanline_cycles -= (float) elapsed_cycles;
      nes_checkfiq(elapsed_cycles);

      ppu_endscanline(nes.scanline);
      nes.scanline++;
   }

   nes.scanline = 0;
}

/* Reset NES hardware */
void nes_reset(int reset_type)
{
   if (HARD_RESET == reset_type)
   {
      memset(ram, 0, NES_RAMSIZE);
      if (nes.rominfo.vram)
         memset(nes.rominfo.vram, 0, 0x2000);
   }

   apu_reset();
   ppu_reset(reset_type);
   mmc_reset();
   nes6502_reset();

   nes.scanline = 241;
   nes.scanline_cycles = 0;
   nes.fiq_cycles = (int) NES_FIQ_PERIOD;
}

/* ------------------------------------------------------------ the pads */
static uint8 pad_now, pad_shift[2];

void input_strobe(void)
{
   pad_shift[0] = pad_now;
   pad_shift[1] = 0;
}

uint8 input_get(int port)
{
   int p = (port & INP_JOYPAD1) ? 1 : 0;
   uint8 bit = pad_shift[p] & 1;
   pad_shift[p] = (uint8) (pad_shift[p] >> 1 | 0x80); /* (1s once the 8 buttons are read) */
   return 0x40 | bit;
}

void nes_set_pad(int rg_pad)
{
   /* the order the NES reads them in is Retro-Go's: A B Select Start Up Down Left Right */
   pad_now = (uint8) rg_pad;
}

/* ------------------------------------------------------------ the palettes */
/* nofrendo's own (its nes_pal.c generator: hue 334, tint 0.4), and the 2C02's composite signal
   decoded (games/retrogo/tools/nespal.py makes both) */
static const uint16 palettes[NES_PAL_COUNT][64] = {
   {
   0x8410, 0x12B3, 0x41D6, 0x7114, 0x98B0, 0xB0AA, 0xA103, 0x81E0,
   0x52C0, 0x1B80, 0x0402, 0x03E8, 0x038E, 0x0000, 0x0020, 0x0020,
   0xC618, 0x3C19, 0x6B1B, 0xA259, 0xC9F5, 0xD9EF, 0xCA68, 0xAB23,
   0x7C01, 0x44C2, 0x1D47, 0x0D4D, 0x14D4, 0x39E7, 0x0841, 0x0841,
   0xFFFF, 0x865F, 0xB55F, 0xE49F, 0xFC3E, 0xFC38, 0xFCB1, 0xF56C,
   0xC64A, 0x8F0B, 0x6790, 0x5796, 0x5F1C, 0x7BCF, 0x0861, 0x0861,
   0xFFFF, 0xAF9F, 0xDEBF, 0xFDFF, 0xFD9F, 0xFD9D, 0xFDF7, 0xFED2,
   0xEFB0, 0xBFF1, 0x97F5, 0x87FB, 0x8FFF, 0xC638, 0x1082, 0x1082
   },
   {
   0x630C, 0x00F1, 0x2073, 0x4010, 0x580B, 0x6004, 0x5880, 0x4140,
   0x21C0, 0x0240, 0x0260, 0x0225, 0x01AC, 0x0000, 0x0000, 0x0000,
   0xAD55, 0x2A7C, 0x59BE, 0x811B, 0xA0F4, 0xA94A, 0xA1E0, 0x7AC0,
   0x53A0, 0x2440, 0x0461, 0x040B, 0x0B75, 0x0000, 0x0000, 0x0000,
   0xFFFF, 0x7D1F, 0xAC3F, 0xD39F, 0xFB7E, 0xFBD4, 0xF469, 0xD562,
   0xA640, 0x76C3, 0x56EB, 0x4EB5, 0x5DFF, 0x4A69, 0x0000, 0x0000,
   0xFFFF, 0xCEDF, 0xDE7F, 0xEE3F, 0xFE3F, 0xFE5B, 0xFE97, 0xEEF4,
   0xDF53, 0xCF94, 0xBF97, 0xB77B, 0xBF3F, 0xBDD7, 0x0000, 0x0000
   },
};

void nes_set_palette(int pal)
{
   nes_palette565 = palettes[pal >= 0 && pal < NES_PAL_COUNT ? pal : 0];
}

void nes_set_sprite_limit(bool on)
{
   ppu_limit_sprites = on;
}

/* ------------------------------------------------------------ the cartridge */
#define  ROM_INES_MAGIC    "NES\x1A"

int nes_load(const uint8_t *data, uint32_t size)
{
   rominfo_t *r = &nes.rominfo;

   if (!data || size < 16 + 0x4000 || memcmp(data, ROM_INES_MAGIC, 4))
      return RG_ERR_ROM;
   memset(&nes, 0, sizeof nes);
   if (!nes_palette565)
      nes_set_palette(NES_PAL_NTSC);

   /* the iNES header (nofrendo's rom_getheader) */
   uint8 flags6 = data[6], flags7 = data[7];
   r->rom_banks = data[4];
   r->vrom_banks = data[5];
   r->sram_banks = 8; /* 1kB banks, so 8KB */
   r->vram_banks = 1; /* 8kB banks, so 8KB */
   r->mirror = (flags6 & 0x01) ? MIRROR_VERT : MIRROR_HORIZ;
   r->flags = 0;
   if (flags6 & 0x02)
      r->flags |= ROM_FLAG_BATTERY;
   if (flags6 & 0x04)
      r->flags |= ROM_FLAG_TRAINER;
   if (flags6 & 0x08)
      r->flags |= ROM_FLAG_FOURSCREEN;
   r->mapper_number = flags6 >> 4;
   /* the high nybble only from a clean header (or NES 2.0's); `DiskDude!' and other tags wrote
      over it */
   static const uint8 zero[8];
   if (!memcmp(data + 8, zero, 8) || (flags7 & 0x0C) == 0x08)
      r->mapper_number |= (flags7 & 0xF0);
   if (99 == r->mapper_number)
      r->flags |= ROM_FLAG_VERSUS;

   uint32 at = 16 + ((r->flags & ROM_FLAG_TRAINER) ? 512 : 0);
   if (!r->rom_banks || at + (uint32) r->rom_banks * 0x4000 > size)
      return RG_ERR_ROM;
   if (!mmc_init(r))
      return RG_ERR_MAPPER;
   /* (a ROM file cut short keeps the CHR banks it has) */
   uint32 chr_at = at + (uint32) r->rom_banks * 0x4000;
   if (chr_at + (uint32) r->vrom_banks * 0x2000 > size)
      r->vrom_banks = (int) ((size - chr_at) / 0x2000);
   r->rom = (uint8 *) data + at;
   r->vrom = r->vrom_banks ? (uint8 *) data + chr_at : NULL;
   nes_rom_lo = (uint8 *) data;
   nes_rom_hi = (uint8 *) data + size;

   rg_alloc_reset();
   ram = rg_alloc(NES_RAMSIZE);
   r->sram = rg_alloc(0x2000);
   r->vram = r->vrom_banks ? NULL : rg_alloc(0x2000);
   nes6502_null_page = rg_alloc(NES6502_BANKSIZE);
   if (!ram || !r->sram || (!r->vrom_banks && !r->vram) || !nes6502_null_page || !nes_ppu_alloc())
      return RG_ERR_MEMORY;
   if (r->flags & ROM_FLAG_TRAINER)
      memcpy(r->sram + 0x1000, data + 16, 512);

   /* the CPU's memory: RAM, the cartridge's RAM at $6000-$7FFF, the rest unmapped */
   nes6502_context cpu;
   memset(&cpu, 0, sizeof cpu);
   cpu.mem_page[0] = ram;
   cpu.mem_page[6] = r->sram;
   cpu.mem_page[7] = r->sram + 0x1000;
   cpu.read_handler = nes.readhandler;
   cpu.write_handler = nes.writehandler;
   build_address_handlers();
   nes6502_setcontext(&cpu);
   memset(&ppu, 0, sizeof ppu);
   ppu.vram_present = r->vram != NULL;
   nes_reset(HARD_RESET);
   sram_dirty = false;
   return RG_OK;
}

void nes_reset_machine(bool hard)
{
   nes_reset(hard ? HARD_RESET : SOFT_RESET);
}

void nes_run_frame(bool draw)
{
   rg_drawing = draw;
   nes_renderframe(draw);
   apu_halfframe();
   apu_halfframe();
}

void nes_redraw(void)
{
   ppu_redraw();
}

int nes_mapper(void)
{
   return nes.rominfo.mapper_number;
}

/* ------------------------------------------------------------ battery RAM */
uint8_t *nes_sram(uint32_t *size)
{
   *size = 0x2000;
   return (nes.rominfo.flags & ROM_FLAG_BATTERY) ? nes.rominfo.sram : NULL;
}

bool nes_sram_dirty(void)
{
   return (nes.rominfo.flags & ROM_FLAG_BATTERY) && sram_dirty;
}

void nes_sram_saved(void)
{
   sram_dirty = false;
}

/* ------------------------------------------------------------ save states */
/* A memory page as a save state keeps it: which memory, and where in it (the pointers
   themselves change from one build to the next). */
enum { AREA_NULL, AREA_PRG, AREA_CHR, AREA_SRAM, AREA_VRAM, AREA_RAM, AREA_NAMETAB, AREA_COUNT };
typedef struct { uint8 area; uint32 offset; } page_ref_t;

static struct
{
   uint32 pc;
   uint8 a, p, x, y, s, jammed, int_pending, int_latency;
   int32 total_cycles, burn_cycles;
   page_ref_t cpu_pages[NES6502_NUMBANKS], ppu_pages[16];
   uint8 fiq_occurred;
   uint8 fiq_state, pad_shift[2];
   int fiq_cycles, scanline;
   float scanline_cycles;
   SnssMapperBlock mapper;
} snap;

static uint8 *area_base(int area, uint32 *size)
{
   rominfo_t *r = &nes.rominfo;
   switch (area)
   {
   case AREA_PRG: *size = (uint32) r->rom_banks * 0x4000; return r->rom;
   case AREA_CHR: *size = (uint32) r->vrom_banks * 0x2000; return r->vrom;
   case AREA_SRAM: *size = 0x2000; return r->sram;
   case AREA_VRAM: *size = r->vram ? 0x2000 : 0; return r->vram;
   case AREA_RAM: *size = NES_RAMSIZE; return ram;
   case AREA_NAMETAB: *size = sizeof ppu.nametab; return ppu.nametab;
   default: *size = 0; return NULL;
   }
}

static page_ref_t ref_of(const uint8 *p)
{
   for (int a = AREA_PRG; a < AREA_COUNT; a++)
   {
      uint32 size;
      uint8 *base = area_base(a, &size);
      if (base && p >= base && p < base + size)
         return (page_ref_t){ (uint8) a, (uint32) (p - base) };
   }
   return (page_ref_t){ AREA_NULL, 0 };
}

/* the memory a page refers to, if that much of it is there (NULL otherwise) */
static uint8 *page_of(page_ref_t r, uint32 length)
{
   uint32 size;
   uint8 *base = area_base(r.area, &size);
   if (!base || r.offset > size || size - r.offset < length)
      return NULL;
   return base + r.offset;
}

int nes_state_chunks(rg_chunk_t *out)
{
   nes6502_context cpu;
   nes6502_getcontext(&cpu);
   snap.pc = cpu.pc_reg, snap.a = cpu.a_reg, snap.p = cpu.p_reg, snap.x = cpu.x_reg;
   snap.y = cpu.y_reg, snap.s = cpu.s_reg, snap.jammed = cpu.jammed;
   snap.int_pending = cpu.int_pending, snap.int_latency = cpu.int_latency;
   snap.total_cycles = cpu.total_cycles, snap.burn_cycles = cpu.burn_cycles;
   for (int i = 0; i < NES6502_NUMBANKS; i++)
      snap.cpu_pages[i] = cpu.mem_page[i] ? ref_of(cpu.mem_page[i]) : (page_ref_t){ AREA_NULL, 0 };
   for (int i = 0; i < 16; i++)
      snap.ppu_pages[i] = ref_of(ppu.page[i] + (i << 10));
   snap.fiq_occurred = nes.fiq_occurred, snap.fiq_state = nes.fiq_state;
   snap.fiq_cycles = nes.fiq_cycles, snap.scanline = nes.scanline;
   snap.scanline_cycles = nes.scanline_cycles;
   snap.pad_shift[0] = pad_shift[0], snap.pad_shift[1] = pad_shift[1];
   memset(&snap.mapper, 0, sizeof snap.mapper);
   if (mmc.intf->get_state)
      mmc.intf->get_state(&snap.mapper);

   int n = 0;
   out[n++] = (rg_chunk_t){ &snap, sizeof snap };
   out[n++] = (rg_chunk_t){ &ppu, sizeof ppu };
   out[n++] = (rg_chunk_t){ &apu, sizeof apu };
   out[n++] = (rg_chunk_t){ ram, NES_RAMSIZE };
   out[n++] = (rg_chunk_t){ nes.rominfo.sram, 0x2000 };
   if (nes.rominfo.vram)
      out[n++] = (rg_chunk_t){ nes.rominfo.vram, 0x2000 };
   return n;
}

/* whatever the state held, a machine this cartridge can be */
void nes_state_loaded(void)
{
   ppulatchfunc_t latch = NULL;
   ppuvromswitch_t vrom = NULL;
   /* (the mapper's callbacks are its own, not the state's) */
   mmc_reset();
   latch = ppu.latchfunc;
   vrom = ppu.vromswitch;

   nes6502_context cpu;
   nes6502_getcontext(&cpu);
   cpu.pc_reg = snap.pc & 0xFFFF, cpu.a_reg = snap.a, cpu.p_reg = snap.p, cpu.x_reg = snap.x;
   cpu.y_reg = snap.y, cpu.s_reg = snap.s, cpu.jammed = snap.jammed != 0;
   cpu.int_pending = snap.int_pending & 1, cpu.int_latency = snap.int_latency;
   cpu.total_cycles = snap.total_cycles;
   cpu.burn_cycles = snap.burn_cycles >= 0 && snap.burn_cycles < 1000 ? snap.burn_cycles : 0;
   cpu.mem_page[0] = ram;
   for (int i = 6; i < NES6502_NUMBANKS; i++)
   {
      uint8 *p = page_of(snap.cpu_pages[i], NES6502_BANKSIZE);
      cpu.mem_page[i] = p ? p : (i < 8 ? nes.rominfo.sram + (i - 6) * 0x1000 : NULL);
   }
   if (!cpu.mem_page[8]) /* (never left without code) */
      for (int i = 8; i < 16; i++)
         cpu.mem_page[i] = nes.rominfo.rom + ((i - 8) * 0x1000) % ((uint32) nes.rominfo.rom_banks * 0x4000);
   cpu.read_handler = nes.readhandler;
   cpu.write_handler = nes.writehandler;
   nes6502_setcontext(&cpu);

   for (int i = 0; i < 16; i++)
   {
      uint8 *p = page_of(snap.ppu_pages[i], 0x400);
      if (!p)
         p = i < 8 ? (nes.rominfo.vrom ? nes.rominfo.vrom : nes.rominfo.vram) + (i << 10)
                   : ppu.nametab + ((i & 3) << 10);
      ppu.page[i] = p - (i << 10);
   }
   ppu.latchfunc = latch;
   ppu_pages_changed = true;
   ppu.vromswitch = vrom;
   ppu.vram_present = nes.rominfo.vram != NULL;
   /* the registers' derived values, from the registers */
   /* (flags read back as bytes: any byte a true or false) */
   bool *flags[] = { &ppu.bg_on, &ppu.obj_on, &ppu.obj_mask, &ppu.bg_mask, &ppu.strikeflag, &ppu.vram_accessible };
   for (unsigned k = 0; k < sizeof flags / sizeof flags[0]; k++)
   {
      uint8 v;
      memcpy(&v, flags[k], 1);
      *flags[k] = v != 0;
   }
   ppu_write(PPU_CTRL0, ppu.ctrl0);
   ppu_write(PPU_CTRL1, ppu.ctrl1);
   ppu.vaddr &= 0x3FFF;
   ppu.vaddr_latch &= 0x7FFF;
   ppu.tile_xofs &= 7;
   ppu.flipflop &= 1;

   nes.fiq_occurred = snap.fiq_occurred != 0, nes.fiq_state = snap.fiq_state;
   nes.fiq_cycles = snap.fiq_cycles > 0 && snap.fiq_cycles <= (int) NES_FIQ_PERIOD ? snap.fiq_cycles : (int) NES_FIQ_PERIOD;
   nes.scanline = snap.scanline >= 0 && snap.scanline < 262 ? snap.scanline : 0;
   nes.scanline_cycles = snap.scanline_cycles > -1000.0f && snap.scanline_cycles < 1000.0f ? snap.scanline_cycles : 0;
   pad_shift[0] = snap.pad_shift[0], pad_shift[1] = snap.pad_shift[1];
   if (mmc.intf->set_state)
      mmc.intf->set_state(&snap.mapper);
   sram_dirty = true;
}
