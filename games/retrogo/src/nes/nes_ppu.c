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
** nes_ppu.c
**
** NES PPU emulation
** $Id: nes_ppu.c,v 1.2 2001/04/27 14:37:11 neil Exp $
*/

#include <string.h>
#include <stdlib.h>
#include "noftypes.h"
#include "nes_ppu.h"
#include "nes.h"
#include "nes6502.h"
#include "nes_mmc.h"

#include "../rg.h"


/* PPU access */
#define  PPU_MEM(x)           ppu.page[((x) >> 10) & 15][(x)]

/* Background (color 0) and solid sprite pixel flags */
#define  BG_TRANS             0x80
#define  SP_PIXEL             0x40
#define  BG_CLEAR(V)          ((V) & BG_TRANS)
#define  BG_SOLID(V)          (0 == BG_CLEAR(V))
#define  SP_CLEAR(V)          (0 == ((V) & SP_PIXEL))

/* Full BG color */
#define  FULLBG               (ppu.palette[0] | BG_TRANS)

/* the NES PPU */
ppu_t *nes_ppu_p;
bool ppu_limit_sprites = true;
bool ppu_pages_changed = true; /* (since the last line drawn: for the lines kept for the menus) */

/* a write to the PPU's memory: not into the cartridge's CHR ROM, read in place */
static void ppu_poke(uint32 address, uint8 value)
{
   uint8 *p = &PPU_MEM(address & 0x3FFF);
   if (p >= nes_rom_lo && p < nes_rom_hi)
      return;
   *p = value;
}


void ppu_setpage(int size, int page_num, uint8 *location)
{
   ppu_pages_changed = true;
   /* deliberately fall through */
   switch (size)
   {
   case 8:  
      ppu.page[page_num++] = location;
      ppu.page[page_num++] = location;
      ppu.page[page_num++] = location;
      ppu.page[page_num++] = location;
   case 4:  
      ppu.page[page_num++] = location;
      ppu.page[page_num++] = location;
   case 2:
      ppu.page[page_num++] = location;
   case 1:
      ppu.page[page_num++] = location;
      break;
   }
}

/* make sure $3000-$3F00 mirrors $2000-$2F00 */
void ppu_mirrorhipages(void)
{
   ppu_pages_changed = true;
   ppu.page[12] = ppu.page[8] - 0x1000;
   ppu.page[13] = ppu.page[9] - 0x1000;
   ppu.page[14] = ppu.page[10] - 0x1000;
   ppu.page[15] = ppu.page[11] - 0x1000;
}

void ppu_mirror(int nt1, int nt2, int nt3, int nt4)
{
   ppu_pages_changed = true;
   ppu.page[8] = ppu.nametab + (nt1 << 10) - 0x2000;
   ppu.page[9] = ppu.nametab + (nt2 << 10) - 0x2400;
   ppu.page[10] = ppu.nametab + (nt3 << 10) - 0x2800;
   ppu.page[11] = ppu.nametab + (nt4 << 10) - 0x2C00;
   ppu.page[12] = ppu.page[8] - 0x1000;
   ppu.page[13] = ppu.page[9] - 0x1000;
   ppu.page[14] = ppu.page[10] - 0x1000;
   ppu.page[15] = ppu.page[11] - 0x1000;
}

/* bleh, for snss */
uint8 *ppu_getpage(int page)
{
   return ppu.page[page];
}


/* reset state of ppu */
void ppu_reset(int reset_type)
{
   if (HARD_RESET == reset_type)
      memset(ppu.oam, 0xFF, 256); /* (sprites off screen) */

   ppu.ctrl0 = 0;
   ppu.ctrl1 = PPU_CTRL1F_OBJON | PPU_CTRL1F_BGON;
   ppu.stat = 0;
   ppu.flipflop = 0;
   ppu.vaddr = ppu.vaddr_latch = 0x2000;
   ppu.oam_addr = 0;
   ppu.tile_xofs = 0;

   ppu.latch = 0;
   ppu.vram_accessible = true;
}

/* we render a scanline of graphics first so we know exactly
** where the sprite 0 strike is going to occur (in terms of
** cpu cycles), using the relation that 3 pixels == 1 cpu cycle
*/
static void ppu_setstrike(int x_loc)
{
   if (false == ppu.strikeflag)
   {
      ppu.strikeflag = true;

      /* 3 pixels per cpu cycle */
      ppu.strike_cycle = nes6502_getcycles(false) + (x_loc / 3);
   }
}

