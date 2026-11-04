/* ce_emu: a TI-84 Plus CE program run headless on CEmu's core (eZ80, LCD controller and panel, keypad, timers,
 * flash, memory protection), with os.c in place of TI-OS. Python's tools/ce_emu.py is the usual way in.
 *
 *   ce_emu [options] PROGRAM.8xp [VARS.8xv|GROUP.8xg ...]
 *
 * --rom FILE        the flash image (build/rom.bin)
 * --ms N            how long to run, in the calculator's milliseconds (5000)
 * --keys SPEC       keys held: "1500-1600:2nd,3000:clear" (CE names, or ok exe back home for the NumWorks's)
 * --shots SPEC      "1000,2500" or "1000:title.png,..." (PNG, 320 x 240, into --out)
 * --out DIR         where pictures go (.)
 * --ram NAME,...    these variables in RAM; the others where their file puts them (the archive, mostly)
 * --frame ADDR      a frame each time the CPU reaches ADDR (eadk_display_wait_for_vblank's, say)
 * --frame-drawn     only when what the LCD shows changed since the last one (a key scan as the marker)
 * --frames-from MS  frames counted from then on
 * --idle ADDR       a function the program waits in (ticksleep): the time inside isn't work
 * --vars-out DIR    the programs and appvars at the end, as files
 * --report FILE     a JSON report (else a summary on stdout)
 * --rev pre-a|i|m   the ASIC revision (m: every CE since 2019, the Python ones)
 * --names INC       ti84pceg.inc, for the routines' names
 * --script FILE     CEmu autotester steps (delay MS, key GROUP BIT, hash ID START SIZE TIMEOUT CRCS)
 * --float-scale X   TI's float routines cost X times os.c's estimates
 * --profile FILE    the PC every 0.1 ms, counted, into FILE
 * --screen-memory   pictures from the LCD controller's memory rather than the panel
 * --trace           every OS call on stderr
 *
 * It stops at a crash (a protection fault: a write to flash, to the stack limit, out in unprivileged code; the
 * reset vector reached: an invalid instruction or a jump to 0; a write to unmapped memory), a hang (halt with
 * interrupts off, or the CPU stuck in a few bytes with no frames, screen changes, key reads or OS calls for
 * 3 seconds), an OS error with no handler, an OS routine os.c doesn't have, or the program's end. */
#include "ce_emu.h"
#include "asic.h"
#include "bus.h"
#include "control.h"
#include "cpu.h"
#include "debug/debug.h"
#include "emu.h"
#include "interrupt.h"
#include "keypad.h"
#include "lcd.h"
#include "mem.h"
#include "panel.h"
#include "schedule.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <zlib.h>

#define CPU_HZ 48000000.0

static const struct {
  const char *name;
  int group, bit;
} KEYS[] = {
  {"graph", 1, 0}, {"trace", 1, 1}, {"zoom", 1, 2}, {"window", 1, 3}, {"yequ", 1, 4}, {"2nd", 1, 5}, {"mode", 1, 6},
  {"del", 1, 7}, {"sto", 2, 1}, {"ln", 2, 2}, {"log", 2, 3}, {"square", 2, 4}, {"recip", 2, 5}, {"math", 2, 6},
  {"alpha", 2, 7}, {"0", 3, 0}, {"1", 3, 1}, {"4", 3, 2}, {"7", 3, 3}, {"comma", 3, 4}, {"sin", 3, 5}, {"apps", 3, 6},
  {"xton", 3, 7}, {"dot", 4, 0}, {"2", 4, 1}, {"5", 4, 2}, {"8", 4, 3}, {"lparen", 4, 4}, {"cos", 4, 5},
  {"prgm", 4, 6}, {"stat", 4, 7}, {"chs", 5, 0}, {"3", 5, 1}, {"6", 5, 2}, {"9", 5, 3}, {"rparen", 5, 4},
  {"tan", 5, 5}, {"vars", 5, 6}, {"enter", 6, 0}, {"add", 6, 1}, {"sub", 6, 2}, {"mul", 6, 3}, {"div", 6, 4},
  {"power", 6, 5}, {"clear", 6, 6}, {"down", 7, 0}, {"left", 7, 1}, {"right", 7, 2}, {"up", 7, 3}, {"on", 2, 0},
  /* the NumWorks's names, on NumPlay's layout (platform/ti84ce/README.md) */
  {"ok", 1, 5}, {"exe", 6, 0}, {"back", 6, 6}, {"home", 1, 6}, {"backspace", 1, 7}, {"shift", 1, 4},
  {"toolbox", 2, 6}, {"var", 5, 6}, {"xnt", 3, 7}, {"plus", 6, 1}, {"minus", 6, 2}, {"ans", 5, 0},
};

typedef struct {
  uint64_t from, to;
  int group, bit;
} key_t_;

