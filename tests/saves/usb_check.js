/* customize/usb.js against a calculator that answers as Epsilon's DFU code does (tests/saves/fake_calculator.js;
 * tests/test_saves.py runs this).
 *
 * node usb_check.js FILES FACTS (as check.js; only FACTS' seed and rounds are used) */
'use strict';
const fs = require('fs'), path = require('path');
const S = require(path.join(__dirname, '..', '..', 'customize', 'saves.js'));
const U = require(path.join(__dirname, '..', '..', 'customize', 'usb.js'));
const { FakeCalculator } = require('./fake_calculator.js');
const facts = JSON.parse(fs.readFileSync(process.argv[3], 'utf8'));

let failed = 0;
const ok = (cond, what) => { if (!cond) { failed++; console.error('  FAIL ' + what); } return !!cond; };
function rng(seed) {
  let a = seed >>> 0;
  return () => { a = a + 0x6D2B79F5 >>> 0; let t = Math.imul(a ^ a >>> 15, 1 | a); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; };
}
const r = rng(facts.seed ^ 0x55AA);
const blob = (n) => { const b = new Uint8Array(n); for (let i = 0; i < n; i++) b[i] = r() * 256; return b; };
const OPT = { timeout: 300 };
const code = async (p) => { try { await p; return null; } catch (e) { return e.code || ('?' + e.message); } };
const names = (list) => (list || []).map((f) => f.name).join();
const FILES = () => [
  { name: 'pr.sys', data: blob(40) }, { name: 'hello.py', data: blob(300) }, { name: 'numplay.set', data: blob(4) },
  { name: 'numdash.nds', data: blob(700) }, { name: 'nb1.nbw', data: blob(5000) }, { name: 'nb1r0_0.nbe', data: blob(2600) },
  { name: 'balatro.sav', data: blob(64) }, { name: 'numplay_saves.py', data: blob(9000) }, { name: 'tetris.sav', data: blob(30) },
];
// a change like Saves makes: some files out, some in or rewritten
function someChange(list) {
  const have = S.files(list).map((f) => f.name).filter((n) => n !== 'pr.sys'), g = { remove: [], write: [] };
  for (let k = 1 + (r() * 3 | 0); k > 0; k--) {
    if (r() < 0.4 && have.length) g.remove.push(have[r() * have.length | 0]);
    else g.write.push({ name: r() < 0.5 && have.length ? have[r() * have.length | 0] : 'new' + (r() * 5 | 0) + '.sav', data: blob(r() < 0.3 ? r() * 9000 | 0 : r() * 200 | 0) });
  }
  g.write = g.write.filter((w) => !g.remove.includes(w.name));
  return S.put(list, [g]).after;
}
// nothing but the files, and the 8 bytes of Epsilon's memory of the last one, ever changes
function onlyFilesTouched(f, before, what) {
  const now = f.around(), [c0, c1] = f.cacheSpan;
  let same = S.same(before.before, now.before);
  for (let i = 0; i < now.after.length && same; i++) if (now.after[i] !== before.after[i] && !(i >= c0 && i < c1)) same = false;
  ok(same, what + ": memory that isn't the files was written");
}

const VARIANTS = [
  { model: 'N0110' }, { model: 'N0115' }, { model: 'N0120' },
  { model: 'N0120', pointer: 'sticky' },                 // the DFU code before the address pointer's slip
  { model: 'N0110', header: 'short', version: '18.2.3' },  // an older header, without the device name
  { model: 'N0120', slot: false },                       // no slot info: the one header there is
  { model: 'N0110', slot: false, slots: 2 },             // ...or two that say the same
  { model: 'N0120', names: false },                      // a system that doesn't give the settings' names
  { model: 'N0110', software: 'upsilon' }, { model: 'N0115', software: 'upsilon', slot: false },
];

