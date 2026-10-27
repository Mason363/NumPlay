/* NumBlocks.nwa only (NB_DIMS), included at the end of mob.c: the Nether's and the End's creatures as
 * Minecraft 1.8.8 has them: zombie pigmen (EntityPigZombie), ghasts and their fireballs (EntityGhast,
 * EntityLargeFireball), blazes (EntityBlaze, EntitySmallFireball), magma cubes (EntityMagmaCube), wither
 * skeletons (EntitySkeleton of type 1), endermen (EntityEnderman) and ender crystals; their models, how
 * they move and fight, where they spawn (ChunkProviderHell.getPossibleCreatures, the End's endermen, the
 * Overworld's), what they drop. The Ender Dragon is end.c's. */

/* java.util.Random.nextGaussian, near enough */
static float rnd_gauss(void) {
    float u = rndf() + 1e-6f, v = rndf();
    return sqrtf(-2 * logf(u)) * cosf(6.2831853f * v);
}

/* isImmuneToFire */
static bool fire_immune(int t) { return t == E_PIGMAN || t == E_GHAST || t == E_BLAZE || t == E_MAGMA || t == E_WSKELETON || t == E_DRAGON || t == E_CRYSTAL; }
/* angry only when crossed (a pigman hit, an enderman looked at or hit): e->panic counts the anger down */
static bool neutral(int t) { return t == E_PIGMAN || t == E_ENDERMAN; }


/* ---------------------------------------------------------------- models */
/* a rotation (world axes, as Minecraft's GL calls turn) as the part turns (Z, then Y, then X, in the
 * model's frame) */
static void part_turn(Part *p, const float *R) {
    float m[9];
    for (int i = 0; i < 9; i++) m[i] = R[i];
    /* F R F: the model's frame is the world's turned about x (F = diag(1, -1, -1), the entity facing 0) */
    m[1] = -m[1], m[2] = -m[2], m[3] = -m[3], m[6] = -m[6];
    float sb = -m[6];
    sb = sb > 1 ? 1 : sb < -1 ? -1 : sb;
    p->ry = asinf(sb);
    p->rx = atan2f(m[7], m[8]);
    p->rz = atan2f(m[3], m[0]);
}
static void mat_axis(float *m, float a, float x, float y, float z) {   /* glRotate: a radians about (x, y, z) */
    float l = sqrtf(x * x + y * y + z * z);
    x /= l, y /= l, z /= l;
    float c = cosf(a), s = sinf(a), t = 1 - c;
    m[0] = t * x * x + c, m[1] = t * x * y - s * z, m[2] = t * x * z + s * y;
    m[3] = t * x * y + s * z, m[4] = t * y * y + c, m[5] = t * y * z - s * x;
    m[6] = t * x * z - s * y, m[7] = t * y * z + s * x, m[8] = t * z * z + c;
}
static void mat_mul(const float *a, const float *b, float *o) {
    float t[9];
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) t[r * 3 + c] = a[r * 3] * b[c] + a[r * 3 + 1] * b[3 + c] + a[r * 3 + 2] * b[6 + c];
    for (int i = 0; i < 9; i++) o[i] = t[i];
}

/* ModelDragon: its parts, each turned in the frame of the one it hangs from (a frame: the pivot and the turn,
 * in the model's pixels) */
