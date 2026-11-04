/* ce_emu's TI-OS: the routines of TI's boot code and OS that rom.S leaves to the host, done on CEmu's state.
 *
 * Variables are kept as TI-OS keeps them, so programs and the CE libraries that read memory directly see the
 * real layout: the VAT grows down from symTable (named variables, then programs and appvars from progPtr to
 * pTemp), RAM data lives from userMem up to the FP stack, and archived variables sit in flash from 0x0C0000
 * in TI's format (0xFC, size, type, version, address, name, data), their VAT pointer at the 0xFC. A running
 * program is a copy at userMem, asm_prgm_size long, outside the VAT.
 *
 * Each routine charges the cycles a real one would take, roughly: the float ones from the toolchain's notes
 * and the eZ80's speed (TI's boot code isn't public), the memory ones by the bytes they move. */
#include "ce_emu.h"
#include "cpu.h"
#include "debug/debug.h"
#include "mem.h"
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define R (cpu.registers)
#define F_C 0x01
#define F_N 0x02
#define F_PV 0x04
#define F_H 0x10
#define F_Z 0x40
#define F_S 0x80

#define E_UNDEFINED 141
#define E_MEMORY 142
#define E_ARCHIVED 175
#define E_ARCH_FULL 49

bool os_trace;

/* ---- memory ---- */
uint8_t *mem_at(uint32_t a, uint32_t n) {
  a &= 0xFFFFFF;
  if (a < mem.flash.size && a + n <= mem.flash.size) return mem.flash.block + a;
  if (a >= 0xD00000 && a + n <= 0xD00000 + SIZE_RAM) return mem.ram.block + (a - 0xD00000);
  return NULL;
}
uint8_t rd8(uint32_t a) {
  uint8_t *p = mem_at(a, 1);
  return p ? *p : 0;
}
uint32_t rd16(uint32_t a) { return rd8(a) | rd8(a + 1) << 8; }
uint32_t rd24(uint32_t a) { return rd8(a) | rd8(a + 1) << 8 | (uint32_t)rd8(a + 2) << 16; }
void wr8(uint32_t a, uint8_t v) {
  uint8_t *p = mem_at(a, 1);
  if (p) *p = v;
}
void wr16(uint32_t a, uint32_t v) { wr8(a, v), wr8(a + 1, v >> 8); }
void wr24(uint32_t a, uint32_t v) { wr8(a, v), wr8(a + 1, v >> 8), wr8(a + 2, v >> 16); }
static bool in_ram(uint32_t a) { return a >= 0xD00000 && a < 0xD40000; }
static void flag(uint8_t m, bool on) { R.F = on ? R.F | m : R.F & ~m; }
static uint32_t arg(int i) { return rd24(R.SPL + 3 + 3 * i); }
static uint32_t arg32(int i) { return arg(i) | (uint32_t)(arg(i + 1) & 0xFF) << 24; }

/* ---- names of the routines (ti84pceg.inc, when there is one) ---- */
static struct name { uint32_t addr; char *name; } *names;
static int nnames;
void os_names_load(const char *path) {
  FILE *f = fopen(path, "r");
  if (!f) return;
  char line[256];
  while (fgets(line, sizeof line, f)) {
    char n[128];
    unsigned a;
    if (line[0] != '?' || sscanf(line + 1, "%127s := %xh", n, &a) != 2) continue;
    if (a < 0x80 || (a >= 0x800 && a < 0x20100) || a >= 0x30000) continue;
    if (!(isupper((unsigned char)n[0]) || n[0] == '_' || !strncmp(n, "boot.", 5) || !strcmp(n, "sqrtf"))) continue;
    names = realloc(names, (nnames + 1) * sizeof *names);
    names[nnames++] = (struct name){a, strdup(n)};
  }
  fclose(f);
}
static const struct { uint32_t addr; const char *name; } BUILTIN[] = {
  {0x2014C, "GetCSC"}, {0x2050C, "ChkFindSym"}, {0x21D2C, "os_PutStrFull"}, {0x21D30, "os_PutStrLine"},
  {0x21D34, "os_SetCursorPos"}, {0x21D28, "os_GetCursorPos"}, {0x21ED4, "os_GetSystemInfo"},
  {0x21FF0, "os_MemChk"}, {0x22040, "os_ArcChk"}, {0x21FB8, "os_ChkFindSym"}, {0x22180, "os_GetAppVarData"},
  {0x22184, "os_CreateAppVar"}, {0x2217C, "os_DelAppVar"}, {0x21E00, "os_FontDrawText"}, {0x21E28, "os_FontSelect"},
};
const char *os_name(uint32_t addr) {
  for (int i = 0; i < nnames; i++)
    if (names[i].addr == addr) return names[i].name;
  for (size_t i = 0; i < sizeof BUILTIN / sizeof BUILTIN[0]; i++)
    if (BUILTIN[i].addr == addr) return BUILTIN[i].name;
  return "?";
}

/* ---- files ---- */
int ti_read_file(const char *path, tivar_t *out, int max) {
  FILE *f = fopen(path, "rb");
  if (!f) return -1;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  rewind(f);
  uint8_t *d = malloc(n > 0 ? n : 1);
  if (fread(d, 1, n, f) != (size_t)n || n < 57 || memcmp(d, "**TI83F*", 8)) {
    fclose(f), free(d);
    return -1;
  }
  fclose(f);
  uint32_t end = 55 + (d[53] | d[54] << 8), p = 55;
  int k = 0;
  while (p + 17 <= end && end <= (uint32_t)n && k < max) {
    uint32_t hl = d[p] | d[p + 1] << 8, q = p + 2 + hl, vl = d[q] | d[q + 1] << 8;
    tivar_t *v = &out[k++];
    memset(v, 0, sizeof *v);
    v->type = d[p + 4];
    memcpy(v->name, d + p + 5, 8);
    for (v->namelen = 0; v->namelen < 8 && v->name[v->namelen]; v->namelen++) {}
    if (hl >= 13) v->version = d[p + 13], v->flag = d[p + 14];
    v->size = vl;
    v->data = malloc(vl);
    memcpy(v->data, d + q + 2, vl);
    p = q + 2 + vl;
  }
  free(d);
  return k;
}

/* ---- the VAT ---- */
typedef struct {
  uint32_t vat, len, data;
  uint8_t type, t2, ver, namelen, name[8];
  bool prog; /* in the programs' part (length-prefixed name) */
} ent_t;

