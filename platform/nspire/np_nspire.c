/* NumPlay on the TI-Nspire CX and CX II (Ndless): the 320 x 240 screen through lcd_blit, the keys from the keypad
 * (platform/nspire/README.md), every game's files in /documents/ndless/numplay.sav.tns.
 *
 * Time: Ndless's clock counts whole seconds, so the second of the fast timers (0x900D0020) runs free at 32768 Hz
 * while the game runs, as the Nspire's SDL does, and is put back as it was at the end. If it doesn't count, time
 * goes by 16 ms a screen drawn (a game's pace stays right, only its clock is rough).
 *
 * Waits read that clock. Not Ndless's msleep: it waits for the timer's interrupt with every other one masked, the ON
 * key's too, and on real calculators the games calling it hundreds of times a second (Undertale, Isaac, Among Us...)
 * soon waited there for good, with only the reset button left.
 *
 * Build defines: NP_APP_ID (the game's id). */
#include <libndls.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <eadk.h>
#include "np_port.h"
#include "../common/np_keys_drawn.h"

#ifndef NP_APP_ID
#define NP_APP_ID "numplay"
#endif
#ifndef NP_FILES_ID /* (its files' name: the game's own, or that of the game whose saves it shares) */
#define NP_FILES_ID NP_APP_ID
#endif

/* the RAM after a game's own: 1 MB (the CX has 64) */
__asm__(".section .bss\n.balign 16\n.globl _heap_start\n_heap_start:\n.space 1048576\n.globl _heap_end\n_heap_end:\n"
        ".previous\n");

static uint16_t fb[EADK_SCREEN_WIDTH * EADK_SCREEN_HEIGHT];
static bool started, dirty, timer_ok;
static uint64_t fallback_ms;
static uint32_t last_blit, drawn_at, waited_at, frame_ms;
/* the keys pressed while a frame is drawn (platform/common/np_keys_drawn.h): push_rect reads them when 30 ms have gone
   by since the last read. A game reads the keyboard once a frame, and at a few frames a second a tap came and went
   between two reads (Temple Run's turns, Mason363/NumPlay#446). A game reading the keys every 30 ms or sooner (over
   33 frames a second) is never read in between. */
static uint32_t keys_read;
static eadk_keyboard_state_t keys_now(void);

/* ---- the clock ---- */
#define T2 ((volatile uint32_t *)0x900D0020) /* load, value, control */
static uint32_t saved_load, saved_control;
static uint32_t ticks_hi, last_raw, spin_ms;
/* a busy loop of about n / spin_ms milliseconds (only for the time a dead clock can't measure) */
static void spin(uint32_t n) {
  for (uint32_t i = 0; i < n; i++) __asm__ volatile("");
}
static void timer_start(void) {
  spin_ms = is_cx2 ? 56000 : 18000; /* (the loop's turns a millisecond: about 7 cycles each, at 396 or 132 MHz) */
  saved_load = T2[0], saved_control = T2[2];
  T2[2] = 0;
  T2[0] = 0xFFFFFFFF;
  T2[2] = 0x82; /* enabled, 32 bits, free running, no interrupt */
  uint32_t before = T2[1];
  spin(2 * spin_ms);
  timer_ok = T2[1] != before;
  last_raw = 0xFFFFFFFF - T2[1];
}
static void timer_stop(void) {
  T2[2] = 0;
  T2[0] = saved_load;
  T2[2] = saved_control;
}
static uint64_t ticks(void) { /* 32768ths of a second since the start */
  uint32_t raw = 0xFFFFFFFF - T2[1];
  if (raw < last_raw) ticks_hi++;
  last_raw = raw;
  return (uint64_t)ticks_hi << 32 | raw;
}
/* n ticks of the clock go by (tools/nspire_emu.py skips the loop, as wfi) */
__attribute__((noinline)) void np_nspire_pause(uint32_t n) {
  uint64_t until = ticks() + n;
  while (ticks() < until) {}
}
static void pause_ms(uint32_t ms) {
  if (!ms) return;
  if (timer_ok) np_nspire_pause((uint32_t)((uint64_t)ms * 32768 / 1000));
  else spin(ms * spin_ms), fallback_ms += ms;
}

static void keys_load(void);
static void start(void) {
  if (started) return;
  started = true;
  lcd_init(SCR_320x240_565);
  timer_start();
  keys_load();
}

