/* The Sega Master System, Game Gear and SG-1000 around superzazu's Z80: the cartridge's mappers
 * (Sega's, Codemasters', the Korean one), the work RAM, the pads, the Game Gear's ports and the
 * video chip (mode 4: the tiles, their scrolling and priorities, the sprites, the line and frame
 * interrupts, the counters; and the TMS9918's modes the SG-1000 draws with: Graphics I and II,
 * Text and Multicolor, with its sprites), drawn a line at a time. Written for NumPlay from the
 * hardware's public descriptions (Charles MacDonald's notes on the VDP and the Master System,
 * Texas Instruments' TMS9918A manual, SMS Power's pages); no sound, the calculator has no
 * speaker. The short list of games that need something of their own (their CRC32s) is SMS Plus
 * GX's (GNU GPL v2 or later). */
#include <string.h>
#include "sms.h"
#include "z80.h"

#define LINE_CLOCKS 228 /* the Z80's clocks a line (3.58 MHz) */

enum { MAP_SEGA, MAP_CODIES, MAP_KOREA };
enum { DB_CODIES = 1, DB_PAL = 2, DB_GGMS = 4, DB_KOREA = 8 };

/* the games that need something of their own (from SMS Plus GX's list): Codemasters' or the
   Korean mapper, 50 Hz (European games made for it), Master System mode on the Game Gear */
static const struct { uint32_t crc; uint8_t flags; } special[] = {
  {0x29822980, DB_CODIES | DB_PAL}, {0x6CAA625B, DB_CODIES}, {0xEA5C3A6F, DB_CODIES},
  {0x152F0DCC, DB_CODIES}, {0x5E53C7F7, DB_CODIES}, {0x8813514B, DB_CODIES | DB_PAL},
  {0xAA140C9C, DB_CODIES | DB_GGMS}, {0xB9664AE1, DB_CODIES | DB_PAL}, {0xC888222B, DB_CODIES | DB_GGMS},
  {0x76C5BDFB, DB_CODIES | DB_GGMS}, {0xD9A7F170, DB_CODIES}, {0xA577CE46, DB_CODIES | DB_PAL},
  {0xF7C524F6, DB_CODIES | DB_PAL}, {0xDBE8895C, DB_CODIES | DB_PAL}, {0xC1756BEE, DB_CODIES | DB_PAL},
  {0x89B79E77, DB_KOREA}, {0x18FB98A3, DB_KOREA}, {0x97D03541, DB_KOREA}, {0x67C2F0FF, DB_KOREA},
  {0x72420F38, DB_PAL}, {0x2D48C1D3, DB_PAL}, {0x1CBB7BF1, DB_PAL}, {0x1B10A951, DB_PAL},
  {0xC0E25D62, DB_PAL}, {0xC9DBF936, DB_PAL}, {0xA109A6FE, DB_PAL}, {0x4FF0CEC7, DB_PAL},
  {0x0047B615, DB_PAL}, {0xF42E145C, DB_PAL}, {0x9F951756, DB_PAL}, {0x1575581D, DB_PAL},
  {0x96B3F29E, DB_PAL}, {0x5B3B922C, DB_PAL}, {0xD6F2BFCA, DB_PAL}, {0xCA1D3752, DB_PAL},
  {0x85CFC9C9, DB_PAL}, {0x38434560, DB_PAL}, {0xDA2A68C6, DB_PAL}, {0x205CAAE8, DB_PAL},
  {0xA67F2A5C, DB_PAL},
  {0x59840FD6, DB_GGMS}, {0x9942B69B, DB_GGMS}, {0x5877B10D, DB_GGMS}, {0x9C76FB3A, DB_GGMS},
  {0x7BB81E3D, DB_GGMS}, {0x44FBE8F6, DB_GGMS}, {0x18086B70, DB_GGMS}, {0xDA8E95A9, DB_GGMS},
  {0xCB42BD33, DB_GGMS}, {0x1D93246E, DB_GGMS}, {0xA2F9C7AF, DB_GGMS}, {0xF037EC00, DB_GGMS},
  {0xE5F789B9, DB_GGMS}, {0x311D2863, DB_GGMS}, {0x45F058D6, DB_GGMS}, {0x56201996, DB_GGMS},
  {0x10DBBEF4, DB_GGMS}, {0xBD1CC7DF, DB_GGMS},
};