static bool prog_type(uint8_t t) {
  t &= 0x3F;
  return t == 1 || t == 5 || t == 6 || t == 0x0D || t == 0x15 || t == 0x16 || t == 0x17;
}
static int type_class(uint8_t t) {
  t &= 0x3F;
  return t == 6 || t == 0x16 ? 5 : t == 0x0D ? 1 : t;
}
static void ent_read(uint32_t vat, bool prog, ent_t *e) {
  e->vat = vat, e->prog = prog;
  e->type = rd8(vat), e->t2 = rd8(vat - 1), e->ver = rd8(vat - 2);
  e->data = rd8(vat - 3) | rd8(vat - 4) << 8 | (uint32_t)rd8(vat - 5) << 16;
  uint32_t at = vat - 6;
  e->namelen = prog ? rd8(at--) : 3;
  if (e->namelen > 8) e->namelen = 8;
  for (int i = 0; i < e->namelen; i++) e->name[i] = rd8(at--);
  e->len = prog ? 7 + e->namelen : 9;
}
/* each entry in turn: from symTable down, then from progPtr down */
static bool ent_next(ent_t *e, bool first) {
  uint32_t prog_ptr = rd24(PROG_PTR), p_temp = rd24(P_TEMP);
  uint32_t vat = first ? SYM_TABLE : e->vat - e->len;
  bool prog = first ? false : e->prog;
  if (!prog && vat <= prog_ptr) prog = true, vat = prog_ptr;
  if (prog && (vat <= p_temp || vat < USER_MEM)) return false;
  ent_read(vat, prog, e);
  return !prog || (e->namelen > 0 && e->len <= vat - p_temp);
}
static int ent_count;
static bool find(uint8_t type, const uint8_t *name, int namelen, ent_t *e) {
  ent_count = 0;
  for (bool ok = ent_next(e, true); ok; ok = ent_next(e, false)) {
    ent_count++;
    if (e->prog != prog_type(type)) continue;
    if (e->prog) {
      if (e->namelen != namelen || memcmp(e->name, name, namelen) || type_class(e->type) != type_class(type)) continue;
    } else if (memcmp(e->name, name, 3)) {
      continue;
    }
    return true;
  }
  return false;
}
static int op1_name(uint8_t *name) {
  int n = 0;
  memset(name, 0, 8);
  while (n < 8 && (name[n] = rd8(OP1 + 1 + n))) n++;
  return n;
}
static bool find_op1(ent_t *e) {
  uint8_t name[8];
  int n = op1_name(name);
  return find(rd8(OP1), name, n, e);
}
static bool archived(const ent_t *e) { return !in_ram(e->data) && !(e->data >= VRAM && e->data < 0xE00000); }
/* where an archived entry's data (its size first) starts */
static uint32_t arc_data(const ent_t *e) { return e->data + 9 + e->prog + e->namelen; }
static uint32_t data_of(const ent_t *e) { return archived(e) ? arc_data(e) : e->data; }
/* the bytes of a variable's data, as TI-OS counts them */
static uint32_t data_size(uint8_t type, uint32_t p) {
  switch (type & 0x3F) {
  case 0x00: case 0x18: case 0x19: case 0x1C: case 0x20: case 0x21: return 9;
  case 0x0C: case 0x1B: case 0x1D: case 0x1E: case 0x1F: return 18;
  case 0x01: return 2 + rd16(p) * 9;
  case 0x0D: return 2 + rd16(p) * 18;
  case 0x02: return 2 + rd8(p) * rd8(p + 1) * 9;
  default: return 2 + rd16(p);
  }
}
static uint32_t fixed_size(uint8_t type) {
  switch (type & 0x3F) {
  case 0x00: case 0x18: case 0x19: case 0x1C: case 0x20: case 0x21: return 9;
  case 0x0C: case 0x1B: case 0x1D: case 0x1E: case 0x1F: return 18;
  default: return 0;
  }
}
static void ent_set_data(const ent_t *e, uint32_t data) { wr8(e->vat - 3, data), wr8(e->vat - 4, data >> 8), wr8(e->vat - 5, data >> 16); }

/* ---- RAM: the variables' data from userMem, the FP stack after it, then free RAM, the OP stack, the VAT ---- */
static uint32_t ram_free(void) { return rd24(OPS) - rd24(FPS); }
static void move_ptr(uint32_t addr, uint32_t at, int32_t n) {
  uint32_t p = rd24(addr);
  if (p < at) return;
  if (n < 0 && p < at - n) p = at;
  else p += n;
  wr24(addr, p);
}
static void move_vars(uint32_t at, int32_t n) {
  static const uint32_t PTRS[] = {TEMP_MEM, FPS_BASE, FPS, NEW_DATA_PTR};
  for (size_t i = 0; i < sizeof PTRS / sizeof PTRS[0]; i++) move_ptr(PTRS[i], at, n);
  ent_t e;
  for (bool ok = ent_next(&e, true); ok; ok = ent_next(&e, false)) {
    if (!in_ram(e.data) || e.data < at) continue;
    ent_set_data(&e, n < 0 && e.data < at - n ? at : e.data + n);
  }
}
static bool ram_insert(uint32_t at, uint32_t n) {
  uint32_t fps = rd24(FPS);
  if (!n) return true;
  if (at < USER_MEM || at > fps) {
    emu_stop("crash", "InsertMem at %06X, outside the variables' memory (%06X-%06X)", at, USER_MEM, fps);
    return false;
  }
  if (n > ram_free()) {
    emu_stop("crash", "InsertMem of %u bytes with %u free (not checked with EnoughMem)", n, ram_free());
    return false;
  }
  memmove(mem_at(at + n, fps - at), mem_at(at, fps - at), fps - at);
  move_vars(at, (int32_t)n);
  emu_charge(200 + 4 * (fps - at));
  return true;
}
static bool ram_delete(uint32_t at, uint32_t n) {
  uint32_t fps = rd24(FPS);
  if (!n) return true;
  if (at < USER_MEM || at + n > fps) {
    emu_stop("crash", "DelMem of %u bytes at %06X, outside the variables' memory (%06X-%06X)", n, at, USER_MEM, fps);
    return false;
  }
  memmove(mem_at(at, fps - at - n), mem_at(at + n, fps - at - n), fps - at - n);
  move_vars(at, -(int32_t)n);
  emu_charge(200 + 4 * (fps - at - n));
  return true;
}
/* room for a VAT entry of len bytes whose top is at `top` (pTemp or progPtr): what is below moves down */
static bool vat_open(uint32_t top, uint32_t len, bool symbol) {
  uint32_t ops = rd24(OPS);
  if (len > ram_free()) return false;
  memmove(mem_at(ops - len, top - ops + 1), mem_at(ops, top - ops + 1), top - ops + 1);
  if (symbol) wr24(PROG_PTR, rd24(PROG_PTR) - len);
  wr24(P_TEMP, rd24(P_TEMP) - len);
  wr24(OP_BASE, rd24(OP_BASE) - len);
  wr24(OPS, ops - len);
  return true;
}
static void vat_close(const ent_t *e) {
  uint32_t ops = rd24(OPS), below = e->vat - e->len;
  memmove(mem_at(ops + e->len, below - ops + 1), mem_at(ops, below - ops + 1), below - ops + 1);
  if (!e->prog) wr24(PROG_PTR, rd24(PROG_PTR) + e->len);
  wr24(P_TEMP, rd24(P_TEMP) + e->len);
  wr24(OP_BASE, rd24(OP_BASE) + e->len);
  wr24(OPS, ops + e->len);
}
static uint32_t vat_write(uint8_t type, uint8_t ver, const uint8_t *name, int namelen, uint32_t data) {
  bool prog = prog_type(type);
  uint32_t len = prog ? 7 + namelen : 9, top = prog ? rd24(P_TEMP) : rd24(PROG_PTR);
  if (!vat_open(top, len, !prog)) return 0;
  wr8(top, type), wr8(top - 1, 0), wr8(top - 2, ver);
  wr8(top - 3, data), wr8(top - 4, data >> 8), wr8(top - 5, data >> 16);
  uint32_t at = top - 6;
  if (prog) wr8(at--, namelen);
  for (int i = 0; i < (prog ? namelen : 3); i++) wr8(at--, i < namelen ? name[i] : 0);
  return top;
}

