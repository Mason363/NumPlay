/* NumBlocks.nwa only (NB_DIMS), included by gen_dims.h: the Overworld's villages and desert temples
 * (MapGenVillage with StructureVillagePieces, MapGenScatteredFeature with ComponentScatteredFeaturePieces).
 *
 * Where: 1.8.8's grid of regions of 32 x 32 chunks, a start in each (World.setRandomSeed with the salt 10387312
 * for villages, 14357617 for temples, 24 places a side), kept when the biome there allows it.
 * Laid out: from MapGenBase's random of the start chunk, piece by piece as StructureVillagePieces does (roads,
 * then the houses along them, each kept where every biome around it is plains, desert or savanna).
 * Built: by the populations whose boxes they cross, before the lakes and the decoration, from the population's
 * random (the crops' ages, the blacksmith's chest), as 1.8.8 builds them; a village in a population's box
 * keeps its lakes away. How each piece is built comes from 1.8.8's own code (tools/structs.py:
 * gen_struct_data.h), its blocks turned the piece's way, and a desert village's wood and cobblestone turned
 * to sandstone.
 *
 * Differences from 1.8.8, as the population view goes (gen.c's top comment): a house's height is the average
 * ground (the terrain as made, without its caves and trees) under all of it, which 1.8.8 takes under the part
 * the first population to reach it holds (all of it for most houses); the pillars under houses stop at the
 * terrain without its caves; a desert temple's chests draw no enchanted book (see dungeon_book); jungle
 * temples and witch huts are not made. A house's villagers are spawned when its chunk is made (mob_villager).
 *
 * The layout is made in the scratch union U before the chunk's terrain, for each gen_slab near a village;
 * the pieces that cross the four populations' boxes are kept on gen_slab's stack (tp), their heights found
 * from the chunk summaries. Which regions have no village near, and each village's box, are remembered
 * (the struct cache, on main's stack next to the summary cache), so most chunks cost nothing. */

/* the palette's turned blocks (gen_struct_data.h) */
enum { VS_STAIRS = 0x1000, VS_TORCH = 0x2000 };
enum { VS_OAK_STAIRS, VS_COBBLESTONE_STAIRS, VS_SANDSTONE_STAIRS };
#include "gen_struct_data.h"
enum { K_SET, K_BOX, K_FILL, K_EXT };
enum { X_STEP, X_DOOR, X_CROP, X_CHEST, X_VILLAGERS, X_CLEAR, X_DOWN, X_IF, X_END };

enum { VT_WELL, VT_PATH, VT_TORCH, VT_HOUSE4, VT_CHURCH, VT_HOUSE1, VT_WOODHUT, VT_HALL, VT_FIELD1, VT_FIELD2,
       VT_HOUSE2, VT_HOUSE3, VT_DESERT };
/* the houses as StructureVillagePieces.getStructureVillageWeightedPieceList lists them: weight, how many
 * (getRandomIntegerInRange(min, max)), size (x, y, z) */
static const uint8_t vw_weight[9] = {4, 20, 20, 3, 15, 3, 3, 15, 8};
static const uint8_t vw_min[9] = {2, 0, 0, 2, 0, 1, 2, 0, 0}, vw_max[9] = {4, 1, 2, 5, 2, 4, 4, 1, 3};
static const uint8_t vt_size[VT_DESERT + 1][3] = {
    {6, 15, 6}, {3, 3, 0}, {3, 4, 2}, {5, 6, 5}, {5, 12, 9}, {9, 9, 6}, {4, 6, 5}, {9, 7, 11}, {13, 4, 9}, {7, 4, 9},
    {10, 6, 7}, {9, 7, 12}, {21, 15, 21}};
static const uint16_t vt_prog[VT_DESERT + 1] = {VSP_WELL, 0, VSP_TORCH0, VSP_HOUSE4GARDEN, VSP_CHURCH, VSP_HOUSE1,
                                                VSP_WOODHUT, VSP_HALL, VSP_FIELD1, VSP_FIELD2, VSP_HOUSE2, VSP_HOUSE3,
                                                VSP_DESERTPYRAMID};

/* ---- the struct cache: per region of the grid, what is there (on main's stack: gen_set_cache) */
typedef struct {
    int16_t rx, rz;     /* the region */
    uint8_t kind, ok;   /* 1 village, 2 temple; ok: the start chunk's biome allows it, and (a village) it is
                         * sizeable */
    int16_t bb[4];      /* x0, z0, x1, z1 of all of it */
} SCache;
#define NSC 8
static SCache *scache;
static uint8_t sc_next;
/* and which cells of GenLayerRiverMix's 1:4 grid are plains, desert or savanna, in tiles of 16 x 16 cells, the
 * last 8 (a village's layout looks at the same cells again and again) */
typedef struct {
    int16_t tx, tz;
    uint8_t ok, pad[3];
    uint32_t bits[8];
} VTile;
#define NVT 8
static VTile *vtile;
static uint8_t vt_next;