typedef struct {
    float m[9], p[3];
} Xf;
static void rzyx(float rx, float ry, float rz, float *m) {
    float cx = cosf(rx), sx = sinf(rx), cy = cosf(ry), sy = sinf(ry), cz = cosf(rz), sz = sinf(rz);
    m[0] = cz * cy, m[1] = cz * sy * sx - sz * cx, m[2] = cz * sy * cx + sz * sx;
    m[3] = sz * cy, m[4] = sz * sy * sx + cz * cx, m[5] = sz * sy * cx - cz * sx;
    m[6] = -sy, m[7] = cy * sx, m[8] = cy * cx;
}
/* a ModelRenderer at (px, py, pz) in a, turned rz, ry, rx */
static void xf_child(const Xf *a, float px, float py, float pz, float rx, float ry, float rz, Xf *o) {
    float r[9], q[3] = {px, py, pz};
    rzyx(rx, ry, rz, r);
    float p[3];
    for (int i = 0; i < 3; i++) p[i] = a->p[i] + a->m[i * 3] * q[0] + a->m[i * 3 + 1] * q[1] + a->m[i * 3 + 2] * q[2];
    mat_mul(a->m, r, o->m);
    o->p[0] = p[0], o->p[1] = p[1], o->p[2] = p[2];
}
/* a box of the dragon's skin in frame x (mirrored: the left side, GL's scale(-1, 1, 1)) */
static void xbox(const Xf *x, int mir, float bx, float by, float bz, int w, int h, int d, int u, int v) {
    float m[9];
    for (int i = 0; i < 9; i++) m[i] = x->m[i];
    float px = x->p[0];
    if (mir) m[1] = -m[1], m[2] = -m[2], m[3] = -m[3], m[6] = -m[6], px = -px, bx = -bx - w;
    if (np >= MAX_PARTS_DRAGON) return;
    Part *p = box(bx, by, bz, w, h, d, u & 255, v, px, x->p[1], x->p[2], SKIN_DRAGON);
    p->mirror = (uint8_t)mir;
    float sb = -m[6];
    p->ry = asinf(sb > 1 ? 1 : sb < -1 ? -1 : sb);
    p->rx = atan2f(m[7], m[8]);
    p->rz = atan2f(m[3], m[0]);
}
static float dwrap(float a) {
    while (a >= 180) a -= 360;
    while (a < -180) a += 360;
    return a;
}
static void dragon_parts(const Entity *e, float pt) {
    float f = dragon_anim(pt), f8 = f * PI * 2;
    float jaw = (sinf(f8) + 1) * 0.2f, f1 = sinf(f8 - 1) + 1;
    f1 = (f1 * f1 + f1 * 2) * 0.05f;
    /* the whole: 2 blocks up, 3 ahead, nodding with the wings */
    Xf G, B, X, Y, Z;
    rzyx(f1 * 2 * PI / 180, 0, 0, G.m);
    G.p[0] = 0, G.p[1] = (f1 - 2) * 16, G.p[2] = -48;
    float y6, h6, y5, h5, y10, h10, yy, hh;
    dragon_offsets(6, pt, &y6, &h6);
    dragon_offsets(5, pt, &y5, &h5);
    dragon_offsets(10, pt, &y10, &h10);
    float f5 = 1.5f, f6 = dwrap(y5 - y10), f7 = dwrap(y5 + f6 / 2), D = PI / 180;
    /* the neck: five vertebrae, following its last turns */
    float f2 = 20, f3 = -12, f4 = 0;
    for (int i = 0; i < 5; i++) {
        dragon_offsets(5 - i, pt, &yy, &hh);
        float f9 = cosf((float)i * 0.45f + f8) * 0.15f;
        float ry = dwrap(yy - y6) * D * f5, rx = f9 + (hh - h6) * D * f5 * 5, rz = -dwrap(yy - f7) * D * f5;
        xf_child(&G, f4, f2, f3, rx, ry, rz, &X);
        xbox(&X, 0, -5, -5, -5, 10, 10, 10, 192, 104);
        f2 += sinf(rx) * 10, f3 -= cosf(ry) * cosf(rx) * 10, f4 -= sinf(ry) * cosf(rx) * 10;
    }
    /* the head and its jaw */
    dragon_offsets(0, pt, &yy, &hh);
    xf_child(&G, f4, f2, f3, 0, dwrap(yy - y6) * D, -dwrap(yy - f7) * D, &X);
    xbox(&X, 0, -6, -1, -24, 12, 5, 16, 176, 44);
    xbox(&X, 0, -8, -8, -10, 16, 16, 16, 112, 30);
    xf_child(&X, 0, 4, -8, jaw, 0, 0, &Y);
    xbox(&Y, 0, -6, 0, -16, 12, 4, 16, 176, 65);
    /* the body, rolled as it turns; its wings and legs on both sides */
    Xf T;
    xf_child(&G, 0, 16, 0, 0, 0, -f6 * f5 * D, &T);
    xf_child(&T, 0, -16, 0, 0, 0, 0, &B);
    xf_child(&B, 0, 4, 8, 0, 0, 0, &X);
    xbox(&X, 0, -12, 0, -16, 24, 24, 64, 0, 0);
    float f11 = f8;
    for (int side = 0; side < 2; side++) {
        xf_child(&B, -12, 5, 2, 0.125f - cosf(f11) * 0.2f, 0.25f, (sinf(f11) + 0.125f) * 0.8f, &X);
        xbox(&X, side, -56, -4, -4, 56, 8, 8, 112, 88);
        xbox(&X, side, -56, 0, 2, 56, 0, 56, -56, 88);
        xf_child(&X, -56, 0, 0, 0, 0, -(sinf(f11 + 2) + 0.5f) * 0.75f, &Y);
        xbox(&Y, side, -56, -2, -2, 56, 4, 4, 112, 136);
        xbox(&Y, side, -56, 0, 2, 56, 0, 56, -56, 144);
        xf_child(&B, -12, 20, 2, 1.3f + f1 * 0.1f, 0, 0, &X);
        xbox(&X, side, -4, -4, -4, 8, 24, 8, 112, 104);
        xf_child(&X, 0, 20, -1, -0.5f - f1 * 0.1f, 0, 0, &Y);
        xbox(&Y, side, -3, -1, -3, 6, 24, 6, 226, 138);
        xf_child(&Y, 0, 23, 0, 0.75f + f1 * 0.1f, 0, 0, &Z);
        xbox(&Z, side, -4, 0, -12, 8, 4, 16, 144, 104);
        xf_child(&B, -16, 16, 42, 1.0f + f1 * 0.1f, 0, 0, &X);
        xbox(&X, side, -8, -4, -8, 16, 32, 16, 0, 0);
        xf_child(&X, 0, 32, -4, 0.5f + f1 * 0.1f, 0, 0, &Y);
        xbox(&Y, side, -6, -2, 0, 12, 32, 12, 196, 0);
        xf_child(&Y, 0, 31, 4, 0.75f + f1 * 0.1f, 0, 0, &Z);
        xbox(&Z, side, -9, 0, -20, 18, 6, 24, 112, 0);
    }
    /* the tail: twelve, swaying */
    float y11, h11, f10 = 0;
    dragon_offsets(11, pt, &y11, &h11);
    f2 = 10, f3 = 60, f4 = 0;
    for (int k = 0; k < 12; k++) {
        dragon_offsets(12 + k, pt, &yy, &hh);
        f10 += sinf((float)k * 0.45f + f8) * 0.05f;
        float ry = (dwrap(yy - y11) * f5 + 180) * D, rx = f10 + (hh - h11) * D * f5 * 5, rz = dwrap(yy - f7) * D * f5;
        xf_child(&G, f4, f2, f3, rx, ry, rz, &X);
        xbox(&X, 0, -5, -5, -5, 10, 10, 10, 192, 104);
        f2 += sinf(rx) * 10, f3 -= cosf(ry) * cosf(rx) * 10, f4 -= sinf(ry) * cosf(rx) * 10;
    }
    (void)e;
}