/* ---- the archive ---- */
static uint32_t arc_free = ARC_START;
static int arc_gcs;
static uint32_t arc_entry_len(const ent_t *e, uint32_t size) { return 9 + e->prog + e->namelen + size; }
static bool flash_write(uint32_t at, const uint8_t *src, uint32_t n) {
  uint8_t *p = mem_at(at, n);
  if (!p || at < ARC_START || at + n > ARC_END) return false;
  for (uint32_t i = 0; i < n; i++) p[i] &= src[i]; /* NOR: programming clears bits */
  return true;
}
/* a spot for n bytes, the next sector when this one hasn't the room */
static uint32_t arc_spot(uint32_t n) {
  uint32_t at = arc_free;
  if ((at & 0xFFFF) == 0) at++;
  if ((at & 0xFFFF) + n > 0x10000 && n <= 0xFFFF) at = (at | 0xFFFF) + 2;
  return at + n <= ARC_END ? at : 0;
}
static uint32_t arc_put(const ent_t *e, const uint8_t *data, uint32_t size) {
  uint32_t n = arc_entry_len(e, size), at = arc_spot(n);
  if (!at) return 0;
  uint8_t hdr[32], *h = hdr;
  *h++ = 0xFC, *h++ = (n - 3), *h++ = (n - 3) >> 8;
  *h++ = e->type, *h++ = e->t2, *h++ = e->ver, *h++ = 0, *h++ = 0, *h++ = 0;
  if (e->prog) *h++ = e->namelen;
  memcpy(h, e->name, e->namelen), h += e->namelen;
  uint8_t sector = 0xF0;
  flash_write(at & ~0xFFFF, &sector, 1);
  flash_write(at, hdr, h - hdr);
  flash_write(at + (h - hdr), data, size);
  arc_free = at + n;
  emu_charge(5000 + 2400 * n); /* (flash writes: some 20 KB a second) */
  return at;
}
/* garbage collection: the entries still in use, packed again from the start; their VAT pointers follow */
static void arc_collect(void) {
  struct { uint32_t vat, data, size; bool prog; } *list = NULL;
  int n = 0;
  ent_t e;
  for (bool ok = ent_next(&e, true); ok; ok = ent_next(&e, false)) {
    if (!archived(&e) || e.data < ARC_START || e.data >= ARC_END) continue;
    list = realloc(list, (n + 1) * sizeof *list);
    list[n].vat = e.vat, list[n].prog = e.prog, list[n].data = arc_data(&e) - ARC_START;
    list[n++].size = data_size(e.type, arc_data(&e));
  }
  size_t cap = ARC_END - ARC_START;
  uint8_t *copy = malloc(cap);
  memcpy(copy, mem_at(ARC_START, cap), cap);
  memset(mem_at(ARC_START, cap), 0xFF, cap);
  arc_free = ARC_START;
  for (int i = 0; i < n; i++) {
    ent_read(list[i].vat, list[i].prog, &e);
    ent_set_data(&e, arc_put(&e, copy + list[i].data, list[i].size));
  }
  free(copy), free(list);
  arc_gcs++;
  emu_charge(48000000); /* about a second */
  emu_warn("archive garbage collected");
}
static bool arc_has_room(uint32_t n) { return arc_spot(n) != 0; }

/* ---- errors: TI-OS's error handlers (PushErrorHandler) or the program's end ---- */
static const char *err_name(int code) {
  switch (code & 0x7F) {
  case 13: return "UNDEFINED";
  case 14: return "MEMORY";
  case 47: return "ARCHIVED";
  case 49: return "ARCHIVE FULL";
  default: return "?";
  }
}
static void os_throw(int code) {
  uint32_t sp = rd24(ERR_SP);
  wr8(ERR_NO, code);
  if (!sp) {
    emu_stop("os-error", "ERR:%s (%d) with no error handler: TI-OS would end the program", err_name(code), code);
    return;
  }
  /* the frame PushErrorHandler made: old errSP, OPBase-OPS, FPS-FPSbase, the handler */
  wr24(ERR_SP, rd24(sp));
  wr24(OPS, rd24(OP_BASE) - rd24(sp + 3));
  wr24(FPS, rd24(FPS_BASE) + rd24(sp + 6));
  uint32_t handler = rd24(sp + 9);
  R.SPL = sp + 12;
  R.A = code;
  if (os_trace) fprintf(stderr, "[os] error %d to the handler at %06X\n", code, handler);
  emu_jump(handler);
}