/* the pieces gen_slab builds (the four populations'), on its stack (STRUCT_MEM) */
typedef struct {
    int16_t x0, z0, x1, z1, y0;
    uint8_t type, face, flag, s;   /* s: which structure (ts) */
} TP;
#define NTP 40
typedef struct {
    int16_t bb[4];
    uint8_t kind, desert;
} TS;
#define NTS 4
_Static_assert(NTP * sizeof(TP) + NTS * sizeof(TS) <= STRUCT_MEM, "gen.c's STRUCT_MEM holds the pieces");
_Static_assert(NSC * sizeof(SCache) + NVT * sizeof(VTile) <= GEN_STRUCT_BYTES, "gen.h's GEN_STRUCT_BYTES holds them");
static TP *tp;
static TS *ts;
static int ntp, nts;
#ifdef HOST
static uint32_t sc_mem[GEN_STRUCT_BYTES / 4];
#endif
static void struct_cache(void *mem) {
#ifdef HOST
    if (!mem) mem = sc_mem;
#endif
    if (mem) scache = mem, vtile = (VTile *)(void *)((uint8_t *)mem + NSC * sizeof(SCache));
    if (scache) memset(scache, 0, NSC * sizeof(SCache) + NVT * sizeof(VTile));
}

/* ======================================================================== */
/* the biomes there                                                         */
/* ======================================================================== */
static int v_cell(int cx, int cz);
/* WorldChunkManager.areBiomesViable(x, z, r, plains, desert, savanna): every cell of GenLayerRiverMix's 1:4
 * grid from (x - r) >> 2 to (x + r) >> 2 */
static int v_biomes(int x, int z, int r) {
    for (int cz = (z - r) >> 2; cz <= (z + r) >> 2; cz++)
        for (int cx = (x - r) >> 2; cx <= (x + r) >> 2; cx++)
            if (!v_cell(cx, cz)) return 0;
    return 1;
}

/* the start chunk of the region (rx, rz) of the grid (MapGen*.canSpawnStructureAtCoords: World.setRandomSeed) */
static void grid_start(int rx, int rz, i32 salt, int *sx, int *sz) {
    JRand r;
    jr_seed(&r, (i64)((u64)(i64)rx * 341873128712ULL + (u64)(i64)rz * 132897987541ULL + (u64)g_seed + (u64)(i64)salt));
    *sx = rx * 32 + jr_int(&r, 24);
    *sz = rz * 32 + jr_int(&r, 24);
}

/* MapGenBase's random for chunk (x, z), as MapGenStructure.recursiveGenerate leaves it (one nextInt) */
static void struct_rand(JRand *r, int x, int z) {
    jr_seed(r, (i64)((u64)(i64)x * (u64)cave_mul_x ^ (u64)(i64)z * (u64)cave_mul_z ^ (u64)g_seed));
    jr_next(r, 32);
}

/* ======================================================================== */
/* villages: the layout (MapGenVillage.Start), in U                          */
/* ======================================================================== */
#define VMAX 128
typedef struct {
    int n, nroad, nhouse, last, x0, z0;   /* pieces; roads and houses waiting; the last weight placed; the start */
    uint8_t wl[9], wn, cnt[9], lim[9];    /* the weights left (houses), how many each placed, how many each may */
    int16_t bb[6];
    FPiece p[VMAX];
    uint8_t road[VMAX], house[VMAX];
} Vill;
#define VL ((Vill *)(void *)&U)

static int v_meets(const int16_t *b) {   /* StructureComponent.findIntersecting */
    const Vill *v = VL;
    for (int i = 0; i < v->n; i++)
        if (boxes_meet(b, &v->p[i])) return i;
    return -1;
}
static int v_add(const int16_t *b, int type, int face, int depth) {
    Vill *v = VL;
    if (v->n >= VMAX) return -1;
    FPiece *p = &v->p[v->n];
    p->x0 = b[0], p->y0 = b[1], p->z0 = b[2], p->x1 = b[3], p->y1 = b[4], p->z1 = b[5];
    p->type = (uint8_t)type, p->face = (uint8_t)face, p->depth = (uint8_t)depth, p->flag = 0, p->seed = 0;
    return v->n++;
}
/* func_176066_d / func_176069_e's test: the biomes around the piece's middle */
static int v_viable(int i) {
    const FPiece *p = &VL->p[i];
    int k = p->x1 - p->x0, l = p->z1 - p->z0, m = k > l ? k : l;
    return v_biomes((p->x0 + p->x1) / 2, (p->z0 + p->z1) / 2, m / 2 + 4);
}

/* cell (cx, cz) of the 1:4 grid is plains, desert or savanna: its tile's, made in U past the layout */
static int v_cell(int cx, int cz) {
    int tx = cx >> 4, tz = cz >> 4, i = 0;
    while (i < NVT && !(vtile[i].ok && vtile[i].tx == tx && vtile[i].tz == tz)) i++;
    VTile *t = i < NVT ? &vtile[i] : &vtile[vt_next++ % NVT];
    if (i == NVT) {
        i32 *out = (i32 *)(void *)((uint8_t *)&U + ((sizeof(Vill) + 3) & ~(size_t)3));
        lay_mem = out + 256;
        lay_cap = (int)((sizeof U - (size_t)((uint8_t *)lay_mem - (uint8_t *)&U)) / sizeof(i32));
        lay_top = lay_peak = 0;
        layers_rivermix(tx * 16, tz * 16, 16, 16, out);
#ifdef HOST
        if (lay_peak > lay_cap) {
            extern int printf(const char *, ...);
            printf("gen_struct.h: the layers needed %d ints of %d\n", lay_peak, lay_cap);
        }
#endif
        memset(t->bits, 0, sizeof t->bits);
        for (int k = 0; k < 256; k++)
            if (out[k] == BI_PLAINS || out[k] == BI_DESERT || out[k] == BI_SAVANNA) t->bits[k >> 5] |= 1u << (k & 31);
        t->tx = (int16_t)tx, t->tz = (int16_t)tz, t->ok = 1;
    }
    int k = (cz & 15) * 16 + (cx & 15);
    return t->bits[k >> 5] >> (k & 31) & 1;
}