static void ppu_oamdma(uint8 value)
{
   uint32 cpu_address;
   uint8 oam_loc;

   cpu_address = (uint32) (value << 8);

   /* Sprite DMA starts at the current SPRRAM address */
   oam_loc = ppu.oam_addr;
   do
   {
      ppu.oam[oam_loc++] = nes6502_getbyte(cpu_address++);
   }
   while (oam_loc != ppu.oam_addr);

   /* TODO: enough with houdini */
   cpu_address -= 256;
   /* Odd address in $2003 */
   if ((ppu.oam_addr >> 2) & 1)
   {
      for (oam_loc = 4; oam_loc < 8; oam_loc++)
         ppu.oam[oam_loc] = nes6502_getbyte(cpu_address++);
      cpu_address += 248;
      for (oam_loc = 0; oam_loc < 4; oam_loc++)
         ppu.oam[oam_loc] = nes6502_getbyte(cpu_address++);
   }
   /* Even address in $2003 */
   else
   {
      for (oam_loc = 0; oam_loc < 8; oam_loc++)
         ppu.oam[oam_loc] = nes6502_getbyte(cpu_address++);
   }

   /* make the CPU spin for DMA cycles */
   nes6502_burn(513);
   nes6502_release();
}

/* TODO: this isn't the PPU! */
void ppu_writehigh(uint32 address, uint8 value)
{
   switch (address)
   {
   case PPU_OAMDMA:
      ppu_oamdma(value);
      break;

   case PPU_JOY0:
      /* VS system VROM switching - bleh!*/
      if (ppu.vromswitch)
         ppu.vromswitch(value);

      /* see if we need to strobe them joypads */
      value &= 1;
      
      if (0 == value && ppu.strobe)
         input_strobe();

      ppu.strobe = value;
      break;

   case PPU_JOY1: /* frame IRQ control */
      nes_setfiq(value);
      break;

   default:
      break;
   }
}

/* TODO: this isn't the PPU! */
uint8 ppu_readhigh(uint32 address)
{
   uint8 value;

   switch (address)
   {
   case PPU_JOY0:
      value = input_get(INP_JOYPAD0);
      break;

   case PPU_JOY1:
      /* TODO: better input handling */
      value = input_get(INP_ZAPPER | INP_JOYPAD1 
                        /*| INP_ARKANOID*/ 
                        /*| INP_POWERPAD*/);
      break;

   default:
      value = 0xFF;
      break;
   }

   return value;
}

/* Read from $2000-$2007 */
uint8 ppu_read(uint32 address)
{
   uint8 value;
   
   /* handle mirrored reads up to $3FFF */
   switch (address & 0x2007)
   {
   case PPU_STAT:
      value = (ppu.stat & 0xE0) | (ppu.latch & 0x1F);

      if (ppu.strikeflag)
      {
         if (nes6502_getcycles(false) >= ppu.strike_cycle)
            value |= PPU_STATF_STRIKE;
      }

      /* clear both vblank flag and vram address flipflop */
      ppu.stat &= ~PPU_STATF_VBLANK;
      ppu.flipflop = 0;
      break;

   case PPU_VDATA:
      /* buffered VRAM reads */
      value = ppu.latch = ppu.vdata_latch;

      /* VRAM only accessible during VBL */
      if ((ppu.bg_on || ppu.obj_on) && !ppu.vram_accessible)
      {
         ppu.vdata_latch = 0xFF;
      }
      else
      {
         uint32 addr = ppu.vaddr;
         if (addr >= 0x3000)
            addr -= 0x1000;
         ppu.vdata_latch = PPU_MEM(addr);
      }

      ppu.vaddr += ppu.vaddr_inc;
      ppu.vaddr &= 0x3FFF;
      break;

   case PPU_OAMDATA:
   case PPU_CTRL0:
   case PPU_CTRL1:
   case PPU_OAMADDR:
   case PPU_SCROLL:
   case PPU_VADDR:
   default:
      value = ppu.latch;
      break;
   }

   return value;
}

