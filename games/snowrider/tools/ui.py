#!/usr/bin/env python3
"""Snow Rider 3D's own interface for the calculator: src/ui.c and src/ui.h, from the game's Unity project.

    python3 -I tools/ui.py <the Unity project's Assets folder> src     (Pillow, NumPy)

- Its lettering: Blockletter (Assets/Font/Blockletter.ttf), at three sizes (the game's 62, 100 and 160 on its
  1920 x 1080 canvas, about a sixth here), as the launcher's fonts are kept (np_font_t: 4-bit coverage).
- Its pictures: the gift, the wooden sign and its sled icon, the shop's arrow buttons and the arrow keys of its
  first run (Assets/Texture2D), made smaller the same way, in one palette of 255 colours each with its own
  see-through amount (index 0: nothing)."""
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

UP = 4
FIRST, LAST = 32, 126
# name, cap height in pixels
FONTS = [("sr_font_small", 9), ("sr_font_mid", 14), ("sr_font_big", 23)]
# picture, file, width and height here
PICS = [("UI_GIFT", "Gift.png", (22, 22)), ("UI_SIGN", "MenuButton.png", (40, 40)), ("UI_SLED", "MenuSleds.png", (22, 22)),
        ("UI_ARROW_BUTTON", "directionalButton.png", (30, 30)), ("UI_KEYS", "arrowKeys-300x205_1.png", (64, 44))]


