/* NumPlay Studio: the calculator's files, for Saves (saves-ui.js, over usb.js). docs/saves.md tells all of it.
 *
 * Epsilon keeps its files in 42 KB of RAM: a 16-bit size, the name ("base.ext" and a zero), then the content, one
 * after the other, and a zero size ends the list. A game's save is one or more of them, named in games/games.json
 * ("records"; kit.json's "saves" for the website), and installing apps or a reset empties them all. Here:
 * - the list read, and changed the way the games change it (games/common/epsilon_files.h, to the byte:
 *   tests/test_saves.py runs both), so a file the website puts back is a file the calculator could have written;
 * - a change turned into the few writes that make it over USB, in an order that leaves a list the calculator can
 *   read whatever happens between two of them (plan);
 * - Epsilon's memory of the last file it looked up, found the way the games find it before a file moves (cache);
 * - the copies of saves kept in Python scripts (launcher/src/storage.c's numplay_saves.py, and each big game's own),
 *   read, so a save that is only in its copy counts as there;
 * - which game a file is of, and a backup as a file of its own.
 * Plain functions of bytes, for the browser and the test in Node. */
'use strict';
(function (root) {
  const MAGIC = 0xEE0BDDBA;  // Epsilon's, before and after its files
  const BLOCK = 2048;        // the most one USB transfer carries: the calculator copies it in one go
  // NumPlay's own files (launcher/src/storage.c: CONFIG_NAME, BG_NAME, COPY_NAME)
  const COPY = 'numplay_saves.py';
  const OWN = ['numplay.set', 'npbg.set', COPY];
  // Epsilon's own kinds of file (ion/storage/record.h): its settings, functions, lists... never read or written here
  const EPSILON = ['eq', 'exp', 'func', 'lis', 'seq', 'mat', 'reg', 'pc', 'rgm', 'stm', 'sys'];
  const FORMAT = 'numplay-saves', VERSION = 1;

  const rd16 = (b, p) => b[p] | b[p + 1] << 8;
  const wr16 = (b, p, v) => { b[p] = v & 255; b[p + 1] = v >> 8 & 255; };
  const rd32 = (b, p) => (b[p] | b[p + 1] << 8 | b[p + 2] << 16 | b[p + 3] << 24) >>> 0;
  const text = (b) => { let s = ''; for (let i = 0; i < b.length; i++) s += String.fromCharCode(b[i]); return s; };
  const bytes = (s) => { const b = new Uint8Array(s.length); for (let i = 0; i < s.length; i++) b[i] = s.charCodeAt(i) & 255; return b; };
  const same = (a, b) => { if (a.length !== b.length) return false; for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false; return true; };
  const ext = (name) => { const d = name.lastIndexOf('.'); return d < 0 ? '' : name.slice(d + 1); };

  // ------------------------------------------------------------------ the list (epsilon_files.h)
  // where the list ends (its zero size), or -1 if it isn't one
  function end(buf) {
    let p = 0;
    while (p + 2 <= buf.length) {
      const n = rd16(buf, p);
      if (!n) return p;
      if (n < 4 || p + n > buf.length) return -1;
      p += n;
    }
    return -1;
  }
  // its files, in order: { name, at, size (the whole record's), data (a view of buf) }, or null if the list isn't one
  function files(buf) {
    const e = end(buf), out = [];
    if (e < 0) return null;
    for (let p = 0; p < e; p += rd16(buf, p)) {
      const n = rd16(buf, p);
      let z = p + 2;
      while (z < p + n && buf[z]) z++;
      if (z === p + n) return null;  // (a name with no end)
      out.push({ name: text(buf.subarray(p + 2, z)), at: p, size: n, data: buf.subarray(z + 1, p + n) });
    }
    return out;
  }
  function find(buf, name, e) {
    const want = bytes(name + '\0');
    for (let p = 0; p < e; p += rd16(buf, p)) {
      const n = rd16(buf, p);
      if (n < 2 + want.length) continue;
      let i = 0;
      while (i < want.length && buf[p + 2 + i] === want[i]) i++;
      if (i === want.length) return p;
    }
    return -1;
  }
  // ef_remove: the last file's size becomes the zero that ends the list; any other is slid over, zeros left behind
  function remove(buf, name) {
    const e = end(buf);
    if (e < 0) return false;
    const at = find(buf, name, e);
    if (at < 0) return true;
    const size = rd16(buf, at);
    if (at + size === e) { wr16(buf, at, 0); return true; }
    buf.copyWithin(at, at + size, e + 2);
    buf.fill(0, e + 2 - size, e + 2);
    return true;
  }
  // ef_write: in place when it keeps its size, else taken out and added at the end; false (nothing changed) with no room
  function write(buf, name, data) {
    let e = end(buf);
    if (e < 0) return false;
    const nlen = name.length + 1, total = 2 + nlen + data.length;
    if (total > 0xFFFF) return false;
    let at = find(buf, name, e);
    if (at >= 0 && rd16(buf, at) !== total) {
      if (e - rd16(buf, at) + total + 2 > buf.length) return false;
      remove(buf, name);
      e = end(buf);
      at = -1;
    }
    if (at < 0) {
      if (e + total + 2 > buf.length) return false;
      at = e;
      wr16(buf, at + total, 0);  // (the list stays ended)
      buf.set(bytes(name + '\0'), at + 2);
      wr16(buf, at, total);
    }
    buf.set(data, at + 2 + nlen);
    return true;
  }

  // A name the website may write: plain characters, a base and an extension with one dot between (Epsilon expects
  // an extension), not one of Epsilon's own kinds, and short enough for the games (launcher/src/storage.c reads
  // names into 64 bytes)
  function okName(name) {
    if (typeof name !== 'string' || !/^[\x21-\x7e]{3,63}$/.test(name)) return false;
    const d = name.indexOf('.');
    return d > 0 && d < name.length - 1 && d === name.lastIndexOf('.') && !EPSILON.includes(ext(name));
  }

  // Groups of changes, each all or nothing (a game with half its files is worse than without): { remove: [names],
  // forget: name -> whether its line leaves NumPlay's copy too (forget, below), write: [{ name, data }] }. Returns
  // the list after them and, for each group that didn't go, why ('name': a file the website won't write, 'room':
  // the calculator's files are full); buf isn't touched.
  function put(buf, groups) {
    let now = buf.slice();
    const failed = [];
    groups.forEach((g, i) => {
      const next = now.slice();
      let why = null;
      for (const name of g.remove || []) if (!remove(next, name)) why = 'list';
      if (g.forget && !why) forget(next, g.forget);
      for (const f of g.write || []) {
        if (why) break;
        if (!okName(f.name)) why = 'name';
        else if (!write(next, f.name, f.data)) why = 'room';
      }
      if (why) failed.push({ group: i, why }); else now = next;
    });
    return { after: now, failed };
  }

  // ------------------------------------------------------------------ a change as writes over USB
  // The calculator waits while the website talks to it, and each transfer (at most BLOCK bytes) lands in one go. A
  // change that fits one transfer is one write. A longer one is cut where the first changed file starts: a zero
  // size there first (the list ends before it: shorter, and whole), then everything after those two bytes, then
  // the two bytes. Unplugged in between, the calculator has the files before the change and no broken list; the
  // backup made before has the rest.
  // "moves": a file that was there is now elsewhere or gone, so Epsilon's memory of the last file it looked up
  // could point into moved bytes (cache, below): it must be cleared first, or the change not made.
  function plan(before, after) {
    const A = files(before), B = files(after), n = before.length;
    if (!A || !B || after.length !== n) throw new Error('not a list of files');
    let f = 0;
    while (f < n && before[f] === after[f]) f++;
    if (f === n) return { writes: [], moves: false, from: n };
    let z = n - 1;
    while (before[z] === after[z]) z--;
    let i = 0;
    while (i < A.length && i < B.length && A[i].at === B[i].at && A[i].size === B[i].size &&
           same(before.subarray(A[i].at, A[i].at + A[i].size), after.subarray(B[i].at, B[i].at + B[i].size))) i++;
    const a = i < A.length ? A[i].at : end(before);
    const kept = new Map(B.map((r) => [r.at, r]));
    const moves = A.slice(i).some((r) => { const s = kept.get(r.at); return !s || s.size !== r.size || s.name !== r.name; });
    if (z + 1 - f <= BLOCK) return { writes: [{ at: f, bytes: after.slice(f, z + 1) }], moves, from: a };
    const writes = [];
    if (rd16(before, a)) writes.push({ at: a, bytes: new Uint8Array(2) });
    for (let p = a + 2; p <= z; p += BLOCK) writes.push({ at: p, bytes: after.slice(p, Math.min(p + BLOCK, z + 1)) });
    if (rd16(after, a)) writes.push({ at: a, bytes: after.slice(a, a + 2) });
    return { writes, moves, from: a };
  }

  // ------------------------------------------------------------------ Epsilon's memory of the last file looked up
  // Ion::crc32Byte: CRC-32/MPEG-2 fed word by word (little-endian words, most significant byte first), then the
  // bytes left (tools/emu.py's epsilon_crc32)
  function crc32(d) {
    if (!d.length) return 0;
    let crc = 0xFFFFFFFF;
    const eat = (b) => {
      crc ^= b << 24;
      for (let k = 0; k < 8; k++) crc = crc & 0x80000000 ? (crc << 1) ^ 0x04C11DB7 : crc << 1;
      crc >>>= 0;
    };
    const n = d.length >> 2;
    for (let i = 0; i < n; i++) for (let j = 3; j >= 0; j--) eat(d[4 * i + j]);
    for (let i = 4 * n; i < d.length; i++) eat(d[i]);
    return crc >>> 0;
  }
  // a file's name as Epsilon keeps it: the checksum of the checksums of its base name and its extension
  function nameCrc(full) {
    const d = full.indexOf('.');
    if (d < 0) return 0;
    const a = crc32(bytes(full.slice(0, d))), b = crc32(bytes(full.slice(d + 1))), w = new Uint8Array(8);
    for (let k = 0; k < 4; k++) { w[k] = a >>> 8 * k & 255; w[4 + k] = b >>> 8 * k & 255; }
    return crc32(w);
  }
  // Epsilon remembers the last file it looked up, by its name's checksum and where it was, in its memory after the
  // files: (the size it may use, the checksum, the pointer), some words after the closing magic. `after`: the bytes
  // that follow that magic; base: the address of the first file. Found as ef_cache finds it: the one place where
  // all three make sense (a pointer on a file whose name has that checksum, or none and no checksum). Returns
  // { at: where the checksum is in `after` (8 bytes to clear, with the pointer), ptr, room: the size Epsilon may use },
  // or null: not found, or two places could be it. On Upsilon (Epsilon 15's files) it is at a fixed place.
  function cache(after, base, buf, upsilon) {
    const e = end(buf), w = (i) => rd32(after, 4 * i);
    if (e < 0 || after.length < 12) return null;
    if (upsilon) {
      const delegate = w(0), ptr = w(2);
      if (delegate && (delegate < 0x20000000 || delegate >= 0x20040000)) return null;
      if (ptr && (ptr < base || ptr >= base + e)) return null;
      return { at: 4, ptr, room: buf.length };
    }
    let found = null;
    for (let i = 1; i < 96 && 4 * (i + 3) <= after.length; i++) {
      const room = w(i), crc = w(i + 1), ptr = w(i + 2);
      if (room > buf.length || room < e + 2 || room < buf.length / 2) continue;
      if (!ptr) {
        if (crc) continue;
      } else {
        if (ptr < base || ptr >= base + e) continue;
        let p = 0;
        while (p < e && p !== ptr - base) p += rd16(buf, p);
        if (p !== ptr - base) continue;
        let z = p + 2;
        while (z < p + rd16(buf, p) && buf[z]) z++;
        if (!crc || crc !== nameCrc(text(buf.subarray(p + 2, z)))) continue;
      }
      if (found) return null;
      found = { at: 4 * (i + 1), ptr, room };
    }
    return found;
  }

  // ------------------------------------------------------------------ the copies of saves in Python scripts
  const B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
  function b64encode(d) {
    let s = '';
    for (let i = 0; i < d.length; i += 3) {
      const left = d.length - i, v = d[i] << 16 | (left > 1 ? d[i + 1] << 8 : 0) | (left > 2 ? d[i + 2] : 0);
      s += B64[v >> 18 & 63] + B64[v >> 12 & 63] + (left > 1 ? B64[v >> 6 & 63] : '=') + (left > 2 ? B64[v & 63] : '=');
    }
    return s;
  }
  // (as storage.c reads it: whole groups of four, '=' only to pad the last; null for anything else)
  function b64decode(s) {
    if (s.length % 4) return null;
    const out = new Uint8Array(s.length / 4 * 3);
    let o = 0;
    for (let i = 0; i < s.length; i += 4) {
      const pad = s[i + 3] !== '=' ? 0 : s[i + 2] !== '=' ? 1 : 2;
      if (pad && i + 4 !== s.length) return null;
      const v = [0, 1, 2, 3].map((k) => (k >= 4 - pad ? 0 : B64.indexOf(s[i + k])));
      if (v.some((x) => x < 0)) return null;
      const w = v[0] << 18 | v[1] << 12 | v[2] << 6 | v[3];
      for (let k = 0; k < 3 - pad; k++) out[o++] = w >> (16 - 8 * k) & 255;
    }
    return out.subarray(0, o);
  }
  // games/common/np_lz.h's unpacker: size bytes, or null if the stream is damaged
  function lzUnpack(d, size) {
    const out = new Uint8Array(size);
    let i = 0, o = 0;
    while (i < d.length) {
      const flags = d[i++];
      for (let b = 0; b < 8 && i < d.length; b++) {
        if (flags >> b & 1) {
          if (o >= size) return null;
          out[o++] = d[i++];
        } else {
          if (i + 2 > d.length) return null;
          const dist = (d[i] | (d[i + 1] & 15) << 8) + 1, len = (d[i + 1] >> 4) + 3;
          i += 2;
          if (dist > o || o + len > size) return null;
          for (let k = 0; k < len; k++, o++) out[o] = out[o - dist];
        }
      }
    }
    return o === size ? out : null;
  }
  // the lines of a copy: [{ name, at: where the line starts in d, line: its text }]
  function copyLines(d) {
    const out = [];
    for (let p = 0; p < d.length;) {
      let e = p;
      while (e < d.length && d[e] !== 10) e++;
      const line = text(d.subarray(p, e)).replace(/[\r\0]+$/, ''), m = /^#([>=])([^:]{1,31}):(.*)$/.exec(line);
      if (m) out.push({ name: m[2], at: p, packed: m[1] === '=', rest: m[3] });
      p = e + 1;
    }
    return out;
  }
  // The saves a copy holds, name to bytes: "#>name:base64" as it is, "#=name:size:base64" packed. A script's
  // content (its status byte first) or the file of one; a damaged line is no save, as on the calculator.
  function copySaves(d) {
    const out = new Map();
    for (const l of copyLines(d)) {
      let data = null;
      if (l.packed) {
        const m = /^([0-9]{1,5}):(.+)$/.exec(l.rest), size = m ? +m[1] : 0, raw = m && b64decode(m[2]);
        if (raw && size > 0 && size < 0x10000) data = lzUnpack(raw, size);
      } else if (l.rest.length) data = b64decode(l.rest);
      if (data && !out.has(l.name)) out.set(l.name, data.slice());
    }
    return out;
  }
  // The calculator forgets a save for good: its line in NumPlay's copy is struck out too (its second character
  // made '-', in place: nothing moves), or NumPlay would bring the save back from it. NumPlay rewrites the whole
  // copy the next time it runs. `gone`: name -> whether that save goes. Changes buf; the number of lines struck out.
  function forget(buf, gone) {
    let n = 0;
    for (const f of files(buf) || []) {
      if (f.name !== COPY) continue;
      const base = f.data.byteOffset - buf.byteOffset;
      for (const l of copyLines(f.data)) if (gone(l.name)) { buf[base + l.at + 1] = 45; n++; }
    }
    return n;
  }

  // ------------------------------------------------------------------ whose file it is
  // kit.json's "saves": [{ id, title, records }], the games in NumPlay's order, a name ending with '*' for every
  // file that starts with the rest. Returns name -> a game's id, 'numplay' (its settings and its copy), 'python'
  // (a script of yours), 'other' (another app's file), or null: one of Epsilon's own, left alone. A file two games
  // share is the first's, as in the launcher.
  function owners(saves) {
    const exact = new Map(), starts = [];
    for (const g of saves || [])
      for (const r of g.records || []) {
        if (r.endsWith('*')) starts.push([r.slice(0, -1), g.id]);
        else if (!exact.has(r)) exact.set(r, g.id);
      }
    starts.sort((a, b) => b[0].length - a[0].length);  // (the longest start first)
    return (name) => {
      if (exact.has(name)) return exact.get(name);
      if (OWN.includes(name)) return 'numplay';
      const s = starts.find(([p]) => name.startsWith(p));
      if (s) return s[1];
      const x = ext(name);
      return EPSILON.includes(x) ? null : x === 'py' ? 'python' : 'other';
    };
  }

  // ------------------------------------------------------------------ what the calculator holds
  // The files of a list a backup keeps (all but Epsilon's own), as copies: [{ name, data }]
  function keep(buf) {
    return (files(buf) || []).filter((f) => !EPSILON.includes(ext(f.name))).map((f) => ({ name: f.name, data: f.data.slice() }));
  }
  // The saves a set of files stands for, name to bytes: the files themselves, and the saves that are only in a
  // copy (a script with copy lines: NumPlay's, a big game's), which the calculator brings back from it. A file
  // counts before a copy of it: it is the newer.
  function saves(list) {
    const out = new Map(list.map((f) => [f.name, f.data]));
    for (const f of list) if (ext(f.name) === 'py') for (const [name, data] of copySaves(f.data)) if (!out.has(name)) out.set(name, data);
    return out;
  }
  // What files hold, by owner (who: owners' function): Map id -> Map name -> bytes. The saves that are only in a
  // copy count; NumPlay's copy itself doesn't (it holds nothing of its own), nor Epsilon's files.
  function holds(list, who) {
    const out = new Map();
    for (const [name, data] of saves(list)) {
      const id = who(name);
      if (!id || name === COPY) continue;
      if (!out.has(id)) out.set(id, new Map());
      out.get(id).set(name, data);
    }
    return out;
  }
  const isGame = (id) => id !== 'numplay' && id !== 'python' && id !== 'other';
  // A backup's game (theirs: what the backup holds) against the calculator's (mine): 'same', 'missing' (the
  // calculator has nothing of it) or 'different'. A game's files are one thing: all of them the backup's, or it
  // isn't the same. Settings, scripts and other apps' files are each their own: the ones not in the backup stay.
  function compare(mine, theirs, id) {
    const a = theirs.get(id) || new Map(), b = mine.get(id) || new Map();
    if (isGame(id)) {
      if (!b.size) return 'missing';
      return a.size === b.size && [...a].every(([name, data]) => b.has(name) && same(b.get(name), data)) ? 'same' : 'different';
    }
    let equal = 0, differ = 0;
    for (const [name, data] of a) { if (!b.has(name)) continue; if (same(b.get(name), data)) equal++; else differ++; }
    return differ ? 'different' : equal === a.size ? 'same' : 'missing';
  }
  // The change (a group for put) that puts a backup's game on the calculator, whose files are buf: its files
  // written, and for a game, those the backup hasn't taken out, from the list and from NumPlay's copy (or NumPlay
  // would bring them back). After it the calculator holds, of that game, exactly what the backup does.
  function putBack(buf, theirs, id, who) {
    const a = theirs.get(id) || new Map(), g = { id, write: [...a].map(([name, data]) => ({ name, data })) };
    if (isGame(id)) {
      g.remove = (files(buf) || []).filter((f) => who(f.name) === id && !a.has(f.name)).map((f) => f.name);
      g.forget = (name) => who(name) === id && !a.has(name);
    }
    return g;
  }
  // the change that takes a game's saves off the calculator, its copy's lines too
  function takeOff(buf, id, who) {
    return { id, remove: (files(buf) || []).filter((f) => who(f.name) === id).map((f) => f.name), forget: (name) => who(name) === id };
  }
  // what tells two sets of files apart: the saves they hold (a save in its copy or as a file is the same save)
  const holding = (list) => digest([...saves(list)].filter(([name]) => name !== COPY).map(([name, data]) => ({ name, data })));
  // a short name for what a set of files holds, the same for the same files in any order (FNV-1a, 32 bits twice)
  function digest(list) {
    let a = 0x811C9DC5, b = 0x01000193;
    const eat = (v) => { a = Math.imul(a ^ v, 0x01000193) >>> 0; b = Math.imul(b + v, 0x85EBCA6B) >>> 0; b ^= b >>> 13; };
    for (const f of [...list].sort((x, y) => (x.name < y.name ? -1 : x.name > y.name ? 1 : 0))) {
      for (let i = 0; i < f.name.length; i++) eat(f.name.charCodeAt(i) & 255);
      eat(0);
      for (let i = 0; i < f.data.length; i++) eat(f.data[i]);
      eat(f.data.length & 255); eat(f.data.length >> 8);
    }
    return (a >>> 0).toString(16).padStart(8, '0') + (b >>> 0).toString(16).padStart(8, '0');
  }

  // ------------------------------------------------------------------ a backup as a file
  // { format, version, made (ISO date), studio (the kit's version), calculator: { model, software, id },
  //   files: [{ name, data: base64 }] }. A version newer than this code's isn't read (it says so): the names and
  // the bytes of the files are all a backup is, so there is little to ever change.
  function pack(b) {
    return JSON.stringify({ format: FORMAT, version: VERSION, made: b.made, studio: b.studio || null, calculator: b.calculator || null,
      files: b.files.map((f) => ({ name: f.name, data: b64encode(f.data) })) }, null, 1);
  }
  // A backup from a file's bytes: one of ours, or a script holding a copy of saves (numplay_saves.py or a big
  // game's, as NumWorks' website and other tools back them up): then the saves it holds. Throws an Error to show.
  function unpack(d, name) {
    const s = text(d);
    if (/^\s*\{/.test(s)) {
      let j;
      try { j = JSON.parse(new TextDecoder().decode(d)); } catch (e) { throw new Error("That file is damaged: it can't be read"); }
      if (!j || j.format !== FORMAT || !Array.isArray(j.files)) throw new Error("That isn't a backup of NumPlay's saves");
      if (j.version > VERSION) throw new Error('That backup is from a newer NumPlay Studio: reload this page');
      const out = [];
      for (const f of j.files) {
        const data = f && typeof f.data === 'string' ? b64decode(f.data) : null;
        if (!data || !okName(f.name) || data.length > 0xFFF0) throw new Error("That file is damaged: it can't be read");
        if (!out.some((x) => x.name === f.name)) out.push({ name: f.name, data: data.slice() });
      }
      return { made: typeof j.made === 'string' ? j.made : null, studio: j.studio || null, calculator: j.calculator || null, files: out };
    }
    const found = copySaves(d);
    if (!found.size) throw new Error("That isn't a backup of NumPlay's saves");
    const out = [...found].filter(([n]) => okName(n)).map(([n, data]) => ({ name: n, data }));
    return { made: null, studio: null, calculator: null, files: out, from: name || null };
  }

  const api = { MAGIC, BLOCK, COPY, OWN, EPSILON, FORMAT, VERSION, end, files, remove, write, okName, put, plan, crc32, nameCrc, cache,
    b64encode, b64decode, lzUnpack, copyLines, copySaves, forget, owners, keep, saves, holds, isGame, compare, putBack, takeOff, holding, digest,
    pack, unpack, ext, same };
  if (typeof module === 'object' && module.exports) module.exports = api;  // (Node, for the test)
  else root.npSaves = api;
})(this);
