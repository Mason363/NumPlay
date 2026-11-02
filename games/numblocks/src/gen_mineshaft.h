/* NumBlocks.nwa only (NB_DIMS), included by gen_dims.h: abandoned mineshafts (MapGenMineshaft,
 * StructureMineshaftPieces). A chunk has one when MapGenBase's random for it says so (0.4 %, rarer near the
 * world's middle); its room and corridors are laid out from that random as 1.8.8 lays them out, then moved down
 * (markAvailableHeight), and built into each chunk they cross after its populations, by the population boxes
 * that hold them, as strongholds are (gen_stronghold.h): what 1.8.8 draws from the population's random as it
 * builds (the cobwebs, the torches, the rails, which supports have a whole beam) is drawn from the block's place,
 * so each block comes out the same whichever rows of its chunk are made. The liquid test that leaves a piece out
 * looks at the terrain as made (the sea). The chests' minecarts are not made (NumBlocks has no minecarts: their
 * rails are), and the cave spider spawners make spiders. */

enum { MS_ROOM, MS_CORRIDOR, MS_CROSS, MS_STAIRS };
/* a piece, packed: its corner, sizes - 1, type << 2 | facing, flag (a corridor: rails 1, spiders 2, sections
 * << 2; a crossing: two floors 1) */
typedef struct {
    int16_t x0, y0, z0;
    uint8_t sx, sy, sz, tf, flag, depth;
} MPiece;
#define MMAX 560
typedef struct {
    int n, nexit;
    int16_t bb[6];
    MPiece p[MMAX];
    int16_t exit[16][6];   /* the room's openings */
} Mine;
#define MN ((Mine *)(void *)&U)
_Static_assert(sizeof(Mine) <= sizeof U, "a mineshaft's layout fits the scratch union");

static FPiece mp_at(int i) {
    const MPiece *m = &MN->p[i];
    FPiece p;
    p.x0 = m->x0, p.y0 = m->y0, p.z0 = m->z0;
    p.x1 = (int16_t)(m->x0 + m->sx), p.y1 = (int16_t)(m->y0 + m->sy), p.z1 = (int16_t)(m->z0 + m->sz);
    p.type = (uint8_t)(m->tf >> 2), p.face = m->tf & 3, p.flag = m->flag, p.depth = m->depth, p.seed = 0;
    return p;
}
static int ms_meets(const int16_t *b) {   /* findIntersecting */
    for (int i = 0; i < MN->n; i++) {
        FPiece p = mp_at(i);
        if (boxes_meet(b, &p)) return i;
    }
    return -1;
}
static int ms_add(const int16_t *b, int type, int face, int flag, int depth) {
    Mine *m = MN;
    if (m->n >= MMAX) return -1;
    MPiece *p = &m->p[m->n];
    p->x0 = b[0], p->y0 = b[1], p->z0 = b[2];
    p->sx = (uint8_t)(b[3] - b[0]), p->sy = (uint8_t)(b[4] - b[1]), p->sz = (uint8_t)(b[5] - b[2]);
    p->tf = (uint8_t)(type << 2 | face), p->flag = (uint8_t)flag, p->depth = (uint8_t)depth;
    return m->n++;
}