/* ---- the home screen: TI-OS's 26 x 10 cells under the status bar, drawn with NumPlay's font ---- */
#include "../../launcher/src/font.h"
#define HOME_COLS 26
#define HOME_ROWS 10
static char line_buf[512];
static int line_len;
static void text_flush(void) {
  if (!line_len) return;
  line_buf[line_len] = 0;
  emu_text(line_buf);
  line_len = 0;
}
static void vram_fill(uint32_t y0, uint32_t y1, uint16_t c) {
  for (uint32_t i = y0 * 320; i < y1 * 320; i++) wr16(VRAM + 2 * i, c);
}
static void draw_cell(int row, int col, char ch) {
  const np_font_t *f = &np_font_body;
  int cw = 12, chh = 20, x0 = 4 + col * cw, y0 = 32 + row * chh;
  for (int y = 0; y < chh; y++)
    for (int x = 0; x < cw; x++) wr16(VRAM + 2 * ((y0 + y) * 320 + x0 + x), 0xFFFF);
  unsigned k = (uint8_t)ch;
  if (k < f->first || k >= (unsigned)f->first + f->count) return;
  const np_glyph_t *g = &f->glyphs[k - f->first];
  int pen = (cw - g->advance) / 2, base = (chh + f->ascent - f->descent) / 2;
  const uint8_t *d = f->data + g->offset;
  for (int j = 0; j < g->h; j++)
    for (int i = 0; i < g->w; i++) {
      int n = j * g->w + i, a = (d[n >> 1] >> ((n & 1) * 4)) & 15, cx = pen + g->x + i, cy = base + g->y + j;
      if (a < 6 || cx < 0 || cx >= cw || cy < 0 || cy >= chh) continue;
      uint16_t v = (uint16_t)(31 - a * 2), c = (uint16_t)(v << 11 | (v * 2) << 5 | v);
      wr16(VRAM + 2 * ((y0 + cy) * 320 + x0 + cx), c);
    }
}
static void home_scroll(void) {
  memmove(mem_at(TEXT_SHADOW, HOME_COLS * HOME_ROWS), mem_at(TEXT_SHADOW + HOME_COLS, HOME_COLS * (HOME_ROWS - 1)),
          HOME_COLS * (HOME_ROWS - 1));
  memset(mem_at(TEXT_SHADOW + HOME_COLS * (HOME_ROWS - 1), HOME_COLS), ' ', HOME_COLS);
  for (int r = 0; r < HOME_ROWS; r++)
    for (int c = 0; c < HOME_COLS; c++) draw_cell(r, c, rd8(TEXT_SHADOW + r * HOME_COLS + c));
}
static void new_line(void) {
  text_flush();
  wr8(CUR_COL, 0);
  if (rd8(CUR_ROW) + 1 >= HOME_ROWS) home_scroll();
  else wr8(CUR_ROW, rd8(CUR_ROW) + 1);
}
static void put_char(uint8_t c) {
  if (line_len < (int)sizeof line_buf - 1) line_buf[line_len++] = c >= 32 && c < 127 ? c : '?';
  int row = rd8(CUR_ROW), col = rd8(CUR_COL);
  if (row >= HOME_ROWS) row = HOME_ROWS - 1;
  if (col >= HOME_COLS) {
    new_line();
    row = rd8(CUR_ROW), col = 0;
  }
  wr8(TEXT_SHADOW + row * HOME_COLS + col, c);
  draw_cell(row, col, c);
  wr8(CUR_COL, col + 1);
  emu_charge(2500);
}
static uint32_t put_string(uint32_t s, bool wrap) {
  for (int n = 0; n < 4096; n++, s++) {
    uint8_t c = rd8(s);
    if (!c) break;
    if (!wrap && rd8(CUR_COL) >= HOME_COLS) continue;
    put_char(c);
  }
  return s;
}
static void clear_home(void) {
  memset(mem_at(TEXT_SHADOW, HOME_COLS * HOME_ROWS), ' ', HOME_COLS * HOME_ROWS);
}

/* ---- printf for boot_sprintf: the CE's 3-byte int arguments ---- */
static int ce_format(char *out, size_t cap, uint32_t fmt, int argi) {
  size_t o = 0;
  for (int n = 0; n < 1024; n++, fmt++) {
    char c = rd8(fmt), tmp[64], spec[16];
    if (!c) break;
    if (c != '%') {
      if (o + 1 < cap) out[o++] = c;
      continue;
    }
    int s = 0;
    spec[s++] = '%';
    for (c = rd8(++fmt); c && strchr("-+ #0123456789.", c) && s < 10; c = rd8(++fmt)) spec[s++] = c;
    bool l = c == 'l';
    if (l) c = rd8(++fmt);
    if (c == 'h') c = rd8(++fmt);
    tmp[0] = 0;
    if (c == '%') {
      strcpy(tmp, "%");
    } else if (c == 's') {
      char str[256];
      uint32_t p = arg(argi++);
      int k = 0;
      while (k < 255 && (str[k] = rd8(p + k))) k++;
      str[k] = 0;
      spec[s++] = 's', spec[s] = 0;
      snprintf(tmp, sizeof tmp, spec, str);
    } else if (c == 'c') {
      tmp[0] = arg(argi++), tmp[1] = 0;
    } else if (strchr("diuxXop", c)) {
      int64_t v = l ? arg32(argi) : arg(argi);
      argi += l ? 2 : 1;
      if (c == 'd' || c == 'i') v = l ? (int32_t)v : (v & 0x800000 ? (int64_t)v - 0x1000000 : v);
      spec[s++] = 'l', spec[s++] = 'l', spec[s++] = c == 'p' ? 'x' : c, spec[s] = 0;
      snprintf(tmp, sizeof tmp, spec, (long long)v);
    }
    for (char *t = tmp; *t; t++)
      if (o + 1 < cap) out[o++] = *t;
  }
  out[o] = 0;
  return (int)o;
}