/* func_176065_a: a house of weight t there, if it fits, with what it draws when made */
static int v_house(JRand *r, int t, int x, int y, int z, int face, int depth) {
    int type = VT_HOUSE4 + t;
    int8_t sz[6] = {0, 0, 0, (int8_t)vt_size[type][0], (int8_t)vt_size[type][1], (int8_t)vt_size[type][2]};
    int16_t b[6];
    box_for(x, y, z, sz, face, b);
    if ((type != VT_HOUSE4 && b[1] <= 10) || v_meets(b) >= 0) return -1;   /* (canVillageGoDeeper) */
    int i = v_add(b, type, face, depth);
    if (i < 0) return -1;
    FPiece *p = &VL->p[i];
    if (type == VT_HOUSE4) p->flag = (uint8_t)jr_bool(r);                    /* isRoofAccessible */
    else if (type == VT_WOODHUT) {
        int tall = jr_bool(r);
        p->flag = (uint8_t)(tall | jr_int(r, 3) << 1);                          /* isTallHouse, tablePosition */
    } else if (type == VT_FIELD1 || type == VT_FIELD2) {
        for (int k = 0; k < (type == VT_FIELD1 ? 4 : 2); k++) {                /* the crops: carrots, potatoes, */
            int c = jr_int(r, 5);                                               /* wheat */
            p->flag |= (uint8_t)((c == 0 ? 1 : c == 1 ? 2 : 0) << (2 * k));
        }
    }
    return i;
}

/* func_176067_c: a house from the weights left (five tries), else a lamp, if it fits */
static int v_pick(JRand *r, int x, int y, int z, int face, int depth) {
    Vill *v = VL;
    int total = 0, any = 0;
    for (int i = 0; i < v->wn; i++) {
        int t = v->wl[i];
        if (v->lim[t] > 0 && v->cnt[t] < v->lim[t]) any = 1;
        total += vw_weight[t];
    }
    if (!any || total <= 0) return -1;
    for (int j = 0; j < 5; j++) {
        int k = jr_int(r, total);
        for (int i = 0; i < v->wn; i++) {
            int t = v->wl[i];
            k -= vw_weight[t];
            if (k < 0) {
                if (!(v->lim[t] == 0 || v->cnt[t] < v->lim[t]) || (t == v->last && v->wn > 1)) break;
                int p = v_house(r, t, x, y, z, face, depth);
                if (p >= 0) {
                    v->cnt[t]++;
                    v->last = t;
                    if (!(v->lim[t] == 0 || v->cnt[t] < v->lim[t])) {
                        memmove(&v->wl[i], &v->wl[i + 1], (size_t)(v->wn - i - 1));
                        v->wn--;
                    }
                    return p;
                }
            }
        }
    }
    static const int8_t lamp[6] = {0, 0, 0, 3, 4, 2};
    int16_t b[6];
    box_for(x, y, z, lamp, face, b);
    if (v_meets(b) >= 0) return -1;
    return v_add(b, VT_TORCH, face, depth);
}

/* func_176066_d: a house (or lamp) by a road, kept if the biomes around it allow it */
static int v_house_at(JRand *r, int x, int y, int z, int face, int depth) {
    Vill *v = VL;
    if (depth > 50 || iabs(x - v->x0) > 112 || iabs(z - v->z0) > 112) return -1;
    int p = v_pick(r, x, y, z, face, depth + 1);
    if (p < 0) return -1;
    if (!v_viable(p)) {
        v->n--;   /* (made and dropped: its draws and its weight's count stay) */
        return -1;
    }
    v->house[v->nhouse++] = (uint8_t)p;
    return p;
}

/* func_176069_e: a road (Path.func_175848_a: 35, 28 or 21 long, the longest that fits) */
static void v_road(JRand *r, int x, int y, int z, int face, int depth) {
    Vill *v = VL;
    if (depth > 3 || iabs(x - v->x0) > 112 || iabs(z - v->z0) > 112) return;
    int8_t sz[6] = {0, 0, 0, 3, 3, 0};
    int16_t b[6];
    int i = 7 * (3 + jr_int(r, 3));
    for (; i >= 7; i -= 7) {
        sz[5] = (int8_t)i;
        box_for(x, y, z, sz, face, b);
        if (v_meets(b) < 0) break;
    }
    if (i < 7 || b[1] <= 10) return;
    int p = v_add(b, VT_PATH, face, depth);
    if (p < 0) return;
    if (!v_viable(p)) {
        v->n--;
        return;
    }
    v->road[v->nroad++] = (uint8_t)p;
}

