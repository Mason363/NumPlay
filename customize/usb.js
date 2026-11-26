/* NumPlay Studio: the calculator over USB, for Saves (saves-ui.js). docs/saves.md tells all of it.
 *
 * Plugged in and out of any app, a NumWorks answers DFU requests (USB's protocol for updates, with ST's commands
 * for an address and blocks of memory) from Epsilon itself, which waits meanwhile: the way NumWorks' own website
 * reads and writes your scripts. Its files are in RAM, at the address the running software's header gives
 * (games/common/epsilon_app.h tells the header; the slot info at the start of the RAM says where it is).
 *
 * What Epsilon's DFU code does (ion/src/device/shared/usb/dfu_interface.cpp, where this was checked):
 * - two alternate settings of one interface: 0 the flash, 1 the RAM. Reading takes any address under either; writing
 *   RAM is a plain copy only under 1 (under 0 it would be sent to the flash's driver), so RAM goes through 1;
 * - a block is at most 2048 bytes, and one written lands in one go;
 * - after any block written the address pointer is back at 0 (its "no change waiting" is 0xFFFFFFFF, and it is left
 *   at 0), so every block written is sent as block 2 after its own "set address"; reads keep the pointer;
 * - the same slip clears the error of a write that failed, and RAM isn't checked at all: "it worked" means nothing.
 *   So only the files' own bytes are ever written (and the 8 after them that saves.js's cache finds), between
 *   Epsilon's two magics read first, and everything written is read back.
 * A change (saves.js's plan) is made only on files read a moment before, and is read back whole after.
 *
 * usb: navigator.usb, or anything like it (tests/saves/fake_calculator.js). Errors are Errors with a "code":
 * none (no calculator picked), busy (something else has it), gone (unplugged), software (not a software this
 * knows), files (its files aren't where they should be, or don't make a list), refused (it didn't take a request),
 * exam (exam mode hides the files), moves (a file would move and Epsilon's memory of it can't be told), stale (its
 * files changed since they were read), changed (what was written isn't what is there). */