/* ---- floats: TI's boot code works on A:UBC and E:UHL ---- */
static float as_float(uint32_t b) {
  float f;
  memcpy(&f, &b, 4);
  return f;
}
static uint32_t bits_of(float f) {
  uint32_t b;
  memcpy(&b, &f, 4);
  return b;
}
static float f_abc(void) { return as_float((uint32_t)R.A << 24 | (R.BC & 0xFFFFFF)); }
static float f_ehl(void) { return as_float((uint32_t)R.E << 24 | (R.HL & 0xFFFFFF)); }
static void set_abc(uint32_t b) { R.A = b >> 24, R.BC = b & 0xFFFFFF; }
static void ret_float(float f) { /* a C function's float: E:UHL */
  uint32_t b = bits_of(f);
  R.E = b >> 24, R.HL = b & 0xFFFFFF;
}
/* long to float, ties away from zero (as the toolchain says TI's __ltof rounds) */
static float ltof(int32_t x) {
  uint32_t v = x < 0 ? 0u - (uint32_t)x : (uint32_t)x;
  int top = 31;
  while (top > 0 && !(v >> top)) top--;
  if (top > 23) {
    int sh = top - 23;
    uint32_t m = v >> sh, rem = v & ((1u << sh) - 1);
    if (rem >= 1u << (sh - 1)) m++;
    return (x < 0 ? -1.0f : 1.0f) * ldexpf((float)m, sh);
  }
  return (float)x;
}
static int32_t ftol(float f) {
  if (isnan(f)) return 0;
  if (f >= 2147483648.0f) return INT32_MAX;
  if (f < -2147483648.0f) return INT32_MIN;
  return (int32_t)f;
}
/* x in A:UBC, y in E:UHL: flags as for y - x (as the toolchain's __dcmp): z when equal, s (and c) when y < x;
 * a NaN by its bits */
static int64_t fkey(float f) {
  uint32_t b = bits_of(f);
  return b >> 31 ? -(int64_t)(b & 0x7FFFFFFF) : (int64_t)(b & 0x7FFFFFFF);
}
static void fcmp(float x, float y) {
  uint8_t f = R.F & ~(F_C | F_Z | F_S | F_PV | F_N | F_H);
  bool less = isnan(x) || isnan(y) ? fkey(y) < fkey(x) : y < x;
  if (x == y) f |= F_Z;
  else if (less) f |= F_S | F_C;
  R.F = f;
}

/* ---- the program ---- */
static uint8_t prog_name[8], prog_namelen;
static uint32_t calls[0x30000 / 4];
static uint32_t unknown_calls;
static int csc_last = -1;
static uint64_t csc_repeat_at;
/* the CE's keys, TI's scan codes ((7 - group) * 8 + bit + 1) */
static int scan_code(void) {
  for (int g = 1; g <= 7; g++)
    for (int b = 0; b < 8; b++)
      if (emu_key_held(g, b)) return (7 - g) * 8 + b + 1;
  return 0;
}
static uint8_t get_csc(void) {
  int k = scan_code();
  uint64_t now = emu_ms();
  if (!k) {
    csc_last = -1;
    return 0;
  }
  if (k != csc_last) {
    csc_last = k, csc_repeat_at = now + 500;
    return k;
  }
  if (k <= 4 && now >= csc_repeat_at) { /* arrows repeat */
    csc_repeat_at = now + 100;
    return k;
  }
  return 0;
}
/* GetKey's codes for the scan codes */
static uint8_t key_code(uint8_t sk) {
  static const uint8_t K[57] = {
    [1] = 4, [2] = 2, [3] = 1, [4] = 3, [9] = 5, [10] = 0x80, [11] = 0x81, [12] = 0x82, [13] = 0x83, [14] = 0x84,
    [15] = 9, [17] = 0x8C, [18] = 0x90, [19] = 0x93, [20] = 0x96, [21] = 0x85, [25] = 0x8D, [26] = 0x8F, [27] = 0x92,
    [28] = 0x95, [33] = 0x8E, [34] = 0x91, [35] = 0x94, [36] = 0x97, [51] = 0x45, [55] = 0x45, [56] = 0x0A,
  };
  return sk < sizeof K && K[sk] ? K[sk] : sk;
}

/* ---- setting up: the variables, then the program to run ---- */
void os_setup(void) {
  wr24(TEMP_MEM, USER_MEM), wr24(FPS_BASE, USER_MEM), wr24(FPS, USER_MEM), wr24(NEW_DATA_PTR, USER_MEM);
  wr24(PROG_PTR, SYM_TABLE), wr24(P_TEMP, SYM_TABLE), wr24(OP_BASE, SYM_TABLE), wr24(OPS, SYM_TABLE);
  wr24(ERR_SP, 0), wr24(ASM_PRGM_SIZE, 0);
  clear_home();
  arc_free = ARC_START;
}
bool os_add_var(const tivar_t *v, bool to_archive) {
  ent_t e = {0};
  if (find(v->type, v->name, v->namelen, &e)) return false;
  memset(&e, 0, sizeof e);
  e.type = v->type & 0x3F, e.ver = v->version, e.namelen = prog_type(v->type) ? v->namelen : 3, e.prog = prog_type(v->type);
  memcpy(e.name, v->name, 8);
  if (to_archive) {
    uint32_t at = arc_put(&e, v->data, v->size);
    return at && vat_write(e.type, e.ver, v->name, v->namelen, at);
  }
  uint32_t at = rd24(FPS_BASE);
  if (!ram_insert(at, v->size)) return false;
  memcpy(mem_at(at, v->size), v->data, v->size);
  return vat_write(e.type, e.ver, v->name, v->namelen, at) != 0;
}
bool os_set_program(const char *name) {
  memset(prog_name, 0, 8);
  prog_namelen = (uint8_t)strnlen(name, 8);
  memcpy(prog_name, name, prog_namelen);
  ent_t e;
  return find(6, prog_name, prog_namelen, &e);
}
/* the program copied to userMem, as Asm( or a shell does */
static void run_program(void) {
  ent_t e;
  if (!find(6, prog_name, prog_namelen, &e)) {
    emu_stop("error", "no program %.8s", prog_name);
    return;
  }
  uint32_t d = data_of(&e), size = rd16(d);
  if (size < 3 || rd8(d + 2) != 0xEF || rd8(d + 3) != 0x7B) {
    emu_stop("error", "%.8s isn't an assembly program (no EF 7B)", prog_name);
    return;
  }
  uint32_t n = size - 2;
  if (!ram_insert(USER_MEM, n)) return;
  find(6, prog_name, prog_namelen, &e); /* (a program in RAM moved up) */
  memcpy(mem_at(USER_MEM, n), mem_at(data_of(&e) + 4, n), n);
  wr24(ASM_PRGM_SIZE, n);
  wr8(OP1, e.type & 0x3F);
  for (int i = 0; i < 8; i++) wr8(OP1 + 1 + i, i < prog_namelen ? prog_name[i] : 0);
  /* the stack, marked to measure how deep it goes */
  for (uint32_t a = STACK_BOT; a < R.SPL - 16; a++) wr8(a, 0xA5);
  emu_program_started();
}
int os_stack_used(void) {
  uint32_t a = STACK_BOT;
  while (a < STACK_TOP && rd8(a) == 0xA5) a++;
  return STACK_TOP - a;
}