static void dims_parts(const Entity *e, float pt, float f, float a, float pitch, float age) {
    float sw = cosf(f * 0.6662f), swo = cosf(f * 0.6662f + PI);
    switch (e->type) {
    case E_PIGMAN:
    case E_WSKELETON: {
        /* ModelZombie (the pigman), ModelSkeleton (its arms held out as a zombie's: no bow) */
        bool pig = e->type == E_PIGMAN;
        int sk = pig ? SKIN_ZOMBIE_PIGMAN : SKIN_WITHER_SKELETON, aw = pig ? 4 : 2, o1 = pig ? 2 : 1;
        box(-4, -8, -4, 8, 8, 8, 0, 0, 0, 0, 0, sk)->rx = pitch;
        box(-4, 0, -2, 8, 12, 4, 16, 16, 0, 0, 0, sk);
        Part *ra = box(pig ? -3 : -1, -2, -o1, aw, 12, aw, 40, 16, -5, 2, 0, sk);
        Part *la = box(-1, -2, -o1, aw, 12, aw, 40, 16, 5, 2, 0, sk);
        Part *rl = box(-o1, 0, -o1, aw, 12, aw, 0, 16, pig ? -1.9f : -2, 12, 0, sk);
        Part *ll = box(-o1, 0, -o1, aw, 12, aw, 0, 16, pig ? 1.9f : 2, 12, 0, sk);
        ra->rx = la->rx = -PI / 2 + sinf(age * 0.067f) * 0.05f;
        ra->rz = cosf(age * 0.09f) * 0.05f + 0.05f;
        la->rz = -ra->rz;
        la->mirror = ll->mirror = 1;
        rl->rx = sw * 1.4f * a;
        ll->rx = swo * 1.4f * a;
        break;
    }
    case E_ENDERMAN: {
        /* ModelEnderman: a biped 14 pixels taller, its limbs long and thin, swinging half as far; the jaw
         * drops (the head rises) when it screams; arms forward carrying a block */
        bool angry = e->panic > 0;
        float hy = -13 - (angry ? 5 : 0);
        box(-4, -8, -4, 8, 8, 8, 0, 0, 0, hy, 0, SKIN_ENDERMAN)->rx = pitch;
        Part *jaw = box(-4, -8, -4, 8, 8, 8, 0, 16, 0, -13, 0, SKIN_ENDERMAN);
        jaw->grow = -0.5f, jaw->rx = pitch;
        box(-4, 0, -2, 8, 12, 4, 32, 16, 0, -14, 0, SKIN_ENDERMAN);
        float ar = swo * a * 0.5f, al = sw * a * 0.5f, lr = sw * 1.4f * a * 0.5f, ll = swo * 1.4f * a * 0.5f;
#define CL(v) ((v) > 0.4f ? 0.4f : (v) < -0.4f ? -0.4f : (v))
        Part *rA = box(-1, -2, -1, 2, 30, 2, 56, 0, -3, -12, 0, SKIN_ENDERMAN);
        Part *lA = box(-1, -2, -1, 2, 30, 2, 56, 0, 5, -12, 0, SKIN_ENDERMAN);
        rA->rx = CL(ar), lA->rx = CL(al), lA->mirror = 1;
        if (e->love) rA->rx = lA->rx = -0.5f, rA->rz = 0.05f, lA->rz = -0.05f;   /* (carrying: love holds the block) */
        box(-1, 0, -1, 2, 30, 2, 56, 0, -2, -5, 0, SKIN_ENDERMAN)->rx = CL(lr);
        Part *lL = box(-1, 0, -1, 2, 30, 2, 56, 0, 2, -5, 0, SKIN_ENDERMAN);
        lL->rx = CL(ll), lL->mirror = 1;
#undef CL
        break;
    }
    case E_GHAST: {
        /* ModelGhast: a 16-pixel cube and nine tentacles (their lengths from Random(1660)), 0.6 lower;
         * shooting, its eyes and mouth open */
        static const uint8_t len[9] = {8, 13, 9, 11, 11, 10, 12, 9, 12};
        int sk = e->state == 1 ? SKIN_GHAST_SHOOTING : SKIN_GHAST;
        box(-8, -8, -8, 16, 16, 16, 0, 0, 0, 8 + 9.6f, 0, sk);
        for (int i = 0; i < 9; i++) {
            float fx = (((float)(i % 3) - (float)(i / 3 % 2) * 0.5f + 0.25f) / 2.0f * 2.0f - 1.0f) * 5.0f;
            float fz = ((float)(i / 3) / 2.0f * 2.0f - 1.0f) * 5.0f;
            box(-1, 0, -1, 2, len[i], 2, 0, 0, fx, 15 + 9.6f, fz, sk)->rx = 0.2f * sinf(age * 0.3f + (float)i) + 0.4f;
        }
        break;
    }
    case E_BLAZE: {
        /* ModelBlaze: the head and twelve rods going round in three rings */
        Part *h = box(-4, -4, -4, 8, 8, 8, 0, 0, 0, 0, 0, SKIN_BLAZE);
        h->rx = pitch;
        float g = age * PI * -0.1f;
        for (int i = 0; i < 4; i++, g++)
            box(0, 0, 0, 2, 8, 2, 0, 16, cosf(g) * 9, -2 + cosf(((float)(i * 2) + age) * 0.25f), sinf(g) * 9, SKIN_BLAZE);
        g = PI / 4 + age * PI * 0.03f;
        for (int j = 4; j < 8; j++, g++)
            box(0, 0, 0, 2, 8, 2, 0, 16, cosf(g) * 7, 2 + cosf(((float)(j * 2) + age) * 0.25f), sinf(g) * 7, SKIN_BLAZE);
        g = 0.47123894f + age * PI * -0.05f;
        for (int k = 8; k < 12; k++, g++)
            box(0, 0, 0, 2, 8, 2, 0, 16, cosf(g) * 5, 11 + cosf(((float)k * 1.5f + age) * 0.5f), sinf(g) * 5, SKIN_BLAZE);
        break;
    }
    case E_MAGMA: {
        /* ModelMagmaCube: eight slices of 8 x 1 x 8 pulled apart as it squishes, and its core */
        float sq = e->on_ground ? 0 : 0.6f;
        for (int i = 0; i < 8; i++) {
            int u = 0, v = i;
            if (i == 2) u = 24, v = 10;
            else if (i == 3) u = 24, v = 19;
            box(-4, (float)(16 + i), -4, 8, 1, 8, u, v, 0, (float)(-(4 - i)) * sq * 1.7f, 0, SKIN_MAGMACUBE);
        }
        box(-2, 18, -2, 4, 4, 4, 0, 16, 0, 0, 0, SKIN_MAGMACUBE);
        break;
    }
    case E_CRYSTAL: {
        /* ModelEnderCrystal (scaled 2: mob_scale): its base, then the glass turning on a slant inside
         * glass inside the cube, bobbing */
        float t = e->age + pt, bob = sinf(t * 0.2f) / 2 + 0.5f;
        bob = (bob * bob + bob) * 0.2f;
        float spin = t * 3 * PI / 180;
        box(-6, 28, -6, 12, 4, 12, 0, 16, 0, 0, 0, SKIN_CRYSTAL);
        float R[9], A[9], Y[9], S[9];
        mat_axis(Y, spin, 0, 1, 0);
        mat_axis(A, PI / 3, 0.7071f, 0, 0.7071f);
        mat_mul(Y, A, R);
        float cy = 24.125f - (0.8f + bob - 0.5f) * 16;   /* (2 x (-0.5 + 0.8 + bob) blocks up, in the model's pixels at scale 2) */
        static const float grow[3] = {0, -0.5f, -0.94f};
        for (int k = 0; k < 3; k++) {
            Part *p = box(-4, -4, -4, 8, 8, 8, k == 2 ? 0 : 32, 0, 0, cy, 0, SKIN_CRYSTAL);
            p->grow = grow[k];
            part_turn(p, R);
            mat_mul(R, A, S);
            mat_mul(S, Y, R);
        }
        break;
    }
    case E_DRAGON:
        dragon_parts(e, pt);
        break;
    }
}