def glyph(f, ch):
    adv = f.getlength(ch) / UP
    bb = f.getbbox(ch, anchor="ls")
    if bb[2] <= bb[0] or bb[3] <= bb[1]:
        return None, round(adv)
    pad = 2 * UP
    x0, y0 = pad - bb[0], pad - bb[1]
    x0 += (-x0) % UP
    y0 += (-y0) % UP
    w, h = x0 + bb[2] + pad, y0 + bb[3] + pad
    w += (-w) % UP
    h += (-h) % UP
    im = Image.new("L", (w, h), 0)
    ImageDraw.Draw(im).text((x0, y0), ch, font=f, fill=255, anchor="ls")
    a = np.asarray(im).astype(np.float32).reshape(h // UP, UP, w // UP, UP).mean(axis=(1, 3)) / 255
    q = np.clip(np.round(a * 15), 0, 15).astype(np.uint8)
    ys, xs = np.nonzero(q)
    if not len(ys):
        return None, round(adv)
    q = q[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
    return (q, xs.min() - x0 // UP, ys.min() - y0 // UP), round(adv)


def load(path, cap):
    for size in range(6, 400):
        f = ImageFont.truetype(path, size * UP)
        if -f.getbbox("H", anchor="ls")[1] >= cap * UP - UP // 2:
            return f
    raise SystemExit("size")


def smaller(img, size):
    a = np.asarray(img.convert("RGBA"), np.float64) / 255
    pre = np.dstack([a[:, :, :3] * a[:, :, 3:], a[:, :, 3:]])
    small = np.dstack([np.asarray(Image.fromarray(pre[:, :, k].astype(np.float32), "F").resize(size, Image.LANCZOS), np.float64)
                       for k in range(4)])
    al = np.clip(small[:, :, 3], 0, 1)
    return np.dstack([np.clip(small[:, :, :3] / np.maximum(al, 1e-6)[:, :, None], 0, 1), al])


def main(assets, src):
    lines = ["/* Made by tools/ui.py: Snow Rider 3D's own lettering (Blockletter) and interface pictures. */", '#include "ui.h"', ""]
    for name, cap in FONTS:
        f = load(os.path.join(assets, "Font", "Blockletter.ttf"), cap)
        data, glyphs, asc, desc = bytearray(), [], 0, 0
        for c in range(FIRST, LAST + 1):
            g, adv = glyph(f, chr(c))
            if g is None:
                glyphs.append((0, 0, 0, 0, adv, len(data)))
                continue
            q, ox, oy = g
            h, w = q.shape
            asc, desc = max(asc, -oy), max(desc, h + oy)
            flat = q.flatten().tolist() + [0]
            off = len(data)
            for i in range(0, w * h, 2):
                data.append(flat[i] | flat[i + 1] << 4)
            glyphs.append((w, h, ox, oy, adv, off))
        lines.append(f"static const uint8_t {name}_data[{len(data)}] = {{")
        lines += ["  " + ",".join(str(b) for b in data[i:i + 32]) + "," for i in range(0, len(data), 32)]
        lines.append("};")
        lines.append(f"static const np_glyph_t {name}_glyphs[{len(glyphs)}] = {{" + ", ".join("{%d,%d,%d,%d,%d,%d}" % g for g in glyphs) + "};")
        lines.append(f"const np_font_t {name} = {{{FIRST}, {LAST - FIRST + 1}, {asc}, {desc}, {name}_glyphs, {name}_data}};")
        lines.append("")
    pics = [(n, smaller(Image.open(os.path.join(assets, "Texture2D", fn)), size)) for n, fn, size in PICS]  # noqa
    px = np.concatenate([p.reshape(-1, 4) for _, p in pics])
    px = px[px[:, 3] > 0.04]
    rng = np.random.default_rng(1)
    w8 = np.array([1, 1, 1, 1.6])
    cent = px[rng.choice(len(px), min(255, len(px)), replace=False)].copy()
    for _ in range(16):
        idx = (((px[:, None, :] - cent[None]) * w8) ** 2).sum(2).argmin(1)
        for j in range(len(cent)):
            m = px[idx == j]
            if len(m):
                cent[j] = m.mean(0)
    data, rows = bytearray(), []
    for n, p in pics:
        flat = p.reshape(-1, 4)
        idx = (((flat[:, None, :] - cent[None]) * w8) ** 2).sum(2).argmin(1) + 1
        idx[flat[:, 3] <= 0.04] = 0
        rows.append((n, p.shape[1], p.shape[0], len(data)))
        data += bytes(idx.astype(np.uint8))
    pal = [((int(c[0] * 255 + .5) >> 3) << 11) | ((int(c[1] * 255 + .5) >> 2) << 5) | (int(c[2] * 255 + .5) >> 3) for c in cent]
    alpha = [min(255, int(c[3] * 255 + .5)) for c in cent]
    pal += [0] * (255 - len(pal))
    alpha += [0] * (255 - len(alpha))
    lines.append("const uint16_t ui_pal[256] = {0," + ",".join(map(str, pal)) + "};")
    lines.append("const uint8_t ui_alpha[256] = {0," + ",".join(map(str, alpha)) + "};")
    lines.append(f"const uint8_t ui_px[{len(data)}] = {{")
    lines += ["  " + ",".join(str(b) for b in data[i:i + 40]) + "," for i in range(0, len(data), 40)]
    lines.append("};")
    lines.append("const UiPic ui_pics[UI_COUNT] = {" + ", ".join("{%d, %d, %d}" % r[1:] for r in rows) + "};")
    open(os.path.join(src, "ui.c"), "w").write("\n".join(lines) + "\n")
    h = ["/* Snow Rider 3D's own lettering and interface pictures (ui.c, made by tools/ui.py). */", "#ifndef SR_UI_H", "#define SR_UI_H",
         "#include <stdint.h>", '#include "../../../launcher/src/font.h"', "",
         "extern const np_font_t " + ", ".join(n for n, _ in FONTS) + ";", "",
         "/* a picture: w x h pixels from ui_px[off], colours ui_pal and their see-through amounts ui_alpha (0: none) */",
         "typedef struct {", "  uint8_t w, h;", "  uint16_t off;", "} UiPic;", "enum {"] + [f"  {r[0]}," for r in rows] + [
         "  UI_COUNT", "};", "extern const UiPic ui_pics[UI_COUNT];", "extern const uint16_t ui_pal[256];",
         "extern const uint8_t ui_alpha[256], ui_px[];", "#endif", ""]
    open(os.path.join(src, "ui.h"), "w").write("\n".join(h))
    print(f"{len(rows)} pictures, {len(data)} pixels")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src"))