/* what a save state keeps of the machine besides the Z80 and the memories (bytes only, any value
   read back is made sound by sms_state_loaded) */
typedef struct {
  uint8_t reg[16];       /* the video chip's registers */
  uint8_t cram[64];      /* its colours: 32 bytes on the Master System, 32 words on the Game Gear */
  uint8_t status;        /* frame interrupt, sprite overflow, sprite collision */
  uint8_t pending;       /* the control port's first byte was written */
  uint8_t latch, code, buffer, cram_latch;
  uint16_t addr;
  uint8_t vint, hint;    /* the frame and line interrupts, pending */
  int16_t counter;       /* the line counter */
  uint16_t line;         /* the line running */
  uint8_t vscroll;       /* the vertical scroll, as the frame took it */
  uint8_t mapper[4];     /* Sega's at $FFFC-$FFFF; Codemasters' and the Korean slots in 1 to 3 */
  uint8_t io3f;          /* the pads' lines (port $3F) */
  uint8_t sram_on;       /* the game turned its cartridge RAM on */
  uint8_t pause;         /* the pause button, held */
  uint8_t pad_[3];
} state_t;

/* a line's registers as it was drawn (sms_redraw draws the frame again from them) */
typedef struct { uint8_t r0, r1, r2, r3, r4, r5, r6, r7, hs, vs; } line_regs_t;

static z80 cpu;
static state_t st;
static uint8_t *ram, *vram, *sram, *sram_slot;
static uint32_t *expand;        /* a byte of a tile's plane -> its 8 pixels, a bit in each nibble */
static line_regs_t *snap;
static int snap_lines;
static const uint8_t *rom;
static uint32_t rom_size, banks;
static uint8_t mapper_kind;
static bool gg, gg_vdp, sg, pal, sram_dirty;
static uint8_t pad_bits;
static bool start_button;
static uint32_t line_start, line_end;
static uint16_t pal565[32];
/* the TMS9918's colours (0 is see-through: the backdrop shows) */
#define RGB565(c) (uint16_t)(((c) >> 19 & 31) << 11 | ((c) >> 10 & 63) << 5 | ((c) >> 3 & 31))
static const uint16_t tms565[16] = {
  RGB565(0x000000), RGB565(0x000000), RGB565(0x21C842), RGB565(0x5EDC78), RGB565(0x5455ED), RGB565(0x7D76FC),
  RGB565(0xD4524D), RGB565(0x42EBF5), RGB565(0xFC5554), RGB565(0xFF7978), RGB565(0xD4C154), RGB565(0xE6CE80),
  RGB565(0x21B03B), RGB565(0xC95BBA), RGB565(0xCCCCCC), RGB565(0xFFFFFF)};
static uint8_t lb[256 + 16];    /* a line: colour index (0-31), 0x40 the tile over the sprites, 0x20 a sprite */
static uint16_t px[256];
static const uint8_t open_bus[0x400] = {[0 ... 0x3FF] = 0xFF};
const uint8_t *z80_rmap[64];

/* ------------------------------------------------------------ memory */
static const uint8_t *rom_page(uint32_t bank, int k) {
  uint32_t at = (bank % banks) * 0x4000 + (uint32_t)k * 0x400;
  return at + 0x400 <= rom_size ? rom + at : open_bus;
}

