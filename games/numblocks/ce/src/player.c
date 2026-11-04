/* The player: Minecraft 1.8's movement (EntityLivingBase.moveEntityWithHeading: 0.1 a tick on the ground, slowed
 * by friction 0.546, 0.02 in the air, gravity 0.08 and drag 0.98, a jump of 0.42, swimming), the box of 0.6 x
 * 1.8 against the blocks, mining (Block.getPlayerRelativeBlockHardness: the tool's speed over the hardness, 30
 * ticks for each with the right tool, 100 without), placing, falling, drowning, lava, and health coming back.
 * Fixed point: 4096 a block. */
#include "nb.h"

Player pl;

#define W2 (FX1 * 3 / 10)    /* half the box's width */
#define BOX_H (FX1 * 18 / 10)
#define C(v) ((fx)((v) * 4096 + ((v) >= 0 ? 0.5 : -0.5)))

static bool solid_at(int x, int y, int z) { return (blk_flags[world_get(x, y, z)] & BF_SOLID) != 0; }

/* the box: feet at (x, y, z); how far it can move d along axis a (0 x, 1 y, 2 z) before a solid block */
static fx clip_move(fx x, fx y, fx z, int a, fx d) {
  if (!d) return 0;
  fx lo[3] = {x - W2, y, z - W2}, hi[3] = {x + W2, y + BOX_H, z + W2};
  /* the blocks the box overlaps across the other two axes (touching is not overlapping) */
  int b0[3], b1[3];
  for (int k = 0; k < 3; k++) b0[k] = FXI(lo[k]), b1[k] = FXI(hi[k] - 1);
  if (d > 0) {
    int s = FXI(hi[a] - 1) + 1, e = FXI(hi[a] + d - 1);
    for (int c = s; c <= e; c++) {
      b0[a] = b1[a] = c;
      for (int i = b0[0]; i <= b1[0]; i++)
        for (int j = b0[1]; j <= b1[1]; j++)
          for (int k = b0[2]; k <= b1[2]; k++)
            if (solid_at(i, j, k)) return (fx)c * FX1 - hi[a];
    }
  } else {
    int s = FXI(lo[a]) - 1, e = FXI(lo[a] + d);
    for (int c = s; c >= e; c--) {
      b0[a] = b1[a] = c;
      for (int i = b0[0]; i <= b1[0]; i++)
        for (int j = b0[1]; j <= b1[1]; j++)
          for (int k = b0[2]; k <= b1[2]; k++)
            if (solid_at(i, j, k)) return (fx)(c + 1) * FX1 - lo[a];
    }
  }
  return d;
}

/* standing on something, a little lower? (sneaking keeps the player from walking off an edge) */
static bool ground_under(fx x, fx y, fx z) { return clip_move(x, y, z, 1, -C(0.6)) != -C(0.6); }

static void move(fx dx, fx dy, fx dz) {
  fx ody = dy, odx = dx, odz = dz;
  if (pl.sneaking && pl.on_ground && !pl.flying) {
    /* (Entity.moveEntity: back off while the step would leave no ground) */
    while (dx && !ground_under(pl.x + dx, pl.y, pl.z)) dx = dx > C(0.05) ? dx - C(0.05) : dx < -C(0.05) ? dx + C(0.05) : 0;
    while (dz && !ground_under(pl.x, pl.y, pl.z + dz)) dz = dz > C(0.05) ? dz - C(0.05) : dz < -C(0.05) ? dz + C(0.05) : 0;
    odx = dx, odz = dz;
  }
  dy = clip_move(pl.x, pl.y, pl.z, 1, dy);
  pl.y += dy;
  dx = clip_move(pl.x, pl.y, pl.z, 0, dx);
  pl.x += dx;
  dz = clip_move(pl.x, pl.y, pl.z, 2, dz);
  pl.z += dz;
  pl.on_ground = ody < 0 && dy != ody;
  if (dy != ody) pl.vy = 0;
  if (dx != odx) pl.vx = 0;
  if (dz != odz) pl.vz = 0;
}

