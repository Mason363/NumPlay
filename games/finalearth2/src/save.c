/* The Final Earth 2: saving the city in the calculator's file system.
 *
 * The calculator keeps 42 KB of files for everything (Python scripts
 * included), so the save is packed: the city is written as a stream of bytes
 * cut into 4 KB chunks, each packed on its own (np_lz.h). It is written twice:
 * once only to measure it, then into the file's room in the storage itself
 * (the storage is RAM), so the stream never needs RAM of its own beyond a
 * chunk. Paths in progress are not kept: citizens ask for new ones.
 *
 * The file: "FE2S", version, the stream's length and checksum, then each
 * chunk as its packed length (2 bytes) and its packed bytes. */
#include <stddef.h>
#include "bld.h"
#include "../../common/np_lz.h"
#include "save.h"
#include "scratch.h"
#include "classes.h"
#include "story.h"
#include "unlocks.h"
#include "view.h"

SavedView saved_view;

#define SAVE_NAME "finalearth2.sav"
#define SAVE_MAGIC 0x53324546u   /* "FE2S" */
#define SAVE_VERSION 2   /* (2: every class's job priority; 1, the first 64 only, still loads) */
static int load_version = SAVE_VERSION;   /* the version of the file being read */
#define CHUNK 4096

/* ---------------------------------------------------------------- the stream */
struct SaveIO {
  uint8_t *out;          /* where packed chunks go (NULL: only measuring) */
  uint32_t outlen;       /* packed bytes so far (after the header) */
  uint32_t chunklen;     /* bytes in the current chunk */
  uint32_t total, crc;   /* the stream's length and checksum */
  /* reading */
  const uint8_t *in;
  uint32_t inlen, inpos, chunkpos;
  bool error;
};

static uint32_t crc_step(uint32_t crc, uint8_t b) {
  crc ^= b;
  for (int k = 0; k < 8; k++) crc = crc >> 1 ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1));
  return crc;
}

static void flush_chunk(SaveIO *io) {
  if (!io->chunklen) return;
  uint8_t *dst = io->out ? io->out + 16 + io->outlen + 2 : NULL;
  uint32_t n = np_lz_pack(scratch->save.chunk, io->chunklen, dst, scratch->save.table);
  if (io->out) {
    io->out[16 + io->outlen] = (uint8_t)n;
    io->out[16 + io->outlen + 1] = (uint8_t)(n >> 8);
  }
  io->outlen += 2 + n;
  io->chunklen = 0;
}
void save_bytes(SaveIO *io, const void *data, uint32_t n) {
  const uint8_t *p = data;
  for (uint32_t i = 0; i < n; i++) {
    io->crc = crc_step(io->crc, p[i]);
    scratch->save.chunk[io->chunklen++] = p[i];
    if (io->chunklen == CHUNK) flush_chunk(io);
  }
  io->total += n;
}
void save_u8(SaveIO *io, uint8_t v) { save_bytes(io, &v, 1); }
void save_u16(SaveIO *io, uint16_t v) { save_bytes(io, &v, 2); }
void save_u32(SaveIO *io, uint32_t v) { save_bytes(io, &v, 4); }
void save_f32(SaveIO *io, float v) { save_bytes(io, &v, 4); }
void save_f64(SaveIO *io, double v) { save_bytes(io, &v, 8); }

/* the next byte of the stream, unpacking the next chunk when needed */
static bool next_chunk(SaveIO *io) {
  if (io->inpos + 2 > io->inlen) return false;
  uint32_t n = io->in[io->inpos] | (uint32_t)io->in[io->inpos + 1] << 8;
  io->inpos += 2;
  if (io->inpos + n > io->inlen) return false;
  int32_t got = np_lz_unpack(io->in + io->inpos, n, scratch->save.chunk, CHUNK);
  io->inpos += n;
  if (got <= 0) return false;
  io->chunklen = (uint32_t)got;
  io->chunkpos = 0;
  return true;
}
bool load_bytes(SaveIO *io, void *data, uint32_t n) {
  uint8_t *p = data;
  for (uint32_t i = 0; i < n; i++) {
    if (io->chunkpos >= io->chunklen && !next_chunk(io)) {
      io->error = true;
      return false;
    }
    p[i] = scratch->save.chunk[io->chunkpos++];
    io->crc = crc_step(io->crc, p[i]);
    io->total++;
  }
  return true;
}
uint8_t load_u8(SaveIO *io) { uint8_t v = 0; load_bytes(io, &v, 1); return v; }
uint16_t load_u16(SaveIO *io) { uint16_t v = 0; load_bytes(io, &v, 2); return v; }
uint32_t load_u32(SaveIO *io) { uint32_t v = 0; load_bytes(io, &v, 4); return v; }
float load_f32(SaveIO *io) { float v = 0; load_bytes(io, &v, 4); return v; }
double load_f64(SaveIO *io) { double v = 0; load_bytes(io, &v, 8); return v; }