static void map_update(void) {
  sram_slot = NULL;
  if (sg) { /* the SG-1000: 48 KB of cartridge in a row (the smaller ones with RAM after them), 1 KB of RAM */
    for (int k = 0; k < 48; k++) {
      uint32_t at = (uint32_t)k * 0x400;
      z80_rmap[k] = at + 0x400 <= rom_size ? rom + at : k >= 32 && rom_size <= 0x8000 ? sram + (at & 0x3FFF) : open_bus;
    }
    for (int k = 0; k < 16; k++) z80_rmap[48 + k] = ram;
    return;
  }
  if (mapper_kind == MAP_SEGA) {
    z80_rmap[0] = rom_page(0, 0); /* (the first 1 KB never moves) */
    for (int k = 1; k < 16; k++) z80_rmap[k] = rom_page(st.mapper[1], k);
    for (int k = 0; k < 16; k++) z80_rmap[16 + k] = rom_page(st.mapper[2], k);
    if (st.mapper[0] & 0x08) sram_slot = sram + (st.mapper[0] & 0x04 ? 0x4000 : 0);
    for (int k = 0; k < 16; k++) z80_rmap[32 + k] = sram_slot ? sram_slot + k * 0x400 : rom_page(st.mapper[3], k);
  } else {
    for (int k = 0; k < 16; k++) {
      z80_rmap[k] = rom_page(mapper_kind == MAP_CODIES ? st.mapper[1] : 0, k);
      z80_rmap[16 + k] = rom_page(mapper_kind == MAP_CODIES ? st.mapper[2] : 1, k);
      z80_rmap[32 + k] = rom_page(st.mapper[3], k);
    }
  }
  for (int k = 0; k < 16; k++) z80_rmap[48 + k] = ram + (k & 7) * 0x400;
}

void z80_bus_write(uint16_t a, uint8_t v) {
  if (sg) {
    if (a >= 0xC000) ram[a & 0x3FF] = v;
    else if (a >= 0x8000 && rom_size <= 0x8000) sram[a & 0x3FFF] = v; /* (The Castle's and Othello's RAM) */
    return;
  }
  if (a >= 0xC000) {
    ram[a & 0x1FFF] = v;
    if (a >= 0xFFFC && mapper_kind == MAP_SEGA) {
      st.mapper[a & 3] = v;
      if ((a & 3) == 0 && (v & 0x08)) st.sram_on = 1;
      map_update();
    }
  } else if (a >= 0x8000 && sram_slot) {
    sram_slot[a & 0x3FFF] = v;
    sram_dirty = true;
  } else if (mapper_kind == MAP_CODIES && (a & 0x3FFF) == 0) {
    st.mapper[1 + (a >> 14)] = v;
    map_update();
  } else if (mapper_kind == MAP_KOREA && a == 0xA000) {
    st.mapper[3] = v;
    map_update();
  }
}

/* ------------------------------------------------------------ the video chip */
static int height_of(uint8_t r0, uint8_t r1) {
  if ((r0 & 0x06) == 0x06) { /* (mode 4 with M2: the taller pictures) */
    if (r1 & 0x10) return 224;
    if (r1 & 0x08) return 240;
  }
  return 192;
}
static int height(void) { return height_of(st.reg[0], st.reg[1]); }

static void irq_update(void) {
  cpu.irq_line = (st.vint && (st.reg[1] & 0x20)) || (st.hint && (st.reg[0] & 0x10) && !sg);
}

static void palette_update(int i) {
  int r, g, b;
  if (gg_vdp) {
    unsigned c = st.cram[2 * i] | st.cram[2 * i + 1] << 8;
    r = c & 15, g = (c >> 4) & 15, b = (c >> 8) & 15;
    pal565[i] = (uint16_t)((r * 31 + 7) / 15 << 11 | (g * 63 + 7) / 15 << 5 | (b * 31 + 7) / 15);
  } else {
    unsigned c = st.cram[i];
    r = c & 3, g = (c >> 2) & 3, b = (c >> 4) & 3;
    pal565[i] = (uint16_t)((r * 31 + 1) / 3 << 11 | (g * 63 + 1) / 3 << 5 | (b * 31 + 1) / 3);
  }
}

static uint8_t vdp_status(void) {
  uint8_t v = sg ? st.status : st.status | 0x1F;
  st.status = 0, st.pending = 0, st.vint = 0, st.hint = 0;
  irq_update();
  return v;
}

static uint8_t vdp_read(void) {
  uint8_t v = st.buffer;
  st.pending = 0;
  st.buffer = vram[st.addr & 0x3FFF];
  st.addr = (st.addr + 1) & 0x3FFF;
  return v;
}