/* Write to $2000-$2007 */
void ppu_write(uint32 address, uint8 value)
{
   /* write goes into ppu latch... */
   ppu.latch = value;
   
   switch (address & 0x2007)
   {
   case PPU_CTRL0:
      ppu.ctrl0 = value;

      ppu.obj_height = (value & PPU_CTRL0F_OBJ16) ? 16 : 8;
      ppu.bg_base = (value & PPU_CTRL0F_BGADDR) ? 0x1000 : 0;
      ppu.obj_base = (value & PPU_CTRL0F_OBJADDR) ? 0x1000 : 0;
      ppu.vaddr_inc = (value & PPU_CTRL0F_ADDRINC) ? 32 : 1;
      ppu.tile_nametab = value & PPU_CTRL0F_NAMETAB;      

      /* Mask out bits 10 & 11 in the ppu latch */
      ppu.vaddr_latch &= ~0x0C00;
      ppu.vaddr_latch |= ((value & 3) << 10);
      break;

   case PPU_CTRL1:
      ppu.ctrl1 = value;

      ppu.obj_on = (value & PPU_CTRL1F_OBJON) ? true : false;
      ppu.bg_on = (value & PPU_CTRL1F_BGON) ? true : false;
      ppu.obj_mask = (value & PPU_CTRL1F_OBJMASK) ? false : true;
      ppu.bg_mask = (value & PPU_CTRL1F_BGMASK) ? false : true;
      break;

   case PPU_OAMADDR:
      ppu.oam_addr = value;
      break;

   case PPU_OAMDATA:
      ppu.oam[ppu.oam_addr++] = value;
      break;

   case PPU_SCROLL:
      if (0 == ppu.flipflop)
      {
         /* Mask out bits 4 - 0 in the ppu latch */
         ppu.vaddr_latch &= ~0x001F;
         ppu.vaddr_latch |= (value >> 3);    /* Tile number */
         ppu.tile_xofs = (value & 7);  /* Tile offset (0-7 pix) */
      }
      else
      {
         /* Mask out bits 14-12 and 9-5 in the ppu latch */
         ppu.vaddr_latch &= ~0x73E0;
         ppu.vaddr_latch |= ((value & 0xF8) << 2);   /* Tile number */
         ppu.vaddr_latch |= ((value & 7) << 12);     /* Tile offset (0-7 pix) */
      }

      ppu.flipflop ^= 1;

      break;

   case PPU_VADDR:
      if (0 == ppu.flipflop)
      {
         /* Mask out bits 15-8 in ppu latch */
         ppu.vaddr_latch &= ~0xFF00;
         ppu.vaddr_latch |= ((value & 0x3F) << 8);
      }
      else
      {
         /* Mask out bits 7-0 in ppu latch */
         ppu.vaddr_latch &= ~0x00FF;
         ppu.vaddr_latch |= value;
         ppu.vaddr = ppu.vaddr_latch;
      }
      
      ppu.flipflop ^= 1;

      break;

   case PPU_VDATA:
      if (ppu.vaddr < 0x3F00)
      {
         /* VRAM only accessible during scanlines 241-260 */
         if ((ppu.bg_on || ppu.obj_on) && !ppu.vram_accessible)
         {
            ppu_poke(ppu.vaddr, 0xFF); /* corrupt */
         }
         else 
         {
            uint32 addr = ppu.vaddr;

            if (false == ppu.vram_present && addr >= 0x3000)
               ppu.vaddr -= 0x1000;

            ppu_poke(addr, value);
         }
      }
      else
      {
         if (0 == (ppu.vaddr & 0x0F))
         {
            int i;

            for (i = 0; i < 8; i ++)
               ppu.palette[i << 2] = (value & 0x3F) | BG_TRANS;
         }
         else if (ppu.vaddr & 3)
         {
            ppu.palette[ppu.vaddr & 0x1F] = value & 0x3F;
         }
      }

      ppu.vaddr += ppu.vaddr_inc;
      ppu.vaddr &= 0x3FFF;
      break;

   default:
      break;
   }
}

void ppu_setlatchfunc(ppulatchfunc_t func)
{
   ppu.latchfunc = func;
}

void ppu_setvromswitch(ppuvromswitch_t func)
{
   ppu.vromswitch = func;
}

/* rendering routines */
INLINE void draw_bgtile(uint8 *surface, uint8 pat1, uint8 pat2, 
                        const uint8 *colors)
{
   uint32 pattern = ((pat2 & 0xAA) << 8) | ((pat2 & 0x55) << 1)
                    | ((pat1 & 0xAA) << 7) | (pat1 & 0x55);
   
   *surface++ = colors[(pattern >> 14) & 3];
   *surface++ = colors[(pattern >> 6) & 3];
   *surface++ = colors[(pattern >> 12) & 3];
   *surface++ = colors[(pattern >> 4) & 3];
   *surface++ = colors[(pattern >> 10) & 3];
   *surface++ = colors[(pattern >> 2) & 3];
   *surface++ = colors[(pattern >> 8) & 3];
   *surface = colors[pattern & 3];
}