/* ---- the screen ---- */
/* What the game draws goes to fb, and fb to the screen whole where a frame ends: where the game waits
 * (wait_for_vblank, msleep, event_get) or, in a game that doesn't wait between its frames, where it reads the keys.
 * Shown from push_rect every 16 ms, as it was, a frame came out split, the old one under the new (NumDash's
 * levels, Dune's selection outline, Mason363/NumPlay#446). A frame drawn for long with neither (a loading screen)
 * is shown as it goes: after twice the time between the last two frames, from 300 ms to 1 s. */
static uint32_t clamp_ms(uint32_t ms, uint32_t lo, uint32_t hi) { return ms < lo ? lo : ms > hi ? hi : ms; }
static void blit(void) {
  if (!dirty) return;
  dirty = false;
  lcd_blit(fb, SCR_320x240_565);
  if (!timer_ok) fallback_ms += 16;
  uint32_t now = (uint32_t)eadk_timing_millis();
  frame_ms = now - last_blit, last_blit = now;
}
/* the game waits: its frame is drawn */
static void waits(void) {
  blit();
  waited_at = (uint32_t)eadk_timing_millis();
}
static void drawn(void) {
  uint32_t now = (uint32_t)eadk_timing_millis();
  if (!dirty) dirty = true, drawn_at = now;
  else if (now - drawn_at >= clamp_ms(2 * frame_ms, 300, 1000)) blit();
  if (now - keys_read >= 30) np_keys_drawing(keys_now()), keys_read = now;
}
void eadk_display_push_rect(eadk_rect_t r, const eadk_color_t *px) {
  start();
  for (int y = 0; y < r.height && r.y + y < EADK_SCREEN_HEIGHT; y++) {
    int w = r.x + r.width > EADK_SCREEN_WIDTH ? EADK_SCREEN_WIDTH - r.x : r.width;
    if (w > 0) memcpy(&fb[(r.y + y) * EADK_SCREEN_WIDTH + r.x], px + y * r.width, (size_t)w * 2);
  }
  drawn();
}
void eadk_display_push_rect_uniform(eadk_rect_t r, eadk_color_t c) {
  start();
  for (int y = r.y; y < r.y + r.height && y < EADK_SCREEN_HEIGHT; y++)
    for (int x = r.x; x < r.x + r.width && x < EADK_SCREEN_WIDTH; x++) fb[y * EADK_SCREEN_WIDTH + x] = c;
  if (!dirty) dirty = true, drawn_at = (uint32_t)eadk_timing_millis(); /* (games fill a pixel a call: no more here) */
}
void eadk_display_pull_rect(eadk_rect_t r, eadk_color_t *px) {
  for (int y = 0; y < r.height; y++)
    for (int x = 0; x < r.width; x++) {
      int sx = r.x + x, sy = r.y + y;
      px[y * r.width + x] = sx < EADK_SCREEN_WIDTH && sy < EADK_SCREEN_HEIGHT ? fb[sy * EADK_SCREEN_WIDTH + sx] : 0;
    }
}
bool eadk_display_wait_for_vblank(void) {
  start();
  waits();
  if (timer_ok) { /* as the NumWorks's: till the screen's next refresh, 60 a second (games pace themselves by it) */
    uint64_t now = ticks(), next = (now * 60 / 32768 + 1) * 32768 / 60;
    np_nspire_pause((uint32_t)(next - now));
  }
  return true;
}

/* ---- the keys: the NumWorks keypad on the Nspire's, as numplay_keys.tns may change it ---- */
/* Each Nspire key and the NumWorks key it gives (NONE: nothing), named as on the Nspire's keypad for
 * numplay_keys.tns (docs/calculators/ti-nspire.md has the table). The letters give the NumWorks key that types each
 * one with alpha (a e^x, b ln, c log, d i, g sin, h cos, k √...), space its minus and ?! its 0, as the NumWorks types
 * them. On a CX, sin and cos are only there, as g and h. The keys at the end give nothing unless the file says. */
