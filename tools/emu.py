#!/usr/bin/env python3
"""Run a calculator build of an app (.nwa) in the Unicorn CPU emulator.

The .nwa is linked by nwlink exactly as the calculator's installer does, at a
realistic address, and the resulting Cortex-M7 code runs here with the
calculator's system calls (SVC) serviced in Python:

- display: a 320x240 RGB565 frame buffer, saved as PNG frames on request;
- keyboard: keys held over scripted time ranges;
- time: a virtual clock driven by executed instructions and msleep;
- flash: sector erase and programming follow NOR rules (erase sets 0xFF,
  programming can only clear bits), so NumPlay's uninstaller runs for real;
- storage: an Epsilon record file system in RAM, found by apps through the
  userland header, optionally loaded from and saved to a file.

Usage:
  emu.py app.nwa --ms 5000 --keys "500-600:ok,1000-1100:right" \
         --shots 900,2000 --gif 0-5000 --out DIR
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile

from elftools.elf.elffile import ELFFile
from unicorn import (UC_ARCH_ARM, UC_HOOK_CODE, UC_HOOK_INTR, UC_HOOK_MEM_INVALID, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE,
                     UC_MODE_MCLASS, UC_MODE_THUMB, Uc, UcError)
from unicorn.arm_const import (UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
                               UC_ARM_REG_R3, UC_ARM_REG_SP)

KEYS = {"left": 0, "up": 1, "down": 2, "right": 3, "ok": 4, "back": 5, "home": 6, "onoff": 8, "shift": 12,
        "alpha": 13, "xnt": 14, "var": 15, "toolbox": 16, "backspace": 17, "exp": 18, "ln": 19, "log": 20,
        "i": 21, "comma": 22, "power": 23, "sin": 24, "cos": 25, "tan": 26, "pi": 27, "sqrt": 28, "square": 29,
        "7": 30, "8": 31, "9": 32, "lparen": 33, "rparen": 34, "4": 36, "5": 37, "6": 38, "mul": 39, "div": 40,
        "1": 42, "2": 43, "3": 44, "plus": 45, "minus": 46, "0": 48, "dot": 49, "ee": 50, "ans": 51, "exe": 52}

FLASH, FLASH_SIZE = 0x90000000, 0x800000
SRAM, SRAM_SIZE = 0x24000000, 0x40000
USERLAND_HEADER = 0x90020000  # N0120: kernel, extra data sector, then userland
TRAMPOLINE = 0x90010400
DRAW_STRING_HOOK = 0x90010800
EXIT_HOOK = 0x90010900
STORAGE = 0x24001000
STORAGE_SIZE = 42 * 1024
EXT_RAM_END = 0x24037000
EXT_RAM_LEN = 153676
EXT_RAM_START = EXT_RAM_END - EXT_RAM_LEN
STACK_TOP = 0x2403F000
EXT_FLASH_START = 0x90200000
EXT_FLASH_END = 0x903F0000
CPU_HZ = 216_000_000
MODEL, SYSTEM = "n0120", "epsilon"


def configure(model="n0120", system="epsilon", ram_length=None):
    """The calculator to emulate: the N0120 (RAM at 0x24000000, the userland after an extra data
    sector) or the N0110/N0115 (RAM at 0x20000000); Epsilon or Upsilon (its userland header has no
    device name, apps get 107674 bytes of RAM, its file system is Epsilon 15's and it has none of
    the system calls for Home, checksums or flash). ram_length: the RAM apps get, when another
    build of the software gives them another amount."""
    global MODEL, SYSTEM, SRAM, USERLAND_HEADER, STORAGE, STORAGE_SIZE, EXT_RAM_END, EXT_RAM_LEN, EXT_RAM_START, \
        STACK_TOP
    MODEL, SYSTEM = model, system
    SRAM = 0x20000000 if model == "n0110" else 0x24000000
    USERLAND_HEADER = 0x90010000 if model == "n0110" else 0x90020000
    STORAGE = SRAM + 0x1000
    STORAGE_SIZE = 64400 if system == "upsilon" else 42 * 1024
    EXT_RAM_END = SRAM + 0x37000
    EXT_RAM_LEN = ram_length or (107674 if system == "upsilon" else 153676)
    EXT_RAM_START = EXT_RAM_END - EXT_RAM_LEN
    STACK_TOP = SRAM + 0x3F000
# Epsilon's SmallFont.ttf and LargeFont.ttf (from the epsilon repository), for text drawn by
# the firmware. Without them, text shows as blank cells.
FONT_DIR = os.environ.get("EPSILON_FONTS", "")


def epsilon_crc32(data):
    """Ion::crc32Byte as in Epsilon's simulator: CRC-32/MPEG-2 fed word by word
    (little-endian words, most significant byte first), then the tail bytes."""
    if not data:
        return 0
    crc = 0xFFFFFFFF

    def eat(crc, b):
        crc ^= b << 24
        for _ in range(8):
            crc = ((crc << 1) ^ 0x04C11DB7) if crc & 0x80000000 else crc << 1
            crc &= 0xFFFFFFFF
        return crc

    n = len(data) // 4
    for i in range(n):
        for j in (3, 2, 1, 0):
            crc = eat(crc, data[4 * i + j])
    for b in data[4 * n:]:
        crc = eat(crc, b)
    return crc


def record_crc(full_name):
    base, _, ext = full_name.rpartition(".")
    parts = struct.pack("<II", epsilon_crc32(base.encode()), epsilon_crc32(ext.encode()))
    return epsilon_crc32(parts)


def sector_of(addr):
    """Epsilon's Ion::Device::Flash::SectorAtAddress (8x4K, 1x32K, then 64K sectors)."""
    off = addr - FLASH
    i = off >> 16
    if i >= 1:
        return 8 + 1 + i - 1
    i = off >> 15
    if i >= 1:
        return 8 + i - 1
    return off >> 12


