/* A NumWorks calculator as USB sees one, for the tests of NumPlay Studio's Saves (tests/saves/usb_check.js) and to
 * try the page without one (docs/saves.md): a WebUSB device, and a navigator.usb that hands it out.
 *
 * It answers as Epsilon does: its DFU requests are ion/src/device/shared/usb/dfu_interface.cpp's, state by state,
 * with what that code does that a careless client trips on:
 * - the address pointer goes back to 0 after any block or command is run (pointer: 'reset'; 'sticky' is the code
 *   before, where it stays), so blocks sent one after the other without their own "set address" land at 0;
 * - the same slip makes a write that failed look done, and nothing checks an address in RAM: a write outside the
 *   RAM, or to RAM through the flash's setting, or a read of memory that isn't there, is a crashed calculator
 *   here (crashed says why, and every request after it fails), since on a real one it is a fault or worse;
 * - a block is at most 2048 bytes; a read of a range that isn't memory answers with no bytes.
 * And as Epsilon keeps its files: the slot info at the start of the RAM, the software's header in flash, the two
 * magics around 42 KB of files, then the delegate, the name verifier's 128 bytes, the size it may use and its
 * memory of the last file looked up (Upsilon, Epsilon 15's: the delegate, then that memory).
 *
 * new FakeCalculator({ model: 'N0110' | 'N0115' | 'N0120', software: 'epsilon' | 'upsilon', version, serial,
 *   files: [{ name, data }], remembers: a file's name (the last Epsilon looked up), hidden: bytes of files exam mode
 *   hides, pointer: 'reset' | 'sticky', names: false (the system doesn't give the settings' names), slot: false (no
 *   slot info), header: 'full' | 'short' (older: no device name), slots: 2 (both slots hold a software),
 *   layout: 'unknown' (Epsilon's memory of the last file isn't where it is known to be) })
 * .usb is the navigator.usb; .list() the files' bytes; .unplugAfter(n): the cable is pulled after n more requests;
 * .plug() puts it back (Epsilon restarted the USB: nothing else changed); .reset() empties its files as a reset
 * does; .log: every request. */