static void vdp_write(uint8_t v) {
  st.pending = 0;
  if (st.code == 3) {
    if (!gg_vdp) {
      st.cram[st.addr & 0x1F] = v;
      palette_update(st.addr & 0x1F);
    } else if (st.addr & 1) { /* (the Game Gear: a word, its first byte kept until the second) */
      st.cram[st.addr & 0x3E] = st.cram_latch;
      st.cram[(st.addr & 0x3E) | 1] = v;
      palette_update((st.addr >> 1) & 0x1F);
    } else {
      st.cram_latch = v;
    }
  } else {
    vram[st.addr & 0x3FFF] = v;
  }
  st.buffer = v;
  st.addr = (st.addr + 1) & 0x3FFF;
}

static void vdp_control(uint8_t v) {
  if (!st.pending) {
    st.latch = v;
    st.addr = (st.addr & 0x3F00) | v;
    st.pending = 1;
    return;
  }
  st.pending = 0;
  st.code = v >> 6;
  st.addr = (uint16_t)((v << 8 | st.latch) & 0x3FFF);
  if (st.code == 0) {
    st.buffer = vram[st.addr];
    st.addr = (st.addr + 1) & 0x3FFF;
  } else if (st.code == 2) {
    st.reg[v & 0x0F] = st.latch;
    irq_update();
  }
}

/* the counters the game can read: the line (with the jumps it makes in the blank) and the
   pixel, from the clocks run in the line */
static uint8_t vcounter(void) {
  int l = st.line, h = height();
  if (!pal) return (uint8_t)(l <= (h == 192 ? 0xDA : 0xEA) ? l : l - 6);
  if (h == 192) return (uint8_t)(l <= 0xF2 ? l : l - 57);
  return (uint8_t)(l <= 0x102 ? l : l - 57);
}
static uint8_t hcounter(void) {
  uint32_t c = cpu.cyc - line_start;
  if (c >= LINE_CLOCKS) c = LINE_CLOCKS - 1;
  uint32_t h = c * 3 / 4;
  return (uint8_t)(h > 0x93 ? h + 0x55 : h);
}

/* ------------------------------------------------------------ the ports */
static uint8_t pad_dc(void) { return (uint8_t)~pad_bits; }

/* the pads' TH lines: set as outputs, they read what was written (a machine from outside Japan) */
static uint8_t pad_dd(void) {
  uint8_t v = 0xFF;
  if (!(st.io3f & 0x02)) v = (uint8_t)((v & ~0x40) | (st.io3f & 0x20) << 1);
  if (!(st.io3f & 0x08)) v = (uint8_t)((v & ~0x80) | (st.io3f & 0x80));
  return v;
}

uint8_t z80_bus_in(uint8_t port) {
  if (gg && port < 7) { /* the Game Gear's: Start, outside Japan, 60 Hz; the link port, idle */
    static const uint8_t idle[7] = {0, 0x7F, 0xFF, 0x00, 0xFF, 0x00, 0xFF};
    return port ? idle[port] : (uint8_t)(0x40 | (start_button ? 0 : 0x80));
  }
  switch (port & 0xC1) {
  case 0x00: case 0x01: return 0xFF;
  case 0x40: return vcounter();
  case 0x41: return hcounter();
  case 0x80: return vdp_read();
  case 0x81: return vdp_status();
  case 0xC0: return pad_dc();
  default: return pad_dd();
  }
}

void z80_bus_out(uint8_t port, uint8_t v) {
  if (gg && port < 7) return; /* (the link port and the stereo sound) */
  switch (port & 0xC1) {
  case 0x01: st.io3f = v; break;
  case 0x80: vdp_write(v); break;
  case 0x81: vdp_control(v); break;
  default: break; /* (the memory control without a BIOS, the sound chip) */
  }
}

/* ------------------------------------------------------------ drawing a line */
static inline uint32_t tile_row(const uint8_t *p) {
  return expand[p[0]] | expand[p[1]] << 1 | expand[p[2]] << 2 | expand[p[3]] << 3;
}

