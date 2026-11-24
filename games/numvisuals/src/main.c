/* NumVisuals: moving backgrounds to look at, with a clock, a stopwatch, a timer, a counter or your own words on top.
 *
 * The backgrounds are in scenes.c, the drawing kit (light, dithering, text) in core.c. This is the app: the menus,
 * the add-ons and what's kept between visits. */
#include <eadk.h>
#include "../../common/epsilon_app.h"
#include "../../common/epsilon_files.h"
#include "../../common/np_text.h"
#include "vis.h"

#ifdef __ELF__ /* app name and API level, for the calculator's installer */
#ifndef NP_PORT /* (another target: platform/port.mk names the app) */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "NumVisuals";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif
#endif

#define KEY(k) (1ull << (k))
void core_init(uint32_t salt);

static int slen(const char *s) {
  int n = 0;
  while (s[n]) n++;
  return n;
}
#define PX(n) ((n) * 16) /* sizes in 1/16 pixels */

/* ------------------------------------------------------------------ add-ons */
enum { A_NONE, A_CLOCK, A_STOPWATCH, A_TIMER, A_COUNTER, A_TEXT, A_COUNT };
static const char *const addon_names[A_COUNT] = {T("None"), T("Clock"), T("Stopwatch"), T("Timer"), T("Counter"), T("Text")};

/* everything kept between visits (numvisuals.sav) */
static struct {
  uint8_t magic, version, bg, addon, h12, sw_on, tm_on, pad; /* tm_on: 0 paused, 1 running, 2 ringing */
  int32_t counter;
  uint32_t clock0; /* seconds of the day when the millisecond counter was 0 */
  uint32_t sw_acc, sw_t0, tm_len, tm_left, tm_t0;
  char text[24];
} V;
#define SAVE_NAME "numvisuals.sav"

static char *two(char *o, unsigned v) {
  *o++ = (char)('0' + v / 10 % 10);
  *o++ = (char)('0' + v % 10);
  return o;
}
static char *num(char *o, int32_t v) {
  char t[12];
  int n = 0;
  uint32_t u = v < 0 ? 0u - (uint32_t)v : (uint32_t)v;
  if (v < 0) *o++ = '-';
  do t[n++] = (char)('0' + u % 10); while (u /= 10);
  while (n) *o++ = t[--n];
  return o;
}
static uint32_t stopwatch(void) { return V.sw_on ? V.sw_acc + (now - V.sw_t0) : V.sw_acc; }
static uint32_t timer_left(void) {
  if (V.tm_on != 1) return V.tm_left;
  uint32_t gone = now - V.tm_t0;
  return gone >= V.tm_left ? 0 : V.tm_left - gone;
}

/* a big reading centred on (160, cy): digits at a fixed width (or words), then a smaller part on its baseline;
   colon: how much its colons show (0 to 32), for the clock's beat */
static void big(const char *s, const char *small, int cy, int cap, bool words, int a, int colon) {
  int ks, w, ws;
  for (;; cap -= 16) {
    ks = cap * 3 / 10 > PX(9) ? cap * 3 / 10 : PX(9);
    w = words ? text_w(s, cap) : digits_w(s, cap), ws = *small ? text_w(small, ks) + cap / 10 : 0;
    if (cap <= PX(14) || w + ws <= PX(296)) break;
  }
  int x = PX(160) - (w + ws) / 2, base = cy + cap / 32, bold = cap > PX(40) ? -14 : 0;
  if (words) {
    say(s, x, base, cap, bold, a);
  } else if (colon >= 32) {
    digits(s, x, base, cap, bold, a);
  } else { /* the colons drawn on their own, fading */
    char part[16];
    int i = 0, px = x;
    for (const char *p = s;; p++) {
      if (*p == ':' || !*p) {
        part[i] = 0;
        digits(part, px, base, cap, bold, a), px += digits_w(part, cap), i = 0;
        if (!*p) break;
        digits(":", px, base, cap, bold, a * colon >> 5), px += digits_w(":", cap);
      } else
        part[i++] = *p;
    }
  }
  if (*small) say(small, x + w + cap / 10, base, ks, 0, a * 26 >> 5);
}