/* ---- the routines ---- */
static void ret_found(const ent_t *e) {
  R.HL = e->vat, R.DE = e->data, R.A = e->type & 0x3F, R.B = archived(e) ? (e->data >> 16) & 0xFF : 0;
  flag(F_C, false);
}
static uint32_t chk_mem(uint32_t n) { return n <= ram_free(); }

bool os_trap(uint32_t pc) {
  bool boot = pc >= 0x80 && pc < 0x800, osj = pc >= 0x20100 && pc < HLE_BASE, own = pc >= HLE_BASE && pc < HLE_BASE + 4 * HLE_SLOTS;
  if (!(boot || osj || own) || (pc & 3)) return false;
  if (pc / 4 < sizeof calls / sizeof calls[0]) calls[pc / 4]++;
  if (os_trace && !own) fprintf(stderr, "[os] %6llu ms %06X %s\n", (unsigned long long)emu_ms(), pc, os_name(pc));
  ent_t e;
  uint8_t name[8];
  switch (pc) {
  /* the host's own */
  case HLE_BASE + 4 * HLE_START: run_program(); return true;
  case HLE_BASE + 4 * HLE_EXIT:
    text_flush();
    ram_delete(USER_MEM, rd24(ASM_PRGM_SIZE));
    wr24(ASM_PRGM_SIZE, 0);
    emu_program_exited(R.HL & 0xFFFFFF);
    return true;
  case HLE_BASE + 4 * HLE_GETKEY_CODE: R.A = key_code(R.A); return true;

  /* boot code */
  case 0x080: R.A = 5; return true;          /* boot.GetBootVerMajor */
  case 0x084: R.A = 0; return true;          /* GetHardwareVers */
  case 0x08C: R.A = 3; return true;          /* GetBootVerMinor */
  case 0x090: R.A = 6; return true;          /* GetBootVerBuild */
  case 0x0B4: {                             /* boot_DebugPrintf */
    char buf[512];
    ce_format(buf, sizeof buf, arg(0), 1);
    fprintf(stderr, "[dbg] %s", buf);
    return true;
  }
  case 0x0BC: {                             /* boot_sprintf */
    char buf[1024];
    int n = ce_format(buf, sizeof buf, arg(1), 2);
    uint8_t *p = mem_at(arg(0), n + 1);
    if (p) memcpy(p, buf, n + 1);
    R.HL = n;
    emu_charge(3000 + 400 * n);
    return true;
  }
  case 0x270: set_abc(bits_of(f_abc() + f_ehl())), emu_charge(350); return true;  /* __fadd */
  case 0x274: fcmp(f_abc(), f_ehl()), emu_charge(110); return true;             /* __fcmp */
  case 0x278: set_abc(bits_of(f_abc() / f_ehl())), emu_charge(1100); return true; /* __fdiv */
  case 0x27C: set_abc((uint32_t)ftol(f_abc())), emu_charge(170); return true;   /* __ftol */
  case 0x284: set_abc(bits_of(ltof((int32_t)((uint32_t)R.A << 24 | (R.BC & 0xFFFFFF))))), emu_charge(150); return true;
  case 0x288: set_abc(bits_of(f_abc() * f_ehl())), emu_charge(450); return true;  /* __fmul */
  case 0x290: set_abc(bits_of(f_abc() - f_ehl())), emu_charge(370); return true;  /* __fsub */
  case 0x298: ret_float(sqrtf(as_float(arg32(0)))), emu_charge(2500); return true;
  case 0x374: vram_fill(0, 240, 0xFFFF), emu_charge(200000); return true;  /* boot.ClearVRAM */
  case 0x378: R.HL = put_string(R.HL, true); return true;                   /* boot.PutS */
  case 0x384: case 0x388: return true;                                      /* Initialize/TurnOffHardware */
  case 0x390: new_line(); return true;                                      /* boot.NewLine */
  case 0x3B4: emu_charge(480000); return true;                              /* Delay10ms */

  /* the OS: keys, the home screen */
  case 0x2014C: R.A = get_csc(), wr8(KBD_SCAN_CODE, 0), emu_charge(300); return true; /* GetCSC */
  case 0x207B8: put_char(R.A); return true;                                       /* PutC */
  case 0x207C0: R.HL = put_string(R.HL, true); return true;                       /* PutS */
  case 0x207C8: {                                                                 /* PutPS */
    uint32_t p = R.HL & 0xFFFFFF, n = rd8(p);
    for (uint32_t i = 0; i < n; i++) put_char(rd8(p + 1 + i));
    R.HL = p + 1 + n;
    return true;
  }
  case 0x207F0: new_line(); return true;                                          /* NewLine */
  case 0x207F4: if (rd8(CUR_ROW) + 1 < HOME_ROWS) wr8(CUR_ROW, rd8(CUR_ROW) + 1); return true; /* MoveDown */
  case 0x20800: if (rd8(CUR_ROW)) wr8(CUR_ROW, rd8(CUR_ROW) - 1); return true;   /* MoveUp */
  case 0x20808: case 0x2080C: text_flush(), vram_fill(0, 240, 0xFFFF), emu_charge(330000); return true; /* ClrLCD(Full) */
  case 0x20814: case 0x20810:                                                     /* ClrScrn(Full) */
    text_flush(), clear_home(), vram_fill(30, 240, 0xFFFF), emu_charge(300000);
    return true;
  case 0x20818: clear_home(); return true;                                        /* ClrTxtShd */
  case 0x20828: text_flush(), wr8(CUR_ROW, 0), wr8(CUR_COL, 0); return true;      /* HomeUp */
  case 0x20844: case 0x20848: case 0x21134: case 0x21138: return true;            /* RunIndic, APD */
  case 0x21A3C: vram_fill(0, 30, 0x52AA), emu_charge(150000); return true;        /* DrawStatusBar */
  case 0x21D2C: case 0x21D30:                                                     /* os_PutStrFull, os_PutStrLine */
    put_string(arg(0), pc == 0x21D2C), R.HL = 1;
    return true;
  case 0x21D34: wr8(CUR_ROW, arg(0)), wr8(CUR_COL, arg(1)); return true;          /* os_SetCursorPos */
  case 0x21D28:                                                                   /* os_GetCursorPos */
    if (arg(0)) wr24(arg(0), rd8(CUR_ROW));
    if (arg(1)) wr24(arg(1), rd8(CUR_COL));
    return true;
  case 0x20164: /* JForceCmd: an app's end */
    emu_program_exited(0);
    return true;

  /* variables */
  case 0x2050C: /* ChkFindSym */
  case 0x20510: /* FindSym */
    if (pc == 0x20510) {
      op1_name(name);
      uint8_t t = rd8(OP1) & 0x3F;
      bool ok = find(prog_type(t) ? 0 : t, name, 3, &e);
      if (ok) ret_found(&e);
      else flag(F_C, true);
    } else if (find_op1(&e)) {
      ret_found(&e);
    } else {
      flag(F_C, true);
    }
    emu_charge(400 + 120 * ent_count);
    return true;
  case 0x21F98: /* ChkInRam: z if DE is in RAM */
    flag(F_Z, in_ram(R.DE & 0xFFFFFF)), flag(F_C, !in_ram(R.DE & 0xFFFFFF));
    return true;
  case 0x20FF4: /* AnsName */
    wr8(OP1, 0), wr8(OP1 + 1, 0x72);
    for (int i = 2; i < 9; i++) wr8(OP1 + i, 0);
    return true;
  case 0x204FC: R.HL = ram_free(), R.DE = rd24(FPS); return true; /* MemChk */
  case 0x2051C: /* EnoughMem: carry if HL bytes aren't free */
    R.DE = R.HL;
    flag(F_C, !chk_mem(R.HL & 0xFFFFFF));
    return true;
  case 0x2072C: /* ErrNotEnoughMem */
    R.DE = R.HL;
    if (!chk_mem(R.HL & 0xFFFFFF)) os_throw(E_MEMORY);
    flag(F_C, false);
    return true;
  case 0x20768: os_throw(E_MEMORY); return true; /* ErrMemory */
  case 0x20790: os_throw(R.A); return true;      /* JError */
  case 0x20514: ram_insert(R.DE & 0xFFFFFF, R.HL & 0xFFFFFF); return true; /* InsertMem */
  case 0x20590: ram_delete(R.HL & 0xFFFFFF, R.DE & 0xFFFFFF); return true; /* DelMem */
  case 0x205AC: R.DE = data_size(R.A, R.HL & 0xFFFFFF); return true;      /* DataSize */
  case 0x20798: { /* PushErrorHandler: a frame under the return address */
    uint32_t sp = R.SPL, ret = rd24(sp), fs = sp + 3 - 12;
    wr24(fs + 9, R.HL & 0xFFFFFF);
    wr24(fs + 6, rd24(FPS) - rd24(FPS_BASE));
    wr24(fs + 3, rd24(OP_BASE) - rd24(OPS));
    wr24(fs, rd24(ERR_SP));
    wr24(ERR_SP, fs);
    wr24(fs - 3, ret);
    R.SPL = fs - 3;
    return true;
  }
  case 0x2079C: { /* PopErrorHandler */
    uint32_t sp = R.SPL, ret = rd24(sp), frame = sp + 3;
    if (rd24(ERR_SP) != frame) emu_warn("PopErrorHandler without its PushErrorHandler's frame on top");
    wr24(ERR_SP, rd24(frame));
    wr24(frame + 9, ret);
    R.SPL = frame + 9;
    return true;
  }
  case 0x21338: { /* CreateVar: A type, OP1 name, HL size */
    uint8_t t = R.A & 0x3F;
    int n = op1_name(name);
    uint32_t size = R.HL & 0xFFFF, total = fixed_size(t) ? fixed_size(t) : size + 2;
    if (prog_type(t) && !n) {
      emu_stop("crash", "CreateVar with an empty name");
      return true;
    }
    if (total + 7 + n > ram_free()) {
      os_throw(E_MEMORY);
      return true;
    }
    uint32_t at = rd24(FPS_BASE);
    ram_insert(at, total);
    if (total == size + 2) wr16(at, size);
    else memset(mem_at(at, total), 0, total);
    uint32_t vat = vat_write(t, 0, name, prog_type(t) ? n : 3, at);
    for (int i = 0; i < 9; i++) wr8(OP4 + i, rd8(OP1 + i));
    R.HL = vat, R.DE = at, R.BC = size;
    emu_charge(3000);
    return true;
  }
  case 0x21434: /* DelVarArc: HL VAT entry, DE data */
  case 0x21430: { /* DelVar */
    uint32_t vat = R.HL & 0xFFFFFF;
    bool prog = vat <= rd24(PROG_PTR);
    ent_read(vat, prog, &e);
    if (archived(&e)) {
      uint8_t deleted = 0xF0;
      flash_write(e.data, &deleted, 1);
    } else {
      ram_delete(e.data, data_size(e.type, e.data));
    }
    vat_close(&e);
    emu_charge(5000);
    return true;
  }
  case 0x21448: { /* Arc_Unarc: the variable named by OP1 */
    if (!find_op1(&e)) {
      os_throw(E_UNDEFINED);
      return true;
    }
    if (archived(&e)) {
      uint32_t d = arc_data(&e), size = data_size(e.type, d);
      if (size > ram_free()) {
        os_throw(E_MEMORY);
        return true;
      }
      uint32_t at = rd24(FPS_BASE), entry = e.data;
      ram_insert(at, size);
      memcpy(mem_at(at, size), mem_at(d, size), size);
      find_op1(&e);
      ent_set_data(&e, at);
      uint8_t deleted = 0xF0;
      flash_write(entry, &deleted, 1);
      emu_charge(20000 + 8 * size);
    } else {
      uint32_t size = data_size(e.type, e.data), n = arc_entry_len(&e, size);
      if (!arc_has_room(n)) arc_collect(), find_op1(&e);
      if (!arc_has_room(n)) {
        os_throw(E_ARCH_FULL);
        return true;
      }
      uint32_t at = arc_put(&e, mem_at(e.data, size), size);
      ram_delete(e.data, size);
      find_op1(&e);
      ent_set_data(&e, at);
    }
    return true;
  }
  case 0x22078: /* FindFreeArcSpot: nz if BC bytes fit without a garbage collection */
    flag(F_Z, !arc_has_room(R.BC & 0xFFFFFF));
    R.DE = arc_spot(R.BC & 0xFFFFFF);
    return true;
  case 0x21FF0: /* os_MemChk(void **free) */
    if (arg(0)) wr24(arg(0), rd24(FPS));
    R.HL = ram_free();
    return true;
  case 0x22040: /* os_ArcChk */
    R.HL = ARC_END - arc_free;
    return true;

  /* the C library's float functions in the OS */
  case 0x220D8: { int ex; float m = frexpf(as_float(arg32(0)), &ex); if (arg(2)) wr24(arg(2), (uint32_t)ex); ret_float(m); emu_charge(300); return true; }
  case 0x220DC: ret_float(ldexpf(as_float(arg32(0)), (int)((arg(2) ^ 0x800000) - 0x800000))), emu_charge(300); return true;
  case 0x220E0: case 0x22110: { /* strtof, atof */
    char s[128];
    int k = 0;
    uint32_t p = arg(0);
    while (k < 127 && (s[k] = rd8(p + k))) k++;
    s[k] = 0;
    char *end;
    float v = strtof(s, &end);
    if (pc == 0x220E0 && arg(1)) wr24(arg(1), p + (uint32_t)(end - s));
    ret_float(v), emu_charge(4000);
    return true;
  }
  case 0x220E4: ret_float(logf(as_float(arg32(0)))), emu_charge(5000); return true;
  case 0x220E8: ret_float(log10f(as_float(arg32(0)))), emu_charge(5500); return true;
  case 0x220EC: { float ip, fr = modff(as_float(arg32(0)), &ip); if (arg(2)) { uint32_t b = bits_of(ip); wr24(arg(2), b & 0xFFFFFF), wr8(arg(2) + 3, b >> 24); } ret_float(fr); emu_charge(300); return true; }
  case 0x220F0: ret_float(fabsf(as_float(arg32(0)))), emu_charge(60); return true;
  case 0x220F4: ret_float(floorf(as_float(arg32(0)))), emu_charge(300); return true;
  case 0x220F8: ret_float(powf(as_float(arg32(0)), as_float(arg32(2)))), emu_charge(12000); return true;
  case 0x220FC: ret_float(fmodf(as_float(arg32(0)), as_float(arg32(2)))), emu_charge(1500); return true;
  case 0x22100: ret_float(acosf(as_float(arg32(0)))), emu_charge(6000); return true;
  case 0x22104: ret_float(asinf(as_float(arg32(0)))), emu_charge(6000); return true;
  case 0x22108: ret_float(atanf(as_float(arg32(0)))), emu_charge(5000); return true;
  case 0x2210C: ret_float(atan2f(as_float(arg32(0)), as_float(arg32(2)))), emu_charge(6000); return true;
  case 0x22114: ret_float(ceilf(as_float(arg32(0)))), emu_charge(300); return true;
  case 0x22118: ret_float(sinf(as_float(arg32(0)))), emu_charge(5000); return true;
  case 0x2211C: ret_float(cosf(as_float(arg32(0)))), emu_charge(5000); return true;
  case 0x22120: ret_float(tanf(as_float(arg32(0)))), emu_charge(9000); return true;
  case 0x22124: ret_float(expf(as_float(arg32(0)))), emu_charge(5000); return true;
  case 0x22130: ret_float(sinhf(as_float(arg32(0)))), emu_charge(7000); return true;
  case 0x22134: ret_float(coshf(as_float(arg32(0)))), emu_charge(7000); return true;
  case 0x22138: ret_float(tanhf(as_float(arg32(0)))), emu_charge(8000); return true;
  }
  unknown_calls++;
  emu_stop("unimplemented", "the program called %s %s at %06X, which ce_emu's OS doesn't do yet (from %06X)",
           boot ? "boot code routine" : "OS routine", os_name(pc), pc, rd24(R.SPL));
  return true;
}