#define NONE 0xFF
typedef struct {
  const char *name;
  const t_key *key;
  uint8_t to;
} nkey_t;
#define K(s, n, k) {s, &KEY_NSPIRE_##n, k}
static nkey_t KEYS[] = {
  K("enter", ENTER, eadk_key_ok), K("return", RET, eadk_key_exe), K("esc", ESC, eadk_key_back),
  K("home", HOME, eadk_key_home), K("del", DEL, eadk_key_backspace), K("shift", SHIFT, eadk_key_shift),
  K("ctrl", CTRL, eadk_key_alpha), K("menu", MENU, eadk_key_toolbox), K("var", VAR, eadk_key_var),
  K("tab", TAB, eadk_key_xnt), K("0", 0, eadk_key_zero), K("1", 1, eadk_key_one), K("2", 2, eadk_key_two),
  K("3", 3, eadk_key_three), K("4", 4, eadk_key_four), K("5", 5, eadk_key_five), K("6", 6, eadk_key_six),
  K("7", 7, eadk_key_seven), K("8", 8, eadk_key_eight), K("9", 9, eadk_key_nine), K("+", PLUS, eadk_key_plus),
  K("-", MINUS, eadk_key_minus), K("*", MULTIPLY, eadk_key_multiplication), K("/", DIVIDE, eadk_key_division),
  K(".", PERIOD, eadk_key_dot), K(",", COMMA, eadk_key_comma), K("(", LP, eadk_key_left_parenthesis),
  K(")", RP, eadk_key_right_parenthesis), K("^", EXP, eadk_key_power), K("x2", SQU, eadk_key_square),
  K("pi", PI, eadk_key_pi), K("sin", SIN, eadk_key_sine), K("cos", COS, eadk_key_cosine), K("tan", TAN, eadk_key_tangent),
  K("ee", EE, eadk_key_ee), K("(-)", NEGATIVE, eadk_key_ans), K("e^x", eEXP, eadk_key_exp),
  K("a", A, eadk_key_exp), K("b", B, eadk_key_ln), K("c", C, eadk_key_log), K("d", D, eadk_key_imaginary),
  K("e", E, eadk_key_comma), K("f", F, eadk_key_power), K("g", G, eadk_key_sine), K("h", H, eadk_key_cosine),
  K("i", I, eadk_key_tangent), K("j", J, eadk_key_pi), K("k", K, eadk_key_sqrt), K("l", L, eadk_key_square),
  K("m", M, eadk_key_seven), K("n", N, eadk_key_eight), K("o", O, eadk_key_nine), K("p", P, eadk_key_left_parenthesis),
  K("q", Q, eadk_key_right_parenthesis), K("r", R, eadk_key_four), K("s", S, eadk_key_five), K("t", T, eadk_key_six),
  K("u", U, eadk_key_multiplication), K("v", V, eadk_key_division), K("w", W, eadk_key_one), K("x", X, eadk_key_two),
  K("y", Y, eadk_key_three), K("z", Z, eadk_key_plus), K("space", SPACE, eadk_key_minus),
  K("?!", QUESEXCL, eadk_key_zero),
  K("doc", DOC, NONE), K("cat", CAT, NONE), K("=", EQU, NONE), K("trig", TRIG, NONE), K("templates", FRAC, NONE),
  K("flag", FLAG, NONE), K("scratchpad", SCRATCHPAD, NONE), K("10^x", TENX, NONE), K("theta", THETA, NONE),
  K("\"", QUOTE, NONE), K("'", APOSTROPHE, NONE), K(":", COLON, NONE), K("|", BAR, NONE), K(">", GTHAN, NONE),
  K("<", LTHAN, NONE), K("?", QUES, NONE),
};
/* the arrows: the touchpad's regions (tpad_arrow_t: the corners two arrows, the middle the click), or the clickpad's
 * keys; what each gives: up, right, down, left, the click */
static uint8_t PAD_TO[5] = {eadk_key_up, eadk_key_right, eadk_key_down, eadk_key_left, eadk_key_ok};
static const char *const PAD_NAMES[5] = {"up", "right", "down", "left", "click"};
static eadk_keyboard_state_t PAD[10];
static const t_key *const PAD_KEYS[] = {NULL, &KEY_NSPIRE_UP, &KEY_NSPIRE_UPRIGHT, &KEY_NSPIRE_RIGHT,
                                        &KEY_NSPIRE_RIGHTDOWN, &KEY_NSPIRE_DOWN, &KEY_NSPIRE_DOWNLEFT,
                                        &KEY_NSPIRE_LEFT, &KEY_NSPIRE_LEFTUP, &KEY_NSPIRE_CLICK};