static struct {
  const char *rom, *out, *vars_out, *report, *names, *rev, *profile;
  uint64_t ms;
  key_t_ keys[256];
  int nkeys;
  struct {
    uint64_t at;
    char path[512];
  } shots[256];
  int nshots;
  char ram[64][9];
  int nram;
  uint32_t frame_addr, idle_addr;
  uint64_t frames_from;
  bool frame_drawn;
} opt = {.ms = 5000, .out = ".", .rev = "m"};

/* ---- the run's state ---- */
static uint64_t now_ms, started_cycles, start_ms;
static bool stopping, started, exited;
static char status[32] = "ok", detail[1024];
static uint32_t exit_code;
static char warnings[8192];
static size_t warn_len;
static int nwarn;
static char console_err[512];
static char text_log[8192];
static size_t text_len;
static bool held[8][8], on_held;
static uint32_t presses[8][8];
static uint64_t key_reads, os_calls, screen_changes, timer_reads;
/* frames: a marker reached (--frame), or the LCD's base set (a double-buffered program's swap) */
typedef struct {
  uint64_t n, at[50000];
} frames_t;
static frames_t marks, swaps;
static uint32_t drawn_hash;
/* time asleep (--idle: a function the program waits in, ticksleep): from its call to its return */
static struct {
  uint64_t from, to;
} idle[200000];
static int nidle;
static uint32_t idle_ret;
static uint64_t idle_from;
static double idle_total;
static double idle_between(uint64_t a, uint64_t b) {
  double t = 0;
  for (int i = 0; i < nidle; i++) {
    uint64_t x = idle[i].from > a ? idle[i].from : a, y = idle[i].to < b ? idle[i].to : b;
    if (y > x) t += y - x;
  }
  return t;
}
static int cmp_double(const void *a, const void *b) {
  double x = *(const double *)a, y = *(const double *)b;
  return x < y ? -1 : x > y;
}
static void add_frame(frames_t *f) {
  if (!started || now_ms < opt.frames_from) return;
  if (f->n < sizeof f->at / sizeof f->at[0]) f->at[f->n] = sched_total_cycles();
  f->n++;
}
static uint32_t last_hash;
static uint32_t *samples;

static uint64_t cycles_now(void) { return sched_total_cycles(); }
uint64_t emu_ms(void) { return now_ms; }
bool emu_key_held(int group, int bit) { return group >= 0 && group < 8 && bit >= 0 && bit < 8 && held[group][bit]; }
uint32_t emu_key_presses(int group, int bit) { return group >= 0 && group < 8 && bit >= 0 && bit < 8 ? presses[group][bit] : 0; }
/* an OS routine's time, passed as a halted CPU passes it (CEmu's cpu_halt): up to the scheduler's next event, that
 * event run (timers, the LCD, the seconds), and so on, the LCD's memory reads kept up with; charged at once, archiving
 * a 19 KB appvar left a second's events behind and stopped the core (sched_second's assertion) */
static bool in_hook;
void emu_charge(uint32_t cycles) {
  if (!in_hook) {
    debug.cpuCycles += cycles;
    return;
  }
  cpu.cycles = debug.cpuCycles;
  while (cycles) {
    uint32_t next = sched_event_next_cycle(), step = next > cpu.cycles ? next - cpu.cycles : 0;
    if (step > cycles) step = cycles;
    if (step > 4800) step = 4800; /* (0.1 ms) */
    cpu.cycles += step, cycles -= step;
    sched_process_pending_dma(0);
    sched_process_pending_events();
  }
  /* (what the debugger puts back after the hook: these, and the events looked at again at once) */
  debug.cpuCycles = cpu.cycles, debug.cpuBaseCycles = cpu.baseCycles, debug.cpuNext = 0;
}
void emu_jump(uint32_t pc) { debug_set_pc(pc); }

void emu_stop(const char *st, const char *fmt, ...) {
  if (stopping) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(detail, sizeof detail, fmt, ap);
  va_end(ap);
  snprintf(status, sizeof status, "%s", st);
  stopping = true;
  cpu_set_signal(CPU_SIGNAL_EXIT);
}
void emu_warn(const char *fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  if (++nwarn > 40) return;
  warn_len += snprintf(warnings + warn_len, sizeof warnings - warn_len, "%s%s", warn_len ? "\n" : "", buf);
  if (warn_len >= sizeof warnings) warn_len = sizeof warnings - 1;
}
void emu_text(const char *s) {
  text_len += snprintf(text_log + text_len, sizeof text_log - text_len, "%s%s", text_len ? "\n" : "", s);
  if (text_len >= sizeof text_log) text_len = sizeof text_log - 1;
}
void emu_program_started(void) {
  started = true;
  started_cycles = cycles_now();
  start_ms = now_ms;
  debug_watch(0x000000, DBG_MASK_EXEC, true);
}
void emu_program_exited(uint32_t code) {
  exited = true;
  exit_code = code;
  emu_stop("exited", "the program returned %d at %llu ms", (int)(code & 0x800000 ? code - 0x1000000 : code),
           (unsigned long long)now_ms);
}