/* ---------------------------------------------------------------- spawning */
/* ChunkProviderHell.getPossibleCreatures and the End's: a try every second, as for the Overworld; the
 * Overworld's endermen are one in 41 of its monsters (weight 10 of 410) */
static bool room_for(float x, float y, float z, float w, float h) {
    float b[6] = {x - w / 2, y, z - w / 2, x + w / 2, y + h, z + w / 2};
    return phys_free(b);
}
static Entity *dims_mob(int type, float x, float y, float z) {
    Entity *e = spawn(type, x, y, z);
    if (!e) return NULL;
    if (type == E_MAGMA) {
        /* EntitySlime.onInitialSpawn: size 1, 2 or 4; the health its square */
        e->sheared = (uint8_t)(1 << rnd(3));
        e->health = (int16_t)(e->sheared * e->sheared);
    }
    return e;
}
static bool dims_spawn(void) {
    if (dim == DIM_OVERWORLD) return false;   /* (mobs_spawn: its endermen) */
    if (pl.dead || opt.difficulty == 0) return true;
    for (int k = 0; k < 4; k++) {
        int x = ifl(pl.x) + rnd(41) - 20, z = ifl(pl.z) + rnd(41) - 20;
        float dx = x + 0.5f - pl.x, dz = z + 0.5f - pl.z;
        if (dx * dx + dz * dz < 14 * 14) continue;
        int y0 = ifl(pl.y) + 8;
        for (int y = y0; y > y0 - 20; y--) {
            if (!world_loaded(x, y + 1, z) || !world_loaded(x, y - 1, z) || !can_stand(x, y, z)) continue;
            if (count(true) >= 4) return true;
            int below = world_get(x, y - 1, z), t;
            if (dim == DIM_END) t = E_ENDERMAN;
            else {
                int fo = gen_fortress_at(x, y, z);
                if (fo == 2 || (fo == 1 && below == B_NETHER_BRICK)) {
                    /* the fortress's: blaze 10, pigman 5, magma cube 3, skeleton 2 (in the Nether, four in
                     * five of them wither skeletons) */
                    int r = rnd(20);
                    t = r < 10 ? E_BLAZE : r < 15 ? E_PIGMAN : r < 18 ? E_MAGMA : rnd(5) ? E_WSKELETON : E_SKELETON;
                } else {
                    /* BiomeGenHell: ghast 50, pigman 100, magma cube 1; a ghast one try in 20 */
                    int r = rnd(151);
                    t = r < 50 ? E_GHAST : r < 150 ? E_PIGMAN : E_MAGMA;
                    if (t == E_GHAST && rnd(20)) return true;
                }
            }
            /* monsters but these (EntityMob.isValidLightLevel) in the dark */
            if ((t == E_WSKELETON || t == E_SKELETON || t == E_ENDERMAN) && light_there(x, y, z) > rnd(8)) return true;
            const MobInfo *mi = &info[t];
            if (!room_for(x + 0.5f, (float)y, z + 0.5f, mi->w, mi->h)) return true;
            dims_mob(t, x + 0.5f, (float)y, z + 0.5f);
            return true;
        }
    }
    return true;
}