'use strict';
(function (root) {
  const S = typeof module === 'object' && module.exports ? require('./saves.js') : root.npSaves;
  const VENDOR = 0x0483, PRODUCT = 0xA291;
  const BLOCK = 2048;
  const MODELS = { 0x0100: 'N0100', 0x0110: 'N0110', 0x0115: 'N0115', 0x0120: 'N0120' };
  const RAM = { N0120: 0x24000000 }, RAM_AT = 0x20000000, RAM_SIZE = 252 * 1024;  // (what its descriptor says is there)
  const FLASH = 0x90000000, FLASH_END = 0x90800000;
  const SLOT_MAGIC = 0xEFEEDBBA, HEADER_MAGIC = 0xDEC0EDFE, OMEGA_MAGIC = 0xEFBEADDE;
  // where a software's header is when the slot info doesn't say: either slot, 64 KiB in, or 128 (epsilon_app.h)
  const HEADERS = [0x90010000, 0x90020000, 0x90410000, 0x90420000];
  const AFTER = 400;  // bytes read past the files, for Epsilon's memory of the last one (saves.js's cache: 98 words)
  const REQ = { download: 1, upload: 2, getStatus: 3, clearStatus: 4, getState: 5, abort: 6 };
  const ST = { idle: 2, dnloadSync: 3, dnBusy: 4, dnloadIdle: 5, manifestSync: 6, uploadIdle: 9, error: 10 };
  const SET_ADDRESS = 0x21;

  const fail = (code, message, cause) => Object.assign(new Error(message), { code, cause });
  const rd32 = (b, p) => (b[p] | b[p + 1] << 8 | b[p + 2] << 16 | b[p + 3] << 24) >>> 0;
  const hex = (n) => '0x' + (n >>> 0).toString(16).toUpperCase().padStart(8, '0');

  // ------------------------------------------------------------------ DFU
  function dfu(device, opt) {
    const timeout = (opt && opt.timeout) || 10000;
    let iface = null, alt = null, alts = [];
    // (a transfer that never ends, or the cable pulled: an Error with a code, not WebUSB's)
    const guard = (p, what) => new Promise((ok, no) => {
      const t = setTimeout(() => no(fail('gone', what + ": the calculator stopped answering")), timeout);
      p.then((v) => { clearTimeout(t); ok(v); }, (e) => {
        clearTimeout(t);
        if (e && e.code) return no(e);
        const lost = (e && e.name === 'NotFoundError') || /disconnect|not found|no device|unplug/i.test(String(e && e.message));
        no(fail(lost ? 'gone' : 'refused', what + ': ' + (lost ? 'the calculator was unplugged' : (e && e.message) || e), e));
      });
    });
    const setup = (request, value) => ({ requestType: 'class', recipient: 'interface', request, value, index: iface });
    async function out(request, value, data, what) {
      const r = await guard(data ? device.controlTransferOut(setup(request, value), data) : device.controlTransferOut(setup(request, value)), what);
      if (r.status !== 'ok') throw fail('refused', what + ": the calculator didn't take it (" + r.status + ')');
    }
    async function inp(request, value, length, what) {
      const r = await guard(device.controlTransferIn(setup(request, value), length), what);
      if (r.status !== 'ok' || !r.data) throw fail('refused', what + ": the calculator didn't answer (" + r.status + ')');
      return new Uint8Array(r.data.buffer.slice(r.data.byteOffset, r.data.byteOffset + r.data.byteLength));
    }
    const state = async () => (await inp(REQ.getState, 0, 1, 'Asking its state'))[0];
    async function status(what) {
      const b = await inp(REQ.getStatus, 0, 6, what);
      if (b.length < 6) throw fail('refused', what + ': a short answer');
      return { status: b[0], wait: b[1] | b[2] << 8 | b[3] << 16, state: b[4] };
    }
    // back to idle: an error cleared, a transfer in the middle dropped (asking its state runs nothing, asking its
    // status would run a block that waits)
    async function idle() {
      for (let k = 0; k < 6; k++) {
        const s = await state();
        if (s === ST.idle) return;
        if (s === ST.error) await out(REQ.clearStatus, 0, null, 'Clearing an error');
        else if (s === ST.dnBusy) await new Promise((r) => setTimeout(r, 10));
        else if (s === ST.dnloadSync || s === ST.dnloadIdle || s === ST.uploadIdle || s === ST.manifestSync) await out(REQ.abort, 0, null, 'Dropping a transfer');
        else throw fail('refused', "The calculator isn't ready for requests (state " + s + '): unplug it and plug it back in');
      }
      throw fail('refused', "The calculator didn't get ready for requests: unplug it and plug it back in");
    }
    // a command or a block is run while its status is asked: asked until it is done
    async function done(what) {
      const until = Date.now() + timeout;
      let s = await status(what);
      while (s.state === ST.dnBusy || s.state === ST.dnloadSync) {
        if (Date.now() > until) throw fail('gone', what + ': the calculator stayed busy');
        if (s.wait) await new Promise((r) => setTimeout(r, Math.min(s.wait, 1000)));
        s = await status(what);
      }
      if (s.state !== ST.dnloadIdle) {
        if (s.state === ST.error) await out(REQ.clearStatus, 0, null, 'Clearing an error').catch(() => {});
        throw fail('refused', what + ": the calculator didn't do it (state " + s.state + ', status ' + s.status + ')');
      }
    }
    async function point(address) {
      const p = new Uint8Array(5);
      p[0] = SET_ADDRESS;
      new DataView(p.buffer).setUint32(1, address, true);
      await out(REQ.download, 0, p, 'Setting the address ' + hex(address));
      await done('Setting the address ' + hex(address));
    }
    async function select(a) {
      if (alt === a) return;
      await guard(device.selectAlternateInterface(iface, a), 'Choosing the memory');
      alt = a;
    }
    return {
      get alts() { return alts; },
      async open() {
        try {
          if (!device.opened) await guard(device.open(), 'Opening the calculator');
          if (!device.configuration) await guard(device.selectConfiguration(1), 'Opening the calculator');
          const i = device.configuration.interfaces.find((x) => x.alternates.some((y) => y.interfaceClass === 0xFE && y.interfaceSubclass === 1));
          if (!i) throw fail('software', "This calculator's software doesn't give its files over USB. Update it at numworks.com.");
          if (!i.claimed) await guard(device.claimInterface(i.interfaceNumber), 'Opening the calculator');
          iface = i.interfaceNumber;
          alt = null;
          alts = i.alternates.filter((y) => y.interfaceClass === 0xFE && y.interfaceSubclass === 1)
            .map((y) => ({ n: y.alternateSetting, name: y.interfaceName || '' }));
          if (alts.some((y) => !y.name)) await names(i.interfaceNumber).catch(() => {});  // (some systems don't give the names)
        } catch (e) {
          if (e.code === 'software' || e.code === 'gone') throw e;
          // (claimed by another tab or app, NumWorks' website for one, or the system's own driver)
          throw fail('busy', "Couldn't open the calculator. Close the other tabs or apps using it, like NumWorks' website, then unplug it and plug it back in.", e);
        }
      },
      async close() {
        const i = iface;
        iface = null;
        alt = null;
        try { if (device.opened && i !== null) await device.releaseInterface(i); } catch (e) { /* (gone already) */ }
        try { if (device.opened) await device.close(); } catch (e) { /* (the same) */ }
      },
      // length bytes at address, of the memory under alternate setting a
      async read(a, address, length) {
        const res = new Uint8Array(length);
        await select(a);
        await idle();
        await point(address);
        await out(REQ.abort, 0, null, 'Reading');  // (the address set, it waits for a block to write: reads start from idle)
        for (let at = 0, k = 0; at < length; at += BLOCK, k++) {
          const n = Math.min(BLOCK, length - at), b = await inp(REQ.upload, 2 + k, n, 'Reading ' + hex(address + at));
          if (b.length !== n) throw fail('refused', 'Reading ' + hex(address + at) + ': ' + b.length + ' bytes instead of ' + n);
          res.set(b, at);
        }
        await idle();
        return res;
      },
      // bytes (one block at most) at address of the RAM: its own "set address", then block 2
      async write(a, address, bytes) {
        if (!bytes.length || bytes.length > BLOCK) throw fail('refused', 'A write of ' + bytes.length + ' bytes');
        await select(a);
        await idle();
        await point(address);
        await out(REQ.download, 2, bytes, 'Writing ' + hex(address));
        await done('Writing ' + hex(address));
        await idle();
      },
    };
    // the alternate settings' names from the descriptors themselves ("@Flash/...", "@SRAM/...")
    async function names(number) {
      const get = async (type, index, lang, length) => {
        const r = await guard(device.controlTransferIn({ requestType: 'standard', recipient: 'device', request: 6, value: type << 8 | index, index: lang }, length), 'Reading its descriptors');
        if (r.status !== 'ok' || !r.data) throw fail('refused', 'no descriptor');
        return new Uint8Array(r.data.buffer.slice(r.data.byteOffset, r.data.byteOffset + r.data.byteLength));
      };
      const head = await get(2, 0, 0, 9), conf = await get(2, 0, 0, head[2] | head[3] << 8);
      for (let p = 0; p + 2 <= conf.length && conf[p] >= 2; p += conf[p]) {
        if (conf[p + 1] !== 4 || conf[p] < 9 || conf[p + 2] !== number || !conf[p + 8]) continue;
        const s = await get(3, conf[p + 8], 0x0409, 255), a = alts.find((y) => y.n === conf[p + 3]);
        let name = '';
        for (let q = 2; q + 1 < Math.min(s[0], s.length); q += 2) name += String.fromCharCode(s[q] | s[q + 1] << 8);
        if (a && !a.name) a.name = name;
      }
    }
  }

  // ------------------------------------------------------------------ the calculator
  // A software's header (epsilon_app.h's epsilon_userland_t), from its first 48 bytes; null if it isn't one whose
  // files are in the RAM of a calculator
  function header(b) {
    if (b.length < 48 || rd32(b, 0) !== HEADER_MAGIC) return null;
    let version = '';
    for (let i = 4; i < 12 && b[i]; i++) version += String.fromCharCode(b[i]);
    const ram = rd32(b, 12), n = rd32(b, 16);
    const inRam = (ram >= 0x20000000 && ram + n + 8 <= 0x20040000) || (ram >= 0x24000000 && ram + n + 8 <= 0x24040000);
    if (!inRam || ram & 3 || n < 1024 || n > 0x10000 || !/^[\x20-\x7e]{1,8}$/.test(version)) return null;
    return { version, address: ram, size: n, upsilon: rd32(b, 36) === HEADER_MAGIC && rd32(b, 40) === OMEGA_MAGIC };
  }

  async function open(device, opt) {
    const d = dfu(device, opt);
    let queue = Promise.resolve(), gone = false;
    // (one request at a time: two at once would mix their steps)
    const turn = (task) => { const run = queue.then(task, task); queue = run.catch(() => {}); return run; };
    await d.open();
    const calc = { device, model: MODELS[device.deviceVersionMajor << 8 | device.deviceVersionMinor << 4 | device.deviceVersionSubminor] || null, serial: device.serialNumber || '' };
    try {
      if (calc.model === 'N0100') throw fail('software', "This is an N0100, which can't run apps like NumPlay.");
      const named = (word) => d.alts.find((a) => new RegExp('^@' + word, 'i').test(a.name));
      const flash = named('Flash') || d.alts.find((a) => a.n === 0), sram = named('SRAM') || d.alts.find((a) => a.n === 1);
      if (!flash || !sram || flash === sram) throw fail('software', "This calculator's software doesn't give its files over USB. Update it at numworks.com.");
      const m = /^@SRAM\s*\/\s*0x([0-9a-f]{8})\s*\/\s*0*1\*0*(\d+)K/i.exec(sram.name);
      const ramAt = m ? parseInt(m[1], 16) : RAM[calc.model] || RAM_AT, ramEnd = ramAt + (m ? +m[2] * 1024 : RAM_SIZE);
      // the running software's header: where the slot info says, or the one header there is
      let h = null;
      const slot = await d.read(sram.n, ramAt, 16);
      if (rd32(slot, 0) === SLOT_MAGIC && rd32(slot, 12) === SLOT_MAGIC) {
        const at = rd32(slot, 8);
        if (at >= FLASH && at + 48 <= FLASH_END && !(at & 3)) h = header(await d.read(flash.n, at, 48));
      }
      if (!h) {
        const found = [];
        for (const at of HEADERS) { const x = header(await d.read(flash.n, at, 48)); if (x) found.push(x); }
        if (found.length && found.every((x) => x.address === found[0].address && x.size === found[0].size && x.upsilon === found[0].upsilon)) h = found[0];
      }
      if (!h || h.address < ramAt || h.address + h.size + 8 > ramEnd)
        throw fail('software', "NumPlay Studio doesn't know this calculator's software. Update it at numworks.com.");
      Object.assign(calc, { software: h.version, upsilon: h.upsilon, size: h.size });
      const base = h.address + 4;  // (the first file, after the magic)

      // Its files as they are now: { list: their bytes, files: saves.js's files (null: not a list), room: the size
      // Epsilon may use (less in exam mode), safe: whether a file may move (Epsilon's memory of the last one found:
      // cache, in after, the bytes that follow the files) }
      async function read() {
        const all = await d.read(sram.n, h.address, h.size + 8);
        if (rd32(all, 0) !== S.MAGIC || rd32(all, 4 + h.size) !== S.MAGIC)
          throw fail('files', "The calculator's files aren't where its software says. Nothing was changed.");
        const list = all.slice(4, 4 + h.size), n = Math.min(AFTER, ramEnd - (h.address + h.size + 8));
        const after = n >= 12 ? await d.read(sram.n, h.address + h.size + 8, n) : null;
        const c = S.files(list) && after ? S.cache(after, base, list, h.upsilon) : null;
        return { list, files: S.files(list), room: c ? c.room : h.size, safe: !!c, cache: c, after };
      }
      calc.files = () => turn(read);
      // Makes the calculator's files `after` (a list, from saves.js's put on what files() gave: `before`). Read
      // again first (the same as before, or nothing is done), written the way plan says, read back.
      calc.change = (before, after) => turn(async () => {
        const p = S.plan(before, after);
        if (!p.writes.length) return { writes: 0 };
        const now = await read();
        if (!S.same(now.list, before)) throw fail('stale', "The calculator's files changed since they were read. Nothing was changed. Try again.");
        if (now.room !== h.size) throw fail('exam', 'The calculator is in exam mode, which hides its files. Nothing was changed.');
        if (S.end(after) + 2 > now.room) throw fail('files', "That doesn't fit the calculator's files. Nothing was changed.");
        if (p.moves) {
          if (!now.cache) throw fail('moves', "That can't be done safely on this calculator's software. Nothing was changed.");
          if (now.cache.ptr) await d.write(sram.n, h.address + h.size + 8 + now.cache.at, new Uint8Array(8));
        }
        for (const w of p.writes) await d.write(sram.n, base + w.at, w.bytes);
        const got = await read();
        if (!S.same(got.list, after)) throw fail('changed', "The calculator's files aren't what was written. Unplug it, plug it back in and try again.");
        return { writes: p.writes.length };
      });
      calc.close = () => { gone = true; return turn(() => d.close()); };
      Object.defineProperty(calc, 'gone', { get: () => gone });
      calc.lost = () => { gone = true; };
      return calc;
    } catch (e) {
      await d.close();
      throw e;
    }
  }

  // usb: navigator.usb (or the test's). The calculator the browser's picker gives (from a click), or one this
  // page was given before, if it is plugged in (null if not).
  const filters = [{ vendorId: VENDOR, productId: PRODUCT }];
  async function ask(usb, opt) {
    let device;
    try { device = await usb.requestDevice({ filters }); } catch (e) {
      if (e && e.name === 'NotFoundError') throw fail('none', 'No calculator was picked', e);
      throw fail('busy', "The browser didn't let this page use USB. Close the other tabs or apps using the calculator and try again.", e);
    }
    return open(device, opt);
  }
  async function known(usb, opt) {
    const device = (await usb.getDevices()).find((x) => x.vendorId === VENDOR && x.productId === PRODUCT);
    return device ? open(device, opt) : null;
  }

  const api = { VENDOR, PRODUCT, BLOCK, HEADERS, AFTER, header, open, ask, known };
  if (typeof module === 'object' && module.exports) module.exports = api;  // (Node, for the test)
  else root.npUsb = api;
})(this);
