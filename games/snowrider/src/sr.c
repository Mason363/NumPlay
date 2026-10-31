/* Snow Rider 3D: Unity's maths (quaternions as Unity builds and blends them, matrices) and its random numbers. */
#include "sr.h"

Quat q_euler(float x, float y, float z) {
  float sx = sinf(x * DEG / 2), cx = cosf(x * DEG / 2), sy = sinf(y * DEG / 2), cy = cosf(y * DEG / 2);
  float sz = sinf(z * DEG / 2), cz = cosf(z * DEG / 2);
  Quat qx = {sx, 0, 0, cx}, qy = {0, sy, 0, cy}, qz = {0, 0, sz, cz};
  return q_mul(q_mul(qy, qx), qz);
}

Quat q_mul(Quat a, Quat b) {
  return (Quat){a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y + a.y * b.w + a.z * b.x - a.x * b.z,
                a.w * b.z + a.z * b.w + a.x * b.y - a.y * b.x, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

Quat q_lerp(Quat a, Quat b, float t) {
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w, s = d < 0 ? -1 : 1;
  Quat q = {a.x + (s * b.x - a.x) * t, a.y + (s * b.y - a.y) * t, a.z + (s * b.z - a.z) * t, a.w + (s * b.w - a.w) * t};
  float l = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  if (l < 1e-9f) return b;
  return (Quat){q.x / l, q.y / l, q.z / l, q.w / l};
}

static void q_mat(Quat q, float r[9]) {
  float x = q.x, y = q.y, z = q.z, w = q.w;
  r[0] = 1 - 2 * (y * y + z * z), r[1] = 2 * (x * y - z * w), r[2] = 2 * (x * z + y * w);
  r[3] = 2 * (x * y + z * w), r[4] = 1 - 2 * (x * x + z * z), r[5] = 2 * (y * z - x * w);
  r[6] = 2 * (x * z - y * w), r[7] = 2 * (y * z + x * w), r[8] = 1 - 2 * (x * x + y * y);
}

void q_rot(Quat q, const float v[3], float out[3]) {
  float r[9];
  q_mat(q, r);
  float x = v[0], y = v[1], z = v[2];
  out[0] = r[0] * x + r[1] * y + r[2] * z, out[1] = r[3] * x + r[4] * y + r[5] * z, out[2] = r[6] * x + r[7] * y + r[8] * z;
}

void m_trs(float m[12], const float t[3], Quat q, const float s[3]) {
  float r[9];
  q_mat(q, r);
  for (int i = 0; i < 3; i++) m[i * 4] = r[i * 3] * s[0], m[i * 4 + 1] = r[i * 3 + 1] * s[1], m[i * 4 + 2] = r[i * 3 + 2] * s[2], m[i * 4 + 3] = t[i];
}

void m_mul(float o[12], const float a[12], const float b[12]) {
  float r[12];
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 4; j++)
      r[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j] + (j == 3 ? a[i * 4 + 3] : 0);
  for (int i = 0; i < 12; i++) o[i] = r[i];
}

void m_point(const float m[12], const float p[3], float o[3]) {
  float x = p[0], y = p[1], z = p[2];
  for (int i = 0; i < 3; i++) o[i] = m[i * 4] * x + m[i * 4 + 1] * y + m[i * 4 + 2] * z + m[i * 4 + 3];
}

void m_dir(const float m[12], const float d[3], float o[3]) {
  float x = d[0], y = d[1], z = d[2];
  for (int i = 0; i < 3; i++) o[i] = m[i * 4] * x + m[i * 4 + 1] * y + m[i * 4 + 2] * z;
}

float m_det(const float m[12]) {
  return m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) + m[2] * (m[4] * m[9] - m[5] * m[8]);
}

bool m_inv(float o[12], const float m[12]) {
  float d = m_det(m);
  if (d > -1e-12f && d < 1e-12f) return false;
  float k = 1 / d;
  float a[9] = {(m[5] * m[10] - m[6] * m[9]) * k, (m[2] * m[9] - m[1] * m[10]) * k, (m[1] * m[6] - m[2] * m[5]) * k,
                (m[6] * m[8] - m[4] * m[10]) * k, (m[0] * m[10] - m[2] * m[8]) * k, (m[2] * m[4] - m[0] * m[6]) * k,
                (m[4] * m[9] - m[5] * m[8]) * k, (m[1] * m[8] - m[0] * m[9]) * k, (m[0] * m[5] - m[1] * m[4]) * k};
  for (int i = 0; i < 3; i++) {
    o[i * 4] = a[i * 3], o[i * 4 + 1] = a[i * 3 + 1], o[i * 4 + 2] = a[i * 3 + 2];
    o[i * 4 + 3] = -(a[i * 3] * m[3] + a[i * 3 + 1] * m[7] + a[i * 3 + 2] * m[11]);
  }
  return true;
}

/* 1 - (1 - amount)^(elapsed * 60): at the game's steps of 1/60 s, the amount itself */
float blend(float amount) { return amount < 0 ? 0 : amount > 1 ? 1 : amount; }

static uint32_t seed = 0x9E3779B9u;
uint32_t rnd(void) {
  if (!seed) seed = plat_random() | 1;
  seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5;
  return seed;
}
float rnd_range(float a, float b) { return a + (b - a) * (float)(rnd() >> 8) * (1.0f / 16777215); }
int rnd_int(int a, int b) { return b <= a ? a : a + (int)(rnd() % (uint32_t)(b - a)); }
void rnd_seed(uint32_t s) { seed = s | 1; }