/* ---- CEmu's callbacks ---- */
void gui_console_clear(void) {}
void gui_console_printf(const char *fmt, ...) {
  if (strncmp(fmt, "[CEmu]", 6) == 0) return;
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap); /* (a program's dbg_printf) */
  va_end(ap);
}
void gui_console_err_printf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(console_err, sizeof console_err, fmt, ap);
  va_end(ap);
  if (!strncmp(console_err, "[CEmu] ", 7)) memmove(console_err, console_err + 7, strlen(console_err + 7) + 1);
  size_t n = strlen(console_err);
  while (n && console_err[n - 1] == '\n') console_err[--n] = 0;
}
asic_rev_t gui_handle_reset(const boot_ver_t *boot_ver, asic_rev_t loaded_rev, asic_rev_t default_rev,
                            emu_device_t device, bool *python) {
  (void)boot_ver, (void)loaded_rev, (void)default_rev, (void)device;
  *python = false;
  return !strcmp(opt.rev, "pre-a") ? ASIC_REV_PRE_A : !strcmp(opt.rev, "i") ? ASIC_REV_I : ASIC_REV_M;
}
void gui_debug_close(void) {}
static void debug_hook(int reason, uint32_t data);
void gui_debug_open(int reason, uint32_t data) {
  in_hook = true;
  debug_hook(reason, data);
  in_hook = false;
}
static void debug_hook(int reason, uint32_t data) {
  uint32_t pc = cpu.registers.PC, sp = cpu.registers.SPL;
  switch (reason) {
  case DBG_BREAKPOINT:
    if (data == opt.frame_addr && opt.frame_addr) {
      if (opt.frame_drawn) { /* only when what the LCD shows changed since the last one */
        uint32_t n = (lcd.control >> 1 & 7) == 3 ? LCD_SIZE : LCD_SIZE * 2;
        uint8_t *p = mem_at(lcd.upbase, n);
        uint32_t h = p ? crc32(0, p, n) : 0;
        if (h == drawn_hash) return;
        drawn_hash = h;
      }
      add_frame(&marks);
      return;
    }
    if (data == opt.idle_addr && opt.idle_addr && !idle_ret) {
      idle_ret = rd24(sp), idle_from = cycles_now();
      if (idle_ret != opt.frame_addr) debug_watch(idle_ret, DBG_MASK_EXEC, true);
      return;
    }
    if (data == idle_ret && idle_ret) {
      if (idle_ret != opt.frame_addr && idle_ret != opt.idle_addr) debug_watch(idle_ret, DBG_MASK_EXEC, false);
      idle_ret = 0;
      if (nidle < (int)(sizeof idle / sizeof idle[0])) idle[nidle].from = idle_from, idle[nidle++].to = cycles_now();
      idle_total += cycles_now() - idle_from;
      if (data != opt.frame_addr) return;
    }
    if (data == 0x000000) {
      emu_stop("crash", "the reset vector: an invalid instruction or a jump to 0, returning to %06X",
               rd24(sp));
      return;
    }
    if (data == 0x000066) {
      emu_stop("crash", "NMI from the memory protection (status %d): %s", control.protectionStatus,
               console_err[0] ? console_err : "?");
      return;
    }
    os_calls++;
    if (!os_trap(data)) emu_stop("crash", "a breakpoint at %06X ce_emu doesn't know", data);
    return;
  case DBG_WATCHPOINT_READ:
    if (data >= 0xF50010 && data < 0xF50020) key_reads++;
    else if (data >= 0xF20000 && data < 0xF20030) timer_reads++;
    return;
  case DBG_WATCHPOINT_WRITE:
    if (data >= 0xE30010 && data < 0xE30013) {
      if (data == 0xE30010) add_frame(&swaps); /* (graphx's gfx_SwapDraw: one a frame) */
      return;
    }
    if (started) emu_stop("crash", "a write to unmapped memory at %06X (pc %06X)", data, pc);
    return;
  case DBG_NMI_TRIGGERED:
    return; /* (the NMI handler's breakpoint tells) */
  case DBG_WATCHDOG_TIMEOUT: emu_stop("crash", "the watchdog timer ran out"); return;
  case DBG_MISC_RESET: emu_stop("crash", "the calculator reset: %s", console_err); return;
  case DBG_READY: emu_stop("crash", "the calculator reset: %s", console_err[0] ? console_err : "?"); return;
  case DBG_FROZEN: emu_stop("hang", "halt with interrupts off at %06X", pc); return;
  default:
    if (reason >= DBG_PORT_RANGE) { /* the toolchain's debug.h: a command written to 0xFFFFFF */
      if (data == CMD_ABORT) emu_stop("crash", "the program aborted (debug command), returning to %06X", rd24(sp));
      else if (data == CMD_DEBUG) emu_warn("dbg_Debugger() at %06X", rd24(sp));
      return;
    }
    return;
  }
}

