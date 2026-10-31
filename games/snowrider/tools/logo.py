"""The title screen's logo: Snow Rider 3D's own (Texture2D/logo.png), made smaller with a palette of 63 colours,
index 0 see-through: src/logo.c.

  python3 tools/logo.py <the Unity project's Assets folder> src"""
import os, sys
from PIL import Image

ROOT, OUT = sys.argv[1], sys.argv[2]
W, H = 200, 150
im = Image.open(os.path.join(ROOT, 'Texture2D', 'logo.png')).convert('RGBA').resize((W, H), Image.LANCZOS)
alpha = im.getchannel('A')
rgb = Image.new('RGB', im.size, (0, 0, 0))
rgb.paste(im, mask=alpha)
pal = rgb.quantize(63, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
p = pal.getpalette()[:63 * 3]
idx = [0 if alpha.getpixel((x, y)) < 128 else pal.getpixel((x, y)) + 1 for y in range(H) for x in range(W)]
c = ['/* Made by tools/logo.py from Snow Rider 3D\'s logo: do not edit. */', '#include <stdint.h>',
     'const uint16_t logo_w = %d, logo_h = %d;' % (W, H),
     'const uint16_t logo_pal[64] = {0,%s};' % ','.join('0x%04X' % (((p[i] >> 3) << 11) | ((p[i + 1] >> 2) << 5) | (p[i + 2] >> 3)) for i in range(0, 63 * 3, 3)),
     'const uint8_t logo_px[] = {%s};' % ','.join(map(str, idx))]
open(os.path.join(OUT, 'logo.c'), 'w').write('\n'.join(c) + '\n')
