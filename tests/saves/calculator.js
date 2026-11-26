/* NumPlay Studio's Saves on a real calculator, plugged into this computer (docs/saves.md): customize/usb.js and
 * saves.js themselves, over node's USB in place of the browser's.
 *
 *   node tests/saves/calculator.js            reads only: what it is, its files, read twice the same
 *   node tests/saves/calculator.js --write    then writes: its files kept in build/calculator/ first, test files
 *                                             (nptest*.sav) added after its own, rewritten, slid, the files filled
 *                                             to the last byte, and at the end its files as they were, to the byte
 *
 * The calculator is on, plugged in, out of any app (it shows "connected"), and nothing else has it (close NumWorks'
 * website). Each change is read back; its own files must not move or change, nor a byte after the files but
 * Epsilon's memory of the last one when a file moves. It stops at the first thing that isn't right, and says what.
 *
 * Needs the "usb" package: nwlink brings it (npm install -g nwlink), or `npm install usb`; NODE_PATH or
 * NUMPLAY_USB can say where it is. Not run by CI: it needs the calculator. */
'use strict';
const fs = require('fs'), os = require('os'), path = require('path'), crypto = require('crypto'), { execSync } = require('child_process');
const ROOT = path.join(__dirname, '..', '..');
const U = require(path.join(ROOT, 'customize', 'usb.js')), S = require(path.join(ROOT, 'customize', 'saves.js'));

function usbPackage() {
  const where = [process.env.NUMPLAY_USB, 'usb', path.join(os.homedir(), '.numplay', 'node_modules', 'usb')];
  try { const g = execSync('npm root -g', { stdio: ['ignore', 'pipe', 'ignore'] }).toString().trim(); where.push(path.join(g, 'usb'), path.join(g, 'nwlink', 'node_modules', 'usb')); } catch (e) { /* (no npm) */ }
  for (const w of where) { if (!w) continue; try { return require(w); } catch (e) { /* (not there) */ } }
  console.error('The "usb" package isn\'t installed: npm install -g nwlink (it brings it), or npm install usb');
  process.exit(2);
}
const rnd = (n) => new Uint8Array(crypto.randomBytes(n));
const names = (l) => S.files(l).map((f) => f.name + ':' + f.data.length).join(' ');

