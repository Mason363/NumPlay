#!/usr/bin/env python3
"""The pictures of the README and How to play, from the host build (make host; python3 tools/media.py):

  docs/start.png, docs/shot.png, docs/hint.png   how to play, a 10 x 10 level, a hint (shot.png is NumPlay's card too)
  ../../docs/media/meowdoku.gif                  level 1 won with the keys, then level 12, for How to play
  ../../docs/media/gallery/meowdoku.gif          level 12: a wrong cat, X's, a hint, a cat found, for the README's gallery
  ../../docs/media/thumb_meowdoku.png            the README's table

Python 3 with Pillow.
"""
import os
import subprocess
import tempfile

from PIL import Image

HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
PLAY = os.path.join(HERE, "build", "play")
MEDIA = os.path.join(HERE, "..", "..", "docs", "media")


def gif(path, frames):
    """Each distinct screen, held as long as it was shown (40 ms steps), at most 2 s."""
    out, times = [], []
    for f in frames:
        if out and f.tobytes() == out[-1].tobytes():
            times[-1] += 40
        else:
            out.append(f)
            times.append(40)
    times = [min(t, 2000) for t in times]
    sheet = Image.new("RGB", (320, 240 * len(out[::3])))
    for i, f in enumerate(out[::3]):
        sheet.paste(f, (0, 240 * i))
    pal = sheet.quantize(96, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    q = [f.quantize(palette=pal, dither=Image.Dither.NONE) for f in out]
    q[0].save(path, save_all=True, append_images=q[1:], duration=times, loop=0, optimize=True, disposal=1)
    print(f"{path}: {len(q)} frames, {os.path.getsize(path)} bytes")


def main():
    with tempfile.TemporaryDirectory() as d:
        out = subprocess.run([PLAY, "film", d], check=True, capture_output=True, text=True).stdout
        level12 = int(out.split("from frame ")[1].split()[0])
        for i, name in ((0, "start.png"), (2, "shot.png"), (3, "hint.png")):
            Image.open(os.path.join(d, f"shot_{i}.ppm")).convert("RGB").save(os.path.join(HERE, "docs", name), optimize=True)
        names = sorted(f for f in os.listdir(d) if f.startswith("f_"))
        frames = [Image.open(os.path.join(d, f)).convert("RGB") for f in names]
    gif(os.path.join(MEDIA, "meowdoku.gif"), frames)
    gif(os.path.join(MEDIA, "gallery", "meowdoku.gif"), frames[level12:])
    thumb = os.path.join(MEDIA, "thumb_meowdoku.png")
    Image.open(os.path.join(HERE, "docs", "shot.png")).crop((0, 0, 320, 240)).resize((160, 120), Image.LANCZOS).save(
        thumb, optimize=True)
    subprocess.run(["python3", os.path.join(HERE, "..", "..", "tools", "media", "round_corners.py"), thumb], check=True)


if __name__ == "__main__":
    main()
