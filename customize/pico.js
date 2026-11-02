/* NumPlay Studio: PICO-8 carts (.p8 and .p8.png) for NumPico. A cart becomes its 0x4300 bytes of ROM, its Lua code
 * (unpacked, in P8SCII) and its label, and the carts a table, np_carts: the same bytes as
 * games/numpico/tools/p8cart.py makes (games/numpico/tests/test_pico_js.py), where the format is told. The packed
 * code of a .p8.png is unpacked with Lexaloffle's algorithms (p8_compress.c, zlib licence; pxa_compress_snippets.c,
 * MIT licence), a .p8's memory read as femto8's parser does (MIT licence).
 * Plain functions of bytes, for app.js in the browser and the test in Node. */
'use strict';
(function (root) {
  const MAGIC = 0x3850504E, VERSION = 1, ENTRY = 64, NAME = 48, ROM = 0x4300;
  // P8SCII as .p8 files write it
  const P8SCII = {
    1: '¹', 2: '²', 3: '³', 4: '⁴', 5: '⁵', 6: '⁶', 7: '⁷', 8: '⁸',
    11: 'ᵇ', 12: 'ᶜ', 14: 'ᵉ', 15: 'ᶠ', 16: '▮', 17: '■', 18: '□',
    19: '⁙', 20: '⁘', 21: '‖', 22: '◀', 23: '▶', 24: '「', 25: '」',
    26: '¥', 27: '•', 28: '、', 29: '。', 30: '゛', 31: '゜', 127: '○',
    128: '█', 129: '▒', 130: '\u{1f431}', 131: '⬇️', 132: '░', 133: '✽',
    134: '●', 135: '♥', 136: '☉', 137: '웃', 138: '⌂', 139: '⬅️',
    140: '\u{1f610}', 141: '♪', 142: '\u{1f17e}️', 143: '◆', 144: '…',
    145: '➡️', 146: '★', 147: '⧗', 148: '⬆️', 149: 'ˇ', 150: '∧',
    151: '❎', 152: '▤', 153: '▥',
  };
  [...'あいうえおかきくけこさしすせそたちつてとなにぬねのはひふへほまみむめもやゆよらりるれろわをんっゃゅょアイウエオカキクケコサシスセソタチツテトナニヌネノハヒフヘホマミムメモヤユヨラリルレロワヲンッャュョ◜◝']
    .forEach((c, i) => { P8SCII[154 + i] = c; });
  const enc = new TextEncoder();
  const SYMBOLS = Object.entries(P8SCII).map(([k, v]) => [enc.encode(v), +k]).sort((a, b) => b[0].length - a[0].length);
  const COLORS = [[0, 0, 0], [29, 43, 83], [126, 37, 83], [0, 135, 81], [171, 82, 54], [95, 87, 79], [194, 195, 199],
    [255, 241, 232], [255, 0, 77], [255, 163, 0], [255, 236, 39], [0, 228, 54], [41, 173, 255], [131, 118, 156],
    [255, 119, 168], [255, 204, 170], [41, 24, 20], [17, 29, 53], [66, 33, 54], [18, 83, 89], [116, 47, 41],
    [73, 51, 59], [162, 136, 121], [243, 239, 125], [190, 18, 80], [255, 108, 36], [168, 231, 46], [0, 181, 67],
    [6, 90, 181], [117, 70, 101], [255, 110, 89], [255, 157, 129]];
  const HEX = '0123456789abcdef', LABEL_DIGITS = '0123456789abcdefghijklmnopqrstuv';

  const startsWith = (b, s, i) => { if (i + s.length > b.length) return false; for (let k = 0; k < s.length; k++) if (b[i + k] !== s[k]) return false; return true; };
  function utf8Len(b, i) {
    const c = b[i], n = (c & 0xE0) === 0xC0 ? 2 : (c & 0xF0) === 0xE0 ? 3 : (c & 0xF8) === 0xF0 ? 4 : 0;
    if (!n || i + n > b.length) return 0;
    for (let k = 1; k < n; k++) if ((b[i + k] & 0xC0) !== 0x80) return 0;
    return n;
  }
  // the code of a .p8 in P8SCII: its UTF-8 symbols to their letters, other UTF-8 letters left out, other bytes kept
  function toP8scii(b) {
    const out = [];
    for (let i = 0; i < b.length;) {
      const c = b[i];
      if (c < 0x80) { out.push(c); i++; continue; }
      const s = SYMBOLS.find(([u]) => startsWith(b, u, i));
      if (s) { out.push(s[1]); i += s[0].length; continue; }
      const n = utf8Len(b, i);
      if (n) i += n; else { out.push(c); i++; }
    }
    return Uint8Array.from(out);
  }

  // ------------------------------------------------------------------ .p8
  function hexBytes(line) {
    const h = [...line].filter((c) => HEX.includes(c.toLowerCase()));
    const out = [];
    for (let k = 0; k + 1 < h.length; k += 2) out.push(parseInt(h[k] + h[k + 1], 16));
    return out;
  }
  function sfxBytes(b) {
    const out = [];
    for (let i = 0; i < 32; i += 2) {
      const at = 4 + i * 5 / 2, [a, bb, c, d, e] = b.slice(at, at + 5);
      for (const [pitch, wave, vol, fx] of [[a, bb >> 4, bb & 15, c >> 4], [((c << 4) | (d >> 4)) & 0xFF, d & 15, e >> 4, e & 15]]) {
        const v = (wave > 7 ? 0x8000 : 0) | (fx << 12) | (vol << 9) | ((wave & 7) << 6) | (pitch & 63);
        out.push(v & 0xFF, v >> 8);
      }
    }
    return out.concat(b.slice(0, 4));
  }
  const TAGS = ['__lua__', '__gfx__', '__gff__', '__label__', '__map__', '__sfx__', '__music__', '__gfx8__', '__meta__', '__change_mask__'];
  function parseP8(data) {
    const raw = [];
    for (let s = 0, i = 0; i <= data.length; i++) if (i === data.length || data[i] === 10) { raw.push(data.subarray(s, i)); s = i + 1; }
    const lines = raw.map((r) => String.fromCharCode(...r));
    if (!lines[0].startsWith('pico-8 cartridge')) throw new Error("isn't a PICO-8 cart");
    const rom = new Uint8Array(ROM), code = [];
    let label = null, section = null, n = 0, music = 0;
    for (let li = 1; li < lines.length; li++) {
      const tag = lines[li].replace(/\r+$/, '');
      if (TAGS.includes(tag) || (tag.startsWith('__') && tag.endsWith('__') && tag.length > 4 && section !== '__lua__')) {
        section = tag; n = 0;
        if (tag === '__label__') label = new Array(128 * 128).fill(0);
        continue;
      }
      if (section === null) continue;
      if (section === '__lua__') code.push(raw[li]);
      else if (section === '__gfx__') {
        const b = hexBytes(tag), at = n * 64;
        b.slice(0, 64).forEach((v, k) => { if (at + k < 0x2000) rom[at + k] = ((v << 4) | (v >> 4)) & 0xFF; });
        if (b.length) n++;
      } else if (section === '__gff__') {
        const b = hexBytes(tag), at = 0x3000 + n * 128;
        b.slice(0, 128).forEach((v, k) => { if (at + k < 0x3100) rom[at + k] = v; });
        if (b.length) n++;
      } else if (section === '__map__') {
        const b = hexBytes(tag), at = 0x2000 + n * 128;
        b.slice(0, 128).forEach((v, k) => { if (at + k < 0x3000) rom[at + k] = v; });
        if (b.length) n++;
      } else if (section === '__sfx__') {
        const b = hexBytes(tag);
        if (b.length >= 84 && n < 64) { rom.set(sfxBytes(b), 0x3200 + n * 68); n++; }
      } else if (section === '__music__') {
        const b = hexBytes(tag);
        if (b.length >= 5 && n < 64) {
          for (let k = 0; k < 4; k++) rom[0x3100 + n * 4 + k] = (b[1 + k] & 0x7F) | (b[0] & (1 << k) ? 0x80 : 0);
          n++; music = n;
        }
      } else if (section === '__label__' && label) {
        const row = [...tag].filter((c) => LABEL_DIGITS.includes(c));
        if (row.length && n < 128) { row.slice(0, 128).forEach((c, x) => { label[n * 128 + x] = LABEL_DIGITS.indexOf(c); }); n++; }
      }
    }
    for (let at = 0x3100 + music * 4; at < 0x3200; at += 4) rom.set([0x41, 0x42, 0x43, 0x44], at);
    const joined = new Uint8Array(code.reduce((a, c) => a + c.length + 1, 0) - (code.length ? 1 : 0));
    let at = 0;
    code.forEach((c, i) => { if (i) joined[at++] = 10; joined.set(c, at); at += c.length; });
    return { rom, code: toP8scii(joined), label };
  }

  // ------------------------------------------------------------------ .p8.png
  async function inflate(bytes) {
    const s = new Blob([bytes]).stream().pipeThrough(new DecompressionStream('deflate'));
    return new Uint8Array(await new Response(s).arrayBuffer());
  }
  async function pngRGBA(data) {
    const v = new DataView(data.buffer, data.byteOffset, data.byteLength);
    const sig = [137, 80, 78, 71, 13, 10, 26, 10];
    if (data.length < 8 || sig.some((x, i) => data[i] !== x)) throw new Error("isn't a PNG");
    let at = 8, w = 0, h = 0, ctype = 0;
    const idat = [];
    while (at + 8 <= data.length) {
      const n = v.getUint32(at), kind = String.fromCharCode(...data.subarray(at + 4, at + 8));
      const body = data.subarray(at + 8, at + 8 + n);
      if (kind === 'IHDR') {
        w = v.getUint32(at + 8); h = v.getUint32(at + 12); ctype = body[9];
        if (body[8] !== 8 || (ctype !== 2 && ctype !== 6) || body[12]) throw new Error("is a PNG PICO-8 didn't make");
      } else if (kind === 'IDAT') idat.push(body);
      else if (kind === 'IEND') break;
      at += 12 + n;
    }
    if (!w) throw new Error("isn't a PNG");
    const bpp = ctype === 6 ? 4 : 3, stride = w * bpp;
    const all = new Uint8Array(idat.reduce((a, b) => a + b.length, 0));
    let o = 0;
    for (const b of idat) { all.set(b, o); o += b.length; }
    const raw = await inflate(all), out = new Uint8Array(w * h * 4);
    let prev = new Uint8Array(stride);
    for (let y = 0; y < h; y++) {
      const f = raw[y * (stride + 1)], line = raw.slice(y * (stride + 1) + 1, (y + 1) * (stride + 1));
      for (let x = 0; x < stride; x++) {
        const a = x >= bpp ? line[x - bpp] : 0, b = prev[x], c = x >= bpp ? prev[x - bpp] : 0;
        if (f === 1) line[x] = (line[x] + a) & 0xFF;
        else if (f === 2) line[x] = (line[x] + b) & 0xFF;
        else if (f === 3) line[x] = (line[x] + ((a + b) >> 1)) & 0xFF;
        else if (f === 4) {
          const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c);
          line[x] = (line[x] + (pa <= pb && pa <= pc ? a : pb <= pc ? b : c)) & 0xFF;
        }
      }
      for (let x = 0; x < w; x++) {
        out.set(line.subarray(x * bpp, x * bpp + bpp), (y * w + x) * 4);
        if (bpp === 3) out[(y * w + x) * 4 + 3] = 255;
      }
      prev = line;
    }
    return { w, h, px: out };
  }

  const LITERALS = enc.encode('^\n 0123456789abcdefghijklmnopqrstuvwxyz!#%(){}[]<>+=/*:;.,~_');
  const FUTURE = ['if(_update60)_update=function()_update60()_update60()end',
    'if(_update60)_update=function()_update60()_update_buttons()_update60()end'].map((s) => enc.encode(s));
  const untilZero = (b) => { const z = b.indexOf(0); return z < 0 ? b : b.subarray(0, z); };
  // PICO-8's first packing (":c:"): Lexaloffle's decompress_mini
  function unpackOld(d) {
    const n = d[4] * 256 + d[5], out = [];
    let at = 8;
    while (out.length < n && at < d.length) {
      const v = d[at++];
      if (v < LITERALS.length) out.push(v === 0 ? d[at++] : LITERALS[v]);
      else {
        const off = (v - LITERALS.length) * 16 + d[at] % 16, len = (d[at] >> 4) + 2;
        at++;
        for (let k = 0; k < len; k++) out.push(off > 0 && off <= out.length ? out[out.length - off] : 0);
      }
    }
    let r = untilZero(Uint8Array.from(out.slice(0, n)));
    for (const f of FUTURE) if (r.length >= f.length && startsWith(r, f, r.length - f.length)) r = r.subarray(0, r.length - f.length);
    return r;
  }
  // PICO-8's 0.2.0 packing ("pxa"): Lexaloffle's pxa_decompress
  function unpackPxa(d) {
    const rawLen = d[4] * 256 + d[5], compLen = d[6] * 256 + d[7];
    let pos = 8, bitv = 1;
    const bit = () => {
      const r = pos < d.length && d[pos] & bitv ? 1 : 0;
      bitv <<= 1;
      if (bitv === 256) { bitv = 1; pos++; }
      return r;
    };
    const val = (bits) => { let v = 0; for (let i = 0; i < bits; i++) if (bit()) v |= 1 << i; return v; };
    const chain = (link, most) => {
      const top = (1 << link) - 1;
      let v = 0, read = 0, vv = top;
      while (vv === top) { vv = val(link); read += link; v += vv; if (read >= most) return v; }
      return v;
    };
    const lit = Array.from({ length: 256 }, (_, i) => i), out = [];
    while (pos < compLen && out.length < rawLen) {
      if (bit() === 0) {
        const bits = (3 - chain(1, 2)) * 5, num = val(bits);
        const off = (num === 0 && bits === 10 ? -1 : num) + 1;
        if (off === 0) {
          while (out.length < rawLen) { const c = val(8); if (c === 0) break; out.push(c); }
        } else {
          const len = chain(3, 100000) + 3;
          for (let k = 0; k < len; k++) out.push(off <= out.length ? out[out.length - off] : 0);
        }
      } else {
        let lpos = 0, bits = 0, safety = 0;
        while (bit() === 1 && safety < 16) { safety++; lpos += 1 << (4 + bits); bits++; }
        lpos += val(bits + 4);
        if (lpos > 255) throw new Error('has damaged code');
        const c = lit[lpos];
        out.push(c);
        lit.splice(lpos, 1);
        lit.unshift(c);
      }
    }
    return untilZero(Uint8Array.from(out.slice(0, rawLen)));
  }
  async function parsePng(data) {
    const { w, h, px } = await pngRGBA(data);
    if (w !== 160 || h !== 205) throw new Error("is a PNG, not a PICO-8 cart");
    const b = new Uint8Array(w * h);
    for (let i = 0; i < b.length; i++) b[i] = ((px[4 * i + 3] & 3) << 6) | ((px[4 * i] & 3) << 4) | ((px[4 * i + 1] & 3) << 2) | (px[4 * i + 2] & 3);
    const rom = b.slice(0, ROM), c = b.subarray(ROM, 0x8000);
    const code = c[0] === 58 && c[1] === 99 && c[2] === 58 && c[3] === 0 ? unpackOld(c)
      : c[0] === 0 && c[1] === 112 && c[2] === 120 && c[3] === 97 ? unpackPxa(c) : untilZero(c).slice();
    // the label: each pixel the colour it is (the low 2 bits held data), else the closest
    const exact = new Map();
    for (let k = 31; k >= 0; k--) exact.set((COLORS[k][0] & 0xFC) << 16 | (COLORS[k][1] & 0xFC) << 8 | (COLORS[k][2] & 0xFC), k);
    const label = [];
    for (let y = 0; y < 128; y++)
      for (let x = 0; x < 128; x++) {
        const i = ((24 + y) * 160 + 16 + x) * 4, rgb = [px[i] & 0xFC, px[i + 1] & 0xFC, px[i + 2] & 0xFC];
        const key = rgb[0] << 16 | rgb[1] << 8 | rgb[2];
        if (!exact.has(key)) exact.set(key, closest(rgb, [...Array(32).keys()]));
        label.push(exact.get(key));
      }
    return { rom, code: code instanceof Uint8Array ? code : Uint8Array.from(code), label };
  }
  // the closest of `among` (PICO-8 colours) to rgb: the sum of squared differences; ties, the first
  function closest(rgb, among) {
    let best = 0, bd = Infinity;
    among.forEach((k, j) => {
      const d = (COLORS[k][0] - rgb[0]) ** 2 + (COLORS[k][1] - rgb[1]) ** 2 + (COLORS[k][2] - rgb[2]) ** 2;
      if (d < bd) { bd = d; best = j; }
    });
    return among === null ? best : best;
  }

  // ------------------------------------------------------------------ the table
  function labelBytes(label) {
    const use = new Array(32).fill(0);
    for (const c of label) use[c]++;
    let keep = [...Array(32).keys()].sort((a, b) => use[b] - use[a] || a - b).slice(0, 16).filter((c) => use[c]);
    if (!keep.length) keep = [0];
    while (keep.length < 16) keep.push(0);
    const near = [];
    for (let c = 0; c < 32; c++) {
      const i = keep.indexOf(c);
      near[c] = i >= 0 ? i : closest(COLORS[c], keep);
    }
    const out = new Uint8Array(16 + 128 * 64);
    keep.forEach((c, i) => { out[i] = (c & 15) | (c >= 16 ? 0x80 : 0); });
    for (let i = 0; i < 128 * 128; i += 2) out[16 + i / 2] = near[label[i]] | near[label[i + 1]] << 4;
    return out;
  }
  // a cart's name: its file's without .p8.png or .p8, at most 48 bytes of UTF-8, no letter cut
  function cartName(file) {
    let base = file.replace(/^.*[\\/]/, '');
    for (const ext of ['.p8.png', '.p8']) if (base.toLowerCase().endsWith(ext)) { base = base.slice(0, -ext.length); break; }
    const b = enc.encode(base);
    let k = Math.min(b.length, NAME);
    while (k > 0 && k < b.length && (b[k] & 0xC0) === 0x80) k--;
    return b.slice(0, k);
  }
  async function readCart(bytes, file) {
    const png = bytes.length >= 8 && bytes[0] === 137 && bytes[1] === 80 && bytes[2] === 78 && bytes[3] === 71;
    const c = png ? await parsePng(bytes) : parseP8(bytes);
    return { name: cartName(file), rom: c.rom, code: c.code, label: c.label ? labelBytes(c.label) : null };
  }
  // the parts' bytes in the table (for its size)
  const align4 = (n) => (n + 3) & ~3;
  const cartBytes = (c) => ENTRY + align4(ROM) + align4(c.code.length) + (c.label ? align4(c.label.length) : 0);
  function cartTable(carts) {
    let size = 12 + ENTRY * carts.length;
    const offs = carts.map((c) => {
      const o = [];
      for (const part of [c.rom, c.code, c.label]) {
        if (!part) { o.push(0); continue; }
        size = align4(size); o.push(size); size += part.length;
      }
      return o;
    });
    const t = new Uint8Array(align4(size)), v = new DataView(t.buffer);
    v.setUint32(0, MAGIC, true); v.setUint32(4, VERSION, true); v.setUint32(8, carts.length, true);
    carts.forEach((c, i) => {
      const e = 12 + ENTRY * i;
      v.setUint32(e, offs[i][0], true); v.setUint32(e + 4, offs[i][1], true); v.setUint32(e + 8, c.code.length, true);
      v.setUint32(e + 12, offs[i][2], true);
      t.set(c.name.subarray(0, NAME), e + 16);
      [c.rom, c.code, c.label].forEach((p, k) => { if (p) t.set(p, offs[i][k]); });
    });
    return t;
  }
  // a cart's label as RGBA, for a picture of it
  function labelRGBA(label) {
    const out = new Uint8ClampedArray(128 * 128 * 4);
    for (let i = 0; i < 128 * 128; i++) {
      const b = label[16 + (i >> 1)], k = label[(i & 1 ? b >> 4 : b & 15)], c = COLORS[(k & 15) | (k & 0x80 ? 16 : 0)];
      out.set([c[0], c[1], c[2], 255], 4 * i);
    }
    return out;
  }

  const api = { readCart, cartTable, cartBytes, labelRGBA, MAGIC };
  if (typeof module === 'object' && module.exports) module.exports = api;  // (Node, for the test)
  else root.npPico = api;
})(this);