static void ms_build(JRand *r, int pi);
/* func_175892_a: a crossing (one in five), stairs (one in ten) or a corridor there, if it fits; -1 */
static int ms_pick(JRand *r, int x, int y, int z, int face, int depth) {
    int i = jr_int(r, 100);
    int16_t b[6] = {(int16_t)x, (int16_t)y, (int16_t)z, (int16_t)x, (int16_t)(y + 2), (int16_t)z};
    if (i >= 80) {
        if (jr_int(r, 4) == 0) b[4] += 4;
        switch (face) {
        case F_N: b[0] = (int16_t)(x - 1), b[3] = (int16_t)(x + 3), b[2] = (int16_t)(z - 4); break;
        case F_S: b[0] = (int16_t)(x - 1), b[3] = (int16_t)(x + 3), b[5] = (int16_t)(z + 4); break;
        case F_W: b[0] = (int16_t)(x - 4), b[2] = (int16_t)(z - 1), b[5] = (int16_t)(z + 3); break;
        default: b[3] = (int16_t)(x + 4), b[2] = (int16_t)(z - 1), b[5] = (int16_t)(z + 3); break;
        }
        if (ms_meets(b) >= 0) return -1;
        return ms_add(b, MS_CROSS, face, b[4] - b[1] + 1 > 3, depth);
    }
    if (i >= 70) {
        b[1] = (int16_t)(y - 5);
        switch (face) {
        case F_N: b[3] = (int16_t)(x + 2), b[2] = (int16_t)(z - 8); break;
        case F_S: b[3] = (int16_t)(x + 2), b[5] = (int16_t)(z + 8); break;
        case F_W: b[0] = (int16_t)(x - 8), b[5] = (int16_t)(z + 2); break;
        default: b[3] = (int16_t)(x + 8), b[5] = (int16_t)(z + 2); break;
        }
        if (ms_meets(b) >= 0) return -1;
        return ms_add(b, MS_STAIRS, face, 0, depth);
    }
    int k = jr_int(r, 3) + 2;
    for (; k > 0; --k) {
        int j = k * 5;
        switch (face) {
        case F_N: b[3] = (int16_t)(x + 2), b[2] = (int16_t)(z - (j - 1)); break;
        case F_S: b[3] = (int16_t)(x + 2), b[5] = (int16_t)(z + (j - 1)); break;
        case F_W: b[0] = (int16_t)(x - (j - 1)), b[5] = (int16_t)(z + 2); break;
        default: b[3] = (int16_t)(x + (j - 1)), b[5] = (int16_t)(z + 2); break;
        }
        if (ms_meets(b) < 0) break;
    }
    if (k <= 0) return -1;
    /* Corridor: rails one in three, else cobwebs and a spawner one in 23; its sections of 5 */
    int rails = jr_int(r, 3) == 0, spiders = !rails && jr_int(r, 23) == 0;
    int len = face == F_N || face == F_S ? b[5] - b[2] + 1 : b[3] - b[0] + 1;
    return ms_add(b, MS_CORRIDOR, face, rails | spiders << 1 | (len / 5) << 2, depth);
}

/* func_175890_b: the next piece there (none past depth 8, or 80 from the room), built on at once */
static int ms_next(JRand *r, int x, int y, int z, int face, int depth) {
    const MPiece *room = &MN->p[0];
    if (depth > 8 || iabs(x - room->x0) > 80 || iabs(z - room->z0) > 80) return -1;
    int p = ms_pick(r, x, y, z, face, depth + 1);
    if (p >= 0) ms_build(r, p);
    return p;
}

