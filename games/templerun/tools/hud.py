#!/usr/bin/env python3
"""Makes src/hud.c and src/hud.h: Temple Run's own interface pictures and its golden lettering, for the run's
screen (the totem with its coin meter and emblems, the vines, the score and coin plates, the idol, the bonus
pictures, the distance board and the messages), from the game's Android package (1.0.8, by Imangi Studios).

    python3 -I tools/hud.py DATA [SRC]     (DATA: the package's assets/bin/Data, unzipped; UnityPy, NumPy, Pillow)

The pictures are the game's InterfaceTexture2x, cut where its sprite atlas (NGUI's UIAtlas) says, and the
lettering its Cheboygan in gold (FontAtlas2x, the glyphs its UIFont lists), each made smaller for the screen
(the game's are for a 640 x 960 one). All of them share one palette of 255 colours, each with its own
see-through amount (index 0: nothing)."""
import os
import struct
import sys

import numpy as np
import UnityPy
from PIL import Image

# picture: (the atlas sprite, scale from the game's)
PICS = [
    ("TOTEM", "totem.png", 0.42), ("TOTEM_BAR", "totemProgress.png", 0.42), ("TOTEM_CAP", "totemProgressCap.png", 0.42),
    ("EMBLEM1", "totemEmblem1.png", 0.42), ("EMBLEM2", "totemEmblem2.png", 0.42), ("EMBLEM3", "totemEmblem3.png", 0.42),
    ("EMBLEM4", "totemEmblem4.png", 0.42),
    ("JUNGLE_A", "jungleA.png", 0.42), ("JUNGLE_B", "jungleB.png", 0.42), ("JUNGLE_C", "jungleC.png", 0.42),
    ("SCOREBOARD", "scoreboard.png", 0.42), ("SCORE_PLATE", "scoreboardPanelScore.png", 0.42),
    ("COINS_PLATE", "scoreboardPanelCoins.png", 0.42), ("WINGS_PLATE", "scoreboardPanelAngelWings.png", 0.42),
    ("COIN", "coin.png", 0.26), ("COIN_RED", "coinRed.png", 0.26), ("COIN_BLUE", "coinBlue.png", 0.26),
    ("IDOL", "idol.png", 0.34), ("PAUSE", "pause.png", 0.34),
    ("BONUS100", "bonus100.png", 0.42), ("BONUS250", "bonus250.png", 0.42), ("BONUS500", "bonus500.png", 0.42),
    ("BONUS1000", "bonus1000.png", 0.42),
    ("BOARD", "messageBoard.png", 0.42),
    ("HIGH_SCORE", "messageHighScore.png", 0.42), ("NICE_JOB", "messageNiceJob.png", 0.42),
    ("TRY_AGAIN", "messageTryAgain.png", 0.42),
    ("ARROW_LEFT", "arrowLeft.png", 0.3), ("ARROW_RIGHT", "arrowRight.png", 0.3), ("ARROW_UP", "arrowUp.png", 0.3),
    ("ARROW_DOWN", "arrowDown.png", 0.3),
    ("HEADSTART", "headstartButton.png", 0.42), ("HEADSTART_MEGA", "headstartMegaButton.png", 0.42),
]
# lettering: (name, characters, scale)
FONTS = [("gold", "".join(chr(c) for c in range(32, 127)), 0.30), ("gold_small", " 0123456789,mx", 0.22)]


def scripts_of(env):
    out = {}
    for o in env.objects:
        if o.type.name == "MonoScript":
            try:
                out[o.path_id] = o.read().m_ClassName
            except Exception:
                pass
    return out


def rd_str(raw, off):
    n = struct.unpack_from("<I", raw, off)[0]
    return raw[off + 4:off + 4 + n].decode("utf-8", "replace"), off + 4 + ((n + 3) & ~3)