async function main() {
  console.log('usb.js');

  // 1. found and read, whatever the model and the software; changed as asked, and only its files
  for (const v of VARIANTS) {
    const what = JSON.stringify(v), files = FILES();
    for (const remembers of [null, 'hello.py', 'numplay_saves.py', 'tetris.sav']) {
      const f = new FakeCalculator({ ...v, files, remembers }), c = await U.ask(f.usb, OPT).catch((e) => e);
      if (!ok(!(c instanceof Error), `${what}: not found (${c.code}: ${c.message})`)) break;
      ok(c.model === v.model && c.software === (v.version || (v.software === 'upsilon' ? '15.5.0' : '25.2.0')) && c.upsilon === (v.software === 'upsilon') && c.size === 43008,
        `${what}: told as ${c.model}, ${c.software}, upsilon ${c.upsilon}, ${c.size} bytes`);
      let st = await c.files();
      ok(S.same(st.list, f.list()) && names(st.files) === names(files) && st.safe && st.room === 43008, `${what}: its files read`);
      ok((st.cache.ptr !== 0) === !!remembers, `${what}: what Epsilon remembers (${remembers}) is seen`);
      const around = f.around();
      for (let k = 0; k < 4 * facts.rounds; k++) {
        const after = someChange(st.list), err = await code(c.change(st.list, after));
        if (!ok(!err, `${what}: a change failed (${err})`)) break;
        ok(S.same(f.list(), after), `${what}: the calculator's files are the change`);
        ok(!f.memoryHolds(), `${what}: Epsilon is left remembering ${f.memoryHolds()}`);
        st = await c.files();
      }
      onlyFilesTouched(f, around, what);
      ok(!f.crashed, `${what}: ${f.crashed}`);
      ok(f.log.every((l) => l[0] === 'alt' || (l[0] === 'out' ? l[3] <= 2048 : l[3] <= 2048)), `${what}: a transfer of more than 2048 bytes`);
      await c.close();
    }
  }
  console.log(`  found, read and changed on ${VARIANTS.length} kinds of calculator and software`);

  // 2. the cable pulled after any number of requests: the files are whole (each as it was or as it will be), the
  //    list is one, Epsilon remembers nothing that moved, and plugged back in the change goes through
  {
    let pulls = 0;
    for (const remembers of [null, 'tetris.sav', 'numdash.nds']) {
      for (let k = 0; k < 2 * facts.rounds; k++) {
        const files = FILES(), probe = new FakeCalculator({ files, remembers });
        const pc = await U.ask(probe.usb, OPT), pst = await pc.files(), after = someChange(pst.list);
        const start = probe.requests;
        await pc.change(pst.list, after);
        const total = probe.requests - start;
        const A = new Map(S.files(pst.list).map((x) => [x.name, x.data])), B = new Map(S.files(after).map((x) => [x.name, x.data]));
        for (let n = 0; n < total; n += 1 + (total > 150 ? r() * 3 | 0 : 0)) {
          const f = new FakeCalculator({ files, remembers }), c = await U.ask(f.usb, OPT), st = await c.files();
          f.unplugAfter(n);
          const err = await code(c.change(st.list, after));
          pulls++;
          if (!ok(err === 'gone', `pulled after ${n} of ${total} requests: the change said ${err}`)) break;
          const list = f.files();
          if (!ok(list, `pulled after ${n} of ${total} requests: the list is broken`)) break;
          let whole = true;
          for (const x of list) if (!((A.has(x.name) && S.same(A.get(x.name), x.data)) || (B.has(x.name) && S.same(B.get(x.name), x.data)))) whole = false;
          ok(whole, `pulled after ${n} of ${total} requests: a file is neither as it was nor as it will be`);
          ok(!f.memoryHolds(), `pulled after ${n} of ${total} requests: Epsilon is left remembering ${f.memoryHolds()}`);
          ok(!f.crashed, `pulled after ${n}: ${f.crashed}`);
          // back in: the backup made before (the files as they were read) and the change again give the change
          f.plug();
          const c2 = await U.known(f.usb, OPT), st2 = await c2.files();
          const want = S.put(st2.list, [{ remove: S.files(st2.list).map((x) => x.name).filter((x) => x !== 'pr.sys' && !B.has(x)),
            write: [...B].filter(([name]) => name !== 'pr.sys').map(([name, data]) => ({ name, data })) }]).after;
          const err2 = await code(c2.change(st2.list, want));
          if (!ok(!err2, `plugged back in after ${n}: the change again said ${err2}`)) break;
          const end = new Map(f.files().map((x) => [x.name, x.data]));
          ok(end.size === B.size && [...B].every(([name, data]) => end.has(name) && S.same(end.get(name), data)), `plugged back in after ${n}: the files aren't the change`);
          await c2.close();
        }
      }
    }
    console.log(`  the cable pulled at ${pulls} moments: whole files every time, and the change again goes through`);
  }

  // 3. what must not be done isn't
  {
    // Epsilon's memory of the last file isn't where it is known to be: files are added and rewritten, none is moved
    let f = new FakeCalculator({ files: FILES(), layout: 'unknown', remembers: 'hello.py' }), c = await U.ask(f.usb, OPT), st = await c.files();
    ok(!st.safe, 'an unknown software: its memory of the last file is not found');
    let after = S.put(st.list, [{ write: [{ name: 'added.sav', data: blob(4000) }] }]).after;
    ok(!(await code(c.change(st.list, after))) && S.same(f.list(), after), 'an unknown software: a file is added');
    st = await c.files();
    after = S.put(st.list, [{ write: [{ name: 'tetris.sav', data: blob(30) }] }]).after;
    ok(!(await code(c.change(st.list, after))) && S.same(f.list(), after), 'an unknown software: a file is rewritten in place');
    st = await c.files();
    let before = f.list();
    after = S.put(st.list, [{ remove: ['numdash.nds'] }]).after;
    ok(await code(c.change(st.list, after)) === 'moves' && S.same(f.list(), before), 'an unknown software: a file is not moved, and nothing is written');
    ok(!f.crashed, 'an unknown software: ' + f.crashed);

    // exam mode hides the files: nothing is written
    f = new FakeCalculator({ files: [{ name: 'pr.sys', data: blob(40) }], hidden: 9000 }); c = await U.ask(f.usb, OPT); st = await c.files();
    ok(st.room === 43008 - 9000, 'exam mode: seen');
    before = f.list();
    after = S.put(st.list, [{ write: [{ name: 'a.sav', data: blob(10) }] }]).after;
    ok(await code(c.change(st.list, after)) === 'exam' && S.same(f.list(), before), 'exam mode: nothing is written');

    // the files changed since they were read (another page, the calculator used in between): nothing is written
    f = new FakeCalculator({ files: FILES() }); c = await U.ask(f.usb, OPT); st = await c.files();
    after = S.put(st.list, [{ write: [{ name: 'a.sav', data: blob(10) }] }]).after;
    const other = S.put(st.list, [{ write: [{ name: 'other.sav', data: blob(10) }] }]).after;
    f.setList(other);
    ok(await code(c.change(st.list, after)) === 'stale' && S.same(f.list(), other), 'files that changed since they were read are left as they are');

    // a write that does nothing and says it did (as Epsilon's DFU code can): seen when read back
    f = new FakeCalculator({ files: FILES() }); c = await U.ask(f.usb, OPT); st = await c.files();
    f.dropWrites = true;
    ok(await code(c.change(st.list, someChange(st.list))) === 'changed', 'a write that did nothing is seen');

    // no magic where the files should be; a list that isn't one
    f = new FakeCalculator({ files: FILES() }); c = await U.ask(f.usb, OPT);
    const bad = f.list(); bad[0] = 1; bad[1] = 0;  // (a file of one byte: no list)
    f.setList(bad);
    st = await c.files();
    ok(st.files === null && !st.safe, 'a list that is broken is told, not read as files');
    ok(await code((async () => S.plan(st.list, st.list))()) !== null, 'a broken list is not changed');

    // an N0100, a device with no DFU interface, something else holding it, no pick, no answer
    f = new FakeCalculator({ model: 'N0100' });
    ok(await code(U.ask(f.usb, OPT)) === 'software', 'an N0100 is refused');
    f = new FakeCalculator({}); f.busy = true;
    ok(await code(U.ask(f.usb, OPT)) === 'busy', 'a calculator something else holds: busy');
    f = new FakeCalculator({}); f.pick = false;
    ok(await code(U.ask(f.usb, OPT)) === 'none', 'the picker closed: none');
    f = new FakeCalculator({});
    ok(await U.known(f.usb, OPT) === null, 'a calculator this page was never given is not opened');
    f.authorise();
    c = await U.known(f.usb, OPT);
    ok(c && c.model === 'N0120', 'one it was given is');
    f.unplug();
    ok(await code(c.files()) === 'gone', 'unplugged: gone');
    f = new FakeCalculator({});
    c = await U.ask(f.usb, OPT);
    const hang = f.device.controlTransferIn;
    f.device.controlTransferIn = () => new Promise(() => {});
    ok(await code(c.files()) === 'gone', "a calculator that stops answering doesn't hang the page");
    f.device.controlTransferIn = hang;
    // a full calculator: what doesn't fit isn't written, by saves.js's put (one group fails, the list is as it was)
    f = new FakeCalculator({ files: [{ name: 'big.sav', data: blob(40000) }] }); c = await U.ask(f.usb, OPT); st = await c.files();
    const res = S.put(st.list, [{ write: [{ name: 'more.sav', data: blob(5000) }] }]);
    ok(res.failed.length === 1 && res.failed[0].why === 'room' && S.same(res.after, st.list), 'a file with no room for it is not written');
  }
  console.log("  what can't be done safely is refused, with nothing written");

  if (failed) { console.error(`usb.js: ${failed} failed (seed ${facts.seed})`); process.exit(1); }
}
main().catch((e) => { console.error(e); process.exit(1); });