INLINE int draw_oamtile(uint8 *surface, uint8 attrib, uint8 pat1, 
                        uint8 pat2, const uint8 *col_tbl, bool check_strike)
{
   int strike_pixel = -1;
   uint32 color = ((pat2 & 0xAA) << 8) | ((pat2 & 0x55) << 1)
                  | ((pat1 & 0xAA) << 7) | (pat1 & 0x55);

   /* sprite is not 100% transparent */
   if (color)
   {
      uint8 colors[8];

      /* swap pixels around if our tile is flipped */
      if (0 == (attrib & OAMF_HFLIP))
      {
         colors[0] = (color >> 14) & 3;
         colors[1] = (color >> 6) & 3;
         colors[2] = (color >> 12) & 3;
         colors[3] = (color >> 4) & 3;
         colors[4] = (color >> 10) & 3;
         colors[5] = (color >> 2) & 3;
         colors[6] = (color >> 8) & 3;
         colors[7] = color & 3;
      }
      else
      {
         colors[7] = (color >> 14) & 3;
         colors[6] = (color >> 6) & 3;
         colors[5] = (color >> 12) & 3;
         colors[4] = (color >> 4) & 3;
         colors[3] = (color >> 10) & 3;
         colors[2] = (color >> 2) & 3;
         colors[1] = (color >> 8) & 3;
         colors[0] = color & 3;
      }

      /* check for solid sprite pixel overlapping solid bg pixel */
      if (check_strike)
      {
         if (colors[0] && BG_SOLID(surface[0]))
            strike_pixel = 0;
         else if (colors[1] && BG_SOLID(surface[1]))
            strike_pixel = 1;
         else if (colors[2] && BG_SOLID(surface[2]))
            strike_pixel = 2;
         else if (colors[3] && BG_SOLID(surface[3]))
            strike_pixel = 3;
         else if (colors[4] && BG_SOLID(surface[4]))
            strike_pixel = 4;
         else if (colors[5] && BG_SOLID(surface[5]))
            strike_pixel = 5;
         else if (colors[6] && BG_SOLID(surface[6]))
            strike_pixel = 6;
         else if (colors[7] && BG_SOLID(surface[7]))
            strike_pixel = 7;
      }

      /* draw the character */
      if (attrib & OAMF_BEHIND)
      {
         if (colors[0])
            surface[0] = SP_PIXEL | (BG_CLEAR(surface[0]) ? col_tbl[colors[0]] : surface[0]);
         if (colors[1])
            surface[1] = SP_PIXEL | (BG_CLEAR(surface[1]) ? col_tbl[colors[1]] : surface[1]);
         if (colors[2])
            surface[2] = SP_PIXEL | (BG_CLEAR(surface[2]) ? col_tbl[colors[2]] : surface[2]);
         if (colors[3])
            surface[3] = SP_PIXEL | (BG_CLEAR(surface[3]) ? col_tbl[colors[3]] : surface[3]);
         if (colors[4])
            surface[4] = SP_PIXEL | (BG_CLEAR(surface[4]) ? col_tbl[colors[4]] : surface[4]);
         if (colors[5])
            surface[5] = SP_PIXEL | (BG_CLEAR(surface[5]) ? col_tbl[colors[5]] : surface[5]);
         if (colors[6])
            surface[6] = SP_PIXEL | (BG_CLEAR(surface[6]) ? col_tbl[colors[6]] : surface[6]);
         if (colors[7])
            surface[7] = SP_PIXEL | (BG_CLEAR(surface[7]) ? col_tbl[colors[7]] : surface[7]);
      }
      else
      {
         if (colors[0] && SP_CLEAR(surface[0]))
            surface[0] = SP_PIXEL | col_tbl[colors[0]];
         if (colors[1] && SP_CLEAR(surface[1]))
            surface[1] = SP_PIXEL | col_tbl[colors[1]];
         if (colors[2] && SP_CLEAR(surface[2]))
            surface[2] = SP_PIXEL | col_tbl[colors[2]];
         if (colors[3] && SP_CLEAR(surface[3]))
            surface[3] = SP_PIXEL | col_tbl[colors[3]];
         if (colors[4] && SP_CLEAR(surface[4]))
            surface[4] = SP_PIXEL | col_tbl[colors[4]];
         if (colors[5] && SP_CLEAR(surface[5]))
            surface[5] = SP_PIXEL | col_tbl[colors[5]];
         if (colors[6] && SP_CLEAR(surface[6]))
            surface[6] = SP_PIXEL | col_tbl[colors[6]];
         if (colors[7] && SP_CLEAR(surface[7]))
            surface[7] = SP_PIXEL | col_tbl[colors[7]];
      }
   }

   return strike_pixel;
}

