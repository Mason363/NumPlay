#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")   /* not drawn every frame: smaller over faster */
#endif
/* The save file: Celeste's SaveData (strawberries, hearts, deaths and times of
 * every chapter side) with the Session being played inside it, so leaving
 * with Home keeps the run like the game's Save & Quit. One storage record. */
#include "level.h"
#include "player.h"

#define SAVE_NAME "celeste.sav"
#define SAVE_MAGIC 0x43454C53u   /* "CELS" */
#define SAVE_VERSION 3
/* 1.6.0's saves (version 1) and those before 1.15.1 (version 2, without bind2): the same up to their checksum, at
 * their end; what came after goes after it */
#define V1_SIZE 1552
#define V1_CHECKSUM 1544
#define V2_SIZE 1680

SaveData g_save;

static uint32_t checksum(uint32_t n) {   /* of the first n bytes */
  const uint8_t *p = (const uint8_t *)&g_save;
  uint32_t h = 2166136261u;
  for (uint32_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}
/* g_save's first n bytes hold a save of this version or 1.6.0's: true if it is whole (then it is of this version, what
 * 1.6.0 had not zero) */
static bool whole(uint32_t n) {
  uint8_t *b = (uint8_t *)&g_save;
  uint32_t at = n == sizeof g_save ? offsetof(SaveData, checksum) : n == V2_SIZE ? V2_SIZE - 4 : n == V1_SIZE ? V1_CHECKSUM : 0, ck;
  if (!at || g_save.magic != SAVE_MAGIC || g_save.size != n ||
      g_save.version != (n == V1_SIZE ? 1 : n == V2_SIZE ? 2 : SAVE_VERSION))
    return false;
  memcpy(&ck, b + at, 4);
  if (ck != checksum(at)) return false;
  memset(b + at, 0, sizeof g_save - at);
  g_save.version = SAVE_VERSION;
  g_save.size = sizeof g_save;
  return true;
}

static void save_new(void) {
  memset(&g_save, 0, sizeof g_save);
  g_save.magic = SAVE_MAGIC;
  g_save.version = SAVE_VERSION;
  g_save.size = sizeof g_save;
  plat_default_binds();
  save_keep_binds();
  g_save.binds_set = 1;
}

void save_keep_binds(void) {   /* g_bind into the save (written with the next save_write) */
  memcpy(g_save.bind, g_bind, 4);
  memcpy(g_save.bind2, g_bind + 4, 5);
}

/* The copy: installing apps from the NumWorks website keeps only Python scripts, so the save is
 * also kept in one, as a comment line "#>celeste.sav:base64" (the way NumPlay keeps its games'
 * saves), and comes back from it when the save itself is gone. */
#define COPY_NAME "celeste_saves.py"
static const char copy_head[] = "# Celeste's saves: keep this file\n";
static const char tag[] = "#>" SAVE_NAME ":";
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static void save_copy(void) {
  const uint8_t *d = (const uint8_t *)&g_save;
  uint32_t n = sizeof g_save, cap, total = 1 + (sizeof copy_head - 1) + (sizeof tag - 1) + (n + 2) / 3 * 4 + 2;
  uint8_t *o = res_scratch(total, &cap), *p = o;
  if (cap < total) return;
  *p++ = 0;   /* the script's status byte: not imported */
  memcpy(p, copy_head, sizeof copy_head - 1), p += sizeof copy_head - 1;
  memcpy(p, tag, sizeof tag - 1), p += sizeof tag - 1;
  for (uint32_t i = 0; i < n; i += 3, p += 4) {
    uint32_t left = n - i, v = (uint32_t)d[i] << 16 | (left > 1 ? d[i + 1] << 8 : 0) | (left > 2 ? d[i + 2] : 0);
    p[0] = (uint8_t)b64[v >> 18 & 63];
    p[1] = (uint8_t)b64[v >> 12 & 63];
    p[2] = left > 1 ? (uint8_t)b64[v >> 6 & 63] : '=';
    p[3] = left > 2 ? (uint8_t)b64[v & 63] : '=';
  }
  *p++ = '\n';
  *p++ = 0;
  plat_save(COPY_NAME, o, (uint32_t)(p - o));
}
static int b64_value(uint8_t c) {
  for (int i = 0; i < 64; i++)
    if ((uint8_t)b64[i] == c) return i;
  return -1;
}
/* the save from the copy (after the app was installed again): true if it was there and whole */
static bool restore_copy(void) {
  uint32_t n;
  const uint8_t *c = plat_load(COPY_NAME, &n), *l = NULL;
  if (!c) return false;
  for (uint32_t i = 0; i + sizeof tag - 1 <= n; i++)
    if (!memcmp(c + i, tag, sizeof tag - 1)) {
      l = c + i + sizeof tag - 1;
      break;
    }
  if (!l) return false;
  uint32_t len = 0, o = 0;
  while (l + len < c + n && l[len] != '\n' && l[len]) len++;
  if (!len || len % 4) return false;
  uint32_t size = len / 4 * 3 - (l[len - 1] == '=') - (l[len - 2] == '=');
  if (size > sizeof g_save) return false;
  uint8_t *out = (uint8_t *)&g_save;
  memset(out, 0, sizeof g_save);
  for (uint32_t i = 0; i < len; i += 4) {
    int v[4];
    for (int k = 0; k < 4; k++) v[k] = l[i + k] == '=' ? 0 : b64_value(l[i + k]);
    if (v[0] < 0 || v[1] < 0 || v[2] < 0 || v[3] < 0) return false;
    uint32_t w = (uint32_t)(v[0] << 18 | v[1] << 12 | v[2] << 6 | v[3]);
    for (int k = 0; k < 3 && o < size; k++) out[o++] = (uint8_t)(w >> (16 - 8 * k));
  }
  return whole(size);
}

void save_load(void) {
  uint32_t len = 0;
  const uint8_t *p = plat_load(SAVE_NAME, &len);
  memset(&g_save, 0, sizeof g_save);
  if (p && len <= sizeof g_save) memcpy(&g_save, p, len);   /* the record is not aligned: copy it */
  if (!(p && whole(len)) && !restore_copy()) save_new();
  if (g_save.cheat_mode) g_save.unlocked_areas = AREAS - 1;   /* SaveData.AfterInitialize: MaxArea */
  if (!g_save.binds_set) {   /* 1.6.0's keys: what was on backspace (pause now) goes to Back, as the defaults did */
    for (int a = 0; a < 4; a++)
      if (g_save.bind[a] == KEY_BACKSPACE) g_save.bind[a] = KEY_BACK;
    g_save.binds_set = 1;
  }
  plat_default_binds();
  if (g_save.bind[0] && g_save.bind[1] && g_save.bind[2] && g_save.bind[3]) memcpy(g_bind, g_save.bind, 4);
  if (g_save.bind2[0]) memcpy(g_bind + 4, g_save.bind2, 5);   /* (pause is never 0: older saves keep the defaults) */
  save_keep_binds();
}

bool save_write(void) {
  bool ok = save_write_quick();
  save_copy();
  return ok;
}

/* the save alone, without its copy: while a chapter is played (its room in the cache would push out what the room
 * uses); the copy is written again at the chapter's end and on leaving */
bool save_write_quick(void) {
  g_save.checksum = checksum(offsetof(SaveData, checksum));
  return plat_save(SAVE_NAME, &g_save, sizeof g_save);
}

ModeStats *save_mode(void) { return &g_save.modes[g_session.area][g_session.mode]; }

bool save_check_berry(int index) { return index >= 0 && (save_mode()->berries >> index & 1); }

void save_add_berry(int index, bool golden) {
  if (index < 0) return;
  ModeStats *m = save_mode();
  if (!(m->berries >> index & 1)) {
    m->berries |= (uint64_t)1 << index;
    if (golden) g_save.total_golden++;
  }
}

void save_add_death(void) {
  g_save.total_deaths++;
  save_mode()->deaths++;
}

void save_add_time(uint32_t frames) {
  g_save.time += frames;
  save_mode()->time += frames;
}

bool save_set_checkpoint(int index) {
  ModeStats *m = save_mode();
  if (m->checkpoints >> index & 1) return false;
  m->checkpoints |= (uint8_t)(1 << index);
  return true;
}

void save_register_heart(void) { save_mode()->flags |= MS_HEART; }
bool save_flag(int bit) { return g_save.flags >> bit & 1; }
void save_set_flag(int bit) { g_save.flags |= (uint8_t)(1 << bit); }
/* SaveData.UnlockedModes: B-sides after a cassette, C-sides after 16 hearts (or in cheat mode) */
int save_unlocked_modes(void) {
  if (g_save.cheat_mode) return 3;
  int hearts = 0;
  for (int a = 0; a < AREAS; a++)
    for (int m = 0; m < 3; m++) hearts += (g_save.modes[a][m].flags & MS_HEART) != 0;
  if (hearts >= 16) return 3;
  return (g_save.cassettes & 0x7FE) ? 2 : 1;
}
void save_register_cassette(void) { g_save.cassettes |= (uint16_t)(1 << g_session.area); }

/* the chapter's strawberries (CH_BERRIES): Session.FullClear needs them all */
static int detected_berries(void) { return g_level.ch ? g_level.ch[CH_DETECTED] : 0; }
static int popcount64(uint64_t v) {
  int n = 0;
  for (; v; v &= v - 1) n++;
  return n;
}
static bool full_clear(void) {
  if (g_session.mode != M_A || !g_session.cassette || !g_session.heart) return false;
  if (popcount64(g_session.berries) < detected_berries()) return false;
  if (g_session.area == 7) return g_session.summit_gems == 0x3F;   /* HasAllSummitGems */
  return true;
}

bool g_should_advance;
/* SaveData.RegisterCompletion */
void save_register_completion(void) {
  ModeStats *m = save_mode();
  Session *s = &g_session;
  if (s->grabbed_golden) m->best_deaths = 0;
  if (s->started_from_beginning) {
    m->flags |= MS_SINGLERUN;
    if (m->best_time == 0 || s->deaths < m->best_deaths) m->best_deaths = (uint16_t)(s->deaths > 0xFFFF ? 0xFFFF : s->deaths);
    if (m->best_time == 0 || s->dashes < m->best_dashes) m->best_dashes = (uint16_t)(s->dashes > 0xFFFF ? 0xFFFF : s->dashes);
    if (m->best_time == 0 || s->time < m->best_time) {
      if (m->best_time > 0) s->beat_best_time = 1;
      m->best_time = s->time;
    }
    if (s->mode == M_A && full_clear()) {
      m->flags |= MS_FULLCLEAR;
      if (m->best_fc_time == 0 || s->time < m->best_fc_time) m->best_fc_time = s->time;
    }
  }
  if (s->area + 1 > g_save.unlocked_areas && s->area < AREAS - 1) g_save.unlocked_areas = (uint8_t)(s->area + 1);
  g_should_advance = s->mode == M_A && !(m->flags & MS_COMPLETED) && chapter_index(s->area + 1, M_A) >= 0;
  m->flags |= MS_COMPLETED;
  s->in_area = 0;
}

/* ---------------------------------------------------------------- chapters in data.bin */
const uint8_t *chapter_at(int c) {
  const uint8_t *s = section(SEC_CHAPTERS);
  return s + rd32(s + 4 + 4 * c);
}
int chapter_index(int area, int mode) {
  const uint8_t *s = section(SEC_CHAPTERS);
  int n = rd16(s);
  for (int c = 0; c < n; c++) {
    const uint8_t *ch = chapter_at(c);
    if (ch[CH_AREA] == area && ch[CH_MODE] == mode) return c;
  }
  return -1;
}
int session_berry_index(int room, int eid) {
  const uint8_t *ch = g_level.ch;
  const uint8_t *b = ch + rd16(ch + CH_BERRIES);
  for (int i = 0; i < ch[CH_NBERRIES]; i++, b += 6)
    if (rd16(b) == room && rd16(b + 2) == (uint16_t)eid) return i;
  return -1;
}

/* PlayerInventory presets */
static void set_inventory(int inv) {
  static const uint8_t dashes[] = {0, 1, 1, 2, 2, 2, 1}, dream[] = {0, 1, 0, 1, 1, 1, 1},
                       pack[] = {1, 1, 1, 1, 0, 1, 0}, norefill[] = {0, 0, 0, 0, 0, 1, 0};
  g_session.inv_dashes = dashes[inv];
  g_session.dreamdash = dream[inv];
  g_session.backpack = pack[inv];
  g_session.no_refills = norefill[inv];
}

/* new Session(area, checkpoint): checkpoint -1 is the start */
void session_start(int chapter, int checkpoint) {
  const uint8_t *ch = chapter_at(chapter);
  Session *s = &g_session;
  memset(s, 0, sizeof *s);
  s->chapter = (uint8_t)chapter;
  s->area = ch[CH_AREA];
  s->mode = ch[CH_MODE];
  s->start_checkpoint = (int8_t)checkpoint;
  /* AreaData: Dreaming, CoreMode */
  s->dreaming = s->area == 2;
  s->core_mode = s->area == 9 ? 1 : 0;
  s->first_level = 1;
  s->dark_room_alpha = 0.75f;
  s->in_area = 1;
  int inv = ch[CH_INVENTORY];
  if (checkpoint < 0) {
    s->level = 0xFF;   /* the room at (0, 0): level_start finds it */
    s->started_from_beginning = 1;
  } else {
    const uint8_t *cp = ch + rd16(ch + CH_CHECKPOINTS) + 4 * checkpoint;
    s->level = (uint8_t)rd16(cp);
    if (cp[2] != 255) inv = cp[2];
    s->dreaming = (cp[3] & CPF_DREAMING) != 0;
    if (cp[3] & CPF_COLD) s->core_mode = 2;
    if (cp[3] & CPF_FEELINGDOWN) s->color_grade = CG_FEELINGDOWN;
  }
  set_inventory(inv);
  s->old_stats = g_save.modes[s->area][s->mode];
  if (checkpoint >= 0 && (ch[rd16(ch + CH_CHECKPOINTS) + 4 * checkpoint + 3] & CPF_BADELINE))
    level_set_flag("badeline_connection", true);
  g_save.last_area = s->area;
  g_save.last_mode = s->mode;
}

/* a session in any room (the room picker, in cheat mode), as the game's "load" command: from its checkpoint if it is
 * one, else from the start with the inventory the chapter ends with (rooms come in any order) */
void session_start_room(int chapter, int room) {
  const uint8_t *ch = chapter_at(chapter);
  int inv = ch[CH_INVENTORY], cp = -1;
  for (int i = 0; i < ch[CH_NCHECKPOINTS]; i++) {
    const uint8_t *c = ch + rd16(ch + CH_CHECKPOINTS) + 4 * i;
    if (rd16(c) == room) cp = i;
    if (c[2] != 255) inv = c[2];
  }
  session_start(chapter, cp);
  if (cp < 0) set_inventory(inv);
  g_session.level = (uint8_t)room;
  g_session.first_level = 0, g_session.started_from_beginning = 0;
}

/* Session.Restart(intoLevel) */
void session_restart(int room) {
  Session old = g_session;
  session_start(old.chapter, old.start_checkpoint);
  g_session.old_stats = old.old_stats;
  g_session.unlocked_cside = old.unlocked_cside;
  if (room >= 0) {
    g_session.level = (uint8_t)room;
    if (room != level_start_room()) g_session.started_from_beginning = 0;
  }
}

/* the player's entrance when a chapter loads (LevelLoader) */
int session_intro(bool just_started) {
  static const uint8_t intro[AREAS] = {INTRO_WALKINRIGHT, INTRO_JUMP, INTRO_WAKEUP, INTRO_WALKINRIGHT, INTRO_WALKINRIGHT,
                                       INTRO_WAKEUP, INTRO_NONE, INTRO_NONE, INTRO_WALKINLEFT, INTRO_WALKINRIGHT,
                                       INTRO_THINKFORABIT};
  Session *s = &g_session;
  if (!s->first_level || !s->started_from_beginning || !just_started) return INTRO_RESPAWN;
  return s->mode != M_C ? intro[s->area] : INTRO_WALKINRIGHT;
}
