/* The 3D view's inner loops: eZ80 assembly on the CE (rs.s), C on a computer (rs.c, the same arithmetic). */
#ifndef NBCE_RS_H
#define NBCE_RS_H
#include <stdint.h>

/* a vertex on the screen: x 0..2560, y 0..1920 (1/16 pixels); u, v the texel x 4096 (0..65535) */
typedef struct { int x, y, u, v; } SV;

/* what rs_poly fills with: a texture and a shade row, 256-byte aligned (tex[v * 16 + u], cm[texel]); alpha: texel 0
 * is see-through */
void rs_setup(const uint8_t *tex, const uint8_t *cm, uint8_t alpha);
/* a convex polygon (n 3..12) all on the screen, into rbuf's pixels that are still 0, its texture mapped linearly
 * along its edges */
void rs_poly(const SV *p, uint8_t n);
/* camera space (Q14, z >= 819, |x|, |y| < 4z) to the screen: out[0] x, out[1] y, in 1/16 pixels */
void rs_proj(int x, int y, int z, int *out);
/* 1 when the pixels around (x, y, z)'s projection (z > 2r), as far as r's projection and a pixel more each way, are
 * all drawn already (or off the screen) */
uint8_t rs_hidden(int x, int y, int z, int r);
/* bits 0-4: the planes a polygon through camera space (x, y, z) is to be cut by (z < 819, x > 4z, -x > 4z, y > 4z,
 * -y > 4z); bits 8-12: the view's sides it is past, a little wider than the screen's (z < 819, 16x > 15z,
 * -16x > 15z, 4y > 3z, -4y > 3z) */
int rs_code(int x, int y, int z);
/* dst[i] = f[a[i]] for the 256 bytes, all three 256-byte aligned */
void rs_compose(uint8_t *dst, const uint8_t *a, const uint8_t *f);
/* the polygon cut to a side of the screen (k 0: x >= 0, 1: x <= 2560, 2: y >= 0, 3: y <= 1920): into out, how many */
uint8_t rs_clip2(const SV *in, uint8_t n, SV *out, uint8_t k);
/* a cell's corners in camera space (corner k: x, y, z at rs_ck[3k..], bits of k x 1, y 2, z 4), and face f of it
 * (0..5 -Y +Y -Z +Z -X +X, 6 and 7 a plant's quads) with rs_setup's texture: passed by when past a side of the view,
 * drawn when all on the screen (0); 1 when to be cut by planes (rs_fclip: rs_code's), 2 when projected into rs_fs but
 * not all on the screen */
extern int rs_ck[24];
extern SV rs_fs[4];
extern uint8_t rs_fclip;
uint8_t rs_face(uint8_t f);
/* p[i] = row[p[i]] for n bytes (1..65535), row 256-byte aligned */
void rs_shade(uint8_t *p, const uint8_t *row, uint16_t n);
/* a picture's row of runs (tools/pack.py rle) to q (its column 0), sw pixels; 0 see-through */
void rs_rle(uint8_t *q, const uint8_t *p, int sw);
/* the HUD layer's rows (h, HUD_W each) as runs into o: pairs (see-through, drawn), each row's adding up to HUD_W
 * (at most HUD_W + 1 bytes a row); after them */
uint8_t *rs_runs_of(const uint8_t *h, uint8_t *o, uint8_t rows);
/* a + floor((b - a) f / 256), a and b 0..255, f 0..256 */
int rs_lerp(int a, int b, int f);
/* the 160 x 120 picture to the screen at 2x, 0s as the row's sky colour (sky[y]); from screen row 180 on, the HUD's
 * layer (187 wide at x 67, 0 see-through) over it (hud 0: none) */
void rs_expand(const uint8_t *src, uint8_t *dst, const uint8_t *sky, const uint8_t *hud);
/* a column's cells with a face (mask: 4 bytes, bit y & 31) from bit slot: up levels up from it (offsets 0, 1..)
 * then down levels down (-1, -2..), those set, in that order, into out; how many */
uint8_t rs_cells(const uint8_t *mask, uint8_t slot, uint8_t up, uint8_t down, int8_t *out);
/* the view rs_column tests cells against, set each frame: by j + 32 (j the levels up from the eye), the camera
 * space steps up and j squared (past 64: 65535); by squared distance, the fog (9: past the view); the limits */
extern int rs_jY[65], rs_jZ[65], rs_jV[65];
extern uint16_t rs_jS[128];
extern uint8_t rs_fog[257];
extern int rs_vlim, rs_cy0, rs_cz0, rs_vzk, rs_R, rs_KX;
extern uint8_t rs_eys, rs_up, rs_down, rs_jbase;
/* a column's cells with a face (as rs_cells finds them), each in the view and not hidden by what is drawn already
 * (rs_hidden): rs_cell(Y, Z, fog, j + 32, its y slot) (render.c) */
void rs_column(const uint8_t *mask, int h2, int Y0, int Z0, int V0, int cx);
void rs_cell(int Y, int Z, uint8_t fog, uint8_t o, uint8_t ys);
/* a quadrant's columns: rows out along x from the first (rs_qx0, its slot rs_qxs0, step rs_qsxi = rs_qsx), columns
 * out along z (rs_qz0, rs_qzs0, rs_qszi = rs_qsz), their low corners at the eye's level in camera space (rs_qXi..,
 * steps rs_qsXx.. along x and rs_qsXz.. along z, V = 0.7 Z), |i| and |k| squared from the tables rs_qsqi and rs_qsqk
 * by the loops' counts (0..rs_vd); those in the window (rs_wx0 <= x < rs_wx1, z the same), the view's distance
 * (rs_vlim) and its sides (|X + rs_cx0| <= Z + rs_czm) and with a face, to rs_column (the column in rs_x, rs_z, rs_ci,
 * rs_X0, rs_xs, rs_zs). (rs_qXi.. are moved along.) */
void rs_quad(void);
extern int rs_qXi, rs_qYi, rs_qZi, rs_qVi, rs_qsXx, rs_qsYx, rs_qsZx, rs_qsVx, rs_qsXz, rs_qsYz, rs_qsZz, rs_qsVz;
extern int rs_qx0, rs_qz0, rs_qsxi, rs_qszi, rs_czm, rs_cx0, rs_wx0, rs_wx1, rs_wz0, rs_wz1;
extern const uint16_t *rs_qsqi, *rs_qsqk;
extern uint8_t rs_qsx, rs_qsz, rs_qxs0, rs_qzs0, rs_vd;
extern int rs_x, rs_z, rs_ci, rs_X0;
extern uint8_t rs_xs, rs_zs;
/* the inner loops' copy in the archive (flash/rsf.s, after NBCETEX's tables), run when of this build: size its
 * bytes; rs_flash it, 0 when not run (the program's own copy then) */
extern const uint8_t *rs_flash;
void rs_flash_init(const uint8_t *blob, unsigned size);
/* 2^28 / k for k 16..2560 (3 bytes each, from k = 16), and f * 256 / d for d 1..15, f < d ([d * 16 + f]): the
 * tables rs_poly divides with (in NBCETEX) */
extern const uint8_t *rs_recip, *rs_ratio;
#endif