static void addon_draw(void) {
  char s[16] = {0}, m[8] = {0}, *o = s;
  int a = 32, colon = 32;
  switch (V.addon) {
    case A_CLOCK: {
      uint32_t t = (V.clock0 + now / 1000) % 86400, h = t / 3600;
      if (V.h12) o = num(o, (int32_t)(h % 12 ? h % 12 : 12));
      else o = two(o, h);
      *o++ = ':';
      two(o, t / 60 % 60);
      if (V.h12) m[0] = h < 12 ? 'A' : 'P', m[1] = 'M';
      else m[0] = ':', two(m + 1, t % 60);
      int ms = (int)(now % 1000); /* the colon breathes with the seconds */
      colon = 12 + (ms < 500 ? 20 : clampi(20 - (ms - 500) / 12, 0, 20));
      break;
    }
    case A_STOPWATCH: {
      uint32_t ms = stopwatch(), sec = ms / 1000;
      if (sec >= 3600) o = num(o, (int32_t)(sec / 3600)), *o++ = ':';
      o = two(o, sec / 60 % 60);
      *o++ = ':';
      two(o, sec % 60);
      if (sec < 3600) m[0] = '.', two(m + 1, ms / 10 % 100);
      if (!V.sw_on) a = 20;
      break;
    }
    case A_TIMER: {
      uint32_t sec = (timer_left() + 999) / 1000;
      o = two(o, sec / 60);
      *o++ = ':';
      two(o, sec % 60);
      if (V.tm_on == 0) a = 20;
      if (V.tm_on == 2) { /* time's up: it blinks, and the screen pulses */
        a = now % 800 < 400 ? 32 : 8;
        shade(0, top, W, SH, WHITE, (isin((int)now / 2) + 16384) >> 12);
      }
      break;
    }
    case A_COUNTER: num(o, V.counter); big(s, "", 120, PX(80), false, 32, 32); return;
    case A_TEXT: big(V.text, "", 120, PX(56), true, 32, 32); return;
    default: return;
  }
  big(s, m, 120, PX(72), false, a, colon);
}

/* ------------------------------------------------------------------ the app */
enum { M_BG, M_ADDON, M_SET, M_SHOW };
static int mode, field, typed, set_v[3];
static uint32_t hint_t, toast_t;
static bool text_wait;
static const bg_t *cur;

static void choose_bg(int i) {
  V.bg = (uint8_t)((i + nbg) % nbg);
  cur = &bgs[V.bg];
  if (cur->init) cur->init();
  if (cur->frame) cur->frame(0);
}

/* OK on an add-on: set it up if it needs it, else show it */
static void open_addon(void) {
  mode = M_SET, field = 0, typed = 0;
  if (V.addon == A_CLOCK) {
    uint32_t t = (V.clock0 + now / 1000) % 86400;
    set_v[0] = (int)(t / 3600), set_v[1] = (int)(t / 60 % 60), set_v[2] = V.h12;
  } else if (V.addon == A_TIMER) {
    set_v[0] = (int)(V.tm_len / 60000), set_v[1] = (int)(V.tm_len / 1000 % 60);
  } else if (V.addon == A_TEXT) {
    text_wait = true;
  } else {
    mode = M_SHOW, hint_t = now;
  }
}
static void apply_set(void) {
  if (V.addon == A_CLOCK) {
    V.clock0 = ((uint32_t)(set_v[0] * 3600 + set_v[1] * 60) + 86400 - now / 1000 % 86400) % 86400;
    V.h12 = (uint8_t)set_v[2];
  } else if (V.addon == A_TIMER) {
    V.tm_len = (uint32_t)(set_v[0] * 60 + set_v[1]) * 1000;
    if (!V.tm_len) V.tm_len = 60000;
    V.tm_left = V.tm_len, V.tm_on = 1, V.tm_t0 = now;
  }
  mode = M_SHOW, hint_t = now;
}

