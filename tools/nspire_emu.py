#!/usr/bin/env python3
"""Run a game's TI-Nspire build (platform/port.mk, TARGET=nspire) in the Unicorn CPU emulator, as a TI-Nspire CX II.

Firebird needs a real calculator's boot images, so this runs the program on its own. The .tns is loaded as Ndless
r2023 loads it (the Zehn file found in the PRG, its relocations applied, .bss zeroed) and run on an ARM926EJ-S in
ARM mode, in supervisor mode with interrupts off, from the stack Ndless gives it. Around it:

- system calls: each swi is served here with Ndless's numbers (ndless-sdk/include/syscall-list.h): the heap
  (malloc, free, realloc, calloc, memset...), Nucleus's files (fopen, fread, fwrite, fseek, stat, mkdir, unlink,
  rename...) kept in a folder (--saves), puts and printf to stdout, the hardware queries (a CX II: hwtype 1,
  hwsubtype 2, a touchpad keypad), lcd_init, and lcd_blit, whose blit function is ours and takes each frame;
- the hardware the program reads itself: the keypad rows (0x900E0010), the touchpad's arrows (touchpad_read), the
  ON key (0x90140810), the SP804 timers (0x900D0000: msleep's, and the second one NumPlay's clock runs, 32768 Hz),
  the interrupt controller msleep masks, the RTC, the LCD controller's registers;
- time: the instructions run, at --cpi cycles each on a --mhz CPU (1.3 at 396 MHz, a CX II), plus what wfi waits,
  what np_nspire_pause waits (np_nspire.c's loop on the clock, skipped), the frame copies and rough costs for the
  OS's calls (memset by size, file calls by milliseconds);
- checks, each stopping the run or listed at the end: unaligned word and halfword loads and stores (an ARM926 rotates
  an unaligned LDR and stores to the aligned word, so they're silent bugs there: unlike the NumWorks, nothing
  faults), LDRD/STRD at 4 mod 8 (UNPREDICTABLE on ARMv5TE), an access outside the program, its heap and stack, an
  unmodelled register, an unknown system call, an undefined instruction (VFP code among them: there's no FPU), a
  wfi nothing would wake, a call to Ndless's msleep (on real calculators it waited for good, every interrupt but the
  timer's masked, after a few hundred calls), a bad free, a file left open, and a screen not shown while the game
  waits (np_nspire.c's buffer still dirty, no lcd_blit);
- speed: the game's frames (its perf_frames when it counts them, else the screens shown) and the CPU's busy time,
  as the frame rate a 396 MHz ARM926 would give (--model cx: a 132 MHz CX); --profile shows where the time goes.

  nspire_emu.py games/snake/build/nspire/snake/snake.elf --keys "1500-1600:ok,3500:ok" \\
      --shots "2000:a.png,6000:b.png" --quit-ms 8000
  nspire_emu.py all [game...] [--quit-ms 7000] [--fuzz SEED] [-j 6]
      every built game (build/nspire/<id>.tns), a line each, screens in build/ports/nspire.png

Keys (--keys "from-to:key,at:key", like NP_KEYS) are the NumWorks keys as platform/nspire/np_nspire.c maps them:
left right up down ok exe back home backspace shift alpha toolbox var xnt 0-9 (or zero...nine) plus minus mul div
dot comma lparen rparen power square pi sin cos tan ee ans onoff, and click (the touchpad's middle).

Not checked: the real caches and SDRAM timing (the frame rates are estimates), the OS's own code (Ndless and the
OS calls are modelled, not run), interrupts other than the timers, the rotated 240x320 LCD of some CXs (HW-W), real
flash timing, the touchpad's I2C time (a guess: 0.2 ms a read), and anything the keys don't reach."""
import argparse
import bisect
import ctypes
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zlib

from unicorn import (UC_ARCH_ARM, UC_ERR_INSN_INVALID, UC_HOOK_BLOCK, UC_HOOK_CODE, UC_HOOK_INTR, UC_HOOK_MEM_INVALID,
                     UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_MODE_ARM, Uc, UcError)
from unicorn.arm_const import (UC_ARM_REG_CPSR, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_R1,
                               UC_ARM_REG_FPEXC, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP, UC_CPU_ARM_926)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NDLESS_SDK = os.environ.get("NDLESS_SDK", os.path.expanduser("~/Documents/Assets/toolchains/ndless-src/ndless-sdk"))

# ---- the calculator (a CX II) ----
MHZ, CPI = 396, 1.3
MODELS = {"cx2": 396, "cx": 132}  # their CPU clocks (MHz)
OS_AREA = 0x10000000          # what the OS and Ndless own: the stubs, FILE objects, errno, argv
EXIT_STUB = OS_AREA           # where main's caller returns to
BLIT_STUB = OS_AREA + 0x10    # the blit function nl_lcd_blit hands out
ERRNO = OS_AREA + 0x100
KEYPAD_TYPE = OS_AREA + 0x104  # 4: a touchpad
STD_FILES = OS_AREA + 0x200   # stdin, stdout, stderr (64 bytes each)
ARGV = OS_AREA + 0x400
FILES = OS_AREA + 0x1000      # the open files' NUC_FILE objects
FB = 0x10100000               # the LCD's frame buffer (REAL_SCREEN_BASE_ADDRESS)
FB_SIZE = 320 * 240 * 2
BASE = 0x10400000             # where execmem_alloc puts the program (8-aligned, as its ELF assumes)
STACK_LOW, STACK_TOP = 0x17FE0000, 0x18008000  # Ndless's 128 KB under the OS task's stack, and 32 KB of that
BLIT_CYCLES = 120_000         # a 150 KB copy to the frame buffer
SYSCALL_CYCLES = 150
FILE_MS = 5                   # an open, close, stat, mkdir... on the NAND file system (a guess)
TOUCHPAD_MS = 0.2             # a touchpad_read: an I2C transfer of a few bytes at 400 kHz
TIMER_HZ = 32768

KEYS_TPAD = {  # Ndless keys.h, touchpad keypads (CX, CX II): row, bit; or the touchpad's region
    "RET": (0x10, 0x001), "ENTER": (0x10, 0x002), "NEGATIVE": (0x10, 0x008), "PERIOD": (0x1A, 0x010),
    "0": (0x10, 0x080), "COMMA": (0x1E, 0x400), "PLUS": (0x1C, 0x004), "3": (0x12, 0x008), "2": (0x1C, 0x010),
    "1": (0x12, 0x080), "PI": (0x12, 0x100), "MINUS": (0x1A, 0x004), "6": (0x14, 0x008), "5": (0x1A, 0x040),
    "4": (0x14, 0x080), "EE": (0x14, 0x100), "MULTIPLY": (0x18, 0x100), "9": (0x16, 0x008), "8": (0x1C, 0x040),
    "7": (0x16, 0x080), "SQU": (0x14, 0x200), "DIVIDE": (0x16, 0x100), "TAN": (0x18, 0x008), "EXP": (0x18, 0x200),
    "RP": (0x1A, 0x008), "LP": (0x1A, 0x020), "VAR": (0x1A, 0x002), "DEL": (0x1A, 0x200), "HOME": (0x10, 0x200),
    "MENU": (0x1C, 0x020), "ESC": (0x1C, 0x080), "TAB": (0x1C, 0x200), "SHIFT": (0x1E, 0x100), "CTRL": (0x1E, 0x200),
    "TRIG": (0x12, 0x200), "DOC": (0x1C, 0x008), "CAT": (0x1A, 0x080), "eEXP": (0x16, 0x200), "SPACE": (0x10, 0x010),
    "QUESEXCL": (0x10, 0x100), "A": (0x18, 0x040), "B": (0x18, 0x020), "C": (0x18, 0x010), "D": (0x18, 0x004),
    "E": (0x18, 0x002), "F": (0x18, 0x001), "G": (0x16, 0x040), "H": (0x16, 0x020), "I": (0x16, 0x010),
    "J": (0x16, 0x004), "K": (0x16, 0x002), "L": (0x16, 0x001), "M": (0x14, 0x040), "N": (0x14, 0x020),
    "O": (0x14, 0x010), "P": (0x14, 0x004), "Q": (0x14, 0x002), "R": (0x14, 0x001), "S": (0x12, 0x040),
    "T": (0x12, 0x020), "U": (0x12, 0x010), "V": (0x12, 0x004), "W": (0x12, 0x002), "X": (0x12, 0x001),
    "Y": (0x10, 0x040), "Z": (0x10, 0x020),
    # SIN and COS are on the dummy key there (0x1C, 0x400): never pressed (np_nspire.c reads them as G and H)
    "UP": "up", "RIGHT": "right", "DOWN": "down", "LEFT": "left", "CLICK": "click", "ON": "on",
}
NAMES = {  # the NumWorks key -> the Nspire key np_nspire.c reads for it
    "left": "LEFT", "right": "RIGHT", "up": "UP", "down": "DOWN", "ok": "ENTER", "click": "CLICK", "exe": "RET",
    "back": "ESC", "home": "HOME", "backspace": "DEL", "shift": "SHIFT", "alpha": "CTRL", "toolbox": "MENU",
    "var": "VAR", "xnt": "TAB", "plus": "PLUS", "minus": "MINUS", "mul": "MULTIPLY", "div": "DIVIDE", "dot": "PERIOD",
    "comma": "COMMA", "lparen": "LP", "rparen": "RP", "power": "EXP", "square": "SQU", "pi": "PI", "tan": "TAN",
    "ee": "EE", "ans": "NEGATIVE", "onoff": "ON", "sin": "G", "cos": "H", "exp": "eEXP", "ln": "B", "log": "C",
    "i": "D", "sqrt": "K",
}
for _i, _n in enumerate("zero one two three four five six seven eight nine".split()):
    NAMES[_n] = NAMES[str(_i)] = str(_i)