/* Entity.moveFlying: speed f along the input, turned by the yaw */
static void move_flying(int s, int fw, fx f) {
  /* inputs: -98..98 (Minecraft's 0.98); their length, at least 1, spreads the speed over both */
  int32_t d2 = (int32_t)s * s + (int32_t)fw * fw;
  if (!d2) return;
  int d = 100;
  while ((int32_t)(d + 1) * (d + 1) <= d2) d++;
  fx ks = f * s / d, kf = f * fw / d;
  int sy = fsin(pl.yaw), cy = fcos(pl.yaw);
  pl.vx += (ks * cy - kf * sy) >> 14;
  pl.vz += (kf * cy + ks * sy) >> 14;
}

static bool in_block(int b) {
  /* any cell the box (a little smaller) is in holds b */
  fx m = C(0.001);
  for (int i = FXI(pl.x - W2 + m); i <= FXI(pl.x + W2 - m); i++)
    for (int j = FXI(pl.y + C(0.4)); j <= FXI(pl.y + BOX_H - C(0.4)); j++)
      for (int k = FXI(pl.z - W2 + m); k <= FXI(pl.z + W2 - m); k++)
        if (world_get(i, j, k) == b) return true;
  return false;
}

void player_hurt(int halves) {
  if (pl.mode || pl.dead || pl.hurt_time > 0 || halves <= 0) return;
  pl.health -= halves;
  pl.hurt_time = 10;
  if (pl.health <= 0) pl.health = 0, pl.dead = true, gui_open(GUI_DEATH);
}

/* ---------------------------------------------------------------- the block looked at */
int ray_pick(int reach, int *hx, int *hy, int *hz, int *face) {
  /* from the eyes along the view: a cell at a time (voxel traversal) */
  int sy = fsin(pl.yaw), cy = fcos(pl.yaw), sp = fsin((uint16_t)pl.pitch), cp = fcos((uint16_t)pl.pitch);
  int32_t d[3] = {-(int32_t)sy * cp >> 14, -sp, (int32_t)cy * cp >> 14};   /* Q14 */
  fx o[3] = {pl.x, pl.y + (pl.sneaking ? C(1.54) : C(1.62)), pl.z};
  int c[3], step[3];
  int32_t tmax[3], tdel[3];   /* in 1/4096 blocks of the ray */
  for (int k = 0; k < 3; k++) {
    c[k] = FXI(o[k]);
    int32_t frac = o[k] - (fx)c[k] * FX1;
    if (d[k] > 0) step[k] = 1, tdel[k] = (int32_t)(((int32_t)FX1 << 14) / d[k]), tmax[k] = (int32_t)(((int32_t)(FX1 - frac) << 14) / d[k]);
    else if (d[k] < 0) step[k] = -1, tdel[k] = (int32_t)(((int32_t)FX1 << 14) / -d[k]), tmax[k] = (int32_t)(((int32_t)frac << 14) / -d[k]);
    else step[k] = 0, tdel[k] = tmax[k] = 0x7FFFFFFF;
  }
  int f = -1;
  for (int n = 0; n < 32; n++) {
    int b = world_get(c[0], c[1], c[2]);
    if (b != B_AIR && !(blk_flags[b] & BF_LIQUID) && n > 0) {
      *hx = c[0], *hy = c[1], *hz = c[2], *face = f;
      return b;
    }
    if (n == 0 && b != B_AIR && !(blk_flags[b] & BF_LIQUID)) {
      *hx = c[0], *hy = c[1], *hz = c[2], *face = 1;
      return b;
    }
    int k = tmax[0] < tmax[1] ? (tmax[0] < tmax[2] ? 0 : 2) : (tmax[1] < tmax[2] ? 1 : 2);
    if (tmax[k] > reach) break;
    c[k] += step[k];
    tmax[k] += tdel[k];
    /* the face entered: -Y +Y -Z +Z -X +X (index of the face of the new cell towards where the ray came from) */
    f = k == 1 ? (step[k] > 0 ? 0 : 1) : k == 2 ? (step[k] > 0 ? 2 : 3) : (step[k] > 0 ? 4 : 5);
  }
  return 0;
}