/* ---- pictures ---- */
static uint32_t frame_px[LCD_WIDTH * LCD_HEIGHT];
static bool from_memory;
static void grab(void) {
  if (!from_memory) {
    emu_lcd_drawframe(frame_px);
    return;
  }
  uint32_t *dat, *end;
  emu_set_lcd_ptrs(&dat, &end, LCD_WIDTH, LCD_HEIGHT, lcd.upbase, lcd.control, false);
  emu_lcd_drawmem(frame_px, dat, end, lcd.control, LCD_SIZE);
}
static void png_chunk(FILE *f, const char *type, const uint8_t *d, uint32_t n) {
  uint8_t b[4] = {n >> 24, n >> 16, n >> 8, n};
  fwrite(b, 1, 4, f);
  fwrite(type, 1, 4, f);
  if (n) fwrite(d, 1, n, f);
  uint32_t c = crc32(crc32(0, (const uint8_t *)type, 4), d, n);
  uint8_t cb[4] = {c >> 24, c >> 16, c >> 8, c};
  fwrite(cb, 1, 4, f);
}
static bool save_png(const char *path) {
  static uint8_t raw[LCD_HEIGHT * (1 + LCD_WIDTH * 3)];
  grab();
  for (int y = 0; y < LCD_HEIGHT; y++) {
    uint8_t *r = raw + y * (1 + LCD_WIDTH * 3);
    r[0] = 0;
    for (int x = 0; x < LCD_WIDTH; x++) {
      uint32_t p = frame_px[y * LCD_WIDTH + x];
      r[1 + x * 3] = p >> 16, r[2 + x * 3] = p >> 8, r[3 + x * 3] = p;
    }
  }
  uLongf zn = compressBound(sizeof raw);
  uint8_t *z = malloc(zn);
  compress2(z, &zn, raw, sizeof raw, 6);
  FILE *f = fopen(path, "wb");
  if (!f) {
    free(z);
    return false;
  }
  fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
  uint8_t ihdr[13] = {0, 0, LCD_WIDTH >> 8, LCD_WIDTH & 0xFF, 0, 0, 0, LCD_HEIGHT, 8, 2, 0, 0, 0};
  png_chunk(f, "IHDR", ihdr, 13);
  png_chunk(f, "IDAT", z, (uint32_t)zn);
  png_chunk(f, "IEND", NULL, 0);
  fclose(f);
  free(z);
  return true;
}
static uint32_t screen_hash(void) {
  grab();
  return crc32(0, (const uint8_t *)frame_px, sizeof frame_px);
}

/* ---- options ---- */
static int key_index(const char *name, size_t n) {
  for (size_t i = 0; i < sizeof KEYS / sizeof KEYS[0]; i++)
    if (strlen(KEYS[i].name) == n && !strncmp(KEYS[i].name, name, n)) return (int)i;
  return -1;
}
static void parse_keys(const char *s) {
  while (s && *s && opt.nkeys < (int)(sizeof opt.keys / sizeof opt.keys[0])) {
    char *e;
    unsigned long long a = strtoull(s, &e, 10), b = a + 100;
    s = e;
    if (*s == '-') b = strtoull(s + 1, &e, 10), s = e;
    if (*s++ != ':') break;
    size_t n = strcspn(s, ",");
    int k = key_index(s, n);
    if (k < 0) {
      fprintf(stderr, "ce_emu: no key %.*s\n", (int)n, s);
      exit(2);
    }
    opt.keys[opt.nkeys++] = (key_t_){a, b, KEYS[k].group, KEYS[k].bit};
    s += n;
    if (*s == ',') s++;
  }
}
static void parse_shots(const char *s) {
  while (s && *s && opt.nshots < (int)(sizeof opt.shots / sizeof opt.shots[0])) {
    char *e;
    unsigned long long t = strtoull(s, &e, 10);
    s = e;
    size_t n = 0;
    if (*s == ':') n = strcspn(++s, ",");
    if (n) snprintf(opt.shots[opt.nshots].path, sizeof opt.shots[0].path, "%s/%.*s", opt.out, (int)n, s);
    else snprintf(opt.shots[opt.nshots].path, sizeof opt.shots[0].path, "%s/shot_%06llu.png", opt.out, t);
    opt.shots[opt.nshots++].at = t;
    s += n;
    if (*s == ',') s++;
  }
}