TPAD_W, TPAD_H = 2328, 1691   # the Synaptics touchpad's size (its page 0x10)

ZEHN_FLAGS = {8: "name", 9: "author", 10: "version", 11: "notice", 12: "runs on HW-W", 13: "uses lcd_blit"}


# ---- the hooks that run per block and per memory access, in C (from Python they'd be 100 times slower) ----
NATIVE_C = r"""
#include <stdint.h>
typedef int (*reg_read_t)(void *, int, void *);
typedef int (*emu_stop_t)(void *);
static reg_read_t reg_read;
static emu_stop_t emu_stop;
static int pc_reg;
static const uint8_t *dmap; /* the program's LDRD/STRD: a bit a word */
static uint32_t dmap_base, dmap_words, last_addr;

uint64_t npe_insns, npe_stop_at, npe_unaligned;
struct npe_site { uint32_t pc, addr, count, kind; }; /* kind: size (2, 4; 8 for LDRD/STRD), 0x10 a store */
#define NSITES 1024
struct npe_site npe_sites[NSITES];
uint32_t npe_nsites;

void npe_init(void *rr, void *es, int pc, const uint8_t *map, uint32_t base, uint32_t words) {
  reg_read = (reg_read_t)rr, emu_stop = (emu_stop_t)es, pc_reg = pc;
  dmap = map, dmap_base = base, dmap_words = words;
}
void npe_block(void *uc, uint64_t address, uint32_t size, void *user) {
  npe_insns += size >> 2;
  if (npe_insns >= npe_stop_at) emu_stop(uc);
}
static void note(uint32_t pc, uint32_t addr, uint32_t kind) {
  npe_unaligned++;
  for (uint32_t i = 0; i < npe_nsites; i++)
    if (npe_sites[i].pc == pc && npe_sites[i].kind == kind) { npe_sites[i].count++; return; }
  if (npe_nsites < NSITES) {
    struct npe_site *s = &npe_sites[npe_nsites++];
    s->pc = pc, s->addr = addr, s->count = 1, s->kind = kind;
  }
}
void npe_mem(void *uc, int type, uint64_t address, int size, int64_t value, void *user) {
  uint32_t a = (uint32_t)address, w = type == 17 ? 0x10 : 0, pc = 0;
  if (size == 4) {
    if (a & 3) reg_read(uc, pc_reg, &pc), note(pc, a, 4 | w);
    else if ((a & 4) && a != last_addr + 4 && dmap) { /* (an LDRD's second word follows its first) */
      reg_read(uc, pc_reg, &pc);
      uint32_t i = (pc - dmap_base) >> 2;
      if (i < dmap_words && (dmap[i >> 3] >> (i & 7) & 1)) note(pc, a, 8 | w);
    }
    last_addr = a;
  } else if (size == 2 && (a & 1)) reg_read(uc, pc_reg, &pc), note(pc, a, 2 | w);
}
"""


class Site(ctypes.Structure):
    _fields_ = [("pc", ctypes.c_uint32), ("addr", ctypes.c_uint32), ("count", ctypes.c_uint32), ("kind", ctypes.c_uint32)]


def native():
    """The C hooks, built once with the computer's compiler (cc) into build/nspire_emu."""
    tag = hashlib.sha1(NATIVE_C.encode()).hexdigest()[:10]
    d = os.path.join(ROOT, "build", "nspire_emu")
    lib = os.path.join(d, f"hooks_{tag}.so")
    if not os.path.exists(lib):
        os.makedirs(d, exist_ok=True)
        src = os.path.join(d, f"hooks_{tag}.c")
        tmp = f"{lib}.{os.getpid()}"
        with open(src, "w") as f:
            f.write(NATIVE_C)
        r = subprocess.run([os.environ.get("CC", "cc"), "-O2", "-shared", "-fPIC", "-o", tmp, src],
                           capture_output=True, text=True)
        if r.returncode:
            sys.exit(f"nspire_emu: can't build the C hooks ({r.stderr.strip()[:200]})")
        os.replace(tmp, lib)
    return ctypes.CDLL(lib)


def uc_lib():
    try:
        from unicorn.unicorn_py3 import unicorn as m
        return m.uclib, getattr(m, "uc_hook_h", ctypes.c_size_t)
    except ImportError:
        from unicorn import unicorn as m
        return m._uc, ctypes.c_size_t


# ---- the program ----
def syscall_names():
    names = {}
    path = os.path.join(NDLESS_SDK, "include", "syscall-list.h")
    if os.path.exists(path):
        for line in open(path):
            w = line.split()
            if len(w) >= 3 and w[0] == "#define" and w[1].startswith("e_") and w[2].isdigit():
                names[int(w[2])] = w[1][2:]
    ext = ["nl_osvalue", "nl_relocdatab", "nl_hwtype", "nl_isstartup", "nl_lua_getstate", "nl_set_resident",
           "nl_ndless_rev", "nl_no_scr_redraw", "nl_loaded_by_3rd_party_loader", "nl_hwsubtype", "nl_exec",
           "nl_osid", "_nl_hassyscall", "nl_lcd_blit", "nl_lcd_type", "nl_lcd_init"]
    for i, n in enumerate(ext):
        names[0x200000 | i] = n
    return names


SYSCALL_NAMES = syscall_names()


def sc_name(n):
    base = n & ~0x800000
    return SYSCALL_NAMES.get(base, f"#{n:#x}") + (" (variable)" if n & 0x800000 else "")


def find_files(path):
    """(the .tns or .zehn, the .elf or None) from either."""
    path = os.path.abspath(path)
    d, name = os.path.split(path)
    gid = name.rsplit(".", 1)[0]
    if path.endswith(".elf"):
        for z in (os.path.join(os.path.dirname(d), gid + ".tns"), os.path.join(d, gid + ".zehn")):
            if os.path.exists(z):
                return z, path
        sys.exit(f"nspire_emu: no {gid}.tns or {gid}.zehn next to {path}")
    elf = os.path.join(d, gid, gid + ".elf")
    if not os.path.exists(elf):
        elf = os.path.join(d, gid + ".elf")
    return path, elf if os.path.exists(elf) else None


def load_zehn(data):
    """Ndless's zehn_load: (header fields, relocations, flags, extra data, the executable, its whole size)."""
    if data[:4] == b"PRG\0":  # ndless_load: a Zehn file in the first 20 KB, at a word
        at = next((i for i in range(0, min(len(data), 20 * 1024) - 8, 4)
                   if data[i:i + 8] == b"Zehn\x01\0\0\0"), None)
    else:
        at = 0 if data[:8] == b"Zehn\x01\0\0\0" else None
    if at is None:
        raise ValueError("no Zehn file in it")
    sig, ver, file_size, nrel, nflag, extra, alloc, entry = struct.unpack_from("<8I", data, at)
    if file_size > alloc:
        raise ValueError("bad Zehn header")
    p = at + 32
    relocs = struct.unpack_from(f"<{nrel}I", data, p)
    p += 4 * nrel
    flags = struct.unpack_from(f"<{nflag}I", data, p)
    p += 4 * nflag
    extra_data = data[p:p + extra]
    p += extra
    mem = alloc - (p - at)
    body = data[p:at + file_size]
    if relocs and relocs[0] & 0xFF == 3:
        body = zlib.decompress(body)
    image = bytearray(body) + bytes(mem - len(body))
    return dict(entry=entry, relocs=relocs, flags=flags, extra=extra_data, image=image, size=mem)


def relocate(z, base):
    img = z["image"]
    for r in z["relocs"]:
        kind, off = r & 0xFF, r >> 8
        if off >= len(img):
            raise ValueError("relocation out of the program")
        if kind == 0:
            struct.pack_into("<I", img, off, (struct.unpack_from("<I", img, off)[0] + base) & 0xFFFFFFFF)
        elif kind == 1:
            while struct.unpack_from("<I", img, off)[0] != 0xFFFFFFFF:
                struct.pack_into("<I", img, off, (struct.unpack_from("<I", img, off)[0] + base) & 0xFFFFFFFF)
                off += 4
        elif kind == 2:
            struct.pack_into("<I", img, off, 0)
        elif kind not in (3, 4):
            raise ValueError(f"relocation type {kind}")


