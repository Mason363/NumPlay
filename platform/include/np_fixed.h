/* Integer helpers for the calculators without a floating-point unit (NP_SOFT_FLOAT, platform/README.md): what a game's
 * fixed-point paths need without calling the C library's software float. */
#ifndef NP_FIXED_H
#define NP_FIXED_H
#include <stdbool.h>
#include <stdint.h>

/* f * 2^k as an integer, rounded down, read from the float's bits; |f| * 2^k past 2^62 gives 2^62 */
static inline int64_t fx_from_float(float f, int k) {
  union {
    float f;
    uint32_t u;
  } v = {f};
  int e = (int)(v.u >> 23 & 255) - 150 + k;
  bool neg = v.u >> 31 && v.u << 1;
  if (e < -24) return neg ? -1 : 0;
  int64_t m = (int64_t)((v.u & 0x7FFFFF) | 0x800000);
  if (e >= 0) m <<= e > 38 ? 38 : e;
  else if (neg) return -((m + ((int64_t)1 << -e) - 1) >> -e);
  else m >>= -e;
  return neg ? -m : m;
}

/* floor(sqrt(v)), a bit at a time */
static inline uint32_t fx_isqrt(uint32_t v) {
  uint32_t r = 0, b = 1u << 30;
  while (b > v) b >>= 2;
  for (; b; b >>= 2)
    if (v >= r + b) v -= r + b, r = (r >> 1) + b;
    else r >>= 1;
  return r;
}

/* x within an int32_t */
static inline int32_t fx_sat32(int64_t x) { return x > INT32_MAX ? INT32_MAX : x < INT32_MIN ? INT32_MIN : (int32_t)x; }

/* 2^61 / n for n from 2^31 (shift it there with __builtin_clz), to 30 bits: a table's 8 bits, then two Newton steps */
static const uint32_t fx_rcp_seed[128] = {
  0x3FC03FC0, 0x3F423954, 0x3EC62159, 0x3E4BEC88, 0x3DD38FF0, 0x3D5D00F5, 0x3CE8354B, 0x3C7522F3, 0x3C03C03C,
  0x3B9403B9, 0x3B25E446, 0x3AB95900, 0x3A4E5947, 0x39E4DCB8, 0x397CDB2C, 0x39164CB5, 0x38B129A2, 0x384D6A72,
  0x37EB07DD, 0x3789FACB, 0x372A3C56, 0x36CBC5C7, 0x366E9095, 0x36129663, 0x35B7D0FF, 0x355E3A5F, 0x3505CCA2,
  0x34AE820E, 0x3458550F, 0x34034034, 0x33AF3E2E, 0x335C49D4, 0x330A5E1B, 0x32B97617, 0x32698CFF, 0x321A9E24,
  0x31CCA4F5, 0x317F9D00, 0x313381EC, 0x30E84F79, 0x309E0184, 0x30549403, 0x300C0300, 0x2FC44AA2, 0x2F7D6724,
  0x2F3754D7, 0x2EF21023, 0x2EAD9584, 0x2E69E18A, 0x2E26F0DB, 0x2DE4C02D, 0x2DA34C4D, 0x2D629215, 0x2D228E75,
  0x2CE33E6C, 0x2CA49F0A, 0x2C66AD71, 0x2C2966D0, 0x2BECC868, 0x2BB0CF87, 0x2B75798C, 0x2B3AC3E2, 0x2B00AC02,
  0x2AC72F74, 0x2A8E4BCD, 0x2A55FEAD, 0x2A1E45C2, 0x29E71EC5, 0x29B0877D, 0x297A7DBB, 0x2944FF5A, 0x29100A44,
  0x28DB9C68, 0x28A7B3C5, 0x28744E61, 0x28416A4C, 0x280F05A2, 0x27DD1E85, 0x27ABB323, 0x277AC1B2, 0x274A4870,
  0x271A45A6, 0x26EAB7A3, 0x26BB9CBF, 0x268CF359, 0x265EB9DA, 0x2630EEB1, 0x26039055, 0x25D69D43, 0x25AA1402,
  0x257DF31C, 0x25523925, 0x2526E4B7, 0x24FBF471, 0x24D166F9, 0x24A73AFD, 0x247D6F2E, 0x24540245, 0x242AF300,
  0x24024024, 0x23D9E878, 0x23B1EACE, 0x238A45F8, 0x2362F8CF, 0x233C0233, 0x23156107, 0x22EF1432, 0x22C91AA1,
  0x22A37347, 0x227E1D1A, 0x22591713, 0x22346033, 0x220FF77C, 0x21EBDBF5, 0x21C80CAB, 0x21A488AC, 0x21814F0D,
  0x215E5EE4, 0x213BB74D, 0x21195766, 0x20F73E53, 0x20D56B38, 0x20B3DD40, 0x20929398, 0x20718D6F, 0x2050C9F8,
  0x2030486C, 0x20100804,
};
static inline int32_t fx_rcp(uint32_t n) {
  int32_t r = (int32_t)fx_rcp_seed[n >> 24 & 127];
  for (int k = 0; k < 2; k++) {
    int64_t e = (int64_t)(((uint64_t)1 << 61) - (uint64_t)n * (uint32_t)r);
    r += (int32_t)((int64_t)r * (int32_t)(e >> 29) >> 32);
  }
  return r;
}
#endif
