/* NumPlay Studio: your own .nwa apps as games of NumPlay. An app's .nwa goes the way a big game's does in the
 * Makefile: `ld -r -T tools/module.ld` and objcopy's renames (app.js runs them), then tools/npmodule.py, which
 * npmodule() is (the same bytes: tests/test_apps.py). Then it gets what tools/gen_games.py gives a game: its entry in
 * the launcher's table and its marks (entryObject), its block and its place in the arena in the link script
 * (app.js), and a card for the home screen, its icon on its own colours (card).
 * Plain functions of bytes, for app.js in the browser and the test in Node. */
'use strict';
(function (root) {
  const SHT_PROGBITS = 1, SHT_SYMTAB = 2, SHT_STRTAB = 3, SHT_RELA = 4, SHT_NOBITS = 8, SHT_REL = 9;
  const SHT_ARM_ATTRIBUTES = 0x70000003;
  const SHF_ALLOC = 2, SHF_EXECINSTR = 4, SHF_INFO_LINK = 0x40;
  const STB_LOCAL = 0, STB_GLOBAL = 1;
  const STT_NOTYPE = 0, STT_OBJECT = 1, STT_SECTION = 3, STT_FILE = 4;
  const SHN_UNDEF = 0, SHN_ABS = 0xFFF1;
  const R_ARM_ABS32 = 2;
  const HEAD = 0x3147504E, TAIL = 0x444E4550;  // (the marks around a game's block, as gen_games.py's)
  const SHOT_W = 192, SHOT_H = 144;             // (launcher/src/ui.h)
  const ICON_W = 55, ICON_H = 56;

  const cstr = (b, o) => { let e = o; while (b[e]) e++; return new TextDecoder().decode(b.subarray(o, e)); };
  const pad = (n, a) => (n + a - 1) & ~(a - 1);

  // ------------------------------------------------------------------ reading an object (npmodule.py's Elf)
  function readElf(b) {
    if (b.length < 52 || b[0] !== 0x7f || b[1] !== 0x45 || b[2] !== 0x4c || b[3] !== 0x46 || b[4] !== 1 || b[5] !== 1)
      throw new Error("isn't an app for the calculator");
    const v = new DataView(b.buffer, b.byteOffset, b.byteLength);
    if (v.getUint16(16, true) !== 1 || v.getUint16(18, true) !== 40) throw new Error("isn't an app for the calculator");
    const e = { flags: v.getUint32(0x24, true), secs: [], syms: [] };
    const shoff = v.getUint32(0x20, true), shentsize = v.getUint16(0x2e, true);
    const shnum = v.getUint16(0x30, true), shstrndx = v.getUint16(0x32, true);
    for (let i = 0; i < shnum; i++) {
      const f = [];
      for (let k = 0; k < 10; k++) f.push(v.getUint32(shoff + i * shentsize + 4 * k, true));
      const [name, type, flags, , off, size, link, info, align, entsize] = f;
      e.secs.push({ name, type, flags, off, size, link, info, align, entsize,
                    data: type === SHT_NOBITS ? new Uint8Array(0) : b.subarray(off, off + size) });
    }
    const shstr = e.secs[shstrndx].data;
    for (const s of e.secs) s.sname = cstr(shstr, s.name);
    const tabs = e.secs.map((s, i) => i).filter((i) => e.secs[i].type === SHT_SYMTAB);
    if (tabs.length !== 1) throw new Error('has no symbol table');
    const st = e.secs[tabs[0]], strtab = e.secs[st.link].data;
    const sv = new DataView(st.data.buffer, st.data.byteOffset, st.data.byteLength);
    for (let k = 0; k < st.size / 16; k++) {
      const info = sv.getUint8(16 * k + 12);
      e.syms.push({ name: cstr(strtab, sv.getUint32(16 * k, true)), value: sv.getUint32(16 * k + 4, true),
                    size: sv.getUint32(16 * k + 8, true), bind: info >> 4, type: info & 15,
                    other: sv.getUint8(16 * k + 13), shndx: sv.getUint16(16 * k + 14, true) });
    }
    return e;
  }
  function relsFor(e, target) {
    const out = [];
    for (const s of e.secs) {
      if (s.type === SHT_REL && s.info === target) {
        const v = new DataView(s.data.buffer, s.data.byteOffset, s.data.byteLength);
        for (let k = 0; k < s.size / 8; k++) {
          const info = v.getUint32(8 * k + 4, true);
          out.push([v.getUint32(8 * k, true), info & 255, info >>> 8]);
        }
      } else if (s.type === SHT_RELA && s.info === target) throw new Error('has relocations NumPlay can\'t place');
    }
    return out;
  }

  // ------------------------------------------------------------------ writing one (npmodule.py's layout)
  // syms: [{ name, value, size, bind, type, other, shndx }], the null one first
  function symbols(syms) {
    const enc = new TextEncoder(), str = [0], at = new Map();
    const add = (s) => {
      if (!s) return 0;
      if (!at.has(s)) { at.set(s, str.length); str.push(...enc.encode(s), 0); }
      return at.get(s);
    };
    const tab = new Uint8Array(16 * syms.length), v = new DataView(tab.buffer);
    syms.forEach((s, i) => {
      v.setUint32(16 * i, add(s.name), true);
      v.setUint32(16 * i + 4, s.value >>> 0, true);
      v.setUint32(16 * i + 8, s.size >>> 0, true);
      tab[16 * i + 12] = (s.bind << 4) | s.type;
      tab[16 * i + 13] = s.other;
      v.setUint16(16 * i + 14, s.shndx, true);
    });
    return { symtab: tab, strtab: Uint8Array.from(str) };
  }
  const relBytes = (rels) => {
    const d = new Uint8Array(8 * rels.length), v = new DataView(d.buffer);
    rels.forEach(([off, typ, si], k) => { v.setUint32(8 * k, off, true); v.setUint32(8 * k + 4, ((si << 8) | typ) >>> 0, true); });
    return d;
  };
  // bodies: the sections after the null one ({ name, type, flags, data, link, info, align, entsize }); the one
  // named .shstrtab gets the names
  function writeElf(flags, bodies) {
    const enc = new TextEncoder(), shstr = [0], nameOff = [0];
    for (const b of bodies) { nameOff.push(shstr.length); shstr.push(...enc.encode(b.name), 0); }
    const shstrndx = 1 + bodies.findIndex((b) => b.name === '.shstrtab');
    bodies[shstrndx - 1].data = Uint8Array.from(shstr);
    let at = 52;
    const offs = bodies.map((b) => { at = pad(at, Math.max(b.align, 4)); const o = at; at += b.data.length; return o; });
    const shoff = pad(at, 4), n = bodies.length + 1;
    const out = new Uint8Array(shoff + 40 * n), v = new DataView(out.buffer);
    out.set([0x7f, 0x45, 0x4c, 0x46, 1, 1, 1], 0);
    v.setUint16(16, 1, true); v.setUint16(18, 40, true); v.setUint32(20, 1, true); v.setUint32(32, shoff, true);
    v.setUint32(36, flags >>> 0, true); v.setUint16(40, 52, true); v.setUint16(46, 40, true);
    v.setUint16(48, n, true); v.setUint16(50, shstrndx, true);
    bodies.forEach((b, i) => {
      out.set(b.data, offs[i]);
      const o = shoff + 40 * (i + 1);
      [nameOff[i + 1], b.type, b.flags, 0, offs[i], b.data.length, b.link, b.info, b.align, b.entsize]
        .forEach((x, k) => v.setUint32(o + 4 * k, x >>> 0, true));
    });
    return out;
  }

  // ------------------------------------------------------------------ tools/npmodule.py
  // An app linked with tools/module.ld (one .text, one .data, one .bss), every definition but main made local:
  // its code in a section of its own, its .data a copy the launcher puts in RAM, its RAM at np_<g>_data and
  // np_<g>_bss, main np_<g>_main.
  function npmodule(b, g, index, entry = 'main') {
    const e = readElf(b);
    const byName = {};
    e.secs.forEach((s, i) => {
      if (s.flags & SHF_ALLOC) {
        if (!['.text', '.data', '.bss'].includes(s.sname)) throw new Error('has a section NumPlay can\'t place (' + s.sname + ')');
        byName[s.sname] = i;
      }
    });
    if (!('.text' in byName)) throw new Error('has no code');
    const textI = byName['.text'], dataI = byName['.data'], bssI = byName['.bss'];
    const text = e.secs[textI], data = dataI === undefined ? null : e.secs[dataI], bss = bssI === undefined ? null : e.secs[bssI];
    const dataSize = data ? data.size : 0, bssSize = bss ? bss.size : 0;
    const ramAlign = Math.max(8, ...[data, bss].filter(Boolean).map((s) => s.align));

    // the symbols: null, the code's and the init's sections, the locals, then the globals
    const CODE = 1, INIT = 3;
    const out = [{ name: '', value: 0, size: 0, bind: 0, type: 0, other: 0, shndx: 0 },
                 { name: '', value: 0, size: 0, bind: STB_LOCAL, type: STT_SECTION, other: 0, shndx: CODE },
                 { name: '', value: 0, size: 0, bind: STB_LOCAL, type: STT_SECTION, other: 0, shndx: INIT }];
    const remap = new Map(), ram = new Map(), globalsOut = [], undef = new Map();
    let found = false;
    e.syms.forEach((s, k) => {
      if (!k) return;
      const sh = s.shndx;
      if (s.type === STT_SECTION) {
        if (sh === textI) remap.set(k, 1);
        else if (sh === dataI) ram.set(k, ['data', 0]);
        else if (sh === bssI) ram.set(k, ['bss', 0]);
        return;
      }
      if (s.type === STT_FILE) return;
      if (dataI !== undefined && sh === dataI) { ram.set(k, ['data', s.value]); return; }
      if (bssI !== undefined && sh === bssI) { ram.set(k, ['bss', s.value]); return; }
      if (sh === SHN_UNDEF) {
        if (s.bind === STB_LOCAL) return;
        if (!undef.has(s.name)) undef.set(s.name, { name: s.name, value: 0, size: 0, bind: s.bind, type: STT_NOTYPE, other: 0, shndx: SHN_UNDEF });
        return;
      }
      if (sh === textI) {
        const n = { ...s, shndx: CODE, other: 0 };
        if (s.name === entry && s.bind !== STB_LOCAL) {
          n.name = 'np_' + g + '_main';
          n.bind = STB_GLOBAL;
          found = true;
          globalsOut.push([k, n]);
        } else {  // (every other definition stays private to the module)
          n.bind = STB_LOCAL;
          remap.set(k, out.length);
          out.push(n);
        }
        return;
      }
      if (sh === SHN_ABS) { remap.set(k, out.length); out.push({ ...s, bind: STB_LOCAL }); return; }
      throw new Error('has a symbol in a section NumPlay can\'t place (' + s.name + ')');
    });
    if (!found) throw new Error('has no main');
    const firstGlobal = out.length;
    for (const [k, n] of globalsOut) { remap.set(k, out.length); out.push(n); }
    out.push({ name: 'np_' + g + '_init', value: 0, size: dataSize, bind: STB_GLOBAL, type: STT_OBJECT, other: 0, shndx: INIT });
    const ramIndex = {};
    for (const r of ['data', 'bss']) {
      ramIndex[r] = out.length;
      out.push({ name: 'np_' + g + '_' + r, value: 0, size: 0, bind: STB_GLOBAL, type: STT_NOTYPE, other: 0, shndx: SHN_UNDEF });
    }
    const undIndex = new Map();
    for (const [name, n] of undef) { undIndex.set(name, out.length); out.push(n); }
    e.syms.forEach((s, k) => {
      if (k && s.shndx === SHN_UNDEF && s.bind !== STB_LOCAL && s.type !== STT_SECTION) remap.set(k, undIndex.get(s.name));
    });

    // the relocations: into the app's RAM, against np_<g>_data or np_<g>_bss (the offset goes in the addend)
    const convert = (ti, blob) => {
      blob = blob.slice();
      const v = new DataView(blob.buffer), rels = [];
      for (const [off, typ, si] of relsFor(e, ti)) {
        if (ram.has(si)) {
          const [region, value] = ram.get(si);
          if (typ !== R_ARM_ABS32) throw new Error('reaches its RAM in a way NumPlay can\'t move');
          if (value) v.setUint32(off, (v.getUint32(off, true) + value) >>> 0, true);
          rels.push([off, typ, ramIndex[region]]);
        } else if (remap.has(si)) rels.push([off, typ, remap.get(si)]);
        else throw new Error('has a relocation NumPlay can\'t place');
      }
      return [blob, rels];
    };
    const [code, codeRels] = convert(textI, text.data);
    const [init, initRels] = data ? convert(dataI, data.data) : [new Uint8Array(0), []];
    for (const s of e.secs)
      if (s.type === SHT_REL && s.info !== textI && s.info !== dataI && e.secs[s.info].flags & SHF_ALLOC)
        throw new Error('has relocations NumPlay can\'t place');

    const base = '.rodata.np.' + index + '.' + g;
    const { symtab, strtab } = symbols(out);
    const bodies = [
      { name: base, type: SHT_PROGBITS, flags: SHF_ALLOC | SHF_EXECINSTR, data: code, link: 0, info: 0, align: Math.max(4, text.align), entsize: 0 },
      { name: '.rel' + base, type: SHT_REL, flags: SHF_INFO_LINK, data: relBytes(codeRels), link: 5, info: 1, align: 4, entsize: 8 },
      { name: base + '.init', type: SHT_PROGBITS, flags: SHF_ALLOC, data: init, link: 0, info: 0, align: data ? Math.max(4, data.align) : 4, entsize: 0 },
      { name: '.rel' + base + '.init', type: SHT_REL, flags: SHF_INFO_LINK, data: relBytes(initRels), link: 5, info: 3, align: 4, entsize: 8 },
      { name: '.symtab', type: SHT_SYMTAB, flags: 0, data: symtab, link: 6, info: firstGlobal, align: 4, entsize: 16 },
      { name: '.strtab', type: SHT_STRTAB, flags: 0, data: strtab, link: 0, info: 0, align: 1, entsize: 0 },
      { name: '.shstrtab', type: SHT_STRTAB, flags: 0, data: null, link: 0, info: 0, align: 1, entsize: 0 },
    ];
    const attrs = e.secs.find((s) => s.type === SHT_ARM_ATTRIBUTES);
    if (attrs) bodies.push({ name: '.ARM.attributes', type: SHT_ARM_ATTRIBUTES, flags: 0, data: attrs.data, link: 0, info: 0, align: 1, entsize: 0 });
    return { obj: writeElf(e.flags, bodies), code: code.length, data: dataSize, bss: bssSize, ramAlign,
             undefined: [...undef.keys()].sort() };
  }

  // ------------------------------------------------------------------ the app's name and icon
  function lz4(src, n) {  // (an LZ4 block of n bytes)
    const out = new Uint8Array(n);
    let i = 0, o = 0;
    const len = (l) => { if (l === 15) { let b; do { b = src[i++]; l += b; } while (b === 255 && i < src.length); } return l; };
    while (i < src.length && o < n) {
      const t = src[i++];
      const lit = len(t >> 4);
      out.set(src.subarray(i, i + lit).subarray(0, n - o), o);
      i += lit; o += lit;
      if (i >= src.length || o >= n) break;
      const back = src[i] | src[i + 1] << 8;
      i += 2;
      if (!back || back > o) throw new Error('has a broken icon');
      for (let m = len(t & 15) + 4; m > 0 && o < n; m--, o++) out[o] = out[o - back];
    }
    return out;
  }
  // { name, icon }: its name as the calculator shows it (or null), its icon's 55 x 56 RGB565 pixels (or null)
  function appInfo(b) {
    const e = readElf(b), sec = (n) => e.secs.find((s) => s.sname === n);
    const nm = sec('.rodata.eadk_app_name'), ic = sec('.rodata.eadk_app_icon');
    let name = nm ? cstr(nm.data, 0) : null, icon = null;
    if (ic) {
      try {
        const raw = lz4(ic.data, ICON_W * ICON_H * 2);
        icon = new Uint16Array(ICON_W * ICON_H);
        for (let p = 0; p < icon.length; p++) icon[p] = raw[2 * p] | raw[2 * p + 1] << 8;
      } catch (x) { icon = null; }
    }
    return { name, icon };
  }

  // ------------------------------------------------------------------ its card on the home screen
  const rgb = (c) => [(c >> 11) * 255 / 31, (c >> 5 & 63) * 255 / 63, (c & 31) * 255 / 31];
  const r565 = (r, g, b) => (Math.round(r * 31 / 255) << 11) | (Math.round(g * 63 / 255) << 5) | Math.round(b * 31 / 255);
  const hex = (r, g, b) => (Math.round(r) << 16) | (Math.round(g) << 8) | Math.round(b);
  // its colours (0xRRGGBB, as games.json's): what its icon looks like, light at the top, dark at the bottom
  function colors(icon) {
    let n = 0, s = [0, 0, 0];
    if (icon) for (const c of icon) {
      const p = rgb(c);
      if (p[0] + p[1] + p[2] > 690) continue;  // (white: the icon's ground)
      s = s.map((x, k) => x + p[k]); n++;
    }
    const a = n ? s.map((x) => x / n) : [90, 90, 100];
    const mix = (t, w) => a.map((x) => x + (t - x) * w);
    return { top: hex(...mix(255, 0.25)), bottom: hex(...mix(0, 0.55)), accent: hex(...a) };
  }
  // its icon, twice its size, with the calculator's round corners, on its bottom colour: palette indices and the
  // palette (RGB565), at most 255 colours
  function card(icon, bottom) {
    const bg = r565(bottom >> 16, bottom >> 8 & 255, bottom & 255);
    const px = new Uint16Array(SHOT_W * SHOT_H).fill(bg);
    if (icon) {
      const s = 2, w = ICON_W * s, h = ICON_H * s, x0 = (SHOT_W - w) >> 1, y0 = (SHOT_H - h) >> 1, r = 16;
      for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
        const cx = x < r ? r - x - 0.5 : x >= w - r ? x - (w - r) + 0.5 : 0;
        const cy = y < r ? r - y - 0.5 : y >= h - r ? y - (h - r) + 0.5 : 0;
        if (cx * cx + cy * cy > r * r) continue;
        px[(y0 + y) * SHOT_W + x0 + x] = icon[(y / s | 0) * ICON_W + (x / s | 0)];
      }
    }
    // (fewer bits of each colour until they're 255 at most)
    for (const mask of [0xFFFF, 0xF7DE, 0xE79C, 0xC718, 0x8610]) {
      const pal = [...new Set(px.map((c) => c & mask))];
      if (pal.length > 255) continue;
      const at = new Map(pal.map((c, i) => [c, i]));
      return { palette: Uint16Array.from(pal), indices: Uint8Array.from(px, (c) => at.get(c & mask)) };
    }
    throw new Error('unreachable');
  }

  // ------------------------------------------------------------------ its entry and marks (gen_games.py's)
  // a: { id, index, title, tagline, top, bottom, accent, data, bss, palette, z (the indices, raw DEFLATE) }
  function entryObject(a) {
    const enc = new TextEncoder(), p = '.rodata.np.' + a.index + '.' + a.id;
    // the card's section: the np_shot_t, its palette, its pixels, the names, an empty list (no save files)
    const parts = [], put = (bytes, align) => { const at = pad(parts.length, align); while (parts.length < at) parts.push(0); parts.push(...bytes); return at; };
    put(new Uint8Array(20), 4);
    const palAt = put(new Uint8Array(a.palette.buffer, a.palette.byteOffset, a.palette.byteLength), 2);
    const zAt = put(a.z, 1);
    const str = (s) => put([...enc.encode(s), 0], 1);
    const idAt = str(a.id), titleAt = str(a.title), tagAt = str(a.tagline);
    const noneAt = put(new Uint8Array(4), 4);
    const shots = Uint8Array.from(parts), sv = new DataView(shots.buffer);
    sv.setUint16(0, SHOT_W, true); sv.setUint16(2, SHOT_H, true); sv.setUint16(4, a.palette.length, true);
    sv.setUint32(8, a.z.length, true); sv.setUint32(12, palAt, true); sv.setUint32(16, zAt, true);
    // the np_game_t (launcher/src/np.h): 80 bytes
    const ent = new Uint8Array(80), v = new DataView(ent.buffer);
    const ENT = 1, SHOTS = 2, G = 3;  // (symbols: the two sections', then np_<id>_begin, end, main, data, init, bss)
    const names = ['begin', 'end', 'main', 'data', 'init', 'bss'];
    const sym = (n) => G + names.indexOf(n);
    const rels = [];
    const at = (off, s, add) => { v.setUint32(off, add || 0, true); rels.push([off, R_ARM_ABS32, s]); };
    at(0, SHOTS, idAt); at(4, SHOTS, titleAt); at(8, SHOTS, tagAt);
    v.setUint32(16, a.top, true); v.setUint32(20, a.bottom, true); v.setUint32(24, a.accent, true);
    at(28, sym('begin')); at(32, sym('end')); at(36, sym('main'));
    at(40, sym('data')); at(44, sym('init')); v.setUint32(48, a.data, true); at(52, sym('bss')); v.setUint32(56, a.bss, true);
    at(60, SHOTS, 0); ent[64] = 1; at(68, SHOTS, noneAt); at(72, SHOTS, noneAt);
    const shotRels = [[12, R_ARM_ABS32, SHOTS], [16, R_ARM_ABS32, SHOTS]];
    const word = (w) => { const d = new Uint8Array(4); new DataView(d.buffer).setUint32(0, w, true); return d; };
    const syms = [{ name: '', value: 0, size: 0, bind: 0, type: 0, other: 0, shndx: 0 },
                  { name: '', value: 0, size: 0, bind: STB_LOCAL, type: STT_SECTION, other: 0, shndx: 1 },
                  { name: '', value: 0, size: 0, bind: STB_LOCAL, type: STT_SECTION, other: 0, shndx: 3 }]
      .concat(names.map((n) => ({ name: 'np_' + a.id + '_' + n, value: 0, size: 0, bind: STB_GLOBAL, type: STT_NOTYPE, other: 0, shndx: SHN_UNDEF })));
    const { symtab, strtab } = symbols(syms);
    const sec = (name, data, align) => ({ name, type: SHT_PROGBITS, flags: SHF_ALLOC, data, link: 0, info: 0, align, entsize: 0 });
    const rel = (name, rs, info) => ({ name: '.rel' + name, type: SHT_REL, flags: SHF_INFO_LINK, data: relBytes(rs), link: 7, info, align: 4, entsize: 8 });
    return writeElf(0, [
      sec('.rodata.np_games.' + a.id, ent, 4), rel('.rodata.np_games.' + a.id, rels, 1),
      sec(p + '.shots', shots, 4), rel(p + '.shots', shotRels, 3),
      sec(p + '.0head', word(HEAD), 4), sec(p + '.zztail', word(TAIL), 4),
      { name: '.symtab', type: SHT_SYMTAB, flags: 0, data: symtab, link: 8, info: 3, align: 4, entsize: 16 },
      { name: '.strtab', type: SHT_STRTAB, flags: 0, data: strtab, link: 0, info: 0, align: 1, entsize: 0 },
      { name: '.shstrtab', type: SHT_STRTAB, flags: 0, data: null, link: 0, info: 0, align: 1, entsize: 0 },
    ]);
  }

  const api = { readElf, npmodule, appInfo, colors, card, entryObject, SHOT_W, SHOT_H, ICON_W, ICON_H };
  if (typeof module === 'object' && module.exports) module.exports = api;  // (Node, for the test)
  else root.npApps = api;
})(this);