def sector_range(index):
    if index < 8:
        return FLASH + index * 0x1000, 0x1000
    if index == 8:
        return FLASH + 0x8000, 0x8000
    return FLASH + (index - 8) * 0x10000, 0x10000


class Font:
    """Approximates Epsilon's fonts (4-bit anti-aliased, fixed cells) with PIL."""

    def __init__(self):
        from PIL import Image, ImageDraw, ImageFont
        self.cells = {}
        for large, (name, px, w, h) in ((False, ("SmallFont.ttf", 12, 7, 14)), (True, ("LargeFont.ttf", 16, 10, 18))):
            path = os.path.join(FONT_DIR, name)
            glyphs = {}
            if os.path.exists(path):
                f = ImageFont.truetype(path, px)
                asc, _ = f.getmetrics()
                for c in range(32, 127):
                    im = Image.new("L", (w, h), 0)
                    ImageDraw.Draw(im).text((0, h - 3 - asc + (1 if large else 0)), chr(c), font=f, fill=255)
                    glyphs[c] = im.tobytes()
            self.cells[large] = (w, h, glyphs)

    def draw(self, screen, text, x, y, large, fg, bg):
        w, h, glyphs = self.cells[large]

        def mix(t):
            out = 0
            for shift, mask in ((11, 31), (5, 63), (0, 31)):
                a, b = (bg >> shift) & mask, (fg >> shift) & mask
                out |= ((a * (255 - t) + b * t) // 255) << shift
            return out

        cx = x
        for ch in text:
            g = glyphs.get(ord(ch)) if ord(ch) < 128 else None
            for j in range(h):
                for i in range(w):
                    px, py = cx + i, y + j
                    if 0 <= px < 320 and 0 <= py < 240:
                        t = g[j * w + i] if g else 0
                        struct.pack_into("<H", screen, (py * 320 + px) * 2, mix(t))
            cx += w
        return cx


class Calculator:
    def __init__(self, nwa, flash_start=EXT_FLASH_START, storage_file=None, nwlink="nwlink", external_data=None):
        self.flash_start = flash_start
        self.storage_file = storage_file
        with tempfile.TemporaryDirectory() as d:
            elf = os.path.join(d, "app.elf")
            cmd = [nwlink, "nwa-elf", nwa, elf, "--flash-start", hex(flash_start), "--flash-length",
                   hex(EXT_FLASH_END - flash_start), "--ram-start", hex(EXT_RAM_START), "--ram-length",
                   str(EXT_RAM_LEN), "--trampoline-start", hex(TRAMPOLINE)]
            if external_data:
                cmd += ["--external-data", external_data]
            subprocess.run(cmd, check=True)
            e = ELFFile(open(elf, "rb"))
            self.segments = [(s["p_paddr"], s.data()) for s in e.iter_segments()
                             if s["p_type"] == "PT_LOAD" and s["p_filesz"]]
            self.entry = e["e_entry"]
            self.symbols = {}
            self.functions = []  # (start, end, name) for --profile
            st = e.get_section_by_name(".symtab")
            if st:
                for s in st.iter_symbols():
                    if s.name:
                        self.symbols[s.name] = s["st_value"]
                        if s["st_info"]["type"] == "STT_FUNC" and s["st_size"]:
                            a = s["st_value"] & ~1
                            self.functions.append((a, a + s["st_size"], s.name))
            self.functions.sort()
        uc = self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        uc.mem_map(FLASH, FLASH_SIZE)
        uc.mem_map(SRAM, SRAM_SIZE)
        uc.mem_write(FLASH, b"\xff" * FLASH_SIZE)
        for addr, data in self.segments:
            uc.mem_write(addr, data)
        self.app_end = max(a + len(d) for a, d in self.segments if a >= FLASH)
        # userland header (Ion::Device::UserlandHeader) and slot info, as apps find them
        if SYSTEM == "upsilon":  # no device name; then Omega's and Upsilon's blocks
            hdr = struct.pack("<I8sIIIIIIII4sI8s16sII16sII", 0xDEC0EDFE, b"15.5.0\0\0", STORAGE, STORAGE_SIZE,
                              EXT_FLASH_START, 0x907FFFFF, EXT_RAM_START, EXT_RAM_END, 0xDEC0EDFE, 0xEFBEADDE,
                              b"1.0\0", DRAW_STRING_HOOK | 1, bytes(8), bytes(16), 0xEFBEADDE, 0x55707369,
                              b"1.1.2", 0x79827178, 0x55707369)
        else:
            hdr = struct.pack("<I8sIIIIIIIII", 0xDEC0EDFE, b"25.2.2\0\0", STORAGE, STORAGE_SIZE, EXT_FLASH_START,
                              EXT_FLASH_END, EXT_RAM_START, EXT_RAM_END, 0x903F0000, 0x903F0400, 0xDEC0EDFE)
        uc.mem_write(USERLAND_HEADER, hdr)
        uc.mem_write(SRAM, struct.pack("<IIII", 0xEFEEDBBA, 0x90000000, USERLAND_HEADER, 0xEFEEDBBA))
        # Epsilon's file system object: magic, buffer, magic, then private members
        storage = bytearray(STORAGE_SIZE)
        if storage_file and os.path.exists(storage_file):
            saved = open(storage_file, "rb").read()[:STORAGE_SIZE]
            storage[:len(saved)] = saved
        uc.mem_write(STORAGE, struct.pack("<I", 0xEE0BDDBA) + bytes(storage) + struct.pack("<I", 0xEE0BDDBA))
        # then, as in Epsilon's FileSystem: delegate, record name verifier (128 bytes),
        # accessible size, and the cache of the last record looked up (checksum, pointer)
        self.fs_private = STORAGE + 8 + STORAGE_SIZE
        if SYSTEM == "upsilon":  # Epsilon 15's: the delegate, then the cache
            self.cache_at = self.fs_private + 4
            uc.mem_write(self.fs_private, struct.pack("<III", SRAM + 0xF00, 0, 0))
        else:
            self.cache_at = self.fs_private + 4 + 128 + 4
            uc.mem_write(self.fs_private, struct.pack("<I", SRAM + 0xF00) + bytes(128) +
                         struct.pack("<III", STORAGE_SIZE, 0, 0))
        # trampoline: entry 0 is the firmware's draw string
        uc.mem_write(TRAMPOLINE, struct.pack("<I", DRAW_STRING_HOOK | 1))
        uc.mem_write(DRAW_STRING_HOOK, b"\x70\x47" * 8)
        uc.mem_write(EXIT_HOOK, b"\x70\x47" * 8)
        self.screen = bytearray(320 * 240 * 2)
        self.font = Font()
        self.keys = []          # (start_ms, end_ms, key index)
        self.now_ms = 0.0
        self.slept_ms = 0.0     # time the app spent in msleep/usleep (its idle time)
        self.samples = None     # --profile: pc samples, one per emulated millisecond
        self.rng = 0x2545F491
        self.events = []
        self.flash_log = []
        self.frames = []
        self.frame_times = []
        self.shots = {}
        self.pending_shots = []
        self.insns = 0
        self.exited = False
        self.svc_counts = {}
        self.locks = self.max_locks = 0
        self.intr_hook = uc.hook_add(UC_HOOK_INTR, self._intr)
        uc.hook_add(UC_HOOK_CODE, self._draw_string, begin=DRAW_STRING_HOOK, end=DRAW_STRING_HOOK + 1)
        uc.hook_add(UC_HOOK_CODE, self._exit, begin=EXIT_HOOK, end=EXIT_HOOK + 1)
        uc.hook_add(UC_HOOK_MEM_INVALID, self._invalid)
        uc.reg_write(UC_ARM_REG_SP, STACK_TOP)
        uc.reg_write(UC_ARM_REG_LR, EXIT_HOOK | 1)
        self.pc = self.entry | 1

    # ------------------------------------------------------------ checks
    def enable_checks(self, reads=True):
        """Watch for what would crash or corrupt a real calculator: unaligned
        multi-word loads (LDRD, LDM, VLDR... fault on the Cortex-M7), writes to
        flash outside the flash system calls, changes to the firmware's RAM
        other than its file system, and the deepest stack use.
        (Unicorn mis-runs some code with write hooks on RAM, so RAM writes are
        checked by comparing snapshots, and the stack is painted.)"""
        self.violations = []
        if reads:  # loads from the file system: records sit at any byte offset
            self.uc.hook_add(UC_HOOK_MEM_READ, self._check, begin=STORAGE, end=STORAGE + STORAGE_SIZE + 8)
        self.uc.hook_add(UC_HOOK_MEM_WRITE, self._flash_write, begin=FLASH, end=FLASH + FLASH_SIZE - 1)
        self.uc.mem_write(EXT_RAM_END, b"\xa5" * (STACK_TOP - EXT_RAM_END))
        self.ram_before = bytes(self.uc.mem_read(SRAM, SRAM_SIZE))

    def _flash_write(self, uc, access, addr, size, value, data):
        self.violations.append(f"write to flash at {addr:#x} (pc {uc.reg_read(UC_ARM_REG_PC):#x})")

    def stack_used(self):
        stack = bytes(self.uc.mem_read(EXT_RAM_END, STACK_TOP - EXT_RAM_END))
        i = 0
        while i < len(stack) and stack[i] == 0xA5:
            i += 1
        return len(stack) - i

    def firmware_ram_changes(self):
        """Offsets of firmware RAM that changed, outside the file system."""
        now = bytes(self.uc.mem_read(SRAM, SRAM_SIZE))
        allowed = [(EXT_RAM_START, STACK_TOP), (STORAGE + 4, STORAGE + 4 + STORAGE_SIZE),
                   (self.cache_at, self.cache_at + 8)]
        changed = []
        for i in range(0, SRAM_SIZE, 4):
            if now[i:i + 4] != self.ram_before[i:i + 4]:
                a = SRAM + i
                if not any(lo <= a < hi for lo, hi in allowed):
                    changed.append(a)
        return changed

    def _needs_alignment(self, pc):
        hw1, = struct.unpack("<H", self.uc.mem_read(pc, 2))
        if hw1 >> 11 in (0x18, 0x19):  # 16-bit LDM/STM
            return "LDM/STM"
        if hw1 < 0xE800:
            return None
        hw2, = struct.unpack("<H", self.uc.mem_read(pc + 2, 2))
        if hw1 & 0xFE40 == 0xE800:
            return "LDM/STM"
        if hw1 & 0xFE40 == 0xE840:
            return "LDRD/STRD/LDREX"
        if hw1 & 0xFE00 == 0xEC00 and (hw2 >> 9) & 7 == 5:
            return "VLDR/VSTR/VLDM/VSTM"
        return None

    def _check(self, uc, access, addr, size, value, data):
        if addr & 3:
            pc = uc.reg_read(UC_ARM_REG_PC)
            kind = self._needs_alignment(pc)
            if kind:
                self.violations.append(f"unaligned {kind} at {addr:#x} (pc {pc:#x})")

    def app_ram_clean(self):
        return not any(self.uc.mem_read(EXT_RAM_START, EXT_RAM_LEN))

    # ------------------------------------------------------------ helpers
    def reg(self, r):
        return self.uc.reg_read(r)

    def keyboard_state(self):
        s = 0
        for a, b, k in self.keys:
            if a <= self.now_ms < b:
                s |= 1 << k
        return s

    def storage_bytes(self):
        return bytes(self.uc.mem_read(STORAGE + 4, STORAGE_SIZE))

    def set_records(self, records, cached=None):
        """Fills the file system with (name, content) records; `cached` names the
        record Epsilon's lookup cache points to."""
        buf = bytearray(STORAGE_SIZE)
        p = 0
        where = {}
        for name, content in records:
            size = 2 + len(name) + 1 + len(content)
            struct.pack_into("<H", buf, p, size)
            buf[p + 2:p + size] = name.encode() + b"\0" + content
            where[name] = p
            p += size
        self.uc.mem_write(STORAGE + 4, bytes(buf))
        crc = ptr = 0
        if cached:
            crc, ptr = record_crc(cached), STORAGE + 4 + where[cached]
        self.uc.mem_write(self.cache_at, struct.pack("<II", crc, ptr))

    def cache(self):
        return struct.unpack("<II", self.uc.mem_read(self.cache_at, 8))

    def records(self):
        buf = self.storage_bytes()
        out, p = [], 0
        while p + 2 <= len(buf):
            size, = struct.unpack_from("<H", buf, p)
            if size == 0:
                break
            name = buf[p + 2:buf.index(b"\0", p + 2)]
            out.append((name.decode("latin1"), size))
            p += size
        return out

    def save_png(self, path):
        from PIL import Image
        im = Image.new("RGB", (320, 240))
        px = []
        for i in range(0, len(self.screen), 2):
            v = self.screen[i] | self.screen[i + 1] << 8
            px.append((((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31))
        im.putdata(px)
        im.save(path)

    def frame_image(self):
        from PIL import Image
        import numpy as np
        a = np.frombuffer(bytes(self.screen), dtype="<u2").reshape(240, 320)
        rgb = np.stack([((a >> 11) & 31) * 255 // 31, ((a >> 5) & 63) * 255 // 63, (a & 31) * 255 // 31], -1)
        return Image.fromarray(rgb.astype("uint8"), "RGB")

    # ------------------------------------------------------------ hooks
    def _invalid(self, uc, access, addr, size, value, data):
        print(f"emu: invalid memory access at {addr:#x} (pc {uc.reg_read(UC_ARM_REG_PC):#x})", file=sys.stderr)
        return False

    def _exit(self, uc, addr, size, data):
        self.exited = True
        uc.emu_stop()

    def _draw_string(self, uc, addr, size, data):
        text_p, point, large, fg = (self.reg(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3))
        sp = self.reg(UC_ARM_REG_SP)
        bg, = struct.unpack("<H", uc.mem_read(sp, 2))
        raw = bytes(uc.mem_read(text_p, 256))
        text = raw[:raw.index(b"\0")].decode("utf-8", "replace")
        self.font.draw(self.screen, text, point & 0xFFFF, point >> 16, bool(large & 0xFF), fg & 0xFFFF, bg)
        uc.reg_write(UC_ARM_REG_PC, self.reg(UC_ARM_REG_LR))

    def _intr(self, uc, intno, data):
        if intno != 2:  # EXCP_SWI
            print(f"emu: exception {intno} at pc {self.reg(UC_ARM_REG_PC):#x}", file=sys.stderr)
            uc.emu_stop()
            self.exited = True
            return
        pc = self.reg(UC_ARM_REG_PC)
        op, = struct.unpack("<H", uc.mem_read(pc - 2, 2))
        n = op & 0xFF
        self.svc_counts[n] = self.svc_counts.get(n, 0) + 1
        if SYSTEM == "upsilon" and n not in (1, 2, 3, 4, 5, 18, 19, 20, 21, 23, 34, 45, 48, 49, 50):
            if hasattr(self, "violations"):
                self.violations.append(f"system call {n}, which Upsilon doesn't have")
        r0, r1, r2, r3 = (self.reg(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3))
        ret = None
        ret1 = None
        if n in (18, 19, 20):  # pull, push, push uniform
            x, y, w, h = r0 & 0xFFFF, r0 >> 16, r1 & 0xFFFF, r1 >> 16
            if x + w > 320 or y + h > 240:
                print(f"emu: rect out of screen {x},{y} {w}x{h}", file=sys.stderr)
                w, h = max(0, min(w, 320 - x)), max(0, min(h, 240 - y))
            if n == 20:
                c = struct.pack("<H", r2 & 0xFFFF) * w
                for j in range(h):
                    o = ((y + j) * 320 + x) * 2
                    self.screen[o:o + 2 * w] = c
            elif n == 19:
                src = bytes(uc.mem_read(r2, w * h * 2)) if w and h else b""
                for j in range(h):
                    o = ((y + j) * 320 + x) * 2
                    self.screen[o:o + 2 * w] = src[j * w * 2:(j + 1) * w * 2]
            else:
                out = bytearray()
                for j in range(h):
                    o = ((y + j) * 320 + x) * 2
                    out += self.screen[o:o + 2 * w]
                if out:
                    uc.mem_write(r2, bytes(out))
            self.insns += w * h // 4   # the bus is not free
        elif n == 21:
            ret = 1
        elif n == 34 or n == 33:
            s = self.keyboard_state()
            ret, ret1 = s & 0xFFFFFFFF, s >> 32
        elif n == 48:
            ms = int(self.now_ms)
            ret, ret1 = ms & 0xFFFFFFFF, ms >> 32
        elif n == 49:
            self.now_ms += r0
            self.slept_ms += r0
        elif n == 50:
            self.now_ms += r0 / 1000
            self.slept_ms += r0 / 1000
        elif n == 45:
            self.rng ^= (self.rng << 13) & 0xFFFFFFFF
            self.rng ^= self.rng >> 17
            self.rng ^= (self.rng << 5) & 0xFFFFFFFF
            ret = self.rng
        elif n == 23:  # event get
            ret = 216  # Ion::Events::None
            s = self.keyboard_state()
            for k in range(64):
                if s >> k & 1:
                    ret = k
                    break
            if ret == 216:
                self.now_ms += 10
        elif n in (1,):
            ret = 128
        elif n == 2:
            pass
        elif n == 3:
            ret = 0
        elif n == 4:
            ret = 3  # Ion::Battery::Charge::FULL
        elif n == 52:
            ret = 0
        elif n == 30:  # erase sector
            base, size = sector_range(r0)
            ok = EXT_FLASH_START <= base and base + size <= EXT_FLASH_END
            self.flash_log.append(("erase", r0, base, size, ok))
            if ok:
                uc.mem_write(base, b"\xff" * size)
                self.now_ms += 400
            ret = 1 if ok else 0
        elif n == 32:  # write memory
            dst, src, length = r0, r1, r2
            ok = EXT_FLASH_START <= dst and dst + length <= EXT_FLASH_END
            self.flash_log.append(("write", dst, length, ok))
            if ok:
                old = bytes(uc.mem_read(dst, length))
                new = bytes(uc.mem_read(src, length))
                uc.mem_write(dst, bytes(a & b for a, b in zip(old, new)))
                self.now_ms += length / 1024
            ret = 1 if ok else 0
        elif n == 10:  # circuit breaker lock: Home cannot interrupt until unlock
            self.locks += 1
            self.max_locks = max(self.max_locks, self.locks)
        elif n == 13:
            self.locks -= 1
        elif n == 15:  # Ion::crc32Byte(data, length)
            ret = epsilon_crc32(bytes(uc.mem_read(r0, r1)))
        elif n == 16:  # Ion::crc32DoubleWord(data, words)
            ret = epsilon_crc32(bytes(uc.mem_read(r0, 4 * r1)))
        else:
            print(f"emu: unhandled svc {n} at {pc - 2:#x}", file=sys.stderr)
        if ret is not None:
            uc.reg_write(UC_ARM_REG_R0, ret & 0xFFFFFFFF)
        if ret1 is not None:
            uc.reg_write(UC_ARM_REG_R1, ret1 & 0xFFFFFFFF)

    # ------------------------------------------------------------ running
    def run(self, until_ms, on_frame=None, frame_ms=None):
        chunk = CPU_HZ // 1000  # about one millisecond of instructions
        next_frame = self.now_ms if frame_ms else None
        while not self.exited and self.now_ms < until_ms:
            try:
                self.uc.emu_start(self.pc, 0xFFFFFFFF, count=chunk)
            except UcError as err:
                pc = self.reg(UC_ARM_REG_PC)
                raise RuntimeError(f"CPU fault {err} at pc {pc:#x}") from None
            self.pc = self.reg(UC_ARM_REG_PC) | 1
            if self.samples is not None:
                self.samples.append(self.pc & ~1)
            self.insns += chunk
            self.now_ms += 1
            while self.pending_shots and self.pending_shots[0][0] <= self.now_ms:
                t, path = self.pending_shots.pop(0)
                self.save_png(path)
            if frame_ms and self.now_ms >= next_frame:
                if on_frame:
                    on_frame(self)
                next_frame += frame_ms
        if self.storage_file:
            open(self.storage_file, "wb").write(self.storage_bytes())


def parse_keys(spec):
    out = []
    for item in filter(None, spec.split(",")):
        rng, k = item.split(":")
        a, _, b = rng.partition("-")
        a = int(a)
        b = int(b) if b else a + 120
        out.append((a, b, KEYS[k] if k in KEYS else int(k)))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("nwa")
    ap.add_argument("--ms", type=int, default=3000)
    ap.add_argument("--keys", default="")
    ap.add_argument("--shots", default="", help="comma separated times (ms)")
    ap.add_argument("--gif", default="", help="start-end (ms) of a GIF recording")
    ap.add_argument("--fps", type=int, default=25)
    ap.add_argument("--out", default=".")
    ap.add_argument("--storage", help="file keeping the calculator's storage between runs")
    ap.add_argument("--flash-start", type=lambda s: int(s, 0), default=EXT_FLASH_START)
    ap.add_argument("--nwlink", default="nwlink")
    ap.add_argument("--model", choices=["n0120", "n0110"], default="n0120")
    ap.add_argument("--system", choices=["epsilon", "upsilon"], default="epsilon")
    ap.add_argument("--ram-length", type=int, help="the RAM apps get (default: the system's)")
    ap.add_argument("--records", action="store_true", help="list storage records at the end")
    ap.add_argument("--frames", help="save raw RGB565 frames here (for tools/record.py --frames)")
    ap.add_argument("--profile", type=int, default=0,
                    help="print the N functions the CPU was found in most (build the .nwa without stripping it)")
    a = ap.parse_args()
    configure(a.model, a.system, a.ram_length)
    os.makedirs(a.out, exist_ok=True)
    c = Calculator(a.nwa, a.flash_start, a.storage, a.nwlink)
    c.keys = parse_keys(a.keys)
    if a.profile:
        c.samples = []
    c.pending_shots = sorted((int(t), os.path.join(a.out, f"shot_{int(t):06d}.png"))
                             for t in filter(None, a.shots.split(",")))
    frames = []
    g0, g1 = (int(x) for x in a.gif.split("-")) if a.gif else (None, None)

    count = [0]

    def grab(calc):
        if g0 is not None and g0 <= calc.now_ms <= g1:
            frames.append(calc.frame_image())
        if a.frames:
            name = f"{count[0]:06d}_{int(calc.now_ms):06d}.raw"
            open(os.path.join(a.frames, name), "wb").write(bytes(calc.screen))
            count[0] += 1

    if a.frames:
        os.makedirs(a.frames, exist_ok=True)
    recording = a.gif or a.frames
    c.run(a.ms, grab if recording else None, 1000 / a.fps if recording else None)
    if frames:
        frames[0].save(os.path.join(a.out, "anim.gif"), save_all=True, append_images=frames[1:],
                       duration=int(1000 / a.fps), loop=0)
    c.save_png(os.path.join(a.out, "last.png"))
    print(f"emu: {c.now_ms:.0f} ms, {c.insns / 1e6:.1f} M instructions, exited={c.exited}, "
          f"idle {100 * c.slept_ms / max(c.now_ms, 1):.0f}%")
    if "perf_frames" in c.symbols:   # apps may count their frames in a uint32_t perf_frames
        n = struct.unpack("<I", bytes(c.uc.mem_read(c.symbols["perf_frames"], 4)))[0]
        print(f"emu: {n} frames, {n * 1000 / max(c.now_ms, 1):.1f} fps, "
              f"{(c.now_ms - c.slept_ms) / max(n, 1):.1f} ms of work per frame", end="")
        if "perf_max" in c.symbols:
            print(f", slowest {struct.unpack('<I', bytes(c.uc.mem_read(c.symbols['perf_max'], 4)))[0]} ms", end="")
        if "perf_slow" in c.symbols:
            print(f", {struct.unpack('<I', bytes(c.uc.mem_read(c.symbols['perf_slow'], 4)))[0]} over 33 ms", end="")
        print()
    if a.profile and c.samples:
        import bisect
        from collections import Counter
        starts = [f[0] for f in c.functions]
        hits = Counter()
        for pc in c.samples:
            i = bisect.bisect_right(starts, pc) - 1
            hits[c.functions[i][2] if i >= 0 and pc < c.functions[i][1] else f"?{pc:#x}"] += 1
        for name, n in hits.most_common(a.profile):
            print(f"emu: {100 * n / len(c.samples):5.1f}%  {name}")
    if c.flash_log:
        print("emu: flash operations:", len(c.flash_log), "failed:", sum(1 for x in c.flash_log if not x[-1]))
    if a.records:
        print("emu: records:", c.records())


if __name__ == "__main__":
    main()
