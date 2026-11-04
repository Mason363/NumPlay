/* The CE version on a computer, for tests and pictures: the same 8-bit screen and palette, the appvars read from
 * data/, the keys from a script, the clock a frame at a time (30 a second, two updates each).
 *
 *   CEL_DATA=data/ CEL_KEYS="R*30 RJ*5 _*60" CEL_FRAMES=600 CEL_SHOTS=60,120 CEL_OUT=dir build/host/celeste
 * Keys: L R U D left right up down, J jump (2nd), X dash (clear), G grab (math), P pause (del), O ok, B back, Q quit;
 * "_" none; "*n": for n updates. CEL_START=chapter,room: straight into a room. */
#include "../src/ce.h"
#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>

uint8_t *g_fb;
static uint8_t fbs[2][320 * 240];
static int cur;
static uint16_t pal[256];
static unsigned clock_ticks;
static long updates, frames, max_updates;
static const char *script;
static int script_left;
static unsigned script_keys;
static const char *shots, *out_dir;
uint8_t *g_shown;   /* the buffer on screen */

static unsigned parse_keys(const char **s) {
  unsigned k = 0;
  for (; **s && **s != '*' && **s != ' '; (*s)++) switch (**s) {
      case 'L': k |= K_LEFT; break;
      case 'R': k |= K_RIGHT; break;
      case 'U': k |= K_UP; break;
      case 'D': k |= K_DOWN; break;
      case 'J': k |= K_JUMP | K_OK; break;
      case 'X': k |= K_DASH | K_BACK | K_TALK; break;
      case 'G': k |= K_GRAB; break;
      case 'P': k |= K_PAUSE; break;
      case 'O': k |= K_OK; break;
      case 'B': k |= K_BACK; break;
      case 'Q': k |= K_QUIT; break;
    }
  return k;
}
static void next_step(void) {
  while (script && *script == ' ') script++;
  if (!script || !*script) {
    script_keys = 0, script_left = 1 << 30;
    return;
  }
  script_keys = parse_keys(&script);
  script_left = 1;
  if (*script == '*') script++, script_left = (int)strtol(script, (char **)&script, 10);
}

void plat_init(void) {
  g_fb = fbs[1];
  g_shown = fbs[0];
  script = getenv("CEL_KEYS");
  script_left = 0;
  max_updates = getenv("CEL_FRAMES") ? atol(getenv("CEL_FRAMES")) : 600;
  shots = getenv("CEL_SHOTS");
  out_dir = getenv("CEL_OUT") ? getenv("CEL_OUT") : ".";
}
void plat_exit(void) {}

static void png(const char *path, const uint8_t *fb) {
  static uint8_t raw[240 * (1 + 320 * 3)];
  uint8_t *p = raw;
  for (int y = 0; y < 240; y++) {
    *p++ = 0;
    for (int x = 0; x < 320; x++) {
      uint16_t c = pal[fb[y * 320 + x]];
      int r = c >> 10 & 31, g = c >> 5 & 31, b = c & 31;
      *p++ = (uint8_t)(r << 3 | r >> 2), *p++ = (uint8_t)(g << 3 | g >> 2), *p++ = (uint8_t)(b << 3 | b >> 2);
    }
  }
  uLongf zn = compressBound(sizeof raw);
  uint8_t *z = malloc(zn);
  compress2(z, &zn, raw, sizeof raw, 6);
  FILE *f = fopen(path, "wb");
  if (!f) return;
  static const uint8_t sig[8] = {137, 'P', 'N', 'G', 13, 10, 26, 10};
  fwrite(sig, 1, 8, f);
  uint8_t ihdr[13] = {0, 0, 1, 64, 0, 0, 0, 240, 8, 2, 0, 0, 0};
  struct { const char *t; const uint8_t *d; uLong n; } ch[3] = {{"IHDR", ihdr, 13}, {"IDAT", z, zn}, {"IEND", NULL, 0}};
  for (int i = 0; i < 3; i++) {
    uint8_t len[4] = {(uint8_t)(ch[i].n >> 24), (uint8_t)(ch[i].n >> 16), (uint8_t)(ch[i].n >> 8), (uint8_t)ch[i].n};
    fwrite(len, 1, 4, f);
    fwrite(ch[i].t, 1, 4, f);
    if (ch[i].n) fwrite(ch[i].d, 1, ch[i].n, f);
    uLong crc = crc32(0, (const Bytef *)ch[i].t, 4);
    if (ch[i].n) crc = crc32(crc, ch[i].d, (uInt)ch[i].n);
    uint8_t c[4] = {(uint8_t)(crc >> 24), (uint8_t)(crc >> 16), (uint8_t)(crc >> 8), (uint8_t)crc};
    fwrite(c, 1, 4, f);
  }
  fclose(f);
  free(z);
}