static void ppu_renderbg(uint8 *vidbuf)
{
   uint8 *bmp_ptr, *data_ptr, *tile_ptr, *attrib_ptr;
   uint32 refresh_vaddr, bg_offset, attrib_base;
   int tile_count;
   uint8 tile_index, x_tile, y_tile;
   uint8 col_high, attrib, attrib_shift;

   /* draw a line of transparent background color if bg is disabled */
   if (false == ppu.bg_on)
   {
      memset(vidbuf, FULLBG, NES_SCREEN_WIDTH);
      return;
   }

   bmp_ptr = vidbuf - ppu.tile_xofs; /* scroll x */
   refresh_vaddr = 0x2000 + (ppu.vaddr & 0x0FE0); /* mask out x tile */
   x_tile = ppu.vaddr & 0x1F;
   y_tile = (ppu.vaddr >> 5) & 0x1F; /* to simplify calculations */
   bg_offset = ((ppu.vaddr >> 12) & 7) + ppu.bg_base; /* offset in y tile */

   /* calculate initial values */
   tile_ptr = &PPU_MEM(refresh_vaddr + x_tile); /* pointer to tile index */
   attrib_base = (refresh_vaddr & 0x2C00) + 0x3C0 + ((y_tile & 0x1C) << 1);
   attrib_ptr = &PPU_MEM(attrib_base + (x_tile >> 2));
   attrib = *attrib_ptr++;
   attrib_shift = (x_tile & 2) + ((y_tile & 2) << 1);
   col_high = ((attrib >> attrib_shift) & 3) << 2;

   /* ppu fetches 33 tiles */
   tile_count = 33;
   while (tile_count--)
   {
      /* Tile number from nametable */
      tile_index = *tile_ptr++;
      data_ptr = &PPU_MEM(bg_offset + (tile_index << 4));

      /* Handle $FD/$FE tile VROM switching (PunchOut) */
      if (ppu.latchfunc)
         ppu.latchfunc(ppu.bg_base, tile_index);

      draw_bgtile(bmp_ptr, data_ptr[0], data_ptr[8], ppu.palette + col_high);
      bmp_ptr += 8;

      x_tile++;

      if (0 == (x_tile & 1))     /* check every 2 tiles */
      {
         if (0 == (x_tile & 3))  /* check every 4 tiles */
         {
            if (32 == x_tile)    /* check every 32 tiles */
            {
               x_tile = 0;
               refresh_vaddr ^= (1 << 10); /* switch nametable */
               attrib_base ^= (1 << 10);

               /* recalculate pointers */
               tile_ptr = &PPU_MEM(refresh_vaddr);
               attrib_ptr = &PPU_MEM(attrib_base);
            }

            /* Get the attribute byte */
            attrib = *attrib_ptr++;
         }

         attrib_shift ^= 2;
         col_high = ((attrib >> attrib_shift) & 3) << 2;
      }
   }

   /* Blank left hand column if need be */
   if (ppu.bg_mask)
   {
      uint32 *buf_ptr = (uint32 *) vidbuf;
      uint32 bg_clear = (uint32) FULLBG * 0x01010101u;

      ((uint32 *) buf_ptr)[0] = bg_clear;
      ((uint32 *) buf_ptr)[1] = bg_clear;
   }
}

/* OAM entry */
typedef struct obj_s
{
   uint8 y_loc;
   uint8 tile;
   uint8 atr;
   uint8 x_loc;
} obj_t;