/* ---------------------------------------------------------------- drops */
static bool dims_loot(Entity *e) {
    switch (e->type) {
    case E_PIGMAN:
        drop(I_ROTTEN_FLESH, rnd(2), e);
        drop(I_GOLD_NUGGET, rnd(2), e);
        if (rnd(40) == 0) drop(I_GOLD_INGOT, 1, e);   /* (addRandomDrop, one in forty with the player's kill) */
        break;
    case E_GHAST:
        drop(I_GHAST_TEAR, rnd(2), e);
        drop(I_GUNPOWDER, rnd(3), e);
        break;
    case E_BLAZE: drop(I_BLAZE_ROD, rnd(2), e); break;
    case E_MAGMA: {
        int n = e->sheared > 1 ? rnd(4) - 2 : 0;
        drop(I_MAGMA_CREAM, n, e);
        /* EntitySlime.setDead: two to four of half its size */
        if (e->sheared > 1) {
            int k = 2 + rnd(3), s = e->sheared / 2;
            for (int i = 0; i < k; i++) {
                float fx = ((float)(i % 2) - 0.5f) * e->sheared / 4.0f, fz = ((float)(i / 2) - 0.5f) * e->sheared / 4.0f;
                Entity *c = spawn(E_MAGMA, e->x + fx, e->y + 0.5f, e->z + fz);
                if (c) c->sheared = (uint8_t)s, c->health = (int16_t)(s * s);
            }
        }
        player_add_xp(e->sheared);
        return true;
    }
    case E_WSKELETON:
        drop(I_COAL, rnd(3) - 1, e);
        drop(I_BONE, rnd(3), e);
        break;
    case E_ENDERMAN: drop(I_ENDER_PEARL, rnd(2), e); break;
    default: return true;
    }
    player_add_xp(e->type == E_BLAZE ? 10 : 5);
    return true;
}

/* ---------------------------------------------------------------- fireballs, eyes */
/* EntityFireball: from a mob towards (dx, dy, dz) with a little spread, sped up 0.1 a tick that way and
 * slowed to 0.95. item.aux: 0 a ghast's (explodes), 1 a blaze's (sets fire); state 1 once the player
 * sent it back */
static void fireball(const Entity *from, float x, float y, float z, float dx, float dy, float dz, int small) {
    Entity *f = ent_new(E_FIREBALL, x, y, z);
    if (!f) return;
    f->item.id = I_FIRE_CHARGE, f->item.aux = (uint16_t)small;
    float sp = small ? 0.0f : 0.4f;   /* (the large one: nextGaussian x 0.4 on each) */
    dx += rnd_gauss() * sp, dy += rnd_gauss() * sp, dz += rnd_gauss() * sp;
    float d = sqrtf(dx * dx + dy * dy + dz * dz);
    if (d < 1e-4f) d = 1;
    f->gx = dx / d * 0.1f, f->gz = dz / d * 0.1f, f->limb = dy / d * 0.1f;   /* (the acceleration: y's in limb) */
    (void)from;
}
/* explosion power p, setting fire about it (Explosion with isFlaming) */
static void explode_fire(float x, float y, float z, float p, bool blocks, bool flame) { explode_ex(x, y, z, p, blocks, flame); }
static void fireball_tick(Entity *e) {
    bool small = e->item.aux == 1;
    float ay = e->limb;
    float nx = e->x + e->vx, ny = e->y + e->vy, nz = e->z + e->vz;
    /* what it hits: the player (not its own, the first 25 ticks), a mob (sent back: its ghast), a block */
    float px = pl.x - nx, pz = pl.z - nz, r = small ? 0.5f : 1.0f;
    if (e->state == 0 && !pl.dead && px * px + pz * pz < r * r + 0.3f && ny > pl.y - r && ny < pl.y + 1.8f + r) {
        if (small) {
            player_hurt(scaled(5), DMG_FIRE);
            if (pl.fire < 100) pl.fire = 100;
        } else {
            player_hurt(scaled(6), DMG_MOB);
            explode_fire(nx, ny, nz, 1, rule(GR_MOB_GRIEFING), rule(GR_MOB_GRIEFING));
        }
        e->type = E_NONE;
        return;
    }
    if (e->state == 1)
        for (int i = 0; i < N_ENT; i++) {
            Entity *m = &ents[i];
            if (!IS_MOB(m->type) || m->state == 255) continue;
            float w = info[m->type].w / 2 + 0.5f, mx = m->x - nx, mz = m->z - nz;
            if (fabsf(mx) < w && fabsf(mz) < w && ny > m->y - 0.5f && ny < m->y + info[m->type].h) {
                /* EntityGhast.attackEntityFrom: its own fireball, sent back by the player, kills it */
                hurt(m, m->type == E_GHAST && !small ? 1000 : 6, e->vx, e->vz);
                explode_fire(nx, ny, nz, 1, rule(GR_MOB_GRIEFING), rule(GR_MOB_GRIEFING));
                e->type = E_NONE;
                return;
            }
        }
    int b = world_get(ifl(nx), ifl(ny), ifl(nz));
    if ((blk_flags[b] & BF_SOLID) || ++e->timer > 600) {
        if (small) {
            /* EntitySmallFireball: fire where it came from, against the block */
            int fx = ifl(e->x), fy = ifl(e->y), fz = ifl(e->z);
            if (world_get(fx, fy, fz) == B_AIR && rule(GR_MOB_GRIEFING)) fire_set(fx, fy, fz, 0);
        } else
            explode_fire(e->x, e->y, e->z, 1, rule(GR_MOB_GRIEFING), rule(GR_MOB_GRIEFING));
        e->type = E_NONE;
        return;
    }
    e->x = nx, e->y = ny, e->z = nz;
    bool water = blk_model[b] == M_LIQUID && !is_lava(b);
    float k = water ? 0.8f : 0.95f;
    e->vx = (e->vx + e->gx) * k, e->vy = (e->vy + ay) * k, e->vz = (e->vz + e->gz) * k;
}
/* the player hits a fireball: it goes the way they look (EntityFireball.attackEntityFrom) */
static void fireball_hit(Entity *e);
bool dims_punch(void) {
  float *o = look_ray, *d = look_ray + 3;
  for (int i = 0; i < N_ENT; i++) {
    Entity *e = &ents[i];
    if (e->type != E_FIREBALL) continue;
    float r = e->item.aux ? 0.5f : 1.0f, vx = e->x - o[0], vy = e->y - o[1], vz = e->z - o[2];
    float t = vx * d[0] + vy * d[1] + vz * d[2];
    if (t < 0 || t > (pl.mode ? 5 : 3) + r) continue;
    float cx = vx - d[0] * t, cy = vy - d[1] * t, cz = vz - d[2] * t;
    if (cx * cx + cy * cy + cz * cz > r * r) continue;
    fireball_hit(e);
    return true;
  }
  return false;
}
static void fireball_hit(Entity *e) {
    float yaw = pl.yaw * 0.017453292f, pitch = pl.pitch * 0.017453292f;
    float lx = -sinf(yaw) * cosf(pitch), ly = -sinf(pitch), lz = cosf(yaw) * cosf(pitch);
    e->vx = lx, e->vy = ly, e->vz = lz;
    e->gx = lx * 0.1f, e->gz = lz * 0.1f, e->limb = ly * 0.1f;
    e->state = 1;
}