/* Path.buildComponent: houses along both sides, then maybe a road off each end */
static void v_path_build(JRand *r, int pi) {
    FPiece p = VL->p[pi];
    int len = p.x1 - p.x0 > p.z1 - p.z0 ? p.x1 - p.x0 + 1 : p.z1 - p.z0 + 1, flag = 0;
    int ns = p.face == F_N || p.face == F_S;
    for (int side = 0; side < 2; side++)
        for (int i = jr_int(r, 5); i < len - 8; i += 2 + jr_int(r, 5)) {
            int c = side == 0 ? (ns ? v_house_at(r, p.x0 - 1, p.y0, p.z0 + i, F_W, p.depth)
                                    : v_house_at(r, p.x0 + i, p.y0, p.z0 - 1, F_N, p.depth))
                              : (ns ? v_house_at(r, p.x1 + 1, p.y0, p.z0 + i, F_E, p.depth)
                                    : v_house_at(r, p.x0 + i, p.y0, p.z1 + 1, F_S, p.depth));
            if (c >= 0) {
                const FPiece *q = &VL->p[c];
                int a = q->x1 - q->x0 + 1, b = q->z1 - q->z0 + 1;
                i += a > b ? a : b;
                flag = 1;
            }
        }
    if (flag && jr_int(r, 3) > 0) switch (p.face) {
        case F_N: v_road(r, p.x0 - 1, p.y0, p.z0, F_W, p.depth); break;
        case F_S: v_road(r, p.x0 - 1, p.y0, p.z1 - 2, F_W, p.depth); break;
        case F_W: v_road(r, p.x0, p.y0, p.z0 - 1, F_N, p.depth); break;
        default: v_road(r, p.x1 - 2, p.y0, p.z0 - 1, F_N, p.depth); break;
        }
    if (flag && jr_int(r, 3) > 0) switch (p.face) {
        case F_N: v_road(r, p.x1 + 1, p.y0, p.z0, F_E, p.depth); break;
        case F_S: v_road(r, p.x1 + 1, p.y0, p.z1 - 2, F_E, p.depth); break;
        case F_W: v_road(r, p.x0, p.y0, p.z1 + 1, F_S, p.depth); break;
        default: v_road(r, p.x1 - 2, p.y0, p.z1 + 1, F_S, p.depth); break;
        }
}

/* MapGenVillage.Start: the village starting in chunk (sx, sz); 1 if it has more than two pieces off the roads
 * (isSizeableStructure) */
static int v_layout(int sx, int sz) {
    Vill *v = VL;
    JRand r;
    struct_rand(&r, sx, sz);
    v->n = v->nroad = v->nhouse = 0, v->last = -1, v->wn = 0;
    for (int t = 0; t < 9; t++) {   /* getRandomIntegerInRange: min >= max draws nothing */
        int lim = vw_min[t] >= vw_max[t] ? vw_min[t] : vw_min[t] + jr_int(&r, vw_max[t] - vw_min[t] + 1);
        v->lim[t] = (uint8_t)lim, v->cnt[t] = 0;
        if (lim) v->wl[v->wn++] = (uint8_t)t;
    }
    /* the well, turned at random */
    int x = (sx << 4) + 2, z = (sz << 4) + 2;
    v->x0 = x, v->z0 = z;
    int16_t b[6] = {(int16_t)x, 64, (int16_t)z, (int16_t)(x + 5), 78, (int16_t)(z + 5)};
    int face = jr_int(&r, 4);
    v_add(b, VT_WELL, face, 0);
    v_road(&r, x - 1, 74, z + 1, F_W, 0);
    v_road(&r, x + 6, 74, z + 1, F_E, 0);
    v_road(&r, x + 1, 74, z - 1, F_N, 0);
    v_road(&r, x + 1, 74, z + 6, F_S, 0);
    while (v->nroad || v->nhouse) {
        if (!v->nroad) {
            int i = jr_int(&r, v->nhouse);
            memmove(&v->house[i], &v->house[i + 1], (size_t)(v->nhouse - i - 1));
            v->nhouse--;   /* (a house builds nothing more) */
        } else {
            int j = jr_int(&r, v->nroad), p = v->road[j];
            memmove(&v->road[j], &v->road[j + 1], (size_t)(v->nroad - j - 1));
            v->nroad--;
            v_path_build(&r, p);
        }
    }
    int16_t *bb = v->bb;
    bb[0] = bb[1] = bb[2] = 32767, bb[3] = bb[4] = bb[5] = -32768;
    int houses = 0;
    for (int i = 0; i < v->n; i++) {
        const FPiece *p = &v->p[i];
        if (p->x0 < bb[0]) bb[0] = p->x0;
        if (p->y0 < bb[1]) bb[1] = p->y0;
        if (p->z0 < bb[2]) bb[2] = p->z0;
        if (p->x1 > bb[3]) bb[3] = p->x1;
        if (p->y1 > bb[4]) bb[4] = p->y1;
        if (p->z1 > bb[5]) bb[5] = p->z1;
        houses += p->type != VT_PATH;
    }
    return houses > 2;
}

/* ======================================================================== */
/* where they are, for a chunk                                               */
/* ======================================================================== */
static SCache *sc_find(int rx, int rz, int kind) {
    if (!scache) return 0;
    for (int i = 0; i < NSC; i++)
        if (scache[i].kind == kind && scache[i].rx == rx && scache[i].rz == rz) return &scache[i];
    return 0;
}
static SCache *sc_new(int rx, int rz, int kind) {
    static SCache spare;
    SCache *c = scache ? &scache[sc_next++ % NSC] : &spare;
    memset(c, 0, sizeof *c);
    c->rx = (int16_t)rx, c->rz = (int16_t)rz, c->kind = (uint8_t)kind;
    return c;
}

/* the box the four populations writing into chunk (cx, cz) cover */
static void pop_area(int cx, int cz, int *a) {
    a[0] = (cx - 1) * 16 + 8, a[1] = (cz - 1) * 16 + 8, a[2] = cx * 16 + 23, a[3] = cz * 16 + 23;
}
static int box2_meet(const int16_t *b, const int *a) { return b[0] <= a[2] && b[2] >= a[0] && b[1] <= a[3] && b[3] >= a[1]; }