/* ---------------------------------------------------------------- items */
int item_max(int id) { return id >= IT0 && (it_kind[id - IT0] & 15) ? 1 : 64; }
int item_dur(int id) { return id >= IT0 ? it_dur[id - IT0] : 0; }

int inv_add(int id, int n, int dmg) {
  int mx = item_max(id);
  for (int pass = 0; pass < 2 && n > 0; pass++)
    for (int i = 0; i < 36 && n > 0; i++) {
      Stack *s = &pl.inv[i];
      if (pass == 0 && s->id == id && s->n < mx && mx > 1) {
        int k = mx - s->n < n ? mx - s->n : n;
        s->n = (uint8_t)(s->n + k), n -= k;
      } else if (pass == 1 && !s->id) {
        int k = n < mx ? n : mx;
        s->id = (uint8_t)id, s->n = (uint8_t)k, s->dmg = (uint16_t)dmg, n -= k;
      }
    }
  return n;
}

/* the ticks it takes to mine b with what is held (ForgeHooks-free 1.8: getPlayerRelativeBlockHardness) */
static bool can_harvest(int b, int tool, int tier) {
  /* (every block mined fastest with a pickaxe here is rock or metal: it needs one) */
  if ((blk_tool[b] & 15) != T_PICKAXE) return true;
  return tool == T_PICKAXE && tier >= blk_tool[b] >> 4;
}
static int break_time(int b) {
  int hard = blk_hard[b];   /* x 4 */
  if (hard == 255) return -1;
  if (pl.mode) return 0;
  if (hard == 0) return 0;
  const Stack *h = held();
  int tool = 0, tier = 0, speed = 1;
  if (h->id >= IT0) {
    int k = it_kind[h->id - IT0];
    tool = k & 15, tier = k >> 4;
    if (tool && tool == (blk_tool[b] & 15)) speed = it_speed[h->id - IT0];
    if (tool == T_SWORD && (b == B_LEAVES_OAK || b == B_LEAVES_SPRUCE || b == B_LEAVES_BIRCH)) speed = 1;
  }
  int per = can_harvest(b, tool, tier) ? 30 : 100;
  /* ticks = hardness * per / speed, a little more under water or in the air */
  int32_t t = ((int32_t)hard * per + speed * 4 - 1) / (speed * 4);
  if (pl.in_water) t *= 5;
  if (!pl.on_ground && !pl.flying) t *= 5;
  return (int)(t < 1 ? 1 : t);
}

static void wear(void) {
  Stack *h = held();
  if (pl.mode || h->id < IT0 || !(it_kind[h->id - IT0] & 15)) return;
  if (++h->dmg >= it_dur[h->id - IT0]) h->id = 0, h->n = 0, h->dmg = 0;
}

static void break_block(int x, int y, int z) {
  int b = world_get(x, y, z);
  if (b == B_CHEST || b == B_FURNACE || b == B_FURNACE_LIT) {
    Tile *t = tile_at(x, y, z);
    if (t && !pl.mode)
      for (int i = 0; i < 27; i++)
        if (t->s[i].id) inv_add(t->s[i].id, t->s[i].n, t->s[i].dmg);
    tile_remove(x, y, z);
  }
  world_set(x, y, z, B_AIR);
  if (!pl.mode) {
    const Stack *h = held();
    int tool = 0, tier = 0;
    if (h->id >= IT0) tool = it_kind[h->id - IT0] & 15, tier = it_kind[h->id - IT0] >> 4;
    int d = blk_drop[b];
    if (b == B_GRAVEL && rnd(10) == 0) d = I_FLINT;
    if ((b == B_LEAVES_OAK) && rnd(200) == 0) d = I_APPLE;
    if ((b == B_LEAVES_OAK || b == B_LEAVES_BIRCH || b == B_LEAVES_SPRUCE) && d == 255 && rnd(20) == 0)
      d = b == B_LEAVES_OAK ? B_SAPLING_OAK : b == B_LEAVES_BIRCH ? B_SAPLING_BIRCH : B_SAPLING_SPRUCE;
    if (d != 255 && can_harvest(b, tool, tier)) {
      int n = b == B_LAPIS_ORE ? 4 + rnd(5) : b == B_REDSTONE_ORE ? 4 + rnd(2) : 1;
      inv_add(d, n, 0);
    }
    wear();
  }
  /* what stood on it falls (plants, torches); sand and gravel fall too */
  int above = world_get(x, y + 1, z);
  if (blk_model[above] == M_CROSS || blk_model[above] == M_TORCH) break_block(x, y + 1, z);
  for (int k = y + 1; k < WORLD_H; k++) {
    int a = world_get(x, k, z);
    if (a != B_SAND && a != B_GRAVEL) break;
    world_set(x, k, z, B_AIR);
    world_set(x, k - 1, z, a);
  }
}

