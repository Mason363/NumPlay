#!/usr/bin/env python3
"""Draws NumPico's icon (src/icon.png, 55 x 56): a cart on PICO-8's dark blue, its label a little scene in PICO-8's
colours. Drawn 4 times bigger, then made smaller."""
import os
from PIL import Image, ImageDraw

S = 4
W, H = 55 * S, 56 * S
NAVY, GREY, DARK, WHITE = (29, 43, 83), (194, 195, 199), (95, 87, 79), (255, 241, 232)
SKY, GRASS, SUN, PINK, RED = (41, 173, 255), (0, 228, 54), (255, 236, 39), (255, 119, 168), (255, 0, 77)


def main():
    im = Image.new("RGB", (W, H), (255, 255, 255))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((0, 0, W - 1, H - 1), radius=12 * S, fill=NAVY)
    # the cart: its body, the notch at the top right, the ridges at the bottom
    x0, y0, x1, y1 = 13 * S, 7 * S, 42 * S, 50 * S
    d.rounded_rectangle((x0, y0, x1, y1), radius=3 * S, fill=GREY)
    d.polygon([(x1 - 7 * S, y0), (x1 + 1, y0), (x1 + 1, y0 + 7 * S)], fill=NAVY)
    for k in range(3):
        y = y1 - (4 + 3 * k) * S
        d.rectangle((x0 + 5 * S, y, x1 - 5 * S, y + S - 1), fill=DARK)
    # the label: 10 x 10 pixels of 2: the sky, the sun, a hill, a pink figure
    lx, ly, px = x0 + 4 * S, y0 + 5 * S, 2 * S
    d.rectangle((lx - S, ly - S, lx + 10 * px + S - 1, ly + 10 * px + S - 1), fill=WHITE)
    scene = ["SSSSSSSSSS",
             "SSSSSSSYYS",
             "SSSSSSSYYS",
             "SSSSSSSSSS",
             "SSSPPSSSSS",
             "SSSPPSSSSS",
             "SSRRRRSSSS",
             "SSSPSPSSSG",
             "GGGGGGGGGG",
             "GGGGGGGGGG"]
    colors = {"S": SKY, "Y": SUN, "P": PINK, "R": RED, "G": GRASS}
    for r, row in enumerate(scene):
        for c, ch in enumerate(row):
            d.rectangle((lx + c * px, ly + r * px, lx + (c + 1) * px - 1, ly + (r + 1) * px - 1), fill=colors[ch])
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "icon.png")
    im.resize((55, 56), Image.LANCZOS).save(out)
    print("ICON   ", out)


if __name__ == "__main__":
    main()
