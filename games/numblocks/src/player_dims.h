/* NumBlocks.nwa only (NB_DIMS), included at the end of player.c: using the Nether's and the End's items and
 * blocks (1.8.8's ItemBlock for the new blocks, ItemEnderPearl, ItemEnderEye, ItemFireball,
 * BlockDragonEgg) */

/* BlockDragonEgg.teleport: to air within 15 across and 7 up or down, of 1000 tries */
static void egg_teleport(int x, int y, int z) {
  for (int i = 0; i < 1000; i++) {
    int a = rnd(16), b = rnd(16), c = rnd(8), d = rnd(8), e = rnd(16), f = rnd(16);
    int bx = x + a - b, by = y + c - d, bz = z + e - f;
    if (world_get(bx, by, bz) != B_AIR || !world_loaded(bx, by, bz)) continue;
    for (int k = 0; k < 16; k++)
      particle_add(x + rndf(), y + rndf(), z + rndf(), (rndf() - 0.5f) * 0.2f, (rndf() - 0.5f) * 0.2f, (rndf() - 0.5f) * 0.2f,
                   0x780F, 20);
    world_set(bx, by, bz, B_DRAGON_EGG);
    world_set(x, y, z, B_AIR);
    neighbours_changed(x, y, z);
    /* (it falls, as sand does) */
    while (by > 0 && replaceable(world_get(bx, by - 1, bz))) world_set(bx, by, bz, B_AIR), world_set(bx, --by, bz, B_DRAGON_EGG);
    return;
  }
}

/* ItemEnderEye.onItemUse on a frame: its eye, then if every frame of the ring has one, the portal. The
 * frames do not keep which way they face here: each way is tried (a ring with frames turned wrong still
 * opens) */
static bool frame_eye(int x, int y, int z) {
  return world_get(x, y, z) == B_END_PORTAL_FRAME_EYE;
}
static void end_ring(int x, int y, int z) {
  static const int8_t FX[4] = {0, 1, 0, -1}, FZ[4] = {-1, 0, 1, 0};   /* north, east, south, west */
  for (int f = 0; f < 4; f++) {
    int r = (f + 1) & 3;   /* rotateY */
    int j = 0, k = 0, found = 0, ok = 1;
    for (int l = -2; l <= 2; l++) {
      int bx = x + FX[r] * l, bz = z + FZ[r] * l, b = world_get(bx, y, bz);
      if (b == B_END_PORTAL_FRAME || b == B_END_PORTAL_FRAME_EYE) {
        if (b != B_END_PORTAL_FRAME_EYE) {
          ok = 0;
          break;
        }
        k = l;
        if (!found) j = l, found = 1;
      }
    }
    if (!ok || k != j + 2) continue;
    int fx = x + FX[f] * 4, fz = z + FZ[f] * 4;
    for (int i = j; i <= k && ok; i++) ok = frame_eye(fx + FX[r] * i, y, fz + FZ[r] * i);
    for (int i = j - 1; i <= k + 1 && ok; i += 4)
      for (int l = 1; l <= 3 && ok; l++) ok = frame_eye(x + FX[r] * i + FX[f] * l, y, z + FZ[r] * i + FZ[f] * l);
    if (!ok) continue;
    for (int i = j; i <= k; i++)
      for (int l = 1; l <= 3; l++) world_set(x + FX[r] * i + FX[f] * l, y, z + FZ[r] * i + FZ[f] * l, B_END_PORTAL);
    return;
  }
}

/* ItemDoor (iron): its lower half where the player looks from, facing away, the upper over it */
static bool place_iron_door(void) {
  int f = pl.hit_face, x = pl.hit_x, y = pl.hit_y, z = pl.hit_z, at = world_get(x, y, z);
  if (!(replaceable(at) && at != B_AIR)) x += NX[f], y += NY[f], z += NZ[f];
  if (y < 1 || y + 1 >= WORLD_H || !world_loaded(x, y, z) || !replaceable(world_get(x, y, z)) ||
      !replaceable(world_get(x, y + 1, z)) || !solid_top(world_get(x, y - 1, z)) || !box_free(x, y, z) || !box_free(x, y + 1, z))
    return false;
  static const uint16_t door[4] = {B_IRON_DOOR_LOWER_S, B_IRON_DOOR_LOWER_W, B_IRON_DOOR_LOWER_N, B_IRON_DOOR_LOWER_E};
  world_set(x, y + 1, z, B_IRON_DOOR_UPPER);
  set_block(x, y, z, door[(int)floorf(pl.yaw / 90 + 0.5f) & 3]);
  return true;
}

