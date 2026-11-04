#!/usr/bin/env python3
"""The NumWorks version's main.c for the TI-84 Plus CE: the same file with the CE's keys named where the screens
name keys (platform/ti84ce/README.md: 2nd is OK, enter is EXE, clear is Back, mode is Home, del is the NumWorks's
backspace, y= is shift, math is toolbox, vars is var), and its main() renamed for src/ce_main.c to call.
Every text below must be found, so a change to the NumWorks texts shows here.

Usage: cekeys.py ../src/main.c gen/main.c"""
import sys

SWAPS = [
    # the How to play page
    (r'\004OK\020  select a card', r'\0042nd\020  select a card'),
    (r'\004EXE\020  \002Play Hand\020      \004DEL\020  \003Discard\020',
     r'\004enter\020  \002Play Hand\020    \004del\020  \003Discard\020'),
    (r'\004shift\020  sort the hand', r'\004y=\020  sort the hand'),
    (r'\004left/right\020 to carry it, \004OK\020 to put it down', r'\004left/right\020 to carry it, \0042nd\020 to put it down'),
    (r'\004toolbox\020  Run Info      \004var\020  view deck', r'\004math\020  Run Info      \004vars\020  view deck'),
    (r'\004back\020  back / Options     \004home\020  save, quit', r'\004clear\020  back / Options     \004mode\020  save, quit'),
    (r'Hold \004OK\020 or \004EXE\020 to speed up scoring.', r'Hold \0042nd\020 or \004enter\020 to speed up scoring.'),
    # the keys under the round's buttons, the move hint
    ('keycap(px + pw / 2, 229, "exe");', 'keycap(px + pw / 2, 229, "enter");'),
    ('keycap(sx + sw / 2, 229, "shift");', 'keycap(sx + sw / 2, 229, "y=");'),
    ('const char *k = moving ? "OK" : "alpha"', 'const char *k = moving ? "2nd" : "alpha"'),
    # Run Info
    ('T("left/right: tabs     back: close")', 'T("left/right: tabs     clear: close")'),
    # the entry point
    ("int main(void) {", "int balatro_main(void) {"),
]


def main():
    src, dst = sys.argv[1], sys.argv[2]
    s = open(src, encoding="utf-8").read()
    for a, b in SWAPS:
        if s.count(a) != 1:
            raise SystemExit(f"cekeys: {a!r} found {s.count(a)} times in {src}")
        s = s.replace(a, b)
    head = "/* Made by tools/cekeys.py from src/main.c: do not edit. */\n"
    s = head + s.replace('#include "', '#include "../../src/', s.count('#include "'))
    open(dst, "w", encoding="utf-8").write(s)


if __name__ == "__main__":
    main()
