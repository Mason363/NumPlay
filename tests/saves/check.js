/* customize/saves.js against the calculator's own code and its own rules (tests/test_saves.py runs this).
 *
 * node check.js FILES FACTS: FILES is tests/saves/files.c built for this computer, FACTS a JSON file:
 * { games: games/games.json, crcs: { name: Epsilon's checksum of it (tools/emu.py) }, seed, rounds,
 *   images: [{ path: a list as the launcher left it in the emulator, files: { name: hex } }] } */
'use strict';
const fs = require('fs'), path = require('path'), { spawn } = require('child_process');
const S = require(path.join(__dirname, '..', '..', 'customize', 'saves.js'));
const facts = JSON.parse(fs.readFileSync(process.argv[3], 'utf8'));

let failed = 0;
const fail = (what) => { failed++; console.error('  FAIL ' + what); };
const ok = (cond, what) => { if (!cond) fail(what); return !!cond; };
const hex = (b) => Buffer.from(b).toString('hex');
const unhex = (s) => new Uint8Array(Buffer.from(s, 'hex'));
function rng(seed) {  // (mulberry32)
  let a = seed >>> 0;
  return () => { a = a + 0x6D2B79F5 >>> 0; let t = Math.imul(a ^ a >>> 15, 1 | a); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; };
}
const fnv = (b) => { let h = 0x811C9DC5; for (let i = 0; i < b.length; i++) h = Math.imul(h ^ b[i], 0x01000193) >>> 0; return h.toString(16).padStart(8, '0'); };

// the C side: one process, a line in, a line out
function cside() {
  const p = spawn(process.argv[2], [], { stdio: ['pipe', 'pipe', 'inherit'] });
  let buf = '', waiting = [];
  p.stdout.on('data', (d) => {
    buf += d;
    for (let i; (i = buf.indexOf('\n')) >= 0; buf = buf.slice(i + 1)) waiting.shift()(buf.slice(0, i));
  });
  return { ask: (line) => new Promise((r) => { waiting.push(r); p.stdin.write(line + '\n'); }), end: () => p.stdin.end() };
}

const NAMES = ['a.sav', 'bb.sav', 'numdash.nds', 'nb1r0102.nbr', 'numplay_saves.py', 'x.cfg', 'long_name_of_a_game.sav', 'p8_cart.p8s'];
const blob = (r, n) => { const b = new Uint8Array(n); for (let i = 0; i < n; i++) b[i] = r() * 256; return b; };

// 1. the same changes here and in epsilon_files.h give the same bytes, change after change
async function sameAsC(c, r, size, steps) {
  await c.ask('size ' + size);
  const buf = new Uint8Array(size);
  for (let s = 0; s < steps; s++) {
    const name = NAMES[r() * NAMES.length | 0];
    let line, mine;
    if (r() < 0.3) {
      line = 'r ' + name;
      mine = S.remove(buf, name);
    } else {
      const big = r() < 0.1, data = blob(r, big ? r() * size | 0 : r() * 60 | 0);
      line = 'w ' + name + ' ' + (data.length ? hex(data) : '-');
      mine = S.write(buf, name, data);
    }
    const theirs = await c.ask(line), want = (mine ? '1 ' : '0 ') + fnv(buf);
    if (!ok(theirs === want, `the list after "${line.slice(0, 40)}" (step ${s}, ${size} bytes): C ${theirs}, here ${want}`)) return;
  }
  ok(await c.ask('dump') === hex(buf), `the whole list after ${steps} changes (${size} bytes)`);
}