/* ---------------------------------------------------------------- the city */
extern uint8_t col_deco[MAX_COLS];
void game_reset(void);

/* Permanents and citizens are written field by field: each field of all of them, then the next field (a
 * byte of a 16-bit field at a time). Like values then sit together, and they pack about twice as small. Their
 * ids come first, as steps from the one before. */
#define EACH_PERM(i) for (int i = 1; i < MAX_PERMS; i++) if (perms[i].flags & PF_USED)
#define EACH_CIT(i) for (int i = 1; i < MAX_CITS; i++) if (cits[i].flags & CF_ALIVE)
#define PLANE8(each, i, v) each(i) save_u8(io, (uint8_t)(v))
#define PLANE16(each, i, v) do { PLANE8(each, i, (uint16_t)(v)); PLANE8(each, i, (uint16_t)(v) >> 8); } while (0)
#define LOAD8(list, n, k, dst) for (int k = 0; k < n; k++) dst = load_u8(io)
#define LOAD16(list, n, k, dst) do { \
    for (int k = 0; k < n; k++) dst = load_u8(io); \
    for (int k = 0; k < n; k++) dst = (uint16_t)((uint16_t)(dst) | load_u8(io) << 8); \
  } while (0)

static void save_ids(SaveIO *io, int count, int max, bool (*used)(int)) {
  save_u16(io, (uint16_t)count);
  int last = 0;
  for (int i = 1; i < max; i++)
    if (used(i)) {
      if (i - last < 255) save_u8(io, (uint8_t)(i - last));
      else save_u8(io, 255), save_u16(io, (uint16_t)i);
      last = i;
    }
}
/* the ids, into list (increasing); their count, or -1 if they are not right */
static int load_ids(SaveIO *io, uint16_t *list, int max) {
  int n = load_u16(io), last = 0;
  if (n >= max) return -1;
  for (int k = 0; k < n; k++) {
    int d = load_u8(io), id = d == 255 ? load_u16(io) : last + d;
    if (id <= last || id >= max) return -1;
    list[k] = (uint16_t)(last = id);
  }
  return io->error ? -1 : n;
}
static bool perm_used(int i) { return perms[i].flags & PF_USED; }
static bool cit_alive(int i) { return cits[i].flags & CF_ALIVE; }

/* the permanents: what they are, where, who is in them and their own state (their neighbours and groups are made
 * again) */
