#!/usr/bin/env python3
"""Five Nights at Freddy's on the TI-84 Plus CE: its pictures, fonts and tables -> data/*.bin, src/assets.h and
tests/pics.crc (tools/cepack.py).

    python3 -I tools/pack.py REF      (REF: the folder ../tools/fetch.py filled; Python + Pillow + NumPy + SciPy,
                                       and the CE toolchain's convbin for zx0)

The pictures are the NumWorks version's (../tools/pack.py makes them from the game's): the office and the cameras
at 534 x 120 (the calculator doubles each row), the jumpscares and the monitor at 160 x 120 (doubled both ways),
the newspaper, the paychecks and the like at 320 x 240."""
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
CE = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(CE), "tools"))
import cepack as C  # noqa: E402
import pack as NW  # noqa: E402  (the NumWorks version's pictures)

P = C.Pack("FNAFD")


def office_pictures(N):
    g = P.group("office")
    office = C.halfv(N["OFFICE"], True)
    P.pic("OFFICE", office, g)
    P.define("OFFICE_W", office.shape[1] + office.shape[1] % 2)
    # the lights: each side's lit doorway, a patch over the office (with Bonnie or Chica: over it again)
    for side, lit, who in (("L", "OFFICE_LL", "OFFICE_LLB"), ("R", "OFFICE_RL", "OFFICE_RLC")):
        a, b = C.halfv(N[lit], True), C.halfv(N[who], True)
        x0, y0, x1, y1 = [min(v1, v2) if k < 2 else max(v1, v2)
                          for k, (v1, v2) in enumerate(zip(C.box(a, office, 24), C.box(b, office, 24)))]
        p = P.pic(f"LIGHT_{side}", C.pad8(a)[y0:y1, x0:x1], g)
        P.pic(f"LIGHT_{side}_WHO", C.pad8(b)[y0:y1, x0:x1], g, base=p, keep=C.TIGHT)
        for k, v in zip("XYWH", (x0, y0, x1 - x0, y1 - y0)):
            P.define(f"LIGHT_{side}_{k}", v)
    # the doors, the buttons lit, the fan and Golden Freddy, as the NumWorks version places them (rows halved)
    for n in ["DOOR_L", "DOOR_R"] + [f"PANEL_{s}{k}" for s in "LR" for k in (1, 2, 3)] + \
             ["FAN0", "FAN1", "FAN2", "OFFICE_GOLD"]:
        P.pic(n, C.halfv(N[n], True), g)
    # out of power: the dark office, and Freddy's face in the doorway (a patch)
    g = P.group("dark")
    dark = C.halfv(N["DARK"], True)
    P.pic("DARK", dark, g)
    df = C.halfv(N["DARK_FREDDY"], True)
    x0, y0, x1, y1 = C.box(df, dark, 20)
    P.pic("DARK_FREDDY", C.pad8(df)[y0:y1, x0:x1], g)
    for k, v in zip("XYWH", (x0, y0, x1 - x0, y1 - y0)):
        P.define(f"DARK_FREDDY_{k}", v)


def camera_pictures(N):
    for cam, states in NW.CAMS.items():
        g = P.group("cam" + cam.lower())
        first = None
        for who, _ in states:
            name = f"CAM{cam}_{who.upper() or 'EMPTY'}"
            p = P.pic(name, C.halfv(N[name]), g, base=first, keep=C.TIGHT if first else None)
            first = first or p
        if cam == "2A":
            # Foxy running down the hall: the whole hall, each frame over the one before
            im = Image.open(NW.web("Foxy-Hallway.webp"))
            prev = first
            for i in range(min(im.n_frames, 18)):
                im.seek(i)
                a = C.halfv(np.asarray(im.convert("RGB").resize((NW.OW, NW.OH), Image.LANCZOS)).astype(np.float64))
                prev = P.pic(f"FOXYRUN{i}", a, g, base=prev, keep=C.ANIM)
            P.define("FOXYRUN_N", 18)
        if cam == "2B":
            P.pic("CAM2B_GOLD", C.halfv(N["CAM2B_GOLD"]), g, base=first, keep=C.TIGHT)


def anim(name, frames, grp, chain=True):
    """160 x 120 frames, each coded over the one before (chain), or on its own (played both ways)"""
    prev = None
    for i, a in enumerate(frames):
        p = P.pic(f"{name}{i}", C.resize(a, 160, 120), grp, base=prev, keep=C.ANIM if prev else None)
        prev = p if chain else None
    P.define(f"{name}_N", len(frames))


