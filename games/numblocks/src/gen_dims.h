/* NumBlocks.nwa only (NB_DIMS): the generators of the Nether (ChunkProviderHell, MapGenCavesHell) and the
 * End (ChunkProviderEnd), included at the end of gen.c to share its java.util.Random, its noise and its
 * memory: while the player is in one of them, the Overworld's caches (the summary cache on main's stack,
 * the scratch union U) are theirs, and gen_init makes them the Overworld's again.
 *
 * As in gen.c, the noise's sample positions are exact (fixed point) and its sums run in float; the
 * terrain, its surface and the caves follow 1.8.8 block for block. Minecraft 1.8.8 decorates the Nether
 * and the End with a random that is not the world's (it depends on the order chunks were made in): here
 * each decoration draws from a random seeded by its chunk, so it is the same every time. */
#include "dims.h"

/* gd_dim (gen.c): the generator gen_slab runs, DIM_* */

/* ---------------------------------------------------------------- noise: NoiseGeneratorOctaves, any size */
/* n octaves of a NoiseGeneratorOctaves (their states st) at grid origin (bx, by, bz), xs x ys x zs samples
 * (ys 1: the 2D noise) spaced sx, sy, sz (Q32), added into out[(x * zs + z) * ys + y] */
static void gd_octaves(const Octave *oc, int n, float *out, i64 bx, i64 by, i64 bz, int xs, int ys, int zs, u64 sx,
                       u64 sy, u64 sz) {
    uint8_t P[256];
    int xc[16], zc[16], yc[17];
    float xf[16], zf[16], yf[17];
    for (int o = 0; o < n; o++) {
        i64 off[3];
        perm_build(oc[o].st, P, off);
        float amp = (float)(1 << o);
        for (int k = 0; k < xs; k++) coord_split(coord_q32(bx, sx, o, k, off[0]), &xc[k], &xf[k]);
        for (int k = 0; k < zs; k++) coord_split(coord_q32(bz, sz, o, k, off[2]), &zc[k], &zf[k]);
        if (ys == 1) {
            /* NoiseGeneratorImproved's 2D path (no y) */
            int idx = 0;
            for (int x = 0; x < xs; x++) {
                float fx = xf[x], ux = fade(fx);
                int i3 = xc[x];
                for (int z = 0; z < zs; z++, idx++) {
                    float fz = zf[z], uz = fade(fz);
                    int l1 = zc[z];
                    int j3 = P[P[i3] & 255] + l1, i1 = P[P[(i3 + 1) & 255] & 255] + l1;
                    float a = lerp(ux, grad3(P[j3 & 255], fx, 0, fz), grad3(P[i1 & 255], fx - 1, 0, fz));
                    float b = lerp(ux, grad3(P[(j3 + 1) & 255], fx, 0, fz - 1), grad3(P[(i1 + 1) & 255], fx - 1, 0, fz - 1));
                    out[idx] += lerp(uz, a, b) * amp;
                }
            }
            continue;
        }
        for (int k = 0; k < ys; k++) coord_split(coord_q32(by, sy, o, k, off[1]), &yc[k], &yf[k]);
        int idx = 0;
        for (int x = 0; x < xs; x++) {
            float fx = xf[x], ux = fade(fx), gx1 = fx - 1;
            int pa = P[xc[x]], pb = P[(xc[x] + 1) & 255];
            for (int z = 0; z < zs; z++) {
                float fz = zf[z], uz = fade(fz), gz1 = fz - 1;
                int j4 = zc[z], cur = -1;
                float d16 = 0, d7 = 0, d17 = 0, d8 = 0;
                for (int y = 0; y < ys; y++, idx++) {
                    /* Java's stale gradients: recomputed only when the y cell changes (and at a column's start),
                     * with the y fraction of the first sample in the cell */
                    if (y == 0 || yc[y] != cur) {
                        cur = yc[y];
                        float fy = yf[y], gy1 = fy - 1;
                        int j5 = pa + cur, i6 = pb + cur;
                        int k5 = P[j5 & 255] + j4, l5 = P[(j5 + 1) & 255] + j4;
                        int i2 = P[i6 & 255] + j4, j6 = P[(i6 + 1) & 255] + j4;
                        d16 = lerp(ux, grad3(P[k5 & 255], fx, fy, fz), grad3(P[i2 & 255], gx1, fy, fz));
                        d7 = lerp(ux, grad3(P[l5 & 255], fx, gy1, fz), grad3(P[j6 & 255], gx1, gy1, fz));
                        d17 = lerp(ux, grad3(P[(k5 + 1) & 255], fx, fy, gz1), grad3(P[(i2 + 1) & 255], gx1, fy, gz1));
                        d8 = lerp(ux, grad3(P[(l5 + 1) & 255], fx, gy1, gz1), grad3(P[(j6 + 1) & 255], gx1, gy1, gz1));
                    }
                    float uy = fade(yf[y]);
                    out[idx] += lerp(uz, lerp(uy, d16, d7), lerp(uy, d17, d8)) * amp;
                }
            }
        }
    }
}

/* ======================================================================== */
/* The Nether: ChunkProviderHell                                             */
/* ======================================================================== */

#define NGC 12   /* the decorations' grids kept (rounded to 1/32: they only look) */
typedef struct {
    Octave oc[48];   /* netherNoiseGen1 (16), netherNoiseGen2 (16), netherNoiseGen3 (8), slowsandGravel (4),
                        netherrackExculsivity (4) */
    float bias[17];  /* the density's offset by height (initializeNoiseField's adouble) */
    int fx, fz, fok;
    float grid[425];   /* the terrain's grid, of chunk (fx, fz): (x * 5 + z) * 17 + y */
    int gx[NGC], gz[NGC];
    uint32_t gage[NGC], gclock;
    uint8_t gok[NGC];
    int16_t cgrid[NGC][425];
} NetherMem;
_Static_assert(sizeof(NetherMem) <= GEN_CACHE_BYTES, "the Nether's state fits in the summary cache");
#define NM ((NetherMem *)(void *)sumc)
static u64 SC_N1XZ, SC_N1Y, SC_N3XZ, SC_N3Y, SC_SLOW, SC_EXCL;

