#!/usr/bin/env python3
"""NumView's packer: one file of yours made into the table the app shows (src/media.h), as an ARM object to link
with it (np_media, and eadk_app_name: the app's name), and the app's icon.

  mediapack.py FILE -o media.o --icon icon.png [--name NAME]
      pictures: .jpg .jpeg .png .webp .gif .bmp .tif    documents: .pdf
      video: .mp4 .mov .m4v .webm .mkv (--start, --length, --fps)   books: .epub .txt .md
  mediapack.py --empty -o media.o      the app with no file (it says so)

A picture or a page is two levels of 64 x 64 tiles: one that fits the screen, one four times as big (--zoom) to zoom
into. A tile is a single colour, a few colours packed with DEFLATE, or a JPEG (baseline, the same tables for the
whole file, its scan data alone). A document's tiles keep 2, 4 or 16 colours when that loses nothing a reader would see
(text in 4 greys), so text stays sharp. NumPlay Studio makes the same tables in the browser (customize/media.js). A video is 32 x 32 tiles, only those that changed in a frame, and a whole frame
every 2 seconds to seek to. A book is its text (UTF-8, in the letters of the reading fonts), in DEFLATE chunks.

Needs Pillow and numpy; pypdfium2 for PDFs, ffmpeg for video.
"""
import argparse
import html
import html.parser
import io
import posixpath
import re
import struct
import subprocess
import sys
import unicodedata
import zipfile
import zlib
from pathlib import Path

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFont

Image.MAX_IMAGE_PIXELS = None
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(HERE))
from make_icon import corner_mask  # noqa: E402
from readfont import READ_EXTRA  # noqa: E402

SW, SH = 320, 240
TILE, SHIFT = 64, 6
MAGIC, VERSION = 0x564D504E, 1
PICTURES, VIDEO, BOOK = 1, 2, 3
SOLID, JPEG, PALETTE = 0, 1, 2
DOCUMENT = 1
CHUNK = 4096
PICTURE_EXT = {".jpg", ".jpeg", ".png", ".webp", ".gif", ".bmp", ".tif", ".tiff"}
VIDEO_EXT = {".mp4", ".mov", ".m4v", ".webm", ".mkv", ".avi"}
BOOK_EXT = {".epub", ".txt", ".md"}


def rgb565(c):
    r, g, b = c
    return (r >> 3) << 11 | (g >> 2) << 5 | b >> 3


def to565(a):
    a = a.astype(np.uint16)
    return (a[..., 0] >> 3) << 11 | (a[..., 1] >> 2) << 5 | a[..., 2] >> 3