static void place(int x, int y, int z, int face) {
  Stack *h = held();
  if (!h->id || h->id >= IT0) return;
  int b = h->id;
  int nx = x + (face == 4 ? -1 : face == 5 ? 1 : 0), ny = y + (face == 0 ? -1 : face == 1 ? 1 : 0),
      nz = z + (face == 2 ? -1 : face == 3 ? 1 : 0);
  int there = world_get(nx, ny, nz);
  int at = world_get(x, y, z);
  if (at == B_TALL_GRASS || at == B_DEAD_BUSH) nx = x, ny = y, nz = z, there = at;
  else if (there != B_AIR && !(blk_flags[there] & BF_LIQUID)) return;
  if (!in_win(nx, ny, nz) || ny >= WORLD_H || ny < 1) return;
  /* plants need ground; torches a floor or a wall */
  int below = world_get(nx, ny - 1, nz);
  if (blk_model[b] == M_CROSS) {
    bool sap = b == B_SAPLING_OAK || b == B_SAPLING_SPRUCE || b == B_SAPLING_BIRCH || b == B_DANDELION || b == B_POPPY ||
               b == B_TALL_GRASS;
    if (sap && below != B_GRASS && below != B_DIRT && below != B_GRASS_SNOWED) return;
    if (b == B_DEAD_BUSH && below != B_SAND) return;
    if (b == B_SUGAR_CANE && below != B_SUGAR_CANE && below != B_GRASS && below != B_DIRT && below != B_SAND) return;
  }
  if (b == B_TORCH && !(blk_flags[below] & BF_OPAQUE) && face < 2) return;
  if (b == B_CACTUS && below != B_SAND && below != B_CACTUS) return;
  /* not into the player */
  if (blk_flags[b] & BF_SOLID) {
    fx x0 = pl.x - W2, x1 = pl.x + W2, z0 = pl.z - W2, z1 = pl.z + W2, y0 = pl.y, y1 = pl.y + BOX_H;
    if ((fx)nx * FX1 < x1 && (fx)(nx + 1) * FX1 > x0 && (fx)ny * FX1 < y1 && (fx)(ny + 1) * FX1 > y0 &&
        (fx)nz * FX1 < z1 && (fx)(nz + 1) * FX1 > z0)
      return;
  }
  if (!edits_room(1)) {
    gui_message("Out of memory");
    return;
  }
  if (b == B_CHEST || b == B_FURNACE) {
    /* its front towards the player (BlockFurnace.onBlockPlacedBy) */
    int q = ((pl.yaw + 8192) >> 14) & 3;   /* 0 south, 1 west, 2 north, 3 east: the way the player faces */
    static const uint8_t front[4] = {2, 5, 3, 4};
    if (!tile_new(nx, ny, nz, b, front[q])) {
      gui_message("Too many chests and furnaces");
      return;
    }
  }
  world_set(nx, ny, nz, b);
  pl.swing = 6;
  if (!pl.mode && !--h->n) h->id = 0;
}