static void nether_init(void) {
    NetherMem *m = NM;
    memset(m, 0, sizeof *m);
    JRand r;
    jr_seed(&r, g_seed);
    for (int i = 0; i < 48; i++) m->oc[i].st = r.s, perm_skip(&r);
    SC_N1XZ = FX(684.412);
    SC_N1Y = FX(2053.236);
    SC_N3XZ = FX(684.412 / 80.0);
    SC_N3Y = FX(2053.236 / 60.0);
    SC_SLOW = FX(0.03125);
    SC_EXCL = FX(0.0625);
    for (int j = 0; j < 17; j++) {
        double a = cos((double)j * 3.141592653589793 * 6.0 / 17.0) * 2.0, d2 = j > 8 ? 16 - j : j;
        if (d2 < 4.0) {
            d2 = 4.0 - d2;
            a -= d2 * d2 * d2 * 10.0;
        }
        m->bias[j] = (float)a;
    }
}

static void nether_cgrid_put(int cx, int cz, const float *g);
/* initializeNoiseField: chunk (cx, cz)'s 5 x 17 x 5 densities, into g */
static void nether_noise(int cx, int cz, float *g) {
    NetherMem *m = NM;
    float n1[425], n2[425], n3[425];
    memset(n1, 0, sizeof n1);
    memset(n2, 0, sizeof n2);
    memset(n3, 0, sizeof n3);
    gd_octaves(m->oc + 32, 8, n3, cx * 4, 0, cz * 4, 5, 17, 5, SC_N3XZ, SC_N3Y, SC_N3XZ);
    gd_octaves(m->oc, 16, n1, cx * 4, 0, cz * 4, 5, 17, 5, SC_N1XZ, SC_N1Y, SC_N1XZ);
    gd_octaves(m->oc + 16, 16, n2, cx * 4, 0, cz * 4, 5, 17, 5, SC_N1XZ, SC_N1Y, SC_N1XZ);
    for (int i = 0; i < 425; i++) {
        int y = i % 17;
        float d6 = n1[i] / 512.0f, d7 = n2[i] / 512.0f, d8 = (n3[i] / 10.0f + 1.0f) / 2.0f, d4;
        if (d8 < 0.0f) d4 = d6;
        else if (d8 > 1.0f) d4 = d7;
        else d4 = d6 + (d7 - d6) * d8;
        d4 -= m->bias[y];
        if (y > 13) {
            float d9 = (float)(y - 13) / 3.0f;
            d4 = d4 * (1.0f - d9) + -10.0f * d9;
        }
        g[i] = d4;
    }
    nether_cgrid_put(cx, cz, g);
}
/* the terrain's grid of chunk (cx, cz) (the last one kept) */
static const float *nether_grid(int cx, int cz) {
    NetherMem *m = NM;
    if (!m->fok || m->fx != cx || m->fz != cz) {
        nether_noise(cx, cz, m->grid);
        m->fx = cx, m->fz = cz, m->fok = 1;
    }
    return m->grid;
}
/* the decorations' grids: the latest used kept, rounded */
static void nether_cgrid_put(int cx, int cz, const float *g) {
    NetherMem *m = NM;
    int k = 0;
    for (int i = 0; i < NGC; i++) {
        if (m->gok[i] && m->gx[i] == cx && m->gz[i] == cz) return;
        if (!m->gok[i]) {
            k = i;
            break;
        }
        if (m->gage[i] < m->gage[k]) k = i;
    }
    for (int i = 0; i < 425; i++) {
        float v = g[i] * 32.0f;
        m->cgrid[k][i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
    m->gx[k] = cx, m->gz[k] = cz, m->gok[k] = 1, m->gage[k] = ++m->gclock;
}
static const int16_t *nether_cgrid(int cx, int cz) {
    NetherMem *m = NM;
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < NGC; i++)
            if (m->gok[i] && m->gx[i] == cx && m->gz[i] == cz) {
                m->gage[i] = ++m->gclock;
                return m->cgrid[i];
            }
        float g[425];
        nether_noise(cx, cz, g);
    }
    return m->cgrid[0];
}

/* func_180515_a: the terrain of a column (netherrack where the density is above 0, lava under y 32),
 * into col[0..127] */
static void nether_column(const float *g, int x, int z, uint8_t *col) {
    int j1 = x >> 2, k1 = z >> 2, j2 = x & 3, k2 = z & 3;
    for (int l1 = 0; l1 < 16; l1++) {
        /* the grid's corners, interpolated as Java does: along y in steps of 1/8, x and z in steps of 1/4 */
        float d1 = g[((j1 + 0) * 5 + k1 + 0) * 17 + l1], d2 = g[((j1 + 0) * 5 + k1 + 1) * 17 + l1];
        float d3 = g[((j1 + 1) * 5 + k1 + 0) * 17 + l1], d4 = g[((j1 + 1) * 5 + k1 + 1) * 17 + l1];
        float d5 = (g[((j1 + 0) * 5 + k1 + 0) * 17 + l1 + 1] - d1) * 0.125f;
        float d6 = (g[((j1 + 0) * 5 + k1 + 1) * 17 + l1 + 1] - d2) * 0.125f;
        float d7 = (g[((j1 + 1) * 5 + k1 + 0) * 17 + l1 + 1] - d3) * 0.125f;
        float d8 = (g[((j1 + 1) * 5 + k1 + 1) * 17 + l1 + 1] - d4) * 0.125f;
        for (int i2 = 0; i2 < 8; i2++) {
            /* (the steps added j2 times, k2 times, as Java's loops add them) */
            float d10 = d1, d11 = d2, d12 = (d3 - d1) * 0.25f, d13 = (d4 - d2) * 0.25f;
            for (int a = 0; a < j2; a++) d10 += d12, d11 += d13;
            float d15 = d10, d16 = (d11 - d10) * 0.25f;
            for (int b = 0; b < k2; b++) d15 += d16;
            int y = l1 * 8 + i2;
            col[y] = d15 > 0.0f ? GEN_CODE(GC_NETHERRACK) : y < 32 ? B_LAVA : B_AIR;
            d1 += d5, d2 += d6, d3 += d7, d4 += d8;
        }
    }
}

