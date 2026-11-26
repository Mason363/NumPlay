/* NumPlay Studio: your pictures, PDFs, videos and books, each an app of its own in your NumPlay (NumView,
 * games/numview). A file becomes the table NumView shows (games/numview/src/media.h), the way
 * games/numview/tools/mediapack.py makes it:
 *   a picture or a PDF's page: two levels of 64 x 64 tiles, one that fits the screen and one 4 times as big to zoom
 *     into; a tile is one colour, its few colours (2, 4, 16; a page's text in 4 greys) packed with DEFLATE, or JPEG;
 *   a video: 32 x 32 tiles, only those that changed since they were last sent, a whole frame every 2 seconds;
 *   a book (.epub, .txt): its text, in the letters of NumView's reading fonts, in DEFLATE chunks.
 * The JPEG encoder is baseline with the standard tables (ITU T.81, annex K), the same for every tile of a file: the
 * file keeps one set of headers and each tile only its scan data. app.js links the table with NumView (the kit's
 * apps/NumView-<viewer>-app.o.gz) and takes the app in as one of your apps. PDFs are drawn by pdf.js (pdfjs/),
 * fetched when one comes. */
'use strict';
(function (root) {
  const SW = 320, SH = 240, TILE = 64, SHIFT = 6, ZOOM = 4;
  const MAGIC = 0x564D504E, VERSION = 1, PICTURES = 1, VIDEO = 2, BOOK = 3, DOCUMENT = 1;
  const SOLID = 0, JPEG = 1, PALETTE = 2, CHUNK = 4096;
  const VIDEO_FPS = 10, VIDEO_MAX = 15;  // (seconds: more doesn't fit beside much)
  const EXTS = {
    picture: ['jpg', 'jpeg', 'png', 'webp', 'gif', 'bmp', 'avif'], document: ['pdf'],
    video: ['mp4', 'mov', 'm4v', 'webm'], book: ['epub', 'txt'],
  };
  const extOf = (name) => ((name.match(/\.([^.]+)$/) || [])[1] || '').toLowerCase();
  const kindOf = (name) => Object.keys(EXTS).find((k) => EXTS[k].includes(extOf(name))) || null;
  const VIEWER = { picture: 'pictures', document: 'pictures', video: 'video', book: 'book' };

  // ------------------------------------------------------------------ JPEG
  const ZZ = [0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7,
    14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60,
    61, 54, 47, 55, 62, 63];
  const QL = [16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55, 14, 13, 16, 24, 40, 57, 69, 56, 14, 17,
    22, 29, 51, 87, 80, 62, 18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92, 49, 64, 78, 87, 103,
    121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99];
  const QC = [17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99, 99, 99, 47, 66,
    99, 99, 99, 99, 99, 99].concat(new Array(32).fill(99));
  const range = (a, b) => Array.from({ length: b - a }, (_, i) => a + i);
  const AC_L = [0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22,
    0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72,
    0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38,
    0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63,
    0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86,
    0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8,
    0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8,
    0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa];
  const AC_C = [0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13,
    0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1,
    0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a,
    0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84,
    0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
    0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6,
    0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
    0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa];
  // (class and id, then the number of codes of each length and the symbols)
  const HUFF = [
    [0x00, [0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0], range(0, 12)],
    [0x10, [0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d], AC_L],
    [0x01, [0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0], range(0, 12)],
    [0x11, [0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77], AC_C],
  ];
  const COS = Float64Array.from({ length: 64 }, (_, i) => {
    const u = i >> 3, x = i & 7;
    return 0.5 * (u ? 1 : Math.SQRT1_2) * Math.cos((2 * x + 1) * u * Math.PI / 16);
  });
  function qtable(base, quality) {
    const s = quality < 50 ? Math.floor(5000 / quality) : 200 - 2 * quality;
    return base.map((b) => Math.min(255, Math.max(1, Math.floor((b * s + 50) / 100))));
  }
  function codes(bits, vals) {
    const code = new Uint16Array(256), size = new Uint8Array(256);
    let c = 0, k = 0;
    for (let l = 1; l <= 16; l++) {
      for (let i = 0; i < bits[l - 1]; i++, k++) code[vals[k]] = c++, size[vals[k]] = l;
      c <<= 1;
    }
    return { code, size };
  }
  // 8 x 8 values (level-shifted) to their coefficients, in natural order
  function fdct(b, out) {
    const t = new Float64Array(64);
    for (let y = 0; y < 8; y++)
      for (let u = 0; u < 8; u++) {
        let s = 0;
        for (let x = 0; x < 8; x++) s += COS[u * 8 + x] * b[y * 8 + x];
        t[y * 8 + u] = s;
      }
    for (let u = 0; u < 8; u++)
      for (let v = 0; v < 8; v++) {
        let s = 0;
        for (let y = 0; y < 8; y++) s += COS[v * 8 + y] * t[y * 8 + u];
        out[v * 8 + u] = s;
      }
  }
  class Jpeg {
    constructor(quality) {
      this.q = [qtable(QL, quality), qtable(QC, quality)];
      this.h = HUFF.map(([, bits, vals]) => codes(bits, vals));
      const h = [0xFF, 0xD8];
      const seg = (m, body) => h.push(0xFF, m, (body.length + 2) >> 8, (body.length + 2) & 255, ...body);
      seg(0xDB, [0, ...ZZ.map((n) => this.q[0][n]), 1, ...ZZ.map((n) => this.q[1][n])]);
      seg(0xC0, [8, 0, 0, 0, 0, 3, 1, 0x22, 0, 2, 0x11, 1, 3, 0x11, 1]);  // (no size: each tile's own)
      seg(0xC4, HUFF.flatMap(([tc, bits, vals]) => [tc, ...bits, ...vals]));
      seg(0xDA, [3, 1, 0x00, 2, 0x11, 3, 0x11, 0, 63, 0]);
      this.headers = Uint8Array.from(h);
    }
    // the scan data of a w x h picture (rgb: 3 bytes a pixel), 4:2:0
    encode(rgb, w, h) {
      const out = [], b = new Float64Array(64), c = new Float64Array(64);
      let acc = 0, n = 0;
      const put = (code, size) => {
        acc = (acc << size) | code, n += size;
        while (n >= 8) {
          const v = (acc >>> (n - 8)) & 255;
          out.push(v);
          if (v === 255) out.push(0);
          n -= 8;
        }
        acc &= (1 << n) - 1;
      };
      const cat = (v) => (v ? 32 - Math.clz32(Math.abs(v)) : 0);
      const block = (q, pred, dc, ac) => {
        fdct(b, c);
        const v = new Int32Array(64);
        for (let k = 0; k < 64; k++) v[k] = Math.max(-1023, Math.min(1023, Math.round(c[ZZ[k]] / q[ZZ[k]])));
        const d = v[0] - pred, s = cat(d);
        put(dc.code[s], dc.size[s]);
        if (s) put(d < 0 ? d + (1 << s) - 1 : d, s);
        let run = 0;
        for (let k = 1; k < 64; k++) {
          if (!v[k]) { run++; continue; }
          while (run > 15) put(ac.code[0xF0], ac.size[0xF0]), run -= 16;
          const e = cat(v[k]), sym = run << 4 | e;
          put(ac.code[sym], ac.size[sym]);
          put(v[k] < 0 ? v[k] + (1 << e) - 1 : v[k], e);
          run = 0;
        }
        if (run) put(ac.code[0], ac.size[0]);
        return v[0];
      };
      const at = (x, y) => 3 * (Math.min(y, h - 1) * w + Math.min(x, w - 1));
      let py = 0, pb = 0, pr = 0;
      for (let my = 0; my < h; my += 16)
        for (let mx = 0; mx < w; mx += 16) {
          for (let by = 0; by < 16; by += 8)
            for (let bx = 0; bx < 16; bx += 8) {
              for (let y = 0; y < 8; y++)
                for (let x = 0; x < 8; x++) {
                  const p = at(mx + bx + x, my + by + y);
                  b[y * 8 + x] = 0.299 * rgb[p] + 0.587 * rgb[p + 1] + 0.114 * rgb[p + 2] - 128;
                }
              py = block(this.q[0], py, this.h[0], this.h[1]);
            }
          for (const [k, f] of [[0, [-0.168736, -0.331264, 0.5]], [1, [0.5, -0.418688, -0.081312]]]) {
            for (let y = 0; y < 8; y++)
              for (let x = 0; x < 8; x++) {
                let s = 0;
                for (let dy = 0; dy < 2; dy++)
                  for (let dx = 0; dx < 2; dx++) {
                    const p = at(mx + 2 * x + dx, my + 2 * y + dy);
                    s += f[0] * rgb[p] + f[1] * rgb[p + 1] + f[2] * rgb[p + 2];
                  }
                b[y * 8 + x] = s / 4;
              }
            if (k === 0) pb = block(this.q[1], pb, this.h[2], this.h[3]);
            else pr = block(this.q[1], pr, this.h[2], this.h[3]);
          }
        }
      if (n) put((1 << (8 - n)) - 1, 8 - n);
      return Uint8Array.from(out);
    }
  }

  // ------------------------------------------------------------------ tiles
  const to565 = (r, g, b) => ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
  const r565 = (v) => ((v >> 11) & 31) * 255 / 31 | 0, g565 = (v) => ((v >> 5) & 63) * 255 / 63 | 0, b565 = (v) => (v & 31) * 255 / 31 | 0;
  async function deflate(u8) {
    const s = new Blob([u8]).stream().pipeThrough(new CompressionStream('deflate-raw'));
    return new Uint8Array(await new Response(s).arrayBuffer());
  }
  async function inflate(u8) {
    const s = new Blob([u8]).stream().pipeThrough(new DecompressionStream('deflate-raw'));
    return new Uint8Array(await new Response(s).arrayBuffer());
  }
  async function paletteBytes(idx, w, h, colors) {
    const n = colors.length, bpp = n <= 2 ? 1 : n <= 4 ? 2 : n <= 16 ? 4 : 8, rowb = (w * bpp + 7) >> 3;
    const raw = new Uint8Array(rowb * h);
    for (let y = 0; y < h; y++)
      for (let x = 0; x < w; x++) raw[y * rowb + ((x * bpp) >> 3)] |= idx[y * w + x] << (8 - bpp - ((x * bpp) & 7));
    const z = await deflate(raw), out = new Uint8Array(3 + 2 * n + z.length);
    out[0] = PALETTE, out[1] = n - 1;
    colors.forEach((c, k) => { out[3 + 2 * k] = c & 255; out[4 + 2 * k] = c >> 8; });
    out.set(z, 3 + 2 * n);
    return out;
  }
  // the colour of each of n boxes of the pixels, split at their median along their widest colour
  function medianCut(rgb, count, n) {
    let boxes = [Array.from({ length: count }, (_, i) => i)];
    while (boxes.length < n) {
      let best = -1, ch = 0, span = 0;
      boxes.forEach((bx, i) => {
        if (bx.length < 2) return;
        for (let c = 0; c < 3; c++) {
          let lo = 255, hi = 0;
          for (const p of bx) { const v = rgb[3 * p + c]; if (v < lo) lo = v; if (v > hi) hi = v; }
          if (hi - lo > span) span = hi - lo, best = i, ch = c;
        }
      });
      if (best < 0) break;
      const bx = boxes[best].sort((p, q) => rgb[3 * p + ch] - rgb[3 * q + ch]), mid = bx.length >> 1;
      boxes.splice(best, 1, bx.slice(0, mid), bx.slice(mid));
    }
    return boxes.map((bx) => {
      let r = 0, g = 0, b = 0;
      for (const p of bx) r += rgb[3 * p], g += rgb[3 * p + 1], b += rgb[3 * p + 2];
      return to565(Math.round(r / bx.length), Math.round(g / bx.length), Math.round(b / bx.length));
    });
  }
  const pct = (hist, total, p) => { let n = 0; for (let v = 0; v < hist.length; v++) if ((n += hist[v]) >= total * p / 100) return v; return hist.length - 1; };
  // a tile's bytes: one colour, its exact colours, 2, 4 or 16 colours a reader can't tell apart (documents), or JPEG
  async function encodeTile(rgb, w, h, jpeg, doc) {
    const count = w * h, v = new Uint16Array(count);
    for (let i = 0; i < count; i++) v[i] = to565(rgb[3 * i], rgb[3 * i + 1], rgb[3 * i + 2]);
    const uniq = [...new Set(v)].sort((a, b) => a - b);
    if (uniq.length === 1) return Uint8Array.of(SOLID, uniq[0] & 255, uniq[0] >> 8);
    const cands = [];
    if (uniq.length <= (doc ? 16 : 256)) {
      const at = new Map(uniq.map((c, i) => [c, i]));
      cands.push(await paletteBytes(Uint8Array.from(v, (c) => at.get(c)), w, h, uniq));
    }
    if (doc) {
      const chroma = new Uint32Array(256);
      for (let i = 0; i < count; i++) {
        const r = rgb[3 * i], g = rgb[3 * i + 1], b = rgb[3 * i + 2];
        chroma[Math.max(r, g, b) - Math.min(r, g, b)]++;
      }
      const gray = pct(chroma, count, 99) <= 24;
      for (const n of [2, 4, 16]) {
        if (n >= uniq.length) break;
        const pal = medianCut(rgb, count, n), idx = new Uint8Array(count), err = new Uint32Array(256);
        const pr = pal.map(r565), pg = pal.map(g565), pb = pal.map(b565);
        let sum = 0;
        for (let i = 0; i < count; i++) {
          const r = rgb[3 * i], g = rgb[3 * i + 1], b = rgb[3 * i + 2];
          let best = 0, bd = 1e9;
          for (let k = 0; k < pal.length; k++) {
            const d = (pr[k] - r) ** 2 + (pg[k] - g) ** 2 + (pb[k] - b) ** 2;
            if (d < bd) bd = d, best = k;
          }
          idx[i] = best;
          for (const e of [Math.abs(pr[best] - r), Math.abs(pg[best] - g), Math.abs(pb[best] - b)]) err[e]++, sum += e;
        }
        const loose = n === 4 && gray;  // (black text's edges in 4 greys: off by more, but looks the same)
        if (sum / (3 * count) < (loose ? 8 : 3) && pct(err, 3 * count, 99.5) < (loose ? 96 : 64)) {
          cands.push(await paletteBytes(idx, w, h, pal));
          break;
        }
      }
    }
    const scan = jpeg.encode(rgb, w, h), jp = new Uint8Array(scan.length + 1);
    jp[0] = JPEG, jp.set(scan, 1);
    const pal = cands.sort((a, b) => a.length - b.length)[0];
    return pal && pal.length <= jp.length * (doc ? 2 : 1) ? pal : jp;
  }
  function rgbOf(px, W, x0, y0, w, h) {  // (px: RGBA, W wide)
    const out = new Uint8Array(w * h * 3);
    for (let y = 0; y < h; y++)
      for (let x = 0; x < w; x++) {
        const p = 4 * ((y0 + y) * W + x0 + x), q = 3 * (y * w + x);
        out[q] = px[p], out[q + 1] = px[p + 1], out[q + 2] = px[p + 2];
      }
    return out;
  }

  // ------------------------------------------------------------------ the table
  class Table {
    constructor() { this.parts = [new Uint8Array(64)]; this.n = 64; }
    add(data, align = 4) {
      const pad = (align - (this.n % align)) % align;
      if (pad) this.parts.push(new Uint8Array(pad)), this.n += pad;
      const at = this.n;
      this.parts.push(data), this.n += data.length;
      return at;
    }
    u32s(list) { const d = new Uint8Array(4 * list.length), v = new DataView(d.buffer); list.forEach((x, i) => v.setUint32(4 * i, x, true)); return this.add(d); }
    finish(kind, flags, count, jpeg, index, backdrop, title) {
      const jat = jpeg ? this.add(jpeg) : 0;
      if (this.n % 4) this.parts.push(new Uint8Array(4 - this.n % 4)), this.n += 4 - this.n % 4;
      const out = new Uint8Array(this.n);
      let at = 0;
      for (const p of this.parts) out.set(p, at), at += p.length;
      const v = new DataView(out.buffer);
      v.setUint32(0, MAGIC, true); v.setUint16(4, VERSION, true); out[6] = kind; out[7] = flags;
      v.setUint32(8, count, true); v.setUint32(12, jat, true); v.setUint32(16, jpeg ? jpeg.length : 0, true);
      v.setUint32(20, index, true); v.setUint16(24, backdrop, true);
      let t = new TextEncoder().encode(title).slice(0, 35);
      while (t.length && (t[t.length - 1] & 0xC0) === 0x80) t = t.slice(0, -1);
      if (t.length && t[t.length - 1] >= 0xC0) t = t.slice(0, -1);
      out.set(t, 28);
      return out;
    }
  }

  // ------------------------------------------------------------------ canvases
  function canvas(w, h) {
    if (typeof OffscreenCanvas !== 'undefined') return new OffscreenCanvas(w, h);
    const c = document.createElement('canvas');
    c.width = w, c.height = h;
    return c;
  }
  function drawn(src, w, h, bg) {
    const c = canvas(w, h), g = c.getContext('2d', { willReadFrequently: true });
    g.fillStyle = bg || '#000';
    g.fillRect(0, 0, w, h);
    g.imageSmoothingEnabled = true;
    g.imageSmoothingQuality = 'high';
    g.drawImage(src, 0, 0, w, h);
    return c;
  }
  const pixels = (c) => c.getContext('2d', { willReadFrequently: true }).getImageData(0, 0, c.width, c.height).data;
  const fit = (w, h, W, H) => { const s = Math.min(W / w, H / h); return [Math.max(1, Math.round(w * s)), Math.max(1, Math.round(h * s))]; };
  // what the app shows first: the picture in the screen's middle, on its backdrop
  function screenOf(src, bg) {
    const c = canvas(SW, SH), g = c.getContext('2d');
    g.fillStyle = bg;
    g.fillRect(0, 0, SW, SH);
    const [w, h] = fit(src.width, src.height, SW, SH);
    g.imageSmoothingQuality = 'high';
    g.drawImage(src, (SW - w) >> 1, (SH - h) >> 1, w, h);
    return c;
  }
  // the app's icon, 55 x 56 with NumPlay's corners (tools/icon.py's), white outside them: filled with the picture,
  // or a page whole on the backdrop the app shows it on
  function iconOf(src, page) {
    const W = 55, H = 56, c = canvas(W, H), g = c.getContext('2d');
    g.fillStyle = '#fff';
    g.fillRect(0, 0, W, H);
    g.save();
    g.beginPath();
    g.roundRect(0, 0, W, H, 12);
    g.clip();
    g.imageSmoothingQuality = 'high';
    if (page) {
      g.fillStyle = '#2c2c2e';
      g.fillRect(0, 0, W, H);
      const [w, h] = fit(src.width, src.height, W - 12, H - 8);
      g.drawImage(src, (W - w) >> 1, (H - h) >> 1, w, h);
    } else {
      const s = Math.max(W / src.width, H / src.height), w = src.width * s, h = src.height * s;
      g.drawImage(src, (W - w) / 2, (H - h) / 2, w, h);
    }
    g.restore();
    return c;
  }

  // ------------------------------------------------------------------ pictures and documents
  // pages: functions giving each page's two levels (canvases: the screen's, then the zoom's or null)
  async function pictures(pages, doc, quality, progress, room = Infinity) {
    const t = new Table(), jpeg = new Jpeg(quality), recs = [];
    for (let i = 0; i < pages.length; i++) {
      const levels = (await pages[i]()).filter(Boolean), out = [];
      const tiles = levels.reduce((n, c) => n + Math.ceil(c.width / TILE) * Math.ceil(c.height / TILE), 0);
      let done = 0;
      for (const c of levels) {
        const px = pixels(c), offs = [];
        for (let ty = 0; ty < c.height; ty += TILE)
          for (let tx = 0; tx < c.width; tx += TILE) {
            const w = Math.min(TILE, c.width - tx), h = Math.min(TILE, c.height - ty);
            offs.push(t.add(await encodeTile(rgbOf(px, c.width, tx, ty, w, h), w, h, jpeg, doc), 1));
            const f = (i + ++done / tiles) / pages.length;
            if (done % 8 === 0) progress(f);
            if (t.n > room) {  // (too big: said as soon as it's known, with about how much)
              const e = new Error("doesn't fit");
              e.over = Math.round(t.n / f) - room;
              throw e;
            }
          }
        offs.push(t.n);
        out.push([c.width, c.height, t.u32s(offs)]);
      }
      const rec = new Uint8Array(4 + 8 * out.length), v = new DataView(rec.buffer);
      rec[0] = out.length, rec[1] = SHIFT;
      out.forEach(([w, h, at], k) => { v.setUint16(4 + 8 * k, w, true); v.setUint16(6 + 8 * k, h, true); v.setUint32(8 + 8 * k, at, true); });
      recs.push(t.add(rec));
    }
    return { t, kind: PICTURES, flags: doc ? DOCUMENT : 0, count: recs.length, jpeg: jpeg.headers, index: t.u32s(recs),
             backdrop: doc ? to565(0x2c, 0x2c, 0x2e) : 0 };
  }
  // the two levels of a picture w x h drawn by draw(w, h) (a canvas)
  function levelsOf(w, h, draw) {
    const [w0, h0] = fit(w, h, SW, SH);
    let big = null;
    if (w >= w0 * 1.5) big = w >= w0 * ZOOM ? [w0 * ZOOM, h0 * ZOOM] : [w, h];
    return async () => [await draw(w0, h0), big && await draw(big[0], big[1])];
  }
  // a picture's size without decoding it (JPEG, PNG, GIF, WebP), for those too big to decode whole
  function sizeOf(b) {
    const v = new DataView(b.buffer, b.byteOffset, b.byteLength);
    if (b[0] === 0x89 && b[1] === 0x50) return [v.getUint32(16), v.getUint32(20)];
    if (b[0] === 0x47 && b[1] === 0x49) return [v.getUint16(6, true), v.getUint16(8, true)];
    if (b[0] === 0xFF && b[1] === 0xD8)
      for (let i = 2; i + 9 < b.length;) {
        if (b[i] !== 0xFF) { i++; continue; }
        const m = b[i + 1], len = v.getUint16(i + 2);
        if (m >= 0xC0 && m <= 0xCF && m !== 0xC4 && m !== 0xC8 && m !== 0xCC) return [v.getUint16(i + 7), v.getUint16(i + 5)];
        i += 2 + len;
      }
    return null;
  }
  async function picture(file, progress, room) {
    const bytes = new Uint8Array(await file.arrayBuffer()), size = sizeOf(bytes);
    let bmp;
    try {
      if (size && size[0] * size[1] > 40e6) {  // (decoded straight to the zoom's size: a whole one wouldn't fit in memory)
        const [w0, h0] = fit(size[0], size[1], SW, SH);
        bmp = await createImageBitmap(file, { resizeWidth: w0 * ZOOM, resizeHeight: h0 * ZOOM, resizeQuality: 'high' });
      } else bmp = await createImageBitmap(file);
    } catch (e) {
      throw new Error(size && size[0] * size[1] > 250e6 ? "is too big for this browser to open" : "isn't a picture this browser opens");
    }
    const r = await pictures([levelsOf(bmp.width, bmp.height, (w, h) => drawn(bmp, w, h))], false, 82, progress, room);
    return { ...r, icon: iconOf(bmp, false), screen: screenOf(bmp, '#000'), tag: 'Picture' };
  }
  async function pdf(file, pdfjs, progress, room) {
    const lib = await pdfjs();
    const doc = await lib.getDocument({ data: new Uint8Array(await file.arrayBuffer()), isEvalSupported: false }).promise;
    const render = async (page, w, h) => {
      const vp1 = page.getViewport({ scale: 1 }), c = document.createElement('canvas');  // (pdf.js draws on a page's canvas)
      c.width = w, c.height = h;
      const g = c.getContext('2d', { willReadFrequently: true });
      g.fillStyle = '#fff';
      g.fillRect(0, 0, w, h);
      await page.render({ canvasContext: g, canvas: c, viewport: page.getViewport({ scale: w / vp1.width }), background: '#fff' }).promise;
      return c;
    };
    const pages = [];
    let first = null;
    for (let i = 1; i <= doc.numPages; i++)
      pages.push(async () => {
        const page = await doc.getPage(i), vp = page.getViewport({ scale: 1 });
        const lv = await levelsOf(vp.width * ZOOM * 4, vp.height * ZOOM * 4, (w, h) => render(page, w, h))();
        if (i === 1) first = lv[0];
        return lv;
      });
    const r = await pictures(pages, true, 82, progress, room);
    return { ...r, icon: iconOf(first, true), screen: screenOf(first, '#2c2c2e'),
             tag: doc.numPages === 1 ? 'PDF' : 'PDF, ' + doc.numPages + ' pages' };
  }

  // ------------------------------------------------------------------ video
  async function video(file, room, progress) {
    const v = document.createElement('video'), url = URL.createObjectURL(file);
    v.muted = true, v.playsInline = true, v.preload = 'auto', v.src = url;
    try {
      await new Promise((ok, no) => { v.onloadeddata = ok; v.onerror = () => no(new Error("isn't a video this browser plays")); });
      const [w, h] = fit(v.videoWidth, v.videoHeight, SW, SH).map((x) => Math.max(2, Math.round(x / 2) * 2));
      const tw = 32, th = 32, cols = Math.ceil(w / tw), rows = Math.ceil(h / th), key = VIDEO_FPS * 2;
      const n = Math.max(1, Math.floor(Math.min(v.duration || 0, VIDEO_MAX) * VIDEO_FPS));
      const c = canvas(w, h), g = c.getContext('2d', { willReadFrequently: true });
      const t = new Table(), jpeg = new Jpeg(55), sent = new Int16Array(w * h * 3), offs = [];
      let icon = null, best = -1;
      for (let f = 0; f < n; f++) {
        await new Promise((ok) => { v.onseeked = ok; v.currentTime = f / VIDEO_FPS + 0.001; });
        g.drawImage(v, 0, 0, w, h);
        const px = g.getImageData(0, 0, w, h).data;
        if (f % 10 === 0) {  // (the icon: its most colourful frame)
          let sc = 0;
          for (let i = 0; i < px.length; i += 64) sc += Math.max(px[i], px[i + 1], px[i + 2]) - Math.min(px[i], px[i + 1], px[i + 2]);
          if (sc > best) best = sc, icon = iconOf(c, false);
        }
        const whole = f % key === 0, bits = new Uint8Array(Math.ceil(cols * rows / 8)), parts = [];
        for (let i = 0; i < cols * rows; i++) {
          const x0 = (i % cols) * tw, y0 = Math.floor(i / cols) * th, ww = Math.min(tw, w - x0), hh = Math.min(th, h - y0);
          const rgb = rgbOf(px, w, x0, y0, ww, hh);
          if (!whole) {
            const d = new Uint32Array(256);
            let sum = 0;
            for (let y = 0; y < hh; y++)
              for (let x = 0; x < ww * 3; x++) {
                const e = Math.abs(rgb[y * ww * 3 + x] - sent[(y0 + y) * w * 3 + x0 * 3 + x]);
                d[Math.min(e, 255)]++, sum += e;
              }
            if (sum / (ww * hh * 3) < 2 && pct(d, ww * hh * 3, 98) < 24) continue;
          }
          const data = await encodeTile(rgb, ww, hh, jpeg, false);
          for (let y = 0; y < hh; y++) for (let x = 0; x < ww * 3; x++) sent[(y0 + y) * w * 3 + x0 * 3 + x] = rgb[y * ww * 3 + x];
          bits[i >> 3] |= 1 << (i & 7);
          const head = new Uint8Array(2);
          head[0] = data.length & 255, head[1] = data.length >> 8;
          parts.push(head, data);
        }
        const fr = new Uint8Array(bits.length + parts.reduce((s, p) => s + p.length, 0));
        fr.set(bits);
        parts.reduce((at, p) => (fr.set(p, at), at + p.length), bits.length);
        if (f && t.n + fr.length > room) break;  // (the rest wouldn't fit)
        offs.push(t.add(fr, 1));
        progress((f + 1) / n);
      }
      const count = offs.length;
      offs.push(t.n);
      const head = new Uint8Array(12), hv = new DataView(head.buffer);
      [w, h, tw, th, VIDEO_FPS * 100, key].forEach((x, k) => hv.setUint16(2 * k, x, true));
      const idx = new Uint8Array(12 + 4 * offs.length);
      idx.set(head);
      offs.forEach((o, k) => new DataView(idx.buffer).setUint32(12 + 4 * k, o, true));
      v.currentTime = 0;
      await new Promise((ok) => { v.onseeked = ok; });
      const first = drawn(v, w, h);
      const secs = Math.round(count / VIDEO_FPS);
      return { t, kind: VIDEO, flags: 0, count, jpeg: jpeg.headers, index: t.add(idx), backdrop: 0,
               icon: icon || iconOf(first, false), screen: screenOf(first, '#000'),
               tag: 'Video, ' + Math.floor(secs / 60) + ':' + String(secs % 60).padStart(2, '0') };
    } finally { URL.revokeObjectURL(url); }
  }

  // ------------------------------------------------------------------ books
  // the letters of NumView's reading fonts (games/numview/tools/readfont.py's READ_EXTRA, and ASCII)
  const KNOWN = new Set(range(32, 127).concat(range(0xA1, 0x100).filter((c) => c !== 0xAD),
    [0x2018, 0x2019, 0x201C, 0x201D, 0x2013, 0x2014, 0x2026, 0x2022, 0x2122, 0x20AC, 0x0152, 0x0153, 0x2032, 0x2033]));
  function letters(s) {
    let out = '';
    for (const ch of s) {
      const o = ch.codePointAt(0);
      if (KNOWN.has(o) || ch === '\n' || ch === '\x01' || ch === '\x02') out += ch;
      else if ('      \t'.includes(ch)) out += ' ';
      else if ('‐‑‒−'.includes(ch)) out += '-';
      else for (const c of ch.normalize('NFKD')) if (KNOWN.has(c.codePointAt(0))) out += c;
    }
    return out;
  }
  // a .zip's files: name -> () => its bytes
  function unzip(b) {
    const v = new DataView(b.buffer, b.byteOffset, b.byteLength), files = new Map();
    let e = b.length - 22;
    while (e >= 0 && v.getUint32(e, true) !== 0x06054b50) e--;
    if (e < 0) throw new Error("isn't a book NumView can read");
    let p = v.getUint32(e + 16, true);
    for (let k = v.getUint16(e + 10, true); k > 0 && v.getUint32(p, true) === 0x02014b50; k--) {
      const method = v.getUint16(p + 10, true), csize = v.getUint32(p + 20, true), nlen = v.getUint16(p + 28, true);
      const name = new TextDecoder().decode(b.subarray(p + 46, p + 46 + nlen)), local = v.getUint32(p + 42, true);
      const at = local + 30 + v.getUint16(local + 26, true) + v.getUint16(local + 28, true), data = b.subarray(at, at + csize);
      files.set(name, () => (method === 8 ? inflate(data) : Promise.resolve(data)));
      p += 46 + nlen + v.getUint16(p + 30, true) + v.getUint16(p + 32, true);
    }
    return files;
  }
  const BLOCKS = new Set(['p', 'div', 'li', 'blockquote', 'tr', 'dt', 'dd', 'section', 'article', 'figure', 'figcaption',
    'pre', 'table', 'ul', 'ol', 'body', 'br', 'hr']);
  const HEADS = new Set(['h1', 'h2', 'h3', 'h4', 'h5', 'h6']);
  function paragraphs(doc) {
    const paras = [];
    let cur = '', head = false;
    const flush = () => { const t = cur.replace(/\s+/g, ' ').trim(); if (t) paras.push((head ? '\x01' : '') + t); cur = '', head = false; };
    const walk = (n) => {
      if (n.nodeType === 3) { cur += n.data; return; }
      if (n.nodeType !== 1) return;
      const tag = n.localName.toLowerCase();
      if (['script', 'style', 'head', 'title'].includes(tag)) return;
      if (HEADS.has(tag)) flush(), head = true;
      else if (BLOCKS.has(tag)) flush();
      if (tag === 'li') cur += '• ';
      for (const c of n.childNodes) walk(c);
      if (HEADS.has(tag) || BLOCKS.has(tag)) flush();
    };
    walk(doc.documentElement);
    flush();
    return paras;
  }
  const xml = (text, type) => {
    const d = new DOMParser().parseFromString(text, type);
    return d.getElementsByTagName('parsererror').length ? new DOMParser().parseFromString(text, 'text/html') : d;
  };
  async function epub(bytes) {
    const z = unzip(bytes), text = async (n) => new TextDecoder().decode(await z.get(n)());
    const join = (base, href) => { const parts = (base ? base + '/' + href : href).split('/'), out = []; for (const p of parts) p === '..' ? out.pop() : p !== '.' && out.push(p); return out.join('/'); };
    const opfPath = xml(await text('META-INF/container.xml'), 'application/xml').querySelector('rootfile').getAttribute('full-path');
    const opf = xml(await text(opfPath), 'application/xml'), base = opfPath.includes('/') ? opfPath.replace(/\/[^/]*$/, '') : '';
    const items = new Map([...opf.getElementsByTagName('item')].map((i) => [i.getAttribute('id'), i]));
    const title = (opf.getElementsByTagName('dc:title')[0] || opf.getElementsByTagName('title')[0] || {}).textContent || '';
    const meta = [...opf.getElementsByTagName('meta')].find((m) => m.getAttribute('name') === 'cover');
    const coverItem = (meta && items.get(meta.getAttribute('content'))) ||
      [...items.values()].find((i) => /cover-image/.test(i.getAttribute('properties') || '')) ||
      [...items.values()].find((i) => /cover/i.test(i.getAttribute('id')) && /^image\//.test(i.getAttribute('media-type') || ''));
    let cover = null;
    if (coverItem) try { cover = await createImageBitmap(new Blob([await z.get(join(base, coverItem.getAttribute('href')))()])); } catch (e) { cover = null; }
    const parts = [];
    for (const ref of opf.getElementsByTagName('itemref')) {
      const it = items.get(ref.getAttribute('idref')), href = it && join(base, decodeURIComponent(it.getAttribute('href')));
      if (!it || !z.has(href)) continue;
      const p = paragraphs(xml(await text(href), 'application/xhtml+xml'));
      if (p.length) parts.push(p);
    }
    return { title: title.trim(), parts, cover };
  }
  function plain(text) {
    const parts = [[]];
    for (const block of text.replace(/\r/g, '').split(/\n\s*\n/)) {
      const b = block.replace(/\s+/g, ' ').trim();
      if (b) parts[0].push(b);
    }
    return { title: '', parts, cover: null };
  }
  // a book with no cover: its title on dark paper, in Nunito
  function titleIcon(title) {
    const S = 4, c = canvas(55 * S, 56 * S), g = c.getContext('2d');
    g.fillStyle = '#23211e';
    g.fillRect(0, 0, c.width, c.height);
    g.fillStyle = '#faf8f3';
    g.font = '800 ' + 11 * S + 'px Nunito, sans-serif';
    g.textBaseline = 'top';
    const lines = [];
    for (const w of title.split(/\s+/).filter(Boolean)) {
      if (lines.length && g.measureText(lines[lines.length - 1] + ' ' + w).width <= 45 * S) lines[lines.length - 1] += ' ' + w;
      else lines.push(w);
    }
    let y = (56 * S - Math.min(3, lines.length) * 13 * S) / 2;
    for (const l of lines.slice(0, 3)) g.fillText(l, 6 * S, y), y += 13 * S;
    g.fillStyle = '#a8a49c';
    g.fillRect(6 * S, 47 * S, 12 * S, S);
    return iconOf(c, false);
  }
  // the book's first page, about as NumView sets it (its fonts are Nunito too)
  function bookScreen(text) {
    const c = canvas(SW, SH), g = c.getContext('2d');
    g.fillStyle = '#faf8f3';
    g.fillRect(0, 0, SW, SH);
    g.fillStyle = '#23211e';
    let y = 14;
    for (const para of text.split('\x02')[0].split('\n')) {
      if (!para) continue;
      const head = para[0] === '\x01', lh = head ? 21 : 18;
      g.font = (head ? '850 17px' : '560 14px') + ' Nunito, sans-serif';
      let line = '';
      const out = [];
      for (const w of para.replace('\x01', '').split(' ')) {
        if (line && g.measureText(line + ' ' + w).width > SW - 40) out.push(line), line = w;
        else line = line ? line + ' ' + w : w;
      }
      out.push(line);
      for (const l of out) {
        if (y + lh > 214) return c;
        g.fillText(l, 20, y + lh - 5);
        y += lh;
      }
      y += 6;
    }
    return c;
  }
  async function book(file, progress) {
    const bytes = new Uint8Array(await file.arrayBuffer());
    const b = extOf(file.name) === 'epub' ? await epub(bytes) : plain(new TextDecoder().decode(bytes));
    const text = letters(b.parts.map((p) => p.join('\n') + '\n').join('\x02')), data = new TextEncoder().encode(text);
    if (!data.length) throw new Error('has no text NumView can show');
    const t = new Table(), starts = [], chunks = [];
    for (let i = 0; i < data.length;) {
      let e = Math.min(data.length, i + CHUNK);
      if (e < data.length) {
        let cut = e - 1;
        while (cut > i && data[cut] !== 32 && data[cut] !== 10) cut--;
        if (cut > i) e = cut + 1;
      }
      starts.push(i);
      chunks.push(t.add(await deflate(data.subarray(i, e)), 1));
      progress(e / data.length);
      i = e;
    }
    starts.push(data.length), chunks.push(t.n);
    const idx = new Uint8Array(8 + 8 * starts.length), v = new DataView(idx.buffer);
    v.setUint32(0, data.length, true); v.setUint16(4, starts.length - 1, true);
    starts.forEach((s, k) => v.setUint32(8 + 4 * k, s, true));
    chunks.forEach((s, k) => v.setUint32(8 + 4 * (starts.length + k), s, true));
    return { t, kind: BOOK, flags: 0, count: data.length, jpeg: null, index: t.add(idx), backdrop: 0, title: b.title,
             icon: b.cover ? iconOf(b.cover, false) : null, screen: bookScreen(text), tag: 'Book' };
  }

  // ------------------------------------------------------------------
  // The app's name: the file's (a book's title when it's short), in ASCII as NumPlay's apps are, 22 letters at most
  function nameOf(file, title) {
    const ascii = (s) => s.normalize('NFKD').replace(/[^\x20-\x7e]/g, '').replace(/[_]+/g, ' ').replace(/\s+/g, ' ').trim();
    let n = title && ascii(title).length <= 22 ? ascii(title) : ascii(file.name.replace(/\.[^.]+$/, ''));
    if (n.length > 22) n = n.slice(0, 22).replace(/\s+\S*$/, '') || n.slice(0, 22);
    return n || 'File';
  }
  // file -> { viewer, name, tag, table, icon (55 x 56 canvas), screen (320 x 240 canvas) }. opts: progress(0..1),
  // room (bytes the table may take: a video stops there, a picture or a PDF throws "doesn't fit" with .over, about
  // how much more it needs), pdfjs (() => pdf.js's module)
  async function pack(file, opts = {}) {
    const kind = kindOf(file.name), progress = opts.progress || (() => {});
    if (!kind) throw new Error("isn't a file NumView shows");
    const room = opts.room || Infinity;
    const r = kind === 'picture' ? await picture(file, progress, room) : kind === 'document' ? await pdf(file, opts.pdfjs, progress, room)
      : kind === 'video' ? await video(file, Math.min(room, 4e6), progress) : await book(file, progress);
    const name = nameOf(file, r.title);
    return { viewer: VIEWER[kind], name, tag: r.tag, icon: r.icon || titleIcon(name), screen: r.screen,
             table: r.t.finish(r.kind, r.flags, r.count, r.jpeg, r.index, r.backdrop, name) };
  }

  root.npMedia = { EXTS, kindOf, pack, Jpeg, encodeTile, Table, letters, unzip, sizeOf };
})(typeof window !== 'undefined' ? window : globalThis);