def atlas_sprites(env, scripts, texture):
    """the UIAtlas whose picture is texture: {sprite name: (x, y, w, h)}"""
    mats = {}
    for o in env.objects:
        if o.type.name == "Material":
            try:
                m = o.read()
                for _, v in m.m_SavedProperties.m_TexEnvs:
                    if v.m_Texture.m_PathID:
                        mats[o.path_id] = v.m_Texture.read().m_Name
            except Exception:
                pass
    for o in env.objects:
        if o.type.name != "MonoBehaviour":
            continue
        raw = o.get_raw_data()
        if scripts.get(struct.unpack_from("<iiIii", raw, 0)[4]) != "UIAtlas":
            continue
        _, off = rd_str(raw, 20)
        mat = struct.unpack_from("<ii", raw, off)[1]
        off += 8
        if mats.get(mat) != texture:
            continue
        n = struct.unpack_from("<I", raw, off)[0]
        off += 4
        out = {}
        for _ in range(n):
            name, off = rd_str(raw, off)
            x, y, w, h = struct.unpack_from("<4f", raw, off)
            off += 48
            out[name] = (int(x), int(y), int(w), int(h))
        return out
    raise SystemExit(f"no atlas for {texture}")


def gold_font(env, scripts):
    """Cheboygan in gold: its place in FontAtlas2x and {char: (x, y, w, h, ox, oy, advance)} (the UIFont there)"""
    for o in env.objects:
        if o.type.name != "MonoBehaviour":
            continue
        raw = o.get_raw_data()
        if scripts.get(struct.unpack_from("<iiIii", raw, 0)[4]) != "UIFont":
            continue
        _, off = rd_str(raw, 20)
        off += 8
        uv = struct.unpack_from("<4f", raw, off)
        off += 16
        if uv[0] < 0.25:   # (the white ones start at the left; the gold one half way across)
            continue
        n = struct.unpack_from("<I", raw, off)[0]
        off += 4
        glyphs = {}
        for c in range(n):
            v = struct.unpack_from("<9i", raw, off + 36 * c)
            if v[3] or v[7]:
                glyphs[chr(c)] = v[1:8]
        return (round(uv[0] * 1024), round((1 - uv[1] - uv[3]) * 1024)), glyphs
    raise SystemExit("no golden font")


def texture(env, name):
    for o in env.objects:
        if o.type.name == "Texture2D":
            t = o.read()
            if t.m_Name == name:
                return t.image.convert("RGBA")
    raise SystemExit(f"no texture {name}")


def smaller(img, s):
    """a picture made s times as big, its colours weighed by how much they show (no dark fringes)"""
    w, h = max(1, round(img.width * s)), max(1, round(img.height * s))
    a = np.asarray(img, np.float64) / 255
    pre = np.dstack([a[:, :, :3] * a[:, :, 3:], a[:, :, 3:]])
    small = np.dstack([np.asarray(Image.fromarray(pre[:, :, k].astype(np.float32), "F").resize((w, h), Image.LANCZOS), np.float64)
                       for k in range(4)])
    al = np.clip(small[:, :, 3], 0, 1)
    rgb = np.clip(small[:, :, :3] / np.maximum(al, 1e-6)[:, :, None], 0, 1)
    return np.dstack([rgb, al])


def palette(pictures, k=255, rounds=14, seed=1):
    """255 colours with their see-through amounts for every pixel that shows (k-means on RGB and alpha)"""
    px = np.concatenate([p.reshape(-1, 4) for p in pictures])
    px = px[px[:, 3] > 0.04]
    rng = np.random.default_rng(seed)
    sample = px[rng.choice(len(px), min(len(px), 80000), replace=False)]
    w = np.array([1, 1, 1, 1.6])
    cent = sample[rng.choice(len(sample), k, replace=False)]
    for _ in range(rounds):
        idx = nearest(sample, cent, w)
        for j in range(k):
            m = sample[idx == j]
            if len(m):
                cent[j] = m.mean(0)
            else:
                cent[j] = sample[rng.integers(len(sample))]
    return cent, w


def nearest(px, cent, w):
    out = np.empty(len(px), np.int64)
    for i in range(0, len(px), 20000):
        d = (((px[i:i + 20000, None, :] - cent[None, :, :]) * w) ** 2).sum(2)
        out[i:i + 20000] = d.argmin(1)
    return out