static void write_perms(SaveIO *io) {
  save_ids(io, perm_count(), MAX_PERMS, perm_used);
  PLANE8(EACH_PERM, i, perms[i].cls);
  PLANE8(EACH_PERM, i, perms[i].world);
  PLANE8(EACH_PERM, i, perms[i].col);
  PLANE8(EACH_PERM, i, perms[i].row);
  PLANE8(EACH_PERM, i, perms[i].flags);
  PLANE8(EACH_PERM, i, perms[i].upgrades);
  PLANE8(EACH_PERM, i, perms[i].mode);
  PLANE8(EACH_PERM, i, perms[i].bonusAttractiveness);
  PLANE16(EACH_PERM, i, perms[i].bonusEntertainmentCapacity);
  PLANE8(EACH_PERM, i, perms[i].nworkers);
  PLANE8(EACH_PERM, i, perms[i].nresidents);
  PLANE8(EACH_PERM, i, perms[i].nstudents);
  PLANE8(EACH_PERM, i, perms[i].nvisitors);
  for (int b = 0; b < (int)sizeof perms[0].s; b++) PLANE8(EACH_PERM, i, perms[i].s[b]);
}
static bool read_perms(SaveIO *io) {
  uint16_t list[MAX_PERMS];
  int n = load_ids(io, list, MAX_PERMS);
  if (n < 0) return false;
  /* (read into the permanents' places, then each one restored from there) */
  LOAD8(list, n, k, perms[list[k]].cls);
  LOAD8(list, n, k, perms[list[k]].world);
  LOAD8(list, n, k, perms[list[k]].col);
  LOAD8(list, n, k, perms[list[k]].row);
  LOAD8(list, n, k, perms[list[k]].flags);
  LOAD8(list, n, k, perms[list[k]].upgrades);
  LOAD8(list, n, k, perms[list[k]].mode);
  LOAD8(list, n, k, perms[list[k]].bonusAttractiveness);
  LOAD16(list, n, k, perms[list[k]].bonusEntertainmentCapacity);
  LOAD8(list, n, k, perms[list[k]].nworkers);
  LOAD8(list, n, k, perms[list[k]].nresidents);
  LOAD8(list, n, k, perms[list[k]].nstudents);
  LOAD8(list, n, k, perms[list[k]].nvisitors);
  for (int b = 0; b < (int)sizeof perms[0].s; b++) LOAD8(list, n, k, perms[list[k]].s[b]);
  if (io->error) return false;
  for (int k = 0; k < n; k++) {
    Perm p = perms[list[k]];
    if (p.cls >= npcls || !(p.flags & PF_USED)) return false;
    memset(&perms[list[k]], 0, sizeof p);
    if (!perm_restore(list[k], &p)) return false;
  }
  return true;
}

/* the citizens: what they are, where they are and what they do; not their paths */
static void write_cits(SaveIO *io) {
  save_ids(io, ncits, MAX_CITS, cit_alive);
  PLANE16(EACH_CIT, i, cits[i].inPermanent);
  PLANE16(EACH_CIT, i, cits[i].home);
  PLANE16(EACH_CIT, i, cits[i].job);
  PLANE16(EACH_CIT, i, cits[i].school);
  PLANE8(EACH_CIT, i, cits[i].onWorld);
  PLANE16(EACH_CIT, i, (int16_t)(cits[i].relativeX * 8));
  PLANE8(EACH_CIT, i, cits[i].relativeY < 0 ? 0 : cits[i].relativeY > 31 ? 31 : cits[i].relativeY);
  /* the age in 4-minute steps */
  #define AGE(c) ((city.timeSinceStart - (c).bornOn) / 4.0 > 65535 ? 65535 : (city.timeSinceStart - (c).bornOn) / 4.0)
  PLANE16(EACH_CIT, i, AGE(cits[i]));
  PLANE16(EACH_CIT, i, cits[i].educationLevel * 256 > 65535 ? 65535 : cits[i].educationLevel * 256);
  PLANE16(EACH_CIT, i, cits[i].dieAgeModifier);
  PLANE16(EACH_CIT, i, cits[i].nameIndex);
  PLANE8(EACH_CIT, i, cits[i].spriteIndex);
  PLANE8(EACH_CIT, i, cits[i].workTimePref);
  PLANE16(EACH_CIT, i, cits[i].flags & (CF_LIKES_NIGHTCLUBS | CF_FORCED_HOME | CF_WITHOUT_JOB | CF_HAS_WORK_TOOLS | CF_CONTROLLED));
  PLANE8(EACH_CIT, i, cits[i].currentAction);
  PLANE8(EACH_CIT, i, cits[i].wantsNightEntertainmentIn);
  PLANE8(EACH_CIT, i, cits[i].hobby);
}
static bool read_cits(SaveIO *io) {
  uint16_t list[MAX_CITS], tmp[MAX_CITS];
  int n = load_ids(io, list, MAX_CITS);
  if (n < 0) return false;
  for (int k = 0; k < n; k++) memset(&cits[list[k]], 0, sizeof cits[0]);
  LOAD16(list, n, k, cits[list[k]].inPermanent);
  LOAD16(list, n, k, cits[list[k]].home);
  LOAD16(list, n, k, cits[list[k]].job);
  LOAD16(list, n, k, cits[list[k]].school);
  LOAD8(list, n, k, cits[list[k]].onWorld);
  /* (16-bit values kept in other forms: read whole, then set) */
  #define LOAD16_SET(set) do { \
      LOAD16(list, n, k, tmp[k]); \
      for (int k = 0; k < n; k++) { Cit *c = &cits[list[k]]; uint16_t v = tmp[k]; set; } \
    } while (0)
  LOAD16_SET(c->relativeX = (int16_t)v / 8.0f);
  LOAD8(list, n, k, cits[list[k]].relativeY);
  LOAD16_SET(c->bornOn = (float)(city.timeSinceStart - v * 4.0));
  LOAD16_SET(c->educationLevel = v / 256.0f);
  LOAD16_SET(c->dieAgeModifier = (int16_t)v);
  LOAD16(list, n, k, cits[list[k]].nameIndex);
  LOAD8(list, n, k, cits[list[k]].spriteIndex);
  LOAD8(list, n, k, cits[list[k]].workTimePref);
  LOAD16(list, n, k, cits[list[k]].flags);
  LOAD8(list, n, k, cits[list[k]].currentAction);
  LOAD8(list, n, k, cits[list[k]].wantsNightEntertainmentIn);
  LOAD8(list, n, k, cits[list[k]].hobby);
  if (io->error) return false;
  for (int k = 0; k < n; k++) {
    Cit *c = &cits[list[k]];
    if (c->inPermanent >= MAX_PERMS || c->home >= MAX_PERMS || c->job >= MAX_PERMS || c->school >= MAX_PERMS ||
        c->onWorld >= nworlds)
      return false;
    c->flags = (uint16_t)(c->flags | CF_ALIVE | CF_CAN_VIEW_SELF | CF_PATH_RECONSIDER);
    c->currentPathAction = -2;
    c->pathWalkSpeed = 100;
    /* (a citizen who was being carried somewhere is let go where they are) */
    c->flags &= (uint16_t)~CF_CONTROLLED;
    if (c->currentAction == 3) c->currentAction = 2;
  }
  return true;
}