/* TODO: fetch valid OAM a scanline before, like the Real Thing */
static void ppu_renderoam(uint8 *vidbuf, int scanline)
{
   uint8 *buf_ptr;
   uint32 vram_offset, savecol[2];
   int sprite_num, spritecount;
   obj_t *sprite_ptr;
   uint8 sprite_height;

   if (false == ppu.obj_on)
      return;

   /* Get our buffer pointer */
   buf_ptr = vidbuf;

   /* Save left hand column? */
   if (ppu.obj_mask)
   {
      savecol[0] = ((uint32 *) buf_ptr)[0];
      savecol[1] = ((uint32 *) buf_ptr)[1];
   }

   sprite_height = ppu.obj_height;
   vram_offset = ppu.obj_base;
   spritecount = 0;

   sprite_ptr = (obj_t *) ppu.oam;

   for (sprite_num = 0; sprite_num < 64; sprite_num++, sprite_ptr++)
   {
      uint8 *data_ptr, *bmp_ptr;
      uint32 vram_adr;
      int y_offset;
      uint8 tile_index, attrib, col_high;
      uint8 sprite_y, sprite_x;
      bool check_strike;
      int strike_pixel;

      sprite_y = sprite_ptr->y_loc + 1;

      /* Check to see if sprite is out of range */
      if ((sprite_y > scanline) || (sprite_y <= (scanline - sprite_height))
          || (0 == sprite_y) || (sprite_y >= 240))
         continue;

      sprite_x = sprite_ptr->x_loc;
      tile_index = sprite_ptr->tile;
      attrib = sprite_ptr->atr;

      bmp_ptr = buf_ptr + sprite_x;

      /* Handle $FD/$FE tile VROM switching (PunchOut) */
      if (ppu.latchfunc)
         ppu.latchfunc(vram_offset, tile_index);

      /* Get upper two bits of color */
      col_high = ((attrib & 3) << 2);

      /* 8x16 even sprites use $0000, odd use $1000 */
      if (16 == ppu.obj_height)
         vram_adr = ((tile_index & 1) << 12) | ((tile_index & 0xFE) << 4);
      else
         vram_adr = vram_offset + (tile_index << 4);

      /* Get the address of the tile */
      data_ptr = &PPU_MEM(vram_adr);

      /* Calculate offset (line within the sprite) */
      y_offset = scanline - sprite_y;
      if (y_offset > 7)
         y_offset += 8;

      /* Account for vertical flippage */
      if (attrib & OAMF_VFLIP)
      {
         if (16 == ppu.obj_height)
            y_offset -= 23;
         else
            y_offset -= 7;

         data_ptr -= y_offset;
      }
      else
      {
         data_ptr += y_offset;
      }

      /* if we're on sprite 0 and sprite 0 strike flag isn't set,
      ** check for a strike 
      */
      check_strike = (0 == sprite_num) && (false == ppu.strikeflag);
      strike_pixel = draw_oamtile(bmp_ptr, attrib, data_ptr[0], data_ptr[8], ppu.palette + 16 + col_high, check_strike);
      if (strike_pixel >= 0)
         ppu_setstrike(strike_pixel);

      /* maximum of 8 sprites per scanline */
      if (++spritecount == PPU_MAXSPRITE)
      {
         ppu.stat |= PPU_STATF_MAXSPRITE;
         if (ppu_limit_sprites)
            break;
      }
   }

   /* Restore lefthand column */
   if (ppu.obj_mask)
   {
      ((uint32 *) buf_ptr)[0] = savecol[0];
      ((uint32 *) buf_ptr)[1] = savecol[1];
   }
}

/* Fake rendering a line */
/* This is needed for sprite 0 hits when we're skipping drawing a frame */
static void ppu_fakeoam(int scanline)
{
   uint8 *data_ptr;
   obj_t *sprite_ptr;
   uint32 vram_adr, color;
   int y_offset;
   uint8 pat1, pat2;
   uint8 tile_index, attrib;
   uint8 sprite_height, sprite_y, sprite_x;

   /* we don't need to be here if strike flag is set */

   if (false == ppu.obj_on || ppu.strikeflag)
      return;

   sprite_height = ppu.obj_height;
   sprite_ptr = (obj_t *) ppu.oam;
   sprite_y = sprite_ptr->y_loc + 1;

   /* Check to see if sprite is out of range */
   if ((sprite_y > scanline) || (sprite_y <= (scanline - sprite_height)) 
       || (0 == sprite_y) || (sprite_y > 240))
      return;

   sprite_x = sprite_ptr->x_loc;
   tile_index = sprite_ptr->tile;
   attrib = sprite_ptr->atr;

   /* 8x16 even sprites use $0000, odd use $1000 */
   if (16 == ppu.obj_height)
      vram_adr = ((tile_index & 1) << 12) | ((tile_index & 0xFE) << 4);
   else
      vram_adr = ppu.obj_base + (tile_index << 4);

   data_ptr = &PPU_MEM(vram_adr);

   /* Calculate offset (line within the sprite) */
   y_offset = scanline - sprite_y;
   if (y_offset > 7)
      y_offset += 8;

   /* Account for vertical flippage */
   if (attrib & OAMF_VFLIP)
   {
      if (16 == ppu.obj_height)
         y_offset -= 23;
      else
         y_offset -= 7;
      data_ptr -= y_offset;
   }
   else
   {
      data_ptr += y_offset;
   }

   /* check for a solid sprite 0 pixel */
   pat1 = data_ptr[0];
   pat2 = data_ptr[8];
   color = ((pat2 & 0xAA) << 8) | ((pat2 & 0x55) << 1)
                  | ((pat1 & 0xAA) << 7) | (pat1 & 0x55);

   if (color)
   {
      uint8 colors[8];

      /* buckle up, it's going to get ugly... */
      if (0 == (attrib & OAMF_HFLIP))
      {
         colors[0] = (color >> 14) & 3;
         colors[1] = (color >> 6) & 3;
         colors[2] = (color >> 12) & 3;
         colors[3] = (color >> 4) & 3;
         colors[4] = (color >> 10) & 3;
         colors[5] = (color >> 2) & 3;
         colors[6] = (color >> 8) & 3;
         colors[7] = color & 3;
      }
      else
      {
         colors[7] = (color >> 14) & 3;
         colors[6] = (color >> 6) & 3;
         colors[5] = (color >> 12) & 3;
         colors[4] = (color >> 4) & 3;
         colors[3] = (color >> 10) & 3;
         colors[2] = (color >> 2) & 3;
         colors[1] = (color >> 8) & 3;
         colors[0] = color & 3;
      }

      if (colors[0])
         ppu_setstrike(sprite_x + 0);
      else if (colors[1])
         ppu_setstrike(sprite_x + 1);
      else if (colors[2])
         ppu_setstrike(sprite_x + 2);
      else if (colors[3])
         ppu_setstrike(sprite_x + 3);
      else if (colors[4])
         ppu_setstrike(sprite_x + 4);
      else if (colors[5])
         ppu_setstrike(sprite_x + 5);
      else if (colors[6])
         ppu_setstrike(sprite_x + 6);
      else if (colors[7])
         ppu_setstrike(sprite_x + 7);
   }
}