def scare_pictures(N):
    for name, n in (("BONNIE", 10), ("CHICA", 15), ("FREDDY", 20), ("FOXY", 19), ("POWEROUT", 21)):
        anim(name, [N[f"{name}{i}"] for i in range(n)], P.group(name.lower()))
    anim("FLIP", [N[f"FLIP{i}"] for i in range(11)], P.group("flip"), chain=False)
    g = P.group("halluc")   # the hallucinations (a frame now and then)
    P.pic("HALLUC_GOLDEN", C.resize(N["GOLDEN"], 160, 120), g)
    P.pic("HALLUC_ENDO", C.resize(N["TITLE3"], 160, 120), g)


def menu_pictures(N):
    g = P.group("title")
    t0 = C.halfv(N["TITLE0"])
    P.pic("TITLE0", t0, g)
    for k in (1, 2, 3):   # Freddy's twitches: patches over the face
        a = C.halfv(N[f"TITLE{k}"])
        x0, y0, x1, y1 = C.box(a, t0, 12)
        P.pic(f"TITLE{k}", C.pad8(a)[y0:y1, x0:x1], g)
        for c, v in zip("XYWH", (x0, y0, x1 - x0, y1 - y0)):
            P.define(f"TITLE{k}_{c}", v)
    for n in ("GAMEOVER", "GOLDEN"):   # (in RAM, their static drawn over them each frame)
        P.pic(n, C.halfv(N[n]), P.group(n.lower()))
    for n in ("NEWSPAPER", "PAYCHECK", "OVERTIME", "PINKSLIP"):
        P.pic(n, N[n], P.group(n.lower()))
    g = P.group("faces")
    for n in ("FREDDY", "BONNIE", "CHICA", "FOXY"):
        P.pic(f"FACE_{n}", N[f"FACE_{n}"], g)


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    NW.REF = sys.argv[1]
    NW.FONT_DIRS[:0] = C.FONT_DIRS
    NW.office_pictures()
    NW.camera_pictures()
    NW.scare_pictures()
    NW.menu_pictures()
    N = {p.name: p.a[:p.h, :p.w].astype(np.float64) for p in NW.PICS}
    office_pictures(N)
    camera_pictures(N)
    scare_pictures(N)
    menu_pictures(N)
    for n, f in zip(["HUD", "BIG", "TEXT", "CLOCK", "PIXEL", "HUGE"], [
            NW.ttf_font(NW.find_font("DejaVuSansMono-Bold.ttf"), 11),
            NW.ttf_font(NW.find_font("DejaVuSansMono.ttf"), 17),
            NW.ttf_font(NW.find_font("DejaVuSans.ttf"), 11),
            NW.fnt_font(NW.hx("numbers/NUMBER.fnt"), NW.hx("numbers/NUMBER.png"), 0.36),
            NW.fnt_font(NW.hx("numbers/PIXEL_NUMBER.fnt"), NW.hx("numbers/PIXEL_NUMBER.png"), 0.36),
            NW.ttf_font(NW.find_font("DejaVuSansMono.ttf"), 24)]):
        P.font(n, *f)
    P.blob("IMG_TITLE", C.img_blob(C.rgba_alpha(NW.hx("mainmenu/texts/FIVE_GAME_TITLE.png"), scale=0.5)))
    P.blob("IMG_STAR", C.img_blob(C.rgba_alpha(NW.hx("mainmenu/STAR.png"), size=(22, 21))))
    P.blob("CAMMAP", cam_map(NW.camera_map()))
    for n, v in NW.DEFINES:
        if n.startswith(("DOOR_", "PANEL_", "FAN_", "GOLD_", "MAP_")):
            P.define(n, v)
    P.write(CE)


def cam_map(raw):
    """the camera map: its walls as runs (size, count (u16), then y, x, length as bytes) and the 11 buttons'
    centres (x, y bytes)"""
    import struct
    size = struct.unpack_from("<H", raw, 0)[0]
    stride = (size + 7) // 8
    runs = []
    for r in range(size):
        row = raw[4 + r * stride:4 + (r + 1) * stride]
        c = 0
        while c < size:
            if row[c >> 3] >> (c & 7) & 1:
                s = c
                while c < size and row[c >> 3] >> (c & 7) & 1:
                    c += 1
                runs.append((r, s, c - s))
            else:
                c += 1
    centres = raw[4 + size * stride:4 + size * stride + 44]
    out = struct.pack("<BH", size, len(runs)) + b"".join(struct.pack("<BBB", *r) for r in runs)
    return out + bytes(v for i in range(11) for v in struct.unpack_from("<HH", centres, 4 * i))


if __name__ == "__main__":
    main()
