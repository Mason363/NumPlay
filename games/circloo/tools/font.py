"""circloO's fonts (Open Sans and Open Sans Semibold, Apache-2.0) as 4-bit glyphs: src/font.h.

    python3 -I tools/font.py <folder with OpenSans-Regular.ttf and OpenSans-Semibold.ttf> src/font.h
"""
import sys
from PIL import Image, ImageDraw, ImageFont

# (name, file, pixel size): the game's fnt_small/fnt_medium, fnt_quitebig_semibold and fnt_big_semibold at the
# calculator's scale, kept readable
FONTS = [("small", "OpenSans-Regular.ttf", 12), ("medium", "OpenSans-Semibold.ttf", 16),
         ("big", "OpenSans-Semibold.ttf", 34)]
FIRST, LAST = 32, 126


def main(folder, out):
    blobs = []
    lines = ["/* Open Sans (c) 2011 Google, Apache License 2.0: made by tools/font.py, do not edit. */",
             "typedef struct { uint32_t off; uint8_t w, h; int8_t x, y; uint8_t adv; } Glyph;",
             "typedef struct { const Glyph *g; const uint8_t *bits; uint8_t height, ascent; } Font;"]
    for name, file, size in FONTS:
        f = ImageFont.truetype(folder + "/" + file, size)
        ascent, descent = f.getmetrics()
        glyphs = []
        data = bytearray()
        for ch in range(FIRST, LAST + 1):
            c = chr(ch)
            box = f.getbbox(c)
            adv = round(f.getlength(c))
            if box[2] <= box[0] or box[3] <= box[1]:
                glyphs.append((0, 0, 0, 0, 0, adv))
                continue
            w, h = box[2] - box[0], box[3] - box[1]
            img = Image.new("L", (w, h), 0)
            ImageDraw.Draw(img).text((-box[0], -box[1]), c, font=f, fill=255)
            px = list(img.getdata())
            off = len(data)
            for i in range(0, len(px), 2):
                a = (px[i] + 8) // 17
                b = (px[i + 1] + 8) // 17 if i + 1 < len(px) else 0
                data.append(min(a, 15) | min(b, 15) << 4)
            glyphs.append((off, w, h, box[0], box[1], adv))
        lines.append("static const uint8_t font_%s_bits[%d] = {" % (name, len(data)))
        for i in range(0, len(data), 32):
            lines.append("  " + ",".join(str(x) for x in data[i:i + 32]) + ",")
        lines.append("};")
        lines.append("static const Glyph font_%s_glyphs[%d] = {" % (name, len(glyphs)))
        for g in glyphs:
            lines.append("  {%d,%d,%d,%d,%d,%d}," % g)
        lines.append("};")
        lines.append("static const Font font_%s = {font_%s_glyphs, font_%s_bits, %d, %d};" %
                     (name, name, name, ascent + descent, ascent))
        blobs.append((name, len(data)))
    open(out, "w").write("\n".join(lines) + "\n")
    print(blobs)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
