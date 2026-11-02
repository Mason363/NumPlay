#!/usr/bin/env python3
"""Universal Paperclips in French: the sources with every text in lang/fr.txt translated.

lang/fr.txt has a text a line: the English as the source has it (string literals next to each other joined, C
escapes), a tab, the French (UTF-8 and the same escapes: \\xA0 is a space no line is broken at). The spaces the
English starts and ends with, the French gets too (so none have to be kept at the ends of the file's lines). The
French goes in as Windows-1252 bytes, the letters of the game's French font (src/font_data_fr.c).

  french.py OUT       the sources in OUT, translated, with the French font as font_data.c
  french.py missing   the texts with letters that have no translation (some, as save names, have none on purpose)
  french.py letters   the letters past ASCII the French needs, for tools/fontgen.py --extra
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "src")
TABLE = os.path.join(HERE, "..", "lang", "fr.txt")
FILES = ("main.c", "sim.c", "projects.c", "num.c", "ui.c", "save.c")
# a run of string literals with only spaces between them, outside comments and #include lines
TOKEN = re.compile(r'//[^\n]*|/\*.*?\*/|#include[^\n]*|\'(?:[^\'\\\n]|\\.)*\'|((?:"(?:[^"\\\n]|\\.)*"\s*)+)', re.S)
LIT = re.compile(r'"((?:[^"\\\n]|\\.)*)"')


def unescape(s):
    """C escapes (and UTF-8 letters) -> the text"""
    out, i = [], 0
    while i < len(s):
        c = s[i]
        if c != "\\":
            out.append(c)
            i += 1
            continue
        n = s[i + 1]
        if n == "x":
            j = i + 2
            while j < len(s) and j < i + 4 and s[j] in "0123456789abcdefABCDEF":
                j += 1
            out.append(chr(int(s[i + 2:j], 16)))
            i = j
        elif n in "01234567":
            j = i + 1
            while j < len(s) and j < i + 4 and s[j] in "01234567":
                j += 1
            out.append(chr(int(s[i + 1:j], 8)))
            i = j
        else:
            out.append({"n": "\n", "t": "\t", "\\": "\\", '"': '"', "'": "'", "0": "\0"}[n])
            i += 2
    return "".join(out)


def latin(s):
    """the source's bytes (Latin-1) as the letters they are in Windows-1252"""
    return s.encode("latin-1").decode("cp1252", errors="ignore") if all(ord(c) < 256 for c in s) else s


def literal(text):
    """the text as one C string literal, past ASCII in octal (\\351: no letter after it can join the escape)"""
    out = []
    for b in text.encode("cp1252"):
        c = chr(b)
        if c == '"' or c == "\\":
            out.append("\\" + c)
        elif c == "\n":
            out.append("\\n")
        elif 32 <= b < 127:
            out.append(c)
        else:
            out.append("\\%03o" % b)
    return '"' + "".join(out) + '"'


def load():
    table = {}
    for n, line in enumerate(open(TABLE, encoding="utf-8"), 1):
        line = line.rstrip("\n")
        if not line or line.startswith("#"):
            continue
        if "\t" not in line:
            raise SystemExit(f"{TABLE}:{n}: no tab between the English and the French")
        en, fr = line.split("\t", 1)
        key = latin(unescape(en))
        if key in table:
            raise SystemExit(f"{TABLE}:{n}: {en!r} twice")
        lead, trail = len(key) - len(key.lstrip(" ")), len(key) - len(key.rstrip(" "))
        table[key] = key[:lead] + unescape(fr).strip(" ") + key[len(key) - trail:]
    return table


def texts(code):
    """(match, the text) for each run of literals in a source"""
    for m in TOKEN.finditer(code):
        if m.group(1):
            yield m, latin("".join(unescape(x) for x in LIT.findall(m.group(1))))


def translate(out):
    table, used, files = load(), set(), {}
    for f in os.listdir(SRC):
        path = os.path.join(SRC, f)
        if f == "font_data.c" or not os.path.isfile(path):
            continue
        data = open(path, encoding="latin-1").read()
        if f in FILES:
            def put(m):
                run = m.group(1)
                if not run:
                    return m.group(0)
                text = latin("".join(unescape(x) for x in LIT.findall(run)))
                if text not in table:
                    return m.group(0)
                used.add(text)
                return literal(table[text]) + run[len(run.rstrip()):]
            data = TOKEN.sub(put, data)
        files["font_data.c" if f == "font_data_fr.c" else f] = data
    unused = [k for k in table if k not in used]
    if unused:
        raise SystemExit("french.py: not in the sources: " + "; ".join(repr(k) for k in unused))
    os.makedirs(out, exist_ok=True)
    for f, data in files.items():
        open(os.path.join(out, f), "w", encoding="latin-1").write(data)


def missing():
    table, seen = load(), set()
    for f in FILES:
        for m, text in texts(open(os.path.join(SRC, f), encoding="latin-1").read()):
            if re.search(r"[A-Za-z]{2}", text) and text not in table and text not in seen:
                seen.add(text)
                print(f"{f}\t{text!r}")


def letters():
    return sorted({ord(c) for t in load().values() for c in t if ord(c) > 127 and c != "\xa0"} | {0xA9, 0xF3, 0xF8})


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    if sys.argv[1] == "missing":
        missing()
    elif sys.argv[1] == "letters":
        print(",".join(str(c) for c in letters()))
    else:
        translate(sys.argv[1])