/* buildComponent */
static void ms_build(JRand *r, int pi) {
    FPiece p = mp_at(pi);
    int d = p.depth;
    switch (p.type) {
    case MS_ROOM: {
        int j = p.y1 - p.y0 + 1 - 3 - 1, sx = p.x1 - p.x0 + 1, sz = p.z1 - p.z0 + 1;
        if (j <= 0) j = 1;
        for (int side = 0; side < 4; side++) {
            int size = side < 2 ? sx : sz;
            for (int k = 0; k < size; k += 4) {
                k += jr_int(r, size);
                if (k + 3 > size) break;
                int y = p.y0 + jr_int(r, j) + 1, c;
                switch (side) {
                case 0: c = ms_next(r, p.x0 + k, y, p.z0 - 1, F_N, d); break;
                case 1: c = ms_next(r, p.x0 + k, y, p.z1 + 1, F_S, d); break;
                case 2: c = ms_next(r, p.x0 - 1, y, p.z0 + k, F_W, d); break;
                default: c = ms_next(r, p.x1 + 1, y, p.z0 + k, F_E, d); break;
                }
                if (c < 0 || MN->nexit >= 16) continue;
                FPiece q = mp_at(c);
                int16_t *e = MN->exit[MN->nexit++];
                e[0] = q.x0, e[1] = q.y0, e[2] = q.z0, e[3] = q.x1, e[4] = q.y1, e[5] = q.z1;
                if (side == 0) e[2] = p.z0, e[5] = (int16_t)(p.z0 + 1);
                else if (side == 1) e[2] = (int16_t)(p.z1 - 1), e[5] = p.z1;
                else if (side == 2) e[0] = p.x0, e[3] = (int16_t)(p.x0 + 1);
                else e[0] = (int16_t)(p.x1 - 1), e[3] = p.x1;
            }
        }
        break;
    }
    case MS_CORRIDOR: {
        int j = jr_int(r, 4);
        switch (p.face) {
        case F_N:
            if (j <= 1) ms_next(r, p.x0, p.y0 - 1 + jr_int(r, 3), p.z0 - 1, F_N, d);
            else if (j == 2) ms_next(r, p.x0 - 1, p.y0 - 1 + jr_int(r, 3), p.z0, F_W, d);
            else ms_next(r, p.x1 + 1, p.y0 - 1 + jr_int(r, 3), p.z0, F_E, d);
            break;
        case F_S:
            if (j <= 1) ms_next(r, p.x0, p.y0 - 1 + jr_int(r, 3), p.z1 + 1, F_S, d);
            else if (j == 2) ms_next(r, p.x0 - 1, p.y0 - 1 + jr_int(r, 3), p.z1 - 3, F_W, d);
            else ms_next(r, p.x1 + 1, p.y0 - 1 + jr_int(r, 3), p.z1 - 3, F_E, d);
            break;
        case F_W:
            if (j <= 1) ms_next(r, p.x0 - 1, p.y0 - 1 + jr_int(r, 3), p.z0, F_W, d);
            else if (j == 2) ms_next(r, p.x0, p.y0 - 1 + jr_int(r, 3), p.z0 - 1, F_N, d);
            else ms_next(r, p.x0, p.y0 - 1 + jr_int(r, 3), p.z1 + 1, F_S, d);
            break;
        default:
            if (j <= 1) ms_next(r, p.x1 + 1, p.y0 - 1 + jr_int(r, 3), p.z0, F_E, d);
            else if (j == 2) ms_next(r, p.x1 - 3, p.y0 - 1 + jr_int(r, 3), p.z0 - 1, F_N, d);
            else ms_next(r, p.x1 - 3, p.y0 - 1 + jr_int(r, 3), p.z1 + 1, F_S, d);
            break;
        }
        if (d < 8) {
            if (p.face != F_N && p.face != F_S) {
                for (int x = p.x0 + 3; x + 3 <= p.x1; x += 5) {
                    int k = jr_int(r, 5);
                    if (k == 0) ms_next(r, x, p.y0, p.z0 - 1, F_N, d + 1);
                    else if (k == 1) ms_next(r, x, p.y0, p.z1 + 1, F_S, d + 1);
                }
            } else {
                for (int z = p.z0 + 3; z + 3 <= p.z1; z += 5) {
                    int k = jr_int(r, 5);
                    if (k == 0) ms_next(r, p.x0 - 1, p.y0, z, F_W, d + 1);
                    else if (k == 1) ms_next(r, p.x1 + 1, p.y0, z, F_E, d + 1);
                }
            }
        }
        break;
    }
    case MS_CROSS: {
        static const int8_t ways[4][3] = {{F_N, F_W, F_E}, {F_N, F_S, F_E}, {F_S, F_W, F_E}, {F_N, F_S, F_W}};
        for (int k = 0; k < 3; k++) {   /* (the three ways on, the way it was entered from left out) */
            int w = ways[p.face][k];
            if (w == F_N) ms_next(r, p.x0 + 1, p.y0, p.z0 - 1, F_N, d);
            else if (w == F_S) ms_next(r, p.x0 + 1, p.y0, p.z1 + 1, F_S, d);
            else if (w == F_W) ms_next(r, p.x0 - 1, p.y0, p.z0 + 1, F_W, d);
            else ms_next(r, p.x1 + 1, p.y0, p.z0 + 1, F_E, d);
        }
        if (p.flag & 1) {
            if (jr_bool(r)) ms_next(r, p.x0 + 1, p.y0 + 3 + 1, p.z0 - 1, F_N, d);
            if (jr_bool(r)) ms_next(r, p.x0 - 1, p.y0 + 3 + 1, p.z0 + 1, F_W, d);
            if (jr_bool(r)) ms_next(r, p.x1 + 1, p.y0 + 3 + 1, p.z0 + 1, F_E, d);
            if (jr_bool(r)) ms_next(r, p.x0 + 1, p.y0 + 3 + 1, p.z1 + 1, F_S, d);
        }
        break;
    }
    case MS_STAIRS:
        switch (p.face) {
        case F_N: ms_next(r, p.x0, p.y0, p.z0 - 1, F_N, d); break;
        case F_S: ms_next(r, p.x0, p.y0, p.z1 + 1, F_S, d); break;
        case F_W: ms_next(r, p.x0 - 1, p.y0, p.z0, F_W, d); break;
        default: ms_next(r, p.x1 + 1, p.y0, p.z0, F_E, d); break;
        }
        break;
    }
}