bool ppu_enabled(void)
{
   return (ppu.bg_on || ppu.obj_on);
}

/* one line: 8 pixels of overdraw on each side for the scroll and the sprites */
static uint8 linebuf[8 + NES_SCREEN_WIDTH + 16] __attribute__((aligned(4)));
static uint16 line565[NES_SCREEN_WIDTH];
const uint16 *nes_palette565 = NULL;

/* (Retro-Go on NumPlay) what each line of the last frame drawn was drawn with: the scroll, the
   controls and the CHR and nametable banks (a few sets a frame, for the games that switch them
   for a status bar) */
#define  CHR_SETS 4
typedef struct
{
   uint16 vaddr;
   uint8 xofs, ctrl0, ctrl1, set, drawn;
} snap_t;
static snap_t *snap; /* (NES_SCREEN_HEIGHT of them, in Retro-Go's heap) */

bool nes_ppu_alloc(void)
{
   nes_ppu_p = rg_alloc(sizeof(ppu_t));
   snap = rg_alloc(sizeof(snap_t) * NES_SCREEN_HEIGHT);
   return nes_ppu_p && snap;
}
static uint8 *chr_sets[CHR_SETS][12];
static int chr_used;

static void ppu_snapshot(int scanline)
{
   int i;
   if (0 == scanline)
   {
      chr_used = 0;
      memset(snap, 0, sizeof(snap_t) * NES_SCREEN_HEIGHT);
   }
   /* (most lines have the banks of the line before) */
   i = scanline && snap[scanline - 1].drawn ? snap[scanline - 1].set : chr_used;
   if (i >= chr_used || ppu_pages_changed)
      for (i = 0; i < chr_used; i++)
         if (!memcmp(chr_sets[i], ppu.page, sizeof chr_sets[0]))
            break;
   ppu_pages_changed = false;
   if (i == chr_used)
   {
      if (chr_used == CHR_SETS)
         i = CHR_SETS - 1; /* (more than that in a frame: the last one stands for the rest) */
      else
         chr_used++;
      memcpy(chr_sets[i], ppu.page, sizeof chr_sets[0]);
   }
   snap[scanline].vaddr = (uint16) ppu.vaddr;
   snap[scanline].xofs = (uint8) ppu.tile_xofs;
   snap[scanline].ctrl0 = ppu.ctrl0;
   snap[scanline].ctrl1 = ppu.ctrl1;
   snap[scanline].set = (uint8) i;
   snap[scanline].drawn = 1;
}

