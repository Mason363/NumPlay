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
/* 2^28 / k for k 16..2560 (3 bytes each, from k = 16), and f * 256 / d for d 1..15, f < d ([d * 16 + f]): the
 * tables rs_poly divides with (in NBCETEX) */
extern const uint8_t *rs_recip, *rs_ratio;
#endif