/* EntityEnderEye: up to 12 blocks towards the stronghold, rising, then down onto it; after 80 ticks it
 * drops (four in five) or shatters */
static void eye_tick(Entity *e) {
    e->x += e->vx, e->y += e->vy, e->z += e->vz;
    float sp = sqrtf(e->vx * e->vx + e->vz * e->vz);
    float d0 = e->gx - e->x, d1 = e->gz - e->z, f1 = sqrtf(d0 * d0 + d1 * d1), f2 = atan2f(d1, d0);
    float d2 = sp + (f1 - sp) * 0.0025f;
    if (f1 < 1) d2 *= 0.8f, e->vy *= 0.8f;
    e->vx = cosf(f2) * d2, e->vz = sinf(f2) * d2;
    e->vy += ((e->y < e->limb ? 1.0f : -1.0f) - e->vy) * 0.015f;   /* (limb: the height it makes for) */
    if (++e->timer > 80) {
        e->type = E_NONE;
        if (e->delay) ent_drop(I_ENDER_EYE, 1, 0, e->x, e->y, e->z, false);
    }
}
void throw_eye(int tx, int ty, int tz) {
    Entity *e = ent_new(E_EYE, pl.x, pl.y + 1.62f, pl.z);
    if (!e) return;
    e->item.id = I_ENDER_EYE;
    float d1 = tx - e->x, d2 = tz - e->z, f = sqrtf(d1 * d1 + d2 * d2);
    if (f > 12) e->gx = e->x + d1 / f * 12, e->gz = e->z + d2 / f * 12, e->limb = e->y + 8;
    else e->gx = (float)tx, e->gz = (float)tz, e->limb = (float)ty;
    e->delay = rnd(5) > 0;
}

/* ---------------------------------------------------------------- ghasts, blazes, magma cubes */
/* EntityGhast: flies about (EntityAIRandomFly: somewhere within 16, while the way is clear), faces the player
 * within 64 it can see, warns at 10 and shoots at 20, then waits 40 */
static void ghast_tick(Entity *e) {
    float gx = e->gx - e->x, gy = (float)e->tz - e->y, gz = e->gz - e->z, d = gx * gx + gy * gy + gz * gz;
    if (d < 1 || d > 3600 || e->panic <= 0) {
        e->gx = e->x + (rndf() * 2 - 1) * 16, e->tz = (int16_t)(e->y + (rndf() * 2 - 1) * 16), e->gz = e->z + (rndf() * 2 - 1) * 16;
        if (e->tz < 5) e->tz = 5;
        if (e->tz > 120) e->tz = 120;
        e->panic = 1;
    }
    /* GhastMoveHelper: a push towards it now and then, if the way there is free */
    if (--e->delay <= 0) {
        e->delay = (int16_t)(rnd(5) + 2);
        gx = e->gx - e->x, gy = (float)e->tz - e->y, gz = e->gz - e->z;
        float l = sqrtf(gx * gx + gy * gy + gz * gz);
        bool free = l > 0;
        for (int k = 1; k < l && free; k++) free = room_for(e->x + gx / l * k, e->y + gy / l * k, e->z + gz / l * k, 4, 4);
        if (free) e->vx += gx / l * 0.1f, e->vy += gy / l * 0.1f, e->vz += gz / l * 0.1f;
        else e->panic = 0;
    }
    float dx = pl.x - e->x, dy = pl.y + 0.9f - (e->y + 2), dz = pl.z - e->z, pd = dx * dx + dy * dy + dz * dz;
    bool target = !pl.dead && pl.mode == 0 && pd < 64 * 64;
    if (target) e->yaw = -atan2f(dx, dz) * 57.29578f;
    else e->yaw = -atan2f(e->vx, e->vz) * 57.29578f;
    if (target && sees(e)) {
        if (++e->timer == 20) {
            float yr = e->yaw * 0.017453292f, lx = -sinf(yr), lz = cosf(yr);
            float fx = e->x + lx * 4, fy = e->y + 2 + 0.5f, fz = e->z + lz * 4;
            fireball(e, fx, fy, fz, pl.x - (e->x + lx * 4), pl.y + 0.9f - (0.5f + e->y + 2), pl.z - (e->z + lz * 4), 0);
            e->timer = -40;
        }
    } else if (e->timer > 0)
        e->timer--;
    e->state = e->timer > 10;
    /* EntityFlying: no gravity, the air's friction */
    float p[3] = {e->x, e->y, e->z}, v[3] = {e->vx, e->vy, e->vz};
    phys_move(p, v, 4, 4);
    e->x = p[0], e->y = p[1], e->z = p[2];
    e->vx = v[0] * 0.91f, e->vy = v[1] * 0.91f, e->vz = v[2] * 0.91f;
}

