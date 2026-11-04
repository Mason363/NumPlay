/* The TI-84 Plus CE's pixel mixing, in the eZ80's own instructions (a C loop costs a few hundred cycles a pixel
 * there): for launcher/src/gfx.c's drawing (gfx_ce.c) and the games' own on the CE.
 *
 *   CE_MIX_ASM     the body of a naked color_t f(color_t a, color_t b, unsigned t): (a (32 - t) + b t) / 32 for each
 *                  of red, green and blue, t 0..32 (the NumWorks's 32-bit mixing, the same results)
 *   CE_BLEND_ASM   the body of a naked void f(color_t *p, unsigned n, const uint8_t *tables): n pixels (1..255, 0 for
 *                  256) at p mixed with one colour, through tables ce_tables made (several times faster)
 *   ce_tables(m, c, t)   those tables at m (CE_TABLES bytes, 256-aligned) for colour c and level t */
#ifndef NP_CE_DRAW_H
#define NP_CE_DRAW_H
#include <stdint.h>
#include <string.h>

#define CE_MIX_ASM \
  "ld iy, 0\n" \
  "add iy, sp\n" \
  "ld d, (iy + 9)\n" \
  "ld a, 32\n" \
  "sub a, d\n" \
  "ld e, a\n" \
  "ld a, (iy + 4)\n" \
  "rrca\n" \
  "rrca\n" \
  "rrca\n" \
  "and a, 31\n" \
  "ld b, a\n" \
  "ld c, e\n" \
  "mlt bc\n" \
  "ld a, (iy + 7)\n" \
  "rrca\n" \
  "rrca\n" \
  "rrca\n" \
  "and a, 31\n" \
  "ld h, a\n" \
  "ld l, d\n" \
  "mlt hl\n" \
  "add hl, bc\n" \
  "add hl, hl\n" \
  "add hl, hl\n" \
  "add hl, hl\n"         /* h = the sum / 32 */ \
  "ld a, h\n" \
  "rlca\n" \
  "rlca\n" \
  "rlca\n" \
  "push af\n"            /* red << 3 */ \
  "ld a, (iy + 3)\n"     /* green: 3 bits of each byte */ \
  "rlca\n" \
  "rlca\n" \
  "rlca\n" \
  "and a, 7\n" \
  "ld b, a\n" \
  "ld a, (iy + 4)\n" \
  "and a, 7\n" \
  "rlca\n" \
  "rlca\n" \
  "rlca\n" \
  "or a, b\n" \
  "ld b, a\n" \
  "ld c, e\n" \
  "mlt bc\n" \
  "ld a, (iy + 6)\n" \
  "rlca\n" \
  "rlca\n" \
  "rlca\n" \
  "and a, 7\n" \
  "ld h, a\n" \
  "ld a, (iy + 7)\n" \
  "and a, 7\n" \
  "rlca\n" \
  "rlca\n" \
  "rlca\n" \
  "or a, h\n" \
  "ld h, a\n" \
  "ld l, d\n" \
  "mlt hl\n" \
  "add hl, bc\n" \
  "add hl, hl\n" \
  "add hl, hl\n" \
  "add hl, hl\n"         /* h = green */ \
  "ld a, h\n" \
  "rrca\n" \
  "rrca\n" \
  "rrca\n"               /* bits 0-2: green >> 3, bits 5-7: green & 7 */ \
  "ld c, a\n" \
  "and a, 7\n" \
  "ld b, a\n" \
  "pop af\n" \
  "or a, b\n" \
  "ld b, a\n"            /* b: the high byte */ \
  "ld a, c\n" \
  "and a, 224\n" \
  "ld c, a\n"            /* c: green's part of the low byte */ \
  "push bc\n" \
  "ld a, (iy + 3)\n"     /* blue: the low byte's 5 bits */ \
  "and a, 31\n" \
  "ld b, a\n" \
  "ld c, e\n" \
  "mlt bc\n" \
  "ld a, (iy + 6)\n" \
  "and a, 31\n" \
  "ld h, a\n" \
  "ld l, d\n" \
  "mlt hl\n" \
  "add hl, bc\n" \
  "add hl, hl\n" \
  "add hl, hl\n" \
  "add hl, hl\n" \
  "pop bc\n" \
  "ld a, h\n" \
  "or a, c\n" \
  "ld l, a\n" \
  "ld h, b\n" \
  "ret\n"

/* five pages: the red of a high byte, the blue of a low byte, the green from each byte (the same for every
 * colour), then the green mixed (its high byte's part, and at +64 its low byte's) */
enum { CE_T_R = 0, CE_T_B = 256, CE_T_GHI = 512, CE_T_GLO = 768, CE_T_GH = 1024, CE_T_GL = 1088, CE_TABLES = 1280 };

#define CE_BLEND_ASM \
  "ld iy, 0\n" \
  "add iy, sp\n" \
  "push ix\n" \
  "ld ix, (iy + 3)\n" \
  "ld b, (iy + 6)\n" \
  "ld hl, (iy + 9)\n" \
  "1:\n" \
  "ld e, (ix + 0)\n"     /* e: the low byte, d: the high byte */ \
  "ld d, (ix + 1)\n" \
  "inc h\n" \
  "inc h\n" \
  "ld l, d\n" \
  "ld a, (hl)\n" \
  "inc h\n" \
  "ld l, e\n" \
  "or a, (hl)\n"         /* the pixel's green */ \
  "inc h\n" \
  "ld l, a\n" \
  "ld c, (hl)\n" \
  "set 6, l\n" \
  "ld a, (hl)\n" \
  "dec h\n" \
  "dec h\n" \
  "dec h\n" \
  "ld l, e\n" \
  "or a, (hl)\n" \
  "ld (ix + 0), a\n" \
  "dec h\n" \
  "ld l, d\n" \
  "ld a, (hl)\n" \
  "or a, c\n" \
  "ld (ix + 1), a\n" \
  "lea ix, ix + 2\n" \
  "djnz 1b\n" \
  "pop ix\n" \
  "ret\n"

static void ce_tables(uint8_t *m, uint16_t c, unsigned t) {
  if (m[CE_T_GHI + 255] != 56) /* (the green pages, the first time) */
    for (unsigned v = 0; v < 256; v++) m[CE_T_GHI + v] = (uint8_t)((v & 7) << 3), m[CE_T_GLO + v] = (uint8_t)(v >> 5);
  unsigned u = 32 - t, cr = (unsigned)(c >> 11) * t, cg = (unsigned)(c >> 5 & 63) * t, cb = (unsigned)(c & 31) * t;
  for (unsigned v = 0; v < 32; v++, cr += u, cb += u) {
    memset(m + CE_T_R + v * 8, (int)((cr >> 5) << 3), 8);
    m[CE_T_B + v] = (uint8_t)(cb >> 5);
  }
  for (int k = 32; k < 256; k += 32) memcpy(m + CE_T_B + k, m + CE_T_B, 32);
  for (unsigned v = 0; v < 64; v++, cg += u)
    m[CE_T_GH + v] = (uint8_t)(cg >> 5 >> 3), m[CE_T_GL + v] = (uint8_t)((cg >> 5 & 7) << 5);
}
#endif