static void use(void) {
  if (pl.hit_face < 0) return;
  int b = world_get(pl.hit_x, pl.hit_y, pl.hit_z);
  if (!pl.sneaking) {
    if (b == B_CRAFTING_TABLE) { gui_open(GUI_CRAFTING); return; }
    if (b == B_FURNACE || b == B_FURNACE_LIT || b == B_CHEST) {
      extern Tile *gui_tile;
      gui_tile = tile_at(pl.hit_x, pl.hit_y, pl.hit_z);
      if (!gui_tile) gui_tile = tile_new(pl.hit_x, pl.hit_y, pl.hit_z, b == B_CHEST ? B_CHEST : B_FURNACE, 3);
      if (gui_tile) gui_open(b == B_CHEST ? GUI_CHEST : GUI_FURNACE);
      return;
    }
  }
  place(pl.hit_x, pl.hit_y, pl.hit_z, pl.hit_face);
}

/* ---------------------------------------------------------------- a tick */
void player_spawn(void) {
  pl.x = (fx)pl.spawn_x * FX1 + FX1 / 2;
  pl.z = (fx)pl.spawn_z * FX1 + FX1 / 2;
  pl.y = (fx)pl.spawn_y * FX1;
  pl.vx = pl.vy = pl.vz = 0;
  pl.health = 20, pl.air = 300, pl.dead = false, pl.fall_from = -1;
  pl.hit_face = -1;
  pl.break_ticks = 0;
}

