/* src/sf.c against the computer's floats: random operands of the sizes Celeste's movement uses, and some edges.
 *   cc -O2 -Isrc tests/sf_test.c src/sf.c -o build/sf_test && build/sf_test */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "ce.h"

static uint32_t bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float flt(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static uint32_t s = 12345;
static uint32_t r32(void) { s ^= s << 13, s ^= s >> 17, s ^= s << 5; return s; }
static float rf(void) {
  switch (r32() % 6) {
    case 0: return (float)((int)(r32() % 2001) - 1000);                 /* whole */
    case 1: return ((int)(r32() % 200001) - 100000) / 64.f;              /* fractions */
    case 2: return flt((r32() & 0x807FFFFF) | ((110 + r32() % 40) << 23));   /* any, 2^-17 .. 2^22 */
    case 3: return ((int)(r32() % 2001) - 1000) * 0.0166667f;            /* speed * DT */
    case 4: return flt((r32() & 0x807FFFFF) | ((120 + r32() % 15) << 23));
    default: return 0.5f + (int)(r32() % 9) - 4;                         /* halves */
  }
}

int main(void) {
  long bad = 0, n = 4000000;
  for (long i = 0; i < n; i++) {
    float a = rf(), b = rf();
    uint32_t A = bits(a), B = bits(b);
    volatile float sum = a + b, dif = a - b, pro = a * b, quo = b != 0 ? a / b : 0;
    if (f_add(A, B) != bits(sum) && !(sum == 0 && f_add(A, B) << 1 == 0)) {
      if (bad++ < 10) printf("add %a + %a = %a, got %a\n", a, b, sum, flt(f_add(A, B)));
    }
    if (f_sub(A, B) != bits(dif) && !(dif == 0 && f_sub(A, B) << 1 == 0)) {
      if (bad++ < 10) printf("sub %a - %a = %a, got %a\n", a, b, dif, flt(f_sub(A, B)));
    }
    if (fabsf(pro) > 1e-30f && f_mul(A, B) != bits(pro)) {
      if (bad++ < 10) printf("mul %a * %a = %a, got %a\n", a, b, pro, flt(f_mul(A, B)));
    }
    if (b != 0 && fabsf(quo) > 1e-30f && f_div(A, B) != bits(quo)) {
      if (bad++ < 10) printf("div %a / %a = %a, got %a\n", a, b, quo, flt(f_div(A, B)));
    }
    if (f_lt(A, B) != (a < b) || f_le(A, B) != (a <= b)) {
      if (bad++ < 10) printf("cmp %a %a\n", a, b);
    }
    if (fabsf(a) < 1e6f) {
      if (f_round(A) != (int)rintf(a) && bad++ < 10) printf("round %a: %d, got %d\n", a, (int)rintf(a), f_round(A));
      if (f_floor(A) != (int)floorf(a) && bad++ < 10) printf("floor %a: %d, got %d\n", a, (int)floorf(a), f_floor(A));
      if (f_trunc(A) != (int)a && bad++ < 10) printf("trunc %a\n", a);
    }
    int k = (int)(r32() % 2000001) - 1000000;
    if (f_int(k) != bits((float)k) && bad++ < 10) printf("int %d\n", k);
  }
  printf("%ld operations each, %ld wrong\n", n, bad);
  return bad != 0;
}