/* the ground under a piece (Village.getAverageGroundLevel, over all of it): its columns' top solid block + 1,
 * 64 at least, from the chunk summaries */
static int v_ground(const TP *t) {
    int sum = 0, n = 0;
    for (int z = t->z0; z <= t->z1; z++)
        for (int x = t->x0; x <= t->x1; x++) {
            const SumEntry *e = sum_get(x >> 4, z >> 4);
            const ColSum *s = &e->s[(x & 15) + (z & 15) * 16];
            int y = s->y < 62 ? (SUM_LIQ[s->c & 3] == B_ICE ? 62 : s->y) : s->y;   /* (ice stops, water not) */
            sum += y + 1 > 64 ? y + 1 : 64, n++;
        }
    return sum / n;
}

static int tp_add(const FPiece *p, int s) {
    if (ntp >= NTP) return 0;
    TP *t = &tp[ntp++];
    t->x0 = p->x0, t->z0 = p->z0, t->x1 = p->x1, t->z1 = p->z1, t->y0 = p->y0;
    t->type = p->type, t->face = p->face, t->flag = p->flag, t->s = (uint8_t)s;
    return 1;
}

/* the villages that may reach the four populations writing into chunk (cx, cz): their pieces there */
static void villages_near(int cx, int cz, const int *a) {
    for (int rz = (cz - 10) >> 5; rz <= (cz + 10) >> 5; rz++)
        for (int rx = (cx - 10) >> 5; rx <= (cx + 10) >> 5; rx++) {
            int sx, sz;
            grid_start(rx, rz, 10387312, &sx, &sz);
            if (iabs(sx - cx) > 10 || iabs(sz - cz) > 10) continue;
            SCache *c = sc_find(rx, rz, 1);
            if (c && (!c->ok || !box2_meet(c->bb, a))) continue;
            if (!c) {
                c = sc_new(rx, rz, 1);
                /* MapGenVillage.canSpawnStructureAtCoords: the start chunk's middle */
                if (!v_biomes(sx * 16 + 8, sz * 16 + 8, 0)) continue;
            }
            int desert = gen_biome(sx * 16 + 2, sz * 16 + 2);   /* (Start: the biome at the well) */
            desert = desert == BI_DESERT || desert == BI_DESERT_HILLS;
            int ok = v_layout(sx, sz);
            const int16_t *b = VL->bb;
            c->ok = (uint8_t)ok, c->bb[0] = b[0], c->bb[1] = b[2], c->bb[2] = b[3], c->bb[3] = b[5];
            if (!ok || !box2_meet(c->bb, a) || nts >= NTS) continue;
            TS *s = &ts[nts];
            memcpy(s->bb, c->bb, sizeof s->bb);
            s->kind = 1, s->desert = (uint8_t)desert;
            for (int i = 0; i < VL->n; i++) {
                const FPiece *p = &VL->p[i];
                int16_t pb[4] = {p->x0, p->z0, p->x1, p->z1};
                if (box2_meet(pb, a)) tp_add(p, nts);
            }
            nts++;
        }
}

/* the desert temples that may reach them (MapGenScatteredFeature: in a desert or desert hills; jungle temples
 * and witch huts are not made). Feature: 21 x 15 x 21 at y 64, turned at random */
static void temples_near(int cx, int cz, const int *a) {
    for (int rz = (cz - 2) >> 5; rz <= (cz + 2) >> 5; rz++)
        for (int rx = (cx - 2) >> 5; rx <= (cx + 2) >> 5; rx++) {
            int sx, sz;
            grid_start(rx, rz, 14357617, &sx, &sz);
            int16_t bb[4] = {(int16_t)(sx * 16), (int16_t)(sz * 16), (int16_t)(sx * 16 + 20), (int16_t)(sz * 16 + 20)};
            if (!box2_meet(bb, a) || nts >= NTS) continue;
            SCache *c = sc_find(rx, rz, 2);
            if (!c) {
                c = sc_new(rx, rz, 2);
                int b = gen_biome(sx * 16 + 8, sz * 16 + 8);
                c->ok = b == BI_DESERT || b == BI_DESERT_HILLS;
                memcpy(c->bb, bb, sizeof bb);
            }
            if (!c->ok) continue;
            JRand r;
            struct_rand(&r, sx, sz);
            FPiece p;
            memset(&p, 0, sizeof p);
            p.x0 = bb[0], p.z0 = bb[1], p.x1 = bb[2], p.z1 = bb[3], p.y0 = 64, p.y1 = 78;
            p.type = VT_DESERT, p.face = (uint8_t)jr_int(&r, 4);
            TS *s = &ts[nts];
            memcpy(s->bb, bb, sizeof s->bb);
            s->kind = 2, s->desert = 0;
            tp_add(&p, nts++);
        }
}

/* gen_slab's start: the pieces the four populations writing into chunk (cx, cz) build (none in a chunk of a
 * world made before NumBlocks had them), their heights found */
static void structs_prepare(int cx, int cz) {
    ntp = nts = 0;
    if (gd_dim || gen_old_chunk(cx, cz)) return;
    int a[4];
    pop_area(cx, cz, a);
    villages_near(cx, cz, a);
    temples_near(cx, cz, a);
    for (int i = 0; i < ntp; i++) {
        TP *t = &tp[i];
        if (t->type == VT_PATH || t->type == VT_DESERT) continue;
        int k = v_ground(t);
        t->y0 = (int16_t)(t->type == VT_WELL ? k - 11 : k);
    }
}
static void structs_slab(int cx, int cz, void *smem) {
    tp = smem;
    ts = (TS *)(void *)((uint8_t *)smem + NTP * sizeof(TP));
    structs_prepare(cx, cz);
}