static int last_attack;
void player_tick(uint32_t keys, uint32_t pressed) {
  if (pl.dead) return;
  if (pl.hurt_time) pl.hurt_time--;
  if (pl.swing) pl.swing--;
  /* looking: the arrows (180 degrees a second at the top speed, easing in) */
  /* (done each frame in main.c) */
  /* the hotbar */
  for (int k = 0; k < 9; k++)
    if (pressed & K_SLOT(k)) pl.slot = k;
  int fw = (keys & K_FWD ? 98 : 0) - (keys & K_BACK ? 98 : 0);
  int st = (keys & K_SLEFT ? 98 : 0) - (keys & K_SRIGHT ? 98 : 0);
  pl.sneaking = (keys & K_SNEAK) && !pl.flying;
  if (pl.sneaking) fw = fw * 3 / 10, st = st * 3 / 10;
  /* sprinting: del, or forward twice quickly */
  if (pressed & K_FWD) {
    if (pl.fwd_tap > 0 && !pl.sneaking) pl.sprinting = true;
    pl.fwd_tap = 7;
  }
  if (pl.fwd_tap) pl.fwd_tap--;
  if ((keys & K_SPRINT) && fw > 0 && !pl.sneaking) pl.sprinting = true;
  if (fw <= 0 || pl.sneaking) pl.sprinting = false;
  /* flying: jump twice in Creative */
  if (pl.mode && (pressed & K_JUMP)) {
    if (pl.jump_wait > 0) pl.flying = !pl.flying, pl.jump_wait = 0, pl.vy = 0;
    else pl.jump_wait = 7;
  }
  if (pl.jump_wait) pl.jump_wait--;
  pl.in_water = in_block(B_WATER);
  pl.in_lava = in_block(B_LAVA);
  fx fall_y = pl.y;
  if (pl.flying) {
    move_flying(st, fw, pl.sprinting ? C(0.1) : C(0.05));
    if (keys & K_JUMP) pl.vy += C(0.15);
    if (keys & K_SNEAK) pl.vy -= C(0.15);
    move(pl.vx, pl.vy, pl.vz);
    pl.vy = pl.vy * 6 / 10;
    pl.vx = pl.vx * 3727 / 4096, pl.vz = pl.vz * 3727 / 4096;
    if (pl.on_ground) pl.flying = false;
  } else if (pl.in_water || pl.in_lava) {
    move_flying(st, fw, C(0.02));
    if (keys & K_JUMP) pl.vy += C(0.04);
    move(pl.vx, pl.vy, pl.vz);
    int k = pl.in_water ? 3277 : 2048;   /* 0.8, 0.5 */
    pl.vx = pl.vx * k / 4096, pl.vy = pl.vy * k / 4096, pl.vz = pl.vz * k / 4096;
    pl.vy -= C(0.02);
    pl.fall_from = -1;
  } else {
    /* on the ground: 0.1 (x 1.3 sprinting) x 0.16277 / 0.546^3; in the air 0.02 (0.026) */
    fx f;
    if (pl.on_ground) f = pl.sprinting ? C(0.13) : C(0.1);
    else f = pl.sprinting ? C(0.026) : C(0.02);
    move_flying(st, fw, f);
    if ((keys & K_JUMP) && pl.on_ground) {
      pl.vy = C(0.42);
      if (pl.sprinting) {
        pl.vx -= (fx)fsin(pl.yaw) * C(0.2) >> 14;
        pl.vz += (fx)fcos(pl.yaw) * C(0.2) >> 14;
      }
    }
    move(pl.vx, pl.vy, pl.vz);
    pl.vy = (pl.vy - C(0.08)) * 4014 / 4096;
    int fr = pl.on_ground ? 2236 : 3727;   /* 0.546, 0.91 */
    pl.vx = pl.vx * fr / 4096, pl.vz = pl.vz * fr / 4096;
  }
  /* falling: the height fallen, the damage on landing (more than 3 blocks) */
  if (pl.on_ground || pl.flying || pl.mode) {
    if (pl.fall_from >= 0 && !pl.flying) {
      int dmg = (int)((pl.fall_from - pl.y + FX1 - 1) / FX1) - 3;
      if (dmg > 0 && !pl.in_water) player_hurt(dmg);
    }
    pl.fall_from = -1;
  } else if (pl.vy < 0 && pl.fall_from < 0) pl.fall_from = (int32_t)fall_y;
  if (pl.fall_from >= 0 && pl.y > pl.fall_from) pl.fall_from = (int32_t)pl.y;
  /* breath, lava, the void */
  int head = world_get(FXI(pl.x), FXI(pl.y + C(1.62)), FXI(pl.z));
  if (head == B_WATER && !pl.mode) {
    if (--pl.air <= -20) pl.air = 0, player_hurt(2);
  } else pl.air = 300;
  if (pl.in_lava && !(world_time & 7)) player_hurt(4);
  if (pl.y < -FX1 * 8) player_hurt(4);
  if (!pl.mode && pl.health < 20 && pl.health > 0 && ++pl.regen >= 80) pl.health++, pl.regen = 0;
  /* the block looked at */
  int hx, hy, hz, hf;
  int hb = ray_pick(pl.mode ? FX1 * 5 : FX1 * 9 / 2, &hx, &hy, &hz, &hf);
  if (!hb) hf = -1;
  if (hf < 0 || hx != pl.hit_x || hy != pl.hit_y || hz != pl.hit_z) pl.break_ticks = 0;
  pl.hit_x = hx, pl.hit_y = hy, pl.hit_z = hz, pl.hit_face = hf;
  /* mining: hold 2nd */
  if (last_attack) last_attack--;
  if ((keys & K_ATTACK) && hf >= 0) {
    if (!pl.swing) pl.swing = 6;
    int t = break_time(hb);
    if (t >= 0 && !last_attack) {
      pl.break_need = t;
      if (++pl.break_ticks > t) {
        break_block(hx, hy, hz);
        pl.break_ticks = 0;
        last_attack = pl.mode ? 5 : 0;
      }
    }
  } else pl.break_ticks = 0;
  if (pressed & K_USE) use();
  if (pressed & K_PICK && hf >= 0 && hb < B_N && pl.mode) {
    Stack *h = held();
    h->id = (uint8_t)hb, h->n = 1, h->dmg = 0;
  }
  if (pressed & K_DROP) {
    Stack *h = held();
    if (h->id && !--h->n) h->id = 0;
  }
}