/* what the two control registers decide (ppu_write's) */
static void ppu_controls(uint8 ctrl0, uint8 ctrl1)
{
   ppu.obj_height = (ctrl0 & PPU_CTRL0F_OBJ16) ? 16 : 8;
   ppu.bg_base = (ctrl0 & PPU_CTRL0F_BGADDR) ? 0x1000 : 0;
   ppu.obj_base = (ctrl0 & PPU_CTRL0F_OBJADDR) ? 0x1000 : 0;
   ppu.obj_on = (ctrl1 & PPU_CTRL1F_OBJON) ? true : false;
   ppu.bg_on = (ctrl1 & PPU_CTRL1F_BGON) ? true : false;
   ppu.obj_mask = (ctrl1 & PPU_CTRL1F_OBJMASK) ? false : true;
   ppu.bg_mask = (ctrl1 & PPU_CTRL1F_BGMASK) ? false : true;
}

static void ppu_renderscanline(int scanline, bool draw_flag)
{
   uint8 *buf = linebuf + 8;

   /* start scanline - transfer ppu latch into vaddr */
   if (ppu.bg_on || ppu.obj_on)
   {
      if (0 == scanline)
      {
         ppu.vaddr = ppu.vaddr_latch;
      }
      else
      {
         ppu.vaddr &= ~0x041F;
         ppu.vaddr |= (ppu.vaddr_latch & 0x041F);
      }
   }

   if (draw_flag)
   {
      ppu_snapshot(scanline);
      ppu_renderbg(buf);
   }

   /* TODO: fetch obj data 1 scanline before */
   if (true == draw_flag)
   {
      ppu_renderoam(buf, scanline);
      for (int x = 0; x < NES_SCREEN_WIDTH; x++)
         line565[x] = nes_palette565[buf[x] & 0x3F];
      rg_line(scanline, line565);
   }
   else
      ppu_fakeoam(scanline);
}

/* (Retro-Go on NumPlay) The last frame drawn, again, under Retro-Go's menus: each line with the
   scroll, controls and CHR banks it had; the PPU is left as it was. */
void ppu_redraw(void)
{
   ppu_t saved;
   saved = ppu;
   for (int y = 0; y < NES_SCREEN_HEIGHT; y++)
   {
      uint8 *buf = linebuf + 8;
      if (!snap[y].drawn)
      {
         for (int x = 0; x < NES_SCREEN_WIDTH; x++)
            line565[x] = 0;
         rg_line(y, line565);
         continue;
      }
      ppu.vaddr = snap[y].vaddr;
      ppu.tile_xofs = snap[y].xofs;
      ppu_controls(snap[y].ctrl0, snap[y].ctrl1);
      memcpy(ppu.page, chr_sets[snap[y].set], sizeof chr_sets[0]);
      ppu.latchfunc = NULL;
      ppu.strikeflag = true;
      ppu_renderbg(buf);
      ppu_renderoam(buf, y);
      for (int x = 0; x < NES_SCREEN_WIDTH; x++)
         line565[x] = nes_palette565[buf[x] & 0x3F];
      rg_line(y, line565);
   }
   ppu = saved;
   ppu_pages_changed = true;
}


void ppu_endscanline(int scanline)
{
   /* modify vram address at end of scanline */
   if (scanline < 240 && (ppu.bg_on || ppu.obj_on))
   {
      int ytile;

      /* check for max 3 bit y tile offset */
      if (7 == (ppu.vaddr >> 12))
      {
         ppu.vaddr &= ~0x7000;      /* clear y tile offset */
         ytile = (ppu.vaddr >> 5) & 0x1F;

         if (29 == ytile)
         {
            ppu.vaddr &= ~0x03E0;   /* clear y tile */
            ppu.vaddr ^= 0x0800;    /* toggle nametable */
         }
         else if (31 == ytile)
         {
            ppu.vaddr &= ~0x03E0;   /* clear y tile */
         }
         else
         {
            ppu.vaddr += 0x20;      /* increment y tile */
         }
      }
      else
      {
         ppu.vaddr += 0x1000;       /* increment tile y offset */
      }
   }
}

void ppu_checknmi(void)
{
   if (ppu.ctrl0 & PPU_CTRL0F_NMI)
      nes_nmi();
}

void ppu_scanline(int scanline, bool draw_flag)
{
   if (scanline < 240)
   {
      /* Lower the Max Sprite per scanline flag */
      ppu.stat &= ~PPU_STATF_MAXSPRITE;
      ppu_renderscanline(scanline, draw_flag);
   }
   else if (241 == scanline)
   {
      ppu.stat |= PPU_STATF_VBLANK;
      ppu.vram_accessible = true;
   }
   else if (261 == scanline)
   {
      ppu.stat &= ~PPU_STATF_VBLANK;
      ppu.strikeflag = false;
      ppu.strike_cycle = (uint32) -1;

      ppu.vram_accessible = false;
   }
}