/* a column's surface (func_180516_b), its draws from r (the chunk's hellRNG) */
static void nether_surface(uint8_t *col, JRand *r, float slow, float grav, float excl) {
    int flag = slow + jr_doublef(r) * 0.2f > 0.0f;
    int flag1 = grav + jr_doublef(r) * 0.2f > 0.0f;
    int l = (int)(excl / 3.0f + 3.0f + jr_doublef(r) * 0.25f);
    int i1 = -1;
    const int top = 64;
    uint8_t s = GEN_CODE(GC_NETHERRACK), s1 = GEN_CODE(GC_NETHERRACK);
    for (int j1 = 127; j1 >= 0; --j1) {
        int a = j1 < 127 - jr_int(r, 5);
        if (a && j1 > jr_int(r, 5)) {
            uint8_t b = col[j1];
            if (b != B_AIR) {
                if (b == GEN_CODE(GC_NETHERRACK)) {
                    if (i1 == -1) {
                        if (l <= 0) {
                            s = B_AIR;
                            s1 = GEN_CODE(GC_NETHERRACK);
                        } else if (j1 >= top - 4 && j1 <= top + 1) {
                            s = GEN_CODE(GC_NETHERRACK);
                            s1 = GEN_CODE(GC_NETHERRACK);
                            if (flag1) s = B_GRAVEL, s1 = GEN_CODE(GC_NETHERRACK);
                            if (flag) s = GEN_CODE(GC_SOUL_SAND), s1 = GEN_CODE(GC_SOUL_SAND);
                        }
                        if (j1 < top && s == B_AIR) s = B_LAVA;
                        i1 = l;
                        col[j1] = j1 >= top - 1 ? s : s1;
                    } else if (i1 > 0) {
                        --i1;
                        col[j1] = s1;
                    }
                }
            } else {
                i1 = -1;
            }
        } else {
            col[j1] = B_BEDROCK;
        }
    }
}

/* ---- MapGenCavesHell: tunnels carved where the box they cut holds no lava */
typedef struct {
    int cx, cz;            /* the chunk carved */
    const uint32_t *lava;  /* its lava, a bit a block: [(x * 16 + z) * 4 + (y >> 5)] bit y & 31 */
    uint8_t *out;          /* the slab carved */
    int y0, h;
} HellCaves;

static int hc_lava(const HellCaves *c, int x, int y, int z) { return c->lava[(x * 16 + z) * 4 + (y >> 5)] >> (y & 31) & 1; }

/* MapGenCavesHell.func_180702_a. As gen.c's caves: the place exact in fixed point (Java's doubles only ever
 * add floats to it), the carving's tests in float */
static void hell_tunnel(HellCaves *c, i64 seed, Q32 px, Q32 py, Q32 pz, float w, float yaw, float pitch, int k,
                        int len, int half) {
    Q32 d0 = SHL(c->cx * 16 + 8, 32), d1 = SHL(c->cz * 16 + 8, 32);
    float f = 0.0f, f1 = 0.0f;
    JRand rnd;
    jr_seed(&rnd, seed);
    if (len <= 0) {
        int i = 8 * 16 - 16;
        len = i - jr_int(&rnd, i / 4);
    }
    int flag1 = 0;
    if (k == -1) {
        k = len / 2;
        flag1 = 1;
    }
    int j = jr_int(&rnd, len / 2) + len / 4;
    for (int flag = jr_int(&rnd, 6) == 0; k < len; ++k) {
        float sv = mh_sin((float)k * 3.1415927f / (float)len) * w * 1.0f;
        Q32 r2 = (Q32)(3LL << 31) + q32_of_float(sv), r3 = half ? r2 / 2 : r2;   /* d2 = 1.5 + ..., d3 = d2 x 0.5 or 1 */
        float f2 = mh_cos(pitch), f3 = mh_sin(pitch);
        px += q32_of_float(mh_cos(yaw) * f2);
        py += q32_of_float(f3);
        pz += q32_of_float(mh_sin(yaw) * f2);
        pitch = flag ? pitch * 0.92f : pitch * 0.7f;
        pitch += f1 * 0.1f;
        yaw += f * 0.1f;
        f1 *= 0.9f;
        f *= 0.75f;
        /* (Java's order of the draws: left to right) */
        float ra = jr_float(&rnd), rb = jr_float(&rnd), rc = jr_float(&rnd);
        f1 += (ra - rb) * rc * 2.0f;
        ra = jr_float(&rnd), rb = jr_float(&rnd), rc = jr_float(&rnd);
        f += (ra - rb) * rc * 4.0f;
        if (!flag1 && k == j && w > 1.0f) {
            i64 s1 = jr_long(&rnd);
            float a = jr_float(&rnd) * 0.5f + 0.5f;
            hell_tunnel(c, s1, px, py, pz, a, yaw - 1.5707964f, pitch / 3.0f, k, len, 0);
            i64 s2 = jr_long(&rnd);
            float b2 = jr_float(&rnd) * 0.5f + 0.5f;
            hell_tunnel(c, s2, px, py, pz, b2, yaw + 1.5707964f, pitch / 3.0f, k, len, 0);
            return;
        }
        if (flag1 || jr_int(&rnd, 4) != 0) {
            if (cave_too_far(px - d0, pz - d1, len - k, w + 2.0f + 16.0f)) return;
            Q32 reach = SHL(16, 32) + r2 * 2;
            if (px >= d0 - reach && pz >= d1 - reach && px <= d0 + reach && pz <= d1 + reach) {
                int k2 = q32_floor(px - r2) - c->cx * 16 - 1, kk = q32_floor(px + r2) - c->cx * 16 + 1;
                int l2 = q32_floor(py - r3) - 1, l = q32_floor(py + r3) + 1;
                int i3 = q32_floor(pz - r2) - c->cz * 16 - 1, i1 = q32_floor(pz + r2) - c->cz * 16 + 1;
                if (k2 < 0) k2 = 0;
                if (kk > 16) kk = 16;
                if (l2 < 1) l2 = 1;
                if (l > 120) l = 120;
                if (i3 < 0) i3 = 0;
                if (i1 > 16) i1 = 16;
                int flag2 = 0;
                for (int j1 = k2; !flag2 && j1 < kk; ++j1)
                    for (int k1 = i3; !flag2 && k1 < i1; ++k1)
                        for (int l1 = l + 1; !flag2 && l1 >= l2 - 1; --l1)
                            if (l1 >= 0 && l1 < 128) {
                                if (hc_lava(c, j1, l1, k1)) flag2 = 1;
                                if (l1 != l2 - 1 && j1 != k2 && j1 != kk - 1 && k1 != i3 && k1 != i1 - 1) l1 = l2;
                            }
                if (!flag2) {
                    float d2 = q32_to_float(r2), d3 = q32_to_float(r3);
                    int ylo = l2 + 1 > c->y0 ? l2 + 1 : c->y0, yhi = l < c->y0 + c->h - 1 ? l : c->y0 + c->h - 1;
                    for (int j3 = k2; j3 < kk; ++j3) {
                        float d10 = q32_to_float(SHL(j3 + c->cx * 16, 32) + (Q32_ONE >> 1) - px) / d2;
                        for (int i2 = i3; i2 < i1; ++i2) {
                            float d8 = q32_to_float(SHL(i2 + c->cz * 16, 32) + (Q32_ONE >> 1) - pz) / d2;
                            float h2 = d10 * d10 + d8 * d8;
                            if (h2 >= 1.0f) continue;
                            for (int y = yhi; y >= ylo; --y) {
                                float d9 = q32_to_float(SHL(y - 1, 32) + (Q32_ONE >> 1) - py) / d3;
                                if (d9 > -0.7f && h2 + d9 * d9 < 1.0f) {
                                    uint8_t *b = &c->out[((y - c->y0) * 16 + i2) * 16 + j3];
                                    if (*b == GEN_CODE(GC_NETHERRACK) || *b == B_DIRT || *b == B_GRASS) *b = B_AIR;
                                }
                            }
                        }
                    }
                    if (flag1) break;
                }
            }
        }
    }
}