/* a typed character (Alpha gives letters), or 0 */
static char event_char(int e) {
  static const char letters[] = "abcdefghijklmnopq\1rstuv\1wxyz \1?!";
  static const uint8_t keys[] = {30, 31, 32, 36, 37, 38, 42, 43, 44, 48, 49, 22, 45, 46, 39, 40, 33, 34, 122, 123, 124, 125, 81, 82, 83, 76};
  static const char chars[] = "7894561230.,+-*/():;\"%=<>_";
  if (e >= 126 && e < 158) return letters[e - 126] == 1 ? 0 : letters[e - 126];
  if (e >= 180 && e < 208) return letters[e - 180] >= 'a' ? (char)(letters[e - 180] - 32) : 0;
  for (unsigned i = 0; i < sizeof keys; i++)
    if (keys[i] == e) return chars[i];
  return 0;
}

static const char *hint(void) {
  switch (V.addon) {
    case A_CLOCK: case A_TEXT: return T("OK  change     BACK  menu");
    case A_STOPWATCH: case A_TIMER: return T("OK  go, stop     DEL  reset");
    case A_COUNTER: return T("OK  +1     -  -1     DEL  0");
    default: return T("BACK  menu");
  }
}

/* a chevron of light, pointing left (d -1) or right (d 1), its tip at (x, y) */
static void chevron(int x, int y, int d) {
  for (int o = 0; o < 2; o++) {
    int tx = (x - d * o) * 16;
    glow_line(tx, y * 16, tx - d * 112, y * 16 - 128, 0xFFFFFF, 220, 120);
    glow_line(tx, y * 16, tx - d * 112, y * 16 + 128, 0xFFFFFF, 220, 120);
  }
}
/* n dots, the i-th lit */
static void dots(int n, int i, int y) {
  int x0 = 160 - (n - 1) * 5;
  for (int k = 0; k < n; k++) glow_dot((x0 + k * 10) * 16 + 8, y * 16 + 8, k == i ? 44 : 28, 0xFFFFFF, k == i ? 256 : 70);
}

static void ui_draw(void) {
  int fade = mode == M_SHOW ? clampi((int)(32 - ((int32_t)(now - hint_t) - 2500) / 16), 0, 32) : 32;
  int toast = mode == M_SHOW ? clampi((int)(32 - ((int32_t)(now - toast_t) - 1500) / 16), 0, 32) : 0;
  if (mode == M_BG || mode == M_ADDON) {
    bool b = mode == M_BG;
    veil(140, H, 0, 190);
    say_c(b ? T("BACKGROUND") : T("ADD-ON"), 160, 183, PX(7), 4, 18);
    say_c(b ? cur->name : addon_names[V.addon], 160, 210, PX(18), 6, 32);
    chevron(22, 204, -1), chevron(298, 204, 1);
    dots(b ? nbg : A_COUNT, b ? V.bg : V.addon, 223);
    say_c(b ? T("OK  add-ons     BACK  quit") : T("OK  choose     BACK  back"), 160, 236, PX(6), 2, 17);
  } else if (mode == M_SET) {
    static const char *const titles[3] = {T("SET THE TIME"), T("SET THE TIMER"), T("TYPE YOUR TEXT")};
    bool blink = now % 1000 < 600;
    veil(0, 44, 140, 0);
    veil(196, H, 0, 170);
    say_c(titles[V.addon == A_CLOCK ? 0 : V.addon == A_TIMER ? 1 : 2], 160, 26, PX(8), 4, 24);
    if (V.addon == A_TEXT) {
      char s[26], *o = s;
      for (int i = 0; V.text[i]; i++) *o++ = V.text[i];
      *o++ = blink ? '_' : ' ';
      *o = 0;
      big(s, "", 120, PX(44), true, 32, 32);
      say_c(T("ALPHA  letters     OK  done"), 160, 230, PX(6), 2, 18);
    } else {
      char s[8], *o = two(s, (unsigned)set_v[0]);
      *o++ = ':';
      *two(o, (unsigned)set_v[1]) = 0;
      int cap = PX(60), w = digits_w(s, cap), x = PX(160) - w / 2, fw = digits_w("00", cap), base = 136;
      digits(s, x, base, cap, -14, 32);
      if (field < 2) { /* the field being set: a bar of light under it */
        int fx = (x + field * (w - fw)) / 16;
        shade(fx + 4, base + 9, fw / 16 - 8, 3, WHITE, blink ? 30 : 12);
      }
      if (V.addon == A_CLOCK) {
        const char *f = set_v[2] ? T("12-hour") : T("24-hour");
        int tw = text_w(f, PX(9)) / 16;
        if (field == 2) shade(160 - tw / 2 - 10, 169, tw + 20, 20, WHITE, blink ? 9 : 5);
        say_c(f, 160, 184, PX(9), 4, 32);
      }
      say_c(T("ARROWS  set     OK  done"), 160, 230, PX(6), 2, 18);
    }
  } else if (fade || toast) {
    veil(190, H, 0, 150 * (fade > toast ? fade : toast) / 32);
    if (toast) say_c(cur->name, 160, 214, PX(11), 6, toast);
    say_c(hint(), 160, 233, PX(6), 2, fade * 18 / 32);
  }
}