/* ======================================================================== */
/* building, in a population                                                */
/* ======================================================================== */
static int pb_in(int x, int y, int z) { return x >= PK + 8 && x <= PK + 23 && z >= PL + 8 && z <= PL + 23 && y >= 1; }
static int tp_x(const TP *t, int x, int z) { return t->face == F_W ? t->x1 - z : t->face == F_E ? t->x0 + z : t->x0 + x; }
static int tp_z(const TP *t, int x, int z) { return t->face == F_N ? t->z1 - z : t->face == F_S ? t->z0 + z : t->z0 + x; }

/* a block of NumBlocks.nwa (9 bits) into the view (as one of 8 bits it acts like) and into C */
static void put9(int X, int Y, int Z, int b) {
    if (b < 256) {
        put(X, Y, Z, (uint8_t)b, R_ALWAYS);
        return;
    }
    uint8_t like = b >= B_SANDSTONE_STAIRS_E ? B_COBBLESTONE_STAIRS : b == B_IRON_BARS ? B_GLASS_PANE : B_TORCH;
    put(X, Y, Z, like, R_ALWAYS);
    if (!in_c_range(X, Y, Z)) return;
    int c = (Y - c_y0) * 256 + (Z - c_cz * 16) * 16 + (X - c_cx * 16);
    if (gen_hi) {
        c_out[c] = (uint8_t)b;
        gen_hi[c >> 3] |= (uint8_t)(1 << (c & 7));
    }
}

/* getMetadataWithOffset of stairs (0 east, 1 west, 2 south, 3 north) for a piece facing f (N, E, S, W) */
static int vs_stairs(int f, int m) {
    static const uint8_t T[4][4] = {{0, 1, 2, 3}, {2, 3, 1, 0}, {0, 1, 3, 2}, {2, 3, 0, 1}};
    return T[f][m];
}
static const uint16_t stair_id[3][4] = {{B_OAK_STAIRS, B_OAK_STAIRS_W, B_OAK_STAIRS_S, B_OAK_STAIRS_N},
                                        {B_COBBLESTONE_STAIRS, B_COBBLESTONE_STAIRS_W, B_COBBLESTONE_STAIRS_S,
                                         B_COBBLESTONE_STAIRS_N},
                                        {B_SANDSTONE_STAIRS_E, B_SANDSTONE_STAIRS_W, B_SANDSTONE_STAIRS_S,
                                         B_SANDSTONE_STAIRS_N}};
/* a palette entry as the piece places it: turned, and (Village.func_175847_a) a desert village's */
static int vs_block(const TP *t, int v, int desert) {
    if (v & VS_STAIRS) {
        int fam = (v >> 4) & 15;
        return stair_id[desert ? VS_SANDSTONE_STAIRS : fam][vs_stairs(t->face, v & 3)];
    }
    if (v & VS_TORCH) {
        static const uint8_t torch[4] = {B_TORCH_N, B_TORCH_E, B_TORCH_S, B_TORCH_W};
        return torch[(t->face + (v & 3)) & 3];
    }
    if (desert) {
        if (v == B_LOG_OAK || v == B_COBBLESTONE || v == B_GRAVEL) return B_SANDSTONE;
        if (v == B_PLANKS_OAK) return B_SANDSTONE_SMOOTH;
    }
    return v;
}

static void vs_xyz(const uint8_t *p, int *x, int *y, int *z) {
    int v = p[0] | p[1] << 8;
    *x = v & 31, *y = ((v >> 5) & 31) - 16, *z = (v >> 10) & 31;
    if (*x == 31) *x = -1;
    if (*z == 31) *z = -1;
}
static int is_liq(int b) { return (bflags[b] & BF_LIQUID) != 0; }

/* WeightedRandomChestContent.generateChestContents's draws: per item its pick, its count, its slot (the loot
 * itself is drawn when the chest is first opened: gui.c) */
static void chest_draws(int n, int total, const uint8_t *w, const uint8_t *span) {
    for (int i = 0; i < n; i++) {
        int k = RI(total), j = 0;
        while ((k -= w[j]) >= 0) j++;
        RI(span[j]);
        RI(27);
    }
}

/* a box of one block, the rows from the top down (so the view follows the air it leaves) */
static void vs_box(const TP *t, int x0, int y0, int z0, int x1, int y1, int z1, int b, int bin) {
    for (int y = y1; y >= y0; y--)
        for (int x = x0; x <= x1; x++)
            for (int z = z0; z <= z1; z++) {
                int X = tp_x(t, x, z), Y = t->y0 + y, Z = tp_z(t, x, z);
                if (!pb_in(X, Y, Z)) continue;
                int in = bin >= 0 && y != y0 && y != y1 && x != x0 && x != x1 && z != z0 && z != z1;
                put9(X, Y, Z, in ? bin : b);
            }
}

