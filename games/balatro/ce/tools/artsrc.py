"""The NumWorks version's art, read back from games/balatro/src/assets.c: every sprite decoded (tools/rc.py) to
RGB565 colours (None where transparent), and the fonts as they are. The TI-84 Plus CE's art is made from these."""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(GAME, "tools"))
import rc  # noqa: E402


def _array(src, name):
    m = re.search(r"const \w+ " + name + r"\[[^\]]*\] = \{(.*?)\};", src, re.S)
    if not m:
        raise SystemExit(f"artsrc: no {name} in assets.c")
    return m.group(1)


def _ints(body):
    return [int(v, 0) for v in re.findall(r"0x[0-9A-Fa-f]+|\d+", body)]


class Art:
    def __init__(self, path=os.path.join(GAME, "src", "assets.c")):
        src = open(path).read()
        hdr = open(os.path.join(GAME, "src", "assets.h")).read()
        self.defines = {k: int(v) for k, v in re.findall(r"#define (\w+) (-?\d+)\n", hdr)}
        self.cw, self.ch = self.defines["ART_CW"], self.defines["ART_CH"]
        self.table = [tuple(map(int, t)) for t in re.findall(r"\{(\d+), (\d+), (\d+), (\d+)\}", _array(src, "art_sprites"))]
        self.blob = bytes(_ints(_array(src, "art_blob")))
        pal = _ints(_array(src, "art_pal"))
        offs = _ints(_array(src, "art_pal_off"))
        tmpl = [int(v) for v in re.findall(r"-?\d+", _array(src, "art_tmpl_of_group"))]
        self.pals = [pal[o:(offs[i + 1] if i + 1 < len(offs) else len(pal))] for i, o in enumerate(offs)]
        self.tmpl_of_group = tmpl
        self.fonts = {}
        for name in ("font_small", "font_med", "font_large", "font_large_digits"):
            glyphs = [tuple(map(int, g)) for g in re.findall(r"\{(\d+),(\d+),(\d+)\}", _array(src, name))]
            self.fonts[name] = (glyphs, bytes(_ints(_array(src, name + "_bits"))))
        self._idx = {}

    def indices(self, i):
        """sprite i as rows of its group's palette indices (0 transparent)"""
        if i in self._idx:
            return self._idx[i]
        off, g, w, h = self.table[i]
        t = self.tmpl_of_group[g]
        tm = None
        if t >= 0 and t != i and w == self.cw:
            tm = self.indices(t)
        end = self.table[i + 1][0] if i + 1 < len(self.table) else len(self.blob)
        pix = rc.decode_sprite(self.blob[off:end + 8], w, h, tm)
        self._idx[i] = pix
        return pix

    def rgb565(self, i):
        """sprite i as rows of RGB565 values, None where transparent"""
        g = self.table[i][1]
        pal = self.pals[g]
        return [[pal[v] if v else None for v in row] for row in self.indices(i)]

    def size(self, i):
        return self.table[i][2], self.table[i][3]


def to888(c):
    r, g, b = c >> 11, (c >> 5) & 63, c & 31
    return (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2)


if __name__ == "__main__":
    import time
    t0 = time.time()
    a = Art()
    n = len(a.table)
    cols = []
    for i in range(n):
        px = a.rgb565(i)
        cols.append(len(set(v for r in px for v in r if v is not None)))
    print(f"{n} sprites decoded in {time.time() - t0:.1f} s; colours per sprite: max {max(cols)},",
          f"over 16: {sum(c > 16 for c in cols)}, over 24: {sum(c > 24 for c in cols)}")