void os_report(FILE *f) {
  int n = 0;
  fprintf(f, "  \"os_calls\": {");
  for (uint32_t i = 0; i < sizeof calls / sizeof calls[0]; i++)
    if (calls[i] && (i * 4 < HLE_BASE || i * 4 >= HLE_BASE + 4 * HLE_SLOTS))
      fprintf(f, "%s\"%s\": %u", n++ ? ", " : "", os_name(i * 4), calls[i]);
  fprintf(f, "},\n  \"stack_bytes\": %d,\n  \"free_ram\": %u,\n  \"archive_used\": %u,\n  \"garbage_collects\": %d,\n",
          os_stack_used(), ram_free(), arc_free - ARC_START, arc_gcs);
}

/* the appvars and programs at the end, as .8xv/.8xp files */
int os_save_vars(const char *dir) {
  ent_t e;
  int k = 0;
  for (bool ok = ent_next(&e, true); ok; ok = ent_next(&e, false)) {
    if (!e.prog || ((e.type & 0x3F) != 0x15 && (e.type & 0x3F) != 5 && (e.type & 0x3F) != 6)) continue;
    uint32_t d = data_of(&e), size = data_size(e.type, d);
    char path[1024], nm[9] = {0};
    memcpy(nm, e.name, e.namelen);
    snprintf(path, sizeof path, "%s/%s.%s", dir, nm, (e.type & 0x3F) == 0x15 ? "8xv" : "8xp");
    FILE *f = fopen(path, "wb");
    if (!f) continue;
    uint8_t hdr[55] = "**TI83F*\x1a\x0a";
    memcpy(hdr + 11, "ce_emu", 6);
    uint32_t entry = 2 + 13 + 2 + size;
    hdr[53] = entry, hdr[54] = entry >> 8;
    uint8_t vh[17] = {13, 0, size, size >> 8, e.type & 0x3F};
    memcpy(vh + 5, e.name, e.namelen);
    vh[13] = e.ver, vh[14] = archived(&e) ? 0x80 : 0, vh[15] = size, vh[16] = size >> 8;
    uint8_t *data = mem_at(d, size);
    uint32_t sum = 0;
    for (int i = 0; i < 17; i++) sum += vh[i];
    for (uint32_t i = 0; i < size; i++) sum += data[i];
    uint8_t cs[2] = {sum, sum >> 8};
    fwrite(hdr, 1, 55, f), fwrite(vh, 1, 17, f), fwrite(data, 1, size, f), fwrite(cs, 1, 2, f);
    fclose(f);
    k++;
  }
  return k;
}
