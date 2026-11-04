#!/usr/bin/env python3
"""The NumWorks version's files the TI-84 Plus CE changes a little (gen/, from ../src):
main.c with the CE's keys named where the screens name keys (platform/ti84ce/README.md: 2nd is OK, enter is EXE,
clear is Back, mode is Home, del is the NumWorks's backspace, y= is shift, math is toolbox, vars is var), the keys
looked through only when one is newly down, and its main() renamed for src/ce_main.c to call; ui.c with its easing
skipped where there is nothing left to ease (a frame's busiest float work on the eZ80, which has no floats); util.c
with the numbers it wrote last kept (the same few every frame, and 64-bit divisions are slow calls on the eZ80).
Every text below must be found, so a change to the NumWorks files shows here.

Usage: cesrc.py ../src/main.c gen/main.c"""
import os
import sys

SWAPS = {
    "main.c": [
        # the How to play page
        (r'\004OK\020  select a card', r'\0042nd\020  select a card'),
        (r'\004EXE\020  \002Play Hand\020      \004DEL\020  \003Discard\020',
         r'\004enter\020  \002Play Hand\020    \004del\020  \003Discard\020'),
        (r'\004shift\020  sort the hand', r'\004y=\020  sort the hand'),
        (r'\004left/right\020 to carry it, \004OK\020 to put it down',
         r'\004left/right\020 to carry it, \0042nd\020 to put it down'),
        (r'\004toolbox\020  Run Info      \004var\020  view deck', r'\004math\020  Run Info      \004vars\020  view deck'),
        (r'\004back\020  back / Options     \004home\020  save, quit',
         r'\004clear\020  back / Options     \004mode\020  save, quit'),
        (r'Hold \004OK\020 or \004EXE\020 to speed up scoring.', r'Hold \0042nd\020 or \004enter\020 to speed up scoring.'),
        # the keys under the round's buttons, the move hint
        ('keycap(px + pw / 2, 229, "exe");', 'keycap(px + pw / 2, 229, "enter");'),
        ('keycap(sx + sw / 2, 229, "shift");', 'keycap(sx + sw / 2, 229, "y=");'),
        ('const char *k = moving ? "OK" : "alpha"', 'const char *k = moving ? "2nd" : "alpha"'),
        # Run Info
        ('T("left/right: tabs     back: close")', 'T("left/right: tabs     clear: close")'),
        # the keys newly down: none most frames (the eZ80 shifts 64 bits by calls)
        ("  for (int k = 0; k < 53; k++)\n    if (((keys >> k) & 1) && !((prev_keys >> k) & 1)) {",
         "  if (keys & ~prev_keys)\n  for (int k = 0; k < 53; k++)\n    if (((keys >> k) & 1) && !((prev_keys >> k) & 1)) {"),
        # the entry point
        ("int main(void) {", "int balatro_main(void) {"),
    ],
    "ui.c": [
        ("static float ease(float cur, float target, int dt) {\n",
         "static float ease(float cur, float target, int dt) {\n  if (cur == target) return cur;\n"),
    ],
    "util.c": [
        ("char *fmt_commas(char *o, double v) {", "static char *fmt_commas_raw(char *o, double v) {"),
    ],
}

TAILS = {
    # (for tools/host.c's player: which screen shows)
    "main.c": "\nint ce_main_screen(void) { return screen == SCR_GAME ? 1 + overlay : 0; }\n",
    "util.c": '''
/* the last numbers written, kept */
#include <string.h>
char *fmt_commas(char *o, double v) {
  static struct {
    double v;
    uint8_t n;
    char s[27];
  } kept[6];
  static uint8_t next;
  for (int i = 0; i < 6; i++)
    if (kept[i].n && !memcmp(&kept[i].v, &v, sizeof v)) {
      memcpy(o, kept[i].s, kept[i].n + 1u);
      return o + kept[i].n;
    }
  char *e = fmt_commas_raw(o, v);
  if (e - o < 27) {
    kept[next].v = v, kept[next].n = (uint8_t)(e - o);
    memcpy(kept[next].s, o, kept[next].n + 1u);
    next = next == 5 ? 0 : next + 1;
  }
  return e;
}
''',
}


def main():
    src, dst = sys.argv[1], sys.argv[2]
    name = os.path.basename(src)
    s = open(src, encoding="utf-8").read()
    for a, b in SWAPS[name]:
        if s.count(a) != 1:
            raise SystemExit(f"cesrc: {a!r} found {s.count(a)} times in {src}")
        s = s.replace(a, b)
    head = f"/* Made by tools/cesrc.py from src/{name}: do not edit. */\n"
    s = head + s.replace('#include "', '#include "../../src/', s.count('#include "'))
    s += TAILS.get(name, "")
    open(dst, "w", encoding="utf-8").write(s)


if __name__ == "__main__":
    main()