static void background(int line, const line_regs_t *r) {
  int h = height_of(r->r0, r->r1);
  unsigned nt = h == 192 ? (r->r2 & 0x0E) << 10 : ((r->r2 & 0x0C) << 10) | 0x0700;
  int fine = r->hs & 7, coarse = r->hs >> 3;
  int vrow = line + r->vs;
  vrow = h == 192 ? vrow % 224 : vrow & 255;
  /* 33 tiles from 8 pixels left of the screen, the first ones half out with the fine scroll; the
     last 8 columns don't scroll down if the game said so */
  for (int k = 0; k <= 32; k++) {
    int v = k >= 25 && (r->r0 & 0x80) ? line : vrow;
    unsigned a = (nt + (unsigned)(v >> 3) * 64 + (unsigned)((k - 1 - coarse) & 31) * 2) & 0x3FFE;
    unsigned e = vram[a] | vram[a + 1] << 8;
    int y = v & 7;
    if (e & 0x400) y ^= 7;
    uint32_t row = tile_row(vram + ((e & 0x1FF) << 5 | (unsigned)y << 2));
    uint8_t pal = e & 0x800 ? 16 : 0, prio = e & 0x1000 ? 0x40 : 0;
    uint8_t *o = lb + fine + 8 * k;
    if (e & 0x200)
      for (int i = 7; i >= 0; i--, row >>= 4) o[i] = (uint8_t)(pal | (row & 15) | (row & 15 ? prio : 0));
    else
      for (int i = 0; i < 8; i++, row >>= 4) o[i] = (uint8_t)(pal | (row & 15) | (row & 15 ? prio : 0));
  }
}

/* the sprites on the line (8 at most: more set the overflow flag), over the tiles but under the
   tiles in front; two sprites' pixels on each other set the collision flag */
static void sprites(int line, const line_regs_t *r) {
  const uint8_t *sat = vram + ((r->r5 << 7) & 0x3F00);
  int h = height_of(r->r0, r->r1), size = r->r1 & 2 ? 16 : 8, zoom = r->r1 & 1, span = size << zoom;
  int base = (r->r6 & 4) << 6, shift = r->r0 & 8 ? 8 : 0, count = 0;
  for (int i = 0; i < 64; i++) {
    int y = sat[i];
    if (y == 0xD0 && h == 192) break;
    if (++y > 240) y -= 256;
    int row = line - y;
    if (row < 0 || row >= span) continue;
    if (++count > 8) {
      st.status |= 0x40;
      break;
    }
    int x = sat[0x80 + 2 * i] - shift, t = sat[0x81 + 2 * i];
    if (size == 16) t &= 0xFE;
    row >>= zoom;
    t = ((base | t) + (row >> 3)) & 0x1FF;
    uint32_t bits = tile_row(vram + (t << 5 | (row & 7) << 2));
    for (int j = 0; bits; j++, bits >>= 4) {
      int c = bits & 15;
      if (!c) continue;
      for (int s = 0; s <= zoom; s++) {
        int xx = x + (j << zoom) + s;
        if (xx >= 256) break;
        uint8_t *d = lb + 8 + xx;
        if (*d & 0x20) st.status |= 0x20;
        else *d = *d & 0x40 ? *d | 0x20 : (uint8_t)(0x30 | c);
      }
    }
  }
}

