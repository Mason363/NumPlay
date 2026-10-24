#include <eadk.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "PolyGT 3D";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;

// ============================================================================
// FAST MATH, TRIGONOMETRY & PRNG
// ============================================================================
#define PI_F 3.14159265f
#define TAU_F 6.28318530f

static inline float clampf(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}
static inline int clampi(int v, int lo, int hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}
static inline float fabsf_fast(float x) { return x < 0.0f ? -x : x; }
static inline float wrap_angle(float x) {
  while (x > PI_F) x -= TAU_F;
  while (x < -PI_F) x += TAU_F;
  return x;
}

static float sinf_fast(float x) {
  x = wrap_angle(x);
  const float B = 4.0f / PI_F;
  const float C = -4.0f / (PI_F * PI_F);
  float y = B * x + C * x * fabsf_fast(x);
  const float P = 0.225f;
  return P * (y * fabsf_fast(y) - y) + y;
}

static float cosf_fast(float x) { return sinf_fast(x + 1.57079632f); }

static float sqrtf_fast(float x) {
  if (x <= 0.0f) return 0.0f;
#if defined(__ARM_FP) && PLATFORM_DEVICE
  float res;
  __asm__("vsqrt.f32 %0, %1" : "=t"(res) : "t"(x));
  return res;
#else
  union {
    float f;
    uint32_t i;
  } u = {x};
  u.i = 0x5f3759df - (u.i >> 1);
  float y = u.f;
  y = y * (1.5f - 0.5f * x * y * y);
  y = y * (1.5f - 0.5f * x * y * y);
  return x * y;
#endif
}

static uint32_t rng_state = 0xCAFEBABEu;
static uint32_t rand_u32(void) {
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 17;
  rng_state ^= rng_state << 5;
  return rng_state;
}
static float rand_range(float lo, float hi) {
  float t = (float)(rand_u32() & 0xFFFF) * (1.0f / 65535.0f);
  return lo + (hi - lo) * t;
}

// ============================================================================
// 3D GRAPHICS ENGINE: TILE-STRIP Z-BUFFER + AFFINE TEXTURES
// ============================================================================
#define SCREEN_W 320
#define SCREEN_H 240
#define STRIP_H 40
#define NUM_STRIPS (SCREEN_H / STRIP_H)
#define MAX_SCREEN_TRIS 720
#define NEAR_Z 1.6f
#define FAR_Z 195.0f

static eadk_color_t fb_strip[SCREEN_W * STRIP_H];
static uint16_t zb_strip[SCREEN_W * STRIP_H];



enum TexType {
  TEX_FLAT = 0,
  TEX_ASPHALT = 1,
  TEX_KERB = 2,
  TEX_FINISH = 3,
  TEX_GRASS = 4,
  TEX_WALL = 5,
  TEX_GHOST = 6
};

typedef struct {
  float x, y, z;
  float u, v;
} Vec3UV;

typedef struct {
  int16_t x0, y0, x1, y1, x2, y2;
  uint16_t z0, z1, z2;
  int16_t u0, v0, u1, v1, u2, v2;
  uint8_t r, g, b;
  uint8_t tex_id;
  int16_t min_y, max_y;
} ScreenTri;

static ScreenTri screen_tris[MAX_SCREEN_TRIS];
static int num_screen_tris = 0;

static int ps1_jitter_shift = 1;

