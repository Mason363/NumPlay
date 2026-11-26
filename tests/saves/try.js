/* NumPlay Studio's Saves without a calculator (docs/saves.md): a calculator that isn't one, with saves for the
 * first games of the kit, in place of USB. With the repository served (python3 -m http.server, from its root),
 * open /customize/?saves and run in the browser's console:
 *
 *   document.head.appendChild(Object.assign(document.createElement('script'), { src: '/tests/saves/try.js' }))
 *
 * then press "Connect your calculator". In the console, `fake` is the calculator (tests/saves/fake_calculator.js):
 *   fake.reset()                 a reset: every file gone (then fake.unplug(), fake.plug(): it is read again)
 *   fake.unplug(), fake.plug()   the cable
 *   fake.unplugAfter(120)        the cable pulled after 120 more requests: in the middle of the next change
 *   fake.files()                 its files now; fake.crashed: what the page did that a real one wouldn't survive
 * tryCalculator({ model: 'N0110', software: 'upsilon', layout: 'unknown', hidden: 9000 }) makes another: an older
 * model, Upsilon, a software whose files can't be moved safely, exam mode (see fake_calculator.js). */
'use strict';
(function () {
  const load = (src) => new Promise((ok, no) => document.head.appendChild(Object.assign(document.createElement('script'), { src, onload: ok, onerror: no })));
  const S = window.npSaves;
  const bytes = (n, seed) => { const b = new Uint8Array(n); let a = seed; for (let i = 0; i < n; i++) { a = a * 1103515245 + 12345 >>> 0; b[i] = a >>> 16 & (i % 3 ? 255 : 7); } return b; };
  const text = (t) => Uint8Array.from(t, (c) => c.charCodeAt(0));
  async function tryCalculator(opt) {
    if (!window.FakeCalculator) await load('/tests/saves/fake_calculator.js');
    const kit = await fetch('kit/kit.json').then((r) => r.json());
    const files = [{ name: 'pr.sys', data: bytes(40, 1) }, { name: 'hello.py', data: text('\x01print("hello")\n\0') }], copied = [];
    let seed = 2;
    for (const g of (kit.saves || []).slice(0, 9)) {
      const own = g.records.some((r) => r.endsWith('.py'));  // (a big game: its saves, and its own copy of them)
      const lines = [];
      for (const r of g.records.slice(0, 4)) {
        if (r.endsWith('.py')) continue;
        const name = r.endsWith('*') ? r.slice(0, -1) + '0_0.nbe' : r, data = bytes(60 + 37 * seed % 1900, seed++);
        files.push({ name, data });
        (own ? lines : copied).push('#>' + name + ':' + S.b64encode(data));
      }
      if (own) files.push({ name: g.records.find((r) => r.endsWith('.py')), data: text('\0# ' + g.title + "'s saves: keep this file\n" + lines.join('\n') + '\n\0') });
    }
    const set = Uint8Array.of(78, 1, 0, 0);  // (NumPlay's settings: in its copy too, as the launcher keeps them)
    files.push({ name: 'numplay.set', data: set });
    copied.unshift('#>numplay.set:' + S.b64encode(set));
    files.push({ name: S.COPY, data: text("\0# NumPlay's saves: keep this file\n" + copied.join('\n') + '\n\0') });
    window.fake = new window.FakeCalculator(Object.assign({ files, remembers: 'hello.py', serial: 'TRY0000000001234' }, opt));
    window.npSavesUi.use(window.fake.usb);
    console.log('A calculator that isn\'t one is plugged in: open Saves and press "Connect your calculator". It is `fake` here.');
    return window.fake;
  }
  window.tryCalculator = tryCalculator;
  tryCalculator();
})();