class Program:
    """The ELF's symbols and what its code contains (system calls, wfi, LDRD/STRD, VFP, Thumb). Without the ELF,
    the whole image is read as ARM code."""

    def __init__(self, elf_path, image):
        self.elf = elf_path
        self.funcs = []
        self.swis, self.wfi, self.ldrd, self.vfp = {}, set(), set(), []
        self.thumb = 0
        self.symbols = {}  # the globals, and a file's statics as "file:name"
        maps, data, tbase = [], bytes(image), 0
        if elf_path:
            from elftools.elf.elffile import ELFFile
            with open(elf_path, "rb") as f:
                e = ELFFile(f)
                text = e.get_section_by_name(".text")
                data = text.data()
                tbase = text["sh_addr"]
                unit = ""
                for s in e.get_section_by_name(".symtab").iter_symbols():
                    kind = s["st_info"]["type"]
                    if kind == "STT_FILE":
                        unit = s.name
                    elif s.name.startswith("$") and s.name[1:2] in ("a", "t", "d"):
                        maps.append((s["st_value"], s.name[1]))
                    elif s.name:
                        local = s["st_info"]["bind"] == "STB_LOCAL"
                        self.symbols[f"{unit}:{s.name}" if local else s.name] = s["st_value"]
                        if s["st_info"]["type"] == "STT_FUNC":
                            self.funcs.append((s["st_value"] & ~1, s["st_value"] + max(s["st_size"], 1), s.name))
        self.funcs.sort()
        self.starts = [f[0] for f in self.funcs]
        maps.sort()
        kinds = [m[0] for m in maps]
        for off in range(0, len(data) - 3, 4):
            a = tbase + off
            i = bisect.bisect_right(kinds, a) - 1
            kind = maps[i][1] if i >= 0 else "a"
            if kind == "t":
                self.thumb += 4
                continue
            if kind != "a":
                continue
            w, = struct.unpack_from("<I", data, off)
            cond = w >> 28
            if cond == 0xF:
                continue
            if w & 0x0F000000 == 0x0F000000:
                n = w & 0xFFFFFF
                self.swis.setdefault(n, []).append(a)
            elif w & 0x0FFF0FFF == 0x0E070F90:
                self.wfi.add(a)
            elif w & 0x0E1000F0 in (0x000000D0, 0x000000F0):
                self.ldrd.add(a)
            elif (w & 0x0E000E00 == 0x0C000A00) or (w & 0x0F000E00 == 0x0E000A00):
                self.vfp.append(a)
        self.text_end = tbase + len(data)

    def where(self, off):
        """name+offset of a program offset."""
        i = bisect.bisect_right(getattr(self, "starts", []), off) - 1
        if i >= 0 and off < self.funcs[i][1]:
            return f"{self.funcs[i][2]}+{off - self.funcs[i][0]:#x}"
        return f"{off:#x}"

    def lines(self, offs):
        """file:line of program offsets (the SDK's addr2line), or {}."""
        tool = os.path.join(NDLESS_SDK, "toolchain", "install", "bin", "arm-none-eabi-addr2line")
        if not self.elf or not offs or not os.path.exists(tool):
            return {}
        offs = sorted(set(offs))
        r = subprocess.run([tool, "-e", self.elf] + [hex(o) for o in offs], capture_output=True, text=True)
        out = {}
        for o, line in zip(offs, r.stdout.splitlines()):
            if not line.startswith("??"):
                path, _, n = line.partition(":")
                out[o] = f"{os.path.relpath(path, ROOT) if path.startswith(ROOT) else os.path.basename(path)}:" \
                         f"{n.split()[0]}"
        return out


def decode(w):
    """The kind of load or store an ARM word is."""
    if w & 0x0E000000 == 0x08000000:
        return "LDM" if w >> 20 & 1 else "STM"
    if w & 0x0C000000 == 0x04000000:
        return ("LDR" if w >> 20 & 1 else "STR") + ("B" if w >> 22 & 1 else "")
    if w & 0x0E000090 == 0x00000090 and w & 0x60:
        l, op = w >> 20 & 1, w >> 5 & 3
        return {(1, 1): "LDRH", (0, 1): "STRH", (1, 2): "LDRSB", (0, 2): "LDRD", (1, 3): "LDRSH", (0, 3): "STRD"}[l, op]
    return f"{w:#010x}"


# ---- the hardware ----
class Timer:
    """One SP804 timer: Load, Value, Control, IntClr, RIS, MIS, BGLoad."""

    def __init__(self):
        self.load, self.ctrl, self.val, self.ris, self.t0 = 0, 0x20, 0xFFFFFFFF, 0, 0

    def width(self):
        return 0xFFFFFFFF if self.ctrl & 2 else 0xFFFF

    def div(self):
        return (1, 16, 256, 256)[self.ctrl >> 2 & 3]

    def sync(self, tick):
        if not self.ctrl & 0x80:
            self.t0 = tick
            return
        e = (tick - self.t0) // self.div()
        if e <= 0:
            return
        self.t0 += e * self.div()
        v = self.val
        if e < v:
            self.val = v - e
        elif self.ctrl & 1:  # one shot: stops at 0
            if v:
                self.ris = 1
            self.val = 0
        else:
            self.ris = 1
            top = self.load if self.ctrl & 0x40 else self.width()
            self.val = top - (e - v - 1) % (top + 1) if e > v else 0

    def next_event(self, tick):
        """The timer tick its interrupt comes at, or None."""
        if not self.ctrl & 0x80 or not self.ctrl & 0x20:
            return None
        if self.val:
            n = self.val
        elif self.ctrl & 1:
            return None
        else:
            n = (self.load if self.ctrl & 0x40 else self.width()) + 1
        return self.t0 + n * self.div()


