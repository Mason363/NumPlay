/* The compiler helpers libgcc would give (there is none for big-endian ARM here). tools/be_check.py links these both
 * ways, so the two builds compute alike. */
#include <stdint.h>

/* 64-bit division, a bit at a time (no call to a helper in here) */
uint64_t be_udivmod(uint64_t n, uint64_t d, uint64_t *rem) {
  uint64_t q = 0, r = 0;
  if (!d) {
    *rem = n;
    return ~(uint64_t)0;
  }
  for (int i = 63; i >= 0; i--) {
    r = r << 1 | (n >> i & 1);
    if (r >= d) r -= d, q |= (uint64_t)1 << i;
  }
  *rem = r;
  return q;
}
int64_t be_divmod(int64_t n, int64_t d, int64_t *rem) {
  int neg = (n < 0) != (d < 0), rneg = n < 0;
  uint64_t r, q = be_udivmod(n < 0 ? -(uint64_t)n : (uint64_t)n, d < 0 ? -(uint64_t)d : (uint64_t)d, &r);
  *rem = rneg ? -(int64_t)r : (int64_t)r;
  return neg ? -(int64_t)q : (int64_t)q;
}
/* (the EABI's: n in r0, r1, d in r2, r3; the quotient back in r0, r1 and the remainder in r2, r3, as an LDM of
   them from memory would put them, whichever the byte order) */
__asm__(".text\n.arm\n.global __aeabi_uldivmod\n.type __aeabi_uldivmod, %function\n__aeabi_uldivmod:\n"
        "push {r11, lr}\nsub sp, sp, #16\nadd r12, sp, #8\nstr r12, [sp]\nbl be_udivmod\n"
        "add r12, sp, #8\nldm r12, {r2, r3}\nadd sp, sp, #16\npop {r11, pc}\n"
        ".global __aeabi_ldivmod\n.type __aeabi_ldivmod, %function\n__aeabi_ldivmod:\n"
        "push {r11, lr}\nsub sp, sp, #16\nadd r12, sp, #8\nstr r12, [sp]\nbl be_divmod\n"
        "add r12, sp, #8\nldm r12, {r2, r3}\nadd sp, sp, #16\npop {r11, pc}\n");

/* integers of 64 bits to and from floating point (the EABI's helpers take and give them in core registers) */
#define AAPCS __attribute__((pcs("aapcs")))
AAPCS double __aeabi_ul2d(uint64_t u) { return (double)(uint32_t)(u >> 32) * 4294967296.0 + (double)(uint32_t)u; }
AAPCS double __aeabi_l2d(int64_t x) { return x < 0 ? -__aeabi_ul2d(-(uint64_t)x) : __aeabi_ul2d((uint64_t)x); }
AAPCS float __aeabi_ul2f(uint64_t u) { return (float)__aeabi_ul2d(u); }
AAPCS float __aeabi_l2f(int64_t x) { return (float)__aeabi_l2d(x); }
AAPCS uint64_t __aeabi_d2ulz(double d) {
  if (!(d > 0)) return 0;
  if (d >= 18446744073709551616.0) return ~(uint64_t)0;
  uint32_t hi = (uint32_t)(d * (1.0 / 4294967296.0));
  return (uint64_t)hi << 32 | (uint32_t)(d - (double)hi * 4294967296.0);
}
AAPCS int64_t __aeabi_d2lz(double d) { return d < 0 ? -(int64_t)__aeabi_d2ulz(-d) : (int64_t)__aeabi_d2ulz(d); }
AAPCS uint64_t __aeabi_f2ulz(float f) { return __aeabi_d2ulz((double)f); }
AAPCS int64_t __aeabi_f2lz(float f) { return __aeabi_d2lz((double)f); }

/* bit counts */
int __popcountsi2(uint32_t x) {
  int n = 0;
  for (; x; x &= x - 1) n++;
  return n;
}
int __popcountdi2(uint64_t x) { return __popcountsi2((uint32_t)x) + __popcountsi2((uint32_t)(x >> 32)); }
int __paritysi2(uint32_t x) { return __popcountsi2(x) & 1; }
int __clzdi2(uint64_t x) { return x >> 32 ? __builtin_clz((uint32_t)(x >> 32)) : 32 + __builtin_clz((uint32_t)x); }
int __ctzdi2(uint64_t x) { return (uint32_t)x ? __builtin_ctz((uint32_t)x) : 32 + __builtin_ctz((uint32_t)(x >> 32)); }