static void save(void) {
  V.magic = 'V', V.version = 1;
  ef_write(SAVE_NAME, &V, sizeof V);
}

/* frames drawn, the slowest one's work and how many took over 33 ms (tools/emu.py reads them) */
__attribute__((used)) uint32_t perf_frames, perf_max, perf_slow;

int main(void) {
  np_app_begin();
  now = eadk_timing_millis();
  core_init(now);
  uint32_t n = 0;
  const uint8_t *d = ef_read(SAVE_NAME, &n);
  if (d && n == sizeof V && d[0] == 'V' && d[1] == 1) {
    for (uint32_t i = 0; i < n; i++) ((uint8_t *)&V)[i] = d[i];
    V.text[23] = 0;
    V.addon %= A_COUNT;
    if (now < V.sw_t0) V.sw_t0 = now; /* the calculator restarted */
    if (now < V.tm_t0) V.tm_t0 = now;
    mode = M_SHOW, hint_t = now;
  } else {
    V.tm_len = V.tm_left = 300000;
    V.clock0 = 12 * 3600;
  }
  choose_bg(V.bg);
  uint64_t held = eadk_keyboard_scan();
  uint32_t last = now, repeat_at = 0;
  for (;;) {
    now = eadk_timing_millis();
    int dt = clampi((int)(now - last), 0, 100);
    last = now;
    uint64_t k = eadk_keyboard_scan(), hit = k & ~held;
    const uint64_t rep = KEY(eadk_key_left) | KEY(eadk_key_right) | KEY(eadk_key_up) | KEY(eadk_key_down) |
                         KEY(eadk_key_plus) | KEY(eadk_key_minus);
    if (hit & rep) repeat_at = now + 380;
    else if ((k & rep) && (int32_t)(now - repeat_at) >= 0) hit |= k & rep, repeat_at = now + 70;
    held = k;
    if (k & (KEY(eadk_key_home) | KEY(eadk_key_on_off))) break;
    bool ok = hit & (KEY(eadk_key_ok) | KEY(eadk_key_exe)), back = hit & KEY(eadk_key_back);
    int lr = (hit & KEY(eadk_key_right) ? 1 : 0) - (hit & KEY(eadk_key_left) ? 1 : 0);
    int ud = (hit & KEY(eadk_key_up) ? 1 : 0) - (hit & KEY(eadk_key_down) ? 1 : 0);
    if (V.tm_on == 1 && !timer_left()) V.tm_on = 2, V.tm_left = 0; /* time's up */
    if (mode == M_SET && V.addon == A_TEXT) { /* typing goes through the calculator's events, for Alpha */
      if (text_wait) {
        text_wait = k != 0;
      } else {
        int32_t timeout = 0;
        int e = eadk_event_get(&timeout), len = slen(V.text);
        if (e == 6 || e == 8) break;
        char c = event_char(e);
        if (e == eadk_event_ok || e == eadk_event_exe || e == eadk_event_back) mode = M_SHOW, hint_t = now;
        else if (e == eadk_event_backspace && len) V.text[len - 1] = 0;
        else if (c && len < 23) V.text[len] = c, V.text[len + 1] = 0;
      }
    } else if (mode == M_BG) {
      if (lr) choose_bg(V.bg + lr);
      if (ok) mode = M_ADDON;
      if (back) break;
    } else if (mode == M_ADDON) {
      if (lr) V.addon = (uint8_t)((V.addon + A_COUNT + lr) % A_COUNT);
      if (ok) open_addon();
      if (back) mode = M_BG;
    } else if (mode == M_SET) {
      int n = V.addon == A_CLOCK ? 3 : 2, max = field == 2 ? 2 : field == 1 ? 60 : V.addon == A_CLOCK ? 24 : 100;
      if (lr) field = clampi(field + lr, 0, n - 1), typed = 0;
      if (ud) set_v[field] = (set_v[field] + max + ud) % max, typed = 0;
      static const int8_t digit_keys[10] = {48, 42, 43, 44, 36, 37, 38, 30, 31, 32};
      for (int i = 0; i < 10; i++)
        if ((hit & KEY(digit_keys[i])) && field < 2) {
          set_v[field] = clampi(typed ? set_v[field] * 10 + i : i, 0, max - 1);
          if (++typed == 2) typed = 0, field = field + 1 < n ? field + 1 : field;
        }
      if (ok) apply_set();
      if (back) mode = M_ADDON;
    } else {
      if (back) mode = M_ADDON;
      if (lr) choose_bg(V.bg + lr), toast_t = now;
      if (V.addon == A_STOPWATCH) {
        if (ok) {
          if (V.sw_on) V.sw_acc = stopwatch(), V.sw_on = 0;
          else V.sw_t0 = now, V.sw_on = 1;
        }
        if (hit & KEY(eadk_key_backspace)) V.sw_acc = 0, V.sw_t0 = now;
      } else if (V.addon == A_TIMER) {
        if (ok) {
          if (V.tm_on == 1) V.tm_left = timer_left(), V.tm_on = 0;
          else if (V.tm_on == 2) V.tm_on = 0, V.tm_left = V.tm_len;
          else V.tm_left = V.tm_left ? V.tm_left : V.tm_len, V.tm_on = 1, V.tm_t0 = now;
        }
        if (hit & KEY(eadk_key_backspace)) V.tm_on = 0, V.tm_left = V.tm_len;
      } else if (V.addon == A_COUNTER) {
        if (ok || (hit & (KEY(eadk_key_up) | KEY(eadk_key_plus)))) V.counter += V.counter < 999999999;
        if (hit & (KEY(eadk_key_down) | KEY(eadk_key_minus))) V.counter -= V.counter > -999999999;
        if (hit & KEY(eadk_key_backspace)) V.counter = 0;
      } else if (ok && (V.addon == A_CLOCK || V.addon == A_TEXT)) {
        open_addon();
      }
    }
    if (cur->frame) cur->frame(dt);
    eadk_display_wait_for_vblank();
    for (top = 0; top < H; top += SH) {
      cur->draw();
      if (mode == M_ADDON || mode == M_SHOW) addon_draw();
      ui_draw();
      eadk_display_push_rect((eadk_rect_t){0, (uint16_t)top, W, SH}, buf);
    }
    uint32_t spent = eadk_timing_millis() - now;
    perf_frames++, perf_max = spent > perf_max ? spent : perf_max, perf_slow += spent > 33;
    if (spent < 33) eadk_timing_msleep(33 - spent);
  }
  save();
  return np_app_end();
}
