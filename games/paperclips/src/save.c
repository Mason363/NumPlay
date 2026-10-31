/* save.h: anything damaged, cut short or from elsewhere is not read: the copy in the Python script is tried, then a
 * fresh start. Past the checksum, every number the game uses as a place in a table is checked too. */
#include <string.h>
#include "plat.h"
#include "save.h"

uint32_t pc_sum(const void *data, uint32_t n) {
  const uint8_t *p = data;
  uint32_t h = 2166136261u;
  for (uint32_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}

static bool ended(const char *s, int n) { return memchr(s, 0, (size_t)n) != 0; }
static bool soon(const pc_game *s, uint32_t at, uint32_t most) {
  uint32_t d = at - s->now;
  return d >= 1 && d <= most;
}

bool pc_game_ok(const pc_game *s) {
  if (s->nstrats < 1 || s->nstrats > PC_STRATS) return false;
  if (s->pick != 10 && (s->pick < 0 || s->pick >= s->nstrats)) return false;
  if (s->pickChoice != 10 && (s->pickChoice < 0 || s->pickChoice >= s->nstrats)) return false;
  if (s->riskChoice < 0 || s->riskChoice > 2 || s->nstocks < 0 || s->nstocks > PC_STOCKS) return false;
  if (s->nactive < 0 || s->nactive > PC_PROJECTS || s->nresults < 0 || s->nresults > s->nstrats) return false;
  uint8_t seen[PC_PROJECTS] = {0};
  for (int i = 0; i < s->nactive; i++) {
    if (s->active[i] >= PC_PROJECTS || seen[s->active[i]]) return false;
    seen[s->active[i]] = 1;
  }
  for (int i = 0; i < s->nresults; i++)
    if (s->results[i] < 0 || s->results[i] >= s->nstrats) return false;
  if (s->battles < 0 || s->battles > 1 || s->tourneyStep < 0 || s->tourneyStep > 2) return false;
  if (s->h < -1 || s->h >= s->nstrats || s->v < -1 || s->v >= s->nstrats) return false;
  if (s->tourneyStep && (s->h < 0 || s->v < 0)) return false;
  if (s->lastCell < 0 || s->lastCell > 4 || s->gridLabel < 0 || s->gridLabel > 16) return false;
  if (s->nIncome < 1 || s->nIncome > 10 || s->hypnoText < 0 || s->hypnoText > 3) return false;
  if (s->blinkProject < -1 || s->blinkProject >= PC_PROJECTS) return false;
  if (!(s->nextQchip >= 0 && s->nextQchip <= 11) || !(s->winnerPtr >= 0 && s->winnerPtr < PC_STRATS)) return false;
  for (int i = 0; i < PC_STRATS; i++)
    if (s->stratPos[i] < 1 || s->stratPos[i] > 2) return false;
  for (int i = 0; i < PC_CHIPS; i++)
    if (s->qChipActive[i] > 1) return false;
  for (int i = 0; i < PC_PROJECTS; i++)
    if (s->flag[i] > 1) return false;
  if (!soon(s, s->at_main, 10) || !soon(s, s->at_slow, 100) || !soon(s, s->at_battle, 16)) return false;
  if (!soon(s, s->at_stocks, 100) || !soon(s, s->at_shop, 1000) || !soon(s, s->at_sell, 2500)) return false;
  if (!soon(s, s->at_pick, 100)) return false;
  if (s->tourneyStep && !soon(s, s->at_tourney, 50)) return false;
  if (s->hypnoOn && !soon(s, s->at_hypno, 32)) return false;
  if (s->blinkProject >= 0 && !soon(s, s->at_blink, 30)) return false;
  for (int i = 0; i < PC_STOCKS; i++)
    if (!ended(s->stocks[i].symbol, sizeof s->stocks[i].symbol)) return false;
  if (!ended(s->battleName, PC_NAME_LEN) || !ended(s->threnodyTitle, PC_NAME_LEN)) return false;
  if (!ended(s->project133Title, PC_NAME_LEN) || !ended(s->tourneyText, sizeof s->tourneyText)) return false;
  return s->resetPending == 0;
}

bool pc_file_ok(const uint8_t *f, uint32_t n) {
  pc_head h;
  if (n != PC_FILE_SIZE) return false;
  memcpy(&h, f, sizeof h);
  if (h.magic != PC_MAGIC || h.version != PC_VERSION || h.size != PC_FILE_SIZE) return false;
  if (h.guide > 1 || h.tab > 10) return false;
  uint32_t sum;
  memcpy(&sum, f + PC_FILE_SIZE - 4, 4);
  if (sum != pc_sum(f, PC_FILE_SIZE - 4)) return false;
  static pc_game t;   /* (static: the stack stays small) */
  memset(&t, 0, sizeof t);
  memcpy(&t, f + sizeof h, PC_KEPT);
  return pc_game_ok(&t);
}

void save_pack(const pc_game *s, uint8_t guide, uint8_t tab, uint8_t *out) {
  pc_head h = {PC_MAGIC, PC_VERSION, (uint16_t)PC_FILE_SIZE, guide, tab, {0, 0}};
  memcpy(out, &h, sizeof h);
  memcpy(out + sizeof h, s, PC_KEPT);
  uint32_t sum = pc_sum(out, PC_FILE_SIZE - 4);
  memcpy(out + PC_FILE_SIZE - 4, &sum, 4);
}

static const char head[] = "# Universal Paperclips keeps a copy of your game here,\n"
                           "# so that installing it again doesn't erase it.\n";
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static uint8_t file[PC_FILE_SIZE];
static uint8_t copy[1 + sizeof head + 2 + sizeof PC_SAVE_NAME + 1 + (PC_FILE_SIZE + 2) / 3 * 4 + 2];

static bool copy_write(void) {
  uint32_t len = PC_FILE_SIZE, nl = sizeof PC_SAVE_NAME - 1;
  uint8_t *p = copy;
  *p++ = 0;   /* (a script's first byte: not imported) */
  memcpy(p, head, sizeof head - 1), p += sizeof head - 1;
  *p++ = '#', *p++ = '>';
  memcpy(p, PC_SAVE_NAME, nl), p += nl;
  *p++ = ':';
  for (uint32_t i = 0; i < len; i += 3, p += 4) {
    uint32_t left = len - i;
    uint32_t v = (uint32_t)file[i] << 16 | (left > 1 ? (uint32_t)file[i + 1] << 8 : 0) | (left > 2 ? file[i + 2] : 0);
    p[0] = (uint8_t)b64[v >> 18 & 63];
    p[1] = (uint8_t)b64[v >> 12 & 63];
    p[2] = left > 1 ? (uint8_t)b64[v >> 6 & 63] : '=';
    p[3] = left > 2 ? (uint8_t)b64[v & 63] : '=';
  }
  *p++ = '\n';
  return plat_save(PC_COPY_NAME, copy, (uint32_t)(p - copy));
}

static int b64v(uint8_t c) {
  for (int i = 0; i < 64; i++)
    if ((uint8_t)b64[i] == c) return i;
  return -1;
}

static bool copy_read(void) {
  uint32_t n, nl = sizeof PC_SAVE_NAME - 1;
  const uint8_t *c = plat_load(PC_COPY_NAME, &n);
  if (!c) return false;
  for (uint32_t i = 0; i + nl + 3 <= n; i++) {
    if (c[i] != '#' || c[i + 1] != '>' || memcmp(c + i + 2, PC_SAVE_NAME, nl) || c[i + 2 + nl] != ':') continue;
    const uint8_t *l = c + i + 3 + nl;
    uint32_t m = 0;
    while (l + m < c + n && l[m] != '\n' && l[m]) m++;
    if (m != (PC_FILE_SIZE + 2) / 3 * 4) return false;
    uint32_t o = 0;
    for (uint32_t k = 0; k < m; k += 4) {
      int v0 = b64v(l[k]), v1 = b64v(l[k + 1]), v2 = l[k + 2] == '=' ? 0 : b64v(l[k + 2]),
          v3 = l[k + 3] == '=' ? 0 : b64v(l[k + 3]);
      if (v0 < 0 || v1 < 0 || v2 < 0 || v3 < 0) return false;
      uint32_t w = (uint32_t)(v0 << 18 | v1 << 12 | v2 << 6 | v3);
      if (o < PC_FILE_SIZE) file[o++] = (uint8_t)(w >> 16);
      if (o < PC_FILE_SIZE) file[o++] = (uint8_t)(w >> 8);
      if (o < PC_FILE_SIZE) file[o++] = (uint8_t)w;
    }
    return true;
  }
  return false;
}

bool save_load(pc_game *s, uint8_t *guide, uint8_t *tab) {
  uint32_t n;
  const uint8_t *d = plat_load(PC_SAVE_NAME, &n);
  bool ok = false;
  if (d && n == PC_FILE_SIZE) {
    memcpy(file, d, PC_FILE_SIZE);
    ok = pc_file_ok(file, PC_FILE_SIZE);
  }
  if (!ok && copy_read() && pc_file_ok(file, PC_FILE_SIZE)) {
    ok = true;
    plat_save(PC_SAVE_NAME, file, PC_FILE_SIZE);   /* back where it belongs */
  }
  if (!ok) return false;
  pc_head h;
  memcpy(&h, file, sizeof h);
  memset(s->msg, 0, sizeof s->msg);
  memcpy(s, file + sizeof h, PC_KEPT);
  strcpy(s->msg[0], "Welcome to Universal Paperclips");
  *guide = h.guide, *tab = h.tab;
  return true;
}

bool save_write(const pc_game *s, uint8_t guide, uint8_t tab) {
  save_pack(s, guide, tab, file);
  bool ok = plat_save(PC_SAVE_NAME, file, PC_FILE_SIZE);
  copy_write();
  return ok;
}