static void hell_caves(HellCaves *c) {
    JRand r;
    jr_seed(&r, g_seed);
    i64 j = jr_long(&r), k = jr_long(&r);
    for (int l = c->cx - 8; l <= c->cx + 8; ++l)
        for (int i1 = c->cz - 8; i1 <= c->cz + 8; ++i1) {
            jr_seed(&r, ((i64)l * j) ^ ((i64)i1 * k) ^ g_seed);
            /* recursiveGenerate */
            int i = jr_int(&r, jr_int(&r, jr_int(&r, 10) + 1) + 1);
            if (jr_int(&r, 5) != 0) i = 0;
            for (int n = 0; n < i; ++n) {
                int x0 = l * 16 + jr_int(&r, 16), y0 = jr_int(&r, 128), z0 = i1 * 16 + jr_int(&r, 16);
                Q32 d0 = SHL(x0, 32), d1 = SHL(y0, 32), d2 = SHL(z0, 32);
                int kk = 1;
                if (jr_int(&r, 4) == 0) {
                    i64 s = jr_long(&r);
                    /* func_180705_a: a room */
                    float w = 1.0f + jr_float(&r) * 6.0f;
                    hell_tunnel(c, s, d0, d1, d2, w, 0.0f, 0.0f, -1, -1, 1);
                    kk += jr_int(&r, 4);
                }
                for (int m = 0; m < kk; ++m) {
                    float f = jr_float(&r) * 3.1415927f * 2.0f;
                    float f1 = (jr_float(&r) - 0.5f) * 2.0f / 8.0f;
                    float f2 = jr_float(&r) * 2.0f + jr_float(&r);
                    hell_tunnel(c, jr_long(&r), d0, d1, d2, f2 * 2.0f, f, f1, 0, 0, 1);
                }
            }
        }
}

static void nether_decorate(int cx, int cz, int y0, int h, uint8_t *out);   /* (below: fortresses, glowstone...) */

/* chunk (cx, cz), rows [y0, y0 + h): terrain, surface, caves, then what decorates it */
static void nether_slab(int cx, int cz, int y0, int h, uint8_t *out) {
    const float *g = nether_grid(cx, cz);
    NetherMem *m = NM;
    float slow[256], grav[256], excl[256];
    memset(slow, 0, sizeof slow);
    memset(grav, 0, sizeof grav);
    memset(excl, 0, sizeof excl);
    gd_octaves(m->oc + 40, 4, slow, cx * 16, cz * 16, 0, 16, 16, 1, SC_SLOW, SC_SLOW, FX(1.0));
    gd_octaves(m->oc + 40, 4, grav, cx * 16, 109, cz * 16, 16, 1, 16, SC_SLOW, FX(1.0), SC_SLOW);
    gd_octaves(m->oc + 44, 4, excl, cx * 16, cz * 16, 0, 16, 16, 1, SC_EXCL, SC_EXCL, SC_EXCL);
    JRand r;
    jr_seed(&r, (i64)cx * 341873128712LL + (i64)cz * 132897987541LL);
    uint32_t lava[16 * 16 * 4];
    for (int z = 0; z < 16; z++)
        for (int x = 0; x < 16; x++) {
            uint8_t col[128];
            nether_column(g, x, z, col);
            nether_surface(col, &r, slow[z + x * 16], grav[z + x * 16], excl[z + x * 16]);
            uint32_t *lv = &lava[(x * 16 + z) * 4];
            lv[0] = lv[1] = lv[2] = lv[3] = 0;
            for (int y = 0; y < 128; y++)
                if (col[y] == B_LAVA) lv[y >> 5] |= 1u << (y & 31);
            for (int y = 0; y < h; y++) out[(y * 16 + z) * 16 + x] = col[y0 + y];
        }
    HellCaves c = {cx, cz, lava, out, y0, h};
    hell_caves(&c);
    nether_decorate(cx, cz, y0, h, out);
}

#include "gen_fortress.h"
#include "gen_stronghold.h"
#include "gen_struct.h"
#include "gen_mineshaft.h"