// 2. a change's writes: each fits a transfer, each leaves a list of whole files, and the last gives the change
function planHolds(before, after, what) {
  const p = S.plan(before, after), now = before.slice();
  const A = new Map(S.files(before).map((f) => [f.name, f])), B = new Map(S.files(after).map((f) => [f.name, f]));
  for (const [k, w] of p.writes.entries()) {
    if (!ok(w.bytes.length <= S.BLOCK && w.bytes.length > 0 && w.at >= 0 && w.at + w.bytes.length <= now.length, `${what}: write ${k} is ${w.bytes.length} bytes at ${w.at}`)) return p;
    now.set(w.bytes, w.at);
    const list = S.files(now);
    if (!ok(list, `${what}: after write ${k} of ${p.writes.length} the list is broken`)) return p;
    for (const f of list) {
      const a = A.get(f.name), b = B.get(f.name);
      if (!ok((a && S.same(a.data, f.data)) || (b && S.same(b.data, f.data)), `${what}: after write ${k}, ${f.name} is neither as it was nor as it will be`)) return p;
    }
  }
  ok(S.same(now, after), `${what}: the writes don't give the change`);
  // "moves": a file that was there is elsewhere or gone (anything else is safe without clearing Epsilon's memory)
  const at = new Map(S.files(after).map((f) => [f.at, f]));
  const moved = S.files(before).some((f) => { const g = at.get(f.at); return !g || g.name !== f.name || g.size !== f.size; });
  ok(p.moves === moved, `${what}: moves is ${p.moves}, a file ${moved ? 'did' : "didn't"} move`);
  return p;
}
function plans(r, size, steps) {
  let buf = new Uint8Array(size);
  for (let s = 0; s < steps; s++) {
    const next = buf.slice();
    for (let k = 1 + (r() * 4 | 0); k > 0; k--) {
      const name = NAMES[r() * NAMES.length | 0];
      if (r() < 0.3) S.remove(next, name);
      else S.write(next, name, blob(r, r() < 0.25 ? r() * size * 0.4 | 0 : r() * 80 | 0));
    }
    planHolds(buf, next, `plan ${s} (${size} bytes)`);
    buf = next;
  }
}