static eadk_keyboard_state_t pad_bit(int d) { return PAD_TO[d] == NONE ? 0 : (eadk_keyboard_state_t)1 << PAD_TO[d]; }
static void pad_masks(void) {
  static const int8_t DIRS[10][2] = {{-1, -1}, {0, -1}, {0, 1}, {1, -1}, {1, 2}, {2, -1}, {2, 3}, {3, -1}, {3, 0}, {4, -1}};
  for (int i = 0; i < 10; i++) PAD[i] = (DIRS[i][0] < 0 ? 0 : pad_bit(DIRS[i][0])) | (DIRS[i][1] < 0 ? 0 : pad_bit(DIRS[i][1]));
}

/* The NumWorks keys by name, for numplay_keys.tns (with the digits' and the operators' signs) */
static const struct {
  const char *name;
  uint8_t key;
} NW[] = {
  {"left", eadk_key_left}, {"up", eadk_key_up}, {"down", eadk_key_down}, {"right", eadk_key_right},
  {"ok", eadk_key_ok}, {"back", eadk_key_back}, {"home", eadk_key_home}, {"onoff", eadk_key_on_off},
  {"shift", eadk_key_shift}, {"alpha", eadk_key_alpha}, {"xnt", eadk_key_xnt}, {"x,n,t", eadk_key_xnt},
  {"var", eadk_key_var}, {"toolbox", eadk_key_toolbox}, {"backspace", eadk_key_backspace}, {"exp", eadk_key_exp},
  {"e^x", eadk_key_exp}, {"ln", eadk_key_ln}, {"log", eadk_key_log}, {"i", eadk_key_imaginary},
  {"comma", eadk_key_comma}, {",", eadk_key_comma}, {"power", eadk_key_power}, {"^", eadk_key_power},
  {"sin", eadk_key_sine}, {"cos", eadk_key_cosine}, {"tan", eadk_key_tangent}, {"pi", eadk_key_pi},
  {"sqrt", eadk_key_sqrt}, {"square", eadk_key_square}, {"x2", eadk_key_square}, {"(", eadk_key_left_parenthesis},
  {")", eadk_key_right_parenthesis}, {"*", eadk_key_multiplication}, {"/", eadk_key_division}, {"+", eadk_key_plus},
  {"-", eadk_key_minus}, {"0", eadk_key_zero}, {"1", eadk_key_one}, {"2", eadk_key_two}, {"3", eadk_key_three},
  {"4", eadk_key_four}, {"5", eadk_key_five}, {"6", eadk_key_six}, {"7", eadk_key_seven}, {"8", eadk_key_eight},
  {"9", eadk_key_nine}, {".", eadk_key_dot}, {"ee", eadk_key_ee}, {"ans", eadk_key_ans}, {"exe", eadk_key_exe},
  {"none", NONE},
};

/* numplay_keys.tns: lines "nspire key = numworks key" (# starts a note), as text, next to the game or in
 * /documents/ndless; for every game. "del = back", "up = 8", "click = none"... */