/* EntityBlaze: falls slowly, rises to the player's eyes (and an offset that changes every 5 seconds);
 * close, it hits; within 16, it sets itself alight for 3 seconds then shoots three small fireballs, 6
 * ticks apart, then rests 5 seconds */
static void blaze_tick(Entity *e, bool chase, float dist) {
    if (e->delay > 0) e->delay--;
    if (--e->growth <= 0) e->growth = 100, e->love = (int16_t)((0.5f + rnd_gauss() * 3) * 100);
    if (chase && pl.y + 1.62f > e->y + 1.53f + e->love / 100.0f) e->vy += (0.3f - e->vy) * 0.3f;
    if (!e->on_ground && e->vy < 0) e->vy *= 0.6f;
    if (!chase) return;
    float dx = pl.x - e->x, dy = pl.y + 0.9f - (e->y + 0.9f), dz = pl.z - e->z, d2 = dx * dx + dy * dy + dz * dz;
    if (d2 < 4) {
        if (e->delay <= 0) e->delay = 20, player_hurt(scaled(6), DMG_MOB), push_player(dx, dz, 0.4f);
    } else if (d2 < 256) {
        if (e->delay <= 0) {
            e->timer++;
            if (e->timer == 1) e->delay = 60, e->fire = 60;
            else if (e->timer <= 4) e->delay = 6;
            else e->delay = 100, e->timer = 0;
            if (e->timer > 1) {
                float f = sqrtf(sqrtf(d2)) * 0.5f;
                fireball(e, e->x, e->y + 0.9f + 0.5f, e->z, dx + rnd_gauss() * f, dy, dz + rnd_gauss() * f, 1);
            }
        }
    }
    (void)dist;
}

/* EntityMagmaCube: hops towards the player (every 2 to 6 seconds, a third of it when after them), its
 * jump 0.42 + 0.1 a size; touching them hurts, its size + 2 */
static void magma_tick(Entity *e, bool chase, float dist) {
    int s = e->sheared ? e->sheared : 1;
    float dx = pl.x - e->x, dz = pl.z - e->z;
    if (chase) e->yaw = -atan2f(dx, dz) * 57.29578f;
    else if (rnd(80) == 0) e->yaw = rndf() * 360;
    if (e->on_ground) {
        e->vx *= 0.2f, e->vz *= 0.2f;
        if (--e->delay <= 0) {
            e->delay = (int16_t)((rnd(20) + 10) * 4);
            if (chase) e->delay /= 3;
            e->vy = 0.42f + s * 0.1f;
            float yr = e->yaw * 0.017453292f;
            e->vx = -sinf(yr) * 0.25f, e->vz = cosf(yr) * 0.25f;
        }
    }
    if (chase && dist < 0.6f * s + 0.6f && fabsf(pl.y - e->y) < s && e->timer <= 0) {
        player_hurt(scaled((float)s + 2), DMG_MOB);
        push_player(dx, dz, 0.4f);
        e->timer = 10;
    }
    if (e->timer > 0) e->timer--;
}