void plat_frame(void) {
  g_shown = g_fb;
  cur ^= 1;
  g_fb = fbs[cur ^ 1];
  frames++;
  clock_ticks += 1093;   /* 30 a second */
  /* the pictures asked for: the frame drawn after update n */
  if (shots) {
    const char *s = shots;
    while (*s) {
      long n = strtol(s, (char **)&s, 10);
      if (n && updates >= n && updates - n < 2) {
        char path[512];
        snprintf(path, sizeof path, "%s/shot_%05ld.png", out_dir, n);
        png(path, g_shown);
      }
      if (*s == ',') s++;
      else break;
    }
  }
}
void plat_begin_draw(void) {}
void plat_palette(const uint16_t *p, int first, int n) { memcpy(pal + first, p, (size_t)n * 2); }

unsigned plat_keys(void) {
  updates++;
  if (getenv("CEL_DEBUG") && updates == 2 && g_level.room) {
    Room *rm = g_level.room;
    int nf = 0, nb = 0, nt = 0;
    for (int i = 0; i < rm->tw * rm->th; i++) nf += rm->grid[i] != 0, nb += rm->grid[rm->tw * rm->th + i] != 0;
    for (int i = 0; i < 320 * 90; i++) nt += g_bg4[i] != 0;
    printf("room %s tw %d th %d ntiles %d fg %d bg %d bg4 %d hasbg %d map %d %d %d\n", level_room_name(), rm->tw, rm->th,
           rm->ntiles, nf, nb, nt, g_has_bg, g_bg_map[0], g_bg_map[1], g_bg_map[2]);
  }
  if (getenv("CEL_TRACE") && g_player.e)
    printf("%ld st %d co %d x %d y %d sx %d sy %d ground %d dashes %d dead %d room %s\n", updates, g_player.state, g_player.co_step,
           g_player.e->x, g_player.e->y, g_player.sx, g_player.sy, g_player.on_ground, g_player.dashes, g_player.dead,
           g_level.room ? level_room_name() : "-");
  if (updates > max_updates) return K_QUIT;
  if (script_left <= 0) next_step();
  script_left--;
  return script_keys;
}
unsigned plat_ticks(void) { return clock_ticks; }
void plat_wait(unsigned t) { clock_ticks += t ? t : 1; }

const uint8_t *plat_appvar(const char *name) {
  static struct { char name[9]; uint8_t *data; } vars[32];
  for (int i = 0; i < 32 && vars[i].data; i++)
    if (!strcmp(vars[i].name, name)) return vars[i].data;
  char path[512];
  snprintf(path, sizeof path, "%s/%s.bin", getenv("CEL_DATA") ? getenv("CEL_DATA") : "data", name);
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *d = malloc((size_t)n);
  if (fread(d, 1, (size_t)n, f) != (size_t)n) n = 0;
  fclose(f);
  for (int i = 0; i < 32; i++)
    if (!vars[i].data) {
      strncpy(vars[i].name, name, 8);
      vars[i].data = d;
      break;
    }
  return d;
}
bool plat_save(const char *name, const void *data, unsigned n) {
  char path[512];
  snprintf(path, sizeof path, "%s/%s.sav", out_dir, name);
  FILE *f = fopen(path, "wb");
  if (!f) return false;
  fwrite(data, 1, n, f);
  fclose(f);
  return true;
}
bool plat_load(const char *name, void *data, unsigned n) {
  char path[512];
  snprintf(path, sizeof path, "%s/%s.sav", out_dir, name);
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  bool ok = fread(data, 1, n, f) == n;
  fclose(f);
  return ok;
}

/* ZX0 (Einar Saukas), as the CE's zx0_Decompress reads it */
static const uint8_t *zin;
static uint8_t zbits, zmask, zlast;
static int zback;
static uint8_t zbyte(void) { return zlast = *zin++; }
static int bit(void) {
  if (zback) {
    zback = 0;
    return zlast & 1;
  }
  zmask >>= 1;
  if (!zmask) zmask = 128, zbits = zbyte();
  return zbits & zmask ? 1 : 0;
}
static int gamma(int inverted) {
  int v = 1;
  while (!bit()) v = v << 1 | (bit() ^ inverted);
  return v;
}
void plat_unpack(void *dst, const void *src) {
  uint8_t *o = dst;
  int last = 1, len;
  zin = src, zmask = 0, zback = 0;
literals:
  len = gamma(0);
  while (len--) *o++ = zbyte();
  if (bit()) goto new_offset;
  len = gamma(0);
  for (int i = 0; i < len; i++, o++) *o = o[-last];
  if (!bit()) goto literals;
new_offset:
  last = gamma(1);
  if (last == 256) return;
  last = last * 128 - (zbyte() >> 1);
  zback = 1;
  len = gamma(0) + 1;
  for (int i = 0; i < len; i++, o++) *o = o[-last];
  if (bit()) goto new_offset;
  goto literals;
}