/* MapGenMineshaft.canSpawnStructureAtCoords for chunk (x, z) (its random as getStructureStart gets it) */
static int ms_start(int x, int z, JRand *r) {
    struct_rand(r, x, z);
    return jr_double(r) < 0.004 && jr_int(r, 80) < (iabs(x) > iabs(z) ? iabs(x) : iabs(z));
}

/* StructureMineshaftStart: the room, all on from it, then markAvailableHeight(10) */
static void ms_layout(int cx, int cz) {
    JRand r;
    ms_start(cx, cz, &r);
    Mine *m = MN;
    m->n = m->nexit = 0;
    int x = (cx << 4) + 2, z = (cz << 4) + 2;
    int x1 = x + 7 + jr_int(&r, 6), y1 = 54 + jr_int(&r, 6), z1 = z + 7 + jr_int(&r, 6);
    int16_t b[6] = {(int16_t)x, 50, (int16_t)z, (int16_t)x1, (int16_t)y1, (int16_t)z1};
    ms_add(b, MS_ROOM, F_S, 0, 0);
    ms_build(&r, 0);
    int16_t *bb = m->bb;
    bb[0] = bb[1] = bb[2] = 32767, bb[3] = bb[4] = bb[5] = -32768;
    for (int i = 0; i < m->n; i++) {
        FPiece p = mp_at(i);
        if (p.x0 < bb[0]) bb[0] = p.x0;
        if (p.y0 < bb[1]) bb[1] = p.y0;
        if (p.z0 < bb[2]) bb[2] = p.z0;
        if (p.x1 > bb[3]) bb[3] = p.x1;
        if (p.y1 > bb[4]) bb[4] = p.y1;
        if (p.z1 > bb[5]) bb[5] = p.z1;
    }
    int lo = 63 - 10, j = bb[4] - bb[1] + 1 + 1;
    if (j < lo) j += jr_int(&r, lo - j);
    int k = j - bb[4];
    bb[1] = (int16_t)(bb[1] + k), bb[4] = (int16_t)(bb[4] + k);
    for (int i = 0; i < m->n; i++) m->p[i].y0 = (int16_t)(m->p[i].y0 + k);
    for (int i = 0; i < m->nexit; i++) m->exit[i][1] = (int16_t)(m->exit[i][1] + k), m->exit[i][4] = (int16_t)(m->exit[i][4] + k);
}