static const char *program; /* the game's file (main's argv[0]: port.mk links main through __wrap_main) */
int __real_main(int argc, char **argv);
int __wrap_main(int argc, char **argv) {
  program = argc > 0 ? argv[0] : NULL;
  return __real_main(argc, argv);
}
static char *trim(char *s) {
  while (*s == ' ' || *s == '\t') s++;
  char *e = s + strlen(s);
  while (e > s && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
  for (char *p = s; *p; p++)
    if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');
  return s;
}
/* other names the keys go by (the signs as printed, as typed on a computer) */
static const char *const ALIASES[][2] = {
  {"ret", "return"}, {"x\xc2\xb2", "x2"}, {"\xc3\x97", "*"}, {"\xc3\xb7", "/"}, {"\xcf\x80", "pi"}, {"\xce\xb8", "theta"},
  {"negative", "(-)"}, {"centre", "click"}, {"center", "click"}, {"multiply", "*"}, {"divide", "/"}, {"plus", "+"},
  {"minus", "-"}, {"\xe2\x88\x9a", "sqrt"}, {"\xe2\x8c\xab", "backspace"},
};
static const char *alias(const char *s) {
  for (unsigned i = 0; i < sizeof ALIASES / sizeof ALIASES[0]; i++)
    if (!strcmp(s, ALIASES[i][0])) return ALIASES[i][1];
  return s;
}
static void keys_line(char *line) {
  char *a = trim(line), *eq = *a ? strchr(a + 1, '=') : NULL; /* (the key "=" itself: "= = ok") */
  if (!eq) return;
  *eq = 0;
  const char *from = alias(trim(a)), *name = alias(trim(eq + 1));
  int to = -1;
  for (unsigned i = 0; i < sizeof NW / sizeof NW[0]; i++)
    if (!strcmp(name, NW[i].name)) to = NW[i].key;
  if (to < 0) return;
  for (unsigned i = 0; i < sizeof KEYS / sizeof KEYS[0]; i++)
    if (!strcmp(from, KEYS[i].name)) KEYS[i].to = (uint8_t)to;
  for (int d = 0; d < 5; d++)
    if (!strcmp(from, PAD_NAMES[d])) PAD_TO[d] = (uint8_t)to;
}
static void keys_load(void) {
  char path[256] = "", text[4096];
  if (program && strlen(program) < sizeof path - 20) {
    strcpy(path, program);
    char *slash = strrchr(path, '/');
    if (slash) strcpy(slash + 1, "numplay_keys.tns");
    else path[0] = 0;
  }
  NUC_FILE *f = path[0] ? nuc_fopen(path, "rb") : NULL;
  if (!f) f = nuc_fopen("/documents/ndless/numplay_keys.tns", "rb");
  if (f) {
    size_t n = nuc_fread(text, 1, sizeof text - 1, f);
    nuc_fclose(f);
    text[n] = 0;
    for (char *line = text, *next; *line; line = next) {
      next = line + strcspn(line, "\r\n");
      if (*next) *next++ = 0;
      char *hash = strchr(line, '#');
      if (hash) *hash = 0;
      keys_line(line);
    }
  }
  pad_masks();
}
/* The pad: its arrows, and its middle (the click) only on its own. A click with an arrow of the pad, or begun within
 * 150 ms of one, gives nothing till it's let go: a thumb on an arrow slipping to the middle, or pressing it with the
 * arrow, opened Monkey Mart's menu while walking (Mason363/NumPlay#446). On the touchpad, a press going on from an
 * arrow to the middle keeps its arrow. enter always gives OK. */
#define CLICK_AFTER_ARROW_MS 150
static eadk_keyboard_state_t pad_now(void) {
  static uint32_t arrow_until; /* till when a click is taken for a slip off an arrow */
  static int pressed_arrow;    /* the touchpad's arrow in the press going on (0: none) */
  static bool click_off;
  eadk_keyboard_state_t s = 0;
  int arrow = 0;
  bool click = false;
  if (is_touchpad) { /* read once: isKeyPressed reads it over I2C for each arrow */
    touchpad_report_t r;
    if (!touchpad_scan(&r) && r.pressed && r.arrow < sizeof PAD / sizeof PAD[0]) {
      if (r.arrow == TPAD_ARROW_CLICK) click = true, arrow = pressed_arrow;
      else if (r.arrow != TPAD_ARROW_NONE) arrow = pressed_arrow = r.arrow;
    } else
      pressed_arrow = 0;
  } else {
    for (int i = 1; i < 9; i++)
      if (isKeyPressed(*PAD_KEYS[i])) s |= PAD[i], arrow = i;
    click = isKeyPressed(KEY_NSPIRE_CLICK);
  }
  uint32_t now = (uint32_t)eadk_timing_millis();
  if (arrow) s |= PAD[arrow], arrow_until = now + CLICK_AFTER_ARROW_MS;
  if (!click) click_off = false;
  else if (arrow || (int32_t)(arrow_until - now) > 0) click_off = true;
  if (click && !click_off) s |= PAD[TPAD_ARROW_CLICK];
  return s;
}
static eadk_keyboard_state_t keys_now(void) {
  eadk_keyboard_state_t s = pad_now();
  for (unsigned i = 0; i < sizeof KEYS / sizeof KEYS[0]; i++)
    if (KEYS[i].to != NONE && isKeyPressed(*KEYS[i].key)) s |= (eadk_keyboard_state_t)1 << KEYS[i].to;
  if (on_key_pressed()) s |= (eadk_keyboard_state_t)1 << eadk_key_home | (eadk_keyboard_state_t)1 << eadk_key_on_off;
  return s;
}
eadk_keyboard_state_t eadk_keyboard_scan(void) {
  start();
  /* the frame's end in a game that doesn't wait between frames (60 a second at most): not in one that waited
     lately, whose keys may be read while it draws (NumDash reads them between its strips) */
  uint32_t now = (uint32_t)eadk_timing_millis();
  if (dirty && now - last_blit >= 16 && now - waited_at >= clamp_ms(2 * frame_ms, 100, 1000)) blit();
  keys_read = now;
  return np_keys_scan(keys_now());
}
eadk_event_t eadk_event_get(int32_t *timeout) {
  static eadk_keyboard_state_t before = ~(eadk_keyboard_state_t)0;
  uint64_t until = eadk_timing_millis() + (uint64_t)(timeout && *timeout > 0 ? *timeout : 0);
  waits(); /* (what was drawn is shown while the key is awaited) */
  do {
    eadk_keyboard_state_t now = eadk_keyboard_scan(), hit = now & ~before;
    before = now;
    for (int k = 0; k < 64; k++)
      if (hit >> k & 1) return (eadk_event_t)k;
    pause_ms(5);
  } while (eadk_timing_millis() < until);
  if (timeout) *timeout = 0;
  return eadk_event_none;
}

/* ---- time, chance, the rest ---- */
uint64_t eadk_timing_millis(void) {
  start();
  return timer_ok ? ticks() * 1000 / 32768 : fallback_ms;
}
void eadk_timing_msleep(uint32_t ms) {
  start();
  waits();
  pause_ms(ms);
}
void eadk_timing_usleep(uint32_t us) { eadk_timing_msleep((us + 999) / 1000); }
uint32_t eadk_random(void) {
  static uint32_t x;
  if (!x) x = (uint32_t)ticks() ^ *(volatile uint32_t *)0x90090000 ^ 0x9E3779B9u;
  x ^= x << 13, x ^= x >> 17, x ^= x << 5;
  return x;
}
void eadk_backlight_set_brightness(uint8_t b) { (void)b; }
uint8_t eadk_backlight_brightness(void) { return 255; }
bool eadk_battery_is_charging(void) { return false; }
uint8_t eadk_battery_level(void) { return 3; }
float eadk_battery_voltage(void) { return 4.0f; }
bool eadk_usb_is_plugged(void) { return false; }
const char *eadk_external_data = NULL;
size_t eadk_external_data_size = 0;

/* ---- the files (the OS's own calls: newlib's fopen leaves its FILE pool on the OS's heap till a restart) ---- */
/* Every game's records are in one file, /documents/ndless/numplay.sav.tns: the Nspire's file browser slows down with
 * each file there is, and each game made one more (Mason363/NumPlay#446). The file is a log: "NPS1", then entries,
 * [the id's length (1 byte)][the game's files id][the records' size (4)][their check (4)][the records], a game's
 * records its last entry.
 *
 * A save adds an entry at the end: as quick as a game's own file was, and an entry cut short (the calculator reset
 * while it was written) ends the log there, the one before it kept. When the log would be over twice what the last
 * entries take (and over 64 KB), or after an entry cut short, it's written again with the last entries alone, into
 * numplay.sav.new.tns, which then takes its name (reset in between, the new file is found at the next start).
 * A game's own file, from an older NumPlay (numplay_<id>.sav.tns) or written when the log couldn't be, is newer: it's
 * read instead, added to the log and removed. */
#define STORAGE_SIZE (64 * 1024)
static uint8_t storage[STORAGE_SIZE + 64];
static bool loaded;
#define DOCS "/documents/ndless/"
#define LOG_PATH DOCS "numplay.sav.tns"
#define NEW_PATH DOCS "numplay.sav.new.tns"
#define OWN_PATH DOCS "numplay_" NP_FILES_ID ".sav.tns"
#define ID_MAX 31
#define GAMES_MAX 256
static struct {
  char id[ID_MAX + 1];
  uint32_t at, size; /* the game's last entry: where, and its records' size */
} games[GAMES_MAX];
static unsigned n_games;
static uint32_t log_end; /* where the log's whole entries end */
/* the log is there; an entry cut short at log_end; more games than games[] holds (the log only grows); the log
   can't be written this time (renaming failed: the next start finds the new file); the game's own file is there */
static bool log_there, log_cut, log_full, log_off, own_file;

static uint32_t storage_end(void) {
  uint32_t at = 0;
  while (at + 2 <= STORAGE_SIZE && (storage[at] | storage[at + 1] << 8)) at += storage[at] | storage[at + 1] << 8;
  return at;
}
static uint32_t check(const uint8_t *p, uint32_t n) {
  uint32_t h = 2166136261u ^ n;
  while (n--) h = (h ^ *p++) * 16777619u;
  return h;
}
static uint32_t get32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void put32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24);
}
static unsigned game_slot(const char *id) { /* (GAMES_MAX: no room) */
  for (unsigned i = 0; i < n_games; i++)
    if (!strcmp(games[i].id, id)) return i;
  if (n_games == GAMES_MAX) return log_full = true, GAMES_MAX;
  strcpy(games[n_games].id, id), games[n_games].at = games[n_games].size = 0;
  return n_games++;
}
static uint32_t entry_size(const char *id, uint32_t size) { return 1 + (uint32_t)strlen(id) + 8 + size; }
static uint32_t entry_head(uint8_t *h, const uint8_t *data, uint32_t size) {
  uint32_t len = (uint32_t)strlen(NP_FILES_ID);
  h[0] = (uint8_t)len, memcpy(h + 1, NP_FILES_ID, len), put32(h + 1 + len, size), put32(h + 5 + len, check(data, size));
  return 1 + len + 8;
}