static inline eadk_color_t rgb565(int r, int g, int b) {
  return (eadk_color_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Camera state
static float cam_x = 0.0f, cam_y = 2.8f, cam_z = 0.0f;
static float cam_yaw = 0.0f, cam_pitch = -0.08f, cam_roll = 0.0f;
static float cam_cos_yaw, cam_sin_yaw;
static float cam_cos_pitch, cam_sin_pitch;
static float cam_cos_roll, cam_sin_roll;
static float fov_scale = 205.0f;

static uint8_t fog_r = 180, fog_g = 110, fog_b = 140;

static void update_camera_trig(void) {
  cam_cos_yaw = cosf_fast(cam_yaw);
  cam_sin_yaw = sinf_fast(cam_yaw);
  cam_cos_pitch = cosf_fast(cam_pitch);
  cam_sin_pitch = sinf_fast(cam_pitch);
  cam_cos_roll = cosf_fast(cam_roll);
  cam_sin_roll = sinf_fast(cam_roll);
}

static Vec3UV world_to_cam(Vec3UV w) {
  float dx = w.x - cam_x;
  float dy = w.y - cam_y;
  float dz = w.z - cam_z;

  float x1 = dx * cam_cos_yaw - dz * cam_sin_yaw;
  float z1 = dx * cam_sin_yaw + dz * cam_cos_yaw;
  float y1 = dy;

  float y2 = y1 * cam_cos_pitch - z1 * cam_sin_pitch;
  float z2 = y1 * cam_sin_pitch + z1 * cam_cos_pitch;
  float x2 = x1;

  float x3 = x2 * cam_cos_roll - y2 * cam_sin_roll;
  float y3 = x2 * cam_sin_roll + y2 * cam_cos_roll;

  Vec3UV out = {x3, y3, z2, w.u, w.v};
  return out;
}

static void push_projected_tri(Vec3UV c0, Vec3UV c1, Vec3UV c2,
                               uint8_t r, uint8_t g, uint8_t b, uint8_t tex_id,
                               bool double_sided) {
  if (num_screen_tris >= MAX_SCREEN_TRIS) return;
  if (c0.z > FAR_Z && c1.z > FAR_Z && c2.z > FAR_Z) return;

  float inv0 = fov_scale / c0.z;
  float inv1 = fov_scale / c1.z;
  float inv2 = fov_scale / c2.z;

  int sx0 = (int)(SCREEN_W * 0.5f + c0.x * inv0);
  int sy0 = (int)(SCREEN_H * 0.48f - c0.y * inv0);
  int sx1 = (int)(SCREEN_W * 0.5f + c1.x * inv1);
  int sy1 = (int)(SCREEN_H * 0.48f - c1.y * inv1);
  int sx2 = (int)(SCREEN_W * 0.5f + c2.x * inv2);
  int sy2 = (int)(SCREEN_H * 0.48f - c2.y * inv2);

  if (ps1_jitter_shift > 0) {
    sx0 = (sx0 >> ps1_jitter_shift) << ps1_jitter_shift;
    sy0 = (sy0 >> ps1_jitter_shift) << ps1_jitter_shift;
    sx1 = (sx1 >> ps1_jitter_shift) << ps1_jitter_shift;
    sy1 = (sy1 >> ps1_jitter_shift) << ps1_jitter_shift;
    sx2 = (sx2 >> ps1_jitter_shift) << ps1_jitter_shift;
    sy2 = (sy2 >> ps1_jitter_shift) << ps1_jitter_shift;
  }

  int cross = (sx1 - sx0) * (sy2 - sy0) - (sy1 - sy0) * (sx2 - sx0);
  if (!double_sided && cross <= 0) return;
  if (cross < 0) {
    int tx = sx1; sx1 = sx2; sx2 = tx;
    int ty = sy1; sy1 = sy2; sy2 = ty;
    Vec3UV tc = c1; c1 = c2; c2 = tc;
  }

  int min_x = sx0 < sx1 ? (sx0 < sx2 ? sx0 : sx2) : (sx1 < sx2 ? sx1 : sx2);
  int max_x = sx0 > sx1 ? (sx0 > sx2 ? sx0 : sx2) : (sx1 > sx2 ? sx1 : sx2);
  int min_y = sy0 < sy1 ? (sy0 < sy2 ? sy0 : sy2) : (sy1 < sy2 ? sy1 : sy2);
  int max_y = sy0 > sy1 ? (sy0 > sy2 ? sy0 : sy2) : (sy1 > sy2 ? sy1 : sy2);

  if (max_x < 0 || min_x >= SCREEN_W || max_y < 0 || min_y >= SCREEN_H) return;
  if (min_y == max_y || min_x == max_x) return;

  float avg_z = (c0.z + c1.z + c2.z) * 0.3333f;
  if (avg_z > 55.0f) {
    float fog_t = clampf((avg_z - 55.0f) * (1.0f / (FAR_Z - 55.0f)), 0.0f, 0.88f);
    r = (uint8_t)(r + (fog_r - r) * fog_t);
    g = (uint8_t)(g + (fog_g - g) * fog_t);
    b = (uint8_t)(b + (fog_b - b) * fog_t);
  }

  ScreenTri *t = &screen_tris[num_screen_tris++];
  t->x0 = (int16_t)clampi(sx0, -512, 832);
  t->y0 = (int16_t)clampi(sy0, -512, 752);
  t->x1 = (int16_t)clampi(sx1, -512, 832);
  t->y1 = (int16_t)clampi(sy1, -512, 752);
  t->x2 = (int16_t)clampi(sx2, -512, 832);
  t->y2 = (int16_t)clampi(sy2, -512, 752);

  t->z0 = (uint16_t)clampf(c0.z * 320.0f, 1.0f, 65534.0f);
  t->z1 = (uint16_t)clampf(c1.z * 320.0f, 1.0f, 65534.0f);
  t->z2 = (uint16_t)clampf(c2.z * 320.0f, 1.0f, 65534.0f);

  t->u0 = (int16_t)(c0.u * 256.0f);
  t->v0 = (int16_t)(c0.v * 256.0f);
  t->u1 = (int16_t)(c1.u * 256.0f);
  t->v1 = (int16_t)(c1.v * 256.0f);
  t->u2 = (int16_t)(c2.u * 256.0f);
  t->v2 = (int16_t)(c2.v * 256.0f);

  t->r = r;
  t->g = g;
  t->b = b;
  t->tex_id = tex_id;
  t->min_y = (int16_t)clampi(min_y, 0, SCREEN_H - 1);
  t->max_y = (int16_t)clampi(max_y, 0, SCREEN_H - 1);
}

static inline Vec3UV lerp_v3(Vec3UV a, Vec3UV b, float t) {
  Vec3UV o = {
      a.x + (b.x - a.x) * t,
      a.y + (b.y - a.y) * t,
      a.z + (b.z - a.z) * t,
      a.u + (b.u - a.u) * t,
      a.v + (b.v - a.v) * t,
  };
  return o;
}

static void submit_world_tri(Vec3UV v0, Vec3UV v1, Vec3UV v2,
                             uint8_t r, uint8_t g, uint8_t b,
                             uint8_t tex_id, bool double_sided) {
  Vec3UV c0 = world_to_cam(v0);
  Vec3UV c1 = world_to_cam(v1);
  Vec3UV c2 = world_to_cam(v2);

  Vec3UV in_pts[3];
  Vec3UV out_pts[3];
  int in_n = 0, out_n = 0;

  if (c0.z >= NEAR_Z) in_pts[in_n++] = c0; else out_pts[out_n++] = c0;
  if (c1.z >= NEAR_Z) in_pts[in_n++] = c1; else out_pts[out_n++] = c1;
  if (c2.z >= NEAR_Z) in_pts[in_n++] = c2; else out_pts[out_n++] = c2;

  if (in_n == 0) return;
  if (in_n == 3) {
    push_projected_tri(c0, c1, c2, r, g, b, tex_id, double_sided);
  } else if (in_n == 1) {
    float t0 = (NEAR_Z - in_pts[0].z) / (out_pts[0].z - in_pts[0].z);
    float t1 = (NEAR_Z - in_pts[0].z) / (out_pts[1].z - in_pts[0].z);
    Vec3UV p1 = lerp_v3(in_pts[0], out_pts[0], t0);
    Vec3UV p2 = lerp_v3(in_pts[0], out_pts[1], t1);
    push_projected_tri(in_pts[0], p1, p2, r, g, b, tex_id, double_sided);
  } else if (in_n == 2) {
    float t0 = (NEAR_Z - in_pts[0].z) / (out_pts[0].z - in_pts[0].z);
    float t1 = (NEAR_Z - in_pts[1].z) / (out_pts[0].z - in_pts[1].z);
    Vec3UV p0 = lerp_v3(in_pts[0], out_pts[0], t0);
    Vec3UV p1 = lerp_v3(in_pts[1], out_pts[0], t1);
    push_projected_tri(in_pts[0], in_pts[1], p0, r, g, b, tex_id, double_sided);
    push_projected_tri(in_pts[1], p1, p0, r, g, b, tex_id, double_sided);
  }
}

static void submit_world_quad(Vec3UV v0, Vec3UV v1, Vec3UV v2, Vec3UV v3,
                              uint8_t r, uint8_t g, uint8_t b,
                              uint8_t tex_id, bool double_sided) {
  submit_world_tri(v0, v1, v2, r, g, b, tex_id, double_sided);
  submit_world_tri(v0, v2, v3, r, g, b, tex_id, double_sided);
}

static inline void sample_ps1_texture(uint8_t tex_id, int u8, int v8,
                                      int sx, int sy,
                                      uint8_t base_r, uint8_t base_g, uint8_t base_b,
                                      int *out_r, int *out_g, int *out_b, bool *discard) {
  *discard = false;
  int r = base_r, g = base_g, b = base_b;
  int u = (u8 >> 2) & 63;
  int v = (v8 >> 2) & 63;

  switch (tex_id) {
    case TEX_ASPHALT: {
      int grain = (((u * 17) ^ (v * 31)) & 7) - 4;
      r += grain * 2;
      g += grain * 2;
      b += grain * 2;
      if (u <= 2 || u >= 61) {
        r = 225; g = 225; b = 230;
      } else if (u >= 30 && u <= 33 && (v & 32)) {
        r = 235; g = 205; b = 70;
      }
      break;
    }
    case TEX_KERB: {
      int stripe = (v >> 4) & 1;
      int bevel = (u < 8 || u > 55) ? -25 : 0;
      if (stripe) {
        r = base_r + bevel;
        g = base_g + bevel;
        b = base_b + bevel;
      } else {
        r = 240 + bevel;
        g = 240 + bevel;
        b = 245 + bevel;
      }
      break;
    }
    case TEX_FINISH: {
      int chk = ((u >> 3) ^ (v >> 3)) & 1;
      if (chk) {
        r = 245; g = 245; b = 245;
      } else {
        r = 25; g = 25; b = 30;
      }
      break;
    }
    case TEX_GRASS: {
      int chk = ((u >> 4) ^ (v >> 4)) & 1;
      int noise = (((u * 13) ^ (v * 29)) & 7) - 3;
      if (chk) {
        r += 10 + noise; g += 16 + noise; b += 8 + noise;
      } else {
        r += noise; g += noise; b += noise;
      }
      break;
    }
    case TEX_WALL: {
      if (v < 14 && (((u + v) >> 3) & 1)) {
        r = 240; g = 180; b = 30;
      } else if ((v & 15) == 0 || (u & 31) == 0) {
        r -= 22; g -= 22; b -= 22;
      }
      break;
    }
    case TEX_GHOST: {
      if ((sx ^ sy) & 1) {
        *discard = true;
        return;
      }
      break;
    }
    default:
      break;
  }

  *out_r = clampi(r, 0, 255);
  *out_g = clampi(g, 0, 255);
  *out_b = clampi(b, 0, 255);
}

static void rasterize_tri_strip(const ScreenTri *t, int strip_y0) {
  int strip_y1 = strip_y0 + STRIP_H - 1;
  if (t->max_y < strip_y0 || t->min_y > strip_y1) return;

  int y_start = t->min_y > strip_y0 ? t->min_y : strip_y0;
  int y_end = t->max_y < strip_y1 ? t->max_y : strip_y1;

  int x0 = t->x0, y0 = t->y0;
  int x1 = t->x1, y1 = t->y1;
  int x2 = t->x2, y2 = t->y2;

  int min_x = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
  int max_x = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
  min_x = clampi(min_x, 0, SCREEN_W - 1);
  max_x = clampi(max_x, 0, SCREEN_W - 1);

  int area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
  if (area <= 0) return;

  int32_t inv_area = (1 << 20) / area;

  int A01 = y0 - y1, B01 = x1 - x0;
  int A12 = y1 - y2, B12 = x2 - x1;
  int A20 = y2 - y0, B20 = x0 - x2;

  int w0_row = (min_x - x1) * A12 + (y_start - y1) * B12;
  int w1_row = (min_x - x2) * A20 + (y_start - y2) * B20;
  int w2_row = (min_x - x0) * A01 + (y_start - y0) * B01;

  int dz1 = (int)t->z1 - (int)t->z0;
  int dz2 = (int)t->z2 - (int)t->z0;

  if (t->tex_id == TEX_FLAT) {
    eadk_color_t flat_col = rgb565(t->r, t->g, t->b);
    for (int y = y_start; y <= y_end; y++) {
      int rx0 = min_x, rx1 = max_x;
      if (A12 == 0) {
        if (w0_row < 0) { w0_row += B12; w1_row += B20; w2_row += B01; continue; }
      } else if (A12 > 0) {
        if (w0_row < 0) {
          int val = min_x + (-w0_row + A12 - 1) / A12;
          if (val > rx0) rx0 = val;
        }
      } else {
        if (w0_row < 0) { w0_row += B12; w1_row += B20; w2_row += B01; continue; }
        int val = min_x + w0_row / (-A12);
        if (val < rx1) rx1 = val;
      }

      if (A20 == 0) {
        if (w1_row < 0) { w0_row += B12; w1_row += B20; w2_row += B01; continue; }
      } else if (A20 > 0) {
        if (w1_row < 0) {
          int val = min_x + (-w1_row + A20 - 1) / A20;
          if (val > rx0) rx0 = val;
        }
      } else {
        if (w1_row < 0) { w0_row += B12; w1_row += B20; w2_row += B01; continue; }
        int val = min_x + w1_row / (-A20);
        if (val < rx1) rx1 = val;
      }

      if (A01 == 0) {
        if (w2_row < 0) { w0_row += B12; w1_row += B20; w2_row += B01; continue; }
      } else if (A01 > 0) {
        if (w2_row < 0) {
          int val = min_x + (-w2_row + A01 - 1) / A01;
          if (val > rx0) rx0 = val;
        }
      } else {
        if (w2_row < 0) { w0_row += B12; w1_row += B20; w2_row += B01; continue; }
        int val = min_x + w2_row / (-A01);
        if (val < rx1) rx1 = val;
      }

      if (rx0 <= rx1) {
        int w1 = w1_row + (rx0 - min_x) * A20;
        int w2 = w2_row + (rx0 - min_x) * A01;
        int row_offset = (y - strip_y0) * SCREEN_W;
        for (int x = rx0; x <= rx1; x++) {
          int b1 = (w1 * inv_area) >> 12;
          int b2 = (w2 * inv_area) >> 12;
          uint16_t z = (uint16_t)((int)t->z0 + ((dz1 * b1 + dz2 * b2) >> 8));
          int idx = row_offset + x;
          if (z < zb_strip[idx]) {
            zb_strip[idx] = z;
            fb_strip[idx] = flat_col;
          }
          w1 += A20;
          w2 += A01;
        }
      }

      w0_row += B12;
      w1_row += B20;
      w2_row += B01;
    }
  } else {
    int du1 = (int)t->u1 - (int)t->u0;
    int du2 = (int)t->u2 - (int)t->u0;
    int dv1 = (int)t->v1 - (int)t->v0;
    int dv2 = (int)t->v2 - (int)t->v0;

    for (int y = y_start; y <= y_end; y++) {
      int rx0 = min_x, rx1 = max_x;
      if (A12 == 0) {
        if (w0_row < 0) { w0_row += B12; w1_row += B20; w2_row += B01; continue; }
      } else if (A12 > 0) {
        if (w0_row < 0) {
          int val = min_x + (-w0_row + A12 - 1) / A12;
          if (val > rx0) rx0 = val;
        }
      } else {
        if (w0_row < 0) { w0_row += B12; w1_row += B20; w2_row += B01; continue; }
        int val = min_x + w0_row / (-A12);
        if (val < rx1) rx1 = val;
      }

      if (A20 == 0) {
        if (w1_row < 0) { w0_row += B12; w1_row += B20; w2_row += B01; continue; }
      } else if (A20 > 0) {
        if (w1_row < 0) {
          int val = min_x + (-w1_row + A20 - 1) / A20;
          if (val > rx0) rx0 = val;
        }
      } else {
        if (w1_row < 0) { w0_row += B12; w1_row += B20; w2_row += B01; continue; }
        int val = min_x + w1_row / (-A20);
        if (val < rx1) rx1 = val;
      }

      if (A01 == 0) {
        if (w2_row < 0) { w0_row += B12; w1_row += B20; w2_row += B01; continue; }
      } else if (A01 > 0) {
        if (w2_row < 0) {
          int val = min_x + (-w2_row + A01 - 1) / A01;
          if (val > rx0) rx0 = val;
        }
      } else {
        if (w2_row < 0) { w0_row += B12; w1_row += B20; w2_row += B01; continue; }
        int val = min_x + w2_row / (-A01);
        if (val < rx1) rx1 = val;
      }

      if (rx0 <= rx1) {
        int w1 = w1_row + (rx0 - min_x) * A20;
        int w2 = w2_row + (rx0 - min_x) * A01;
        int row_offset = (y - strip_y0) * SCREEN_W;
        for (int x = rx0; x <= rx1; x++) {
          int b1 = (w1 * inv_area) >> 12;
          int b2 = (w2 * inv_area) >> 12;
          uint16_t z = (uint16_t)((int)t->z0 + ((dz1 * b1 + dz2 * b2) >> 8));
          int idx = row_offset + x;
          if (z < zb_strip[idx]) {
            int u8 = (int)t->u0 + ((du1 * b1 + du2 * b2) >> 8);
            int v8 = (int)t->v0 + ((dv1 * b1 + dv2 * b2) >> 8);
            int cr, cg, cb;
            bool discard = false;
            sample_ps1_texture(t->tex_id, u8, v8, x, y, t->r, t->g, t->b, &cr, &cg, &cb, &discard);
            if (!discard) {
              zb_strip[idx] = z;
              fb_strip[idx] = rgb565(cr, cg, cb);
            }
          }
          w1 += A20;
          w2 += A01;
        }
      }

      w0_row += B12;
      w1_row += B20;
      w2_row += B01;
    }
  }
}

// ============================================================================
// KNOT-FREE CLOSED-LOOP POLAR SPLINE TRACK GENERATOR
// ============================================================================
#define TRACK_N 180
#define ROAD_HALF_W 10.0f

typedef struct {
  float x, y, z;
  float tx, tz;      // Forward unit tangent
  float nx, nz;      // Right unit normal
  float seg_len;     // Exact Euclidean distance to next node
  float cum_dist;    // Cumulative distance from start line
  float curv;        // Signed angle change at this node
  uint8_t prop_type; // 0=none, 1=tree/lamp, 2=arch, 3=gantry, 4=chevron_sign
  uint8_t biome_id;  // 0..4 terrain biome index (morphs continuously in Infinity Mode!)
  int8_t prop_side;
  int8_t orb_slot;   // 0=none, 1=active orb, >1=respawn cooldown ticks
  float orb_offset;  // Lateral road offset of the boost orb (-6.5m .. +6.5m)
} TrackNode;

typedef struct {
  const char *name;
  const char *subtitle;
  uint8_t sky_top_r, sky_top_g, sky_top_b;
  uint8_t sky_bot_r, sky_bot_g, sky_bot_b;
  uint8_t ground_r, ground_g, ground_b;
  uint8_t road_r, road_g, road_b;
  uint8_t kerb_r, kerb_g, kerb_b;
  float grip_mult;
} TrackTheme;

static const TrackTheme themes[3] = {
    {
        "RIDGE VALLEY GT",
        "Sunset High-Speed Circuit",
        35, 25, 85,
        235, 110, 80,
        35, 85, 42,
        62, 64, 72,
        220, 35, 35,
        1.00f
    },
    {
        "NEO-TOKYO WANGAN",
        "Midnight Bay Expressway",
        10, 12, 32,
        55, 25, 85,
        22, 24, 34,
        46, 50, 62,
        0, 195, 235,
        1.04f
    },
    {
        "MT. AKAGI TOUGE",
        "Dusk Mountain Drift Pass",
        45, 35, 60,
        165, 130, 145,
        48, 62, 42,
        58, 56, 60,
        230, 165, 20,
        0.92f
    }
};

#define NUM_INF_BIOMES 5
#define INF_BIOME_SEGS 45
#define INF_SEG_LEN 9.5f

static const TrackTheme infinity_biomes[NUM_INF_BIOMES] = {
    {
        "ZONE: ALPINE VALLEY",
        "Rolling Forest Highway",
        25, 60, 140,
        130, 190, 245,
        34, 98, 44,
        62, 65, 72,
        225, 40, 40,
        1.00f
    },
    {
        "ZONE: SUNSET CANYON",
        "Red-Gold Desert Dunes",
        55, 20, 75,
        245, 115, 50,
        180, 98, 46,
        74, 62, 56,
        240, 185, 25,
        0.98f
    },
    {
        "ZONE: NEON WANGAN",
        "Midnight Cyber Expressway",
        10, 12, 34,
        70, 25, 105,
        20, 26, 42,
        44, 48, 60,
        0, 215, 255,
        1.04f
    },
    {
        "ZONE: GLACIER PASS",
        "Icy Alpine Mountain Peaks",
        38, 82, 150,
        185, 225, 250,
        198, 215, 232,
        68, 76, 92,
        35, 115, 240,
        0.93f
    },
    {
        "ZONE: VOLCANIC ASH",
        "Molten Dusk Caldera",
        36, 16, 38,
        215, 65, 35,
        44, 36, 38,
        48, 44, 46,
        255, 95, 15,
        0.96f
    }
};

static TrackNode track_nodes[TRACK_N];
static float track_total_length = 1200.0f;
static int current_track_id = 0;
static uint32_t current_track_seed = 101;
static float map_cx = 0.0f, map_cz = 0.0f, map_scale = 0.14f;

// Streaming Endless Procedural Highway state for Infinity Mode
static uint32_t inf_gen_count = 0;
static int inf_head_seg = 0;
static float inf_yaw = 0.0f;
static float inf_curv = 0.0f;
static float inf_target_curv = 0.0f;
static int inf_curve_hold = 0;
static float inf_pitch_slope = 0.0f;
static float inf_target_slope = 0.0f;
static int inf_slope_hold = 0;
static uint8_t inf_current_biome = 0;

// Generates one new procedural road segment at the horizon frontier in Infinity Mode!
// Continuously varies road curvature, elevation hills/valleys, props, and terrain biomes.
static void append_infinity_segment(void) {
  int cur_idx = inf_head_seg;
  int next_idx = (cur_idx + 1) % TRACK_N;
  TrackNode *cn = &track_nodes[cur_idx];
  TrackNode *nn = &track_nodes[next_idx];

  // Switch to a new distinct terrain biome every INF_BIOME_SEGS segments (~425m)!
  if (inf_gen_count > 0 && (inf_gen_count % INF_BIOME_SEGS) == 0) {
    uint8_t step = 1 + (uint8_t)(rand_u32() % (NUM_INF_BIOMES - 1));
    inf_current_biome = (inf_current_biome + step) % NUM_INF_BIOMES;
  }
  cn->biome_id = inf_current_biome;

  // Update procedural horizontal curvature (sweeping corners, S-bends, hairpins, and straights)
  if (inf_curve_hold <= 0) {
    uint32_t r = rand_u32() & 7;
    if (r < 2) {
      inf_target_curv = 0.0f; // High-speed straightaway
      inf_curve_hold = 10 + (int)(rand_u32() % 14);
    } else {
      // Biome-specific corner sharpness, strictly bounded so road never folds or inverts!
      float max_c = (inf_current_biome == 3 || inf_current_biome == 4) ? 0.046f : 0.038f;
      float mag = rand_range(0.016f, max_c);
      inf_target_curv = ((rand_u32() & 1) ? mag : -mag);
      inf_curve_hold = 12 + (int)(rand_u32() % 18);
    }
  }
  inf_curve_hold--;
  inf_curv += (inf_target_curv - inf_curv) * 0.18f;
  inf_curv = clampf(inf_curv, -0.046f, 0.046f);

  // Update procedural vertical terrain elevation (hills, canyon dips, mountain climbs!)
  if (inf_slope_hold <= 0) {
    // Glacier Pass & Volcanic Ash have steeper mountain climbs/descents; Neon Wangan is flatter
    float max_sl = (inf_current_biome == 3) ? 0.95f
                   : ((inf_current_biome == 4 || inf_current_biome == 1) ? 0.78f
                   : ((inf_current_biome == 2) ? 0.28f : 0.55f));
    inf_target_slope = rand_range(-max_sl, max_sl);
    inf_slope_hold = 10 + (int)(rand_u32() % 16);
  }
  inf_slope_hold--;
  inf_pitch_slope += (inf_target_slope - inf_pitch_slope) * 0.16f;

  // Advance heading and compute unit tangent & right normal
  inf_yaw = wrap_angle(inf_yaw + inf_curv);
  float tx = sinf_fast(inf_yaw);
  float tz = cosf_fast(inf_yaw);
  cn->tx = tx;
  cn->tz = tz;
  cn->nx = tz;
  cn->nz = -tx;
  cn->seg_len = INF_SEG_LEN;
  cn->curv = inf_curv;

  // Place next frontier vertex in 3D space
  nn->x = cn->x + tx * INF_SEG_LEN;
  nn->y = cn->y + inf_pitch_slope;
  nn->z = cn->z + tz * INF_SEG_LEN;
  nn->cum_dist = cn->cum_dist + INF_SEG_LEN;
  nn->tx = tx;
  nn->tz = tz;
  nn->nx = tz;
  nn->nz = -tx;
  nn->seg_len = INF_SEG_LEN;
  nn->curv = inf_curv;
  nn->biome_id = inf_current_biome;
  nn->prop_type = 0;
  nn->orb_slot = 0;

  // Roadside props tailored to the terrain & biome transitions
  cn->prop_type = 0;
  cn->prop_side = (inf_gen_count & 1) ? 1 : -1;
  if ((inf_gen_count % INF_BIOME_SEGS) == 0 && inf_gen_count > 0) {
    cn->prop_type = 2; // Biome Transition Gate Arch!
  } else if (inf_current_biome == 2 && (inf_gen_count % 3) == 0) {
    cn->prop_type = 3; // Cyber Highway Overhead Light Gantry Pillars
  } else if (fabsf_fast(cn->curv) > 0.028f && (inf_gen_count % 2) == 0) {
    cn->prop_type = 4; // Turn Chevron Sign on outside of curve
    cn->prop_side = (cn->curv > 0.0f) ? -1 : 1;
  } else if ((inf_gen_count % 3) == 0) {
    cn->prop_type = 1; // Biome Tree / Desert Pillar / Snow Pine / Streetlamp
  }

  // Spawn glowing 3D Boost Orbs along the endless road (every 12 segments)
  if (inf_gen_count >= 6 && (inf_gen_count % 12) == 6) {
    cn->orb_slot = 1;
    if (fabsf_fast(cn->curv) > 0.022f) {
      cn->orb_offset = (cn->curv > 0.0f) ? 4.0f : -4.0f;
    } else {
      cn->orb_offset = rand_range(-5.0f, 5.0f);
    }
  } else {
    cn->orb_slot = 0;
    cn->orb_offset = 0.0f;
  }

  inf_head_seg = next_idx;
  inf_gen_count++;
}

static void update_infinity_minimap_bounds(int center_seg) {
  float min_x = 1e9f, max_x = -1e9f, min_z = 1e9f, max_z = -1e9f;
  for (int s = -8; s <= 36; s++) {
    int idx = (center_seg + s + TRACK_N) % TRACK_N;
    float x = track_nodes[idx].x;
    float z = track_nodes[idx].z;
    if (x < min_x) min_x = x;
    if (x > max_x) max_x = x;
    if (z < min_z) min_z = z;
    if (z > max_z) max_z = z;
  }
  map_cx = (min_x + max_x) * 0.5f;
  map_cz = (min_z + max_z) * 0.5f;
  float span = (max_x - min_x) > (max_z - min_z) ? (max_x - min_x) : (max_z - min_z);
  map_scale = (span > 1.0f) ? (42.0f / span) : 0.14f;
}

static void init_infinity_road(uint32_t seed) {
  rng_state = seed ^ 0xA5A5F00Du;
  for (int k = 0; k < 4; k++) rand_u32();

  inf_gen_count = 0;
  inf_head_seg = 0;
  inf_yaw = 0.0f;
  inf_curv = 0.0f;
  inf_target_curv = 0.0f;
  inf_curve_hold = 8;
  inf_pitch_slope = 0.0f;
  inf_target_slope = 0.0f;
  inf_slope_hold = 6;
  inf_current_biome = (uint8_t)(current_track_id % NUM_INF_BIOMES);

  track_nodes[0].x = 0.0f;
  track_nodes[0].y = 0.0f;
  track_nodes[0].z = 0.0f;
  track_nodes[0].cum_dist = 0.0f;
  track_nodes[0].biome_id = inf_current_biome;

  // Pre-generate 60 segments ahead so the horizon is completely filled
  for (int i = 0; i < 60; i++) {
    append_infinity_segment();
  }
  update_infinity_minimap_bounds(2);
}

// Generates a 100% non-self-intersecting closed loop in polar coordinates,
// then smooths and resamples it so there are ZERO knots or upside-down flips!
static void build_track_seeded(int track_id, uint32_t seed) {
  current_track_id = track_id;
  current_track_seed = seed;
  rng_state = seed ^ 0x9E3779B9u ^ ((uint32_t)track_id * 0x45D9F3Bu);
  for (int k = 0; k < 5; k++) rand_u32();

  const TrackTheme *th = &themes[track_id];
  fog_r = th->sky_bot_r;
  fog_g = th->sky_bot_g;
  fog_b = th->sky_bot_b;

  // Randomized Fourier harmonics for radius R(theta): strictly positive (R > 110m)
  // so the loop can NEVER cross itself or pinch into a knot!
  float base_r = 185.0f;
  float a2 = rand_range(-38.0f, 38.0f);
  float p2 = rand_range(0.0f, TAU_F);
  float a3 = rand_range(-28.0f, 28.0f);
  float p3 = rand_range(0.0f, TAU_F);
  float a4 = rand_range(-16.0f, 16.0f);
  float p4 = rand_range(0.0f, TAU_F);
  float aspect_x = rand_range(0.82f, 1.22f);
  float aspect_z = rand_range(0.82f, 1.22f);

  float h_amp1 = rand_range(3.5f, 6.5f);
  float h_amp2 = rand_range(2.0f, 4.5f);
  float h_phase = rand_range(0.0f, TAU_F);

  for (int i = 0; i < TRACK_N; i++) {
    float theta = (float)i / (float)TRACK_N * TAU_F;
    float r = base_r +
              a2 * cosf_fast(2.0f * theta + p2) +
              a3 * sinf_fast(3.0f * theta + p3) +
              a4 * cosf_fast(4.0f * theta + p4);
    // Flatten section around theta == 0 into a clean start/finish straightaway
    track_nodes[i].x = -r * cosf_fast(theta) * aspect_x;
    track_nodes[i].z =  r * sinf_fast(theta) * aspect_z;
    track_nodes[i].y =  h_amp1 * sinf_fast(theta + h_phase) + h_amp2 * cosf_fast(2.0f * theta);
  }

  // Smooth the closed polygon (4-pass Laplacian filter) to guarantee large minimum turn radius
  // so inner kerbs and grass shoulders NEVER overlap or invert!
  for (int pass = 0; pass < 4; pass++) {
    float prev_x = track_nodes[TRACK_N - 1].x;
    float prev_y = track_nodes[TRACK_N - 1].y;
    float prev_z = track_nodes[TRACK_N - 1].z;
    float first_x = track_nodes[0].x;
    float first_y = track_nodes[0].y;
    float first_z = track_nodes[0].z;
    for (int i = 0; i < TRACK_N; i++) {
      int nxt = (i + 1) % TRACK_N;
      float nx_x = (nxt == 0) ? first_x : track_nodes[nxt].x;
      float nx_y = (nxt == 0) ? first_y : track_nodes[nxt].y;
      float nx_z = (nxt == 0) ? first_z : track_nodes[nxt].z;
      float cur_x = track_nodes[i].x;
      float cur_y = track_nodes[i].y;
      float cur_z = track_nodes[i].z;
      track_nodes[i].x = 0.25f * prev_x + 0.50f * cur_x + 0.25f * nx_x;
      track_nodes[i].y = 0.25f * prev_y + 0.50f * cur_y + 0.25f * nx_y;
      track_nodes[i].z = 0.25f * prev_z + 0.50f * cur_z + 0.25f * nx_z;
      prev_x = cur_x;
      prev_y = cur_y;
      prev_z = cur_z;
    }
  }

  // Shift origin so node 0 is at (0, 0, 0) and compute mini-map bounds
  float ox = track_nodes[0].x, oy = track_nodes[0].y, oz = track_nodes[0].z;
  float min_x = 1e9f, max_x = -1e9f, min_z = 1e9f, max_z = -1e9f;
  for (int i = 0; i < TRACK_N; i++) {
    track_nodes[i].x -= ox;
    track_nodes[i].y -= oy;
    track_nodes[i].z -= oz;
    if (track_nodes[i].x < min_x) min_x = track_nodes[i].x;
    if (track_nodes[i].x > max_x) max_x = track_nodes[i].x;
    if (track_nodes[i].z < min_z) min_z = track_nodes[i].z;
    if (track_nodes[i].z > max_z) max_z = track_nodes[i].z;
  }
  map_cx = (min_x + max_x) * 0.5f;
  map_cz = (min_z + max_z) * 0.5f;
  float span = (max_x - min_x) > (max_z - min_z) ? (max_x - min_x) : (max_z - min_z);
  map_scale = (span > 1.0f) ? (42.0f / span) : 0.14f;

  // Compute exact tangents, normals, segment lengths, cumulative distances, and props
  float cum = 0.0f;
  int gantry_start = 50 + (int)(rand_u32() % 65);
  for (int i = 0; i < TRACK_N; i++) {
    int next = (i + 1) % TRACK_N;
    float dx = track_nodes[next].x - track_nodes[i].x;
    float dz = track_nodes[next].z - track_nodes[i].z;
    float len = sqrtf_fast(dx * dx + dz * dz);
    if (len < 0.1f) len = 0.1f;

    track_nodes[i].tx = dx / len;
    track_nodes[i].tz = dz / len;
    track_nodes[i].nx = track_nodes[i].tz;
    track_nodes[i].nz = -track_nodes[i].tx;
    track_nodes[i].seg_len = len;
    track_nodes[i].cum_dist = cum;
    track_nodes[i].biome_id = (uint8_t)track_id;
    cum += len;
  }
  track_total_length = cum;

  for (int i = 0; i < TRACK_N; i++) {
    int next = (i + 1) % TRACK_N;
    track_nodes[i].curv = track_nodes[next].tx * track_nodes[i].tz -
                          track_nodes[next].tz * track_nodes[i].tx;
    track_nodes[i].prop_type = 0;
    track_nodes[i].prop_side = (i & 1) ? 1 : -1;

    if (i == 0 || i == TRACK_N / 3 || i == (2 * TRACK_N) / 3) {
      track_nodes[i].prop_type = 2; // Start/Finish or Sector Arch
    } else if (i >= gantry_start && i <= gantry_start + 12 && (i % 2 == 0)) {
      track_nodes[i].prop_type = 3; // Overhead Highway Light Gantry Pillars (no solid roof slab!)
    } else if (fabsf_fast(track_nodes[i].curv) > 0.045f && (i % 2 == 0)) {
      track_nodes[i].prop_type = 4; // Turn Chevron Sign on outside of curve
      track_nodes[i].prop_side = (track_nodes[i].curv > 0.0f) ? -1 : 1;
    } else if ((i % 4) == 0) {
      track_nodes[i].prop_type = 1; // Tree or Streetlamp
    }

    // Spawn rarer glowing 3D Boost Orbs on the road (every 15 segments)
    if (i >= 8 && (i % 15) == 8) {
      track_nodes[i].orb_slot = 1; // Boost Orb
      if (fabsf_fast(track_nodes[i].curv) > 0.025f) {
        track_nodes[i].orb_offset = (track_nodes[i].curv > 0.0f) ? 4.2f : -4.2f;
      } else {
        track_nodes[i].orb_offset = rand_range(-5.0f, 5.0f);
      }
    } else {
      track_nodes[i].orb_slot = 0;
      track_nodes[i].orb_offset = 0.0f;
    }
  }
}

// ============================================================================
// CAR SPECS & LOW-POLY 3D CAR MESH RENDERER
// ============================================================================
typedef struct {
  const char *name;
  const char *drivetrain;
  uint8_t body_r, body_g, body_b;
  uint8_t accent_r, accent_g, accent_b;
  float max_speed;
  float accel_force;
  float grip_lat;
  float drift_ease;
  int redline_rpm;
} CarSpec;

#define NUM_CARS 5

static const CarSpec car_specs[NUM_CARS] = {
    {
        "RX-79 APEX GT",
        "RWD / TWIN-TURBO",
        225, 32, 42,
        245, 190, 40,
        79.0f, 38.0f, 1.00f, 1.15f, 8500
    },
    {
        "NSX-R PROTO",
        "MR / NA VTEC",
        240, 240, 245,
        220, 30, 35,
        82.0f, 36.5f, 1.16f, 0.90f, 9000
    },
    {
        "EVO-IV RALLY",
        "AWD / TURBO",
        35, 105, 230,
        245, 205, 45,
        76.5f, 43.0f, 1.10f, 1.05f, 7800
    },
    {
        "BLACK STALLION",
        "4-HOOF / GALLOP",
        24, 24, 32,
        255, 45, 35,
        84.5f, 44.0f, 1.18f, 1.08f, 9200
    },
    {
        "BROWN MUSTANG",
        "4-HOOF / GALLOP",
        148, 84, 42,
        225, 175, 55,
        82.5f, 42.5f, 1.20f, 1.05f, 8800
    }
};

static const uint8_t npc_colors[8][3] = {
    {245, 195,  25},
    { 30, 205,  95},
    {175,  55, 235},
    {245, 110,  25},
    { 50, 195, 240},
    {215, 215, 225},
    {235,  45, 135},
    { 40,  85, 220}
};

typedef struct {
  float x, y, z;
  float cos_y, sin_y;
  float roll, pitch;
  float steer_angle;
} CarPose;

static Vec3UV car_local_to_world(const CarPose *p, float lx, float ly, float lz, float u, float v) {
  float rx = lx - ly * p->roll;
  float ry = ly + lx * p->roll + lz * p->pitch;
  float rz = lz - ly * p->pitch;

  float wx = p->x + rx * p->cos_y + rz * p->sin_y;
  float wy = p->y + ry;
  float wz = p->z - rx * p->sin_y + rz * p->cos_y;

  Vec3UV out = {wx, wy, wz, u, v};
  return out;
}

static uint32_t frame_counter;
static float player_speed;

static void draw_3d_horse(const CarPose *pose, bool is_brown, bool braking, bool shift_flame, bool is_ghost) {
  uint8_t tex = is_ghost ? TEX_GHOST : TEX_FLAT;

  // Coat colors: Obsidian Black Stallion vs Classic Warm Chestnut/Bay Brown Mustang
  uint8_t c_top_r = is_ghost ? 70  : (is_brown ? 168 : 38);
  uint8_t c_top_g = is_ghost ? 220 : (is_brown ? 98  : 38);
  uint8_t c_top_b = is_ghost ? 250 : (is_brown ? 50  : 48);

  uint8_t c_side_r = is_ghost ? 55  : (is_brown ? 140 : 24);
  uint8_t c_side_g = is_ghost ? 185 : (is_brown ? 78  : 24);
  uint8_t c_side_b = is_ghost ? 225 : (is_brown ? 38  : 32);

  uint8_t c_dark_r = is_ghost ? 40  : (is_brown ? 96  : 14);
  uint8_t c_dark_g = is_ghost ? 140 : (is_brown ? 52  : 14);
  uint8_t c_dark_b = is_ghost ? 180 : (is_brown ? 24  : 20);

  // Dark espresso/black mane & tail for both horses
  uint8_t c_mane_r = is_ghost ? 90  : (is_brown ? 36  : 10);
  uint8_t c_mane_g = is_ghost ? 240 : (is_brown ? 22  : 10);
  uint8_t c_mane_b = is_ghost ? 255 : (is_brown ? 14  : 14);

  // Gallop animation phase
  float phase = (float)frame_counter * 0.42f + (pose->x + pose->z) * 0.18f;
  float bob = 0.06f * fabsf_fast(sinf_fast(phase));
  float head_turn = clampf(pose->steer_angle * 0.45f, -0.45f, 0.45f);

  if (!is_ghost) {
    Vec3UV s0 = car_local_to_world(pose, -0.60f, 0.04f, -1.85f, 0, 0);
    Vec3UV s1 = car_local_to_world(pose,  0.60f, 0.04f, -1.85f, 1, 0);
    Vec3UV s2 = car_local_to_world(pose,  0.60f, 0.04f,  1.85f, 1, 1);
    Vec3UV s3 = car_local_to_world(pose, -0.60f, 0.04f,  1.85f, 0, 1);
    submit_world_quad(s0, s1, s2, s3, 14, 14, 20, TEX_FLAT, true);
  }

  // Sleek, narrow equine torso (Barrel, Haunches & Chest)
  float by0 = 0.86f + bob;
  float by1 = 1.44f + bob;
  Vec3UV r_bl = car_local_to_world(pose, -0.42f, by0, -1.38f, 0, 0);
  Vec3UV r_br = car_local_to_world(pose,  0.42f, by0, -1.38f, 1, 0);
  Vec3UV r_tl = car_local_to_world(pose, -0.38f, by1, -1.28f, 0, 1);
  Vec3UV r_tr = car_local_to_world(pose,  0.38f, by1, -1.28f, 1, 1);

  Vec3UV m_bl = car_local_to_world(pose, -0.46f, by0 - 0.04f, 0.05f, 0, 0);
  Vec3UV m_br = car_local_to_world(pose,  0.46f, by0 - 0.04f, 0.05f, 1, 0);
  Vec3UV m_tl = car_local_to_world(pose, -0.40f, by1 + 0.02f, 0.08f, 0, 1);
  Vec3UV m_tr = car_local_to_world(pose,  0.40f, by1 + 0.02f, 0.08f, 1, 1);

  Vec3UV f_bl = car_local_to_world(pose, -0.42f, by0,  1.25f, 0, 0);
  Vec3UV f_br = car_local_to_world(pose,  0.42f, by0,  1.25f, 1, 0);
  Vec3UV f_tl = car_local_to_world(pose, -0.36f, by1 + 0.08f, 1.18f, 0, 1);
  Vec3UV f_tr = car_local_to_world(pose,  0.36f, by1 + 0.08f, 1.18f, 1, 1);

  // Back, flanks, chest, and rump
  submit_world_quad(r_bl, r_br, r_tr, r_tl, c_dark_r, c_dark_g, c_dark_b, tex, true);
  submit_world_quad(r_tl, r_tr, m_tr, m_tl, c_top_r, c_top_g, c_top_b, tex, true);
  submit_world_quad(m_tl, m_tr, f_tr, f_tl, c_top_r, c_top_g, c_top_b, tex, true);
  submit_world_quad(f_tl, f_tr, f_br, f_bl, c_side_r, c_side_g, c_side_b, tex, true);

  submit_world_quad(r_bl, m_bl, m_tl, r_tl, c_side_r, c_side_g, c_side_b, tex, true);
  submit_world_quad(m_bl, f_bl, f_tl, m_tl, c_side_r, c_side_g, c_side_b, tex, true);
  submit_world_quad(m_br, r_br, r_tr, m_tr, c_side_r, c_side_g, c_side_b, tex, true);
  submit_world_quad(f_br, m_br, m_tr, f_tr, c_side_r, c_side_g, c_side_b, tex, true);

  // Saddle on the horse's back
  if (!is_ghost) {
    uint8_t sr = is_brown ? 75 : 185;
    uint8_t sg = is_brown ? 42 : 28;
    uint8_t sb = is_brown ? 20 : 35;
    Vec3UV sd_rl = car_local_to_world(pose, -0.42f, by1 + 0.04f, -0.48f, 0, 0);
    Vec3UV sd_rr = car_local_to_world(pose,  0.42f, by1 + 0.04f, -0.48f, 1, 0);
    Vec3UV sd_fl = car_local_to_world(pose, -0.42f, by1 + 0.05f,  0.42f, 0, 1);
    Vec3UV sd_fr = car_local_to_world(pose,  0.42f, by1 + 0.05f,  0.42f, 1, 1);
    submit_world_quad(sd_rl, sd_rr, sd_fr, sd_fl, sr, sg, sb, TEX_FLAT, true);
  }

  // Arched Neck & Head (steers with player input!)
  float nx_top = head_turn * 0.32f;
  Vec3UV n_bl = car_local_to_world(pose, -0.22f, by1 - 0.06f, 0.88f, 0, 0);
  Vec3UV n_br = car_local_to_world(pose,  0.22f, by1 - 0.06f, 0.88f, 1, 0);
  Vec3UV n_fl = car_local_to_world(pose, -0.20f, by1 - 0.18f, 1.34f, 0, 0);
  Vec3UV n_fr = car_local_to_world(pose,  0.20f, by1 - 0.18f, 1.34f, 1, 0);

  Vec3UV h_bl = car_local_to_world(pose, nx_top - 0.18f, by1 + 0.76f, 1.38f, 0, 1);
  Vec3UV h_br = car_local_to_world(pose, nx_top + 0.18f, by1 + 0.76f, 1.38f, 1, 1);
  Vec3UV h_fl = car_local_to_world(pose, nx_top - 0.16f, by1 + 0.52f, 1.72f, 0, 1);
  Vec3UV h_fr = car_local_to_world(pose, nx_top + 0.16f, by1 + 0.52f, 1.72f, 1, 1);

  submit_world_quad(n_bl, n_br, h_br, h_bl, c_top_r, c_top_g, c_top_b, tex, true);
  submit_world_quad(n_fl, n_bl, h_bl, h_fl, c_side_r, c_side_g, c_side_b, tex, true);
  submit_world_quad(n_br, n_fr, h_fr, h_br, c_side_r, c_side_g, c_side_b, tex, true);
  submit_world_quad(n_fl, n_fr, h_fr, h_fl, c_dark_r, c_dark_g, c_dark_b, tex, true);

  // Flowing Mane Ridge along the neck
  Vec3UV mn_b = car_local_to_world(pose, 0.0f, by1 + 0.06f, 0.65f, 0, 0);
  Vec3UV mn_m = car_local_to_world(pose, nx_top * 0.5f, by1 + 0.52f, 0.95f, 1, 0);
  Vec3UV mn_t = car_local_to_world(pose, nx_top, by1 + 0.86f, 1.35f, 0, 1);
  submit_world_tri(mn_b, mn_m, mn_t, c_mane_r, c_mane_g, c_mane_b, tex, true);

  // Sculpted Snout / Muzzle
  float sx_tip = head_turn * 0.52f;
  Vec3UV sn_tl = car_local_to_world(pose, sx_tip - 0.12f, by1 + 0.45f, 2.18f, 0, 0);
  Vec3UV sn_tr = car_local_to_world(pose, sx_tip + 0.12f, by1 + 0.45f, 2.18f, 1, 0);
  Vec3UV sn_bl = car_local_to_world(pose, sx_tip - 0.11f, by1 + 0.25f, 2.10f, 0, 1);
  Vec3UV sn_br = car_local_to_world(pose, sx_tip + 0.11f, by1 + 0.25f, 2.10f, 1, 1);

  // White blaze on forehead/snout for the Brown Mustang!
  uint8_t st_r = (is_brown && !is_ghost) ? 235 : c_top_r;
  uint8_t st_g = (is_brown && !is_ghost) ? 230 : c_top_g;
  uint8_t st_b = (is_brown && !is_ghost) ? 220 : c_top_b;
  submit_world_quad(h_bl, h_br, sn_tr, sn_tl, st_r, st_g, st_b, tex, true);
  submit_world_quad(h_fl, h_bl, sn_tl, sn_bl, c_side_r, c_side_g, c_side_b, tex, true);
  submit_world_quad(h_br, h_fr, sn_br, sn_tr, c_side_r, c_side_g, c_side_b, tex, true);
  submit_world_quad(sn_tl, sn_tr, sn_br, sn_bl, c_dark_r, c_dark_g, c_dark_b, tex, true);

  // Pointed Ears & Eyes
  Vec3UV el0 = car_local_to_world(pose, nx_top - 0.18f, by1 + 0.74f, 1.40f, 0, 0);
  Vec3UV el1 = car_local_to_world(pose, nx_top - 0.08f, by1 + 0.74f, 1.46f, 1, 0);
  Vec3UV el2 = car_local_to_world(pose, nx_top - 0.14f, by1 + 1.02f, 1.36f, 0, 1);
  submit_world_tri(el0, el1, el2, c_side_r, c_side_g, c_side_b, tex, true);

  Vec3UV er0 = car_local_to_world(pose, nx_top + 0.08f, by1 + 0.74f, 1.46f, 0, 0);
  Vec3UV er1 = car_local_to_world(pose, nx_top + 0.18f, by1 + 0.74f, 1.40f, 1, 0);
  Vec3UV er2 = car_local_to_world(pose, nx_top + 0.14f, by1 + 1.02f, 1.36f, 0, 1);
  submit_world_tri(er0, er1, er2, c_side_r, c_side_g, c_side_b, tex, true);

  if (!is_ghost) {
    uint8_t ey_r = is_brown ? 245 : 255;
    uint8_t ey_g = is_brown ? 195 : 40;
    uint8_t ey_b = is_brown ? 60  : 30;
    Vec3UV eyl0 = car_local_to_world(pose, nx_top - 0.19f, by1 + 0.64f, 1.58f, 0, 0);
    Vec3UV eyl1 = car_local_to_world(pose, nx_top - 0.19f, by1 + 0.70f, 1.72f, 1, 0);
    Vec3UV eyl2 = car_local_to_world(pose, nx_top - 0.17f, by1 + 0.59f, 1.74f, 0, 1);
    submit_world_tri(eyl0, eyl1, eyl2, ey_r, ey_g, ey_b, TEX_FLAT, true);

    Vec3UV eyr0 = car_local_to_world(pose, nx_top + 0.19f, by1 + 0.64f, 1.58f, 0, 0);
    Vec3UV eyr1 = car_local_to_world(pose, nx_top + 0.19f, by1 + 0.70f, 1.72f, 1, 0);
    Vec3UV eyr2 = car_local_to_world(pose, nx_top + 0.17f, by1 + 0.59f, 1.74f, 0, 1);
    submit_world_tri(eyr0, eyr1, eyr2, ey_r, ey_g, ey_b, TEX_FLAT, true);
  }

  // Flowing Tail (billows dynamically in the wind!)
  float tail_sway = sinf_fast(phase * 1.3f) * 0.20f - head_turn * 0.25f;
  float tail_lift = shift_flame ? 0.32f : 0.0f;
  Vec3UV t_top_l = car_local_to_world(pose, -0.14f, by1 - 0.06f, -1.34f, 0, 0);
  Vec3UV t_top_r = car_local_to_world(pose,  0.14f, by1 - 0.06f, -1.34f, 1, 0);
  Vec3UV t_mid_l = car_local_to_world(pose, tail_sway - 0.22f, 0.85f + tail_lift, -1.85f, 0, 1);
  Vec3UV t_mid_r = car_local_to_world(pose, tail_sway + 0.22f, 0.85f + tail_lift, -1.85f, 1, 1);
  Vec3UV t_tip   = car_local_to_world(pose, tail_sway * 1.3f,  0.32f + tail_lift, -2.20f, 0, 1);
  submit_world_quad(t_top_l, t_top_r, t_mid_r, t_mid_l, c_mane_r, c_mane_g, c_mane_b, tex, true);
  submit_world_tri(t_mid_l, t_mid_r, t_tip, c_mane_r, c_mane_g, c_mane_b, tex, true);

  // 4 Animated Galloping Slender Legs + Hooves
  const float lx[4] = {-0.32f,  0.32f, -0.30f,  0.30f};
  const float lz[4] = {-1.10f, -1.10f,  1.02f,  1.02f};
  const float l_phase[4] = {0.0f, 2.7f, 1.4f, 4.1f};

  for (int l = 0; l < 4; l++) {
    float p_ang = phase + l_phase[l];
    float swing = sinf_fast(p_ang) * 0.52f;
    float lift  = clampf(cosf_fast(p_ang) * 0.28f, 0.0f, 0.30f);

    float top_x = lx[l];
    float top_z = lz[l];
    float bot_z = lz[l] + swing;
    float bot_y = 0.04f + lift;

    Vec3UV l_tl = car_local_to_world(pose, top_x - 0.09f, by0 + 0.08f, top_z - 0.10f, 0, 0);
    Vec3UV l_tr = car_local_to_world(pose, top_x + 0.09f, by0 + 0.08f, top_z + 0.10f, 1, 0);
    Vec3UV l_br = car_local_to_world(pose, top_x + 0.07f, bot_y,       bot_z + 0.08f, 1, 1);
    Vec3UV l_bl = car_local_to_world(pose, top_x - 0.07f, bot_y,       bot_z - 0.08f, 0, 1);
    submit_world_quad(l_tl, l_tr, l_br, l_bl, c_dark_r, c_dark_g, c_dark_b, tex, true);

    // Side thickness of leg + hoof
    Vec3UV ls_t0 = car_local_to_world(pose, top_x, by0 + 0.08f, top_z - 0.12f, 0, 0);
    Vec3UV ls_t1 = car_local_to_world(pose, top_x, by0 + 0.08f, top_z + 0.12f, 1, 0);
    Vec3UV ls_b1 = car_local_to_world(pose, top_x, bot_y,       bot_z + 0.10f, 1, 1);
    Vec3UV ls_b0 = car_local_to_world(pose, top_x, bot_y,       bot_z - 0.10f, 0, 1);
    submit_world_quad(ls_t0, ls_t1, ls_b1, ls_b0, c_side_r, c_side_g, c_side_b, tex, true);

    if (!is_ghost) {
      uint8_t hr = braking ? 255 : (shift_flame ? 40  : (is_brown ? 225 : 195));
      uint8_t hg = braking ? 55  : (shift_flame ? 235 : (is_brown ? 215 : 165));
      uint8_t hb = braking ? 35  : (shift_flame ? 255 : (is_brown ? 195 : 90));
      Vec3UV hf0 = car_local_to_world(pose, top_x - 0.10f, bot_y,        bot_z - 0.11f, 0, 0);
      Vec3UV hf1 = car_local_to_world(pose, top_x + 0.10f, bot_y,        bot_z - 0.11f, 1, 0);
      Vec3UV hf2 = car_local_to_world(pose, top_x + 0.10f, bot_y + 0.16f, bot_z + 0.11f, 1, 1);
      Vec3UV hf3 = car_local_to_world(pose, top_x - 0.10f, bot_y + 0.16f, bot_z + 0.11f, 0, 1);
      submit_world_quad(hf0, hf1, hf2, hf3, hr, hg, hb, TEX_FLAT, true);
    }
  }
}

static void draw_3d_car_colored(const CarPose *pose, int car_id,
                                uint8_t br, uint8_t bg, uint8_t bb,
                                bool braking, bool shift_flame, bool is_ghost) {
  int cid = car_id % NUM_CARS;
  if (cid == 3 || cid == 4) {
    draw_3d_horse(pose, cid == 4, braking, shift_flame, is_ghost);
    return;
  }
  const CarSpec *cs = &car_specs[cid];
  uint8_t tex = is_ghost ? TEX_GHOST : TEX_FLAT;

  if (is_ghost) {
    br = 70; bg = 220; bb = 250;
  }

  uint8_t dr = (br * 3) >> 2;
  uint8_t dg = (bg * 3) >> 2;
  uint8_t db = (bb * 3) >> 2;

  uint8_t sr = (br * 5) >> 3;
  uint8_t sg = (bg * 5) >> 3;
  uint8_t sb = (bb * 5) >> 3;

  if (!is_ghost) {
    Vec3UV s0 = car_local_to_world(pose, -1.28f, 0.04f, -2.35f, 0, 0);
    Vec3UV s1 = car_local_to_world(pose,  1.28f, 0.04f, -2.35f, 1, 0);
    Vec3UV s2 = car_local_to_world(pose,  1.28f, 0.04f,  2.35f, 1, 1);
    Vec3UV s3 = car_local_to_world(pose, -1.28f, 0.04f,  2.35f, 0, 1);
    submit_world_quad(s0, s1, s2, s3, 18, 18, 24, TEX_FLAT, true);
  }

  Vec3UV r_bot_l = car_local_to_world(pose, -1.15f, 0.28f, -2.20f, 0, 0);
  Vec3UV r_bot_r = car_local_to_world(pose,  1.15f, 0.28f, -2.20f, 1, 0);
  Vec3UV r_top_l = car_local_to_world(pose, -1.08f, 0.82f, -2.12f, 0, 1);
  Vec3UV r_top_r = car_local_to_world(pose,  1.08f, 0.82f, -2.12f, 1, 1);

  Vec3UV m_bot_l = car_local_to_world(pose, -1.20f, 0.24f,  0.20f, 0, 0);
  Vec3UV m_bot_r = car_local_to_world(pose,  1.20f, 0.24f,  0.20f, 1, 0);
  Vec3UV m_top_l = car_local_to_world(pose, -1.12f, 0.80f,  0.55f, 0, 1);
  Vec3UV m_top_r = car_local_to_world(pose,  1.12f, 0.80f,  0.55f, 1, 1);

  Vec3UV f_bot_l = car_local_to_world(pose, -1.10f, 0.24f,  2.25f, 0, 0);
  Vec3UV f_bot_r = car_local_to_world(pose,  1.10f, 0.24f,  2.25f, 1, 0);
  Vec3UV f_top_l = car_local_to_world(pose, -1.02f, 0.64f,  2.15f, 0, 1);
  Vec3UV f_top_r = car_local_to_world(pose,  1.02f, 0.64f,  2.15f, 1, 1);

  Vec3UV d_rear_l = car_local_to_world(pose, -1.04f, 0.84f, -1.30f, 0, 0);
  Vec3UV d_rear_r = car_local_to_world(pose,  1.04f, 0.84f, -1.30f, 1, 0);

  float roof_h = ((car_id % NUM_CARS) == 1) ? 1.18f : (((car_id % NUM_CARS) == 2) ? 1.32f : 1.24f);
  Vec3UV c_rear_l = car_local_to_world(pose, -0.82f, roof_h, -0.95f, 0, 0);
  Vec3UV c_rear_r = car_local_to_world(pose,  0.82f, roof_h, -0.95f, 1, 0);
  Vec3UV c_frnt_l = car_local_to_world(pose, -0.78f, roof_h - 0.04f, 0.15f, 0, 1);
  Vec3UV c_frnt_r = car_local_to_world(pose,  0.78f, roof_h - 0.04f, 0.15f, 1, 1);

  submit_world_quad(r_bot_l, r_bot_r, r_top_r, r_top_l, dr, dg, db, tex, true);
  submit_world_quad(r_top_l, r_top_r, d_rear_r, d_rear_l, br, bg, bb, tex, true);

  uint8_t gr = is_ghost ? 60 : 32;
  uint8_t gg = is_ghost ? 180 : 48;
  uint8_t gb = is_ghost ? 220 : 75;
  submit_world_quad(d_rear_l, d_rear_r, c_rear_r, c_rear_l, gr, gg, gb, tex, true);

  uint8_t rfr = ((car_id % NUM_CARS) == 1 && !is_ghost) ? 30 : br;
  uint8_t rfg = ((car_id % NUM_CARS) == 1 && !is_ghost) ? 32 : bg;
  uint8_t rfb = ((car_id % NUM_CARS) == 1 && !is_ghost) ? 40 : bb;
  submit_world_quad(c_rear_l, c_rear_r, c_frnt_r, c_frnt_l, rfr, rfg, rfb, tex, true);

  submit_world_quad(c_frnt_l, c_frnt_r, m_top_r, m_top_l, gr + 10, gg + 15, gb + 20, tex, true);
  submit_world_quad(m_top_l, m_top_r, f_top_r, f_top_l, br, bg, bb, tex, true);
  submit_world_quad(f_top_l, f_top_r, f_bot_r, f_bot_l, dr, dg, db, tex, true);

  submit_world_quad(r_bot_l, m_bot_l, m_top_l, r_top_l, sr, sg, sb, tex, true);
  submit_world_quad(m_bot_l, f_bot_l, f_top_l, m_top_l, sr, sg, sb, tex, true);
  submit_world_quad(d_rear_l, m_top_l, c_frnt_l, c_rear_l, gr, gg, gb, tex, true);

  submit_world_quad(m_bot_r, r_bot_r, r_top_r, m_top_r, sr, sg, sb, tex, true);
  submit_world_quad(f_bot_r, m_bot_r, m_top_r, f_top_r, sr, sg, sb, tex, true);
  submit_world_quad(m_top_r, d_rear_r, c_rear_r, c_frnt_r, gr, gg, gb, tex, true);

  Vec3UV w_l0 = car_local_to_world(pose, -1.14f, 1.08f, -2.08f, 0, 0);
  Vec3UV w_r0 = car_local_to_world(pose,  1.14f, 1.08f, -2.08f, 1, 0);
  Vec3UV w_r1 = car_local_to_world(pose,  1.14f, 1.02f, -1.78f, 1, 1);
  Vec3UV w_l1 = car_local_to_world(pose, -1.14f, 1.02f, -1.78f, 0, 1);
  submit_world_quad(w_l0, w_r0, w_r1, w_l1, is_ghost ? 90 : 35, is_ghost ? 230 : 35, is_ghost ? 255 : 42, tex, true);

  if (!is_ghost) {
    uint8_t tl_r = braking ? 255 : 185;
    uint8_t tl_g = braking ? 55 : 15;
    uint8_t tl_b = braking ? 40 : 20;

    Vec3UV tl_l0 = car_local_to_world(pose, -1.02f, 0.56f, -2.23f, 0, 0);
    Vec3UV tl_l1 = car_local_to_world(pose, -0.42f, 0.56f, -2.23f, 1, 0);
    Vec3UV tl_l2 = car_local_to_world(pose, -0.42f, 0.74f, -2.18f, 1, 1);
    Vec3UV tl_l3 = car_local_to_world(pose, -1.02f, 0.74f, -2.18f, 0, 1);
    submit_world_quad(tl_l0, tl_l1, tl_l2, tl_l3, tl_r, tl_g, tl_b, TEX_FLAT, true);

    Vec3UV tl_r0 = car_local_to_world(pose,  0.42f, 0.56f, -2.23f, 0, 0);
    Vec3UV tl_r1 = car_local_to_world(pose,  1.02f, 0.56f, -2.23f, 1, 0);
    Vec3UV tl_r2 = car_local_to_world(pose,  1.02f, 0.74f, -2.18f, 1, 1);
    Vec3UV tl_r3 = car_local_to_world(pose,  0.42f, 0.74f, -2.18f, 0, 1);
    submit_world_quad(tl_r0, tl_r1, tl_r2, tl_r3, tl_r, tl_g, tl_b, TEX_FLAT, true);

    Vec3UV pl0 = car_local_to_world(pose, -0.24f, 0.36f, -2.23f, 0, 0);
    Vec3UV pl1 = car_local_to_world(pose,  0.24f, 0.36f, -2.23f, 1, 0);
    Vec3UV pl2 = car_local_to_world(pose,  0.24f, 0.54f, -2.21f, 1, 1);
    Vec3UV pl3 = car_local_to_world(pose, -0.24f, 0.54f, -2.21f, 0, 1);
    submit_world_quad(pl0, pl1, pl2, pl3, cs->accent_r, cs->accent_g, cs->accent_b, TEX_FLAT, true);

    if (shift_flame) {
      Vec3UV ex0 = car_local_to_world(pose, -0.68f, 0.28f, -2.24f, 0, 0);
      Vec3UV ex1 = car_local_to_world(pose, -0.44f, 0.28f, -2.24f, 1, 0);
      Vec3UV ex2 = car_local_to_world(pose, -0.56f, 0.34f, -2.95f, 0, 1);
      submit_world_tri(ex0, ex1, ex2, 255, 195, 40, TEX_FLAT, true);
    }

    const float wx[4] = {-1.18f, 1.18f, -1.16f, 1.16f};
    const float wz[4] = {-1.38f, -1.38f, 1.38f, 1.38f};
    for (int w = 0; w < 4; w++) {
      float x_in = wx[w] > 0 ? wx[w] - 0.22f : wx[w] + 0.22f;
      float x_out = wx[w];
      Vec3UV w0 = car_local_to_world(pose, x_in,  0.05f, wz[w] - 0.36f, 0, 0);
      Vec3UV w1 = car_local_to_world(pose, x_out, 0.05f, wz[w] - 0.36f, 1, 0);
      Vec3UV w2 = car_local_to_world(pose, x_out, 0.58f, wz[w] - 0.36f, 1, 1);
      Vec3UV w3 = car_local_to_world(pose, x_in,  0.58f, wz[w] - 0.36f, 0, 1);
      submit_world_quad(w0, w1, w2, w3, 24, 24, 28, TEX_FLAT, true);

      Vec3UV s0 = car_local_to_world(pose, x_out, 0.08f, wz[w] - 0.34f, 0, 0);
      Vec3UV s1 = car_local_to_world(pose, x_out, 0.08f, wz[w] + 0.34f, 1, 0);
      Vec3UV s2 = car_local_to_world(pose, x_out, 0.56f, wz[w] + 0.34f, 1, 1);
      Vec3UV s3 = car_local_to_world(pose, x_out, 0.56f, wz[w] - 0.34f, 0, 1);
      submit_world_quad(s0, s1, s2, s3, cs->accent_r >> 1, cs->accent_g >> 1, cs->accent_b >> 1, TEX_FLAT, true);
    }
  }
}

static void draw_3d_car(const CarPose *pose, int car_id, bool braking, bool shift_flame, bool is_ghost) {
  const CarSpec *cs = &car_specs[car_id % NUM_CARS];
  draw_3d_car_colored(pose, car_id, cs->body_r, cs->body_g, cs->body_b, braking, shift_flame, is_ghost);
}

// ============================================================================
// TRUE 3D WORLD-SPACE CAR PHYSICS (ZERO AUTO-STEER / ZERO STATIC PULL!)
// ============================================================================
#define GHOST_SAMPLES 600
#define MAX_NPCS 7

typedef struct {
  int16_t wx_dm;
  int16_t wy_dm;
  int16_t wz_dm;
  int16_t yaw_mrad;
  uint8_t seg_idx;
} GhostFrame;

typedef struct {
  int seg_idx;
  float seg_frac;
  int lap;
  float total_dist;
  float offset;
  float target_offset;
  float speed;
  float max_speed;
  float wx, wy, wz;
  float sin_y, cos_y;
  float boost_fuel;
  uint8_t car_model;
  uint8_t color_idx;
  bool braking;
  bool boosting;
} NpcCar;

static GhostFrame best_ghost[GHOST_SAMPLES];
static GhostFrame curr_ghost[GHOST_SAMPLES];
static bool has_best_ghost[3] = {false, false, false};
static uint32_t best_lap_ms[3] = {0, 0, 0};
static uint32_t best_sector_ms[3][3] = {{0}};

static bool infinity_mode = false;
static int race_total_laps = 3;
static int race_npc_count = 5;
static int saved_race_laps = 3;
static int saved_npc_count = 5;
static NpcCar npcs[MAX_NPCS];
static int player_position = 1;
static int final_finish_position = 1;
static bool race_finished = false;
static uint32_t total_race_ms = 0;
static uint32_t race_start_ms = 0;
static bool slipstreaming = false;
static float player_boost_fuel = 30.0f;
static bool player_boosting = false;
static int orb_pickup_flash = 0;

// Player TRUE 3D World-Space state
static int selected_car_id = 0;
static float player_wx = 0.0f, player_wy = 0.0f, player_wz = 0.0f;
static float player_cos_yaw = 1.0f, player_sin_yaw = 0.0f;
static int player_seg = 0;
static int player_laps_done = 0;
static float player_offset = 0.0f;
static float player_dist = 0.0f;
static float player_total_dist = 0.0f;
static float player_speed = 0.0f;
static float player_drift = 0.0f;
static float player_steer = 0.0f;
static float player_yaw_rate = 0.0f;
static float body_roll = 0.0f;
static float body_pitch = 0.0f;

static int current_gear = 1;
static bool manual_trans = false;
static int engine_rpm = 1200;
static int shift_flame_timer = 0;
static int cam_mode = 0;

static int current_lap = 1;
static int current_sector = 0;
static uint32_t lap_start_ms = 0;
static uint32_t last_lap_ms = 0;
static int32_t split_delta_ms = 0;
static int split_popup_timer = 0;
static uint32_t frame_counter = 0;

static const float gear_ratios[6] = {3.45f, 2.35f, 1.72f, 1.34f, 1.08f, 0.88f};
static const float gear_top_spd[6] = {22.0f, 35.0f, 48.0f, 60.0f, 71.0f, 84.0f};

static void update_npc_world_pos(NpcCar *ai) {
  const TrackNode *n0 = &track_nodes[ai->seg_idx];
  const TrackNode *n1 = &track_nodes[(ai->seg_idx + 1) % TRACK_N];
  float bx = n0->x + (n1->x - n0->x) * ai->seg_frac;
  float by = n0->y + (n1->y - n0->y) * ai->seg_frac;
  float bz = n0->z + (n1->z - n0->z) * ai->seg_frac;
  ai->wx = bx + n0->nx * ai->offset;
  ai->wy = by;
  ai->wz = bz + n0->nz * ai->offset;
  ai->sin_y = n0->tx;
  ai->cos_y = n0->tz;
}

static void reset_race_state(uint32_t now_ms) {
  if (infinity_mode) {
    race_npc_count = 0;
    init_infinity_road(current_track_seed);
    player_seg = 2;
  } else {
    if (inf_gen_count > 0) {
      build_track_seeded(current_track_id, current_track_seed);
      inf_gen_count = 0;
    }
    player_seg = 1;
  }
  player_laps_done = 0;
  player_offset = (race_npc_count > 0) ? -3.0f : 0.0f;
  const TrackNode *n = &track_nodes[player_seg];
  player_wx = n->x + n->nx * player_offset;
  player_wy = n->y;
  player_wz = n->z + n->nz * player_offset;
  // Point player car straight along the starting grid tangent
  player_sin_yaw = n->tx;
  player_cos_yaw = n->tz;

  player_dist = n->cum_dist;
  player_total_dist = 0.0f;
  player_speed = 0.0f;
  player_drift = 0.0f;
  player_steer = 0.0f;
  player_yaw_rate = 0.0f;
  cam_sin_yaw = player_sin_yaw;
  cam_cos_yaw = player_cos_yaw;
  body_roll = 0.0f;
  body_pitch = 0.0f;
  current_gear = 1;
  engine_rpm = 1200;
  shift_flame_timer = 0;
  current_lap = 1;
  current_sector = 0;
  lap_start_ms = now_ms;
  race_start_ms = now_ms;
  last_lap_ms = 0;
  total_race_ms = 0;
  split_delta_ms = 0;
  split_popup_timer = 0;
  race_finished = false;
  slipstreaming = false;
  player_boost_fuel = infinity_mode ? 100.0f : 30.0f;
  player_boosting = false;
  orb_pickup_flash = 0;

  for (int i = 0; i < TRACK_N; i++) {
    if (track_nodes[i].orb_slot > 1) track_nodes[i].orb_slot = 1;
  }

  for (int i = 0; i < race_npc_count; i++) {
    npcs[i].seg_idx = 3 + i * 2;
    npcs[i].seg_frac = 0.0f;
    npcs[i].lap = 0;
    npcs[i].offset = (i & 1) ? 3.0f : -3.0f;
    npcs[i].target_offset = npcs[i].offset;
    npcs[i].speed = 0.0f;
    npcs[i].max_speed = 60.0f + (float)i * 2.4f;
    npcs[i].boost_fuel = 25.0f;
    npcs[i].car_model = (uint8_t)((i + 1) % 3);
    npcs[i].color_idx = (uint8_t)(i % 8);
    npcs[i].braking = false;
    npcs[i].boosting = false;
    npcs[i].total_dist = track_nodes[npcs[i].seg_idx].cum_dist;
    update_npc_world_pos(&npcs[i]);
  }
  player_position = race_npc_count + 1;
  final_finish_position = player_position;
}

static void step_npcs_and_collisions(float dt) {
  slipstreaming = false;

  // Tick Boost Orb respawn timers
  if ((frame_counter & 3) == 0) {
    for (int i = 0; i < TRACK_N; i++) {
      if (track_nodes[i].orb_slot > 1) {
        track_nodes[i].orb_slot--;
      }
    }
  }

  for (int i = 0; i < race_npc_count; i++) {
    NpcCar *ai = &npcs[i];
    int s0 = ai->seg_idx;
    int s_look = (s0 + 4) % TRACK_N;
    float curv_ahead = track_nodes[s_look].curv;
    float corner_sev = fabsf_fast(track_nodes[s0].curv) + fabsf_fast(curv_ahead) * 1.4f;

    // Check if this NPC is the current race leader (1st place)
    bool ai_is_leader = (ai->total_dist > player_total_dist);
    for (int j = 0; j < race_npc_count && ai_is_leader; j++) {
      if (j != i && npcs[j].total_dist > ai->total_dist) ai_is_leader = false;
    }

    // NPC Boost activation: fire nitro on straights when fuel >= 32%, or sustain until depleted!
    if (!ai->boosting) {
      if (ai->boost_fuel >= 32.0f && corner_sev < 0.045f && ai->speed > 38.0f) {
        ai->boosting = true;
      }
    } else {
      if (ai->boost_fuel <= 0.0f || corner_sev > 0.085f) {
        ai->boosting = false;
      }
    }

    float boost_top_bonus = 0.0f;
    float boost_accel_bonus = 0.0f;
    if (ai->boosting && ai->boost_fuel > 0.0f) {
      // Leader burns boost 1.75x faster too!
      float burn_rate = ai_is_leader ? 44.0f : 25.0f;
      ai->boost_fuel = clampf(ai->boost_fuel - burn_rate * dt, 0.0f, 100.0f);
      boost_top_bonus = 10.5f;
      boost_accel_bonus = 22.0f;
    }

    float target_spd = (ai->max_speed + boost_top_bonus) *
                       clampf(1.06f - corner_sev * 2.8f, 0.50f, 1.0f);

    ai->braking = (!ai->boosting && ai->speed > target_spd + 3.5f);
    if (ai->braking) {
      ai->speed -= 35.0f * dt;
    } else if (ai->speed < target_spd) {
      ai->speed += (28.0f + boost_accel_bonus) * dt;
    }

    float gap = ai->total_dist - player_total_dist;
    if (gap > 150.0f) ai->speed *= (1.0f - 0.12f * dt);
    else if (gap < -120.0f) ai->speed += 8.0f * dt;
    ai->speed = clampf(ai->speed, 12.0f, ai->max_speed + boost_top_bonus + 4.0f);

    // Balanced AI Steering & Partial Boost Orb Seeking:
    // Bots check 1..5 segments ahead for an active Boost Orb (~50% chance if within 5.2m laterally).
    bool seeking_orb = false;
    if (ai->boost_fuel < 85.0f) {
      for (int look = 1; look <= 5; look++) {
        int so = (s0 + look) % TRACK_N;
        if (track_nodes[so].orb_slot == 1) {
          float orb_off = track_nodes[so].orb_offset;
          float lat_diff = fabsf_fast(orb_off - ai->offset);
          int decision_hash = (so * 7 + i * 13 + ai->lap * 5) & 7;
          if (lat_diff < 5.2f && decision_hash < 4) {
            ai->target_offset = ai->offset * 0.25f + orb_off * 0.75f;
            seeking_orb = true;
          }
          break;
        }
      }
    }

    if (!seeking_orb) {
      if (fabsf_fast(curv_ahead) > 0.03f) {
        ai->target_offset = (curv_ahead > 0.0f) ? 4.5f : -4.5f;
      } else if (((frame_counter + i * 19) & 63) == 0) {
        ai->target_offset = rand_range(-5.0f, 5.0f);
      }
    }

    ai->offset += clampf((ai->target_offset - ai->offset) * 1.9f, -6.2f, 6.2f) * dt;
    ai->offset = clampf(ai->offset, -(ROAD_HALF_W - 1.5f), ROAD_HALF_W - 1.5f);

    float move_d = ai->speed * dt;
    while (move_d > 0.0f) {
      float seg_l = track_nodes[ai->seg_idx].seg_len;
      float rem = (1.0f - ai->seg_frac) * seg_l;
      if (move_d < rem) {
        ai->seg_frac += move_d / seg_l;
        move_d = 0.0f;
      } else {
        move_d -= rem;
        ai->seg_frac = 0.0f;
        ai->seg_idx++;
        if (ai->seg_idx >= TRACK_N) {
          ai->seg_idx = 0;
          ai->lap++;
        }
        // Check if NPC collected a Boost Orb on the segment it just entered
        if (track_nodes[ai->seg_idx].orb_slot == 1 &&
            fabsf_fast(ai->offset - track_nodes[ai->seg_idx].orb_offset) < 2.5f) {
          track_nodes[ai->seg_idx].orb_slot = 55; // Longer respawn cooldown (rarer!)
          ai->boost_fuel = clampf(ai->boost_fuel + (ai_is_leader ? 18.0f : 30.0f), 0.0f, 100.0f);
        }
      }
    }
    ai->total_dist = (float)ai->lap * track_total_length +
                     track_nodes[ai->seg_idx].cum_dist +
                     ai->seg_frac * track_nodes[ai->seg_idx].seg_len;
    update_npc_world_pos(ai);

    // True 3D World-Space Collision & Slipstream Check with Player!
    float dx = ai->wx - player_wx;
    float dz = ai->wz - player_wz;
    float dist_sq = dx * dx + dz * dz;

    // Project relative vector onto player's forward & right axes
    float rel_fwd = dx * player_sin_yaw + dz * player_cos_yaw;
    float rel_right = dx * player_cos_yaw - dz * player_sin_yaw;

    if (rel_fwd > 4.5f && rel_fwd < 22.0f && fabsf_fast(rel_right) < 1.9f && player_speed > 30.0f) {
      slipstreaming = true;
    }

    if (dist_sq < 14.5f && fabsf_fast(rel_fwd) < 4.2f && fabsf_fast(rel_right) < 2.2f) {
      if (fabsf_fast(rel_fwd) > fabsf_fast(rel_right) * 1.4f) {
        if (rel_fwd > 0.0f && player_speed > ai->speed) {
          float avg = (player_speed + ai->speed) * 0.5f;
          player_speed = avg * 0.90f;
          ai->speed = avg * 1.08f;
          player_wx -= player_sin_yaw * 0.4f;
          player_wz -= player_cos_yaw * 0.4f;
          body_pitch = -0.06f;
        }
      } else {
        float push = (rel_right >= 0.0f) ? -0.55f : 0.55f;
        player_wx += player_cos_yaw * push;
        player_wz -= player_sin_yaw * push;
        ai->offset -= push;
        body_roll = push * 0.22f;
      }
    }
  }

  int pos = 1;
  for (int i = 0; i < race_npc_count; i++) {
    if (npcs[i].total_dist > player_total_dist) pos++;
  }
  player_position = pos;
}

static void step_physics(float dt, bool key_L, bool key_R, bool key_gas,
                         bool key_brake, bool key_ebrake, bool key_boost,
                         bool shift_up_pressed, bool shift_dn_pressed,
                         uint32_t now_ms) {
  if (race_finished) return;
  const CarSpec *cs = &car_specs[selected_car_id];
  const TrackTheme *th = infinity_mode
                             ? &infinity_biomes[track_nodes[player_seg].biome_id % NUM_INF_BIOMES]
                             : &themes[current_track_id];

  // 1. Direct Player Steering Input (fast & snappy response)
  if (key_L) {
    player_steer = clampf(player_steer - 5.5f * dt, -1.0f, 1.0f);
  } else if (key_R) {
    player_steer = clampf(player_steer + 5.5f * dt, -1.0f, 1.0f);
  } else {
    player_steer *= 0.72f;
    if (fabsf_fast(player_steer) < 0.02f) player_steer = 0.0f;
  }

  // 2. Gearbox, RPM & Position-Scaled Boost Burn Simulation
  // If you are in 1st place in front of everyone, boost burns 1.85x faster so you can't hoard & runaway!
  player_boosting = (key_boost && player_boost_fuel > 0.5f);
  if (player_boosting) {
    float burn_rate = (player_position == 1 && race_npc_count > 0) ? 45.0f : 24.0f;
    player_boost_fuel = clampf(player_boost_fuel - burn_rate * dt, 0.0f, 100.0f);
    shift_flame_timer = 2;
  }
  if (orb_pickup_flash > 0) orb_pickup_flash--;

  float boost_top_bonus = player_boosting ? 13.5f : 0.0f;
  float gear_max = gear_top_spd[current_gear - 1] * ((cs->max_speed + boost_top_bonus) / 80.0f);
  float rpm_norm = player_speed / gear_max;
  engine_rpm = 1250 + (int)(rpm_norm * (cs->redline_rpm - 1250));

  if (!manual_trans) {
    if (engine_rpm > cs->redline_rpm - 250 && current_gear < 6) {
      current_gear++;
      shift_flame_timer = 5;
    } else if (engine_rpm < 3600 && current_gear > 1 && player_speed < gear_top_spd[current_gear - 2] * 0.85f) {
      current_gear--;
    }
  } else {
    if (shift_up_pressed && current_gear < 6) {
      current_gear++;
      shift_flame_timer = 6;
    }
    if (shift_dn_pressed && current_gear > 1) {
      current_gear--;
      shift_flame_timer = 5;
    }
  }

  if (engine_rpm > cs->redline_rpm + 200) {
    engine_rpm = cs->redline_rpm - 150;
    shift_flame_timer = 3;
  }
  if (shift_flame_timer > 0) shift_flame_timer--;

  // 3. Longitudinal Forces
  float torque_curve = 0.72f + 0.35f * sinf_fast(clampf(rpm_norm, 0.1f, 1.05f) * 2.4f);
  float gear_pull = gear_ratios[current_gear - 1] * 0.48f;
  float accel = 0.0f;

  if ((key_gas || player_boosting) && player_speed < gear_max * 1.05f) {
    accel += cs->accel_force * torque_curve * clampf(gear_pull, 0.52f, 1.35f);
    if (slipstreaming) accel += 10.5f;
    if (player_boosting) accel += 32.0f;
  }
  if (key_brake) accel -= 62.0f;
  if (key_ebrake) accel -= 28.0f;

  float drag = 0.0065f * player_speed * player_speed + 2.1f;
  float drift_scrub = fabsf_fast(player_drift) * player_speed * 0.30f;

  float abs_off = fabsf_fast(player_offset);
  bool on_kerb = (abs_off > ROAD_HALF_W - 1.1f && abs_off < ROAD_HALF_W + 1.2f);
  bool off_road = (abs_off >= ROAD_HALF_W + 1.2f);
  if (off_road) {
    drag += player_speed * (selected_car_id == 2 ? 0.55f : 1.10f) + 8.0f;
  }

  player_speed += (accel - drag - drift_scrub) * dt;
  player_speed = clampf(player_speed, 0.0f,
                        cs->max_speed + boost_top_bonus + (slipstreaming ? 5.0f : 0.0f));

  // 4. Rotate Player World Heading ONLY by Player Steering (ZERO phantom auto-steer!)
  float grip = cs->grip_lat * th->grip_mult * (off_road ? 0.72f : 1.0f);
  float steer_auth = clampf(player_speed / 12.0f, 0.0f, 1.0f);
  player_yaw_rate = player_steer * 1.38f * grip * steer_auth;
  float d_yaw = player_yaw_rate * dt;

  if (key_ebrake && player_speed > 18.0f) {
    player_drift += player_steer * 2.6f * cs->drift_ease * dt;
  } else if (fabsf_fast(player_steer) > 0.7f && player_speed > 50.0f && key_gas) {
    player_drift += player_steer * 0.85f * cs->drift_ease * dt;
  } else {
    player_drift *= (1.0f - clampf(4.5f * grip * dt, 0.0f, 0.9f));
  }
  if (fabsf_fast(player_drift) < 0.005f) player_drift = 0.0f;
  player_drift = clampf(player_drift, -0.65f, 0.65f);

  // Rotate unit heading vector (player_sin_yaw, player_cos_yaw) by d_yaw
  if (d_yaw != 0.0f) {
    float c = cosf_fast(d_yaw), s = sinf_fast(d_yaw);
    float ns = player_sin_yaw * c + player_cos_yaw * s;
    float nc = player_cos_yaw * c - player_sin_yaw * s;
    float inv_l = 1.0f / sqrtf_fast(ns * ns + nc * nc);
    player_sin_yaw = ns * inv_l;
    player_cos_yaw = nc * inv_l;
  }

  // Advance car in 3D World Space along its true world heading!
  player_wx += player_sin_yaw * player_speed * dt;
  player_wz += player_cos_yaw * player_speed * dt;

  // 5. Locate closest track segment to (player_wx, player_wz) to get road height, offset, and lap progress
  int best_s = player_seg;
  float best_d2 = 1e18f;
  for (int ds = -6; ds <= 10; ds++) {
    int s = (player_seg + ds + TRACK_N) % TRACK_N;
    float dx = player_wx - track_nodes[s].x;
    float dz = player_wz - track_nodes[s].z;
    float d2 = dx * dx + dz * dz;
    if (d2 < best_d2) {
      best_d2 = d2;
      best_s = s;
    }
  }

  int prev_seg = player_seg;
  player_seg = best_s;

  // In Infinity Mode, continuously generate new procedural road & changing biomes ahead of the horizon!
  if (infinity_mode) {
    while (((inf_head_seg - player_seg + TRACK_N) % TRACK_N) < 48) {
      append_infinity_segment();
    }
    if (fabsf_fast(player_wx) > 2000.0f || fabsf_fast(player_wz) > 2000.0f) {
      float sx = player_wx, sz = player_wz;
      for (int i = 0; i < TRACK_N; i++) {
        track_nodes[i].x -= sx;
        track_nodes[i].z -= sz;
      }
      player_wx = 0.0f;
      player_wz = 0.0f;
      cam_x -= sx;
      cam_z -= sz;
    }
    if (player_seg != prev_seg) {
      update_infinity_minimap_bounds(player_seg);
    }
  }

  const TrackNode *n0 = &track_nodes[player_seg];
  const TrackNode *n1 = &track_nodes[(player_seg + 1) % TRACK_N];

  float rel_x = player_wx - n0->x;
  float rel_z = player_wz - n0->z;
  float along = rel_x * n0->tx + rel_z * n0->tz;
  float seg_frac = clampf(along / n0->seg_len, 0.0f, 1.0f);
  player_offset = rel_x * n0->nx + rel_z * n0->nz;
  player_wy = n0->y + (n1->y - n0->y) * seg_frac;

  // Player Boost Orb Pickup Check (check current and next segment in 3D world space!)
  for (int os = 0; os <= 1; os++) {
    int check_s = (player_seg + os) % TRACK_N;
    if (track_nodes[check_s].orb_slot == 1) {
      const TrackNode *on = &track_nodes[check_s];
      float ox = on->x + on->nx * on->orb_offset;
      float oz = on->z + on->nz * on->orb_offset;
      float odx = player_wx - ox;
      float odz = player_wz - oz;
      if (odx * odx + odz * odz < 7.8f) {
        track_nodes[check_s].orb_slot = infinity_mode ? 24 : 55;
        // Leader in 1st place (with bots) gets +18% vs +30%; Infinity mode gets +35%
        float gain = infinity_mode ? 35.0f : ((player_position == 1 && race_npc_count > 0) ? 18.0f : 30.0f);
        player_boost_fuel = clampf(player_boost_fuel + gain, 0.0f, 100.0f);
        orb_pickup_flash = 14;
      }
    }
  }

  // Outer guardrail wall collision: keeps car inside track corridor without auto-steering on road!
  const float max_wall = ROAD_HALF_W + 3.2f;
  if (player_offset > max_wall) {
    player_offset = max_wall;
    player_wx = n0->x + n0->tx * along + n0->nx * player_offset;
    player_wz = n0->z + n0->tz * along + n0->nz * player_offset;
    player_speed -= player_speed * 1.5f * dt;
    float dot_out = player_sin_yaw * n0->nx + player_cos_yaw * n0->nz;
    if (dot_out > 0.0f) {
      player_sin_yaw = n0->tx;
      player_cos_yaw = n0->tz;
    }
  } else if (player_offset < -max_wall) {
    player_offset = -max_wall;
    player_wx = n0->x + n0->tx * along + n0->nx * player_offset;
    player_wz = n0->z + n0->tz * along + n0->nz * player_offset;
    player_speed -= player_speed * 1.5f * dt;
    float dot_out = player_sin_yaw * n0->nx + player_cos_yaw * n0->nz;
    if (dot_out < 0.0f) {
      player_sin_yaw = n0->tx;
      player_cos_yaw = n0->tz;
    }
  }

  // Dynamic 3D suspension body roll & pitch
  float target_roll = clampf(-player_steer * (player_speed / 75.0f) * 0.15f - player_drift * 0.14f, -0.20f, 0.20f);
  float target_pitch = clampf((accel - drag) * 0.0018f, -0.08f, 0.08f);
  if ((on_kerb || off_road) && player_speed > 10.0f) {
    target_roll += ((frame_counter & 1) ? 0.04f : -0.04f);
  }
  body_roll += (target_roll - body_roll) * 0.18f;
  body_pitch += (target_pitch - body_pitch) * 0.25f;

  if (infinity_mode) {
    player_dist = n0->cum_dist + seg_frac * n0->seg_len;
    player_total_dist = player_dist;
    int stage_num = 1 + (int)(player_total_dist / (INF_BIOME_SEGS * INF_SEG_LEN));
    if (stage_num > current_lap) {
      current_lap = stage_num;
      player_laps_done = current_lap - 1;
      uint32_t elapsed_ms = now_ms - lap_start_ms;
      last_lap_ms = elapsed_ms;
      lap_start_ms = now_ms;
      if (last_lap_ms > 2000 &&
          (best_lap_ms[current_track_id] == 0 || last_lap_ms < best_lap_ms[current_track_id])) {
        split_delta_ms = (best_lap_ms[current_track_id] > 0)
                             ? ((int32_t)last_lap_ms - (int32_t)best_lap_ms[current_track_id])
                             : -1;
        best_lap_ms[current_track_id] = last_lap_ms;
        split_popup_timer = 75;
      }
    }
  } else {
    // Update Lap & Sector Timing (with exact player_laps_done tracking so finish rank is 100% accurate!)
    if (prev_seg > TRACK_N - 15 && player_seg < 15) {
      // Crossed Start/Finish line forward!
      player_laps_done++;
      uint32_t elapsed_ms = now_ms - lap_start_ms;
      last_lap_ms = elapsed_ms;
      lap_start_ms = now_ms;
      current_sector = 0;

      if (last_lap_ms > 3000 &&
          (best_lap_ms[current_track_id] == 0 || last_lap_ms < best_lap_ms[current_track_id])) {
        split_delta_ms = (best_lap_ms[current_track_id] > 0)
                             ? ((int32_t)last_lap_ms - (int32_t)best_lap_ms[current_track_id])
                             : -1;
        best_lap_ms[current_track_id] = last_lap_ms;
        has_best_ghost[current_track_id] = true;
        for (int i = 0; i < GHOST_SAMPLES; i++) {
          best_ghost[i] = curr_ghost[i];
        }
        split_popup_timer = 95;
      }

      if (race_total_laps > 0 && player_laps_done >= race_total_laps) {
        race_finished = true;
        total_race_ms = now_ms - race_start_ms;
      } else {
        current_lap = player_laps_done + 1;
      }
    } else if (prev_seg < 15 && player_seg > TRACK_N - 15) {
      if (player_laps_done > 0) player_laps_done--;
    }

    player_dist = n0->cum_dist + seg_frac * n0->seg_len;
    // Use player_laps_done (not current_lap - 1!) so crossing the finish line increments total_dist!
    player_total_dist = (float)player_laps_done * track_total_length + player_dist;
  }

  step_npcs_and_collisions(dt);
  if (race_finished) {
    final_finish_position = player_position;
  }

  if (!infinity_mode) {
    uint32_t elapsed_ms = now_ms - lap_start_ms;
    int ghost_slot = clampi((int)(elapsed_ms / 100), 0, GHOST_SAMPLES - 1);
    curr_ghost[ghost_slot].wx_dm = (int16_t)(player_wx * 10.0f);
    curr_ghost[ghost_slot].wy_dm = (int16_t)(player_wy * 10.0f);
    curr_ghost[ghost_slot].wz_dm = (int16_t)(player_wz * 10.0f);
    curr_ghost[ghost_slot].yaw_mrad = (int16_t)(player_sin_yaw * 1000.0f);
    curr_ghost[ghost_slot].seg_idx = (uint8_t)player_seg;

    int new_sec = (player_seg * 3) / TRACK_N;
    if (new_sec > current_sector && new_sec < 3) {
      current_sector = new_sec;
      if (best_sector_ms[current_track_id][current_sector] > 0) {
        split_delta_ms = (int32_t)elapsed_ms - (int32_t)best_sector_ms[current_track_id][current_sector];
        split_popup_timer = 75;
      }
      if (best_sector_ms[current_track_id][current_sector] == 0 ||
          elapsed_ms < best_sector_ms[current_track_id][current_sector]) {
        best_sector_ms[current_track_id][current_sector] = elapsed_ms;
      }
    }
  }
  if (split_popup_timer > 0) split_popup_timer--;
}

// ============================================================================
// BUILD 3D SCENE (CAMERA LOCKED TO PLAYER HEADING, NOT TRACK TANGENT!)
// ============================================================================
static TrackTheme active_sky_theme;

static void update_active_sky_theme(void) {
  if (!infinity_mode) {
    active_sky_theme = themes[current_track_id];
  } else {
    uint8_t b0 = track_nodes[player_seg].biome_id % NUM_INF_BIOMES;
    uint8_t b1 = track_nodes[(player_seg + 14) % TRACK_N].biome_id % NUM_INF_BIOMES;
    const TrackTheme *t0 = &infinity_biomes[b0];
    const TrackTheme *t1 = &infinity_biomes[b1];
    int b1_count = 0;
    if (b0 != b1) {
      for (int s = 1; s <= 14; s++) {
        if ((track_nodes[(player_seg + s) % TRACK_N].biome_id % NUM_INF_BIOMES) == b1) b1_count++;
      }
    }
    float w = (float)b1_count * (1.0f / 14.0f);
    active_sky_theme.name = t0->name;
    active_sky_theme.subtitle = t0->subtitle;
    active_sky_theme.sky_top_r = (uint8_t)(t0->sky_top_r + (t1->sky_top_r - t0->sky_top_r) * w);
    active_sky_theme.sky_top_g = (uint8_t)(t0->sky_top_g + (t1->sky_top_g - t0->sky_top_g) * w);
    active_sky_theme.sky_top_b = (uint8_t)(t0->sky_top_b + (t1->sky_top_b - t0->sky_top_b) * w);
    active_sky_theme.sky_bot_r = (uint8_t)(t0->sky_bot_r + (t1->sky_bot_r - t0->sky_bot_r) * w);
    active_sky_theme.sky_bot_g = (uint8_t)(t0->sky_bot_g + (t1->sky_bot_g - t0->sky_bot_g) * w);
    active_sky_theme.sky_bot_b = (uint8_t)(t0->sky_bot_b + (t1->sky_bot_b - t0->sky_bot_b) * w);
    active_sky_theme.ground_r  = (uint8_t)(t0->ground_r  + (t1->ground_r  - t0->ground_r)  * w);
    active_sky_theme.ground_g  = (uint8_t)(t0->ground_g  + (t1->ground_g  - t0->ground_g)  * w);
    active_sky_theme.ground_b  = (uint8_t)(t0->ground_b  + (t1->ground_b  - t0->ground_b)  * w);
    active_sky_theme.road_r = t0->road_r;
    active_sky_theme.road_g = t0->road_g;
    active_sky_theme.road_b = t0->road_b;
    active_sky_theme.kerb_r = t0->kerb_r;
    active_sky_theme.kerb_g = t0->kerb_g;
    active_sky_theme.kerb_b = t0->kerb_b;
    active_sky_theme.grip_mult = t0->grip_mult;
  }
  fog_r = active_sky_theme.sky_bot_r;
  fog_g = active_sky_theme.sky_bot_g;
  fog_b = active_sky_theme.sky_bot_b;
}

static void build_race_scene(bool braking, bool ebraking, uint32_t now_ms) {
  num_screen_tris = 0;
  update_active_sky_theme();

  // Camera sits directly behind the player car along the player's TRUE world heading!
  // Zero track-tangent camera rotation!
  bool is_horse = (selected_car_id == 3 || selected_car_id == 4);
  float cam_dist = (cam_mode == 0) ? (is_horse ? 6.6f : 6.8f) : ((cam_mode == 1) ? 9.5f : -0.6f);
  float cam_h = (cam_mode == 0) ? (is_horse ? 2.50f : 2.40f) : ((cam_mode == 1) ? 3.35f : 1.55f);

  cam_sin_yaw = player_sin_yaw;
  cam_cos_yaw = player_cos_yaw;
  cam_x = player_wx - player_sin_yaw * cam_dist;
  cam_y = player_wy + cam_h;
  cam_z = player_wz - player_cos_yaw * cam_dist;

  const TrackNode *pn0 = &track_nodes[player_seg];
  const TrackNode *pn2 = &track_nodes[(player_seg + 2) % TRACK_N];
  float slope = (pn2->y - pn0->y) * (1.0f / (pn0->seg_len * 2.0f));
  cam_pitch = -0.11f + clampf(slope * 0.35f, -0.08f, 0.08f);
  cam_roll = -body_roll * 0.35f;
  cam_cos_pitch = cosf_fast(cam_pitch);
  cam_sin_pitch = sinf_fast(cam_pitch);
  cam_cos_roll = cosf_fast(cam_roll);
  cam_sin_roll = sinf_fast(cam_roll);

  // Render 22 segments ahead and 2 behind
  const int DRAW_SEGS = 22;
  for (int s = DRAW_SEGS; s >= -2; s--) {
    int i0 = (player_seg + s + TRACK_N) % TRACK_N;
    int i1 = (i0 + 1) % TRACK_N;
    const TrackNode *n0 = &track_nodes[i0];
    const TrackNode *n1 = &track_nodes[i1];
    uint8_t b_id = n0->biome_id % NUM_INF_BIOMES;
    const TrackTheme *th = infinity_mode ? &infinity_biomes[b_id] : &themes[current_track_id];

    float u_v0 = (float)(i0 & 1);
    float u_v1 = u_v0 + 1.0f;

    Vec3UV r0_l = {n0->x - n0->nx * ROAD_HALF_W, n0->y, n0->z - n0->nz * ROAD_HALF_W, 0.0f, u_v0};
    Vec3UV r0_m = {n0->x,                        n0->y, n0->z,                        0.5f, u_v0};
    Vec3UV r0_r = {n0->x + n0->nx * ROAD_HALF_W, n0->y, n0->z + n0->nz * ROAD_HALF_W, 1.0f, u_v0};

    Vec3UV r1_l = {n1->x - n1->nx * ROAD_HALF_W, n1->y, n1->z - n1->nz * ROAD_HALF_W, 0.0f, u_v1};
    Vec3UV r1_m = {n1->x,                        n1->y, n1->z,                        0.5f, u_v1};
    Vec3UV r1_r = {n1->x + n1->nx * ROAD_HALF_W, n1->y, n1->z + n1->nz * ROAD_HALF_W, 1.0f, u_v1};

    uint8_t road_tex = (!infinity_mode && i0 == 0) ? TEX_FINISH : TEX_ASPHALT;
    uint8_t shade = (i0 & 1) ? 0 : 6;
    submit_world_quad(r0_l, r0_m, r1_m, r1_l,
                      th->road_r + shade, th->road_g + shade, th->road_b + shade, road_tex, true);
    submit_world_quad(r0_m, r0_r, r1_r, r1_m,
                      th->road_r + shade, th->road_g + shade, th->road_b + shade, road_tex, true);

    float kw = 1.40f;
    Vec3UV k0_ll = {n0->x - n0->nx * (ROAD_HALF_W + kw), n0->y + 0.12f, n0->z - n0->nz * (ROAD_HALF_W + kw), 0.0f, u_v0};
    Vec3UV k1_ll = {n1->x - n1->nx * (ROAD_HALF_W + kw), n1->y + 0.12f, n1->z - n1->nz * (ROAD_HALF_W + kw), 0.0f, u_v1};
    submit_world_quad(k0_ll, r0_l, r1_l, k1_ll, th->kerb_r, th->kerb_g, th->kerb_b, TEX_KERB, true);

    Vec3UV k0_rr = {n0->x + n0->nx * (ROAD_HALF_W + kw), n0->y + 0.12f, n0->z + n0->nz * (ROAD_HALF_W + kw), 0.0f, u_v0};
    Vec3UV k1_rr = {n1->x + n1->nx * (ROAD_HALF_W + kw), n1->y + 0.12f, n1->z + n0->nz * (ROAD_HALF_W + kw), 0.0f, u_v1};
    submit_world_quad(r0_r, k0_rr, k1_rr, r1_r, th->kerb_r, th->kerb_g, th->kerb_b, TEX_KERB, true);

    // Compact terrain shoulder verge (grass / desert sand / cyber grid / snow / volcanic ash!)
    if (s < 14) {
      float gw = ROAD_HALF_W + 6.5f;
      Vec3UV g0_l = {n0->x - n0->nx * gw, n0->y - 0.05f, n0->z - n0->nz * gw, 0.0f, u_v0};
      Vec3UV g1_l = {n1->x - n1->nx * gw, n1->y - 0.05f, n1->z - n1->nz * gw, 0.0f, u_v1};
      submit_world_quad(g0_l, k0_ll, k1_ll, g1_l, th->ground_r, th->ground_g, th->ground_b, TEX_GRASS, true);

      Vec3UV g0_r = {n0->x + n0->nx * gw, n0->y - 0.05f, n0->z + n0->nz * gw, 0.0f, u_v0};
      Vec3UV g1_r = {n1->x + n1->nx * gw, n1->y - 0.05f, n1->z + n1->nz * gw, 0.0f, u_v1};
      submit_world_quad(k0_rr, g0_r, g1_r, k1_rr, th->ground_r, th->ground_g, th->ground_b, TEX_GRASS, true);
    }

    if (s < 0) continue;

    // Render 3D Floating Boost Orb (slot == 1) on road!
    if (n0->orb_slot == 1 && s < 20) {
      float ox = n0->x + n0->nx * n0->orb_offset;
      float oz = n0->z + n0->nz * n0->orb_offset;
      float bob = 1.25f + 0.25f * sinf_fast((float)frame_counter * 0.22f + (float)i0);
      float oy = n0->y + bob;
      float ang = (float)frame_counter * 0.16f + (float)i0;
      float rx = cosf_fast(ang) * 1.15f;
      float rz = sinf_fast(ang) * 1.15f;

      Vec3UV o_top = {ox, oy + 1.10f, oz, 0, 0};
      Vec3UV o_bot = {ox, oy - 0.85f, oz, 0, 1};
      Vec3UV o_l   = {ox - rx, oy + 0.10f, oz - rz, 0, 0};
      Vec3UV o_r   = {ox + rx, oy + 0.10f, oz + rz, 1, 0};
      Vec3UV o_f   = {ox - rz, oy + 0.10f, oz + rx, 0, 1};
      Vec3UV o_b   = {ox + rz, oy + 0.10f, oz - rx, 1, 1};

      // Glowing ground pad under the orb
      Vec3UV p0 = {ox - n0->nx * 1.1f, n0->y + 0.05f, oz - n0->nz * 1.1f, 0, 0};
      Vec3UV p1 = {ox + n0->nx * 1.1f, n0->y + 0.05f, oz + n0->nz * 1.1f, 1, 0};
      Vec3UV p2 = {ox + n0->tx * 1.4f, n0->y + 0.05f, oz + n0->tz * 1.4f, 0, 1};
      submit_world_tri(p0, p1, p2, 0, 195, 255, TEX_FLAT, true);

      // Cyan/Gold Nitro Boost Orb
      submit_world_tri(o_top, o_l, o_f, 60, 255, 255, TEX_FLAT, true);
      submit_world_tri(o_top, o_f, o_r, 0, 210, 255, TEX_FLAT, true);
      submit_world_tri(o_bot, o_f, o_l, 20, 150, 245, TEX_FLAT, true);
      submit_world_tri(o_bot, o_r, o_f, 255, 230, 80, TEX_FLAT, true);
      submit_world_tri(o_top, o_r, o_b, 80, 255, 220, TEX_FLAT, true);
      submit_world_tri(o_top, o_b, o_l, 0, 185, 255, TEX_FLAT, true);
    }

    // Roadside Props & Arches (no floating tunnel ceiling slabs!)
    if (n0->prop_type == 2 || n0->prop_type == 3) {
      float aw = ROAD_HALF_W + 2.4f;
      float ah = (n0->prop_type == 2) ? 6.8f : 5.8f;
      Vec3UV p_l0 = {n0->x - n0->nx * aw, n0->y, n0->z - n0->nz * aw, 0, 0};
      Vec3UV p_l1 = {n0->x - n0->nx * (aw - 0.9f), n0->y, n0->z - n0->nz * (aw - 0.9f), 1, 0};
      Vec3UV p_l2 = {n0->x - n0->nx * (aw - 0.7f), n0->y + ah, n0->z - n0->nz * (aw - 0.7f), 1, 1};
      submit_world_tri(p_l0, p_l1, p_l2, 200, 200, 215, TEX_FLAT, true);

      Vec3UV p_r0 = {n0->x + n0->nx * (aw - 0.9f), n0->y, n0->z + n0->nz * (aw - 0.9f), 0, 0};
      Vec3UV p_r1 = {n0->x + n0->nx * aw, n0->y, n0->z + n0->nz * aw, 1, 0};
      Vec3UV p_r2 = {n0->x + n0->nx * (aw - 0.7f), n0->y + ah, n0->z + n0->nz * (aw - 0.7f), 1, 1};
      submit_world_tri(p_r0, p_r1, p_r2, 200, 200, 215, TEX_FLAT, true);

      if (n0->prop_type == 2) {
        Vec3UV b0 = {n0->x - n0->nx * aw, n0->y + ah - 1.3f, n0->z - n0->nz * aw, 0, 0};
        Vec3UV b1 = {n0->x + n0->nx * aw, n0->y + ah - 1.3f, n0->z + n0->nz * aw, 1, 0};
        Vec3UV b2 = {n0->x + n0->nx * aw, n0->y + ah,        n0->z + n0->nz * aw, 1, 1};
        Vec3UV b3 = {n0->x - n0->nx * aw, n0->y + ah,        n0->z - n0->nz * aw, 0, 1};
        uint8_t ban_r = (!infinity_mode && i0 == 0) ? 235 : th->kerb_r;
        uint8_t ban_g = (!infinity_mode && i0 == 0) ? 45  : th->kerb_g;
        uint8_t ban_b = (!infinity_mode && i0 == 0) ? 45  : th->kerb_b;
        submit_world_quad(b0, b1, b2, b3, ban_r, ban_g, ban_b,
                          (!infinity_mode && i0 == 0) ? TEX_FINISH : TEX_FLAT, true);
      }
    } else if (n0->prop_type == 1 && s < 16) {
      float side = (float)n0->prop_side * (ROAD_HALF_W + 4.8f);
      float tx = n0->x + n0->nx * side;
      float ty = n0->y;
      float tz = n0->z + n0->nz * side;
      bool is_cyber_lamp = infinity_mode ? (b_id == 2) : (current_track_id == 1);
      if (is_cyber_lamp) {
        Vec3UV l0 = {tx - n0->nx * 0.25f, ty,        tz - n0->nz * 0.25f, 0, 0};
        Vec3UV l1 = {tx + n0->nx * 0.25f, ty,        tz + n0->nz * 0.25f, 1, 0};
        Vec3UV l2 = {tx,                  ty + 6.5f, tz,                  0, 1};
        submit_world_tri(l0, l1, l2, 90, 220, 255, TEX_FLAT, true);
      } else {
        Vec3UV t0 = {tx - n0->nx * 2.2f, ty + 0.6f, tz - n0->nz * 2.2f, 0, 0};
        Vec3UV t1 = {tx + n0->nx * 2.2f, ty + 0.6f, tz + n0->nz * 2.2f, 1, 0};
        Vec3UV t2 = {tx,                 ty + 6.8f, tz,                 0, 1};
        uint8_t pr = 24, pg = 95, pb = 44;
        if (infinity_mode) {
          if (b_id == 1) { pr = 205; pg = 120; pb = 52; }      // Desert sandstone spire
          else if (b_id == 3) { pr = 185; pg = 220; pb = 245; } // Snow-covered glacier pine
          else if (b_id == 4) { pr = 235; pg = 85;  pb = 28;  } // Volcanic crystal obelisk
        }
        submit_world_tri(t0, t1, t2, pr, pg, pb, TEX_FLAT, true);
      }
    } else if (n0->prop_type == 4 && s < 16) {
      float side = (float)n0->prop_side * (ROAD_HALF_W + 2.5f);
      float sx = n0->x + n0->nx * side;
      float sy = n0->y;
      float sz = n0->z + n0->nz * side;
      Vec3UV c0 = {sx - n0->nx * 1.2f, sy + 0.4f, sz - n0->nz * 1.2f, 0, 0};
      Vec3UV c1 = {sx + n0->nx * 1.2f, sy + 0.4f, sz + n0->nz * 1.2f, 1, 0};
      Vec3UV c2 = {sx + n0->nx * 1.2f, sy + 2.2f, sz + n0->nz * 1.2f, 1, 1};
      Vec3UV c3 = {sx - n0->nx * 1.2f, sy + 2.2f, sz - n0->nz * 1.2f, 0, 1};
      submit_world_quad(c0, c1, c2, c3, 240, 185, 20, TEX_WALL, true);
    }
  }

  // Render 3D AI NPC Rival Cars (with exhaust flame when NPC is boosting!)
  for (int i = 0; i < race_npc_count; i++) {
    const NpcCar *ai = &npcs[i];
    int seg_diff = (ai->seg_idx - player_seg + TRACK_N) % TRACK_N;
    if (seg_diff < 22 || seg_diff > TRACK_N - 4) {
      CarPose apose = {
          ai->wx,
          ai->wy,
          ai->wz,
          ai->cos_y,
          ai->sin_y,
          0.0f,
          0.0f,
          0.0f
      };
      const uint8_t *col = npc_colors[ai->color_idx & 7];
      draw_3d_car_colored(&apose, ai->car_model, col[0], col[1], col[2], ai->braking, ai->boosting, false);
    }
  }

  // Render Best Lap 3D Ghost Car (Circuit mode only)
  if (!infinity_mode && has_best_ghost[current_track_id]) {
    uint32_t elapsed_ms = now_ms - lap_start_ms;
    int g_slot = clampi((int)(elapsed_ms / 100), 0, GHOST_SAMPLES - 1);
    const GhostFrame *gf = &best_ghost[g_slot];
    int seg_diff = ((int)gf->seg_idx - player_seg + TRACK_N) % TRACK_N;
    if (seg_diff < 22 || seg_diff > TRACK_N - 4) {
      const TrackNode *gn = &track_nodes[gf->seg_idx % TRACK_N];
      CarPose gpose = {
          (float)gf->wx_dm * 0.1f,
          (float)gf->wy_dm * 0.1f,
          (float)gf->wz_dm * 0.1f,
          gn->tz,
          gn->tx,
          0.0f, 0.0f, 0.0f
      };
      draw_3d_car(&gpose, selected_car_id, false, false, true);
    }
  }

  // Render Player 3D Car (with drift visual rotation & twin cyan nitro flames!)
  if (cam_mode != 2) {
    float slip_ang = player_drift * 0.65f;
    float cd = cosf_fast(slip_ang);
    float sd = sinf_fast(slip_ang);
    float vis_sin = player_sin_yaw * cd + player_cos_yaw * sd;
    float vis_cos = player_cos_yaw * cd - player_sin_yaw * sd;
    CarPose ppose = {
        player_wx,
        player_wy,
        player_wz,
        vis_cos,
        vis_sin,
        body_roll,
        body_pitch,
        player_steer
    };
    draw_3d_car(&ppose, selected_car_id, braking, shift_flame_timer > 0 || player_boosting, false);

    if (player_boosting) {
      float f_len = (frame_counter & 1) ? -3.45f : -3.10f;
      Vec3UV nf0 = car_local_to_world(&ppose, -0.76f, 0.30f, -2.24f, 0, 0);
      Vec3UV nf1 = car_local_to_world(&ppose, -0.36f, 0.30f, -2.24f, 1, 0);
      Vec3UV nf2 = car_local_to_world(&ppose, -0.56f, 0.36f,  f_len, 0, 1);
      submit_world_tri(nf0, nf1, nf2, 40, 240, 255, TEX_FLAT, true);

      Vec3UV nr0 = car_local_to_world(&ppose,  0.36f, 0.30f, -2.24f, 0, 0);
      Vec3UV nr1 = car_local_to_world(&ppose,  0.76f, 0.30f, -2.24f, 1, 0);
      Vec3UV nr2 = car_local_to_world(&ppose,  0.56f, 0.36f,  f_len, 0, 1);
      submit_world_tri(nr0, nr1, nr2, 40, 240, 255, TEX_FLAT, true);
    }

    if ((fabsf_fast(player_drift) > 0.16f || ebraking) && player_speed > 15.0f) {
      float jit = ((frame_counter & 1) ? 0.25f : -0.25f);
      Vec3UV sm0 = car_local_to_world(&ppose, -1.25f + jit, 0.10f, -2.1f, 0, 0);
      Vec3UV sm1 = car_local_to_world(&ppose, -0.65f + jit, 0.10f, -2.6f, 1, 0);
      Vec3UV sm2 = car_local_to_world(&ppose, -0.95f,       0.75f, -2.5f, 0, 1);
      submit_world_tri(sm0, sm1, sm2, 215, 215, 225, TEX_GHOST, true);

      Vec3UV sr0 = car_local_to_world(&ppose,  0.65f + jit, 0.10f, -2.6f, 0, 0);
      Vec3UV sr1 = car_local_to_world(&ppose,  1.25f + jit, 0.10f, -2.1f, 1, 0);
      Vec3UV sr2 = car_local_to_world(&ppose,  0.95f,       0.75f, -2.5f, 0, 1);
      submit_world_tri(sr0, sr1, sr2, 215, 215, 225, TEX_GHOST, true);
    }
  }
}

// ============================================================================
// 2D PIXEL FONT, HUD, MINI-MAP & 2-STEP MENU OVERLAYS
// ============================================================================
static inline void strip_put_px(int strip_y0, int x, int y, eadk_color_t c) {
  if (x >= 0 && x < SCREEN_W && y >= strip_y0 && y < strip_y0 + STRIP_H) {
    fb_strip[(y - strip_y0) * SCREEN_W + x] = c;
  }
}

static void strip_fill_rect(int strip_y0, int x, int y, int w, int h, eadk_color_t c) {
  int y0 = y > strip_y0 ? y : strip_y0;
  int y1 = (y + h - 1) < (strip_y0 + STRIP_H - 1) ? (y + h - 1) : (strip_y0 + STRIP_H - 1);
  if (y0 > y1) return;
  int x0 = clampi(x, 0, SCREEN_W - 1);
  int x1 = clampi(x + w - 1, 0, SCREEN_W - 1);
  for (int py = y0; py <= y1; py++) {
    eadk_color_t *row = &fb_strip[(py - strip_y0) * SCREEN_W];
    for (int px = x0; px <= x1; px++) row[px] = c;
  }
}

static const uint16_t font3x5[45] = {
    0x7B6F, // 0
    0x2492, // 1
    0x73E7, // 2
    0x73CF, // 3
    0x5BC9, // 4
    0x79CF, // 5
    0x79EF, // 6
    0x7249, // 7
    0x7BEF, // 8
    0x7BCF, // 9
    0x0000, // space (10)
    0x0410, // : (11)
    0x0002, // . (12)
    0x01C0, // - (13)
    0x05D0, // + (14)
    0x2524, // / (15)
    // A..Z (16..41)
    0x7BED, 0x6BAE, 0x7927, 0x6B6E, 0x79E7, 0x79E4, 0x796F, 0x5BED,
    0x7497, 0x126F, 0x5BAD, 0x4927, 0x5FED, 0x6B6D, 0x7B6F, 0x7BE4,
    0x7B7A, 0x6BAD, 0x79CF, 0x7492, 0x5B6F, 0x5B6A, 0x5BFD, 0x5AAD,
    0x5A92, 0x72A7,
    0x1511, // < (42)
    0x4544, // > (43)
    0x0000
};

static int char_to_glyph(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'Z') return 16 + (c - 'A');
  if (c >= 'a' && c <= 'z') return 16 + (c - 'a');
  if (c == ':') return 11;
  if (c == '.') return 12;
  if (c == '-') return 13;
  if (c == '+') return 14;
  if (c == '/') return 15;
  if (c == '<') return 42;
  if (c == '>') return 43;
  return 10;
}

static void strip_draw_text(int strip_y0, int x, int y, const char *str, int scale, eadk_color_t col) {
  if (y + 5 * scale < strip_y0 || y >= strip_y0 + STRIP_H) return;
  int cx = x;
  while (*str) {
    int g = char_to_glyph(*str++);
    uint16_t bits = font3x5[g];
    if (bits) {
      for (int r = 0; r < 5; r++) {
        for (int c = 0; c < 3; c++) {
          if ((bits >> (14 - (r * 3 + c))) & 1) {
            strip_fill_rect(strip_y0, cx + c * scale, y + r * scale, scale, scale, col);
          }
        }
      }
    }
    cx += 4 * scale;
  }
}

static void format_time_ms(uint32_t ms, char *buf) {
  uint32_t mins = ms / 60000;
  uint32_t secs = (ms / 1000) % 60;
  uint32_t hund = (ms / 10) % 100;
  buf[0] = '0' + (mins % 10);
  buf[1] = ':';
  buf[2] = '0' + (secs / 10);
  buf[3] = '0' + (secs % 10);
  buf[4] = '.';
  buf[5] = '0' + (hund / 10);
  buf[6] = '0' + (hund % 10);
  buf[7] = '\0';
}

static void format_int3(int val, char *buf) {
  if (val < 0) val = 0;
  buf[0] = (val >= 100) ? ('0' + (val / 100) % 10) : ' ';
  buf[1] = (val >= 10)  ? ('0' + (val / 10) % 10)  : ' ';
  buf[2] = '0' + (val % 10);
  buf[3] = '\0';
}

static void fill_background_strip(int strip_y0) {
  const TrackTheme *th = &active_sky_theme;
  int horizon_y = (int)(SCREEN_H * 0.48f - cam_sin_pitch * fov_scale);
  horizon_y = clampi(horizon_y, 20, SCREEN_H - 20);

  // Fast reset 16-bit depth buffer to 0xFFFF (32-bit word fills)
  uint32_t *zrow32 = (uint32_t *)zb_strip;
  for (int i = 0; i < (SCREEN_W * STRIP_H) / 2; i++) {
    zrow32[i] = 0xFFFFFFFFu;
  }

  for (int sy = 0; sy < STRIP_H; sy++) {
    int y = strip_y0 + sy;
    eadk_color_t *row = &fb_strip[sy * SCREEN_W];

    if (y < horizon_y) {
      float t = clampf((float)y / (float)(horizon_y > 0 ? horizon_y : 1), 0.0f, 1.0f);
      int r = (int)(th->sky_top_r + (th->sky_bot_r - th->sky_top_r) * t);
      int g = (int)(th->sky_top_g + (th->sky_bot_g - th->sky_top_g) * t);
      int b = (int)(th->sky_top_b + (th->sky_bot_b - th->sky_top_b) * t);

      eadk_color_t sky_col = rgb565(r, g, b);
      eadk_color_t mtn_col = rgb565(th->sky_top_r + 18, th->sky_top_g + 16, th->sky_top_b + 28);

      int m_scroll = (int)(cam_sin_yaw * 95.0f);
      for (int x = 0; x < SCREEN_W; x++) {
        int mx = (x + m_scroll) & 255;
        int m_h = ((mx & 31) < 16 ? (mx & 15) : (15 - (mx & 15))) + (((mx >> 5) * 7) & 11);
        row[x] = (y >= horizon_y - m_h) ? mtn_col : sky_col;
      }
    } else {
      float t = clampf((float)(y - horizon_y) / (float)(SCREEN_H - horizon_y), 0.0f, 1.0f);
      int r = (int)(fog_r + (th->ground_r - fog_r) * t);
      int g = (int)(fog_g + (th->ground_g - fog_g) * t);
      int b = (int)(fog_b + (th->ground_b - fog_b) * t);
      eadk_color_t gnd_col = rgb565(r, g, b);
      for (int x = 0; x < SCREEN_W; x++) {
        row[x] = gnd_col;
      }
    }
  }
}

static void draw_minimap_strip(int strip_y0, int center_x, int center_y, bool show_cars) {
  if (strip_y0 > center_y + 30 || strip_y0 + STRIP_H < center_y - 30) return;

  if (infinity_mode && show_cars) {
    for (int s = -6; s <= 34; s++) {
      int i = (player_seg + s + TRACK_N) % TRACK_N;
      int mx = center_x + (int)((track_nodes[i].x - map_cx) * map_scale);
      int my = center_y - (int)((track_nodes[i].z - map_cz) * map_scale);
      strip_fill_rect(strip_y0, mx, my, 2, 2, (s > 0) ? 0x07FF : 0xAD55);
    }
  } else {
    for (int i = 0; i < TRACK_N; i++) {
      int mx = center_x + (int)((track_nodes[i].x - map_cx) * map_scale);
      int my = center_y - (int)((track_nodes[i].z - map_cz) * map_scale);
      strip_fill_rect(strip_y0, mx, my, 2, 2, (i == 0) ? 0xFFE0 : 0xAD55);
    }
  }
  if (!show_cars) return;

  for (int i = 0; i < race_npc_count; i++) {
    int nx = center_x + (int)((npcs[i].wx - map_cx) * map_scale);
    int ny = center_y - (int)((npcs[i].wz - map_cz) * map_scale);
    strip_fill_rect(strip_y0, nx - 1, ny - 1, 3, 3, 0x07E0);
  }

  int px = center_x + (int)((player_wx - map_cx) * map_scale);
  int py = center_y - (int)((player_wz - map_cz) * map_scale);
  strip_fill_rect(strip_y0, px - 2, py - 2, 4, 4, 0xF800);
}

static void draw_hud_strip(int strip_y0, uint32_t now_ms) {
  const CarSpec *cs = &car_specs[selected_car_id];
  int disp_pos = race_finished ? final_finish_position : player_position;

  if (strip_y0 == 0) {
    strip_fill_rect(strip_y0, 0, 0, SCREEN_W, 20, 0x1082);
    if (infinity_mode) {
      strip_draw_text(strip_y0, 5, 5, "INF", 2, 0xFFE0);
      char lap_str[8];
      int l_idx = 0;
      lap_str[l_idx++] = 'Z';
      if (current_lap >= 10) lap_str[l_idx++] = '0' + ((current_lap / 10) % 10);
      lap_str[l_idx++] = '0' + (current_lap % 10);
      lap_str[l_idx] = '\0';
      strip_draw_text(strip_y0, 35, 5, lap_str, 2, 0xFFFF);

      strip_draw_text(strip_y0, 76, 5, "KM", 2, 0x07E0);
      int km_tenths = (int)(player_total_dist * 0.01f);
      int km_whole = km_tenths / 10;
      int km_frac = km_tenths % 10;
      char km_str[8];
      int k_idx = 0;
      if (km_whole >= 10) km_str[k_idx++] = '0' + ((km_whole / 10) % 10);
      km_str[k_idx++] = '0' + (km_whole % 10);
      km_str[k_idx++] = '.';
      km_str[k_idx++] = '0' + km_frac;
      km_str[k_idx] = '\0';
      strip_draw_text(strip_y0, 98, 5, km_str, 2, 0xFFFF);
    } else {
      strip_draw_text(strip_y0, 5, 5, "LAP", 2, 0xFFE0);
      char lap_str[8];
      int l_idx = 0;
      if (current_lap >= 10) lap_str[l_idx++] = '0' + (current_lap / 10);
      lap_str[l_idx++] = '0' + (current_lap % 10);
      lap_str[l_idx++] = '/';
      if (race_total_laps >= 10) lap_str[l_idx++] = '0' + (race_total_laps / 10);
      lap_str[l_idx++] = '0' + (race_total_laps % 10);
      lap_str[l_idx] = '\0';
      strip_draw_text(strip_y0, 33, 5, lap_str, 2, 0xFFFF);

      strip_draw_text(strip_y0, 82, 5, "POS", 2, 0x07E0);
      char pos_str[6] = {
          '0' + (disp_pos % 10),
          '/',
          '0' + ((race_npc_count + 1) % 10),
          '\0'
      };
      strip_draw_text(strip_y0, 110, 5, pos_str, 2, 0xFFFF);
    }

    char tbuf[12];
    format_time_ms(now_ms - lap_start_ms, tbuf);
    strip_draw_text(strip_y0, 144, 5, tbuf, 2, 0x07FF);

    strip_draw_text(strip_y0, 212, 5, "BEST", 2, 0xFBE0);
    if (best_lap_ms[current_track_id] > 0) {
      format_time_ms(best_lap_ms[current_track_id], tbuf);
    } else {
      const char *dash = "-:--.--";
      for (int i = 0; i < 8; i++) tbuf[i] = dash[i];
    }
    strip_draw_text(strip_y0, 248, 5, tbuf, 2, 0xFBE0);

    if (split_popup_timer > 0) {
      strip_fill_rect(strip_y0, 108, 22, 104, 15, 0x0841);
      bool faster = (split_delta_ms <= 0);
      uint32_t abs_d = faster ? (uint32_t)(-split_delta_ms) : (uint32_t)split_delta_ms;
      char dbuf[12];
      dbuf[0] = faster ? '-' : '+';
      dbuf[1] = '0' + ((abs_d / 1000) % 10);
      dbuf[2] = '.';
      dbuf[3] = '0' + ((abs_d / 100) % 10);
      dbuf[4] = '0' + ((abs_d / 10) % 10);
      dbuf[5] = 'S';
      dbuf[6] = '\0';
      strip_draw_text(strip_y0, 132, 25, dbuf, 2, faster ? 0x07E0 : 0xF800);
    }
  }

  draw_minimap_strip(strip_y0, 288, 52, true);

  if (race_finished && strip_y0 >= 40 && strip_y0 <= 120) {
    strip_fill_rect(strip_y0, 44, 62, 232, 74, 0x0841);
    strip_fill_rect(strip_y0, 44, 62, 232, 3, 0xFFE0);
    strip_draw_text(strip_y0, 74, 70, "FINISH! POSITION:", 2, 0xFFE0);
    char p_str[4] = {'0' + disp_pos, '\0'};
    strip_draw_text(strip_y0, 222, 68, p_str, 3, (disp_pos == 1) ? 0x07E0 : 0xFFFF);

    char rt_buf[12];
    format_time_ms(total_race_ms, rt_buf);
    strip_draw_text(strip_y0, 74, 94, "RACE TIME:", 2, 0x07FF);
    strip_draw_text(strip_y0, 166, 94, rt_buf, 2, 0xFFFF);
    strip_draw_text(strip_y0, 62, 116, "OK: NEW TRACK   HOME: MENU", 1, 0xAD55);
  }

  if (strip_y0 == 200) {
    strip_fill_rect(strip_y0, 0, 204, SCREEN_W, 36, 0x0841);
    strip_fill_rect(strip_y0, 0, 204, SCREEN_W, 2, player_boosting ? 0x07FF : 0xAD55);

    strip_draw_text(strip_y0, 8, 208, "RPM", 1, 0xAD55);
    if (orb_pickup_flash > 0) {
      strip_draw_text(strip_y0, 30, 208, "+ORB BOOST!", 1, 0xFFE0);
    } else if (player_boosting) {
      strip_draw_text(strip_y0, 30, 208,
                      (!infinity_mode && disp_pos == 1 && race_npc_count > 0) ? "NITRO (1ST 2X BURN)" : "NITRO ACTIVE!",
                      1, 0x07FF);
    } else if (slipstreaming) {
      strip_draw_text(strip_y0, 30, 208, "SLIPSTREAM!", 1, 0x07E0);
    } else if (infinity_mode) {
      uint8_t b_id = track_nodes[player_seg].biome_id % NUM_INF_BIOMES;
      strip_draw_text(strip_y0, 30, 208, infinity_biomes[b_id].name, 1, 0x07E0);
    }

    int bars = (engine_rpm * 20) / cs->redline_rpm;
    bars = clampi(bars, 1, 20);
    for (int b = 0; b < 20; b++) {
      eadk_color_t bcol = (b >= 17) ? 0xF800 : ((b >= 13) ? 0xFFE0 : 0x07FF);
      if (b >= bars) bcol = 0x2104;
      int bh = 5 + (b >> 2);
      strip_fill_rect(strip_y0, 8 + b * 6, 226 - bh, 4, bh, bcol);
    }

    // NITRO / BOOST ORB FUEL BAR
    strip_draw_text(strip_y0, 8, 230, "BST", 1, player_boosting ? 0xFFE0 : 0x07FF);
    strip_fill_rect(strip_y0, 26, 230, 102, 6, 0x2104);
    int boost_w = clampi((int)player_boost_fuel, 0, 100);
    if (boost_w > 0) {
      eadk_color_t ncol = player_boosting ? ((frame_counter & 1) ? 0xFFFF : 0xFFE0)
                                          : ((orb_pickup_flash > 0) ? 0xFFE0 : 0x07FF);
      strip_fill_rect(strip_y0, 27, 231, boost_w, 4, ncol);
    }

    strip_fill_rect(strip_y0, 156, 209, 26, 27, 0x2104);
    char g_str[2] = {'0' + current_gear, '\0'};
    eadk_color_t g_col = (engine_rpm > cs->redline_rpm - 350) ? 0xF800 : 0xFFE0;
    strip_draw_text(strip_y0, 163, 213, g_str, 4, g_col);
    strip_draw_text(strip_y0, 186, 212, manual_trans ? "MT" : "AT", 2, 0x07E0);

    if (fabsf_fast(player_drift) > 0.16f) {
      strip_draw_text(strip_y0, 186, 226, "DRIFT", 1, 0xF81F);
    }

    int kmh = (int)(player_speed * 3.6f);
    char spd_str[4];
    format_int3(kmh, spd_str);
    strip_draw_text(strip_y0, 234, 212, spd_str, 4, player_boosting ? 0x07FF : 0xFFFF);
    strip_draw_text(strip_y0, 286, 224, "KMH", 2, 0xAD55);
  }
}


static uint16_t strip_tri_list[NUM_STRIPS][MAX_SCREEN_TRIS];
static uint16_t strip_tri_count[NUM_STRIPS];

static inline void bin_screen_tris_to_strips(void) {
  for (int s = 0; s < NUM_STRIPS; s++) strip_tri_count[s] = 0;
  for (int i = 0; i < num_screen_tris; i++) {
    int s_min = screen_tris[i].min_y / STRIP_H;
    int s_max = screen_tris[i].max_y / STRIP_H;
    if (s_min < 0) s_min = 0;
    if (s_max >= NUM_STRIPS) s_max = NUM_STRIPS - 1;
    for (int s = s_min; s <= s_max; s++) {
      strip_tri_list[s][strip_tri_count[s]++] = (uint16_t)i;
    }
  }
}

static void render_full_frame(bool braking, bool ebraking, uint32_t now_ms) {
  build_race_scene(braking, ebraking, now_ms);
  bin_screen_tris_to_strips();

  for (int s = 0; s < NUM_STRIPS; s++) {
    int strip_y0 = s * STRIP_H;
    fill_background_strip(strip_y0);

    int count = strip_tri_count[s];
    const uint16_t *list = strip_tri_list[s];
    for (int t = 0; t < count; t++) {
      rasterize_tri_strip(&screen_tris[list[t]], strip_y0);
    }

    draw_hud_strip(strip_y0, now_ms);

    eadk_rect_t rect = {0, (uint16_t)strip_y0, SCREEN_W, STRIP_H};
    eadk_display_push_rect(rect, fb_strip);
  }
}

// ============================================================================
// 2-STEP MENU SYSTEM (GARAGE -> RACE SETUP -> RACE)
// ============================================================================
enum GameScreen {
  SCREEN_GARAGE = 0,
  SCREEN_RACE_SETUP = 1,
  SCREEN_RACING = 2,
  SCREEN_CONTROLS = 3
};

static int setup_cursor = 0;
static int controls_return_screen = SCREEN_GARAGE;

static void render_menu_frame(float spin_angle, int screen_state) {
  num_screen_tris = 0;
  const TrackTheme *th = &themes[current_track_id];
  const CarSpec *cs = &car_specs[selected_car_id];
  active_sky_theme = *th;
  fog_r = th->sky_bot_r;
  fog_g = th->sky_bot_g;
  fog_b = th->sky_bot_b;

  cam_x = 0.0f;
  cam_y = (selected_car_id >= 3) ? 2.25f : 2.15f;
  cam_z = (selected_car_id >= 3) ? -5.5f : -5.6f;
  cam_yaw = 0.0f;
  cam_pitch = -0.14f;
  cam_roll = 0.0f;
  update_camera_trig();

  for (int i = 0; i < 12; i++) {
    float a0 = (float)i * (TAU_F / 12.0f) + spin_angle * 0.5f;
    float a1 = (float)(i + 1) * (TAU_F / 12.0f) + spin_angle * 0.5f;
    Vec3UV v0 = {0.0f, -0.02f, 0.0f, 0.5f, 0.5f};
    Vec3UV v1 = {cosf_fast(a0) * 3.5f, -0.02f, sinf_fast(a0) * 3.5f, 0.0f, 0.0f};
    Vec3UV v2 = {cosf_fast(a1) * 3.5f, -0.02f, sinf_fast(a1) * 3.5f, 1.0f, 1.0f};
    uint8_t c = (i & 1) ? 45 : 70;
    submit_world_tri(v0, v1, v2, c, c + 5, c + 18, TEX_FLAT, true);
  }

  CarPose pose = {0.0f, 0.0f, 0.0f, cosf_fast(spin_angle), sinf_fast(spin_angle), 0.0f, 0.0f, 0.25f};
  draw_3d_car(&pose, selected_car_id, true, false, false);
  bin_screen_tris_to_strips();

  for (int s = 0; s < NUM_STRIPS; s++) {
    int strip_y0 = s * STRIP_H;
    fill_background_strip(strip_y0);
    int count = strip_tri_count[s];
    const uint16_t *list = strip_tri_list[s];
    for (int t = 0; t < count; t++) {
      rasterize_tri_strip(&screen_tris[list[t]], strip_y0);
    }

    if (screen_state == SCREEN_GARAGE) {
      if (strip_y0 == 0) {
        strip_fill_rect(0, 0, 0, SCREEN_W, 38, 0x0841);
        strip_draw_text(0, 12, 6, "POLYGT 3D", 3, 0xFFE0);
        strip_draw_text(0, 140, 10, "PS1 RACING SIMULATOR", 2, 0x07FF);
        strip_draw_text(0, 12, 26, "UP/DN: THEME  L/R: CAR  TOOLBOX: CONTROLS", 1, 0xAD55);
      }
      if (strip_y0 == 40) {
        strip_fill_rect(40, 10, 44, 300, 26, 0x1082);
        strip_draw_text(40, 16, 48, "THEME:", 2, 0xFFE0);
        strip_draw_text(40, 72, 48, th->name, 2, 0xFFFF);
        strip_draw_text(40, 72, 60, th->subtitle, 1, 0xAD55);
      }
      if (strip_y0 == 160) {
        strip_fill_rect(160, 10, 176, 300, 22, 0x1082);
        strip_draw_text(160, 16, 182, "CAR:", 2, 0x07E0);
        strip_draw_text(160, 56, 182, cs->name, 2, 0xFFFF);
        strip_draw_text(160, 200, 184, cs->drivetrain, 1, 0x07FF);
      }
      if (strip_y0 == 200) {
        strip_fill_rect(200, 0, 202, SCREEN_W, 38, 0x0841);
        strip_draw_text(200, 12, 210, "OK / EXE: RACE SETUP", 2, 0x07E0);
        strip_draw_text(200, 12, 226, "TOOLBOX / C: VIEW FULL CONTROLS LIST", 1, 0xFFE0);
      }
    } else if (screen_state == SCREEN_CONTROLS) {
      if (strip_y0 == 0) {
        strip_fill_rect(0, 0, 0, SCREEN_W, 36, 0x0841);
        strip_draw_text(0, 12, 8, "CONTROLS LIST", 3, 0xFFE0);
        strip_draw_text(0, 188, 12, "NUMWORKS / PC", 2, 0x07FF);
        strip_draw_text(0, 12, 26, "DRIVER MANUAL AND KEYBINDINGS", 1, 0xAD55);
      }

      if (strip_y0 >= 40 && strip_y0 <= 160) {
        strip_fill_rect(strip_y0, 8, 42, 304, 154, 0x0841);
        strip_fill_rect(strip_y0, 12, 46, 296, 14, 0x2104);
        strip_draw_text(strip_y0, 16, 50, "ACTION", 1, 0xFFE0);
        strip_draw_text(strip_y0, 126, 50, "NUMWORKS KEY", 1, 0x07E0);
        strip_draw_text(strip_y0, 232, 50, "PC KEY", 1, 0x07FF);

        const char *act[9] = {
            "ACCELERATE / GAS",
            "BRAKE / REVERSE",
            "STEER LEFT/RIGHT",
            "E-BRAKE DRIFT",
            "NITRO BOOST",
            "AT / MT GEARBOX",
            "SHIFT UP / DOWN",
            "CAMERA VIEW",
            "GARAGE / PAUSE"
        };
        const char *nw_k[9] = {
            "OK / UP / EXE / *",
            "BACK / DOWN / /",
            "LEFT / RIGHT / 4 6",
            "SHIFT / CLEAR",
            "XNT / ANS / 0 / .",
            "ALPHA",
            "+ / -  OR  ( )",
            "TOOLBOX / VAR",
            "HOME"
        };
        const char *pc_k[9] = {
            "W / UP / ENTER",
            "S / DN / BKSP",
            "A / D / ARROWS",
            "SHIFT",
            "SPACE / X / B",
            "M / TAB",
            "E / Q  OR  + -",
            "C / V / P",
            "ESCAPE"
        };

        for (int r = 0; r < 9; r++) {
          int ry = 64 + r * 14;
          if (r & 1) {
            strip_fill_rect(strip_y0, 12, ry - 2, 296, 13, 0x1082);
          }
          strip_draw_text(strip_y0, 16, ry, act[r], 1, 0xFFFF);
          strip_draw_text(strip_y0, 126, ry, nw_k[r], 1, 0x07E0);
          strip_draw_text(strip_y0, 232, ry, pc_k[r], 1, 0x07FF);
        }
      }

      if (strip_y0 == 200) {
        strip_fill_rect(200, 0, 202, SCREEN_W, 38, 0x0841);
        strip_draw_text(200, 16, 212, "PRESS OK OR BACK TO RETURN", 2, 0x07E0);
        strip_draw_text(200, 16, 228, "HOME / ESC DURING RACE RETURNS TO GARAGE", 1, 0xAD55);
      }
    } else {
      if (strip_y0 == 0) {
        strip_fill_rect(0, 0, 0, SCREEN_W, 36, 0x0841);
        strip_draw_text(0, 12, 8, "RACE SETUP", 3, 0x07E0);
        strip_draw_text(0, 152, 12, "NEW PROCEDURAL TRACK", 2, 0xFFE0);
        strip_draw_text(0, 12, 26, "UP/DN: SELECT  L/R: CHANGE  TOOLBOX: CONTROLS", 1, 0xAD55);
      }

      if (strip_y0 >= 40 && strip_y0 <= 160) {
        strip_fill_rect(strip_y0, 12, 46, 210, 138, 0x0841);
        strip_fill_rect(strip_y0, 228, 46, 80, 138, 0x0841);

        // Row 0: MODE (< RACE > or < INFINITY >)
        eadk_color_t c0 = (setup_cursor == 0) ? 0xFFE0 : 0xFFFF;
        if (setup_cursor == 0) strip_fill_rect(strip_y0, 16, 52, 202, 22, 0x2104);
        strip_draw_text(strip_y0, 22, 58, "MODE:", 2, c0);
        strip_draw_text(strip_y0, 96, 58,
                        infinity_mode ? "< INFINITY >" : "< RACE >",
                        2, infinity_mode ? 0xF81F : 0x07E0);

        // Row 1: LAPS
        eadk_color_t c1 = (setup_cursor == 1) ? 0xFFE0 : 0xFFFF;
        if (setup_cursor == 1) strip_fill_rect(strip_y0, 16, 82, 202, 22, 0x2104);
        strip_draw_text(strip_y0, 22, 88, "LAPS:", 2, c1);
        if (infinity_mode) {
          strip_draw_text(strip_y0, 96, 88, "INFINITE", 2, 0x07FF);
        } else {
          char l_str[8];
          l_str[0] = '<';
          l_str[1] = ' ';
          if (race_total_laps >= 10) {
            l_str[2] = '1'; l_str[3] = '0'; l_str[4] = ' '; l_str[5] = '>'; l_str[6] = '\0';
          } else {
            l_str[2] = '0' + race_total_laps; l_str[3] = ' '; l_str[4] = '>'; l_str[5] = '\0';
          }
          strip_draw_text(strip_y0, 132, 88, l_str, 2, 0x07FF);
        }

        // Row 2: NPC CARS
        eadk_color_t c2 = (setup_cursor == 2) ? 0xFFE0 : 0xFFFF;
        if (setup_cursor == 2) strip_fill_rect(strip_y0, 16, 112, 202, 22, 0x2104);
        strip_draw_text(strip_y0, 22, 118, "NPC CARS:", 2, c2);
        if (infinity_mode) {
          strip_draw_text(strip_y0, 116, 118, "NO BOTS", 2, 0xAD55);
        } else {
          char n_str[8] = {'<', ' ', (char)('0' + race_npc_count), ' ', '>', '\0'};
          strip_draw_text(strip_y0, 132, 118, n_str, 2, 0x07E0);
        }

        // Row 3: COURSE SEED
        eadk_color_t c3 = (setup_cursor == 3) ? 0xFFE0 : 0xFFFF;
        if (setup_cursor == 3) strip_fill_rect(strip_y0, 16, 142, 202, 22, 0x2104);
        strip_draw_text(strip_y0, 22, 148, "COURSE:", 2, c3);
        char s_str[10] = {'<', ' ', 'N', 'E', 'W', ' ', (char)('0' + (current_track_seed % 10)), ' ', '>', '\0'};
        strip_draw_text(strip_y0, 116, 148, s_str, 2, 0xFBE0);

        strip_draw_text(strip_y0, 242, 54, "COURSE MAP", 1, 0xFFE0);
        draw_minimap_strip(strip_y0, 268, 116, false);
      }

      if (strip_y0 == 200) {
        strip_fill_rect(200, 0, 202, SCREEN_W, 38, 0x0841);
        strip_draw_text(200, 16, 212,
                        infinity_mode ? "OK/ENTER: START INFINITY DRIVE!" : "PRESS OK / ENTER TO START THE RACE!",
                        2, 0x07E0);
        strip_draw_text(200, 16, 228, "BACK: GARAGE   TOOLBOX / C: CONTROLS LIST", 1, 0xAD55);
      }
    }

    eadk_rect_t rect = {0, (uint16_t)strip_y0, SCREEN_W, STRIP_H};
    eadk_display_push_rect(rect, fb_strip);
  }
}

static void toggle_infinity_mode(void) {
  if (!infinity_mode) {
    saved_race_laps = (race_total_laps > 0) ? race_total_laps : 3;
    saved_npc_count = race_npc_count;
    infinity_mode = true;
    race_npc_count = 0;
  } else {
    infinity_mode = false;
    race_total_laps = (saved_race_laps > 0) ? saved_race_laps : 3;
    race_npc_count = saved_npc_count;
  }
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  uint32_t boot_ms = (uint32_t)eadk_timing_millis();
  build_track_seeded(0, 101 + boot_ms);

  int screen_state = SCREEN_GARAGE;
  float menu_spin = 2.6f;

  uint64_t prev_kb = 0;
  uint32_t last_ms = boot_ms;
  reset_race_state(last_ms);

  while (true) {
    eadk_keyboard_state_t kb = eadk_keyboard_scan();
    uint64_t just_pressed = kb & (~prev_kb);
    prev_kb = kb;

    uint32_t now_ms = (uint32_t)eadk_timing_millis();
    uint32_t dt_ms = now_ms - last_ms;
    if (dt_ms < 25) {
      eadk_timing_msleep(25 - dt_ms);
      now_ms = (uint32_t)eadk_timing_millis();
      dt_ms = now_ms - last_ms;
    }
    last_ms = now_ms;
    float dt = clampf((float)dt_ms * 0.001f, 0.01f, 0.08f);
    frame_counter++;

    if (screen_state == SCREEN_GARAGE) {
      if (eadk_keyboard_key_down(just_pressed, eadk_key_home)) {
        break;
      }
      if (eadk_keyboard_key_down(just_pressed, eadk_key_toolbox) ||
          eadk_keyboard_key_down(just_pressed, eadk_key_var)) {
        controls_return_screen = SCREEN_GARAGE;
        screen_state = SCREEN_CONTROLS;
        continue;
      }
      if (eadk_keyboard_key_down(just_pressed, eadk_key_left)) {
        selected_car_id = (selected_car_id + NUM_CARS - 1) % NUM_CARS;
      }
      if (eadk_keyboard_key_down(just_pressed, eadk_key_right)) {
        selected_car_id = (selected_car_id + 1) % NUM_CARS;
      }
      if (eadk_keyboard_key_down(just_pressed, eadk_key_up)) {
        current_track_id = (current_track_id + 2) % 3;
      }
      if (eadk_keyboard_key_down(just_pressed, eadk_key_down)) {
        current_track_id = (current_track_id + 1) % 3;
      }
      if (eadk_keyboard_key_down(just_pressed, eadk_key_ok) ||
          eadk_keyboard_key_down(just_pressed, eadk_key_exe)) {
        build_track_seeded(current_track_id, now_ms ^ (frame_counter * 2654435761u));
        setup_cursor = 0;
        screen_state = SCREEN_RACE_SETUP;
      }
      menu_spin += 1.1f * dt;
      render_menu_frame(menu_spin, SCREEN_GARAGE);
    } else if (screen_state == SCREEN_CONTROLS) {
      if (eadk_keyboard_key_down(just_pressed, eadk_key_back) ||
          eadk_keyboard_key_down(just_pressed, eadk_key_home) ||
          eadk_keyboard_key_down(just_pressed, eadk_key_ok) ||
          eadk_keyboard_key_down(just_pressed, eadk_key_exe) ||
          eadk_keyboard_key_down(just_pressed, eadk_key_toolbox) ||
          eadk_keyboard_key_down(just_pressed, eadk_key_var)) {
        screen_state = controls_return_screen;
        continue;
      }
      menu_spin += 1.1f * dt;
      render_menu_frame(menu_spin, SCREEN_CONTROLS);
    } else if (screen_state == SCREEN_RACE_SETUP) {
      if (eadk_keyboard_key_down(just_pressed, eadk_key_back) ||
          eadk_keyboard_key_down(just_pressed, eadk_key_home)) {
        screen_state = SCREEN_GARAGE;
        continue;
      }
      if (eadk_keyboard_key_down(just_pressed, eadk_key_toolbox) ||
          eadk_keyboard_key_down(just_pressed, eadk_key_var)) {
        controls_return_screen = SCREEN_RACE_SETUP;
        screen_state = SCREEN_CONTROLS;
        continue;
      }
      if (eadk_keyboard_key_down(just_pressed, eadk_key_up)) {
        setup_cursor = (setup_cursor + 3) % 4;
      }
      if (eadk_keyboard_key_down(just_pressed, eadk_key_down)) {
        setup_cursor = (setup_cursor + 1) % 4;
      }
      if (eadk_keyboard_key_down(just_pressed, eadk_key_left)) {
        if (setup_cursor == 0) {
          toggle_infinity_mode();
        } else if (setup_cursor == 1) {
          if (!infinity_mode) {
            if (race_total_laps > 1) race_total_laps--;
            else toggle_infinity_mode();
          }
        } else if (setup_cursor == 2 && !infinity_mode && race_npc_count > 0) {
          race_npc_count--;
        } else if (setup_cursor == 3) {
          build_track_seeded(current_track_id, current_track_seed + 17 + now_ms);
        }
      }
      if (eadk_keyboard_key_down(just_pressed, eadk_key_right)) {
        if (setup_cursor == 0) {
          toggle_infinity_mode();
        } else if (setup_cursor == 1) {
          if (infinity_mode) {
            toggle_infinity_mode();
            race_total_laps = 1;
          } else if (race_total_laps < 10) {
            race_total_laps++;
          }
        } else if (setup_cursor == 2 && !infinity_mode && race_npc_count < MAX_NPCS) {
          race_npc_count++;
        } else if (setup_cursor == 3) {
          build_track_seeded(current_track_id, current_track_seed + 31 + now_ms);
        }
      }
      if (eadk_keyboard_key_down(just_pressed, eadk_key_ok) ||
          eadk_keyboard_key_down(just_pressed, eadk_key_exe)) {
        reset_race_state(now_ms);
        screen_state = SCREEN_RACING;
      }
      menu_spin += 1.1f * dt;
      render_menu_frame(menu_spin, SCREEN_RACE_SETUP);
    } else {
      if (eadk_keyboard_key_down(just_pressed, eadk_key_home)) {
        screen_state = SCREEN_GARAGE;
        continue;
      }
      if (race_finished &&
          (eadk_keyboard_key_down(just_pressed, eadk_key_ok) ||
           eadk_keyboard_key_down(just_pressed, eadk_key_exe))) {
        build_track_seeded(current_track_id, now_ms ^ (frame_counter * 1664525u));
        reset_race_state(now_ms);
        continue;
      }
      if (eadk_keyboard_key_down(just_pressed, eadk_key_alpha)) {
        manual_trans = !manual_trans;
      }
      if (eadk_keyboard_key_down(just_pressed, eadk_key_toolbox)) {
        cam_mode = (cam_mode + 1) % 3;
      }
      if (eadk_keyboard_key_down(just_pressed, eadk_key_var)) {
        cam_mode = (cam_mode + 1) % 3;
      }

      bool key_L = eadk_keyboard_key_down(kb, eadk_key_left) ||
                   eadk_keyboard_key_down(kb, eadk_key_four);
      bool key_R = eadk_keyboard_key_down(kb, eadk_key_right) ||
                   eadk_keyboard_key_down(kb, eadk_key_six);
      bool key_gas = eadk_keyboard_key_down(kb, eadk_key_ok) ||
                     eadk_keyboard_key_down(kb, eadk_key_up) ||
                     eadk_keyboard_key_down(kb, eadk_key_exe) ||
                     eadk_keyboard_key_down(kb, eadk_key_multiplication);
      bool key_brake = eadk_keyboard_key_down(kb, eadk_key_back) ||
                       eadk_keyboard_key_down(kb, eadk_key_down) ||
                       eadk_keyboard_key_down(kb, eadk_key_division);
      bool key_ebrake = eadk_keyboard_key_down(kb, eadk_key_shift) ||
                        eadk_keyboard_key_down(kb, eadk_key_backspace);
      bool key_boost = eadk_keyboard_key_down(kb, eadk_key_xnt) ||
                       eadk_keyboard_key_down(kb, eadk_key_ans) ||
                       eadk_keyboard_key_down(kb, eadk_key_zero) ||
                       eadk_keyboard_key_down(kb, eadk_key_dot) ||
                       eadk_keyboard_key_down(kb, eadk_key_exp);

      bool shift_up = eadk_keyboard_key_down(just_pressed, eadk_key_plus) ||
                      eadk_keyboard_key_down(just_pressed, eadk_key_right_parenthesis);
      bool shift_dn = eadk_keyboard_key_down(just_pressed, eadk_key_minus) ||
                      eadk_keyboard_key_down(just_pressed, eadk_key_left_parenthesis);

      step_physics(dt, key_L, key_R, key_gas, key_brake, key_ebrake, key_boost,
                   shift_up, shift_dn, now_ms);
      render_full_frame(key_brake, key_ebrake, now_ms);
    }
  }
  return 0;
}