/* ---------------------------------------------------------------- endermen */
/* EntityEnderman.teleportTo: there, down onto the ground, if it fits and is dry */
static bool ender_tp(Entity *e, float x, float y, float z) {
    int bx = ifl(x), bz = ifl(z), by = ifl(y);
    if (!world_loaded(bx, by, bz)) return false;
    while (by > 0 && !(blk_flags[world_get(bx, by - 1, bz)] & BF_SOLID)) by--;
    if (by <= 0 || !room_for(x, (float)by, z, 0.6f, 2.9f) || blk_model[world_get(bx, by, bz)] == M_LIQUID) return false;
    for (int k = 0; k < 8; k++)
        particle_add(e->x + (rndf() - 0.5f) * 0.6f, e->y + rndf() * 2.9f, e->z + (rndf() - 0.5f) * 0.6f, 0, 0.05f, 0, 0xA01F, 20);
    e->x = x, e->y = (float)by, e->z = z, e->vx = e->vy = e->vz = 0;
    e->px = e->x, e->py = e->y, e->pz = e->z, e->gx = e->x, e->gz = e->z, e->tx = (int16_t)bx, e->tz = (int16_t)bz;
    return true;
}
static bool ender_tp_random(Entity *e) {
    return ender_tp(e, e->x + (rndf() - 0.5f) * 64, e->y + (float)(rnd(64) - 32), e->z + (rndf() - 0.5f) * 64);
}
/* shouldAttackPlayer: the player looks at its head, and sees it */
static bool stared_at(const Entity *e) {
    float yaw = pl.yaw * 0.017453292f, pitch = pl.pitch * 0.017453292f;
    float lx = -sinf(yaw) * cosf(pitch), ly = -sinf(pitch), lz = cosf(yaw) * cosf(pitch);
    float vx = e->x - pl.x, vy = e->y + 1.45f - (pl.y + 1.62f), vz = e->z - pl.z, d = sqrtf(vx * vx + vy * vy + vz * vz);
    if (d < 1e-3f || d > 64) return false;
    return (lx * vx + ly * vy + lz * vz) / d > 1 - 0.025f / d && sees(e);
}
/* what an enderman picks up (EntityEnderman's carriable blocks) */
static bool carriable(int b) {
    return b == B_GRASS || b == B_DIRT || b == B_SAND || b == B_GRAVEL || b == B_DANDELION || b == B_POPPY ||
           b == B_BROWN_MUSHROOM || b == B_RED_MUSHROOM || b == B_TNT || b == B_CACTUS || b == B_CLAY ||
           b == B_PUMPKIN || b == B_MELON || b == B_MYCELIUM;
}
static void ender_tick(Entity *e) {
    int bx = ifl(e->x), by = ifl(e->y), bz = ifl(e->z);
    /* water and rain hurt it, and it goes */
    if (is_water(world_get(bx, by, bz)) || is_water(world_get(bx, by + 1, bz)) || rain_at(bx, by + 2, bz)) {
        hurt(e, 1, 0, 0);
        e->panic = 0;
        ender_tp_random(e);
    }
    /* by day in the Overworld's sky, now and then it goes */
    if (dim == DIM_OVERWORLD && sky_sub() < 4 && light_there(bx, by + 1, bz) > 7 && rndf() * 30 < 1.2f) {
        e->panic = 0;
        ender_tp_random(e);
    }
    /* EntityAIFindPlayer: looked at for 5 ticks: it screams and comes; close and still looked at, it goes;
     * far and angry, it comes to the player */
    if (!pl.dead && pl.mode == 0 && opt.difficulty > 0) {
        if (stared_at(e)) {
            if (e->panic <= 0 && ++e->timer >= 5) e->panic = 600, e->timer = 0;
            else if (e->panic > 0 && (pl.x - e->x) * (pl.x - e->x) + (pl.z - e->z) * (pl.z - e->z) < 16) ender_tp_random(e);
        } else
            e->timer = 0;
        float d2 = (pl.x - e->x) * (pl.x - e->x) + (pl.z - e->z) * (pl.z - e->z);
        if (e->panic > 0 && d2 > 256 && rnd(30) == 0) {
            float a = rndf() * 6.2831853f;
            ender_tp(e, pl.x + cosf(a) * 4, pl.y + 4, pl.z + sinf(a) * 4);
        }
    }
    /* EntityEnderman.AITakeBlock and AIPlaceBlock (mobGriefing): love holds the block carried */
    if (!rule(GR_MOB_GRIEFING)) return;
    if (!e->love && rnd(20) == 0) {
        int x = ifl(e->x - 1 + rndf() * 2), y = ifl(e->y + rndf() * 2), z = ifl(e->z - 1 + rndf() * 2);
        int b = world_get(x, y, z);
        if (carriable(b) && world_loaded(x, y, z)) {
            world_set(x, y, z, B_AIR);
            neighbours_changed(x, y, z);
            e->love = (int16_t)b;
        }
    } else if (e->love && rnd(2000) == 0) {
        int x = ifl(e->x - 1 + rndf() * 2), y = ifl(e->y + rndf() * 2), z = ifl(e->z - 1 + rndf() * 2);
        int below = world_get(x, y - 1, z);
        if (world_get(x, y, z) == B_AIR && below != B_AIR && (blk_flags[below] & BF_OPAQUE) && world_loaded(x, y, z)) {
            world_set(x, y, z, e->love);
            e->love = 0;
        }
    }
}

/* the dimensions' creatures each tick: the ones that are all theirs (true), or what the others do before
 * they go on as zombies do */
void dragon_tick(Entity *e);     /* (end.c) */
void crystal_gone(Entity *e);    /* (end.c) */
static bool dims_tick(Entity *e) {
    if (e->type == E_FIREBALL) {
        fireball_tick(e);
        return true;
    }
    if (e->type == E_EYE) {
        eye_tick(e);
        return true;
    }
    if (e->type == E_DRAGON) {
        dragon_tick(e);
        return true;
    }
    if (e->type == E_CRYSTAL) {
        /* EntityEnderCrystal: fire where it is, in the End; caught in an explosion, it blows up */
        if (e->health <= 0) {
            crystal_gone(e);
            return true;
        }
        if (e->hurt > 0) e->hurt--;
        if (dim == DIM_END && world_get(ifl(e->x), ifl(e->y), ifl(e->z)) == B_AIR) world_set(ifl(e->x), ifl(e->y), ifl(e->z), B_FIRE);
        return true;
    }
    if (e->type == E_PIGMAN && e->panic > 0) e->panic--;
    if (e->type == E_ENDERMAN && e->state != 255) ender_tick(e);
    if (e->type == E_GHAST && e->state != 255) {
        if (e->hurt > 0) e->hurt--;
        if (e->invuln > 0) e->invuln--;
        ghast_tick(e);
        return true;
    }
    return false;
}

/* the dimensions' creatures hurt (after their health went down) */
void dragon_hurt(Entity *e, float dmg);   /* (end.c) */
static void dims_hurt(Entity *e, float kx, float kz) {
    if (e->type == E_PIGMAN && (kx != 0 || kz != 0)) {
        /* EntityPigZombie.becomeAngryAt: every pigman within 32 */
        for (int i = 0; i < N_ENT; i++) {
            Entity *o = &ents[i];
            if (o->type == E_PIGMAN && fabsf(o->x - e->x) < 32 && fabsf(o->y - e->y) < 32 && fabsf(o->z - e->z) < 32)
                o->panic = (int16_t)(400 + rnd(400));
        }
    }
    if (e->type == E_ENDERMAN && (kx != 0 || kz != 0)) e->panic = 600;
}