/* the log, read a block at a time */
static NUC_FILE *rf;
static uint8_t blk[4096];
static uint32_t blk_at, blk_n;
static bool log_read(uint32_t at, void *to, uint32_t n) {
  uint8_t *d = to;
  while (n) {
    if (at < blk_at || at >= blk_at + blk_n) {
      blk_at = at & ~(uint32_t)(sizeof blk - 1), blk_n = 0;
      if (nuc_fseek(rf, (long)blk_at, SEEK_SET)) return false;
      blk_n = (uint32_t)nuc_fread(blk, 1, sizeof blk, rf);
      if (at >= blk_at + blk_n) return false;
    }
    uint32_t k = blk_at + blk_n - at < n ? blk_at + blk_n - at : n;
    memcpy(d, blk + (at - blk_at), k), d += k, at += k, n -= k;
  }
  return true;
}

/* each game's last entry, and the game's own records into storage */
static void log_scan(void) {
  n_games = 0, log_end = 0, log_there = log_cut = log_full = false;
  if (!(rf = nuc_fopen(LOG_PATH, "rb"))) {
    if (!(rf = nuc_fopen(NEW_PATH, "rb"))) return;
    nuc_fclose(rf); /* (written again, and the calculator reset before it was renamed) */
    if (rename(NEW_PATH, LOG_PATH) || !(rf = nuc_fopen(LOG_PATH, "rb"))) return (void)(log_off = true);
  }
  log_there = true, blk_at = blk_n = 0;
  uint8_t h[ID_MAX + 9];
  uint32_t at = 4, good = 0, good_size = 0;
  bool found = false, last_good = false;
  if (!log_read(0, h, 4) || memcmp(h, "NPS1", 4)) at = 0, log_cut = true; /* (not a log: written again) */
  else
    for (;;) {
      if (!log_read(at, h, 1)) break; /* (the end) */
      uint32_t len = h[0], data = at + 1 + len + 8, size = 0;
      uint8_t last;
      if (!len || len > ID_MAX || !log_read(at + 1, h + 1, len + 8) || (size = get32(h + 1 + len)) > STORAGE_SIZE ||
          (size && !log_read(data + size - 1, &last, 1))) {
        log_cut = true; /* (cut short) */
        break;
      }
      char id[ID_MAX + 1];
      memcpy(id, h + 1, len), id[len] = 0;
      if (!strcmp(id, NP_FILES_ID)) { /* (one of the game's spoilt: the one before it) */
        last_good = log_read(data, storage, size) && check(storage, size) == get32(h + 5 + len);
        if (last_good) good = at, good_size = size, found = true;
      } else {
        unsigned i = game_slot(id);
        if (i < GAMES_MAX) games[i].at = at, games[i].size = size;
      }
      at = data + size;
    }
  log_end = at;
  if (found) {
    unsigned i = game_slot(NP_FILES_ID);
    if (i < GAMES_MAX) games[i].at = good, games[i].size = good_size;
    if (!last_good) log_read(good + entry_size(NP_FILES_ID, 0), storage, good_size);
  } else
    good_size = 0;
  memset(storage + good_size, 0, sizeof storage - good_size);
  nuc_fclose(rf);
}