def from565(v):
    v = v.astype(np.int32)
    return np.stack([((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31], -1)


# ------------------------------------------------------------------ tiles
class Jpeg:
    """Baseline JPEG tiles with one set of headers for the whole file."""

    def __init__(self, quality):
        self.quality = quality
        self.headers = None

    def encode(self, rgb):
        b = io.BytesIO()
        Image.fromarray(rgb, "RGB").save(b, "JPEG", quality=self.quality, subsampling=2, optimize=False)
        data = b.getvalue()
        i, keep, scan = 2, [], None
        while i < len(data):
            m = data[i + 1]
            ln = struct.unpack(">H", data[i + 2:i + 4])[0]
            seg = data[i:i + 2 + ln]
            if m in (0xDB, 0xC4, 0xDD):
                keep.append(seg)
            elif m == 0xC0:
                keep.append(seg[:5] + b"\0\0\0\0" + seg[9:])  # (its size: each tile's own)
            elif m == 0xDA:
                keep.append(seg)
                scan = data[i + 2 + ln:]
                if scan.endswith(b"\xff\xd9"):
                    scan = scan[:-2]
                break
            i += 2 + ln
        headers = b"".join(keep)
        if self.headers is None:
            self.headers = headers
        elif headers != self.headers:
            raise SystemExit("mediapack: the JPEG encoder changed its tables between tiles")
        shown = np.asarray(Image.open(io.BytesIO(data)).convert("RGB"))
        return scan, shown


def palette_bytes(idx, colors):
    n = len(colors)
    bpp = 1 if n <= 2 else 2 if n <= 4 else 4 if n <= 16 else 8
    h, w = idx.shape
    idx = idx.astype(np.uint8)
    if bpp < 8:
        per = 8 // bpp
        wp = -(-w // per) * per
        pad = np.zeros((h, wp), np.uint8)
        pad[:, :w] = idx
        pad = pad.reshape(h, wp // per, per)
        packed = np.zeros((h, wp // per), np.uint8)
        for k in range(per):
            packed |= (pad[:, :, k] << (8 - bpp * (k + 1))).astype(np.uint8)
        raw = packed.tobytes()
    else:
        raw = idx.tobytes()
    z = zlib.compressobj(9, zlib.DEFLATED, -15)
    return bytes([PALETTE, n - 1, 0]) + b"".join(struct.pack("<H", int(c)) for c in colors) + z.compress(raw) + z.flush()


class Stats(dict):
    def add(self, k, n=1):
        self[k] = self.get(k, 0) + n


def encode_tile(rgb, jpeg, doc, stats):
    """(the tile's bytes, what the calculator will show)"""
    v = to565(rgb)
    uniq = np.unique(v)
    if len(uniq) == 1:
        stats.add("solid")
        return bytes([SOLID]) + struct.pack("<H", int(uniq[0])), from565(v).astype(np.uint8)
    cands = []
    if len(uniq) <= (16 if doc else 256):
        cands.append((palette_bytes(np.searchsorted(uniq, v), uniq), from565(v).astype(np.uint8)))
    if doc:  # (2, 4 or 16 colours, when a reader wouldn't see the difference: text in 4 greys is still sharp)
        gray = np.percentile(rgb.max(-1).astype(np.int16) - rgb.min(-1), 99) <= 24
        for n in (2, 4, 16):
            if n >= len(uniq):
                break
            q = Image.fromarray(rgb, "RGB").quantize(n, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
            idx = np.asarray(q)
            cols = np.array(q.getpalette()[:3 * n], np.int32).reshape(n, 3)
            shown = from565(to565(cols.astype(np.uint8)))[idx]
            err = np.abs(shown - rgb.astype(np.int32))
            loose = n == 4 and gray  # (black text's edges in 4 greys: off by more, but looks the same)
            if err.mean() < (8 if loose else 3) and np.percentile(err, 99.5) < (96 if loose else 64):
                cands.append((palette_bytes(idx, [rgb565(c) for c in cols]), shown.astype(np.uint8)))
                break
    pal = min(cands, key=lambda c: len(c[0])) if cands else None
    scan, shown = jpeg.encode(rgb)
    jp = bytes([JPEG]) + scan
    if pal and len(pal[0]) <= len(jp) * (2.0 if doc else 1.0):
        stats.add("palette")
        return pal
    stats.add("jpeg")
    return jp, shown


# ------------------------------------------------------------------ the table
class Table:
    def __init__(self):
        self.b = bytearray(64)

    def add(self, data, align=4):
        self.b += bytes(-len(self.b) % align)
        at = len(self.b)
        self.b += data
        return at

    def finish(self, kind, flags, count, jpeg, index, backdrop, title):
        jpeg_at = self.add(jpeg) if jpeg else 0
        t = title.encode("utf-8")[:35]
        while t and (t[-1] & 0xC0) == 0x80:  # (a letter isn't cut)
            t = t[:-1]
        if t and t[-1] >= 0xC0:
            t = t[:-1]
        struct.pack_into("<IHBBIIIIHH36s", self.b, 0, MAGIC, VERSION, kind, flags, count, jpeg_at,
                         len(jpeg or b""), index, backdrop, 0, t)
        self.b += bytes(-len(self.b) % 4)
        return bytes(self.b)


def levels_of(img, zoom):
    """The page's levels: one that fits the screen, then one `zoom` times as big if the picture has it."""
    w, h = img.size
    s = min(SW / w, SH / h)
    l0 = (max(1, round(w * s)), max(1, round(h * s)))
    out = [img.resize(l0, Image.LANCZOS, reducing_gap=3.0)]
    big = (l0[0] * zoom, l0[1] * zoom)
    if w >= l0[0] * 1.5:
        if w < big[0]:
            big = (w, h)
        out.append(img.resize(big, Image.LANCZOS, reducing_gap=3.0) if big != (w, h) else img)
    return out


def pictures(pages, doc, quality, zoom):
    t, jpeg, stats, recs = Table(), Jpeg(quality), Stats(), []
    for pg in pages:
        img = pg() if callable(pg) else pg
        levels = []
        for lv in levels_of(img, zoom):
            a = np.asarray(lv.convert("RGB"))
            h, w, _ = a.shape
            offs = []
            for ty in range(0, h, TILE):
                for tx in range(0, w, TILE):
                    data, _ = encode_tile(np.ascontiguousarray(a[ty:ty + TILE, tx:tx + TILE]), jpeg, doc, stats)
                    offs.append(t.add(data, 1))
            offs.append(len(t.b))
            levels.append((w, h, t.add(b"".join(struct.pack("<I", o) for o in offs))))
        recs.append(t.add(struct.pack("<BBH", len(levels), SHIFT, 0) +
                          b"".join(struct.pack("<HHI", w, h, at) for w, h, at in levels)))
        print(f"  page {len(recs)}: " + ", ".join(f"{w}x{h}" for w, h, _ in levels), flush=True)
    index = t.add(b"".join(struct.pack("<I", r) for r in recs))
    backdrop = rgb565((0x2C, 0x2C, 0x2E)) if doc else 0
    print(f"  tiles: {dict(stats)}")
    return t, PICTURES, DOCUMENT if doc else 0, len(recs), jpeg.headers, index, backdrop


def open_picture(path):
    img = Image.open(path)
    try:
        from PIL import ImageOps
        img = ImageOps.exif_transpose(img)
    except Exception:
        pass
    if img.format == "JPEG":
        img.draft("RGB", (SW * 8, SH * 8))
    if img.mode in ("RGBA", "LA", "P"):
        img = img.convert("RGBA")
        bg = Image.new("RGBA", img.size, (0, 0, 0, 255))
        img = Image.alpha_composite(bg, img)
    return img.convert("RGB")


def pdf_pages(path, zoom):
    import pypdfium2 as pdfium
    pdf = pdfium.PdfDocument(str(path))
    out = []
    for i in range(len(pdf)):
        def render(i=i):
            page = pdf[i]
            w, h = page.get_size()
            s = min(SW / w, SH / h) * zoom
            return page.render(scale=s, draw_annots=True).to_pil().convert("RGB")
        out.append(render)
    return out


# ------------------------------------------------------------------ video
def video(path, start, length, fps, quality):
    probe = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries",
                            "stream=width,height:stream_side_data=rotation", "-of", "csv=p=0", str(path)],
                           capture_output=True, text=True, check=True).stdout.split("\n")[0].split(",")
    sw, sh = int(probe[0]), int(probe[1])
    s = min(SW / sw, SH / sh)
    w, h = round(sw * s / 2) * 2, round(sh * s / 2) * 2
    cmd = ["ffmpeg", "-v", "error"] + (["-ss", str(start)] if start else []) + ["-i", str(path)] + \
          (["-t", str(length)] if length else []) + \
          ["-vf", f"fps={fps},scale={w}:{h}:flags=area", "-f", "rawvideo", "-pix_fmt", "rgb24", "-"]
    raw = subprocess.run(cmd, capture_output=True, check=True).stdout
    frames = np.frombuffer(raw, np.uint8).reshape(-1, h, w, 3)
    tw = th = 32
    cols, rows = -(-w // tw), -(-h // th)
    key = max(1, round(fps * 2))
    t, jpeg, stats = Table(), Jpeg(quality), Stats()
    shown = np.zeros((h, w, 3), np.uint8)
    sent = np.zeros((h, w, 3), np.int16)  # (each tile as it was when last sent: a tile that hasn't changed since stays)
    offs = []
    for f, fr in enumerate(frames):
        whole = f % key == 0
        bits, parts = bytearray(-(-(cols * rows) // 8)), []
        for i in range(cols * rows):
            r, c = divmod(i, cols)
            ys, xs = slice(r * th, min(h, r * th + th)), slice(c * tw, min(w, c * tw + tw))
            src = np.ascontiguousarray(fr[ys, xs])
            if not whole:
                diff = np.abs(src.astype(np.int16) - sent[ys, xs])
                if diff.mean() < 2.0 and np.percentile(diff, 98) < 24:
                    stats.add("kept")
                    continue
            data, look = encode_tile(src, jpeg, False, stats)
            shown[ys, xs] = look
            sent[ys, xs] = src
            bits[i >> 3] |= 1 << (i & 7)
            parts.append(struct.pack("<H", len(data)) + data)
        offs.append(t.add(bytes(bits) + b"".join(parts), 1))
    offs.append(len(t.b))
    head = struct.pack("<HHHHHH", w, h, tw, th, round(fps * 100), key)
    index = t.add(head + b"".join(struct.pack("<I", o) for o in offs))
    print(f"  {len(frames)} frames of {w}x{h} at {fps} a second; tiles: {dict(stats)}")
    return t, VIDEO, 0, len(frames), jpeg.headers, index, 0


def video_still(path, start, length):
    """Its most colourful frame of a few, for the icon."""
    best, score = None, -1
    for k in range(1, 8):
        raw = subprocess.run(["ffmpeg", "-v", "error", "-ss", str(start + length * k / 8), "-i", str(path),
                              "-frames:v", "1", "-f", "image2pipe", "-vcodec", "png", "-"],
                             capture_output=True, check=True).stdout
        im = Image.open(io.BytesIO(raw)).convert("RGB")
        a = np.asarray(im.resize((64, 36))).astype(np.float32)
        sc = (a.max(-1) - a.min(-1)).mean() + a.std()
        if sc > score:
            best, score = im, sc
    return best


# ------------------------------------------------------------------ books
HEADING, NEW_PAGE = "\x01", "\x02"
BLOCKS = {"p", "div", "li", "blockquote", "tr", "dt", "dd", "section", "article", "figure", "figcaption", "pre",
          "table", "ul", "ol", "body", "br", "hr"}
HEADS = {"h1", "h2", "h3", "h4", "h5", "h6"}
KNOWN = set(range(32, 127)) | set(READ_EXTRA)


def letters(s):
    """The text in the reading fonts' letters: the others as their plain form, or left out."""
    out = []
    for ch in s:
        o = ord(ch)
        if o in KNOWN or ch in "\n\x01\x02":
            out.append(ch)
        elif ch in "      \t":
            out.append(" ")
        elif ch in "‐‑‒−":
            out.append("-")
        else:
            plain = "".join(c for c in unicodedata.normalize("NFKD", ch) if ord(c) in KNOWN)
            out.append(plain)
    return "".join(out)


class Xhtml(html.parser.HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.paras, self.cur, self.head, self.skip = [], [], False, 0

    def flush(self):
        text = re.sub(r"\s+", " ", "".join(self.cur)).strip()
        if text:
            self.paras.append((HEADING if self.head else "") + text)
        self.cur, self.head = [], False

    def handle_starttag(self, tag, attrs):
        if tag in ("script", "style", "head", "title"):
            self.skip += 1
        elif tag in HEADS:
            self.flush()
            self.head = True
        elif tag in BLOCKS:
            self.flush()
        if tag == "li":
            self.cur.append("• ")

    def handle_endtag(self, tag):
        if tag in ("script", "style", "head", "title"):
            self.skip = max(0, self.skip - 1)
        elif tag in HEADS or tag in BLOCKS:
            self.flush()

    def handle_data(self, data):
        if not self.skip:
            self.cur.append(data)


def epub(path):
    z = zipfile.ZipFile(path)
    container = z.read("META-INF/container.xml").decode("utf-8", "replace")
    opf_path = re.search(r'full-path="([^"]+)"', container).group(1)
    opf = z.read(opf_path).decode("utf-8", "replace")
    base = posixpath.dirname(opf_path)
    items = {m.group(1): m.group(2) for m in re.finditer(r'<item\b[^>]*?id="([^"]+)"[^>]*?href="([^"]+)"', opf)}
    items.update({m.group(2): m.group(1) for m in re.finditer(r'<item\b[^>]*?href="([^"]+)"[^>]*?id="([^"]+)"', opf)})
    spine = re.findall(r'<itemref\b[^>]*?idref="([^"]+)"', opf)
    title = re.search(r"<dc:title[^>]*>(.*?)</dc:title>", opf, re.S)
    title = html.unescape(title.group(1)).strip() if title else Path(path).stem
    cover = None
    m = re.search(r'<meta\s+name="cover"\s+content="([^"]+)"', opf) or re.search(
        r'<meta\s+content="([^"]+)"\s+name="cover"', opf)
    cid = m.group(1) if m else next((i for i in items if "cover" in i.lower() and re.search(
        r"\.(jpe?g|png)$", items[i], re.I)), None)
    if cid and cid in items:
        try:
            cover = Image.open(io.BytesIO(z.read(posixpath.join(base, items[cid])))).convert("RGB")
        except Exception:
            cover = None
    parts = []
    for idref in spine:
        if idref not in items:
            continue
        p = Xhtml()
        p.feed(z.read(posixpath.normpath(posixpath.join(base, items[idref]))).decode("utf-8", "replace"))
        p.flush()
        if p.paras:
            parts.append(p.paras)
    return title, parts, cover


def plain_text(path):
    text = Path(path).read_text("utf-8", "replace")
    parts, paras = [], []
    for block in re.split(r"\n\s*\n", text):
        b = block.strip()
        if not b:
            continue
        m = re.match(r"^(#{1,6})\s+(.*)$", b)
        if m:
            if m.group(1) in ("#", "##") and paras:
                parts.append(paras)
                paras = []
            paras.append(HEADING + m.group(2).strip())
            continue
        lines = [ln.strip() for ln in b.split("\n")]
        if all(re.match(r"^([-*+]|\d+\.)\s+", ln) for ln in lines if ln):
            paras += ["• " + re.sub(r"^([-*+]|\d+\.)\s+", "", ln) for ln in lines if ln]
        else:
            paras.append(" ".join(lines))
    if paras:
        parts.append(paras)
    clean = lambda s: re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", re.sub(r"[*_`]+", "", s))  # (markdown's marks)
    return Path(path).stem, [[clean(p) for p in part] for part in parts], None


def book(title, parts):
    text = letters(NEW_PAGE.join("\n".join(paras) + "\n" for paras in parts))
    data = text.encode("utf-8")
    t = Table()
    starts, chunks, i = [], [], 0
    while i < len(data):
        e = min(len(data), i + CHUNK)
        if e < len(data):
            cut = max(data.rfind(b" ", i, e), data.rfind(b"\n", i, e))
            if cut > i:
                e = cut + 1
        starts.append(i)
        z = zlib.compressobj(9, zlib.DEFLATED, -15)
        chunks.append(t.add(z.compress(data[i:e]) + z.flush(), 1))
        i = e
    starts.append(len(data))
    chunks.append(len(t.b))
    index = t.add(struct.pack("<IHH", len(data), len(starts) - 1, 0) +
                  b"".join(struct.pack("<I", s) for s in starts) + b"".join(struct.pack("<I", c) for c in chunks))
    print(f"  {len(parts)} parts, {len(data)} bytes of text in {len(starts) - 1} chunks")
    return t, BOOK, 0, len(data), None, index, 0


# ------------------------------------------------------------------ the object, the icon
def elf_object(sections):
    """A 32-bit ARM relocatable object: for each (section, symbol, data, align), the section and a global symbol."""
    names = [s for s, _, _, _ in sections]
    shstr = b"\0" + b"".join(n.encode() + b"\0" for n in names) + b".symtab\0.strtab\0.shstrtab\0"
    strtab = b"\0" + b"".join(sym.encode() + b"\0" for _, sym, _, _ in sections)
    symtab = struct.pack("<IIIBBH", 0, 0, 0, 0, 0, 0)
    at = 1
    for k, (_, sym, data, _) in enumerate(sections):
        symtab += struct.pack("<IIIBBH", at, 0, len(data), 1 << 4 | 1, 0, k + 1)
        at += len(sym) + 1
    out = bytearray(52)
    offs = []
    for part in [d for _, _, d, _ in sections] + [symtab, strtab, shstr]:
        out += bytes(-len(out) % 4)
        offs.append(len(out))
        out += part
    out += bytes(-len(out) % 4)
    shoff = len(out)
    n = len(sections)
    sh = [(0,) * 10]
    for k, (name, _, data, align) in enumerate(sections):
        sh.append((shstr.index(name.encode() + b"\0"), 1, 2, 0, offs[k], len(data), 0, 0, align, 0))
    sh.append((shstr.index(b".symtab"), 2, 0, 0, offs[n], len(symtab), n + 2, 1, 4, 16))
    sh.append((shstr.index(b".strtab"), 3, 0, 0, offs[n + 1], len(strtab), 0, 0, 1, 0))
    sh.append((shstr.index(b".shstrtab"), 3, 0, 0, offs[n + 2], len(shstr), 0, 0, 1, 0))
    for s in sh:
        out += struct.pack("<10I", *s)
    struct.pack_into("<16sHHIIIIIHHHHHH", out, 0, b"\x7fELF\x01\x01\x01" + bytes(9), 1, 40, 1, 0, 0, shoff, 0, 52,
                     0, 0, 40, len(sh), len(sh) - 1)
    return bytes(out)


def icon(img, out, page=False):
    """55 x 56, NumPlay's corners (tools/icon.py's way), from a picture: filled, or a whole page on the backdrop
    the app shows it on."""
    W, H = 55, 56
    if page:
        s = min((W - 12) / img.width, (H - 8) / img.height)
        small = img.resize((max(1, round(img.width * s)), max(1, round(img.height * s))), Image.LANCZOS)
        canvas = Image.new("RGBA", (W, H), (0x2C, 0x2C, 0x2E, 255))
        canvas.paste(small, ((W - small.width) // 2, (H - small.height) // 2))
    else:
        s = max(W / img.width, H / img.height)
        big = img.resize((max(W, round(img.width * s)), max(H, round(img.height * s))), Image.LANCZOS)
        x, y = (big.width - W) // 2, (big.height - H) // 2
        canvas = big.crop((x, y, x + W, y + H)).convert("RGBA")
    alpha = ImageChops.darker(canvas.getchannel("A"), corner_mask(W, H))
    flat = Image.new("RGB", (W, H), (255, 255, 255))
    flat.paste(canvas.convert("RGB"), (0, 0), alpha)
    flat.putalpha(alpha)
    flat.save(out, optimize=True)


def book_icon(title, out):
    """A book with no cover: its title on paper, in Nunito."""
    W, H, S = 55, 56, 4
    im = Image.new("RGB", (W * S, H * S), (0x23, 0x21, 0x1E))
    d = ImageDraw.Draw(im)
    font = ImageFont.truetype(str(ROOT / "tools" / ".fonts" / "Nunito.ttf"), 11 * S)
    font.set_variation_by_axes([850])
    words, lines = title.split(), []
    for w in words:
        if lines and d.textlength(lines[-1] + " " + w, font=font) <= (W - 10) * S:
            lines[-1] += " " + w
        else:
            lines.append(w)
    lines = lines[:3]
    y = (H * S - len(lines) * 13 * S) // 2
    for ln in lines:
        d.text((6 * S, y), ln, font=font, fill=(0xFA, 0xF8, 0xF3))
        y += 13 * S
    d.rectangle((6 * S, (H - 9) * S, 18 * S, (H - 8) * S), fill=(0xA8, 0xA4, 0x9C))
    icon(im.resize((W, H), Image.LANCZOS), out)


def app_name(path, name):
    if name:
        return name
    n = re.sub(r"[_]+", " ", Path(path).stem).strip()
    if len(n) > 22:
        n = n[:22].rsplit(" ", 1)[0] if " " in n[:22] else n[:22]
    return n or "File"


def main():
    ap = argparse.ArgumentParser(description="NumView: a file as an app (np_media)")
    ap.add_argument("file", nargs="?")
    ap.add_argument("-o", dest="obj", required=True, help="ARM object to write")
    ap.add_argument("--icon", help="the app's icon to write (PNG, 55 x 56)")
    ap.add_argument("--bin", help="the raw table to write too")
    ap.add_argument("--name", help="the app's name (the file's, otherwise)")
    ap.add_argument("--empty", action="store_true", help="no file: the app says how to add one")
    ap.add_argument("--quality", type=int, help="JPEG quality (80 for pictures, 62 for video)")
    ap.add_argument("--zoom", type=int, default=4, help="the bigger level, times the screen's (4)")
    ap.add_argument("--start", type=float, default=0, help="video: from this second")
    ap.add_argument("--length", type=float, default=0, help="video: this many seconds")
    ap.add_argument("--fps", type=float, default=12.5, help="video: frames a second (12.5)")
    a = ap.parse_args()
    if a.empty:
        blob, name = b"", a.name or "NumView"
        still = None
    else:
        path = Path(a.file)
        ext = path.suffix.lower()
        name = app_name(path, a.name)
        print(f"NUMVIEW {path.name}: {name}")
        crop_top = False
        if ext in PICTURE_EXT:
            img = open_picture(path)
            res = pictures([img], False, a.quality or 82, a.zoom)
            still = img
        elif ext == ".pdf":
            pages = pdf_pages(path, a.zoom)
            res = pictures(pages, True, a.quality or 82, a.zoom)
            still, crop_top = pages[0](), True
        elif ext in VIDEO_EXT:
            res = video(path, a.start, a.length, a.fps, a.quality or 62)
            still = video_still(path, a.start, a.length or 6)
        elif ext in BOOK_EXT:
            title, parts, cover = epub(path) if ext == ".epub" else plain_text(path)
            res = book(title, parts)
            still = cover
            if not a.name and ext == ".epub":
                name = app_name(title, None) if len(title) <= 22 else name
        else:
            sys.exit(f"mediapack: {path.name}: NumView doesn't know {ext or 'this kind of'} files")
        t, kind, flags, count, jpeg, index, backdrop = res
        blob = t.finish(kind, flags, count, jpeg, index, backdrop, name)
        if a.icon:
            if still is not None:
                icon(still, a.icon, crop_top)
            else:
                book_icon(name, a.icon)
    if a.empty and a.icon:
        book_icon(name, a.icon)
    nz = name.encode("utf-8") + b"\0"
    obj = elf_object([(".rodata.np_media", "np_media", blob or bytes(4), 4),
                      (".rodata.eadk_app_name", "eadk_app_name", nz, 1)])
    Path(a.obj).write_bytes(obj)
    if a.bin:
        Path(a.bin).write_bytes(blob)
    print(f"MEDIA   {a.obj}: {len(blob)} bytes")


if __name__ == "__main__":
    main()