async function main() {
  const { WebUSB } = usbPackage(), usb = new WebUSB({ allowAllDevices: true });
  let t = Date.now();
  const calc = await U.known(usb, { timeout: 5000 });
  if (!calc) { console.error('No calculator: plug it in, turn it on and leave any app'); process.exit(2); }
  console.log(`${calc.model}, Epsilon ${calc.software}${calc.upsilon ? ' (Upsilon)' : ''}, ${calc.size} bytes of files, found in ${Date.now() - t} ms`);
  t = Date.now();
  let st = await calc.files();
  const ms = Date.now() - t, again = await calc.files();
  if (!st.files) throw new Error("its files don't make a list");
  if (!S.same(st.list, again.list)) throw new Error('two reads of its files differ');
  console.log(`its files, read in ${ms} ms (twice the same): ${S.end(st.list) + 2} bytes used; ` +
    (st.safe ? `Epsilon's memory of the last file found (${st.cache.ptr ? 'a file' : 'nothing'} remembered)` : "Epsilon's memory of the last file NOT found: files can be added, not moved") +
    (st.room !== calc.size ? '; EXAM MODE (its files are hidden)' : ''));
  for (const f of st.files) console.log('  ' + String(f.size).padStart(5) + '  ' + f.name);
  if (!process.argv.includes('--write')) { console.log('read only: --write to test changes too'); return calc.close(); }
  if (!st.safe || st.room !== calc.size) throw new Error('not written to: see above');
  if (st.files.some((f) => /^nptest\d\.sav$/.test(f.name))) throw new Error('test files from a run that stopped are on it: delete nptest*.sav first');

  // its files, kept before anything is written
  const dir = path.join(ROOT, 'build', 'calculator'), stamp = new Date().toISOString().replace(/[:.]/g, '-');
  fs.mkdirSync(dir, { recursive: true });
  fs.writeFileSync(path.join(dir, `files ${stamp}.bin`), st.list);
  fs.writeFileSync(path.join(dir, `NumPlay saves ${stamp}.json`), S.pack({ made: new Date().toISOString(), studio: 'calculator.js', calculator: { model: calc.model, software: calc.software, id: calc.serial.slice(-4) }, files: S.keep(st.list) }));
  console.log(`kept in build/calculator/ (the .json opens in Studio's Saves)`);

  const orig = st.list.slice(), want = st.after.slice();
  async function step(what, groups, check) {
    const res = S.put(st.list, groups);
    if (res.failed.length) throw new Error(`${what}: ${JSON.stringify(res.failed)}`);
    const plan = S.plan(st.list, res.after), cleared = plan.moves && st.cache.ptr, t0 = Date.now();
    const did = await calc.change(st.list, res.after), took = Date.now() - t0, now = await calc.files();
    if (!S.same(now.list, res.after)) throw new Error(`${what}: its files aren't the change`);
    for (const f of S.files(orig)) {
      const g = S.files(now.list).find((x) => x.name === f.name);
      if (!g || g.at !== f.at || !S.same(g.data, f.data)) throw new Error(`${what}: ${f.name}, its own, changed or moved`);
    }
    if (cleared) want.fill(0, st.cache.at, st.cache.at + 8);
    for (let i = 0; i < want.length; i++) if (now.after[i] !== want[i]) throw new Error(`${what}: byte ${i} after the files changed`);
    if (!now.safe || now.cache.at !== st.cache.at || (cleared && now.cache.ptr)) throw new Error(`${what}: Epsilon's memory of the last file isn't as it should be`);
    console.log('ok  ' + what.padEnd(47) + String(did.writes).padStart(2) + ' writes, ' + String(took).padStart(3) + ' ms' + (plan.moves ? ', a file moved' : '') + (cleared ? ", Epsilon's memory cleared" : '') + (check ? ' | ' + check(now) : ''));
    st = now;
  }
  const has = (n, len) => (now) => { const f = S.files(now.list).find((x) => x.name === n); if (!f || f.data.length !== len) throw new Error(n + ' is missing'); return n + ' is there'; };
  const gone = (n) => (now) => { if (S.files(now.list).some((x) => x.name === n)) throw new Error(n + ' is still there'); return n + ' is gone'; };
  const big = rnd(5000);
  await step('a small file added', [{ write: [{ name: 'nptest1.sav', data: rnd(100) }] }], has('nptest1.sav', 100));
  await step('rewritten in place', [{ write: [{ name: 'nptest1.sav', data: rnd(100) }] }], has('nptest1.sav', 100));
  await step('a 5000-byte file added (several blocks)', [{ write: [{ name: 'nptest2.sav', data: big }] }], has('nptest2.sav', 5000));
  await step('the first test file removed (the next slides)', [{ remove: ['nptest1.sav'] }], gone('nptest1.sav'));
  if (!S.same(S.files(st.list).find((f) => f.name === 'nptest2.sav').data, big)) throw new Error('nptest2.sav changed when it slid');
  await step('resized (taken out, added again)', [{ write: [{ name: 'nptest2.sav', data: rnd(1200) }] }], has('nptest2.sav', 1200));
  await step('two more added at once', [{ write: [{ name: 'nptest3.sav', data: rnd(300) }, { name: 'nptest4.sav', data: rnd(2500) }] }], has('nptest4.sav', 2500));
  await step('the first of three removed (two slide)', [{ remove: ['nptest2.sav'] }], gone('nptest2.sav'));
  await step('a file as big as what is left', [{ write: [{ name: 'nptest5.sav', data: rnd(st.list.length - S.end(st.list) - 2 - 2 - 12) }] }], (now) => `${S.end(now.list) + 2} of ${now.list.length} bytes used`);
  await step('the test files removed', [{ remove: ['nptest3.sav', 'nptest4.sav', 'nptest5.sav'] }]);
  await calc.change(st.list, orig);  // (what the test files left after the list's end is cleared too)
  const end = await calc.files();
  if (!S.same(end.list, orig)) throw new Error("its files aren't as they were");
  console.log('ok  its files as they were, to the byte: ' + names(end.list));
  console.log(`\n${calc.model}, Epsilon ${calc.software}: read, added to, rewritten, slid, filled and put back as it was. Unplug it and check it still`);
  console.log('works as usual (its scripts open), then note it in docs/saves.md.');
  await calc.close();
}
main().then(() => process.exit(0), (e) => { console.error('STOPPED: ' + (e.code ? e.code + ': ' : '') + e.message); process.exit(1); });