class Nspire:
    def __init__(self, path, keys=(), saves=None, mhz=MHZ, cpi=CPI, align=True, heap_mb=16, model="cx2", gid=None):
        self.tns, elf = find_files(path)
        self.gid = gid or os.path.basename(self.tns).rsplit(".", 1)[0]
        self.hz, self.cpi = mhz * 1_000_000, cpi
        self.model = model
        self.findings, self.notes = [], []
        self.stopped = self.exited = False
        self.exit_code = None
        self.keys = list(keys)
        z = load_zehn(open(self.tns, "rb").read())
        self.zehn = z
        self.prog = Program(elf, z["image"])
        relocate(z, BASE)
        self.prog_end = BASE + z["size"]
        self.heap_lo = (self.prog_end + 0xFFF) & ~0xFFF
        self.heap_hi = self.heap_lo + heap_mb * 1024 * 1024
        self.free_list = [(self.heap_lo, self.heap_hi - self.heap_lo)]
        self.blocks, self.block_from, self.heap_used, self.heap_peak = {}, {}, 0, 0
        self.flags = {}
        for f in z["flags"]:
            kind, val = f & 0xFF, f >> 8
            if kind in (8, 9, 11):
                s = z["extra"][val:]
                val = s[:s.index(0)].decode("latin1")
            self.flags[ZEHN_FLAGS.get(kind, kind)] = val
        # files
        self.own_saves = saves is None
        self.saves = saves or tempfile.mkdtemp(prefix="nspire_")
        os.makedirs(os.path.join(self.saves, "documents", "ndless"), exist_ok=True)
        self.files = {}  # NUC_FILE address -> dict
        self.std = {STD_FILES: "stdin", STD_FILES + 64: "stdout", STD_FILES + 128: "stderr"}
        self.out_line = {}
        # hardware state
        self.timers = [Timer(), Timer()]
        self.vic_enable = 1 << 19
        self.lcd_base, self.lcd_ctrl = FB, 0x1829
        self.lcd_type = None
        self.irq_mask = 0xC0
        self.tpad_page = 4
        self.extra = 0.0      # cycles not from instructions: waits, copies, the OS's calls
        self.slept = 0.0      # of which wfi
        self.frames = []      # (cycles busy since the previous frame, cycles at the frame)
        self.last_frame_busy = 0.0
        self.on_frame = None
        self.syscalls = {}
        self.samples = None
        self.wall_limit = None
        self.last_wfi = None
        self.msleep_noted = False
        self.stale_since, self.stale_blits, self.stale_noted = None, 0, False
        # the CPU
        lib = self.lib = native()
        self.uclib, self.hook_t = uc_lib()
        uc = self.uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        uc.ctl_set_cpu_model(UC_CPU_ARM_926)
        uc.mem_map(OS_AREA, 0x100000)
        uc.mem_map(FB, (FB_SIZE + 0xFFF) & ~0xFFF)
        uc.mem_map(BASE, self.heap_hi - BASE)
        uc.mem_map(STACK_LOW, STACK_TOP - STACK_LOW)
        uc.mem_write(BASE, bytes(z["image"]))
        uc.mem_write(STACK_LOW, b"\xa5" * (STACK_TOP - STACK_LOW))  # painted: the deepest use shows
        uc.mem_write(BLIT_STUB, struct.pack("<I", 0xE12FFF1E))  # bx lr
        uc.mem_write(EXIT_STUB, struct.pack("<I", 0xE12FFF1E))
        uc.mem_write(KEYPAD_TYPE, b"\x04")
        argv0 = f"/documents/numplay/{self.gid}.tns".encode() + b"\0"
        uc.mem_write(ARGV, struct.pack("<II", ARGV + 16, 0) + bytes(8) + argv0)
        pmu = 0x90140000 if model == "cx2" else 0x900B0000  # (the ON key's register: +0x810 bit 8, +0x28 bit 4)
        for addr, size, name in ((0x900D0000, 0x1000, "timers"), (0x900E0000, 0x1000, "keypad"),
                                 (0x90090000, 0x1000, "rtc"), (pmu, 0x1000, "pmu"),
                                 (0xDC000000, 0x1000, "vic"), (0xC0000000, 0x1000, "lcd")):
            uc.mmio_map(addr, size, self._mmio_read, (addr, name), self._mmio_write, (addr, name))
        uc.hook_add(UC_HOOK_INTR, self._intr)
        uc.hook_add(UC_HOOK_MEM_INVALID, self._invalid)
        uc.hook_add(UC_HOOK_CODE, self._exit, begin=EXIT_STUB, end=EXIT_STUB)
        uc.hook_add(UC_HOOK_CODE, self._blit, begin=BLIT_STUB, end=BLIT_STUB)
        for name, hook in (("np_nspire_pause", self._pause), ("msleep", self._msleep)):
            if name in self.prog.symbols:
                uc.hook_add(UC_HOOK_CODE, hook, begin=BASE + self.prog.symbols[name], end=BASE + self.prog.symbols[name])
        # the C hooks
        self.dmap = None
        words = 0
        if align and self.prog.ldrd:
            words = (self.prog.text_end + 3) // 4
            buf = bytearray((words + 7) // 8)
            for a in self.prog.ldrd:
                buf[a >> 5] |= 1 << (a >> 2 & 7)
            self.dmap = (ctypes.c_uint8 * len(buf)).from_buffer_copy(buf)
        fp = lambda f: ctypes.cast(f, ctypes.c_void_p).value
        lib.npe_init.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_uint32,
                                 ctypes.c_uint32]
        lib.npe_init(fp(self.uclib.uc_reg_read), fp(self.uclib.uc_emu_stop), UC_ARM_REG_PC,
                     ctypes.cast(self.dmap, ctypes.c_void_p) if self.dmap is not None else None, BASE, words)
        self.insns = ctypes.c_uint64.in_dll(lib, "npe_insns")
        self.stop_at = ctypes.c_uint64.in_dll(lib, "npe_stop_at")
        self.insns.value = 0
        self.native_hooks = [self._native_hook(UC_HOOK_BLOCK, lib.npe_block)]
        if align:
            self.native_hooks.append(self._native_hook(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, lib.npe_mem))
        ctypes.c_uint32.in_dll(lib, "npe_nsites").value = 0
        ctypes.c_uint64.in_dll(lib, "npe_unaligned").value = 0
        self.align = align
        uc.reg_write(UC_ARM_REG_SP, STACK_TOP)
        uc.reg_write(UC_ARM_REG_LR, EXIT_STUB)
        uc.reg_write(UC_ARM_REG_R0, 1)
        uc.reg_write(UC_ARM_REG_R1, ARGV)
        uc.reg_write(UC_ARM_REG_FPEXC, 0)  # no FPU: a VFP instruction run is undefined
        self.pc = BASE + z["entry"]
        vfp = sorted({self.prog.where(a).split("+")[0] for a in self.prog.vfp} - {
            "__gnu_Unwind_Restore_VFP", "__gnu_Unwind_Save_VFP", "__gnu_Unwind_Restore_VFP_D",
            "__gnu_Unwind_Save_VFP_D", "__gnu_Unwind_Restore_VFP_D_16_to_31", "__gnu_Unwind_Save_VFP_D_16_to_31"})
        if vfp:  # (the unwinder's are only run for a C++ exception through VFP code)
            self.notes.append("VFP instructions in " + ", ".join(vfp[:6]))
        if self.prog.thumb:
            self.notes.append(f"{self.prog.thumb} bytes of Thumb code (instruction counts there are rough)")
        known = set(SYSCALLS)
        missing = sorted(n for n in self.prog.swis if n not in known)
        if missing and elf:
            self.notes.append("system calls in the code this doesn't serve: " +
                              ", ".join(f"{sc_name(n)} at {self.prog.where(self.prog.swis[n][0])}" for n in missing))

    def _native_hook(self, kind, func):
        h = self.hook_t()
        err = self.uclib.uc_hook_add(self.uc._uch, ctypes.byref(h), kind, ctypes.cast(func, ctypes.c_void_p), None,
                                     1, 0)
        if err:
            raise RuntimeError(f"uc_hook_add: {err}")
        return h

    # ---- time ----
    def cycles(self):
        return self.insns.value * self.cpi + self.extra

    def ms(self):
        return self.cycles() * 1000 / self.hz

    def tick(self):
        return int(self.cycles() * TIMER_HZ // self.hz)

    # ---- helpers ----
    def reg(self, r):
        return self.uc.reg_read(r)

    def args(self, n):
        a = [self.reg(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)][:n]
        if n > 4:
            a += struct.unpack(f"<{n - 4}I", self.uc.mem_read(self.reg(UC_ARM_REG_SP), 4 * (n - 4)))
        return a

    def cstr(self, p, limit=1024):
        out = bytearray()
        while len(out) < limit:
            chunk = bytes(self.uc.mem_read(p + len(out), 64))
            i = chunk.find(0)
            if i >= 0:
                return (out + chunk[:i]).decode("latin1")
            out += chunk
        return out.decode("latin1")

    def u16str(self, p, limit=512):
        out = []
        while len(out) < limit:
            c, = struct.unpack("<H", self.uc.mem_read(p + 2 * len(out), 2))
            if not c:
                break
            out.append(chr(c))
        return "".join(out)

    def site(self, pc):
        off = pc - BASE
        if 0 <= off < self.prog_end - BASE:
            line = self.prog.lines([off]).get(off)
            return f"{self.prog.where(off)}" + (f" ({line})" if line else "")
        return f"{pc:#x}"

    def stop(self, why, finding=True):
        if finding:
            self.findings.append(why)
        self.stopped = True
        self.uc.emu_stop()

    def held(self):
        t = self.ms()
        return {NAMES.get(k, k) for a, b, k in self.keys if a <= t <= b}

    # ---- the peripherals ----
    def _mmio_read(self, uc, off, size, data):
        base, name = data
        v = self.read_io(name, off, size)
        if v is None:
            self.stop(f"read of an unmodelled register: {name} +{off:#x} ({size} bytes) at "
                      f"{self.site(self.reg(UC_ARM_REG_PC))}")
            return 0
        return v

    def _mmio_write(self, uc, off, size, value, data):
        base, name = data
        if not self.write_io(name, off, size, value):
            self.stop(f"write of an unmodelled register: {name} +{off:#x} = {value:#x} at "
                      f"{self.site(self.reg(UC_ARM_REG_PC))}")

    def read_io(self, name, off, size):
        if name == "timers" and off < 0x40:
            t = self.timers[off >> 5]
            r = off & 0x1F
            t.sync(self.tick())
            return {0: t.load, 4: t.val, 8: t.ctrl, 0x10: t.ris, 0x14: t.ris & t.ctrl >> 5, 0x18: t.load}.get(r)
        if name == "keypad" and 0x10 <= off < 0x20:
            rows = self.key_rows()
            v = 0
            for i in range(0, size, 2):
                v |= rows.get(off + i, 0) << 8 * i
            return v
        if name == "pmu" and off == (0x810 if self.model == "cx2" else 0x28):
            return 0 if "ON" in self.held() else 0x100 if self.model == "cx2" else 0x10
        if name == "rtc" and off == 0:
            return 0x60000000 + int(self.ms() // 1000)
        if name == "vic" and off in (0, 8, 0x10):
            pending = self.pending()
            return {0: pending & self.vic_enable, 8: pending, 0x10: self.vic_enable}[off]
        if name == "lcd" and off in (0x10, 0x18):
            return self.lcd_base if off == 0x10 else self.lcd_ctrl
        return None

    def write_io(self, name, off, size, value):
        if name == "timers" and off < 0x40:
            t = self.timers[off >> 5]
            r = off & 0x1F
            now = self.tick()
            t.sync(now)
            if r == 0:
                t.load = t.val = value
                t.t0 = now
                if not value:  # (the SP804 interrupts at once: msleep(0) returns)
                    t.ris = 1
            elif r == 8:
                if value & 0x80 and not t.ctrl & 0x80:
                    t.t0 = now
                t.ctrl = value & 0xFF
            elif r == 0xC:
                t.ris = 0
            elif r == 0x18:
                t.load = value
            else:
                return False
            return True
        if name == "vic" and off in (0x10, 0x14):
            if off == 0x10:
                self.vic_enable |= value
            else:
                self.vic_enable &= ~value
            return True
        if name == "lcd" and off in (0x10, 0x18):
            if off == 0x10:
                self.lcd_base = value
            else:
                self.lcd_ctrl = value
            return True
        return False

    def pending(self):
        """The interrupt lines raised: the timers' (19)."""
        now = self.tick()
        p = 0
        for t in self.timers:
            t.sync(now)
            if t.ris and t.ctrl & 0x20:
                p |= 1 << 19
        return p

    def key_rows(self):
        rows = {}
        for k in self.held():
            v = KEYS_TPAD.get(k)
            if isinstance(v, tuple):
                rows[v[0]] = rows.get(v[0], 0) | v[1]
        return rows

    def touchpad_report(self):
        h = self.held()
        dx = ("RIGHT" in h) - ("LEFT" in h)
        dy = ("UP" in h) - ("DOWN" in h)
        if not (dx or dy or "CLICK" in h):
            return bytes(11)
        x = TPAD_W * (1 + dx * 0.8) // 2
        y = TPAD_H * (1 + dy * 0.8) // 2
        return struct.pack(">BBHHBBHB", 1, 100, int(x), int(y), 0, 0, 0, 1)

    def wfi(self):
        """wfi: on to the next interrupt."""
        self.check_shown()
        if self.pending() & self.vic_enable:
            return
        now = self.tick()
        ticks = [t.next_event(now) for t in self.timers]
        ticks = [t for t in ticks if t is not None]
        if not ticks or not self.vic_enable & 1 << 19:
            pc = self.reg(UC_ARM_REG_PC) - 4
            self.stop(f"wfi with no interrupt to come at {self.site(pc)}: the calculator would hang")
            return
        wait = (min(ticks) * self.hz + TIMER_HZ - 1) // TIMER_HZ - self.cycles()
        if wait > 0:
            self.extra += wait
            self.slept += wait

    # ---- the frames ----
    def check_shown(self):
        """Whether what the game drew is shown while the CPU waits: NumPlay's backend keeps the screen in its own
        buffer, marked dirty till lcd_blit. Dirty at each wait for 200 ms, with no lcd_blit: the screen is out of
        date."""
        a = self.prog.symbols.get("np_nspire.c:dirty")
        if a is None or self.stale_noted:
            return
        now = self.ms()
        if not self.uc.mem_read(BASE + a, 1)[0]:
            self.stale_since = None
        elif self.stale_since is None or self.stale_blits != len(self.frames):
            self.stale_since, self.stale_blits = now, len(self.frames)
        elif now - self.stale_since > 200:
            self.stale_noted = True
            self.findings.append(f"what was drawn by {self.stale_since:.0f} ms isn't shown {now - self.stale_since:.0f}"
                                 f" ms later while the game waits (np_nspire.c didn't lcd_blit)")

    def screen(self):
        try:
            return bytes(self.uc.mem_read(self.lcd_base, FB_SIZE))
        except UcError:
            return bytes(FB_SIZE)

    def png(self, path):
        from PIL import Image
        import numpy as np
        a = np.frombuffer(self.screen(), dtype="<u2").reshape(240, 320)
        rgb = np.stack([(a >> 11 & 31) * 255 // 31, (a >> 5 & 63) * 255 // 63, (a & 31) * 255 // 31], -1)
        Image.fromarray(rgb.astype("uint8"), "RGB").save(path)

    def _blit(self, uc, addr, size, data):
        buf = self.reg(UC_ARM_REG_R0)
        try:
            uc.mem_write(FB, bytes(uc.mem_read(buf, FB_SIZE)))
        except UcError:
            self.stop(f"lcd_blit of a buffer outside memory ({buf:#x}) from {self.site(self.reg(UC_ARM_REG_LR))}")
            return
        self.extra += BLIT_CYCLES
        busy = self.cycles() - self.slept
        self.frames.append((busy - self.last_frame_busy, self.cycles()))
        self.last_frame_busy = busy
        if self.on_frame:
            self.on_frame(self)

    def _pause(self, uc, addr, size, data):
        """np_nspire_pause(n): n ticks of the clock go by, as a wfi waits (its loop on the clock isn't run)"""
        self.check_shown()
        wait = self.reg(UC_ARM_REG_R0) * self.hz / TIMER_HZ
        self.extra += wait
        self.slept += wait
        uc.reg_write(UC_ARM_REG_PC, self.reg(UC_ARM_REG_LR))

    def _msleep(self, uc, addr, size, data):
        if not self.msleep_noted:
            self.msleep_noted = True
            self.findings.append(f"msleep called from {self.site(self.reg(UC_ARM_REG_LR))}: on a real calculator it "
                                 "can wait for good (np_nspire.c's np_nspire_pause waits on the clock instead)")

    def _exit(self, uc, addr, size, data):
        self.exited = True
        self.exit_code = self.reg(UC_ARM_REG_R0)
        self.stopped = True
        uc.emu_stop()

    def _invalid(self, uc, access, addr, size, value, data):
        pc = self.reg(UC_ARM_REG_PC)
        what = {19: "read", 20: "write", 21: "fetch", 22: "read", 23: "write", 24: "fetch"}.get(access, "access")
        where = "near NULL" if addr < 0x10000 else \
            "under the stack: stack overflow" if STACK_LOW - 0x100000 <= addr < STACK_LOW else \
            "after the heap" if self.heap_hi <= addr < self.heap_hi + 0x100000 else "outside the program's memory"
        self.findings.append(f"{what} of {addr:#x} ({where}) at {self.site(pc)}")
        self.stopped = True
        return False

    def _intr(self, uc, intno, data):
        pc = self.reg(UC_ARM_REG_PC)
        if intno != 2:
            kind = {1: "undefined instruction", 3: "prefetch abort", 4: "data abort", 7: "breakpoint"}.get(intno,
                                                                                                    f"exception {intno}")
            word, = struct.unpack("<I", uc.mem_read(pc, 4)) if self._mapped(pc) else (0,)
            self.stop(f"{kind} ({word:#010x}) at {self.site(pc)}")
            return
        thumb = self.reg(UC_ARM_REG_CPSR) & 0x20
        if thumb:
            n = struct.unpack("<H", uc.mem_read(pc - 2, 2))[0] & 0xFF
        else:
            n = struct.unpack("<I", uc.mem_read(pc - 4, 4))[0] & 0xFFFFFF
        self.syscalls[n] = self.syscalls.get(n, 0) + 1
        f = SYSCALLS.get(n)
        if not f:
            self.stop(f"system call {sc_name(n)} ({n:#x}), not emulated, at {self.site(pc - 4)}")
            return
        self.extra += SYSCALL_CYCLES
        try:
            ret = f(self)
        except UcError as e:
            self.stop(f"system call {sc_name(n)} at {self.site(pc - 4)} given a bad pointer ({e})")
            return
        if ret is not None:
            uc.reg_write(UC_ARM_REG_R0, ret & 0xFFFFFFFF)

    def _mapped(self, a):
        try:
            self.uc.mem_read(a, 4)
            return True
        except UcError:
            return False

    # ---- the heap (the OS's malloc: 4 mod 8, as Ndless's malloc wrapper expects at worst) ----
    def malloc(self, size):
        need = (size + 4 + 7) & ~7
        for i, (a, n) in enumerate(self.free_list):
            if n >= need:
                if n == need:
                    del self.free_list[i]
                else:
                    self.free_list[i] = (a + need, n - need)
                self.blocks[a + 4] = need
                self.block_from[a + 4] = self.reg(UC_ARM_REG_LR)
                self.uc.mem_write(a, struct.pack("<I", size))
                self.heap_used += need
                self.heap_peak = max(self.heap_peak, self.heap_used)
                return a + 4
        self.notes.append(f"malloc({size}) failed: the {(self.heap_hi - self.heap_lo) >> 20} MB heap is full")
        return 0

    def free(self, p, who="free"):
        if not p:
            return True
        need = self.blocks.pop(p, None)
        if need is None:
            self.stop(f"{who}({p:#x}) of a block malloc didn't give (or freed already) from "
                      f"{self.site(self.reg(UC_ARM_REG_LR))}")
            return False
        self.heap_used -= need
        a = p - 4
        i = bisect.bisect_left(self.free_list, (a, 0))
        self.free_list.insert(i, (a, need))
        if i + 1 < len(self.free_list) and a + need == self.free_list[i + 1][0]:
            self.free_list[i] = (a, need + self.free_list[i + 1][1])
            del self.free_list[i + 1]
        if i and self.free_list[i - 1][0] + self.free_list[i - 1][1] == a:
            self.free_list[i - 1] = (self.free_list[i - 1][0], self.free_list[i - 1][1] + self.free_list[i][1])
            del self.free_list[i]
        return True

    # ---- files ----
    def host(self, path):
        if not path.startswith("/"):
            path = "/documents/" + path
        parts = [p for p in path.split("/") if p not in ("", ".", "..")]
        return os.path.join(self.saves, *parts)

    def errno(self, e):
        self.uc.mem_write(ERRNO, struct.pack("<i", e))

    def file(self, f):
        return self.files.get(f)

    # ---- running ----
    def counters(self):
        """What the speed is measured from: cycles, of which slept, frames shown, and the game's own counts of
        frames drawn and steps (perf_frames, perf_updates: uint32_t) when it keeps them."""
        def own(name):
            a = self.prog.symbols.get(name)
            return struct.unpack("<I", self.uc.mem_read(BASE + a, 4))[0] if a is not None else None
        return dict(cycles=self.cycles(), slept=self.slept, blits=len(self.frames), frames=own("perf_frames"),
                    updates=own("perf_updates"), max=own("perf_max"), slow=own("perf_slow"))

    def run(self, until_ms, quit_ms=None, shots=(), profile=False, from_ms=0):
        """Runs to until_ms of calculator time (or the program's end); Home is held from quit_ms. The speed is
        measured from from_ms (past the menus)."""
        if quit_ms is not None:
            self.keys.append((quit_ms, float("inf"), "home"))
        shots = sorted(shots)
        per_ms = self.hz / 1000 / self.cpi
        if profile:
            self.samples = []
        self.window = None
        t_wall = time.time()
        while not self.stopped and self.ms() < until_ms:
            if self.window is None and self.ms() >= from_ms:
                self.window = self.counters()
            if quit_ms is not None and self.ms() >= quit_ms and self.window and "end" not in self.window:
                self.window["end"] = self.counters()
            if self.wall_limit and time.time() - t_wall > self.wall_limit:
                self.notes.append(f"stopped after {self.wall_limit} s of real time")
                break
            self.stop_at.value = self.insns.value + max(1, int(min(1, until_ms - self.ms()) * per_ms))
            before = self.insns.value
            try:
                self.uc.emu_start(self.pc, 0xFFFFFFFC)
            except UcError as e:
                pc = self.reg(UC_ARM_REG_PC)
                if not self.stopped:
                    if e.errno == UC_ERR_INSN_INVALID:
                        w = struct.unpack("<I", self.uc.mem_read(pc, 4))[0] if self._mapped(pc) else 0
                        self.findings.append(f"undefined instruction ({w:#010x}"
                                             f"{', VFP' if (w >> 8 & 0xE) == 0xA else ''}) at {self.site(pc)}")
                    else:
                        self.findings.append(f"CPU fault ({e}) at {self.site(pc)}")
                self.stopped = True
            self.pc = self.reg(UC_ARM_REG_PC) | (1 if self.reg(UC_ARM_REG_CPSR) & 0x20 else 0)
            if self.samples is not None:
                self.samples.append((self.pc & ~1, self.reg(UC_ARM_REG_LR)))
            if not self.stopped and self.insns.value < self.stop_at.value:  # halted: a wfi
                pc = self.pc & ~1
                w = struct.unpack("<I", self.uc.mem_read(pc - 4, 4))[0] if self._mapped(pc - 4) else 0
                if w & 0x0FFF0FFF != 0x0E070F90:
                    self.stop(f"the CPU stopped at {self.site(pc)} (not a wfi)")
                elif self.last_wfi == (pc, before) and self.insns.value == before:
                    self.stop(f"the CPU is stuck at {self.site(pc)}")
                else:
                    self.last_wfi = (pc, self.insns.value)
                    self.wfi()
            while shots and shots[0][0] <= self.ms():
                self.png(shots.pop(0)[1])
        for t, p in shots:
            if self.exited:
                self.png(p)

    def close(self):
        for f, st in list(self.files.items()):
            self.notes.append(f"{st['path']} left open")
        if self.exited and self.blocks:  # (the OS's heap keeps them till the calculator restarts)
            sites = sorted({self.site(self.block_from[p]) for p in self.blocks})
            self.findings.append(f"{len(self.blocks)} blocks ({sum(self.blocks.values())} bytes) never freed, lost "
                                 f"till the calculator restarts: from {', '.join(sites[:4])}")
        if self.own_saves:
            shutil.rmtree(self.saves, ignore_errors=True)

    def unaligned(self):
        """[(kind, pc offset, count, first address)] of the unaligned accesses seen."""
        n = ctypes.c_uint32.in_dll(self.lib, "npe_nsites").value
        sites = (Site * 1024).in_dll(self.lib, "npe_sites")
        out = []
        for s in sites[:n]:
            off = s.pc - BASE
            w = struct.unpack("<I", self.uc.mem_read(s.pc, 4))[0] if self._mapped(s.pc) else 0
            out.append((decode(w), s.kind, off, s.count, s.addr))
        return out

    def stack_used(self):
        mem = bytes(self.uc.mem_read(STACK_LOW, STACK_TOP - STACK_LOW))
        i = 0
        while i < len(mem) and mem[i] == 0xA5:
            i += 1
        return len(mem) - i

    def report(self):
        """The run's numbers (a dict). fps: the game's frames (perf_frames) or else the screens shown, a second of
        calculator time in the window; max_fps: the same if the CPU never slept; frame_minsns: instructions a frame;
        slowest: the busiest tenth of the screens shown, as a frame rate."""
        zero = dict(cycles=0, slept=0, blits=0, frames=0, updates=0)
        w0 = self.window or zero
        w1 = w0.get("end") or self.counters()
        if w1["cycles"] - w0["cycles"] < self.hz / 4:  # (it ended before the window)
            w0, w1 = zero, self.counters()
        cyc = w1["cycles"] - w0["cycles"]
        busy = cyc - (w1["slept"] - w0["slept"])
        own = w1["frames"] is not None and w0["frames"] is not None
        n = w1["frames"] - w0["frames"] if own else w1["blits"] - w0["blits"]
        per = sorted(b for b, at in self.frames if w0["cycles"] <= at <= w1["cycles"])
        p90 = per[min(len(per) - 1, len(per) * 9 // 10)] if per else None
        r = dict(game=self.gid, name=self.flags.get("name"), model=self.model, ms=round(self.ms()), insns=self.insns.value, screens=len(self.frames),
                 window_ms=round(cyc * 1000 / self.hz), own_frames=own, fps=round(n * self.hz / cyc, 1) if cyc else 0,
                 busy=round(100 * busy / cyc) if cyc else 0,
                 max_fps=round(n * self.hz / busy, 1) if busy and n else None,
                 frame_minsns=round(busy / n / self.cpi / 1e6, 2) if n else None,
                 slowest=round(self.hz / p90, 1) if p90 else None,
                 exited=self.exited, exit_code=self.exit_code, findings=list(self.findings), notes=list(self.notes),
                 stack=self.stack_used(), heap_peak=self.heap_peak,
                 syscalls={sc_name(n): c for n, c in sorted(self.syscalls.items())})
        if w1.get("updates") is not None and w0.get("updates") is not None:
            r["ups"] = round((w1["updates"] - w0["updates"]) * self.hz / cyc, 1)
        c = self.counters()
        if c["max"] is not None:
            r["perf_max_ms"], r["perf_slow"] = c["max"], c["slow"]
        if self.exited and self.lcd_type != -1:
            self.notes.append("lcd_init(SCR_TYPE_INVALID) wasn't called before the end")
            r["notes"] = list(self.notes)
        un = self.unaligned() if self.align else []
        lines = self.prog.lines([u[2] for u in un])
        r["unaligned"] = [dict(insn=i, size=k & 0xF, store=bool(k & 0x10), at=self.prog.where(off),
                               line=lines.get(off), count=c, addr=f"{a:#x}") for i, k, off, c, a in un]
        if self.samples:
            from collections import Counter
            import re
            helper = re.compile(r"__(aeabi_|ieee754|\w*(sf|df|si|di)\d?$)")  # libgcc's soft float and division
            hits, calls, at = Counter(), Counter(), {}
            for pc, lr in self.samples:
                name = self.prog.where(pc - BASE).split("+")[0]
                hits[name] += 1
                caller = self.prog.where(lr - BASE).split("+")[0]
                if helper.match(name) and not helper.match(caller):
                    calls[caller] += 1
                    at.setdefault(caller, Counter())[lr - BASE] += 1
            n = len(self.samples)
            r["profile"] = [(name, round(100 * c / n, 1)) for name, c in hits.most_common(15)]
            top = calls.most_common(8)
            lines = self.prog.lines([a for f, _ in top for a, _ in at[f].most_common(3)])
            r["helpers_from"] = [(f + " (" + ", ".join(dict.fromkeys(lines[a] for a, _ in at[f].most_common(3) if a in lines))
                                   + ")", round(100 * c / n, 1)) for f, c in top]
        return r


# ---- the system calls: f(machine) -> r0 or None ----
SYSCALLS = {}


def sc(*numbers):
    def wrap(f):
        for n in numbers:
            SYSCALLS[n] = f
        return f
    return wrap


VAR, EXT = 0x800000, 0x200000


@sc(5)
def _malloc(m):
    size, = m.args(1)
    m.extra += 300
    return m.malloc(size)


@sc(6)
def _free(m):
    p, = m.args(1)
    m.free(p)


@sc(49)
def _calloc(m):
    n, size = m.args(2)
    p = m.malloc(n * size)
    if p:
        m.uc.mem_write(p, bytes(n * size))
        m.extra += n * size / 4
    return p


@sc(50)
def _realloc(m):
    p, size = m.args(2)
    if not p:
        return m.malloc(size)
    old = m.blocks.get(p)
    if old is None:
        m.free(p, "realloc")
        return 0
    q = m.malloc(size)
    if q:
        n = min(size, struct.unpack("<I", m.uc.mem_read(p - 4, 4))[0])
        m.uc.mem_write(q, bytes(m.uc.mem_read(p, n)))
        m.free(p)
        m.extra += n / 2
    return q


@sc(7)
def _memset(m):
    p, v, n = m.args(3)
    m.uc.mem_write(p, bytes([v & 0xFF]) * n)
    m.extra += n / 4
    return p


@sc(8, 55)
def _memcpy(m):
    d, s, n = m.args(3)
    m.uc.mem_write(d, bytes(m.uc.mem_read(s, n)))
    m.extra += n / 2
    return d


@sc(9)
def _memcmp(m):
    a, b, n = m.args(3)
    x, y = bytes(m.uc.mem_read(a, n)), bytes(m.uc.mem_read(b, n))
    return 0 if x == y else (-1 if x < y else 1)


@sc(29)
def _strlen(m):
    return len(m.cstr(m.args(1)[0], 1 << 20))


@sc(22)
def _puts(m):
    print(f"[{m.gid}] {m.cstr(m.args(1)[0])}")
    return 1


@sc(10)
def _printf(m):
    fmt = m.cstr(m.args(1)[0])
    regs = m.args(4)[1:]
    sp = m.reg(UC_ARM_REG_SP)
    n = max(0, min(16, (STACK_TOP - sp) // 4)) if STACK_LOW <= sp < STACK_TOP else 16
    words = list(regs) + list(struct.unpack(f"<{n}I", m.uc.mem_read(sp, 4 * n)))
    out, i, k = [], 0, 0
    while i < len(fmt):
        c = fmt[i]
        if c != "%":
            out.append(c)
            i += 1
            continue
        j = i + 1
        while j < len(fmt) and fmt[j] in "-+ #0123456789.lhz":
            j += 1
        conv = fmt[j] if j < len(fmt) else "%"
        spec = fmt[i:j].replace("l", "").replace("h", "").replace("z", "")
        if conv == "%":
            out.append("%")
        elif conv == "s":
            out.append((spec + "s") % m.cstr(words[k]))
            k += 1
        elif conv in "fgeFGE":
            k += 1 - (k & 1)  # a double: r2-r3 or 8-aligned on the stack (words[k] is r(k+1), then the stack)
            out.append((spec + conv) % struct.unpack("<d", struct.pack("<II", words[k], words[k + 1]))[0])
            k += 2
        else:
            v = words[k]
            k += 1
            if conv in "di":
                v = v - (1 << 32) if v >> 31 else v
            out.append((spec + ("x" if conv == "p" else conv if conv in "diouxXc" else "x")) % v)
        i = j + 1
    print(f"[{m.gid}] " + "".join(out), end="" if "".join(out).endswith("\n") else "\n")
    return len("".join(out))


@sc(14)
def _interrupts(m):
    mask, = m.args(1)
    old, m.irq_mask = m.irq_mask, mask & 0xC0
    return old


@sc(61)
def _errno_addr(m):
    return ERRNO


@sc(VAR | 71, VAR | 72, VAR | 73)
def _std(m):
    n = struct.unpack("<I", m.uc.mem_read(m.reg(UC_ARM_REG_PC) - 4, 4))[0] & 0xFF
    return STD_FILES + 64 * (n - 71)


@sc(VAR | 59)
def _keypad_type(m):
    return KEYPAD_TYPE


@sc(75)
def _touchpad_read(m):
    first, last, buf = m.args(3)
    m.extra += TOUCHPAD_MS * m.hz / 1000
    if m.tpad_page == 0x10 and (first, last) == (4, 7):
        m.uc.mem_write(buf, struct.pack(">HH", TPAD_W, TPAD_H))
        return 1
    if m.tpad_page == 4 and last == 0x0A and first <= last:
        m.uc.mem_write(buf, m.touchpad_report()[first:last + 1])
        return 1
    m.stop(f"touchpad_read({first:#x}, {last:#x}) on page {m.tpad_page:#x}, not modelled")
    return 0


@sc(76)
def _touchpad_write(m):
    first, last, buf = m.args(3)
    if (first, last) == (0xFF, 0xFF):
        m.tpad_page = m.uc.mem_read(buf, 1)[0]
        return 1
    m.stop(f"touchpad_write({first:#x}, {last:#x}), not modelled")
    return 0


@sc(198, 199, EXT | 5, EXT | 7)
def _nothing(m):
    return None


@sc(13)
def _ascii2utf16(m):
    dst, src, size = m.args(3)
    s = m.cstr(src)[:max(0, size // 2 - 1)]
    m.uc.mem_write(dst, s.encode("utf-16-le") + b"\0\0")


@sc(30, 93, 94)
def _msgbox(m):
    _, title, msg = m.args(3)
    m.findings.append(f"message box: {m.u16str(title)}: {m.u16str(msg)}")
    return 1


@sc(EXT | 2)
def _hwtype(m):
    return 1


@sc(EXT | 9)
def _hwsubtype(m):
    return 2 if m.model == "cx2" else 0


@sc(EXT | 6)
def _ndless_rev(m):
    return 2023


@sc(EXT | 3, EXT | 8)
def _zero(m):
    return 0


@sc(EXT | 11)
def _osid(m):
    return 47 if m.model == "cx2" else 33


@sc(EXT | 12)
def _hassyscall(m):
    return int(m.args(1)[0] in SYSCALLS)


@sc(EXT | 13)
def _lcd_blit(m):
    kind, = m.args(1)
    if kind in (0, 3):  # SCR_320x240_565, SCR_320x240_16
        return BLIT_STUB
    m.notes.append(f"nl_lcd_blit({kind}): only RGB565 is emulated")
    return 0


@sc(EXT | 14)
def _lcd_type(m):
    return 0


@sc(EXT | 15)
def _lcd_init(m):
    kind, = m.args(1)
    m.lcd_type = kind if kind < 0x80000000 else -1
    return 1


# files
NUC_EOF = 0x10


def nuc_open(m, path, mode):
    host = m.host(path)
    st = dict(path=path, host=host, pos=0, write="r" not in mode or "+" in mode, append="a" in mode)
    if "r" in mode:
        if not os.path.isfile(host):
            m.errno(2)
            return 0
        st["data"] = bytearray(open(host, "rb").read())
    else:
        if not os.path.isdir(os.path.dirname(host)):
            m.errno(2)
            return 0
        st["data"] = bytearray(open(host, "rb").read()) if "a" in mode and os.path.isfile(host) else bytearray()
        open(host, "ab" if "a" in mode else "wb").close()
    if st["append"]:
        st["pos"] = len(st["data"])
    for i in range(64):
        f = FILES + 64 * i
        if f not in m.files:
            m.files[f] = st
            m.uc.mem_write(f, bytes(64))
            return f
    m.errno(24)
    return 0


@sc(0)
def _fopen(m):
    p, mo = m.args(2)
    m.extra += FILE_MS * m.hz / 1000
    return nuc_open(m, m.cstr(p), m.cstr(mo))


def set_eof(m, f, on):
    flags = struct.unpack("<I", m.uc.mem_read(f + 12, 4))[0]
    m.uc.mem_write(f + 12, struct.pack("<I", flags | NUC_EOF if on else flags & ~NUC_EOF))


@sc(1)
def _fread(m):
    ptr, size, n, f = m.args(4)
    st = m.file(f)
    if st is None:
        if f in m.std:
            return 0
        m.stop(f"fread from {f:#x}, not an open file, at {m.site(m.reg(UC_ARM_REG_LR))}")
        return 0
    if not size:
        return 0
    take = min(n, (len(st["data"]) - st["pos"]) // size) if st["pos"] < len(st["data"]) else 0
    m.uc.mem_write(ptr, bytes(st["data"][st["pos"]:st["pos"] + take * size]))
    st["pos"] += take * size
    set_eof(m, f, take < n)
    m.extra += take * size * m.hz / 4e6  # 4 MB/s
    return take


@sc(2)
def _fwrite(m):
    ptr, size, n, f = m.args(4)
    data = bytes(m.uc.mem_read(ptr, size * n))
    if f in m.std:
        text = m.out_line.get(f, "") + data.decode("latin1")
        *full, m.out_line[f] = text.split("\n")
        for line in full:
            print(f"[{m.gid}] {line}")
        return n
    st = m.file(f)
    if st is None or not st["write"]:
        m.stop(f"fwrite to {f:#x}, not a file open for writing, at {m.site(m.reg(UC_ARM_REG_LR))}")
        return 0
    if st["append"]:
        st["pos"] = len(st["data"])
    p = st["pos"]
    st["data"][p:p + len(data)] = data
    st["pos"] = p + len(data)
    st["dirty"] = True
    m.extra += len(data) * m.hz / 1e6  # 1 MB/s
    return n


@sc(3)
def _fclose(m):
    f, = m.args(1)
    m.extra += FILE_MS * m.hz / 1000
    st = m.files.pop(f, None)
    if st is None:
        if f in m.std:
            return 0
        m.stop(f"fclose of {f:#x}, not an open file, at {m.site(m.reg(UC_ARM_REG_LR))}")
        return -1
    if st.get("dirty"):
        with open(st["host"], "wb") as out:
            out.write(st["data"])
    return 0


@sc(69)
def _fflush(m):
    return 0


@sc(33)
def _fseek(m):
    f, off, whence = m.args(3)
    st = m.file(f)
    if st is None:
        return -1
    off = off - (1 << 32) if off >> 31 else off
    pos = {0: 0, 1: st["pos"], 2: len(st["data"])}.get(whence, 0) + off
    if pos < 0:
        m.errno(22)
        return -1
    st["pos"] = pos
    set_eof(m, f, False)
    return 0


@sc(89)
def _ftell(m):
    st = m.file(m.args(1)[0])
    return st["pos"] if st else -1


@sc(52)
def _fgetc(m):
    f, = m.args(1)
    st = m.file(f)
    if not st or st["pos"] >= len(st["data"]):
        if st:
            set_eof(m, f, True)
        return -1
    st["pos"] += 1
    return st["data"][st["pos"] - 1]


@sc(65)
def _ungetc(m):
    return m.args(1)[0]


@sc(18)
def _stat(m):
    p, buf = m.args(2)
    host = m.host(m.cstr(p))
    m.extra += FILE_MS * m.hz / 1000
    if not os.path.exists(host):
        m.errno(2)
        return -1
    d = os.path.isdir(host)
    size = 0 if d else os.path.getsize(host)
    t = int(os.path.getmtime(host))
    m.uc.mem_write(buf, struct.pack("<HxxIIHHHHIIII", 0, 0, 0x41FF if d else 0x81FF, 1, 0, 0, 0, size, t, t, t))
    return 0


@sc(15)
def _mkdir(m):
    p, _ = m.args(2)
    host = m.host(m.cstr(p))
    m.extra += FILE_MS * m.hz / 1000
    if os.path.exists(host):
        m.errno(17)
        return -1
    if not os.path.isdir(os.path.dirname(host)):
        m.errno(2)
        return -1
    os.mkdir(host)
    return 0


@sc(19, 70)
def _unlink(m):
    host = m.host(m.cstr(m.args(1)[0]))
    m.extra += FILE_MS * m.hz / 1000
    if not os.path.isfile(host):
        m.errno(2)
        return -1
    os.remove(host)
    return 0


@sc(16)
def _rmdir(m):
    host = m.host(m.cstr(m.args(1)[0]))
    try:
        os.rmdir(host)
        return 0
    except OSError:
        m.errno(2)
        return -1


@sc(20)
def _rename(m):
    a, b = (m.host(m.cstr(x)) for x in m.args(2))
    m.extra += FILE_MS * m.hz / 1000
    if not os.path.exists(a):
        m.errno(2)
        return -1
    os.replace(a, b)
    return 0


# ---- the command line ----
def parse_keys(spec):
    out = []
    for item in filter(None, spec.split(",")):
        rng, k = item.split(":")
        a, _, b = rng.partition("-")
        if k not in NAMES and k not in KEYS_TPAD:
            sys.exit(f"nspire_emu: unknown key {k}")
        out.append((int(a), int(b) if b else int(a) + 90, NAMES.get(k, k)))
    return out


def fuzz_keys(seed, until_ms, start_ms=1500):
    """Random presses (the same for a seed): mostly OK and the arrows, some EXE and digits, now and then Back."""
    import random
    rnd = random.Random(seed)
    pool = ["ok"] * 6 + ["up", "down", "left", "right"] * 3 + ["exe"] * 2 + ["1", "2", "5", "back"]
    t, out = start_ms, []
    while t < until_ms:
        hold = rnd.choice((60, 120, 250, 600))
        out.append(f"{t}-{t + hold}:{rnd.choice(pool)}")
        t += hold + rnd.randint(40, 400)
    return ",".join(out)


def parse_shots(spec, out):
    shots = []
    for item in filter(None, spec.split(",")):
        t, _, name = item.partition(":")
        shots.append((int(t), os.path.join(out, name or f"shot_{int(t):06d}.png")))
    return shots


def print_report(r):
    print(f"nspire_emu: {r['name'] or r['game']} on a {'CX II' if r['model'] == 'cx2' else 'CX'}: {r['ms']} ms, "
          f"{r['insns'] / 1e6:.0f} M instructions, {r['screens']} screens shown")
    what = "frames (its own count)" if r["own_frames"] else "screens"
    print(f"nspire_emu: over {r['window_ms']} ms: {r['fps']} {what} a second, CPU busy {r['busy']}%"
          + (f", {r['ups']} steps a second" if "ups" in r else ""))
    if r["frame_minsns"]:
        print(f"nspire_emu: {r['frame_minsns']} M instructions a frame: {r['max_fps']} a second at most"
              + (f" (the slowest tenth of screens {r['slowest']})" if r["slowest"] and not r["own_frames"] else ""))
    if "perf_max_ms" in r:
        print(f"nspire_emu: the game's slowest frame {r['perf_max_ms']} ms, {r['perf_slow']} over 33 ms")
    print(f"nspire_emu: stack {r['stack'] // 1024} KB, heap {r['heap_peak'] // 1024} KB at most, "
          + ("quit" + (f" ({r['exit_code']})" if r["exit_code"] else "") if r["exited"] else "still running"))
    for u in r["unaligned"]:
        kind = "LDRD/STRD at 4 mod 8" if u["size"] == 8 else f"unaligned {u['size']}-byte " + \
            ("store" if u["store"] else "load")
        print(f"nspire_emu: {kind}: {u['insn']} at {u['at']}" + (f" ({u['line']})" if u["line"] else "") +
              f", {u['count']} times (first at {u['addr']})")
    for f in r["findings"]:
        print("nspire_emu: FINDING: " + f)
    for n in r["notes"]:
        print("nspire_emu: note: " + n)
    for name, pct in r.get("profile", []):
        print(f"nspire_emu: {pct:5.1f}%  {name}")
    if r.get("helpers_from"):
        print("nspire_emu: soft float and division (libgcc) called from:")
        for name, pct in r["helpers_from"]:
            print(f"nspire_emu: {pct:5.1f}%  {name}")


def run_one(path, keys="", shots=(), quit_ms=None, until_ms=None, saves=None, mhz=None, cpi=CPI, align=True,
            profile=False, frames=None, wall=None, gid=None, from_ms=0, model="cx2"):
    m = Nspire(path, parse_keys(keys) if isinstance(keys, str) else keys, saves, mhz or MODELS[model], cpi, align,
               model=model, gid=gid)
    m.wall_limit = wall
    if frames:
        os.makedirs(frames, exist_ok=True)

        def grab(mm):
            open(os.path.join(frames, f"{len(mm.frames) - 1:06d}_{int(mm.ms()):06d}.raw"), "wb").write(mm.screen())
        m.on_frame = grab
    end = until_ms or ((quit_ms + 3000) if quit_ms is not None else 5000)
    try:
        m.run(end, quit_ms, shots, profile, from_ms)
    finally:
        m.close()
    return m, m.report()


def status(r):
    if r.get("error"):
        return "error"
    if r["findings"]:
        return "crash" if not r["exited"] else "finding"
    if not r["screens"]:
        return "no screen"
    if not r["exited"]:
        return "didn't quit"
    if any(u["size"] != 8 for u in r["unaligned"]):
        return "unaligned"
    if r["busy"] >= 90 and r["fps"] < 25:
        return "slow"
    return "ok"


def all_games(argv):
    ap = argparse.ArgumentParser(prog="nspire_emu.py all")
    ap.add_argument("games", nargs="*")
    ap.add_argument("--quit-ms", type=int, default=7000, help="Home from then (ms)")
    ap.add_argument("--keys", default="1500-1600:ok,3500-3600:ok")
    ap.add_argument("--from-ms", type=int, default=4000, help="the speed measured from then to Home")
    ap.add_argument("--fuzz", type=int, metavar="SEED", help="random keys (OK, arrows, EXE, a few digits and Back) "
                    "till Home instead of --keys")
    ap.add_argument("-j", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--model", choices=list(MODELS), default="cx2", help="a CX II (396 MHz) or a CX (132 MHz)")
    ap.add_argument("--no-align", action="store_true")
    ap.add_argument("--json", help="the results, as JSON")
    a = ap.parse_args(argv)
    ids = a.games or sorted(g for g in os.listdir(os.path.join(ROOT, "games"))
                            if os.path.exists(os.path.join(ROOT, "games", g, "build", "nspire", g + ".tns")))
    out = os.path.join(ROOT, "build", "ports", "nspire")
    os.makedirs(out, exist_ok=True)
    jobs = [(g, fuzz_keys(f"{a.fuzz}:{g}", a.quit_ms - 300) if a.fuzz is not None else a.keys, a.quit_ms, a.from_ms,
             not a.no_align, out, a.model) for g in ids]
    import concurrent.futures as cf
    with cf.ProcessPoolExecutor(a.j) as ex:
        results = list(ex.map(_one_game, jobs))
    print("fps: frames a second (* the game's own count, else screens shown), max: if the CPU never slept, "
          "Mi: M instructions a frame")
    print(f"{'game':16} {'status':11} {'fps':>6} {'max':>6} {'Mi':>5} {'busy':>4} {'stack':>5}  details")
    for r in results:
        un = [u for u in r.get("unaligned", []) if u["size"] != 8]
        dw = [u for u in r.get("unaligned", []) if u["size"] == 8]
        det = "; ".join(r.get("findings", []) + ([r["error"]] if r.get("error") else []))
        if un:
            det = (det + "; " if det else "") + "unaligned: " + ", ".join(
                f"{u['insn']} {u['at']}" + (f" {u['line']}" if u["line"] else "") for u in un[:4])
        if dw:
            det = (det + "; " if det else "") + f"{len(dw)} LDRD/STRD at 4 mod 8"
        fps = f"{r.get('fps', 0)}{'*' if r.get('own_frames') else ''}"
        print(f"{r['game']:16} {status(r):11} {fps:>6} {r.get('max_fps') or '-':>6} {r.get('frame_minsns') or '-':>5} "
              f"{r.get('busy', 0):>3}% {r.get('stack', 0) // 1024:>4}K  {det[:300]}")
    ok = sum(status(r) == "ok" for r in results)
    sheet(results, out, os.path.join(ROOT, "build", "ports", "nspire.png"))
    if a.json:
        json.dump(results, open(a.json, "w"), indent=1)
    print(f"{ok}/{len(results)} ok; build/ports/nspire.png")
    return 0 if ok == len(results) else 1


def _one_game(job):
    g, keys, quit_ms, from_ms, align, out, model = job
    tns = os.path.join(ROOT, "games", g, "build", "nspire", g + ".tns")
    shots = [(1200, os.path.join(out, f"{g}_a.png")), (quit_ms - 1000, os.path.join(out, f"{g}_b.png"))]
    for _, p in shots:
        if os.path.exists(p):
            os.remove(p)
    try:
        _, r = run_one(tns, keys, shots, quit_ms, quit_ms + 3000, align=align, wall=300, gid=g, from_ms=from_ms,
                       model=model)
    except Exception as e:  # a broken file: listed, the others still run
        r = dict(game=g, error=f"{type(e).__name__}: {e}", findings=[], unaligned=[])
    r["shots"] = [p for _, p in shots if os.path.exists(p)]
    return r


def sheet(results, out, path):
    from PIL import Image, ImageDraw
    w, h, cols = 160, 120, 6
    rows = (len(results) + cols - 1) // cols
    im = Image.new("RGB", (cols * (2 * w + 8), rows * (h + 14)), "white")
    d = ImageDraw.Draw(im)
    for i, r in enumerate(results):
        x, y = (i % cols) * (2 * w + 8), (i // cols) * (h + 14)
        for k, p in enumerate(r.get("shots", [])[:2]):
            im.paste(Image.open(p).resize((w, h)), (x + k * w, y + 14))
        s = status(r)
        d.text((x + 2, y + 1), f"{r['game']}: {s} {r.get('fps', 0)} fps", fill="black" if s == "ok" else "red")
    im.save(path)


def main():
    if sys.argv[1:2] == ["all"]:
        return all_games(sys.argv[2:])
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("file", help="the game's .elf (build/nspire/<id>/<id>.elf) or .tns")
    ap.add_argument("--keys", default="", help='keys held: "1500-1600:ok,3500:back" (ms; one time holds 90 ms)')
    ap.add_argument("--shots", default="", help='screens saved: "2000:a.png,6000" (ms[:file])')
    ap.add_argument("--quit-ms", type=int, help="Home held from then; the run ends 3 s later")
    ap.add_argument("--ms", type=int, help="how long to run (calculator ms)")
    ap.add_argument("--out", default=".", help="where the screens go")
    ap.add_argument("--saves", help="the calculator's files (its documents folder inside); default a fresh one")
    ap.add_argument("--model", choices=list(MODELS), default="cx2", help="a CX II or a CX")
    ap.add_argument("--mhz", type=float, help="CPU clock (default the model's: 396 a CX II, 132 a CX)")
    ap.add_argument("--cpi", type=float, default=CPI, help="cycles an instruction")
    ap.add_argument("--no-align", action="store_true", help="no unaligned-access check (faster)")
    ap.add_argument("--from-ms", type=int, default=0, help="the speed measured from then (to Home)")
    ap.add_argument("--profile", action="store_true", help="the functions the CPU was found in most")
    ap.add_argument("--frames", help="each frame as raw RGB565 here (for tools/record.py --frames)")
    ap.add_argument("--json", help="the report as JSON")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    m, r = run_one(a.file, a.keys, parse_shots(a.shots, a.out), a.quit_ms, a.ms, a.saves, a.mhz, a.cpi,
                   not a.no_align, a.profile, a.frames, from_ms=a.from_ms, model=a.model)
    m.png(os.path.join(a.out, "last.png"))
    print_report(r)
    if a.json:
        json.dump(r, open(a.json, "w"), indent=1)
    return 1 if r["findings"] else 0


if __name__ == "__main__":
    sys.exit(main())