/* addComponentParts: piece t's program, the parts in population P's box */
static void vs_run(const TP *t, const uint8_t *p, int desert) {
    for (;;) {
        int op = *p++, k = op >> 6, a = op & 63, x, y, z, x1, y1, z1;
        if (k == K_SET) {
            vs_xyz(p, &x, &y, &z), p += 2;
            vs_box(t, x, y, z, x, y, z, vs_block(t, vs_pal[a], desert), -1);
            continue;
        }
        if (k == K_BOX || k == K_FILL) {
            int in = k == K_FILL ? vs_block(t, vs_pal[*p++], desert) : -1;
            vs_xyz(p, &x, &y, &z), vs_xyz(p + 2, &x1, &y1, &z1), p += 4;
            vs_box(t, x, y, z, x1, y1, z1, vs_block(t, vs_pal[a], desert), in);
            continue;
        }
        if (a == X_END) return;
        if (a == X_IF) {
            int sh = p[0] >> 4, m = p[0] & 15, val = p[1], len = p[2];
            p += 3;
            if (((t->flag >> sh) & m) != val) p += len;
            continue;
        }
        vs_xyz(p, &x, &y, &z), p += 2;
        int X = tp_x(t, x, z), Y = t->y0 + y, Z = tp_z(t, x, z);
        switch (a) {
        case X_STEP: {
            /* a stone step in front of the door: air there, something under it (outside the box: air) */
            int here = pb_in(X, Y, Z) ? vget(X, Y, Z) : B_AIR, under = pb_in(X, Y - 1, Z) ? vget(X, Y - 1, Z) : B_AIR;
            if (here == B_AIR && under != B_AIR && pb_in(X, Y, Z))
                put9(X, Y, Z, stair_id[desert ? VS_SANDSTONE_STAIRS : VS_COBBLESTONE_STAIRS][vs_stairs(t->face, 3)]);
            break;
        }
        case X_DOOR: {
            /* placeDoorCurrentPosition: ItemDoor.placeDoor, the lower half's meta getMetadataWithOffset(door, m) */
            int m = *p++, f = t->face;
            m = f == F_S ? (m == 0 ? 2 : m == 2 ? 0 : m) : f == F_W ? (m + 1) & 3 : f == F_E ? (m + 3) & 3 : m;
            static const uint8_t lower[4] = {B_DOOR_OAK_LOWER, B_DOOR_OAK_LOWER_S, B_DOOR_OAK_LOWER_W, B_DOOR_OAK_LOWER_N};
            if (pb_in(X, Y, Z)) put(X, Y, Z, lower[m], R_ALWAYS), put(X, Y + 1, Z, B_DOOR_OAK_UPPER, R_ALWAYS);
            break;
        }
        case X_CROP: {
            /* a crop of the field's kind, its age MathHelper.getRandomIntegerInRange(2, 7) (drawn even outside) */
            int c = (t->flag >> (2 * *p++)) & 3, age = 2 + RI(6);
            if (!pb_in(X, Y, Z)) break;
            int st = age < 2 ? 0 : age < 4 ? 1 : age < 7 ? 2 : 3;
            put(X, Y, Z, (uint8_t)(c == 0 ? B_WHEAT_0 + age : c == 1 ? B_CARROTS_0 + st : B_POTATOES_0 + st), R_ALWAYS);
            break;
        }
        case X_CHEST: {
            int kind = *p++;
            if (kind == 0) {
                /* House2: the blacksmith's, 3 to 8 of 17 kinds (94 in weights), by the population that holds it */
                static const uint8_t w[17] = {3, 10, 5, 15, 15, 5, 5, 5, 5, 5, 5, 5, 5, 3, 1, 1, 1};
                static const uint8_t s[17] = {3, 5, 3, 3, 3, 1, 1, 1, 1, 1, 1, 5, 5, 1, 1, 1, 1};
                if (!pb_in(X, Y, Z)) break;
                int n = 3 + RI(6);
                if (vget(X, Y, Z) != B_CHEST) put(X, Y, Z, B_CHEST, R_ALWAYS), chest_draws(n, 94, w, s);
            } else {
                /* DesertPyramid: 2 to 6 of 11 kinds (73 in weights, the book's draws not made) */
                static const uint8_t w[11] = {3, 10, 15, 2, 20, 16, 3, 1, 1, 1, 1};
                static const uint8_t s[11] = {3, 5, 6, 3, 3, 5, 1, 1, 1, 1, 1};
                int n = 2 + RI(5);
                if (pb_in(X, Y, Z) && vget(X, Y, Z) != B_CHEST) put(X, Y, Z, B_CHEST, R_ALWAYS), chest_draws(n, 73, w, s);
            }
            break;
        }
        case X_VILLAGERS: {
            /* spawnVillagers: n of them in a row, while in the population's box (when their chunk is made);
             * the church's a priest, the library's a librarian, the blacksmith's one, the butcher's first */
            int n = *p++;
            for (int i = 0; i < n; i++) {
                int vx = tp_x(t, x + i, z), vz = tp_z(t, x + i, z);
                if (!pb_in(vx, Y, vz)) break;
                if (!in_c_range(vx, Y, vz)) continue;
                int prof = t->type == VT_CHURCH ? 2 : t->type == VT_HOUSE1 ? 1 : t->type == VT_HOUSE2 ? 3
                           : t->type == VT_HALL && i == 0 ? 4 : -1;
                mob_villager(vx, Y, vz, prof);
            }
            break;
        }
        case X_CLEAR:
        case X_DOWN: {
            vs_xyz(p, &x1, &y1, &z1), p += 2;
            int b = a == X_DOWN ? vs_block(t, vs_pal[*p++], desert) : B_AIR;
            for (int cz = z; cz <= z1; cz++)
                for (int cx = x; cx <= x1; cx++) {
                    int CX = tp_x(t, cx, cz), CZ = tp_z(t, cx, cz);
                    if (!pb_in(CX, Y, CZ)) continue;
                    if (a == X_CLEAR) {
                        /* clearCurrentPositionBlocksUpwards: what is not air from there up, cleared from the top */
                        int top = Y;
                        while (top < 255 && vget(CX, top, CZ) != B_AIR) top++;
                        while (top > Y) put(CX, --top, CZ, B_AIR, R_ALWAYS);
                    } else {
                        /* replaceAirAndLiquidDownwards */
                        for (int cy = Y; cy > 1; cy--) {
                            int u = vget(CX, cy, CZ);
                            if (u != B_AIR && !is_liq(u)) break;
                            put9(CX, cy, CZ, b);
                        }
                    }
                }
            break;
        }
        }
    }
}