/* ---- ChunkProviderHell.populate
 * A chunk is decorated by the populations of the four chunks whose areas (offset by 8) cover it, replayed
 * each time its rows are made. What their features look at is the terrain without the caves (nq: the
 * same for every row of the chunk, so the rows agree) and what they placed before (a small set of
 * blocks); what they place lands in the rows where it fits (glowstone, fire and mushrooms in air, lava in
 * air or netherrack, quartz in netherrack). */
static int nq(int x, int y, int z) {
    if (y <= 0 || y == 127) return B_BEDROCK;
    if (y > 127) return B_AIR;
    const int16_t *g = nether_cgrid(x >> 4, z >> 4);
    int lx = x & 15, lz = z & 15, j1 = lx >> 2, k1 = lz >> 2, l1 = y >> 3;
#define G(i) ((float)g[i] * (1.0f / 32.0f))
    float d1 = G(((j1 + 0) * 5 + k1 + 0) * 17 + l1), d2 = G(((j1 + 0) * 5 + k1 + 1) * 17 + l1);
    float d3 = G(((j1 + 1) * 5 + k1 + 0) * 17 + l1), d4 = G(((j1 + 1) * 5 + k1 + 1) * 17 + l1);
    float f = (float)(y & 7) * 0.125f;
    d1 += (G(((j1 + 0) * 5 + k1 + 0) * 17 + l1 + 1) - d1) * f;
    d2 += (G(((j1 + 0) * 5 + k1 + 1) * 17 + l1 + 1) - d2) * f;
    d3 += (G(((j1 + 1) * 5 + k1 + 0) * 17 + l1 + 1) - d3) * f;
    d4 += (G(((j1 + 1) * 5 + k1 + 1) * 17 + l1 + 1) - d4) * f;
#undef G
    float fx = (float)(lx & 3) * 0.25f, a = d1 + (d3 - d1) * fx, b = d2 + (d4 - d2) * fx;
    float d = a + (b - a) * (float)(lz & 3) * 0.25f;
    return d > 0.0f ? GEN_CODE(GC_NETHERRACK) : y < 32 ? B_LAVA : B_AIR;
}

/* the blocks placed by the populations replayed, in the rest of U (open addressing on the place) */
#define WS_N ((int)((sizeof U - sizeof(Fort)) / 4))
#define WSET ((uint32_t *)(void *)((uint8_t *)&U + sizeof(Fort)))
static struct {
    int cx, cz, y0, h;
    uint8_t *out;
} nd;
static uint32_t ws_key(int x, int y, int z) {
    int rx = x - nd.cx * 16 + 48, rz = z - nd.cz * 16 + 48;
    if ((unsigned)rx > 127 || (unsigned)rz > 127 || (unsigned)y > 127) return 0;
    return (uint32_t)(rx << 14 | rz << 7 | y) + 1;
}
static uint32_t *ws_find(uint32_t k) {
    uint32_t *w = WSET;
    for (uint32_t i = k * 2654435761u % (uint32_t)WS_N, n = 0; n < (uint32_t)WS_N; n++, i = i + 1 == (uint32_t)WS_N ? 0 : i + 1)
        if (!w[i] || w[i] >> 8 == k) return &w[i];
    return NULL;
}
static int wget(int x, int y, int z) {
    uint32_t k = ws_key(x, y, z), *e = k ? ws_find(k) : NULL;
    return e && *e ? (int)(*e & 255) : nq(x, y, z);
}
static void wput(int x, int y, int z, int b) {
    uint32_t k = ws_key(x, y, z), *e = k ? ws_find(k) : NULL;
    if (e) *e = k << 8 | (uint32_t)b;
    int lx = x - nd.cx * 16, lz = z - nd.cz * 16, ly = y - nd.y0;
    if ((unsigned)lx > 15 || (unsigned)lz > 15 || ly < 0 || ly >= nd.h) return;
    uint8_t *c = &nd.out[(ly * 16 + lz) * 16 + lx];
    if (*c == B_AIR || (b == B_LAVA && *c == GEN_CODE(GC_NETHERRACK))) *c = (uint8_t)b;
}
#define RACK GEN_CODE(GC_NETHERRACK)