/* the keys held this millisecond, into CEmu's keypad */
static void apply_keys(void) {
  bool want[8][8] = {{0}}, want_on = false;
  for (int i = 0; i < opt.nkeys; i++)
    if (now_ms >= opt.keys[i].from && now_ms <= opt.keys[i].to) {
      if (opt.keys[i].group == 2 && opt.keys[i].bit == 0) want_on = true;
      else want[opt.keys[i].group][opt.keys[i].bit] = true;
    }
  for (int i = 0; i < opt.nkeys; i++) /* a press right after another of the same key: let go first */
    if (now_ms && now_ms == opt.keys[i].from && held[opt.keys[i].group][opt.keys[i].bit])
      emu_keypad_event(opt.keys[i].group, opt.keys[i].bit, false), held[opt.keys[i].group][opt.keys[i].bit] = false;
  for (int g = 1; g < 8; g++)
    for (int b = 0; b < 8; b++)
      if (want[g][b] != held[g][b]) {
        held[g][b] = want[g][b], emu_keypad_event(g, b, want[g][b]);
        if (want[g][b]) presses[g][b]++;
      }
  if (want_on != on_held) on_held = want_on, emu_keypad_event(2, 0, want_on);
}

/* ---- a script (--script, CEmu's autotester's steps): delay MS, key GROUP BIT, hash ID START SIZE TIMEOUT CRC,...
 * A key is held 80 ms; a hash (CRC-32C, the autotester's) is checked every 100 ms until it matches or times out ---- */
static struct step {
  char op;
  uint32_t a, b, timeout, crcs[16];
  int ncrc;
  char id[32];
} steps[512];
static int nsteps, step_i;
static bool step_on;
static uint64_t step_until, step_from;
static char hash_log[16384];
static size_t hash_len;
static int hashes_ok, hashes_bad;
static uint32_t crc32c(const uint8_t *p, size_t n) {
  uint32_t c = 0xFFFFFFFF;
  while (n--) {
    c ^= *p++;
    for (int k = 0; k < 8; k++) c = c >> 1 ^ (0x82F63B78 & -(c & 1));
  }
  return ~c;
}
static void load_script(const char *path) {
  FILE *f = fopen(path, "r");
  char line[512];
  while (f && fgets(line, sizeof line, f) && nsteps < (int)(sizeof steps / sizeof steps[0])) {
    struct step *s = &steps[nsteps];
    memset(s, 0, sizeof *s);
    char crcs[400] = "";
    if (sscanf(line, "delay %u", &s->a) == 1) s->op = 'd';
    else if (sscanf(line, "key %u %u", &s->a, &s->b) == 2) s->op = 'k';
    else if (sscanf(line, "hash %31s %x %u %u %399s", s->id, &s->a, &s->b, &s->timeout, crcs) == 5) {
      s->op = 'h';
      for (char *t = strtok(crcs, ","); t && s->ncrc < 16; t = strtok(NULL, ",")) s->crcs[s->ncrc++] = strtoul(t, NULL, 16);
    } else continue;
    nsteps++;
  }
  if (f) fclose(f);
}
static void script_tick(void) {
  while (step_i < nsteps && !stopping) {
    struct step *s = &steps[step_i];
    if (!step_on) step_on = true, step_from = now_ms, step_until = now_ms;
    if (s->op == 'd') {
      if (now_ms < step_from + s->a) return;
    } else if (s->op == 'k') {
      if (now_ms == step_from && opt.nkeys < (int)(sizeof opt.keys / sizeof opt.keys[0]))
        opt.keys[opt.nkeys++] = (key_t_){now_ms, now_ms + 79, (int)s->a, (int)s->b};
      if (now_ms < step_from + 80) return;
    } else if (s->op == 'h') {
      if (now_ms < step_until) return;
      uint8_t *p = mem_at(s->a, s->b);
      uint32_t c = p ? crc32c(p, s->b) : 0;
      bool ok = false;
      for (int i = 0; i < s->ncrc; i++) ok |= c == s->crcs[i];
      if (!ok && now_ms < step_from + s->timeout) {
        step_until = now_ms + 100;
        return;
      }
      ok ? hashes_ok++ : hashes_bad++;
      hash_len += snprintf(hash_log + hash_len, sizeof hash_log - hash_len, "%s{\"id\": \"%s\", \"ms\": %llu, \"crc\": \"%08X\", \"match\": %s, \"exited\": %s}",
                           hash_len ? ", " : "", s->id, (unsigned long long)now_ms, c, ok ? "true" : "false", exited ? "true" : "false");
      if (hash_len >= sizeof hash_log) hash_len = sizeof hash_log - 1;
    }
    step_i++, step_on = false;
  }
  if (nsteps && step_i >= nsteps && !stopping) {
    stopping = true;
    snprintf(detail, sizeof detail, "the script's end");
    cpu_set_signal(CPU_SIGNAL_EXIT);
  }
}