/* the game's records at the log's end */
static bool log_add(uint32_t size) {
  NUC_FILE *f = nuc_fopen(LOG_PATH, "ab");
  if (!f) return false;
  uint8_t h[ID_MAX + 9];
  uint32_t hl = entry_head(h, storage, size);
  bool ok = nuc_fwrite(h, 1, hl, f) == hl && nuc_fwrite(storage, 1, size, f) == size;
  if (nuc_fclose(f)) ok = false;
  if (!ok) return log_cut = true, false;
  unsigned i = game_slot(NP_FILES_ID);
  if (i < GAMES_MAX) games[i].at = log_end, games[i].size = size;
  log_end += hl + size;
  return true;
}
/* the log written again: each other game's last entry, then the game's records */
static bool log_write_all(uint32_t size) {
  NUC_FILE *out = nuc_fopen(NEW_PATH, "wb");
  if (!out) return false;
  rf = log_there ? nuc_fopen(LOG_PATH, "rb") : NULL, blk_at = blk_n = 0;
  bool ok = nuc_fwrite("NPS1", 1, 4, out) == 4 && (rf || !log_there);
  unsigned ours = game_slot(NP_FILES_ID);
  for (unsigned i = 0; ok && rf && i < n_games; i++) {
    if (i == ours) continue;
    uint8_t buf[512];
    for (uint32_t from = games[i].at, n = entry_size(games[i].id, games[i].size); ok && n;) {
      uint32_t k = n < sizeof buf ? n : sizeof buf;
      ok = log_read(from, buf, k) && nuc_fwrite(buf, 1, k, out) == k;
      from += k, n -= k;
    }
  }
  if (rf) nuc_fclose(rf);
  uint8_t h[ID_MAX + 9];
  uint32_t hl = entry_head(h, storage, size);
  ok = ok && nuc_fwrite(h, 1, hl, out) == hl && nuc_fwrite(storage, 1, size, out) == size;
  if (nuc_fclose(out)) ok = false;
  if (!ok || (log_there && unlink(LOG_PATH))) { /* (Nucleus's rename doesn't replace a file) */
    unlink(NEW_PATH);
    return false;
  }
  if (rename(NEW_PATH, LOG_PATH)) return log_off = true, false;
  uint32_t at = 4;
  for (unsigned i = 0; i < n_games; i++)
    if (i != ours) games[i].at = at, at += entry_size(games[i].id, games[i].size);
  if (ours < GAMES_MAX) games[ours].at = at, games[ours].size = size;
  log_end = at + hl + size, log_there = true, log_cut = false;
  return true;
}