/* WorldGenGlowStone1 (and 2, the same): from under netherrack, 1500 tries to hang one by just one other */
static void feat_glow(JRand *r, int x, int y, int z) {
    if (wget(x, y, z) != B_AIR || wget(x, y + 1, z) != RACK) return;
    wput(x, y, z, B_GLOWSTONE);
    for (int i = 0; i < 1500; i++) {
        int a = jr_int(r, 8), b = jr_int(r, 8), dy = -jr_int(r, 12), c = jr_int(r, 8), d = jr_int(r, 8);
        int bx = x + a - b, by = y + dy, bz = z + c - d;
        if (wget(bx, by, bz) != B_AIR) continue;
        static const int8_t D[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
        int j = 0;
        for (int f = 0; f < 6 && j <= 1; f++) j += wget(bx + D[f][0], by + D[f][1], bz + D[f][2]) == B_GLOWSTONE;
        if (j == 1) wput(bx, by, bz, B_GLOWSTONE);
    }
}
/* WorldGenHellLava: under netherrack, in air or netherrack: walled on all five sides, or (not inside the
 * rock) on four with air on the fifth (it pours out there) */
static void feat_lava(int x, int y, int z, int inside) {
    if (wget(x, y + 1, z) != RACK) return;
    int b = wget(x, y, z);
    if (b != B_AIR && b != RACK) return;
    static const int8_t D[5][3] = {{-1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 1}, {0, -1, 0}};
    int i = 0, j = 0, open = 0;
    for (int f = 0; f < 5; f++) i += wget(x + D[f][0], y + D[f][1], z + D[f][2]) == RACK;
    for (int f = 0; f < 5; f++)
        if (wget(x + D[f][0], y + D[f][1], z + D[f][2]) == B_AIR) j++, open = f;
    if ((!inside && i == 4 && j == 1) || i == 5) {
        wput(x, y, z, B_LAVA);
        if (i == 4) wput(x + D[open][0], y + D[open][1], z + D[open][2], B_FLOWING_LAVA);
    }
}
/* WorldGenFire and WorldGenBush: 64 tries about the place */
static void feat_scatter(JRand *r, int x, int y, int z, int b) {
    for (int i = 0; i < 64; i++) {
        int a = jr_int(r, 8), c = jr_int(r, 8), e = jr_int(r, 4), f = jr_int(r, 4), g = jr_int(r, 8), k = jr_int(r, 8);
        int bx = x + a - c, by = y + e - f, bz = z + g - k;
        if (wget(bx, by, bz) != B_AIR) continue;
        int below = wget(bx, by - 1, bz);
        if (b == B_FIRE ? below == RACK : (below == RACK || below == B_GLOWSTONE || below == GEN_CODE(GC_SOUL_SAND)))
            wput(bx, by, bz, b);
    }
}
/* WorldGenMinable(quartz ore, 14), in netherrack: only the rows made matter */
static void feat_quartz(JRand *r, int x, int y, int z) {
    const int n = 14;
    float f = jr_float(r) * 3.1415927f;
    float d0 = (float)(x + 8) + mh_sin(f) * (float)n / 8.0f, d1 = (float)(x + 8) - mh_sin(f) * (float)n / 8.0f;
    float d2 = (float)(z + 8) + mh_cos(f) * (float)n / 8.0f, d3 = (float)(z + 8) - mh_cos(f) * (float)n / 8.0f;
    float d4 = (float)(y + jr_int(r, 3) - 2), d5 = (float)(y + jr_int(r, 3) - 2);
    for (int i = 0; i < n; i++) {
        float f1 = (float)i / (float)n;
        float d6 = d0 + (d1 - d0) * f1, d7 = d4 + (d5 - d4) * f1, d8 = d2 + (d3 - d2) * f1;
        float d9 = jr_doublef(r) * (float)n / 16.0f;
        float d10 = (mh_sin(3.1415927f * f1) + 1.0f) * d9 + 1.0f;
        int j = (int)floorf(d6 - d10 / 2), k = (int)floorf(d7 - d10 / 2), l = (int)floorf(d8 - d10 / 2);
        int i1 = (int)floorf(d6 + d10 / 2), j1 = (int)floorf(d7 + d10 / 2), k1 = (int)floorf(d8 + d10 / 2);
        if (i1 < nd.cx * 16 || j > nd.cx * 16 + 15 || k1 < nd.cz * 16 || l > nd.cz * 16 + 15 || j1 < nd.y0 || k >= nd.y0 + nd.h)
            continue;
        for (int bx = j; bx <= i1; bx++) {
            float d12 = ((float)bx + 0.5f - d6) / (d10 / 2);
            if (d12 * d12 >= 1) continue;
            for (int by = k; by <= j1; by++) {
                float d13 = ((float)by + 0.5f - d7) / (d10 / 2);
                if (d12 * d12 + d13 * d13 >= 1) continue;
                for (int bz = l; bz <= k1; bz++) {
                    float d14 = ((float)bz + 0.5f - d8) / (d10 / 2);
                    if (d12 * d12 + d13 * d13 + d14 * d14 >= 1) continue;
                    int lx = bx - nd.cx * 16, lz = bz - nd.cz * 16, ly = by - nd.y0;
                    if ((unsigned)lx > 15 || (unsigned)lz > 15 || ly < 0 || ly >= nd.h) continue;
                    uint8_t *c = &nd.out[(ly * 16 + lz) * 16 + lx];
                    if (*c == RACK) *c = GEN_CODE(GC_QUARTZ_ORE);
                }
            }
        }
    }
}

static void nether_populate(int px, int pz) {
    JRand r;
    jr_seed(&r, ((i64)px * 341873128712LL + (i64)pz * 132897987541LL) ^ g_seed);
    int bx = px * 16, bz = pz * 16;
    for (int i = 0; i < 8; i++) {
        int x = jr_int(&r, 16) + 8, y = jr_int(&r, 120) + 4, z = jr_int(&r, 16) + 8;
        feat_lava(bx + x, y, bz + z, 0);
    }
    for (int j = 0; j < jr_int(&r, jr_int(&r, 10) + 1) + 1; j++) {
        int x = jr_int(&r, 16) + 8, y = jr_int(&r, 120) + 4, z = jr_int(&r, 16) + 8;
        feat_scatter(&r, bx + x, y, bz + z, B_FIRE);
    }
    for (int k = 0; k < jr_int(&r, jr_int(&r, 10) + 1); k++) {
        int x = jr_int(&r, 16) + 8, y = jr_int(&r, 120) + 4, z = jr_int(&r, 16) + 8;
        feat_glow(&r, bx + x, y, bz + z);
    }
    for (int l = 0; l < 10; l++) {
        int x = jr_int(&r, 16) + 8, y = jr_int(&r, 128), z = jr_int(&r, 16) + 8;
        feat_glow(&r, bx + x, y, bz + z);
    }
    for (int m = 0; m < 2; m++)
        if (jr_bool(&r)) {
            int x = jr_int(&r, 16) + 8, y = jr_int(&r, 128), z = jr_int(&r, 16) + 8;
            feat_scatter(&r, bx + x, y, bz + z, m ? B_RED_MUSHROOM : B_BROWN_MUSHROOM);
        }
    for (int i = 0; i < 16; i++) {
        int x = jr_int(&r, 16), y = jr_int(&r, 108) + 10, z = jr_int(&r, 16);
        feat_quartz(&r, bx + x, y, bz + z);
    }
    for (int i = 0; i < 16; i++) {
        int x = jr_int(&r, 16), y = jr_int(&r, 108) + 10, z = jr_int(&r, 16);
        feat_lava(bx + x, y, bz + z, 1);
    }
}

static void nether_decorate(int cx, int cz, int y0, int h, uint8_t *out) {
    fort_build(cx, cz, y0, h, out);
    nd.cx = cx, nd.cz = cz, nd.y0 = y0, nd.h = h, nd.out = out;
    memset(WSET, 0, (size_t)WS_N * 4);
    for (int pz = cz - 1; pz <= cz; pz++)
        for (int px = cx - 1; px <= cx; px++) nether_populate(px, pz);
}
#undef RACK

/* ======================================================================== */
/* The End: ChunkProviderEnd                                                 */
/* ======================================================================== */

typedef struct {
    Octave oc[40];   /* noiseGen1 (16), noiseGen2 (16), noiseGen3 (8) */
} EndMem;
_Static_assert(sizeof(EndMem) <= GEN_CACHE_BYTES, "the End's state fits in the summary cache");
#define EM ((EndMem *)(void *)sumc)
static u64 SC_E1XZ, SC_E3XZ, SC_E3Y;

static void end_init(void) {
    EndMem *m = EM;
    memset(m, 0, sizeof *m);
    JRand r;
    jr_seed(&r, g_seed);
    for (int i = 0; i < 40; i++) m->oc[i].st = r.s, perm_skip(&r);
    SC_E1XZ = FX(684.412 * 2.0);
    SC_E3XZ = FX(684.412 * 2.0 / 80.0);
    SC_E3Y = FX(684.412 / 160.0);
}

/* initializeNoiseField: chunk (cx, cz)'s 3 x 33 x 3 densities (cells of 8 blocks across, 4 up), with the
 * island's falloff from (0, 0) */
static void end_grid(int cx, int cz, float *g) {
    EndMem *m = EM;
    float n1[297], n2[297], n3[297];
    memset(n1, 0, sizeof n1);
    memset(n2, 0, sizeof n2);
    memset(n3, 0, sizeof n3);
    gd_octaves(m->oc + 32, 8, n3, cx * 2, 0, cz * 2, 3, 33, 3, SC_E3XZ, SC_E3Y, SC_E3XZ);
    gd_octaves(m->oc, 16, n1, cx * 2, 0, cz * 2, 3, 33, 3, SC_E1XZ, FX(684.412), SC_E1XZ);
    gd_octaves(m->oc + 16, 16, n2, cx * 2, 0, cz * 2, 3, 33, 3, SC_E1XZ, FX(684.412), SC_E1XZ);
    int i = 0;
    for (int j = 0; j < 3; j++)
        for (int k = 0; k < 3; k++) {
            float f = (float)(j + cx * 2), f1 = (float)(k + cz * 2);
            float f2 = 100.0f - sqrtf(f * f + f1 * f1) * 8.0f;
            if (f2 > 80.0f) f2 = 80.0f;
            if (f2 < -100.0f) f2 = -100.0f;
            for (int l = 0; l < 33; l++, i++) {
                float d3 = n1[i] / 512.0f, d4 = n2[i] / 512.0f, d5 = (n3[i] / 10.0f + 1.0f) / 2.0f, d2;
                if (d5 < 0.0f) d2 = d3;
                else if (d5 > 1.0f) d2 = d4;
                else d2 = d3 + (d4 - d3) * d5;
                d2 -= 8.0f;
                d2 += f2;
                if (l > 33 / 2 - 2) {
                    float d6 = (float)(l - (33 / 2 - 2)) / 64.0f;
                    d6 = d6 < 0 ? 0 : d6 > 1 ? 1 : d6;
                    d2 = d2 * (1.0f - d6) + -3000.0f * d6;
                }
                if (l < 8) {
                    float d7 = (float)(8 - l) / 7.0f;
                    d2 = d2 * (1.0f - d7) + -30.0f * d7;
                }
                g[i] = d2;
            }
        }
}

static void end_decorate(int cx, int cz, int y0, int h, uint8_t *out);

/* the density at one place of the grid g of its chunk, summed as func_180520_a sums it: end stone or not */
static int end_point(const float *g, int lx, int lz, int y) {
    int j1 = lx >> 3, k1 = lz >> 3, l1 = y >> 2;
    float d1 = g[((j1 + 0) * 3 + k1 + 0) * 33 + l1], d2 = g[((j1 + 0) * 3 + k1 + 1) * 33 + l1];
    float d3 = g[((j1 + 1) * 3 + k1 + 0) * 33 + l1], d4 = g[((j1 + 1) * 3 + k1 + 1) * 33 + l1];
    float d5 = (g[((j1 + 0) * 3 + k1 + 0) * 33 + l1 + 1] - d1) * 0.25f;
    float d6 = (g[((j1 + 0) * 3 + k1 + 1) * 33 + l1 + 1] - d2) * 0.25f;
    float d7 = (g[((j1 + 1) * 3 + k1 + 0) * 33 + l1 + 1] - d3) * 0.25f;
    float d8 = (g[((j1 + 1) * 3 + k1 + 1) * 33 + l1 + 1] - d4) * 0.25f;
    for (int i2 = 0; i2 < (y & 3); i2++) d1 += d5, d2 += d6, d3 += d7, d4 += d8;
    float d10 = d1, d11 = d2, d12 = (d3 - d1) * 0.125f, d13 = (d4 - d2) * 0.125f;
    for (int j2 = 0; j2 < (lx & 7); j2++) d10 += d12, d11 += d13;
    float d15 = d10, d16 = (d11 - d10) * 0.125f;
    for (int k2 = 0; k2 < (lz & 7); k2++) d15 += d16;
    return d15 > 0.0f;
}
/* the End's terrain at one place (a grid kept for its chunk in g, gx, gz) */
static int end_solid(float *g, int *gc, int x, int y, int z) {
    if (y < 0 || y >= 128) return 0;
    int cx = x >> 4, cz = z >> 4;
    if (cx * cx + cz * cz > 30 * 30) return 0;
    if (gc[2] != 1 || gc[0] != cx || gc[1] != cz) end_grid(cx, cz, g), gc[0] = cx, gc[1] = cz, gc[2] = 1;
    return end_point(g, x & 15, z & 15, y);
}

/* BiomeEndDecorator: a chunk in five has an obsidian pillar (WorldGenSpikes) where its decoration lands on
 * end stone: 6 to 37 high, 1 to 4 across, bedrock on top (its crystal: end.c). 1.8.8 decorates the End with
 * the world's random; here each chunk's is its own, so the pillars are the same every time: listed once,
 * in U (the End has no other use for it) */
typedef struct {
    int n, ok;
    EndSpike s[64];
} SpikeList;
#define SPK ((SpikeList *)(void *)&U)
const EndSpike *gen_spikes(int *n) {
    SpikeList *l = SPK;
    if (!l->ok) {
        l->ok = 1, l->n = 0;
        float g[297];
        int gc[3] = {0, 0, 0};
        for (int cz = -8; cz <= 8; cz++)
            for (int cx = -8; cx <= 8; cx++) {
                JRand r;
                jr_seed(&r, ((i64)cx * 341873128712LL + (i64)cz * 132897987541LL) ^ g_seed);
                if (jr_int(&r, 5) != 0) continue;
                int x = cx * 16 + jr_int(&r, 16) + 8, z = cz * 16 + jr_int(&r, 16) + 8, y = 127;
                while (y >= 0 && !end_solid(g, gc, x, y, z)) y--;
                if (y < 0 || ++y >= 127) continue;
                int h = jr_int(&r, 32) + 6, rad = jr_int(&r, 4) + 1, ok = 1;
                for (int k = x - rad; k <= x + rad && ok; k++)
                    for (int l = z - rad; l <= z + rad && ok; l++)
                        if ((k - x) * (k - x) + (l - z) * (l - z) <= rad * rad + 1 && !end_solid(g, gc, k, y - 1, l)) ok = 0;
                if (!ok || l->n >= 64) continue;
                if (y + h > 127) h = 127 - y;
                l->s[l->n++] = (EndSpike){(int16_t)x, (int16_t)z, (uint8_t)y, (uint8_t)h, (uint8_t)rad, 0};
            }
    }
    *n = l->n;
    return l->s;
}
/* what the End has there as made (end stone, a pillar, air) */
int gen_end_block(int x, int y, int z) {
    int n;
    const EndSpike *s = gen_spikes(&n);
    for (int i = 0; i < n; i++) {
        int dx = x - s[i].x, dz = z - s[i].z;
        if (dx * dx + dz * dz <= s[i].r * s[i].r + 1 && y >= s[i].y && y <= s[i].y + s[i].h)
            return y == s[i].y + s[i].h ? B_BEDROCK : B_OBSIDIAN;
    }
    float g[297];
    int gc[3] = {0, 0, 0};
    return end_solid(g, gc, x, y, z) ? B_END_STONE : B_AIR;
}

/* func_180520_a: end stone where the density is above 0 (the surface pass changes nothing there) */
static void end_slab(int cx, int cz, int y0, int h, uint8_t *out) {
    float g[297];
    if (cx * cx + cz * cz > 30 * 30) {   /* (far out, the falloff leaves the void: no noise needed) */
        memset(out, B_AIR, (size_t)h * 256);
        return;
    }
    end_grid(cx, cz, g);
    for (int j1 = 0; j1 < 2; j1++)
        for (int k1 = 0; k1 < 2; k1++)
            for (int l1 = 0; l1 < 32; l1++) {
                float d1 = g[((j1 + 0) * 3 + k1 + 0) * 33 + l1], d2 = g[((j1 + 0) * 3 + k1 + 1) * 33 + l1];
                float d3 = g[((j1 + 1) * 3 + k1 + 0) * 33 + l1], d4 = g[((j1 + 1) * 3 + k1 + 1) * 33 + l1];
                float d5 = (g[((j1 + 0) * 3 + k1 + 0) * 33 + l1 + 1] - d1) * 0.25f;
                float d6 = (g[((j1 + 0) * 3 + k1 + 1) * 33 + l1 + 1] - d2) * 0.25f;
                float d7 = (g[((j1 + 1) * 3 + k1 + 0) * 33 + l1 + 1] - d3) * 0.25f;
                float d8 = (g[((j1 + 1) * 3 + k1 + 1) * 33 + l1 + 1] - d4) * 0.25f;
                for (int i2 = 0; i2 < 4; i2++) {
                    float d10 = d1, d11 = d2, d12 = (d3 - d1) * 0.125f, d13 = (d4 - d2) * 0.125f;
                    int y = l1 * 4 + i2 - y0;
                    for (int j2 = 0; j2 < 8; j2++) {
                        float d15 = d10, d16 = (d11 - d10) * 0.125f;
                        for (int k2 = 0; k2 < 8; k2++) {
                            if (y >= 0 && y < h)
                                out[(y * 16 + k2 + k1 * 8) * 16 + j2 + j1 * 8] =
                                    d15 > 0.0f ? GEN_CODE(GC_END_STONE) : B_AIR;
                            d15 += d16;
                        }
                        d10 += d12, d11 += d13;
                    }
                    d1 += d5, d2 += d6, d3 += d7, d4 += d8;
                }
            }
    end_decorate(cx, cz, y0, h, out);
}

static void end_decorate(int cx, int cz, int y0, int h, uint8_t *out) {
    int n;
    const EndSpike *s = gen_spikes(&n);
    for (int i = 0; i < n; i++) {
        const EndSpike *p = &s[i];
        if (p->x + p->r < cx * 16 || p->x - p->r > cx * 16 + 15 || p->z + p->r < cz * 16 || p->z - p->r > cz * 16 + 15) continue;
        for (int y = p->y; y <= p->y + p->h; y++) {
            if (y < y0 || y >= y0 + h) continue;
            for (int z = p->z - p->r; z <= p->z + p->r; z++)
                for (int x = p->x - p->r; x <= p->x + p->r; x++) {
                    int lx = x - cx * 16, lz = z - cz * 16;
                    if ((unsigned)lx > 15 || (unsigned)lz > 15) continue;
                    if ((x - p->x) * (x - p->x) + (z - p->z) * (z - p->z) > p->r * p->r + 1) continue;
                    out[((y - y0) * 16 + lz) * 16 + lx] = y == p->y + p->h ? B_BEDROCK : B_OBSIDIAN;
                }
        }
    }
}

/* ======================================================================== */
/* which generator runs                                                      */
/* ======================================================================== */

void gen_dim(int d) {
    gd_dim = d;
    memset(&U, 0, sizeof U);   /* (the fortress kept there, the Overworld's flags) */
    if (d == DIM_NETHER) nether_init();
    else if (d == DIM_END) end_init();
}

static int dims_slab(int cx, int cz, int y0, int h, uint8_t *out) {
    if (gd_dim == DIM_NETHER) nether_slab(cx, cz, y0, h, out);
    else if (gd_dim == DIM_END) end_slab(cx, cz, y0, h, out);
    else return 0;
    return 1;
}