static void write_city(SaveIO *io) {
  /* the story and the time */
  save_u8(io, story.index);
  save_bytes(io, &story, sizeof story);
  save_f64(io, city.timeSinceStart);
  save_bytes(io, &city, sizeof city);
  save_bytes(io, &unlocks, sizeof unlocks);
  save_bytes(io, &happiness, sizeof happiness);
  save_bytes(io, &stats, sizeof stats);
  save_bytes(io, &eating, sizeof eating);
  save_bytes(io, &babies, sizeof babies);
  save_bytes(io, &bonuses, sizeof bonuses);
  save_bytes(io, &policies, sizeof policies);
  save_bytes(io, &upgrades, sizeof upgrades);
  save_bytes(io, &operating_cost, sizeof operating_cost);
  extern int priority_jobs[CLS_COUNT];
  save_bytes(io, priority_jobs, sizeof(int) * CLS_COUNT);
  /* the worlds and their columns */
  save_u8(io, (uint8_t)nworlds);
  for (int w = 0; w < nworlds; w++) {
    save_bytes(io, &worlds[w], sizeof worlds[w]);
    save_bytes(io, col_height + worlds[w].col0, worlds[w].ncols);
    save_bytes(io, col_deco + worlds[w].col0, worlds[w].ncols);
  }
  write_perms(io);
  write_cits(io);
  bld_save(io);
  boosts_save(io);
  /* the view: viewPos and zoomScale (as the camera's top-left), and the cursor */
  save_f32(io, camera.x), save_f32(io, camera.y);
  save_u8(io, (uint8_t)camera.zoom);
  save_u16(io, (uint16_t)saved_view.cx), save_u16(io, (uint16_t)saved_view.cy);
  save_u32(io, 0x454E44u);   /* "END" */
}


