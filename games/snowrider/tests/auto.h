/* Snow Rider 3D: the autopilot of tests/play.c (and of a timing build, make EXTRA=-DSR_BENCH). */
/* the autopilot: looks ahead along a fan of headings, steers to the clearest, jumps when it must */
static float clear_along(float yaw) {
  float o[3] = {sled.x, sled.py + 0.25f, sled.z}, d[3] = {sinf(yaw * DEG), 0, cosf(yaw * DEG)};
  Hit h;
  float best = 14;
  for (int s = -1; s <= 1; s++) {
    float os[3] = {o[0] + d[2] * 0.4f * s, o[1], o[2] - d[0] * 0.4f * s}, low[3] = {os[0], os[1] - 0.15f, os[2]};
    if (ray_cast(os, d, best, L_OBSTACLE, &h)) best = h.t;
    if (ray_cast(low, d, best, L_OBSTACLE, &h)) best = h.t;
  }
  for (float t = 1.5f; t < best; t += 1.5f) {
    float g[3] = {o[0] + d[0] * t, o[1] + 3, o[2] + d[2] * t}, dn[3] = {0, -1, 0};
    if (!ray_cast(g, dn, 8, L_SURFACE, &h)) return t * 0.8f;   /* a hole */
  }
  return best;
}

static uint32_t autopilot(void) {
  float best = -1, best_yaw = 0;
  for (float a = -60; a <= 60; a += 6) {
    float c = clear_along(a) - fabsf(a) * 0.02f - fabsf(a - sled.hrot) * 0.01f;
    if (c > best) best = c, best_yaw = a;
  }
  uint32_t k = 0;
  if (best_yaw < sled.hrot - 3) k |= SK_LEFT;
  if (best_yaw > sled.hrot + 3) k |= SK_RIGHT;
  if (clear_along(sled.hrot) < 2.5f) k |= SK_UP;
  return k;
}