uint8_t *np_storage(uint32_t *size) {
  if (!loaded) {
    loaded = true;
    log_scan();
    NUC_FILE *f = nuc_fopen(OWN_PATH, "rb");
    if (f) {
      memset(storage, 0, sizeof storage);
      size_t n = nuc_fread(storage, 1, STORAGE_SIZE, f);
      nuc_fclose(f);
      if (storage_end() + 2 > n) memset(storage, 0, sizeof storage);
      own_file = true;
      np_storage_changed();
    }
  }
  *size = STORAGE_SIZE;
  return storage;
}
void np_storage_changed(void) {
  uint32_t size = storage_end() + 2, add = entry_size(NP_FILES_ID, size), live = add;
  for (unsigned i = 0; i < n_games; i++)
    if (strcmp(games[i].id, NP_FILES_ID)) live += entry_size(games[i].id, games[i].size);
  bool ok = false;
  if (!log_off) {
    if (log_there && !log_cut && (log_full || log_end + add <= 2 * live || log_end + add <= 64 * 1024))
      ok = log_add(size);
    else if (!log_full)
      ok = log_write_all(size);
  }
  if (ok) {
    if (own_file && !unlink(OWN_PATH)) own_file = false;
    return;
  }
  NUC_FILE *f = nuc_fopen(OWN_PATH, "wb"); /* (the log can't be written: the game's own file, as before) */
  if (!f) return;
  nuc_fwrite(storage, 1, size, f);
  nuc_fclose(f);
  own_file = true;
}

bool np_slow(void) { return false; }
const char *np_target(void) { return "nspire"; }
void np_begin(void) { start(); }
int np_end(void) {
  if (started) timer_stop(), lcd_init(SCR_TYPE_INVALID), started = false;
  refresh_osscr();
  return 0;
}