def main():
    data = sys.argv[1]
    src = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src")
    env = UnityPy.load(data)
    scripts = scripts_of(env)
    sprites = atlas_sprites(env, scripts, "InterfaceTexture2x")
    ui = texture(env, "InterfaceTexture2x")
    fa = texture(env, "FontAtlas2x")
    origin, glyphs = gold_font(env, scripts)
    items = []   # (name, pixels, ox, oy, advance)
    for name, sprite, s in PICS:
        x, y, w, h = sprites[sprite]
        items.append((f"HP_{name}", smaller(ui.crop((x, y, x + w, y + h)), s), 0, 0, 0))
    fonts = []
    for fname, chars, s in FONTS:
        first = ord(min(chars))
        last = ord(max(chars))
        start = len(items)
        line = 0
        for c in range(first, last + 1):
            ch = chr(c)
            g = glyphs.get(ch)
            if ch not in chars or not g or not g[2] or not g[3]:
                adv = round((g[6] if g else 20) * s)
                items.append((None, np.zeros((1, 1, 4)), 0, 0, adv))
                continue
            gx, gy, w, h, ox, oy, adv = g
            img = fa.crop((origin[0] + gx, origin[1] + gy, origin[0] + gx + w, origin[1] + gy + h))
            p = smaller(img, s)
            items.append((None, p, round(ox * s), round(oy * s), round(adv * s)))
            line = max(line, round((oy + h) * s))
        fonts.append((fname, first, last - first + 1, start, line))
    cent, w = palette([it[1] for it in items])
    data_px, rows = bytearray(), []
    for name, p, ox, oy, adv in items:
        flat = p.reshape(-1, 4)
        idx = nearest(flat, cent, w) + 1
        idx[flat[:, 3] <= 0.04] = 0
        rows.append((name, p.shape[1], p.shape[0], ox, oy, adv, len(data_px)))
        data_px += bytes(idx.astype(np.uint8))
    pal = [((int(c[0] * 255 + .5) >> 3) << 11) | ((int(c[1] * 255 + .5) >> 2) << 5) | (int(c[2] * 255 + .5) >> 3) for c in cent]
    alpha = [min(255, int(c[3] * 255 + .5)) for c in cent]
    h = ["/* Temple Run: the game's interface pictures and its golden lettering (hud.c, made by tools/hud.py). */",
         "#ifndef TR_HUD_H", "#define TR_HUD_H", "#include <stdint.h>", "",
         "/* a picture: its pixels hud_px[off ..], w a row, colours hud_pal (see-through amounts hud_alpha, index 0 none);",
         " * a letter also has where it sits from the pen (ox, oy below the line's top) and how far the pen goes */",
         "typedef struct {", "  uint8_t w, h;", "  int8_t ox, oy;", "  uint8_t advance;", "  uint32_t off : 24;", "} HudPic;", "",
         "typedef struct {", "  uint8_t first, count, line;   /* its letters, and the height of a line */", "  uint16_t at;   /* hud_pics[at ..] */",
         "} HudFont;", "", "enum {"]
    h += [f"  {r[0]}," for r in rows if r[0]] + ["};", "",
          "extern const HudPic hud_pics[];", "extern const uint16_t hud_pal[256];", "extern const uint8_t hud_alpha[256], hud_px[];",
          "extern const HudFont " + ", ".join(f"hud_{f[0]}" for f in fonts) + ";", "#endif", ""]
    c = ["/* Made by tools/hud.py: Temple Run's own interface pictures and golden Cheboygan lettering (Imangi Studios). */",
         '#include "hud.h"', "",
         "const uint16_t hud_pal[256] = {0," + ",".join(str(v) for v in pal) + "};",
         "const uint8_t hud_alpha[256] = {0," + ",".join(str(v) for v in alpha) + "};",
         f"const uint8_t hud_px[{len(data_px)}] = {{"]
    for i in range(0, len(data_px), 40):
        c.append(",".join(str(b) for b in data_px[i:i + 40]) + ",")
    c += ["};", "const HudPic hud_pics[] = {"]
    for name, w_, h_, ox, oy, adv, off in rows:
        c.append(f"  {{{w_}, {h_}, {ox}, {oy}, {adv}, {off}}},{'  /* ' + name + ' */' if name else ''}")
    c.append("};")
    for fname, first, count, start, line in fonts:
        c.append(f"const HudFont hud_{fname} = {{{first}, {count}, {line}, {start}}};")
    c.append("")
    open(os.path.join(src, "hud.h"), "w").write("\n".join(h))
    open(os.path.join(src, "hud.c"), "w").write("\n".join(c))
    print(f"{len(rows)} pictures, {len(data_px)} pixels")


if __name__ == "__main__":
    main()