/* Path.addComponentParts: gravel on the top solid block of each column, cobblestone under it (sandstone in a
 * desert) */
static void vs_road(const TP *t, int desert) {
    for (int x = t->x0; x <= t->x1; x++)
        for (int z = t->z0; z <= t->z1; z++) {
            if (!pb_in(x, 64, z)) continue;
            int y = top_solid(x, z) - 1;
            put(x, y, z, desert ? B_SANDSTONE : B_GRAVEL, R_ALWAYS);
            put(x, y - 1, z, desert ? B_SANDSTONE : B_COBBLESTONE, R_ALWAYS);
        }
}

/* MapGenStructure.generateStructure for population P (villages, then temples, as ChunkProviderGenerate
 * calls them): 1 if a village's box meets P's (no lakes then) */
static int structs_populate(void) {
    int flag = 0, a[4] = {PK + 8, PL + 8, PK + 23, PL + 23};
    for (int s = 0; s < nts; s++)
        if (ts[s].kind == 1 && box2_meet(ts[s].bb, a)) flag = 1;
    for (int pass = 1; pass <= 2; pass++)
        for (int i = 0; i < ntp; i++) {
            const TP *t = &tp[i];
            int16_t b[4] = {t->x0, t->z0, t->x1, t->z1};
            if (ts[t->s].kind != pass || !box2_meet(b, a)) continue;
            if (t->type == VT_PATH) vs_road(t, ts[t->s].desert);
            else {
                int prog = vt_prog[t->type];
                if (t->type == VT_TORCH && (t->face == F_S || t->face == F_W)) prog = VSP_TORCH1;
                vs_run(t, vs_prog + prog, ts[t->s].desert);
            }
        }
    return flag;
}

/* ======================================================================== */
/* for the game and the tests                                               */
/* ======================================================================== */
/* the chest there, if a village's blacksmith or a desert temple made it: 1, 2 (0: none) */
int gen_struct_chest(int x, int y, int z) {
    if (g_flat || gd_dim) return 0;
    uint32_t smem[STRUCT_MEM / 4];
    structs_slab(x >> 4, z >> 4, smem);
    int found = 0;
    for (int i = 0; i < ntp && !found; i++) {
        const TP *t = &tp[i];
        if (t->type == VT_HOUSE2) found = tp_x(t, 5, 5) == x && t->y0 + 1 == y && tp_z(t, 5, 5) == z;
        else if (t->type == VT_DESERT)
            for (int k = 0; k < 4 && !found; k++) {
                static const int8_t d[4][2] = {{0, 2}, {-2, 0}, {0, -2}, {2, 0}};   /* (south, west, north, east) */
                found = tp_x(t, 10 + d[k][0], 10 + d[k][1]) == x && t->y0 - 11 == y && tp_z(t, 10 + d[k][0], 10 + d[k][1]) == z;
            }
        if (found) found = t->type == VT_HOUSE2 ? 1 : 2;
    }
    tp = 0, ts = 0, ntp = nts = 0;
    return found;
}

/* (tests) the village of region (rx, rz): its start chunk, whether it is made, its box and pieces */
int gen_village_find(int rx, int rz, int *out) {
    int sx, sz;
    grid_start(rx, rz, 10387312, &sx, &sz);
    out[0] = sx, out[1] = sz, out[2] = 0;
    if (g_flat || gd_dim || !v_biomes(sx * 16 + 8, sz * 16 + 8, 0)) return 0;
    out[2] = v_layout(sx, sz);
    for (int k = 0; k < 6; k++) out[3 + k] = VL->bb[k];
    out[9] = VL->n;
    return 1;
}
/* (tests) piece i of the village gen_village_find just laid out: type, face, box (x0, y0, z0, x1, y1, z1) */
int gen_village_piece(int i, int *out) {
    if (i < 0 || i >= VL->n) return 0;
    const FPiece *p = &VL->p[i];
    out[0] = p->type, out[1] = p->face, out[2] = p->x0, out[3] = p->y0, out[4] = p->z0, out[5] = p->x1, out[6] = p->y1,
    out[7] = p->z1;
    return 1;
}
/* (tests) the temple grid's start in region (rx, rz) and whether a desert temple is there */
int gen_temple_find(int rx, int rz, int *out) {
    int sx, sz;
    grid_start(rx, rz, 14357617, &sx, &sz);
    out[0] = sx, out[1] = sz;
    int b = g_flat || gd_dim ? -1 : gen_biome(sx * 16 + 8, sz * 16 + 8);
    return b == BI_DESERT || b == BI_DESERT_HILLS;
}
