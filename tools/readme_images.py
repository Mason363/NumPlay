#!/usr/bin/env python3
"""The README's pictures, all with the same rounded corners (a radius of 6% of their width; outside it they're
transparent, so they sit right on GitHub's light and dark pages).

- docs/media/readme/thumb_<id>.png: each game's thumbnail, made from docs/media/thumb_<id>.png (left as it is). The
  games in NumPlay.nwa by default (games.json: not "xl", "default" not false) get a blue ring.
- The README's GIFs and other pictures in docs/media (the gallery, the real-calculator clips, the launcher, NumPlay
  Studio's picture): rounded where they are. Those already rounded are left alone, so it can run again any time.

Usage: tools/readme_images.py   (from anywhere; needs Pillow, ImageMagick's magick and gifsicle)"""
import json
import os
import re
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
MEDIA = os.path.join(ROOT, "docs", "media")
OUT = os.path.join(MEDIA, "readme")
RING = (10, 132, 255, 255)  # NumPlay Studio's blue


def radius(w):
    return max(4, round(w * 0.06))


def mask(w, h):
    m = Image.new("L", (w, h), 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, w - 1, h - 1), radius(w), fill=255)
    return m


def rounded(path):
    """already rounded: its top left corner is transparent"""
    im = Image.open(path).convert("RGBA")
    return im.getpixel((0, 0))[3] == 0


def thumb(gid, ring):
    im = Image.open(os.path.join(MEDIA, f"thumb_{gid}.png")).convert("RGBA")
    w, h = im.size
    if ring:
        d = ImageDraw.Draw(im)
        t = max(3, round(w / 40))
        d.rounded_rectangle((0, 0, w - 1, h - 1), radius(w), outline=RING, width=t)
    im.putalpha(Image.composite(im.getchannel("A"), Image.new("L", im.size, 0), mask(w, h)))
    im.save(os.path.join(OUT, f"thumb_{gid}.png"), optimize=True)


def round_png(path):
    src = Image.open(path)
    im = src.convert("RGBA")
    w, h = im.size
    im.putalpha(Image.composite(im.getchannel("A"), Image.new("L", im.size, 0), mask(w, h)))
    if src.mode == "P":  # (a palette picture stays one: 256 colours, transparency included)
        im = im.quantize(256, method=Image.Quantize.FASTOCTREE)
    im.save(path, optimize=True)


def round_gif(path):
    w, h = Image.open(path).size
    with tempfile.TemporaryDirectory() as d:
        m, a = os.path.join(d, "mask.png"), os.path.join(d, "a.gif")
        mk = Image.new("RGBA", (w, h), (255, 255, 255, 0))
        mk.putalpha(mask(w, h))
        mk.save(m)
        subprocess.run(["magick", path, "-coalesce", "null:", m, "-alpha", "set", "-compose", "DstIn",
                        "-layers", "composite", "-layers", "OptimizePlus", a], check=True)
        subprocess.run(["gifsicle", "-O3", a, "-o", path], check=True)


def main():
    os.makedirs(OUT, exist_ok=True)
    games = {g["id"]: g for g in json.load(open(os.path.join(ROOT, "games", "games.json")))}
    readme = open(os.path.join(ROOT, "README.md")).read()
    for gid, g in games.items():
        if os.path.exists(os.path.join(MEDIA, f"thumb_{gid}.png")):
            thumb(gid, not g.get("xl") and g.get("default", True))
    for name in sorted(set(re.findall(r'src="docs/media/((?:gallery/)?[A-Za-z0-9_.-]+\.(?:gif|png))"', readme))):
        path = os.path.join(MEDIA, name)
        if name.startswith("thumb_") or name == "icon.png" or not os.path.exists(path) or rounded(path):
            continue
        (round_gif if name.endswith(".gif") else round_png)(path)
        print("rounded", name)


if __name__ == "__main__":
    sys.exit(main())
