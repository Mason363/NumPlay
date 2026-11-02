#include "carts.h"
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* a comment line of the code: its text after "--", spaces trimmed, at most n - 1 bytes */
static const char *comment(const char *code, const char *end, char *out, int n) {
  out[0] = 0;
  const char *eol = code;
  while (eol < end && *eol != '\n') eol++;
  const char *p = code;
  while (p < eol && (*p == ' ' || *p == '\t')) p++;
  if (eol - p >= 2 && p[0] == '-' && p[1] == '-' && !(eol - p >= 4 && p[2] == '[' && p[3] == '[')) {
    p += 2;
    while (p < eol && (*p == ' ' || *p == '\t' || *p == '-')) p++;
    const char *q = eol;
    while (q > p && (q[-1] == ' ' || q[-1] == '\t' || q[-1] == '\r')) q--;
    int k = 0;
    for (; p < q && k < n - 1; p++) out[k++] = *p;
    out[k] = 0;
  }
  return eol < end ? eol + 1 : end;
}

int carts_read(const uint8_t *t, uint32_t limit, p8_cart_t *out, int max) {
  if (!t || limit < 12 || rd32(t) != P8_CARTS_MAGIC || rd32(t + 4) != P8_CARTS_VERSION) return -1;
  uint32_t count = rd32(t + 8);
  if (count > (limit - 12) / P8_CARTS_ENTRY) return -1;
  int n = 0;
  for (uint32_t i = 0; i < count && n < max; i++) {
    const uint8_t *e = t + 12 + P8_CARTS_ENTRY * i;
    uint32_t rom = rd32(e), code = rd32(e + 4), len = rd32(e + 8), label = rd32(e + 12);
    if (rom & 3 || rom < 12 || rom > limit || limit - rom < P8_ROM_SIZE) continue;
    if (code > limit || len > limit - code || len > 0x30000) continue;
    if (label && (label > limit || limit - label < P8_LABEL_SIZE)) continue;
    p8_cart_t *c = &out[n++];
    c->rom = t + rom;
    c->code = (const char *)t + code;
    c->code_len = len;
    c->label = label ? t + label : NULL;
    c->name = e + 16;
  }
  return n;
}

void carts_name(const p8_cart_t *c, char *out, int n) {
  int k = 0;
  for (int i = 0; i < P8_CARTS_NAME && c->name[i] && k < n - 1; i++) {
    uint8_t b = c->name[i];
    if ((b & 0xC0) == 0x80) continue; /* (the rest of a letter) */
    out[k++] = b >= 0x20 && b < 0x7F ? (char)b : '?';
  }
  out[k] = 0;
}

void carts_title(const p8_cart_t *c, char *title, int tn, char *author, int an) {
  const char *p = c->code, *end = c->code + c->code_len;
  p = comment(p, end, title, tn);
  author[0] = 0;
  if (title[0]) comment(p, end, author, an);
}

static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }

/* the name as load() gives it: after any path or "#", without .p8 or .p8.png */
static int base_len(const char **s) {
  const char *p = *s;
  for (const char *q = p; *q; q++)
    if (*q == '/' || *q == '\\' || *q == '#') p = q + 1;
  int n = (int)strlen(p);
  if (n >= 7 && lower(p[n - 7]) == '.' && lower(p[n - 6]) == 'p' && p[n - 5] == '8' && p[n - 4] == '.' &&
      lower(p[n - 3]) == 'p' && lower(p[n - 2]) == 'n' && lower(p[n - 1]) == 'g')
    n -= 7;
  else if (n >= 3 && p[n - 3] == '.' && lower(p[n - 2]) == 'p' && p[n - 1] == '8')
    n -= 3;
  *s = p;
  return n;
}

int carts_find(const p8_cart_t *carts, int n, const char *name) {
  if (!name) return -1;
  int len = base_len(&name);
  if (len <= 0) return -1;
  /* the same name, else the BBS's file for it ("#poom_1" is poom_1-0.p8.png when downloaded) */
  for (int pass = 0; pass < 2; pass++)
    for (int i = 0; i < n; i++) {
      char c[P8_CARTS_NAME + 1];
      carts_name(&carts[i], c, sizeof c);
      int k = 0;
      while (k < len && c[k] && lower(c[k]) == lower(name[k])) k++;
      if (k != len) continue;
      if (!c[k]) return i;
      if (pass == 1 && c[k] == '-' && c[k + 1]) {
        k++;
        while (c[k] >= '0' && c[k] <= '9') k++;
        if (!c[k]) return i;
      }
    }
  return -1;
}
