/* NumPlay Studio: Saves, the tab. Your calculator's saves, game by game: backed up on this computer each time
 * it is plugged in, and put back after an update or a reset emptied it. docs/saves.md tells all of it.
 *
 * saves.js reads and changes the calculator's files, usb.js carries them. Here:
 * - the calculator's files, grouped by game (kit.json's "saves": every game's files, from games/games.json), with
 *   NumPlay's own, your Python scripts and other apps' files;
 * - a backup each time the files read aren't the last backup's: every file but Epsilon's own, whether or not this
 *   page knows whose it is, kept in the browser (IndexedDB) and yours to download as a file;
 * - putting a backup back, game by game: what a game holds is its files and the saves that are only in a copy of
 *   them (a script), so a save waiting in its copy isn't taken for a missing one. A game put back has exactly the
 *   backup's files; NumPlay's copy is never written (NumPlay makes it again), only its lines for files that go
 *   are struck out;
 * - nothing written to a calculator whose files weren't backed up a moment before. */
'use strict';
(function (root) {
  const S = root.npSaves, U = root.npUsb;
  const KIT = 'kit/', KEEP = 40;  // backups kept for each calculator (the oldest go first)
  const ICON = {
    numplay: KIT + 'variants/NumPlay.png',
    python: 'data:image/svg+xml,' + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 56 56"><rect width="56" height="56" rx="12" fill="#e3e1dd"/><path d="M19 22l-7 6 7 6M37 22l7 6-7 6M31 17l-6 22" fill="none" stroke="#6b6862" stroke-width="3.4" stroke-linecap="round" stroke-linejoin="round"/></svg>'),
    other: 'data:image/svg+xml,' + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 56 56"><rect width="56" height="56" rx="12" fill="#e3e1dd"/><path d="M19 13h13l7 7v23H19z M32 13v7h7" fill="none" stroke="#6b6862" stroke-width="3.2" stroke-linejoin="round"/></svg>'),
  };
  const SVG = {
    down: '<svg viewBox="0 0 24 24" width="17" height="17" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M12 4v11"/><path d="M7 10l5 5 5-5"/><path d="M5 20h14"/></svg>',
    bin: '<svg viewBox="0 0 24 24" width="17" height="17" fill="none" stroke="currentColor" stroke-width="2.1" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M4 7h16"/><path d="M9 7V4h6v3"/><path d="M6.5 7l1 13h9l1-13"/></svg>',
    ok: '<svg viewBox="0 0 12 12" width="12" height="12" aria-hidden="true"><path d="M2.5 6.2l2.3 2.3 4.7-5" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"/></svg>',
  };

  let kit = null, toast = () => {}, usb = null, who = () => 'other';
  let calc = null, state = null, kept = [];  // the calculator, its files as last read, those a backup keeps
  let backups = [], db = null;               // newest first; null: the browser keeps nothing (a private window)
  let ready = Promise.resolve();             // the backups are loaded (before any is made: no backup twice)
  let view = null, busy = '', note = null, problem = null, opened = false;
  let asking = null;  // a question before something goes: { key, text, yes, go }, asked in place of its row

  const $ = (s) => document.querySelector(s);
  const el = (tag, cls, txt) => { const e = document.createElement(tag); if (cls) e.className = cls; if (txt !== undefined) e.textContent = txt; return e; };
  const kb = (n) => (n < 1024 ? n + ' B' : (n / 1024).toFixed(n < 10240 ? 1 : 0) + ' KB');
  const plural = (n, one, many) => n + ' ' + (n === 1 ? one : many || one + 's');
  const tail = (serial) => (serial ? '··' + String(serial).slice(-4) : '');
  const pad = (n) => String(n).padStart(2, '0');
  function when(ms) {
    const d = new Date(ms), now = new Date(), day = (x) => new Date(x.getFullYear(), x.getMonth(), x.getDate()).getTime();
    const days = Math.round((day(now) - day(d)) / 864e5), hm = pad(d.getHours()) + ':' + pad(d.getMinutes());
    if (days === 0) return (Date.now() - ms < 90000 ? 'Just now' : 'Today ' + hm);
    if (days === 1) return 'Yesterday ' + hm;
    return d.toLocaleDateString('en', { month: 'short', day: 'numeric', year: d.getFullYear() === now.getFullYear() ? undefined : 'numeric' }) + ', ' + hm;
  }
  const stamp = (ms) => { const d = new Date(ms); return d.getFullYear() + '-' + pad(d.getMonth() + 1) + '-' + pad(d.getDate()) + ' ' + pad(d.getHours()) + '.' + pad(d.getMinutes()); };

  // ------------------------------------------------------------------ whose files, and what they hold
  const title = (id) => (id === 'numplay' ? 'NumPlay' : id === 'python' ? 'Your Python scripts' : id === 'other' ? (kit.saves ? "Other apps' files" : "Games' saves") :
    (kit.byId && kit.byId[id] && kit.byId[id].title) || ((kit.saves || []).find((g) => g.id === id) || {}).title || id);
  const icon = (id) => ICON[id] || (kit.byId && kit.byId[id] && kit.byId[id].icon ? KIT + 'icons/' + id + '.png' : ICON.other);
  const isGame = S.isGame;
  const rank = (id) => { const i = (kit.saves || []).findIndex((g) => g.id === id); return i >= 0 ? i : id === 'numplay' ? 1e6 : id === 'python' ? 1e6 + 1 : 1e6 + 2; };
  const inOrder = (map) => new Map([...map].sort((x, y) => rank(x[0]) - rank(y[0])));  // (the games as NumPlay has them, then the rest)
  // what files hold, by owner (saves.js's holds: the saves that are only in a copy count)
  const holds = (files) => inOrder(S.holds(files, who));
  // the files themselves, by owner: Map id -> [{ name, data }] (the calculator's list: what takes its room)
  function owned(files) {
    const out = new Map();
    for (const f of files) { const id = who(f.name); if (!id) continue; if (!out.has(id)) out.set(id, []); out.get(id).push(f); }
    return inOrder(out);
  }
  const bytesOf = (list) => list.reduce((n, f) => n + f.name.length + 3 + f.data.length, 0);
  const digest = S.holding;  // (what tells a backup from the next: the saves, not the copy NumPlay makes of them)

  // ------------------------------------------------------------------ the backups, in the browser
  function openDb() {
    return new Promise((ok) => {
      let req;
      try { req = indexedDB.open('numplay-saves', 1); } catch (e) { return ok(null); }
      req.onupgradeneeded = () => req.result.createObjectStore('backups', { keyPath: 'id', autoIncrement: true });
      req.onsuccess = () => ok(req.result);
      req.onerror = req.onblocked = () => ok(null);
    });
  }
  const tx = (mode, run) => new Promise((ok, no) => {
    if (!db) return ok(null);
    const t = db.transaction('backups', mode), r = run(t.objectStore('backups'));
    t.oncomplete = () => ok(r && r.result);
    t.onerror = t.onabort = () => no(t.error);
  });
  async function loadBackups() {
    db = await openDb();
    let all = [];
    try { all = (await tx('readonly', (s) => s.getAll())) || []; } catch (e) { db = null; }
    backups = all.filter((b) => b && Array.isArray(b.files)).sort((a, b) => b.made - a.made);
  }
  async function keepBackup(b) {
    b.digest = digest(b.files);
    b.size = bytesOf(b.files);
    try { b.id = (await tx('readwrite', (s) => s.add(b))) || -Date.now(); } catch (e) { b.id = -Date.now(); db = null; }
    backups.unshift(b);
    // the oldest of that calculator's go, past KEEP (never one you opened from a file)
    const mine = backups.filter((x) => x.serial === b.serial && !x.from);
    for (const old of mine.slice(KEEP)) await dropBackup(old);
    if (db && navigator.storage && navigator.storage.persist) navigator.storage.persist().catch(() => {});
    return b;
  }
  async function dropBackup(b) {
    backups = backups.filter((x) => x !== b);
    try { await tx('readwrite', (s) => s.delete(b.id)); } catch (e) { /* (gone from the list anyway) */ }
  }
  // the calculator's files as a backup, if they aren't the last one's (nothing to keep: none)
  async function backUp() {
    await ready;
    if (!state || !state.files || !kept.length) return null;
    const last = backups.find((b) => b.serial === calc.serial && !b.from);
    if (last && last.digest === digest(kept)) return last;
    return keepBackup({ made: Date.now(), serial: calc.serial, model: calc.model, software: calc.software, studio: kit.version || null, files: kept });
  }
  function download(b, only) {
    const files = only ? b.files.filter((f) => who(f.name) === only) : b.files;
    const body = S.pack({ made: new Date(b.made).toISOString(), studio: b.studio, calculator: { model: b.model || null, software: b.software || null, id: String(b.serial || '').slice(-4) }, files });
    const a = el('a');
    a.download = (only ? title(only) + ' save ' : 'NumPlay saves ') + stamp(b.made) + '.json';
    a.href = URL.createObjectURL(new Blob([body], { type: 'application/json' }));
    document.body.appendChild(a);
    a.click();
    setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 2000);
  }

  // ------------------------------------------------------------------ the calculator
  const usbNow = () => usb || (typeof navigator !== 'undefined' && navigator.usb) || null;
  // (what a request that failed says is for the console: here, what to do)
  const say = (e) => (e && e.code === 'refused' ? "The calculator didn't take a request. Unplug it, plug it back in and try again." :
    e && e.code ? e.message : 'Something went wrong. Unplug the calculator, plug it back in and try again.');
  async function run(what, task) {
    if (busy) return;
    busy = what; problem = null; note = null;
    render();
    try { await task(); } catch (e) {
      console.error(e);
      const read = !!(state && state.files);
      if (e && e.code === 'gone') { drop(); problem = read ? 'The calculator was unplugged. Plug it back in. Every file it had is in the backup made a moment ago.' : 'The calculator was unplugged. Plug it back in.'; }
      else if (!(e && e.code === 'none')) problem = say(e);
    }
    busy = '';
    if (!opened) drop();  // (the tab was left meanwhile: the calculator is let go)
    render();
  }
  function drop() {
    const c = calc;
    calc = null; state = null; kept = []; asking = null;
    if (c) { c.lost(); c.close().catch(() => {}); }
    if (view && view.name === 'put') view = null;
  }
  // the calculator's files read, and backed up; the backup of them (null: nothing to keep)
  async function refresh() {
    state = await calc.files();
    kept = state.files ? S.keep(state.list) : [];
    return backUp();
  }
  const connect = (ask) => run('Looking for your calculator', async () => {
    const u = usbNow();
    if (!u) return;
    calc = ask ? await U.ask(u) : await U.known(u);
    if (calc) await refresh();
  });
  // A change cut short (the cable pulled): the backup from before it is remembered, by its calculator, until a
  // change goes through or the calculator holds that backup's saves again, and offered when it is plugged back in
  const PENDING = 'numplay-saves-cut';
  function cut(v) {
    try {
      if (v === undefined) return JSON.parse(localStorage.getItem(PENDING) || 'null');
      if (v) localStorage.setItem(PENDING, JSON.stringify(v)); else localStorage.removeItem(PENDING);
    } catch (e) { /* (no storage: nothing is offered) */ }
    return null;
  }
  function cutBackup() {
    const c = cut();
    const b = c && calc && c.serial === calc.serial ? backups.find((x) => x.id === c.id) : null;
    if (b && state && state.files && b.digest === digest(kept)) { cut(null); return null; }
    return b || null;
  }
  // Changes on the calculator, group by group (make: the calculator's files, read a moment before, to saves.js's
  // groups): the ones that would move a file on a software where that can't be done safely are left out and told.
  // Returns the groups that didn't go: [{ id, why }].
  async function change(make) {
    const before = await refresh();  // (the files as they are now, and their backup)
    if (!state.files) throw Object.assign(new Error("The calculator's files can't be read. Nothing was changed."), { code: 'files' });
    let list = state.list;
    const left = [];
    for (const g of make(list)) {
      const res = S.put(list, [g]);
      if (res.failed.length) { left.push({ id: g.id, why: res.failed[0].why }); continue; }
      if (!state.safe && S.plan(list, res.after).moves) { left.push({ id: g.id, why: 'moves' }); continue; }
      list = res.after;
    }
    if (before) cut({ serial: calc.serial, id: before.id });
    await calc.change(state.list, list);
    cut(null);
    await refresh();
    return left;
  }
  const WHY = { room: (t) => "There isn't room on the calculator for " + t, name: (t) => t + " has a file this page won't write",
    moves: (t) => t + " can't be changed safely on this calculator's software", list: () => "The calculator's files couldn't be read" };
  const tell = (left) => left.map((l) => (WHY[l.why] || WHY.list)(title(l.id))).join('. ');

  // ------------------------------------------------------------------ the tab
  function button(cls, label, on, tip) {
    const b = el('button', cls);
    b.type = 'button';
    if (/^</.test(label)) b.innerHTML = label; else b.textContent = label;
    if (tip) { b.setAttribute('aria-label', tip); b.dataset.tip = tip; }
    if (busy) b.disabled = true;
    b.addEventListener('click', on);
    return b;
  }
  // the question in place of a row: what goes, and the two answers
  function question() {
    const r = el('div', 'sv-row sv-ask'), a = asking;
    r.appendChild(el('span', 'sv-q', a.text));
    r.append(button('sv-btn sv-no', 'Keep it', () => { asking = null; render(); }), button('sv-btn sv-yes', a.yes, () => { asking = null; a.go(); }));
    return r;
  }
  const ask = (key, text, yes, go) => { asking = { key, text, yes, go }; problem = note = null; render(); };
  function row(id, sub, right) {
    const r = el('div', 'sv-row'), img = el('img');
    img.src = icon(id); img.alt = ''; img.width = img.height = 34;
    img.addEventListener('error', () => { img.src = ICON.other; }, { once: true });
    const t = el('div', 'sv-name');
    t.appendChild(el('b', '', title(id)));
    if (sub) t.appendChild(el('span', '', sub));
    r.append(img, t);
    if (right) r.append(...[].concat(right));
    return r;
  }
  function render() {
    const body = $('#saves-body');
    if (!body || !kit) return;
    body.textContent = '';
    $('#saves-title').textContent = view && view.name === 'put' ? 'Put back from ' + mid(label(view.backup)) : 'Your saves';
    if (view && view.name === 'put') return renderPut(body);
    const u = usbNow();
    // the calculator
    const box = el('section', 'sv-calc');
    if (!calc) {
      box.appendChild(el('p', 'sv-lede', 'Your saves are files on your calculator, and installing apps or a reset empties them. Plug it in to back them up here, and to put them back.'));
      if (u) {
        box.appendChild(button('install wide', busy || 'Connect your calculator', () => connect(true)));
        box.appendChild(el('p', 'sv-hint', 'With its USB cable, and out of NumPlay (press the Home key). In exam mode it stays closed.'));
      } else box.appendChild(el('p', 'sv-warn', "This browser can't use USB. Open this page in Chrome or Edge, on a computer or an Android phone."));
    } else {
      const head = el('div', 'sv-head');
      head.appendChild(el('b', '', (calc.model || 'NumWorks') + ' ' + tail(calc.serial)));
      head.appendChild(el('span', '', (calc.upsilon ? 'Upsilon, Epsilon ' : 'Epsilon ') + calc.software));
      box.appendChild(head);
      if (!state || !state.files) box.appendChild(el('p', 'sv-warn', "Its files can't be read. Nothing was changed. Turn the calculator off and on, then plug it in again."));
      else renderFiles(box);
    }
    if (busy && calc) box.appendChild(el('p', 'sv-busy', busy + '…'));
    if (problem) box.appendChild(el('p', 'sv-warn', problem));
    if (note) box.appendChild(el('p', 'sv-note', note));
    body.appendChild(box);
    // the backups
    const sec = el('section', 'sv-backups'), h = el('div', 'sv-sub');
    h.appendChild(el('h3', '', 'Your backups, kept in this browser'));
    const file = el('label', 'sv-link', 'Open a file');
    const inp = el('input');
    inp.type = 'file'; inp.accept = '.json,.py,application/json,text/x-python'; inp.hidden = true;
    inp.addEventListener('change', () => { const f = inp.files[0]; inp.value = ''; if (f) openFile(f); });
    file.appendChild(inp);
    h.appendChild(file);
    sec.appendChild(h);
    if (!backups.length) sec.appendChild(el('p', 'sv-hint', 'None yet. One is made each time you plug your calculator in here.'));
    const shown = view && view.name === 'all' ? backups : backups.slice(0, 4);
    for (const b of shown) {
      const games = [...holds(b.files).keys()].filter(isGame).length;
      const line = el('div', 'sv-backup'), t = el('div', 'sv-name');
      t.appendChild(el('b', '', label(b)));
      t.appendChild(el('span', '', [b.from ? 'from ' + b.from : (b.model || 'NumWorks') + ' ' + tail(b.serial), games ? plural(games, 'game') : plural(b.files.length, 'file'), kb(b.size || bytesOf(b.files))].join(' · ')));
      line.append(t, button('sv-btn', 'Put back', () => { view = { name: 'put', backup: b, picks: null }; asking = null; problem = note = null; render(); }),
        button('sv-icon', SVG.down, () => download(b), 'Download this backup'),
        button('sv-icon', SVG.bin, () => ask(b, 'Delete this backup? It is only kept here.', 'Delete', () => dropBackup(b).then(render)), 'Delete this backup'));
      sec.appendChild(asking && asking.key === b ? question() : line);
    }
    if (backups.length > shown.length) sec.appendChild(button('sv-link', 'All ' + backups.length, () => { view = { name: 'all' }; render(); }));
    if (!db && backups.length) sec.appendChild(el('p', 'sv-warn', "This window doesn't keep backups. It may be a private one. Download the ones you want."));
    else if (backups.length) sec.appendChild(el('p', 'sv-hint', 'Clearing this browser\'s data clears them, so download the ones that matter.'));
    body.appendChild(sec);
  }
  const label = (b) => when(b.made);
  const mid = (t) => (/^(Just|Today|Yesterday)/.test(t) ? t[0].toLowerCase() + t.slice(1) : t);  // (inside a sentence)
  function renderFiles(box) {
    const used = S.end(state.list) + 2, groups = owned(kept), content = holds(kept);
    const meter = el('div', 'sv-meter'), fill = el('i');
    fill.style.width = Math.min(100, 100 * used / calc.size) + '%';
    meter.appendChild(fill);
    box.appendChild(meter);
    const last = backups.find((b) => b.serial === calc.serial && !b.from);
    const line = el('p', 'sv-status');
    line.appendChild(el('span', '', kb(used) + ' of ' + kb(calc.size) + ' used'));
    if (last && last.digest === digest(kept)) { const okay = el('span', 'sv-ok'); okay.innerHTML = SVG.ok; okay.append(' Backed up'); line.appendChild(okay); }
    box.appendChild(line);
    if (state.room !== calc.size) box.appendChild(el('p', 'sv-warn', 'The calculator is in exam mode, which hides its files. Nothing can be put back until it is off.'));
    const undone = cutBackup();
    if (undone && !busy) {
      const w = el('div', 'sv-row sv-ask');
      w.appendChild(el('span', 'sv-q', 'The last change was cut short. The backup from before it has every file.'));
      w.append(button('sv-btn sv-no', 'Leave it', () => { cut(null); render(); }), button('sv-btn sv-yes', 'Put it back', () => { view = { name: 'put', backup: undone, picks: 'all' }; render(); }));
      box.appendChild(w);
    }
    const list = el('div', 'sv-list');
    const ids = new Set([...groups.keys(), ...content.keys()]);
    for (const id of [...ids].sort((a, b) => rank(a) - rank(b))) {
      const files = groups.get(id) || [], held = content.get(id) || new Map();
      const waiting = [...held.keys()].filter((n) => !files.some((f) => f.name === n)).length;
      const parts = files.length ? [kb(bytesOf(files))] : [];
      if (id === 'numplay') parts.push(files.some((f) => f.name === S.COPY) ? 'its settings, and its copy of your saves' : 'its settings');
      else if (id === 'python' || id === 'other') parts.push(plural(files.length, 'file'));
      if (waiting && isGame(id)) parts.push(files.length ? 'more in its copy' : 'in its copy, back when you open it');
      const sub = parts.join(' · ');
      const acts = [];
      if (isGame(id) && files.length) {
        acts.push(button('sv-icon', SVG.down, () => download({ made: Date.now(), serial: calc.serial, model: calc.model, software: calc.software, studio: kit.version, files: kept }, id), 'Download ' + title(id) + "'s save"));
        acts.push(button('sv-icon', SVG.bin, () => erase(id), 'Delete ' + title(id) + "'s save from the calculator"));
      }
      list.appendChild(asking && asking.key === id ? question() : row(id, sub, acts));
    }
    if (!ids.size) {
      const empty = el('p', 'sv-hint', 'No save on this calculator.');
      const mine = backups.find((b) => b.serial === calc.serial && [...holds(b.files).keys()].some(isGame));
      list.appendChild(empty);
      if (mine) list.appendChild(button('install wide', 'Put back the backup of ' + mid(label(mine)), () => { view = { name: 'put', backup: mine, picks: null }; render(); }));
    }
    box.appendChild(list);
  }
  const erase = (id) => ask(id, 'Delete ' + title(id) + "'s save from the calculator? It stays in your backups here.", 'Delete', () => {
    run('Deleting', async () => {
      const left = await change((list) => [S.takeOff(list, id, who)]);
      if (left.length) problem = tell(left) + '.'; else note = title(id) + "'s save is off the calculator.";
    });
  });
  // putting a backup back: its games against the calculator's, the ones to put back ticked
  function renderPut(body) {
    const b = view.backup, theirs = holds(b.files), mine = calc && state && state.files ? holds(kept) : null;
    if (!mine) {
      body.appendChild(el('p', 'sv-lede', 'Plug your calculator in to put this backup back on it.'));
      if (usbNow()) body.appendChild(button('install wide', busy || 'Connect your calculator', () => connect(true)));
      else body.appendChild(el('p', 'sv-warn', "This browser can't use USB. Open this page in Chrome or Edge, on a computer or an Android phone."));
    } else {
      const status = new Map([...theirs.keys()].map((id) => [id, S.compare(mine, theirs, id)]));
      // (ticked: what the calculator doesn't have; after a change cut short, everything that isn't as the backup has it)
      if (!view.picks || view.picks === 'all') view.picks = new Set([...status].filter(([, s]) => (view.picks === 'all' ? s !== 'same' : s === 'missing')).map(([id]) => id));
      const list = el('div', 'sv-list');
      for (const [id, s] of status) {
        const tick = el('input');
        tick.type = 'checkbox'; tick.checked = view.picks.has(id); tick.disabled = s === 'same' || !!busy;
        tick.setAttribute('aria-label', 'Put back ' + title(id));
        tick.addEventListener('change', () => { if (tick.checked) view.picks.add(id); else view.picks.delete(id); render(); });
        const text = s === 'same' ? 'The same on the calculator' : s === 'missing' ? 'Not on the calculator' : "Different on the calculator. This replaces what's there";
        const r = row(id, text, tick);
        if (id === 'numplay') r.querySelector('b').textContent = "NumPlay's settings";
        r.classList.add('sv-pick', 'sv-' + s);
        r.addEventListener('click', (e) => { if (e.target !== tick && !tick.disabled) tick.click(); });
        list.appendChild(r);
      }
      if (!status.size) list.appendChild(el('p', 'sv-hint', 'This backup holds no save.'));
      else if ([...status.values()].every((x) => x === 'same')) list.appendChild(el('p', 'sv-hint', 'Your calculator has everything this backup holds.'));
      body.appendChild(list);
      const n = view.picks.size;
      body.appendChild(button('install wide', busy || (n === 1 ? 'Put back ' + ([...view.picks][0] === 'numplay' ? "NumPlay's settings" : title([...view.picks][0])) : n ? 'Put back ' + (n === status.size ? 'all ' : 'these ') + n : 'Nothing to put back'), () => {
        const picks = [...view.picks];
        if (!picks.length) return;
        run('Putting back', async () => {
          const left = await change((list) => picks.map((id) => S.putBack(list, theirs, id, who)));
          view = null;
          if (left.length) problem = 'Not everything went back. ' + tell(left) + '.';
          else note = 'Back on your calculator. Unplug it and open NumPlay.';
        });
      })).disabled = !n || !!busy;
    }
    if (problem) body.appendChild(el('p', 'sv-warn', problem));
    body.appendChild(button('sv-link sv-back', 'Back to your saves', () => { view = null; problem = null; render(); }));
  }
  async function openFile(f) {
    try {
      if (f.size > 4e6) throw new Error("That file is too big to be a backup of NumPlay's saves");
      const got = S.unpack(new Uint8Array(await f.arrayBuffer()), f.name);
      const made = got.made && !isNaN(Date.parse(got.made)) ? Date.parse(got.made) : f.lastModified || Date.now();
      const c = got.calculator || {};
      const b = await keepBackup({ made, serial: c.id ? String(c.id) : '', model: c.model || null, software: c.software || null, studio: got.studio, files: got.files, from: f.name });
      view = { name: 'put', backup: b, picks: null };
      problem = note = null;
    } catch (e) { problem = e.message; }
    render();
  }

  // ------------------------------------------------------------------ opening and closing
  // (the page opens the tab and leaves it: app.js's showTab, which also reads ?saves in the address)
  function open() {
    if (!kit || opened) return;
    opened = true;
    $('#saves').hidden = false;
    view = null; asking = null; problem = note = null;
    render();
    if (!calc && usbNow()) connect(false);  // (one this page was given before, if it is plugged in)
  }
  function close() {
    if (!opened) return;
    opened = false;
    $('#saves').hidden = true;
    if (!busy) drop();  // (the calculator is let go: NumWorks' website, in another tab, can have it)
  }
  // kit: kit.json (its "saves": every game's files; without it, an older kit, the files are still backed up, as
  // "Games' saves"); say: the page's toast. Called once the kit is loaded.
  async function start(k, say) {
    kit = k;
    toast = say || toast;
    who = S.owners(kit.saves || []);
    watch();
    ready = loadBackups();
    await ready;
  }
  let watched = null;
  function watch() {
    const u = usbNow();
    if (!u || !u.addEventListener || watched === u) return;
    watched = u;
    u.addEventListener('disconnect', (e) => {
      if (!calc || e.device !== calc.device) return;
      if (busy) { calc.lost(); return; }  // (the request under way fails, and says so)
      drop();
      toast('Calculator unplugged');
      render();
    });
    u.addEventListener('connect', () => { if (opened && !calc && !busy) connect(false); });
  }
  // (the tests' and docs/saves.md's calculator that isn't one: tests/saves/fake_calculator.js)
  function use(other) { if (!busy) drop(); usb = other; watch(); render(); }

  root.npSavesUi = { start, open, close, use };
})(this);
