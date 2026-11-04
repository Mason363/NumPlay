/*
** Nofrendo (c) 1998-2000 Matthew Conte (matt@conte.com)
**
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
** Any permitted reproduction of these routines, in whole or in part,
** must bear this legend.
**
**
** nes_ppu.h
**
** NES Picture Processing Unit (PPU) emulation header file
** $Id: nes_ppu.h,v 1.2 2001/04/27 14:37:11 neil Exp $
*/

#ifndef _NES_PPU_H_
#define _NES_PPU_H_

#include "noftypes.h"

/* PPU register defines */
#define  PPU_CTRL0            0x2000
#define  PPU_CTRL1            0x2001
#define  PPU_STAT             0x2002
#define  PPU_OAMADDR          0x2003
#define  PPU_OAMDATA          0x2004
#define  PPU_SCROLL           0x2005
#define  PPU_VADDR            0x2006
#define  PPU_VDATA            0x2007

#define  PPU_OAMDMA           0x4014
#define  PPU_JOY0             0x4016
#define  PPU_JOY1             0x4017

/* $2000 */
#define  PPU_CTRL0F_NMI       0x80
#define  PPU_CTRL0F_OBJ16     0x20
#define  PPU_CTRL0F_BGADDR    0x10
#define  PPU_CTRL0F_OBJADDR   0x08
#define  PPU_CTRL0F_ADDRINC   0x04
#define  PPU_CTRL0F_NAMETAB   0x03

/* $2001 */
#define  PPU_CTRL1F_OBJON     0x10
#define  PPU_CTRL1F_BGON      0x08
#define  PPU_CTRL1F_OBJMASK   0x04
#define  PPU_CTRL1F_BGMASK    0x02

/* $2002 */
#define  PPU_STATF_VBLANK     0x80
#define  PPU_STATF_STRIKE     0x40
#define  PPU_STATF_MAXSPRITE  0x20

/* Sprite attribute byte bitmasks */
#define  OAMF_VFLIP           0x80
#define  OAMF_HFLIP           0x40
#define  OAMF_BEHIND          0x20

/* Maximum number of sprites per horizontal scanline */
#define  PPU_MAXSPRITE        8

/* some mappers do *dumb* things */
typedef void (*ppulatchfunc_t)(uint32 address, uint8 value);
typedef void (*ppuvromswitch_t)(uint8 value);

typedef struct ppu_s
{
   /* big nasty memory chunks */
   uint8 nametab[0x1000];
   uint8 oam[256];
   uint8 palette[32];
   uint8 *page[16];

   /* hardware registers */
   uint8 ctrl0, ctrl1, stat, oam_addr;
   uint32 vaddr, vaddr_latch;
   int tile_xofs, flipflop;
   int vaddr_inc;
   uint32 tile_nametab;

   uint8 obj_height;
   uint32 obj_base, bg_base;

   bool bg_on, obj_on;
   bool obj_mask, bg_mask;
   
   uint8 latch, vdata_latch;
   uint8 strobe;

   bool strikeflag;
   uint32 strike_cycle;

   /* callbacks for naughty mappers */
   ppulatchfunc_t latchfunc;
   ppuvromswitch_t vromswitch;

   bool vram_accessible;

   bool vram_present;
} ppu_t;

/* (Retro-Go on NumPlay: in Retro-Go's heap, nes_ppu_alloc, not in the app's RAM: the
   Mega Drive takes it when it runs) */
extern ppu_t *nes_ppu_p;
#define ppu (*nes_ppu_p)
/* the PPU's memory taken from Retro-Go's heap; false if it is full */
bool nes_ppu_alloc(void);
extern bool ppu_pages_changed;
extern bool ppu_limit_sprites;          /* 8 sprites a line at most, as the NES (an option) */
extern const uint16 *nes_palette565;    /* the 64 NES colours in RGB565 */

/* TODO: should use this pointers */
extern void ppu_setlatchfunc(ppulatchfunc_t func);
extern void ppu_setvromswitch(ppuvromswitch_t func);

/* Mirroring */
/* TODO: this is only used bloody once */
extern void ppu_mirrorhipages(void);

extern void ppu_mirror(int nt1, int nt2, int nt3, int nt4);

extern void ppu_setpage(int size, int page_num, uint8 *location);
extern uint8 *ppu_getpage(int page);


/* control */
extern void ppu_reset(int reset_type);
extern bool ppu_enabled(void);
extern void ppu_scanline(int scanline, bool draw_flag);
extern void ppu_endscanline(int scanline);
extern void ppu_checknmi();
extern void ppu_redraw(void);

/* IO */
extern uint8 ppu_read(uint32 address);
extern void ppu_write(uint32 address, uint8 value);
extern uint8 ppu_readhigh(uint32 address);
extern void ppu_writehigh(uint32 address, uint8 value);

#endif /* _NES_PPU_H_ */