/* BlockButton: pressed, it powers the block behind it and its neighbours; the doors there open
 * (BlockDoor.onNeighborBlockChange) until it lets go, 20 ticks on */
static struct {
  int16_t x, z;
  uint8_t y, t;
} presses[4];
static void doors_powered(int x, int y, int z, int b, bool open) {
  static const int8_t bx[4] = {-1, 1, 0, 0}, bz[4] = {0, 0, -1, 1};
  int k = b - B_STONE_BUTTON_E, at[2][3] = {{x, y, z}, {x + bx[k], y, z + bz[k]}};
  for (int p = 0; p < 2; p++)
    for (int f = 0; f < 6; f++) {
      int dx = at[p][0] + NX[f], dy = at[p][1] + NY[f], dz = at[p][2] + NZ[f], d = world_get(dx, dy, dz);
      if (d == B_IRON_DOOR_UPPER || d == B_DOOR_OAK_UPPER) d = world_get(dx, --dy, dz);
      int st = is_iron_door(d) ? d - B_IRON_DOOR_LOWER_E
               : d == B_DOOR_OAK_LOWER ? 0 : is_door_lower(d) ? d - B_DOOR_OAK_LOWER_S + 1 : -1;
      if (st < 0 || (st >= 4) == open) continue;
      st = open ? st | 4 : st & 3;
      if (is_iron_door(d)) world_set(dx, dy, dz, B_IRON_DOOR_LOWER_E + st);
      else world_set(dx, dy, dz, st == 0 ? B_DOOR_OAK_LOWER : B_DOOR_OAK_LOWER_S + st - 1);
    }
}
static void button_press(int x, int y, int z, int b) {
  for (int i = 0; i < 4; i++)
    if (presses[i].t && presses[i].x == x && presses[i].y == y && presses[i].z == z) return;   /* (already down) */
  for (int i = 0; i < 4; i++)
    if (!presses[i].t) {
      presses[i].x = (int16_t)x, presses[i].y = (uint8_t)y, presses[i].z = (int16_t)z, presses[i].t = 20;
      doors_powered(x, y, z, b, true);
      return;
    }
}
static void buttons_tick(void) {
  for (int i = 0; i < 4; i++)
    if (presses[i].t && !--presses[i].t) {
      int b = world_get(presses[i].x, presses[i].y, presses[i].z);
      if (b >= B_STONE_BUTTON_E && b <= B_STONE_BUTTON_N) doors_powered(presses[i].x, presses[i].y, presses[i].z, b, false);
    }
}

/* BlockPressurePlate: stepped on, it powers the block under it and its neighbours; TNT next to either is lit
 * (BlockTNT.onNeighborBlockChange): the desert temples' trap */
static void plates_tick(void) {
  static int16_t last[3] = {0, -1, 0};
  int x = ifloor(pl.x), y = ifloor(pl.y), z = ifloor(pl.z), b = world_get(x, y, z);
  if (b != B_WOODEN_PRESSURE_PLATE && b != B_STONE_PRESSURE_PLATE) {
    last[1] = -1;
    return;
  }
  if (last[0] == x && last[1] == y && last[2] == z) return;
  last[0] = (int16_t)x, last[1] = (int16_t)y, last[2] = (int16_t)z;
  for (int p = 0; p < 2; p++)
    for (int f = 0; f < 6; f++) {
      int tx = x + NX[f], ty = y - p + NY[f], tz = z + NZ[f];
      if (world_get(tx, ty, tz) == B_TNT) world_set(tx, ty, tz, B_AIR), tnt_light(tx, ty, tz, 80);
    }
}

