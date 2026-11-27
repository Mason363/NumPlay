#!/usr/bin/env python3
"""Check TwiliPlay theme contrast after RGB565 quantization."""
T = {
    "Starlight": (0x191027, 0x33204D, 0xFFF1FA),
    "Literature Club": (0xFFF0F5, 0xE9ACCB, 0x79456D),
    "Midnight Library": (0x101725, 0x30354C, 0xEEE4FF),
}
def q(c):
    r, g, b = c >> 16 & 255, c >> 8 & 255, c & 255
    return ((r >> 3) << 3 | r >> 5, (g >> 2) << 2 | g >> 6, (b >> 3) << 3 | b >> 5)
def lum(c):
    def f(v):
        v /= 255
        return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
    r, g, b = map(f, q(c))
    return .2126*r + .7152*g + .0722*b
def contrast(a,b):
    hi,lo=sorted((lum(a),lum(b)),reverse=True)
    return (hi+.05)/(lo+.05)
for name,(bg,panel,text) in T.items():
    print(f"{name}: text/bg {contrast(text,bg):.2f}; text/panel {contrast(text,panel):.2f}")