/* ---- the TMS9918's modes: lb gets its colours (0-15, 0 see-through) */
static void tms_background(int line, const line_regs_t *r) {
  uint8_t *o = lb + 8, fg, bg;
  unsigned nt = (r->r2 & 0x0F) << 10, row = (unsigned)line & 7;
  if (r->r1 & 0x10) { /* Text: 40 columns of 6 pixels, in R7's colours */
    fg = r->r7 >> 4, bg = r->r7 & 15;
    memset(o, 0, 256);
    for (int c = 0; c < 40; c++) {
      uint8_t bits = vram[((r->r4 & 7) << 11 | vram[(nt + (unsigned)(line >> 3) * 40 + (unsigned)c) & 0x3FFF] << 3 | row) & 0x3FFF];
      for (int i = 0; i < 6; i++) o[8 + 6 * c + i] = bits & (0x80 >> i) ? fg : bg;
    }
    return;
  }
  for (int c = 0; c < 32; c++) {
    unsigned name = vram[nt + (unsigned)(line >> 3) * 32 + (unsigned)c];
    uint8_t *d = o + 8 * c;
    if (r->r1 & 0x08) { /* Multicolor: blocks of 4 by 4 */
      uint8_t v = vram[((r->r4 & 7) << 11 | name << 3 | ((unsigned)(line >> 3) & 3) << 1 | ((unsigned)line >> 2 & 1)) & 0x3FFF];
      memset(d, v >> 4, 4), memset(d + 4, v & 15, 4);
      continue;
    }
    uint8_t bits, col;
    if (r->r0 & 0x02) { /* Graphics II: a pattern and colours for each third of the screen */
      unsigned i = ((unsigned)(line >> 6) << 8 | name) << 3 | row;
      bits = vram[(r->r4 & 4) << 11 | (i & ((r->r4 & 3) << 11 | 0x7FF))];
      col = vram[(r->r3 & 0x80) << 6 | (i & ((r->r3 & 0x7F) << 6 | 0x3F))];
    } else { /* Graphics I: colours for each 8 patterns */
      bits = vram[((r->r4 & 7) << 11 | name << 3 | row) & 0x3FFF];
      col = vram[(r->r3 << 6 | name >> 3) & 0x3FFF];
    }
    fg = col >> 4, bg = col & 15;
    for (int i = 0; i < 8; i++) d[i] = bits & (0x80 >> i) ? fg : bg;
  }
}

/* its sprites: 32, 4 on a line (the fifth sets the flag and its number), one colour each; two
   sprites' pixels on each other set the collision flag */
static void tms_sprites(int line, const line_regs_t *r) {
  const uint8_t *sat = vram + ((r->r5 & 0x7F) << 7);
  unsigned pg = (r->r6 & 7) << 11;
  int size = r->r1 & 2 ? 16 : 8, zoom = r->r1 & 1, span = size << zoom, count = 0;
  uint8_t taken[256 + 32] = {0};
  for (int i = 0; i < 32; i++) {
    int y = sat[4 * i];
    if (y == 0xD0) break;
    if (y > 0xE0) y -= 256;
    int row = line - (y + 1);
    if (row < 0 || row >= span) continue;
    if (++count > 4) {
      if (!(st.status & 0x40)) st.status = (uint8_t)((st.status & 0xA0) | 0x40 | i);
      break;
    }
    int x = sat[4 * i + 1], t = sat[4 * i + 2], c = sat[4 * i + 3] & 15;
    if (sat[4 * i + 3] & 0x80) x -= 32;
    row >>= zoom;
    if (size == 16) t &= 0xFC;
    unsigned bits = (unsigned)vram[(pg | (unsigned)t << 3) + (unsigned)row] << 8;
    if (size == 16) bits |= vram[(pg | (unsigned)t << 3) + 16 + (unsigned)row];
    for (int j = 0; j < size; j++) {
      if (!(bits & (0x8000u >> j))) continue;
      for (int s = 0; s <= zoom; s++) {
        int xx = x + (j << zoom) + s;
        if (xx < 0) continue;
        if (xx >= 256) break;
        if (taken[xx]) st.status |= 0x20;
        else if (taken[xx] = 1, c) lb[8 + xx] = (uint8_t)c;
      }
    }
  }
}

/* the picture shown (the Game Gear's screen: 160 x 144 in the middle) */
void sms_view(int *x, int *y, int *w, int *h) {
  int lines = height();
  if (gg_vdp) *x = 48, *y = (lines - 144) / 2, *w = 160, *h = 144;
  else *x = 0, *y = 0, *w = 256, *h = lines;
}