async function main() {
  const c = cside(), r = rng(facts.seed);
  console.log(`saves.js (seed ${facts.seed})`);

  for (const size of [64, 300, 1500, 43008]) for (let k = 0; k < facts.rounds; k++) await sameAsC(c, r, size, size > 5000 ? 400 : 250);
  console.log('  the list changes as epsilon_files.h changes it');

  for (const size of [300, 5000, 43008]) plans(r, size, facts.rounds * 60);
  {  // the cases by name
    const b = new Uint8Array(43008);
    S.write(b, 'a.sav', blob(r, 3000)); S.write(b, 'b.sav', blob(r, 10)); S.write(b, 'c.sav', blob(r, 5000));
    let n = b.slice(); S.write(n, 'd.sav', blob(r, 100));
    let p = planHolds(b, n, 'a small file added');
    ok(p.writes.length === 1 && !p.moves, 'a small file added is one write and moves nothing');
    n = b.slice(); S.write(n, 'd.sav', blob(r, 9000));
    p = planHolds(b, n, 'a big file added');
    ok(!p.moves && p.writes[p.writes.length - 1].bytes.length === 2 && p.writes[p.writes.length - 1].at === S.end(b), 'a big file added: its size is written last, where the list ended');
    n = b.slice(); S.write(n, 'c.sav', blob(r, 5000));
    p = planHolds(b, n, 'a big file rewritten in place');
    ok(!p.moves, 'a file rewritten in place moves nothing');
    n = b.slice(); S.remove(n, 'a.sav');
    p = planHolds(b, n, 'the first file removed');
    ok(p.moves && p.writes[0].at === 0 && p.writes[0].bytes.length === 2 && !p.writes[0].bytes[0], 'a file removed from the middle: the list is cut there first');
    n = b.slice(); S.remove(n, 'c.sav');
    p = planHolds(b, n, 'the last file removed');
    ok(p.moves && p.writes.length === 1, 'the last file removed is one write (and it is gone: moves)');
    ok(S.plan(b, b.slice()).writes.length === 0, 'no change, no write');
  }
  console.log('  a change is writes that each leave whole files');

  // 3. Epsilon's memory of the last file: found where the emulator puts it, and only when it can be told
  {
    for (const [name, crc] of Object.entries(facts.crcs)) ok(S.nameCrc(name) === crc, `Epsilon's checksum of ${name}: ${S.nameCrc(name)}, the emulator's ${crc}`);
    const base = 0x20003004, b = new Uint8Array(43008);
    for (const n of Object.keys(facts.crcs)) S.write(b, n, blob(r, 20));
    const list = S.files(b);
    const after = (room, crc, ptr, extra) => {  // (tools/emu.py: the delegate, the name verifier's 128 bytes, then the three)
      const a = new Uint8Array(400), v = new DataView(a.buffer);
      v.setUint32(0, 0x20000F00, true);
      v.setUint32(132, room, true); v.setUint32(136, crc, true); v.setUint32(140, ptr, true);
      if (extra) extra(v);
      return a;
    };
    let c0 = S.cache(after(43008, 0, 0), base, b, false);
    ok(c0 && c0.at === 136 && c0.ptr === 0 && c0.room === 43008, 'no file remembered: found, nothing to clear');
    for (const f of list) {
      c0 = S.cache(after(43008, S.nameCrc(f.name), base + f.at), base, b, false);
      ok(c0 && c0.at === 136 && c0.ptr === base + f.at, `${f.name} remembered: found`);
    }
    ok(!S.cache(after(43008, 0x12345678, base + list[1].at), base, b, false), "a checksum that isn't the file's name: not it");
    ok(!S.cache(after(43008, S.nameCrc(list[1].name), base + list[1].at + 1), base, b, false), 'a pointer inside a file: not it');
    ok(!S.cache(after(43008, 0, 0, (v) => { v.setUint32(200, 43008, true); }), base, b, false), 'two places that could be it: neither');
    ok(!S.cache(after(1000, 0, 0), base, b, false), 'a size that makes no sense: not it');
    c0 = S.cache(after(30000, 0, 0), base, b, false);
    ok(c0 && c0.room === 30000, 'exam mode (Epsilon hides the files past a smaller size): found, with that size');
    const ups = (crc, ptr) => { const a = new Uint8Array(400), v = new DataView(a.buffer); v.setUint32(0, 0x20000F00, true); v.setUint32(4, crc, true); v.setUint32(8, ptr, true); return a; };
    c0 = S.cache(ups(5, base + list[0].at), base, b, true);
    ok(c0 && c0.at === 4 && c0.ptr === base + list[0].at, 'Upsilon: at its fixed place');
    ok(!S.cache(ups(5, 0x30000000), base, b, true), 'Upsilon: a pointer outside the files is not it');
  }
  console.log("  Epsilon's memory of the last file is found as the games find it");

  // 4. the copies of saves: what np_lz.h packs, this unpacks; a copy's lines give the saves, a damaged line none
  {
    const b64 = (d) => Buffer.from(d).toString('base64');
    for (let k = 0; k < 40 * facts.rounds; k++) {
      const n = k < 3 ? k : r() * 9000 | 0, d = new Uint8Array(n);
      let v = 0;
      for (let i = 0; i < n; i++) d[i] = r() < 0.7 ? v : (v = r() * 6 | 0);  // (runs, as saves have)
      ok(S.b64encode(d) === b64(d) && S.same(S.b64decode(b64(d)), d), `base64 of ${n} bytes`);
      if (!n) continue;
      const packed = unhex(await c.ask('pack ' + hex(d))), back = S.lzUnpack(packed, n);
      ok(back && S.same(back, d), `np_lz.h's packing of ${n} bytes unpacks to them`);
      ok(!S.lzUnpack(packed, n + 1) && (n < 2 || !S.lzUnpack(packed, n - 1)), 'the wrong size is refused');
    }
    const plain = blob(r, 50), runs = new Uint8Array(600).fill(7), packed = unhex(await c.ask('pack ' + hex(runs)));
    const copy = '\0# NumPlay\'s saves: keep this file\n#>a.sav:' + b64(plain) + '\n#=b.sav:600:' + b64(packed) + '\n' +
      '#>bad.sav:@@@@\n#=short.sav:601:' + b64(packed) + '\n#=zero.sav:0:' + b64(packed) + '\n#>empty.sav:\n#>a.sav:' + b64(runs) + '\n' +
      '#-struck.sav:' + b64(plain) + '\nprint(1)\n#>' + 'n'.repeat(32) + '.sav:' + b64(plain) + '\n#>tail.sav:' + b64(plain) + '\0';
    const got = S.copySaves(new Uint8Array(Buffer.from(copy, 'latin1')));
    ok([...got.keys()].join() === 'a.sav,b.sav,tail.sav', `a copy's saves: ${[...got.keys()].join()}`);
    ok(S.same(got.get('a.sav'), plain) && S.same(got.get('b.sav'), runs) && S.same(got.get('tail.sav'), plain), "a copy's saves, to the byte");
    const crlf = S.copySaves(new Uint8Array(Buffer.from(copy.replace(/\n/g, '\r\n'), 'latin1')));
    ok(crlf.size === 3 && S.same(crlf.get('b.sav'), runs), 'a copy saved with Windows line ends reads the same');
    // forgetting a save: its line struck out in place, the file's size and every other line as they were
    const b = new Uint8Array(43008);
    S.write(b, 'a.sav', plain); S.write(b, S.COPY, new Uint8Array(Buffer.from(copy, 'latin1'))); S.write(b, 'z.sav', plain);
    const n = b.slice();
    ok(S.forget(n, (name) => name === 'b.sav') === 1, 'one line struck out');
    const left = S.copySaves(S.files(n).find((f) => f.name === S.COPY).data);
    ok([...left.keys()].join() === 'a.sav,tail.sav', "the forgotten save isn't in the copy any more");
    const p = planHolds(b, n, 'a save forgotten');
    ok(p.writes.length === 1 && p.writes[0].bytes.length === 1 && !p.moves, 'forgetting is one byte, and nothing moves');
  }
  for (const im of facts.images) {  // lists the launcher left in the emulator: its copy holds its saves
    const b = new Uint8Array(fs.readFileSync(im.path)), list = S.files(b);
    if (!ok(list, `${im.path}: not a list`)) continue;
    const copy = list.find((f) => f.name === S.COPY);
    if (!ok(copy, `${im.path}: no ${S.COPY}`)) continue;
    const got = S.copySaves(copy.data);
    ok([...got.keys()].sort().join() === Object.keys(im.files).sort().join(), `${im.path}: the copy holds ${[...got.keys()].sort()}, expected ${Object.keys(im.files).sort()}`);
    for (const [name, h] of Object.entries(im.files)) ok(got.has(name) && hex(got.get(name)) === h, `${im.path}: ${name} in the launcher's copy, to the byte`);
    const all = S.saves(S.keep(b));
    for (const [name, h] of Object.entries(im.files)) ok(hex(all.get(name)) === h, `${im.path}: ${name} among the saves`);
  }
  console.log('  the copies of saves read as the calculator reads them' + (facts.images.length ? ` (${facts.images.length} from the launcher itself)` : ''));

  // 5. whose file: every name of games.json is its game's, the first's when two share it
  {
    const who = S.owners(facts.games.map((g) => ({ id: g.id, title: g.title, records: g.records })));
    const first = new Map();
    for (const g of facts.games) for (const rec of g.records) if (!first.has(rec)) first.set(rec, g.id);
    for (const [rec, id] of first) {
      const name = rec.endsWith('*') ? rec.slice(0, -1) + '0102.dat' : rec;
      ok(who(name) === id, `${name} is ${id}'s, not ${who(name)}'s`);
      ok(S.okName(name), `${name} is a name the website may write`);
      ok(!S.EPSILON.includes(S.ext(name)), `${name} isn't one of Epsilon's kinds of file`);
    }
    for (const n of S.OWN) ok(who(n) === 'numplay', `${n} is NumPlay's own`);
    ok(who('hello.py') === 'python' && who('thing.xyz') === 'other', 'a script of yours, another app\'s file');
    for (const x of S.EPSILON) ok(who('f.' + x) === null && !S.okName('f.' + x), `Epsilon's .${x} files are left alone`);
    for (const bad of ['', 'a', 'noext', '.sav', 'a.', 'a b.sav', 'a\n.sav', 'é.sav', 'a.b.c', 'x'.repeat(64) + '.s', 5, null]) ok(!S.okName(bad), `${JSON.stringify(bad)} is not a name to write`);
  }
  console.log('  every file of games.json is its game\'s');

  // 6. changes by game: all of a game or none of it; a backup's file, there and back
  {
    const b = new Uint8Array(600);
    S.write(b, 'keep.sav', blob(r, 100)); S.write(b, 'old.sav', blob(r, 100));
    const big = blob(r, 500), small = blob(r, 50), tiny = blob(r, 30);
    const res = S.put(b, [
      { write: [{ name: 'one.sav', data: small }, { name: 'two.sav', data: big }] },  // (no room for the second: neither goes)
      { remove: ['old.sav'], write: [{ name: 'new.sav', data: tiny }] },
      { write: [{ name: 'pr.sys', data: tiny }] },
    ]);
    const names = S.files(res.after).map((f) => f.name).join();
    ok(names === 'keep.sav,new.sav', `after the groups: ${names}`);
    ok(res.failed.length === 2 && res.failed[0].group === 0 && res.failed[0].why === 'room' && res.failed[2 - 1].why === 'name', 'the groups that did not go say why');
    ok(S.files(b).map((f) => f.name).join() === 'keep.sav,old.sav', "put doesn't touch the list it was given");
    const backup = { made: '2026-11-25T12:00:00.000Z', studio: '1.24.25', calculator: { model: 'N0120', software: '25.2.0', id: '1234' }, files: S.keep(res.after) };
    const back = S.unpack(new TextEncoder().encode(S.pack(backup)));
    ok(back.made === backup.made && back.calculator.model === 'N0120' && back.files.length === 2 && back.files.every((f, i) => f.name === backup.files[i].name && S.same(f.data, backup.files[i].data)), 'a backup packed and unpacked is the same');
    ok(S.digest(backup.files) === S.digest([...back.files].reverse()) && S.digest(backup.files) !== S.digest(backup.files.slice(1)), 'the same files have the same digest in any order');
    const j = JSON.parse(S.pack(backup));
    const bad = (change, what) => { const x = JSON.parse(JSON.stringify(j)); change(x); let threw = false; try { S.unpack(new TextEncoder().encode(JSON.stringify(x))); } catch (e) { threw = true; } ok(threw, what + ' is refused'); };
    bad((x) => { x.version = 99; }, 'a backup from a newer version');
    bad((x) => { x.format = 'other'; }, 'another format');
    bad((x) => { x.files[0].name = 'pr.sys'; }, "a backup with one of Epsilon's files");
    bad((x) => { x.files[0].data = '@@@'; }, 'a backup with damaged data');
    let threw = false;
    try { S.unpack(new TextEncoder().encode('hello')); } catch (e) { threw = true; }
    ok(threw, 'a file that is no backup is refused');
    const fromCopy = S.unpack(new Uint8Array(Buffer.from('# x\n#>a.sav:' + Buffer.from(small).toString('base64') + '\n', 'latin1')), 'numplay_saves.py');
    ok(fromCopy.files.length === 1 && fromCopy.files[0].name === 'a.sav' && S.same(fromCopy.files[0].data, small), 'a script holding a copy is a backup of its saves');
  }
  console.log('  changes go a game at a time, and a backup file reads back');

  // 7. a backup against the calculator, game by game, and a game put back or taken off
  {
    const who = S.owners(facts.games.map((g) => ({ id: g.id, records: g.records })));
    const b64 = (d) => Buffer.from(d).toString('base64'), lat = (t) => new Uint8Array(Buffer.from(t, 'latin1'));
    const dash = blob(r, 180), world = blob(r, 900), region = blob(r, 700), jump = blob(r, 64), set = Uint8Array.of(78, 1, 0, 0), py = lat('\x01print(1)\n\0');
    const copyOf = (o) => lat("\0# NumPlay's saves: keep this file\n" + Object.entries(o).map(([n, d]) => '#>' + n + ':' + b64(d)).join('\n') + '\n\0');
    const ownCopy = lat("\0# Doodle Jump's saves: keep this file\n#>doodlejump.sav:" + b64(jump) + '\n\0');
    const list = (files) => { const b = new Uint8Array(43008); for (const [n, d] of files) if (!S.write(b, n, d)) throw new Error('no room'); return b; };
    const full = [['pr.sys', blob(r, 30)], ['hello.py', py], ['numdash.nds', dash], ['nb1.nbw', world], ['nb1r0_0.nbe', region], ['numplay.set', set],
      [S.COPY, copyOf({ 'numdash.nds': dash, 'nb1.nbw': world, 'nb1r0_0.nbe': region, 'numplay.set': set })], ['doodlejump.sav', jump], ['doodlejump_saves.py', ownCopy]];
    const backup = S.holds(S.keep(list(full)), who);
    ok([...backup.keys()].sort().join() === 'doodlejump,numblocks,numdash,numplay,python', `a backup holds ${[...backup.keys()].sort()}`);
    ok(!backup.get('numplay').has(S.COPY) && backup.get('numplay').has('numplay.set'), "NumPlay's copy isn't something a backup holds");
    const cmp = (files) => { const mine = S.holds(S.keep(list(files)), who); return [...backup.keys()].sort().map((id) => id + ':' + S.compare(mine, backup, id)).join(); };
    ok(cmp(full) === 'doodlejump:same,numblocks:same,numdash:same,numplay:same,python:same', 'the calculator as it was backed up: all the same');
    ok(cmp([]) === 'doodlejump:missing,numblocks:missing,numdash:missing,numplay:missing,python:missing', 'after a reset: all missing');
    // after an update only the scripts are left: every save is in a copy, so nothing is missing
    ok(cmp(full.filter(([n]) => n.endsWith('.py'))) === 'doodlejump:same,numblocks:same,numdash:same,numplay:same,python:same', 'after an update (the scripts only): the saves in their copies count');
    ok(cmp(full.filter(([n]) => n !== 'nb1r0_0.nbe' && n !== S.COPY)) === 'doodlejump:same,numblocks:different,numdash:same,numplay:same,python:same', 'a game with a file missing is different, not the same');
    ok(cmp(full.map(([n, d]) => (n === 'numdash.nds' ? [n, blob(r, 180)] : n === S.COPY ? [n, copyOf({})] : [n, d]))).includes('numdash:different'), 'a save that changed is different');
    ok(cmp([...full, ['nb1r5_5.nbe', region]]).includes('numblocks:different'), 'a game with a file more is different');
    ok(cmp([...full.filter(([n]) => n !== 'hello.py'), ['other.py', py]]).includes('python:missing'), 'a script of yours missing, another there: missing (the other stays)');
    // put back: of each game the calculator then holds exactly what the backup does, the rest untouched
    for (let k = 0; k < 30 * facts.rounds; k++) {
      const some = full.filter(([n]) => n === 'pr.sys' || r() < 0.6).map(([n, d]) => (r() < 0.2 && n !== 'pr.sys' && !n.endsWith('.py') ? [n, blob(r, d.length)] : [n, d]));
      if (r() < 0.5) some.push(['nb1r9_9.nbe', blob(r, 300)], ['thing.xyz', blob(r, 20)], ['mine.py', py]);
      const before = list(some), ids = [...backup.keys()].filter(() => r() < 0.7);
      const res = S.put(before, ids.map((id) => S.putBack(before, backup, id, who)));
      if (!ok(!res.failed.length, `put back ${ids}: ${JSON.stringify(res.failed)}`)) continue;
      const mine = S.holds(S.keep(res.after), who);
      for (const id of ids) ok(S.compare(mine, backup, id) === 'same', `put back, ${id} is ${S.compare(mine, backup, id)}`);
      const was = new Map(S.files(before).map((f) => [f.name, f.data])), now = new Map(S.files(res.after).map((f) => [f.name, f.data]));
      for (const [name, data] of was) if (!ids.includes(who(name)) && name !== S.COPY) ok(now.has(name) && S.same(now.get(name), data), `put back ${ids}: ${name}, not theirs, changed`);
      ok(!was.has(S.COPY) || (now.has(S.COPY) && now.get(S.COPY).length === was.get(S.COPY).length), "NumPlay's copy keeps its size (only lines struck out)");
      planHolds(before, res.after, 'a backup put back');
    }
    // taken off: the game's files gone, its lines out of NumPlay's copy, so NumPlay doesn't bring it back
    const before = list(full), res = S.put(before, [S.takeOff(before, 'numblocks', who)]);
    const left = S.holds(S.keep(res.after), who);
    ok(!left.has('numblocks') && left.has('numdash') && left.has('doodlejump'), 'a game taken off is gone, files and copy, the others stay');
    ok(S.compare(left, backup, 'numblocks') === 'missing', 'and the backup has it to put back');
  }
  console.log('  a backup is compared and put back game by game');

  c.end();
  if (failed) { console.error(`saves.js: ${failed} failed (seed ${facts.seed})`); process.exit(1); }
}
main().catch((e) => { console.error(e); process.exit(1); });