/* ---- building (gen_stronghold.h's SB: the part of a population's box in C, positional draws) */
static int ms_rails(int face) { return face == F_N || face == F_S ? B_RAIL : B_RAIL_X; }   /* (getMetadataWithOffset) */
/* a cell of chunk C's slab that is air (as made so far) */
static int sb_air(const SB *b, int x, int y, int z) {
    int c = sb_cell(b, x, y, z);
    return c >= 0 && sb_get(b, c) == B_AIR;
}
static void ms_parts(const SB *b, int idx) {   /* (idx: the piece's index) */
    const FPiece *p = b->p;
    switch (p->type) {
    case MS_ROOM: {
        /* (no facing: the room's coordinates are the world's; here p is at 0, 0, 0) */
        const MPiece *m = &MN->p[0];
        int x0 = m->x0, y0 = m->y0, z0 = m->z0, x1 = x0 + m->sx, y1 = y0 + m->sy, z1 = z0 + m->sz;
        for (int x = x0; x <= x1; x++)   /* dirt floor, over what is there */
            for (int z = z0; z <= z1; z++) {
                int c = sb_cell(b, x, y0, z);
                if (c >= 0 && sb_get(b, c) != B_AIR) sb_put(b, c, B_DIRT);
            }
        sb_fill(b, x0, y0 + 1, z0, x1, y1 < y0 + 3 ? y1 : y0 + 3, z1, B_AIR);
        for (int i = 0; i < MN->nexit; i++) {
            const int16_t *e = MN->exit[i];
            sb_fill(b, e[0], e[4] - 2, e[2], e[3], e[4], e[5], B_AIR);
        }
        /* randomlyRareFillWithBlocks: the dome */
        float f = (float)(x1 - x0 + 1), f1 = (float)(y1 - (y0 + 4) + 1), f2 = (float)(z1 - z0 + 1);
        float f3 = (float)x0 + f / 2.0f, f4 = (float)z0 + f2 / 2.0f;
        for (int y = y0 + 4; y <= y1; y++) {
            float f5 = (float)(y - (y0 + 4)) / f1;
            for (int x = x0; x <= x1; x++) {
                float f6 = ((float)x - f3) / (f * 0.5f);
                for (int z = z0; z <= z1; z++) {
                    float f7 = ((float)z - f4) / (f2 * 0.5f);
                    if (f6 * f6 + f5 * f5 + f7 * f7 <= 1.05f) sb_set(b, x, y, z, B_AIR);
                }
            }
        }
        break;
    }
    case MS_CORRIDOR: {
        int n = p->flag >> 2, i1 = n * 5 - 1;
        sb_fill(b, 0, 0, 0, 2, 1, i1, B_AIR);
        for (int x = 0; x <= 2; x++)
            for (int z = 0; z <= i1; z++)
                if (sb_rnd(b, x, 2, z, 11) <= 0.8f) sb_set(b, x, 2, z, B_AIR);
        if (p->flag & 2)
            for (int y = 0; y <= 1; y++)
                for (int x = 0; x <= 2; x++)
                    for (int z = 0; z <= i1; z++)
                        if (sb_rnd(b, x, y, z, 12) <= 0.6f) sb_set(b, x, y, z, B_COBWEB);
        for (int j = 0; j < n; j++) {
            int k1 = 2 + j * 5;
            sb_fill(b, 0, 0, k1, 0, 1, k1, B_FENCE_OAK);
            sb_fill(b, 2, 0, k1, 2, 1, k1, B_FENCE_OAK);
            if (sb_rnd(b, 1, 2, k1, 13) < 0.25f) sb_set(b, 0, 2, k1, B_PLANKS_OAK), sb_set(b, 2, 2, k1, B_PLANKS_OAK);
            else sb_fill(b, 0, 2, k1, 2, 2, k1, B_PLANKS_OAK);
            static const int8_t web[8][3] = {{0, -1, 10}, {2, -1, 10}, {0, 1, 10}, {2, 1, 10},
                                             {0, -2, 5}, {2, -2, 5}, {0, 2, 5}, {2, 2, 5}};
            for (int w = 0; w < 8; w++)
                if (sb_rnd(b, web[w][0], 2, k1 + web[w][1], 14) < web[w][2] / 100.0f) sb_set(b, web[w][0], 2, k1 + web[w][1], B_COBWEB);
            if (sb_rnd(b, 1, 2, k1 - 1, 15) < 0.05f) sb_set(b, 1, 2, k1 - 1, B_TORCH);
            if (sb_rnd(b, 1, 2, k1 + 1, 15) < 0.05f) sb_set(b, 1, 2, k1 + 1, B_TORCH);
            /* a chest's minecart one in a hundred each side: its rail (no minecarts in NumBlocks) */
            if (sb_rnd(b, 2, 0, k1 - 1, 16) < 0.01f && sb_air(b, 2, 0, k1 - 1)) sb_set(b, 2, 0, k1 - 1, ms_rails(p->face));
            if (sb_rnd(b, 0, 0, k1 + 1, 16) < 0.01f && sb_air(b, 0, 0, k1 + 1)) sb_set(b, 0, 0, k1 + 1, ms_rails(p->face));
            if ((p->flag & 2) && j == 0) {
                int z = k1 - 1 + (int)(sb_rnd(b, 1, 0, k1, 17) * 3), c = sb_cell(b, 1, 0, z);
                if (c >= 0) {
                    sb_put(b, c, B_MOB_SPAWNER);
                    gen_spawner_made(fb_x(p, 1, z), p->y0, fb_z(p, 1, z), 2);   /* (a cave spider's: spiders) */
                }
            }
        }
        for (int x = 0; x <= 2; x++)
            for (int z = 0; z <= i1; z++)
                if (sb_air(b, x, -1, z)) sb_set(b, x, -1, z, B_PLANKS_OAK);
        if (p->flag & 1)
            for (int z = 0; z <= i1; z++)
                if (sb_rnd(b, 1, 0, z, 18) < 0.7f) sb_set(b, 1, 0, z, ms_rails(p->face));
        break;
    }
    case MS_CROSS: {
        const MPiece *m = &MN->p[idx];   /* (no facing: the world's coordinates) */
        int x0 = m->x0, y0 = m->y0, z0 = m->z0, x1 = x0 + m->sx, y1 = y0 + m->sy, z1 = z0 + m->sz;
        if (m->flag & 1) {
            sb_fill(b, x0 + 1, y0, z0, x1 - 1, y0 + 2, z1, B_AIR);
            sb_fill(b, x0, y0, z0 + 1, x1, y0 + 2, z1 - 1, B_AIR);
            sb_fill(b, x0 + 1, y1 - 2, z0, x1 - 1, y1, z1, B_AIR);
            sb_fill(b, x0, y1 - 2, z0 + 1, x1, y1, z1 - 1, B_AIR);
            sb_fill(b, x0 + 1, y0 + 3, z0 + 1, x1 - 1, y0 + 3, z1 - 1, B_AIR);
        } else {
            sb_fill(b, x0 + 1, y0, z0, x1 - 1, y1, z1, B_AIR);
            sb_fill(b, x0, y0, z0 + 1, x1, y1, z1 - 1, B_AIR);
        }
        sb_fill(b, x0 + 1, y0, z0 + 1, x0 + 1, y1, z0 + 1, B_PLANKS_OAK);
        sb_fill(b, x0 + 1, y0, z1 - 1, x0 + 1, y1, z1 - 1, B_PLANKS_OAK);
        sb_fill(b, x1 - 1, y0, z0 + 1, x1 - 1, y1, z0 + 1, B_PLANKS_OAK);
        sb_fill(b, x1 - 1, y0, z1 - 1, x1 - 1, y1, z1 - 1, B_PLANKS_OAK);
        for (int x = x0; x <= x1; x++)
            for (int z = z0; z <= z1; z++)
                if (sb_air(b, x, y0 - 1, z)) sb_set(b, x, y0 - 1, z, B_PLANKS_OAK);
        break;
    }
    case MS_STAIRS:
        sb_fill(b, 0, 5, 0, 2, 7, 1, B_AIR);
        sb_fill(b, 0, 0, 7, 2, 2, 8, B_AIR);
        for (int i = 0; i < 5; i++) sb_fill(b, 0, 5 - i - (i < 4 ? 1 : 0), 2 + i, 2, 7 - i, 2 + i, B_AIR);
        break;
    }
}