static void draw_line(int line, const line_regs_t *r, bool draw) {
  bool on = r->r1 & 0x40, tms = !(r->r0 & 0x04), text = tms && (r->r1 & 0x10);
  int vx, vy, vw, vh;
  sms_view(&vx, &vy, &vw, &vh);
  if (!draw || line < vy || line >= vy + vh) { /* (not seen: only the flags the game can read) */
    if (!on || text) return;
    memset(lb + 8, 0, 256);
    tms ? tms_sprites(line, r) : sprites(line, r);
    return;
  }
  if (tms) {
    uint16_t back = tms565[r->r7 & 15];
    if (on) {
      tms_background(line, r);
      if (!text) tms_sprites(line, r);
    }
    for (int x = vx; x < vx + vw; x++) px[x] = on && lb[8 + x] ? tms565[lb[8 + x]] : back;
    rg_line(line, px);
    return;
  }
  uint8_t backdrop = (uint8_t)(16 | (r->r7 & 15));
  if (!on) {
    memset(lb + 8, backdrop, 256);
  } else {
    background(line, r);
    sprites(line, r);
    if (r->r0 & 0x20) memset(lb + 8, backdrop, 8); /* (the left column hidden) */
  }
  for (int x = vx; x < vx + vw; x++) px[x] = pal565[lb[8 + x] & 31];
  rg_line(line, px);
}

static void snap_line(int line) {
  line_regs_t *r = &snap[line];
  *r = (line_regs_t){st.reg[0], st.reg[1], st.reg[2], st.reg[3], st.reg[4], st.reg[5], st.reg[6], st.reg[7],
                     (uint8_t)((st.reg[0] & 0x40) && line < 16 ? 0 : st.reg[8]), st.vscroll};
}

/* ------------------------------------------------------------ the machine */
static int lines_per_frame(void) { return pal ? 313 : 262; }

static void run_line(bool draw) {
  int line = st.line, h = height();
  if (line == 0) st.vscroll = st.reg[9];
  if (line < h) {
    snap_line(line);
    draw_line(line, &snap[line], draw);
  }
  /* the line counter, on the picture's lines and the one after; reloaded below */
  if (line <= h) {
    if (--st.counter < 0) st.counter = st.reg[10], st.hint = 1;
  } else {
    st.counter = st.reg[10];
  }
  if (line == h) st.vint = 1, st.status |= 0x80;
  irq_update();
  line_start = line_end;
  line_end += LINE_CLOCKS;
  z80_run(&cpu, line_end);
}

/* A frame: from the blank to the picture's last line, so that what the game changes in the blank
   is in the frame's picture (and sms_redraw draws the same again). */
void sms_run_frame(bool draw) {
  int lpf = lines_per_frame();
  for (int n = 0; n < 2 * lpf; n++) {
    run_line(draw);
    if (++st.line >= lpf) st.line = 0;
    if (st.line == height()) break;
  }
  snap_lines = height();
  cpu.cyc -= line_end, line_end = 0; /* (the clocks from 0 again, with what the last instruction ran over) */
}

void sms_redraw(void) {
  uint8_t status = st.status;
  for (int line = 0; line < snap_lines; line++) draw_line(line, &snap[line], true);
  st.status = status;
}

void sms_set_pad(int p) {
  pad_bits = (uint8_t)((p & RG_PAD_UP ? 0x01 : 0) | (p & RG_PAD_DOWN ? 0x02 : 0) | (p & RG_PAD_LEFT ? 0x04 : 0) |
                       (p & RG_PAD_RIGHT ? 0x08 : 0) | (p & RG_PAD_B ? 0x10 : 0) | (p & RG_PAD_A ? 0x20 : 0));
  bool start = p & RG_PAD_START;
  if (gg_vdp) {
    start_button = start;
  } else { /* the Master System's pause button (or the Game Gear's Start in its mode): an NMI */
    if (start && !st.pause) z80_gen_nmi(&cpu);
    st.pause = start;
  }
}

uint32_t sms_frame_us(void) { return pal ? 20120 : 16688; } /* (313 or 262 lines of 228 clocks) */
bool sms_is_gg(void) { return gg; }
bool sms_is_sg(void) { return sg; }

