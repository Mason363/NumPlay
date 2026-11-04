"""Screenshots (PPM) into one sheet, 2x, for looking at them: python3 tools/sheet.py out.png a.ppm b.ppm ..."""
import sys
from PIL import Image

files = sys.argv[2:]
cols = min(3, len(files))
rows = (len(files) + cols - 1) // cols
sheet = Image.new("RGB", (cols * 640, rows * 480), (40, 40, 40))
for i, f in enumerate(files):
    im = Image.open(f).convert("RGB").resize((640, 480), Image.NEAREST)
    sheet.paste(im, ((i % cols) * 640, (i // cols) * 480))
sheet.save(sys.argv[1])