/* using an item of the Nether or the End (true: done) */
static bool dims_use(int k, bool on_block, int x, int y, int z, int b) {
  Stack *h = held();
  if (on_block && b == B_DRAGON_EGG && !pl.sneaking) {
    egg_teleport(x, y, z);
    player_swing();
    return true;
  }
  if (on_block && (b == B_BED_FOOT || b == B_BED_HEAD) && dim != DIM_OVERWORLD) {
    /* BlockBed.onBlockActivated: no sleeping here, it blows up (5, with fire) */
    for (int f = 2; f < 6; f++)
      if (world_get(x + NX[f], y, z + NZ[f]) == (b == B_BED_FOOT ? B_BED_HEAD : B_BED_FOOT)) world_set(x + NX[f], y, z + NZ[f], B_AIR);
    world_set(x, y, z, B_AIR);
    dims_explode(x + 0.5f, y + 0.5f, z + 0.5f, 5, true);
    return true;
  }
  if (k == IK_WATER_BUCKET && dim == DIM_NETHER) {
    /* ItemBucket.tryPlaceContainedLiquid: the water boils away (the bucket is emptied) */
    if (on_block) {
      int f = pl.hit_face, px = x + NX[f], py = y + NY[f], pz = z + NZ[f];
      if (replaceable(world_get(px, py, pz))) {
        for (int i = 0; i < 8; i++) particle_add(px + rndf(), py + rndf(), pz + rndf(), 0, 0.05f, 0, 0x8C51, 20);
        if (pl.mode == 0) h->id = I_BUCKET, h->aux = 1;
      }
    }
    return true;
  }
  if (on_block && b >= B_STONE_BUTTON_E && b <= B_STONE_BUTTON_N && !pl.sneaking) {
    button_press(x, y, z, b);
    player_swing();
    return true;
  }
  if (k == IK_PLACER && it_place[h->id - 256] == B_IRON_DOOR_LOWER_E) {
    if (on_block && place_iron_door()) use_up(), player_swing();
    return true;
  }
  switch (k) {
    case IK_DBLOCK: {
      /* ItemBlock: stairs rising the way the player looks, a pillar along the face's axis */
      int pb = it_place[h->id - 256];
      int way = (int)floorf(pl.yaw / 90 + 0.5f) & 3;
      static const int8_t st[4] = {2, 1, 3, 0};
      if (pb == B_NETHER_BRICK_STAIRS_E || pb == B_QUARTZ_STAIRS_E || pb == B_SANDSTONE_STAIRS_E) pb += st[way];
      if (pb == B_STONE_BUTTON_E) {   /* (on a side: 1.8.8's on floors and ceilings are not here) */
        static const uint16_t bt[6] = {0, 0, B_STONE_BUTTON_N, B_STONE_BUTTON_S, B_STONE_BUTTON_W, B_STONE_BUTTON_E};
        if (pl.hit_face < 2 || world_get(x, y, z) == B_AIR) return true;
        pb = bt[pl.hit_face];
      }
      if (pb == B_PILLAR_QUARTZ && pl.hit_face >= 0) {
        int a = pl.hit_face >> 1;
        pb += a == 2 ? 1 : a == 1 ? 2 : 0;
      }
      if (on_block && place(pb)) use_up(), player_swing();
      return true;
    }
    case IK_ENDER_PEARL:
      /* ItemEnderPearl (in Creative, nothing) */
      if (pl.mode == 1) return true;
      throw_item(I_ENDER_PEARL, 1.5f, true);
      use_up();
      player_swing();
      return true;
    case IK_ENDER_EYE: {
      if (on_block && b == B_END_PORTAL_FRAME && pl.mode != 3) {
        world_set(x, y, z, B_END_PORTAL_FRAME_EYE);
        use_up();
        for (int i = 0; i < 16; i++) particle_add(x + rndf(), y + 0.8125f, z + rndf(), 0, 0.05f, 0, 0x4208, 20);
        end_ring(x, y, z);
        return true;
      }
      if (on_block && b == B_END_PORTAL_FRAME_EYE) return true;
      /* thrown towards the nearest stronghold (in the Overworld; nowhere else) */
      int at[3];
      if (dim == DIM_OVERWORLD && gen_stronghold_near(ifloor(pl.x), ifloor(pl.z), at)) {
        throw_eye(at[0], at[1], at[2]);
        use_up();
        player_swing();
      }
      return true;
    }
    case IK_FIRE_CHARGE:
      /* ItemFireball.onItemUse: fire against the face */
      if (on_block) {
        int f = pl.hit_face, fx = x + NX[f], fy = y + NY[f], fz = z + NZ[f];
        if (world_get(fx, fy, fz) == B_AIR && fy >= 0 && fy < WORLD_H) fire_set(fx, fy, fz, 0);
        use_up();
      }
      return true;
  }
  return false;
}