void sms_reset(bool hard) {
  if (hard) {
    memset(ram, 0, 0x2000);
    memset(vram, 0, 0x4000);
    memset(&st, 0, sizeof st);
    /* the video chip as the BIOS leaves it (the SG-1000 has none: all 0) */
    static const uint8_t regs[11] = {0x36, 0x80, 0xFF, 0xFF, 0xFF, 0xFF, 0xFB, 0x00, 0x00, 0x00, 0xFF};
    if (!sg) memcpy(st.reg, regs, sizeof regs);
    st.counter = 0xFF;
    for (int i = 0; i < 32; i++) palette_update(i);
    memset(snap, 0, 240 * sizeof *snap);
    snap_lines = 0;
  }
  st.mapper[0] = 0, st.mapper[1] = 0, st.mapper[2] = 1;
  st.mapper[3] = mapper_kind == MAP_SEGA ? 2 : 0;
  st.line = 0, st.pause = 0;
  map_update();
  z80_init(&cpu);
  if (!sg) cpu.sp = 0xDFF0; /* (where the BIOS leaves it; a few games count on it) */
  line_start = line_end = 0;
  irq_update();
}

int sms_load(const uint8_t *data, uint32_t size, uint32_t crc, int machine) {
  if ((size & 0x3FFF) == 512 && size > 512) data += 512, size -= 512; /* (a copier's header) */
  if (size < 0x400) return RG_ERR_ROM;
  rg_alloc_reset();
  ram = rg_alloc(0x2000), vram = rg_alloc(0x4000), sram = rg_alloc(0x8000);
  expand = rg_alloc(256 * sizeof *expand), snap = rg_alloc(240 * sizeof *snap);
  if (!ram || !vram || !sram || !expand || !snap) return RG_ERR_MEMORY;
  for (int b = 0; b < 256; b++) {
    uint32_t v = 0;
    for (int i = 0; i < 8; i++)
      if (b & (0x80 >> i)) v |= 1u << (4 * i);
    expand[b] = v;
  }
  rom = data, rom_size = size, banks = (size + 0x3FFF) >> 14;
  uint8_t flags = 0;
  for (unsigned i = 0; i < sizeof special / sizeof special[0]; i++)
    if (special[i].crc == crc) flags = special[i].flags;
  /* Codemasters' games say so: a checksum at $7FE6 and its complement after it */
  if (size > 0x8000) {
    unsigned sum = data[0x7FE6] | data[0x7FE7] << 8, inv = data[0x7FE8] | data[0x7FE9] << 8;
    if (sum && sum + inv == 0x10000) flags |= DB_CODIES;
  }
  mapper_kind = flags & DB_CODIES ? MAP_CODIES : flags & DB_KOREA ? MAP_KOREA : MAP_SEGA;
  pal = flags & DB_PAL;
  gg = machine == SMS_GG, gg_vdp = gg && !(flags & DB_GGMS), sg = machine == SMS_SG;
  if (sg) pal = false;
  pad_bits = 0, start_button = false, sram_dirty = false;
  sms_reset(true);
  return RG_OK;
}

/* ------------------------------------------------------------ saves */
uint8_t *sms_sram(uint32_t *size) {
  *size = 0x8000;
  return sram;
}
bool sms_sram_used(void) { return st.sram_on; }
bool sms_sram_dirty(void) { return sram_dirty; }
void sms_sram_saved(void) { sram_dirty = false; }
void sms_sram_loaded(void) { st.sram_on = 1, sram_dirty = false; }

int sms_state_chunks(rg_chunk_t *out) {
  out[0] = (rg_chunk_t){&cpu, sizeof cpu};
  out[1] = (rg_chunk_t){&st, sizeof st};
  out[2] = (rg_chunk_t){ram, 0x2000};
  out[3] = (rg_chunk_t){vram, 0x4000};
  out[4] = (rg_chunk_t){sram, 0x8000};
  return 5;
}

void sms_state_loaded(void) {
  z80_fix(&cpu);
  if (cpu.cyc > 64) cpu.cyc = 0;
  line_start = line_end = 0;
  if (st.line >= lines_per_frame()) st.line = 0;
  st.addr &= 0x3FFF, st.code &= 3, st.pending &= 1, st.vint &= 1, st.hint &= 1;
  st.sram_on &= 1, st.pause &= 1;
  if (st.counter < -1 || st.counter > 255) st.counter = st.reg[10];
  map_update();
  for (int i = 0; i < 32; i++) palette_update(i);
  irq_update();
  /* (the picture under the menus: this state's registers on every line) */
  snap_lines = height();
  for (int line = 0; line < snap_lines; line++) snap_line(line);
}