static void json_str(FILE *f, const char *s) {
  fputc('"', f);
  for (; *s; s++) {
    if (*s == '"' || *s == '\\') fprintf(f, "\\%c", *s);
    else if (*s == '\n') fputs("\\n", f);
    else if ((unsigned char)*s < 32) fprintf(f, "\\u%04x", *s);
    else fputc(*s, f);
  }
  fputc('"', f);
}

int main(int argc, char **argv) {
  const char *files[64], *keys = NULL, *shots = NULL;
  int nfiles = 0;
  for (int i = 1; i < argc; i++) {
    const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
    if (!strcmp(a, "--rom") && v) opt.rom = v, i++;
    else if (!strcmp(a, "--ms") && v) opt.ms = strtoull(v, NULL, 10), i++;
    else if (!strcmp(a, "--keys") && v) keys = v, i++;
    else if (!strcmp(a, "--shots") && v) shots = v, i++;
    else if (!strcmp(a, "--out") && v) opt.out = v, i++;
    else if (!strcmp(a, "--vars-out") && v) opt.vars_out = v, i++;
    else if (!strcmp(a, "--report") && v) opt.report = v, i++;
    else if (!strcmp(a, "--names") && v) opt.names = v, i++;
    else if (!strcmp(a, "--rev") && v) opt.rev = v, i++;
    else if (!strcmp(a, "--frame") && v) opt.frame_addr = (uint32_t)strtoul(v, NULL, 0), i++;
    else if (!strcmp(a, "--frame-drawn")) opt.frame_drawn = true;
    else if (!strcmp(a, "--idle") && v) opt.idle_addr = (uint32_t)strtoul(v, NULL, 0), i++;
    else if (!strcmp(a, "--frames-from") && v) opt.frames_from = strtoull(v, NULL, 10), i++;
    else if (!strcmp(a, "--trace")) os_trace = true;
    else if (!strcmp(a, "--float-scale") && v) os_float_scale = atof(v), i++;
    else if (!strcmp(a, "--script") && v) load_script(v), i++;
    else if (!strcmp(a, "--screen-memory")) from_memory = true;
    else if (!strcmp(a, "--profile") && v) opt.profile = v, i++;
    else if (!strcmp(a, "--ram") && v) {
      for (const char *s = v; *s && opt.nram < 64;) {
        size_t n = strcspn(s, ",");
        snprintf(opt.ram[opt.nram++], 9, "%.*s", (int)(n > 8 ? 8 : n), s);
        s += n + (s[n] == ',');
      }
      i++;
    } else if (a[0] == '-') {
      fprintf(stderr, "ce_emu: unknown option %s (see the top of ce_emu.c)\n", a);
      return 2;
    } else if (nfiles < 64) {
      files[nfiles++] = a;
    }
  }
  if (!nfiles || !opt.rom) {
    fprintf(stderr, "usage: ce_emu --rom rom.bin [options] PROGRAM.8xp [VARS.8xv ...]\n");
    return 2;
  }
  mkdir(opt.out, 0755);
  parse_keys(keys);
  parse_shots(shots);
  if (opt.names) os_names_load(opt.names);

  debug_init();
  emu_state_t st = emu_load(EMU_DATA_ROM, opt.rom);
  if (st == EMU_STATE_INVALID) {
    fprintf(stderr, "ce_emu: can't load %s\n", opt.rom);
    return 2;
  }
  bus_init_rand(1, 2, 3); /* (the same unmapped reads every run) */
  emu_set_lcd_dma(1);
  emu_set_run_rate(1000);
  os_setup();

  /* the variables: the program first, in the archive unless --ram names it */
  char prog[9] = {0};
  static tivar_t vars[64];
  for (int i = 0; i < nfiles; i++) {
    int n = ti_read_file(files[i], vars, 64);
    if (n <= 0) {
      fprintf(stderr, "ce_emu: %s isn't a TI file\n", files[i]);
      return 2;
    }
    for (int k = 0; k < n; k++) {
      char nm[9] = {0};
      memcpy(nm, vars[k].name, vars[k].namelen);
      bool ram = !(vars[k].flag & 0x80);
      for (int r = 0; r < opt.nram; r++)
        if (!strcmp(opt.ram[r], nm)) ram = true;
      if (!prog[0] && ((vars[k].type & 0x3F) == 5 || (vars[k].type & 0x3F) == 6)) strcpy(prog, nm);
      if (!os_add_var(&vars[k], !ram)) fprintf(stderr, "ce_emu: %s twice, or no room for it\n", nm);
    }
  }
  if (!prog[0] || !os_set_program(prog)) {
    fprintf(stderr, "ce_emu: no program among the files\n");
    return 2;
  }

  /* the host's routines: every slot of the jump tables still holding a ret */
  for (uint32_t a = 0x80; a < 0x800; a += 4)
    if (rd8(a) == 0xC9) debug_watch(a, DBG_MASK_EXEC, true);
  for (uint32_t a = 0x20100; a < HLE_BASE + 4 * HLE_SLOTS; a += 4)
    if (rd8(a) == 0xC9) debug_watch(a, DBG_MASK_EXEC, true);
  debug_watch(0x66, DBG_MASK_EXEC, true);
  if (opt.frame_addr) debug_watch(opt.frame_addr, DBG_MASK_EXEC, true);
  if (opt.idle_addr) debug_watch(opt.idle_addr, DBG_MASK_EXEC, true);
  for (uint32_t a = 0xF50010; a < 0xF50020; a++) debug_watch(a, DBG_MASK_READ, true);
  for (uint32_t a = 0xF20000; a < 0xF20030; a++) debug_watch(a, DBG_MASK_READ, true); /* (waiting on a timer is waiting) */
  for (uint32_t a = 0xE30010; a < 0xE30013; a++) debug_watch(a, DBG_MASK_WRITE, true);
  for (uint32_t a = 0xD00000; a < 0xE00000; a++)
    if ((a & 0x7FFFF) >= SIZE_RAM) debug_watch(a, DBG_MASK_WRITE, true);
  for (uint32_t a = 0xC00000; a < 0xD00000; a++) debug_watch(a, DBG_MASK_WRITE, true);
  debug_flag(DBG_SOFT_COMMANDS | DBG_OPEN_ON_RESET, true);

  if (opt.profile) emu_set_run_rate(10000), samples = calloc(1 << 24, sizeof *samples);
  clock_t wall0 = clock();
  int shot = 0;
  uint32_t stuck_pc = 0;
  uint64_t stuck_since = 0, activity = 0, last_activity = 0;
  while (opt.nshots > 1) { /* (in time order) */
    bool sorted = true;
    for (int i = 1; i < opt.nshots; i++)
      if (opt.shots[i].at < opt.shots[i - 1].at) {
        __typeof__(opt.shots[0]) t = opt.shots[i];
        opt.shots[i] = opt.shots[i - 1], opt.shots[i - 1] = t, sorted = false;
      }
    if (sorted) break;
  }
  for (now_ms = 0; now_ms < opt.ms && !stopping; now_ms++) {
    if (started) script_tick();
    apply_keys();
    if (opt.profile) { /* the PC every 0.1 ms */
      for (int k = 0; k < 10 && !stopping; k++) {
        emu_run(1);
        if (started) samples[cpu.registers.PC & 0xFFFFFF]++;
      }
    } else {
      emu_run(1);
    }
    while (shot < opt.nshots && opt.shots[shot].at <= now_ms) save_png(opt.shots[shot++].path);
    if (!started || stopping) continue;
    if (now_ms % 16 == 0) {
      uint32_t h = screen_hash();
      if (h != last_hash) screen_changes++, last_hash = h;
    }
    /* hangs */
    if (cpu.halted && !cpu.IEF1) emu_stop("hang", "halt with interrupts off at %06X", cpu.registers.PC);
    uint32_t pc = cpu.registers.PC;
    uint64_t act = marks.n + swaps.n + screen_changes + key_reads + os_calls + timer_reads;
    if (act != activity) activity = act, last_activity = now_ms;
    if (pc - stuck_pc > 64 && stuck_pc - pc > 64) stuck_pc = pc, stuck_since = now_ms;
    uint64_t quiet = now_ms - (stuck_since > last_activity ? stuck_since : last_activity);
    if (quiet >= 3000)
      emu_stop("hang", "the CPU stayed around %06X for %llu ms: no frame, screen change, key or timer read, or OS call", pc,
               (unsigned long long)quiet);
    uint32_t sp = cpu.registers.SPL;
    if (cpu.ADL && (sp < STACK_BOT - 0x100 || sp > STACK_TOP)) {
      static bool told;
      if (!told) emu_warn("the stack pointer left the stack: %06X at %llu ms (PC %06X)", sp, (unsigned long long)now_ms, pc);
      told = true;
    }
  }
  double wall = (double)(clock() - wall0) / CLOCKS_PER_SEC;
  for (; shot < opt.nshots; shot++) save_png(opt.shots[shot].path);
  char last[600];
  snprintf(last, sizeof last, "%s/last.png", opt.out);
  save_png(last);
  int saved = opt.vars_out ? (mkdir(opt.vars_out, 0755), os_save_vars(opt.vars_out)) : 0;
  if (opt.profile) {
    FILE *pf = fopen(opt.profile, "w");
    for (uint32_t a = 0; pf && a < 1 << 24; a++)
      if (samples[a]) fprintf(pf, "%06X %u\n", a, samples[a]);
    if (pf) fclose(pf);
  }

  /* frames: the marker's, else the buffer swaps'; the time from each to the next */
  frames_t *fr = marks.n >= 2 || swaps.n < 2 ? &marks : &swaps;
  const char *frame_kind = fr == &swaps ? "lcd-swap" : opt.frame_drawn ? "marker-drawn" : opt.frame_addr ? "marker" : "none";
  uint64_t nf = fr->n < sizeof fr->at / sizeof fr->at[0] ? fr->n : sizeof fr->at / sizeof fr->at[0];
  double fps = 0, mean_ms = 0, max_ms = 0, p95 = 0, work_ms = 0;
  uint64_t over33 = 0;
  if (nf >= 2) {
    double *d = malloc((nf - 1) * sizeof *d);
    for (uint64_t i = 1; i < nf; i++) {
      d[i - 1] = (fr->at[i] - fr->at[i - 1]) / CPU_HZ * 1000;
      if (d[i - 1] > max_ms) max_ms = d[i - 1];
      if (d[i - 1] > 33.4) over33++;
    }
    mean_ms = (fr->at[nf - 1] - fr->at[0]) / CPU_HZ * 1000 / (nf - 1);
    fps = 1000 / mean_ms;
    work_ms = mean_ms - idle_between(fr->at[0], fr->at[nf - 1]) / CPU_HZ * 1000 / (nf - 1);
    qsort(d, nf - 1, sizeof *d, cmp_double);
    p95 = d[(uint64_t)((nf - 2) * 0.95)];
    free(d);
  }
  double emu_s = (double)now_ms / 1000;
  FILE *f = opt.report ? fopen(opt.report, "w") : NULL;
  if (f) {
    fprintf(f, "{\n  \"program\": ");
    json_str(f, prog);
    fprintf(f, ",\n  \"status\": ");
    json_str(f, status);
    fprintf(f, ",\n  \"detail\": ");
    json_str(f, stopping ? detail : "");
    fprintf(f, ",\n  \"ms\": %llu,\n  \"exit_code\": %d,\n  \"wall_s\": %.3f,\n  \"speed\": %.2f,\n",
            (unsigned long long)now_ms, exited ? (int)exit_code : -1, wall, wall > 0 ? emu_s / wall : 0);
    fprintf(f, "  \"cycles\": %llu,\n  \"frames\": %llu,\n  \"frame_kind\": \"%s\",\n  \"fps\": %.2f,\n  \"frame_ms\": %.2f,\n"
               "  \"frame_ms_p95\": %.2f,\n  \"frame_ms_max\": %.2f,\n  \"frames_over_33ms\": %llu,\n"
               "  \"cycles_per_frame\": %.0f,\n  \"work_ms_per_frame\": %.2f,\n  \"idle_ms\": %.0f,\n",
            (unsigned long long)cycles_now(), (unsigned long long)fr->n, frame_kind, fps, mean_ms, p95, max_ms,
            (unsigned long long)over33, mean_ms * CPU_HZ / 1000, work_ms, idle_total / CPU_HZ * 1000);
    fprintf(f, "  \"lcd_swaps\": %llu,\n  \"screen_changes\": %llu,\n  \"key_reads\": %llu,\n",
            (unsigned long long)swaps.n, (unsigned long long)screen_changes, (unsigned long long)key_reads);
    os_report(f);
    fprintf(f, "  \"hashes\": [%s],\n  \"hashes_ok\": %d,\n  \"hashes_bad\": %d,\n", hash_log, hashes_ok, hashes_bad);
    fprintf(f, "  \"vars_saved\": %d,\n  \"text\": ", saved);
    json_str(f, text_log);
    fprintf(f, ",\n  \"warnings\": ");
    json_str(f, warnings);
    fprintf(f, "\n}\n");
    fclose(f);
  } else {
    printf("ce_emu: %s %s%s%s\n", prog, status, stopping ? ": " : "", stopping ? detail : "");
    printf("ce_emu: %.2f s emulated in %.2f s (%.1fx) at %.0f MHz", emu_s, wall, wall > 0 ? emu_s / wall : 0,
           sched_get_clock_rate(CLOCK_CPU) / 1e6);
    if (nf >= 2) printf(", %llu frames (%s), %.1f fps, %.1f ms a frame (95%%: %.1f, slowest %.1f)", (unsigned long long)fr->n,
                        frame_kind, fps, mean_ms, p95, max_ms);
    printf("\n");
    if (text_len) printf("ce_emu: home screen: %s\n", text_log);
    if (warn_len) printf("ce_emu: warnings: %s\n", warnings);
  }
  return !strcmp(status, "ok") || !strcmp(status, "exited") ? 0 : 1;
}