/* the mineshafts that may reach chunk (cx, cz) (starts within 8 chunks), their pieces built into its rows
 * [y0, y0 + h) by the four populations writing into it, each in its box, in the order laid out */
static void mineshaft_slab(int cx, int cz, int y0, int h, uint8_t *out) {
    if (gen_old_chunk(cx, cz)) return;
    for (int sz = cz - 8; sz <= cz + 8; sz++)
        for (int sx = cx - 8; sx <= cx + 8; sx++) {
            JRand r;
            if (!ms_start(sx, sz, &r)) continue;
            SCache *c = sc_find(sx, sz, 3);
            int16_t area[4] = {(int16_t)(cx * 16 - 1), (int16_t)(cz * 16 - 1), (int16_t)(cx * 16 + 16), (int16_t)(cz * 16 + 16)};
            int a[4] = {area[0], area[1], area[2], area[3]};
            if (c && !box2_meet(c->bb, a)) continue;
            ms_layout(sx, sz);
            const int16_t *bb = MN->bb;
            if (!c) {
                c = sc_new(sx, sz, 3);
                c->ok = 1, c->bb[0] = bb[0], c->bb[1] = bb[2], c->bb[2] = bb[3], c->bb[3] = bb[5];
            }
            if (!box2_meet(c->bb, a) || bb[4] < y0 || bb[1] >= y0 + h + 1) continue;
            for (int q = 0; q < 4; q++) {
                int px = cx - 1 + (q & 1), pz = cz - 1 + (q >> 1);
                int pb[4] = {px * 16 + 8, pz * 16 + 8, px * 16 + 23, pz * 16 + 23};
                SB b = {0, pb[0] > cx * 16 ? pb[0] : cx * 16, pb[1] > cz * 16 ? pb[1] : cz * 16,
                        pb[2] < cx * 16 + 15 ? pb[2] : cx * 16 + 15, pb[3] < cz * 16 + 15 ? pb[3] : cz * 16 + 15, y0, h, out};
                if (b.x0 > b.x1 || b.z0 > b.z1) continue;
                for (int i = 0; i < MN->n; i++) {
                    FPiece pc = mp_at(i);
                    if (pc.x1 < pb[0] || pc.x0 > pb[2] || pc.z1 < pb[1] || pc.z0 > pb[3]) continue;   /* not this population's */
                    if (pc.x1 + 1 < b.x0 || pc.x0 - 1 > b.x1 || pc.z1 + 1 < b.z0 || pc.z0 - 1 > b.z1 || pc.y1 + 1 < y0 ||
                        pc.y0 - 1 >= y0 + h)
                        continue;   /* (nothing of it here) */
                    if (sb_liquid(&pc, pb)) continue;
                    if (pc.type == MS_ROOM || pc.type == MS_CROSS) {
                        /* (no facing: world coordinates) */
                        FPiece w;
                        memset(&w, 0, sizeof w);
                        w.face = F_S, w.type = pc.type;
                        b.p = &w;
                        ms_parts(&b, i);
                    } else {
                        b.p = &pc;
                        ms_parts(&b, i);
                    }
                }
            }
        }
}

/* (tests) the mineshaft starting in chunk (cx, cz), if one does: its box and pieces */
int gen_mineshaft_find(int cx, int cz, int *out) {
    JRand r;
    if (g_flat || gd_dim || !ms_start(cx, cz, &r)) return 0;
    ms_layout(cx, cz);
    for (int k = 0; k < 6; k++) out[k] = MN->bb[k];
    out[6] = MN->n;
    return 1;
}