'use strict';
(function (root) {
  const S = typeof module === 'object' && module.exports ? require('../../customize/saves.js') : root.npSaves;
  const SIZE = 42 * 1024, RAM_SIZE = 256 * 1024, SEEN = 252 * 1024, AT = 0x3000;
  const ST = { idle: 2, dnloadSync: 3, dnBusy: 4, dnloadIdle: 5, manifestSync: 6, manifest: 7, uploadIdle: 9, error: 10 };
  const NONE = 0xFFFFFFFF;
  const u32 = (v) => { const b = new Uint8Array(4); new DataView(b.buffer).setUint32(0, v >>> 0, true); return b; };
  const cat = (...parts) => { const b = new Uint8Array(parts.reduce((n, p) => n + p.length, 0)); let o = 0; for (const p of parts) { b.set(p, o); o += p.length; } return b; };
  const str = (s, n) => { const b = new Uint8Array(n); for (let i = 0; i < s.length && i < n; i++) b[i] = s.charCodeAt(i); return b; };
  const error = (name, message) => Object.assign(new Error(message), { name });

  function FakeCalculator(opt) {
    opt = Object.assign({ model: 'N0120', software: 'epsilon', version: null, serial: '0123456789ABCDEF', files: [], remembers: null,
      hidden: 0, pointer: 'reset', names: true, slot: true, header: 'full', slots: 1, layout: 'usual' }, opt);
    const self = this, upsilon = opt.software === 'upsilon';
    const ramAt = opt.model === 'N0120' ? 0x24000000 : 0x20000000, ram = new Uint8Array(RAM_SIZE);
    const storage = ramAt + AT, base = storage + 4, priv = storage + 8 + SIZE;
    const cacheAt = priv + (upsilon ? 4 : opt.layout === 'unknown' ? 4 + 128 + 4 + 4000 : 4 + 128 + 4);
    const flash = new Map();  // address -> bytes (the rest of the flash reads 0xFF)
    const headerAt = [opt.model === 'N0120' ? 0x90020000 : 0x90010000, opt.model === 'N0120' ? 0x90420000 : 0x90410000];
    const version = opt.version || (upsilon ? '15.5.0' : '25.2.0');
    const hd = (appsStart) => {
      const first = cat(u32(0xDEC0EDFE), str(version, 8), u32(storage), u32(SIZE), u32(appsStart), u32(appsStart + 0x1F0000), u32(ramAt + 0x10000), u32(ramAt + 0x30000));
      if (upsilon) return cat(first, u32(0xDEC0EDFE), u32(0xEFBEADDE), str('1.0', 4), u32(0), new Uint8Array(24));
      if (opt.header === 'short') return cat(first, u32(0xDEC0EDFE), new Uint8Array(12));
      return cat(first, u32(appsStart + 0x1F0000), u32(appsStart + 0x1F0400), u32(0xDEC0EDFE));
    };
    flash.set(headerAt[0], hd(0x90200000));
    if (opt.slots === 2) flash.set(headerAt[1], hd(0x90600000));
    const w32 = (address, v) => ram.set(u32(v), address - ramAt), r32 = (address) => new DataView(ram.buffer).getUint32(address - ramAt, true);
    if (opt.slot) ram.set(cat(u32(0xEFEEDBBA), u32(0), u32(headerAt[0]), u32(0xEFEEDBBA)), 0);
    // (the rest of the RAM isn't empty on a calculator)
    for (let i = 16; i < SEEN; i++) ram[i] = (i * 2654435761 >>> 13) & 255;
    function boot(files, remembers) {
      const list = new Uint8Array(SIZE);
      for (const f of files) if (!S.write(list, f.name, f.data)) throw new Error('fake calculator: ' + f.name + " doesn't fit");
      ram.set(cat(u32(S.MAGIC), list, u32(S.MAGIC)), storage - ramAt);
      ram.fill(0, priv - ramAt, priv - ramAt + 4 + 128 + 12);
      w32(priv, ramAt + 0xF00);  // the delegate
      if (!upsilon && opt.layout !== 'unknown') w32(priv + 4 + 128, SIZE - opt.hidden);
      if (opt.layout === 'unknown') w32(cacheAt - 4, SIZE - opt.hidden);
      const at = remembers ? (S.files(list).find((f) => f.name === remembers) || {}).at : undefined;
      w32(cacheAt, at === undefined ? 0 : S.nameCrc(remembers));
      w32(cacheAt + 4, at === undefined ? 0 : base + at);
    }
    boot(opt.files, opt.remembers);

    // ---- what a test looks at
    this.model = opt.model;
    this.storage = { address: storage, size: SIZE };
    this.log = [];
    this.crashed = null;
    this.requests = 0;
    this.dropWrites = false;  // (a write that does nothing and says it did)
    this.list = () => ram.slice(base - ramAt, base - ramAt + SIZE);
    this.files = () => S.files(self.list());
    this.remembered = () => ({ crc: r32(cacheAt), ptr: r32(cacheAt + 4) });
    // what Epsilon does with its memory of the last file: the file it would hand out for that name, or what is wrong
    this.memoryHolds = () => {
      const { crc, ptr } = self.remembered();
      if (!ptr) return crc ? 'a checksum and no pointer' : null;
      const f = (self.files() || []).find((x) => base + x.at === ptr);
      return !f ? 'a pointer to no file (' + (ptr - base) + ')' : S.nameCrc(f.name) !== crc ? 'a pointer to ' + f.name + ', another file' : null;
    };
    this.ram = () => ram.slice();
    this.around = () => ({ before: ram.slice(0, storage - ramAt), after: ram.slice(priv - ramAt) });  // (all but the files)
    this.cacheSpan = [cacheAt - priv, cacheAt - priv + 8];  // (in around().after)
    this.setList = (list) => ram.set(list, base - ramAt);
    this.reset = () => boot([], null);
    let unplugIn = -1, plugged = true, authorised = false;
    this.unplugAfter = (n) => { unplugIn = n; };
    this.pick = true;  // (false: the picker is closed without a pick)

    // ---- memory, as the DFU code reaches it
    const inRam = (a, n) => a >= ramAt && a + n <= ramAt + SEEN;
    const inFlash = (a, n) => a >= 0x90000000 && a + n <= 0x90800000;
    function peek(address, n) {
      if (inRam(address, n)) return ram.slice(address - ramAt, address - ramAt + n);
      if (inFlash(address, n)) {
        const out = new Uint8Array(n).fill(0xFF);
        for (const [at, b] of flash) for (let i = 0; i < b.length; i++) if (at + i >= address && at + i < address + n) out[at + i - address] = b[i];
        return out;
      }
      self.crashed = self.crashed || 'a read of ' + n + ' bytes at 0x' + address.toString(16) + ", which isn't memory it may read";
      return null;
    }
    function poke(alt, address, b) {
      if (alt !== 1) { self.crashed = self.crashed || 'a write at 0x' + address.toString(16) + " through the flash's setting"; return false; }
      if (!inRam(address, b.length)) { self.crashed = self.crashed || 'a write of ' + b.length + ' bytes at 0x' + address.toString(16) + ', outside the RAM'; return false; }
      const lo = address - ramAt, hi = lo + b.length, ok0 = base - ramAt, ok1 = ok0 + SIZE, c0 = cacheAt - ramAt;
      // (the test's own rule, not Epsilon's: only the files and its memory of the last one are the website's to write)
      if (!((lo >= ok0 && hi <= ok1) || (lo >= c0 && hi <= c0 + 8))) self.crashed = self.crashed || 'a write at 0x' + address.toString(16) + ' (' + b.length + " bytes): not the files, not Epsilon's memory of the last one";
      if (!self.dropWrites) ram.set(b, lo);
      return true;
    }

    // ---- dfu_interface.cpp
    const WAITING = opt.pointer === 'reset' ? NONE : 0;  // ("no change of address waiting")
    let state = ST.idle, status = 0, pointer = 0, potential = WAITING, alt = 0, held = null, writeAddress = 0;
    function afterStatusSent() {  // wholeDataSentCallback
      if (state !== ST.dnBusy) return;
      if (held) {  // writeOnMemory
        const done = poke(alt, writeAddress, held);
        held = null;
        if (done) { state = ST.dnloadIdle; status = 0; } else { state = ST.error; status = 1; }
      }
      // changeAddressPointerIfNeeded
      if (potential !== WAITING) {
        pointer = potential;
        potential = 0;
        state = ST.dnloadIdle;
        status = 0;
      }
      state = ST.dnloadIdle;
    }
    function request(dir, setup, payload, length) {
      const { request: req, value } = setup;
      if (dir === 'out') {
        if (req === 1 && payload && payload.length) {  // wholeDataReceivedCallback
          if (payload.length > 2048) { self.crashed = self.crashed || 'a block of ' + payload.length + ' bytes'; return 'stall'; }
          if (value === 0) {
            if (payload[0] === 0x21 && payload.length === 5) { potential = new DataView(payload.buffer, payload.byteOffset).getUint32(1, true); state = ST.dnloadSync; return 'ok'; }
            if (payload[0] === 0x41) { self.crashed = self.crashed || 'an erase: Saves never erases flash'; return 'stall'; }
            state = ST.error; status = 0x0F;
            return 'ok';
          }
          if (value === 1) return 'stall';
          writeAddress = ((value - 2) * 2048 + pointer) >>> 0;
          held = payload.slice();
          state = ST.dnloadSync;
          return 'ok';
        }
        if (req === 1) {  // processDownloadRequest: an empty download leaves DFU and restarts
          if (state !== ST.idle && state !== ST.dnloadIdle) { state = ST.error; status = 9; return 'stall'; }
          self.crashed = self.crashed || 'a request to leave DFU: Saves never restarts the calculator';
          return 'ok';
        }
        if (req === 4) { status = 0; state = ST.idle; return 'ok'; }  // clearStatus
        if (req === 6) { status = 0; state = ST.idle; return 'ok'; }  // dfuAbort
        return 'stall';
      }
      if (req === 2) {  // processUploadRequest
        if (state !== ST.idle && state !== ST.uploadIdle) return 'stall';
        if (value < 2) return 'stall';
        const n = Math.min(2048, length), got = peek(((value - 2) * 2048 + pointer) >>> 0, n);
        state = ST.uploadIdle;
        return got || new Uint8Array(0);
      }
      if (req === 3) {  // getStatus
        if (state === ST.manifestSync) state = ST.manifest; else if (state === ST.dnloadSync) state = ST.dnBusy;
        const answer = Uint8Array.of(status, 0, 0, 0, state, 0);
        afterStatusSent();
        return answer.subarray(0, length);
      }
      if (req === 5) return Uint8Array.of(state);  // getState
      return 'stall';
    }

    // ---- the USB device
    const settingNames = [(opt.model === 'N0120' ? '@Flash/0x90030000/61*064Kg' : '@Flash/0x90030000/61*064Kg,64*064Kg'), '@SRAM/0x' + ramAt.toString(16) + '/01*252Ke'];
    const alternates = [0, 1].map((n) => ({ alternateSetting: n, interfaceClass: 0xFE, interfaceSubclass: 1, interfaceProtocol: 2, interfaceName: opt.names ? settingNames[n] : null, endpoints: [] }));
    const iface = { interfaceNumber: 0, alternates, alternate: alternates[0], claimed: false };
    const configuration = { configurationValue: 1, configurationName: null, interfaces: [iface] };
    const bcd = { N0100: 0x0100, N0110: 0x0110, N0115: 0x0115, N0120: 0x0120 }[opt.model] || 0x0199;
    function descriptor(type, index) {
      if (type === 2) {  // the configuration, its two settings of one interface, the DFU functional descriptor
        const body = cat(...[0, 1].map((n) => Uint8Array.of(9, 4, 0, n, 0, 0xFE, 1, 2, 16 + n)), Uint8Array.of(9, 0x21, 3, 0, 0, 0, 8, 0, 1));
        return cat(Uint8Array.of(9, 2, (9 + body.length) & 255, (9 + body.length) >> 8, 1, 1, 0, 0x80, 0x32), body);
      }
      if (type === 3 && index === 0) return Uint8Array.of(4, 3, 9, 4);
      if (type === 3 && (index === 16 || index === 17)) {
        const s = settingNames[index - 16], b = new Uint8Array(2 + 2 * s.length);
        b[0] = b.length; b[1] = 3;
        for (let i = 0; i < s.length; i++) b[2 + 2 * i] = s.charCodeAt(i);
        return b;
      }
      return null;
    }
    const listeners = { connect: new Set(), disconnect: new Set() };
    const fire = (type) => { for (const l of listeners[type]) l({ type, device }); };
    function live() {
      if (self.crashed) throw error('NetworkError', 'A transfer error has occurred.');
      if (!plugged) throw error('NotFoundError', 'The device was disconnected.');
      if (unplugIn === 0) { unplugIn = -1; plugged = false; device.opened = false; iface.claimed = false; setTimeout(() => fire('disconnect'), 0); throw error('NotFoundError', 'The device was disconnected.'); }
      if (unplugIn > 0) unplugIn--;
      self.requests++;
    }
    const device = {
      vendorId: 0x0483, productId: 0xA291, productName: 'NumWorks Calculator', manufacturerName: 'NumWorks', serialNumber: opt.serial,
      deviceVersionMajor: bcd >> 8, deviceVersionMinor: bcd >> 4 & 15, deviceVersionSubminor: bcd & 15,
      opened: false, configuration: null, configurations: [configuration],
      async open() { live(); if (self.busy) throw error('SecurityError', 'Access denied.'); device.opened = true; },
      async close() { device.opened = false; iface.claimed = false; },
      async selectConfiguration() { live(); device.configuration = configuration; },
      async claimInterface() { live(); if (self.busy) throw error('NetworkError', 'Unable to claim interface.'); iface.claimed = true; },
      async releaseInterface() { iface.claimed = false; },
      async selectAlternateInterface(n, a) { live(); if (!iface.claimed) throw error('InvalidStateError', 'not claimed'); alt = a; iface.alternate = alternates[a]; self.log.push(['alt', a]); },
      async controlTransferIn(setup, length) {
        live();
        if (setup.requestType === 'standard') {
          const d = descriptor(setup.value >> 8, setup.value & 255);
          return d ? { status: 'ok', data: new DataView(d.slice(0, length).buffer) } : { status: 'stall' };
        }
        if (!iface.claimed) throw error('InvalidStateError', 'not claimed');
        const got = request('in', setup, null, length);
        self.log.push(['in', setup.request, setup.value, length, got === 'stall' ? 'stall' : got.length]);
        if (self.crashed) throw error('NetworkError', 'A transfer error has occurred.');
        return got === 'stall' ? { status: 'stall' } : { status: 'ok', data: new DataView(got.slice().buffer) };
      },
      async controlTransferOut(setup, data) {
        live();
        if (!iface.claimed) throw error('InvalidStateError', 'not claimed');
        const payload = data ? new Uint8Array(data.buffer ? data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength) : data) : null;
        const got = request('out', setup, payload, 0);
        self.log.push(['out', setup.request, setup.value, payload ? payload.length : 0, got]);
        if (self.crashed) throw error('NetworkError', 'A transfer error has occurred.');
        return { status: got, bytesWritten: payload ? payload.length : 0 };
      },
    };
    this.device = device;
    this.plug = () => {
      plugged = true; unplugIn = -1; self.crashed = null;
      state = ST.idle; status = 0; pointer = 0; potential = WAITING; alt = 0; held = null;  // (a new DFUInterface each time)
      if (authorised) setTimeout(() => fire('connect'), 0);
    };
    this.unplug = () => { plugged = false; device.opened = false; iface.claimed = false; setTimeout(() => fire('disconnect'), 0); };
    this.usb = {
      async requestDevice() {
        if (!self.pick || !plugged) throw error('NotFoundError', 'No device selected.');
        authorised = true;
        return device;
      },
      async getDevices() { return authorised && plugged ? [device] : []; },
      addEventListener(type, l) { if (listeners[type]) listeners[type].add(l); },
      removeEventListener(type, l) { if (listeners[type]) listeners[type].delete(l); },
    };
    this.authorise = () => { authorised = true; };
  }

  if (typeof module === 'object' && module.exports) module.exports = { FakeCalculator };
  else root.FakeCalculator = FakeCalculator;
})(this);