static bool read_city(SaveIO *io) {
  int index = load_u8(io);
  if (index >= nstories) return false;
  game_reset();
  load_bytes(io, &story, sizeof story);
  /* (the running SpawnCitizensRegularly parts point into the story's data: their places are kept by index) */
  city.timeSinceStart = load_f64(io);
  load_bytes(io, &city, sizeof city);
  city.cityName[CITY_NAME_MAX] = 0;
  load_bytes(io, &unlocks, sizeof unlocks);
  load_bytes(io, &happiness, sizeof happiness);
  load_bytes(io, &stats, sizeof stats);
  load_bytes(io, &eating, sizeof eating);
  load_bytes(io, &babies, sizeof babies);
  load_bytes(io, &bonuses, sizeof bonuses);
  load_bytes(io, &policies, sizeof policies);
  load_bytes(io, &upgrades, sizeof upgrades);
  load_bytes(io, &operating_cost, sizeof operating_cost);
  extern int priority_jobs[CLS_COUNT];
  memset(priority_jobs, 0, sizeof priority_jobs);
  load_bytes(io, priority_jobs, sizeof(int) * (load_version >= 2 ? CLS_COUNT : 64));
  int nw = load_u8(io);
  if (nw > MAX_WORLDS) return false;
  for (int w = 0; w < nw; w++) {
    World wd;
    load_bytes(io, &wd, sizeof wd);
    int id = world_add(wd.x, wd.y, wd.w, wd.h, wd.seed, wd.appearance);
    if (id != w) return false;
    worlds[w].flags = wd.flags;
    worlds[w].id = wd.id;
    load_bytes(io, col_height + worlds[w].col0, worlds[w].ncols);
    load_bytes(io, col_deco + worlds[w].col0, worlds[w].ncols);
  }
  if (!read_perms(io) || !read_cits(io)) return false;
  cits_recount();
  if (!bld_load(io)) return false;
  saved_view.valid = false;
  if (!boosts_load(io)) return false;
  saved_view.x = load_f32(io), saved_view.y = load_f32(io);
  saved_view.zoom = load_u8(io);
  saved_view.cx = (int16_t)load_u16(io), saved_view.cy = (int16_t)load_u16(io);
  saved_view.valid = saved_view.zoom == 1 || saved_view.zoom == 2;
  return load_u32(io) == 0x454E44u && !io->error;
}

/* ---------------------------------------------------------------- the file */
bool save_game(void) {
  SaveIO io = {0};
  write_city(&io);
  flush_chunk(&io);
  uint32_t size = 16 + io.outlen;
  uint8_t *dst = plat_reserve(SAVE_NAME, size);
  if (!dst) {   /* the copy gives way to the save */
    save_copy_drop();
    dst = plat_reserve(SAVE_NAME, size);
    if (!dst) return false;
  }
  uint32_t total = io.total, crc = io.crc;
  memset(&io, 0, sizeof io);
  io.out = dst;
  write_city(&io);
  flush_chunk(&io);
  if (io.total != total || io.crc != crc || 16 + io.outlen != size) return false;
  uint32_t head[4] = {SAVE_MAGIC, SAVE_VERSION, total, crc};
  memcpy(dst, head, 16);
  plat_reserved(SAVE_NAME, size);
  save_copy();
  return true;
}

bool save_exists(void) {
  uint32_t len;
  const uint8_t *d = plat_load(SAVE_NAME, &len);
  uint32_t head[4];
  if (!d || len < 16) return false;
  memcpy(head, d, 16);
  return head[0] == SAVE_MAGIC && (head[1] == 1 || head[1] == SAVE_VERSION);
}

bool load_game(void) {
  uint32_t len;
  const uint8_t *d = plat_load(SAVE_NAME, &len);
  uint32_t head[4];
  if (!d || len < 16) return false;
  memcpy(head, d, 16);
  if (head[0] != SAVE_MAGIC || (head[1] != 1 && head[1] != SAVE_VERSION)) return false;
  load_version = (int)head[1];
  SaveIO io = {0};
  io.in = d + 16;
  io.inlen = len - 16;
  bool ok = read_city(&io) && io.total == head[2] && io.crc == head[3];
  if (!ok) {
    saved_view.valid = false;
    game_reset();
    return false;
  }
  city_update_connections();
  sim_update_pathfinder(true, NONE);
  return true;
}

void save_delete(void) {
  plat_remove(SAVE_NAME);
  save_copy();
}
