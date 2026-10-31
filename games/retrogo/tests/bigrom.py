#!/usr/bin/env python3
"""A big Game Boy Color cartridge to try Retro-Go with, made here (no game's bytes in it): 2 MB with an MBC3,
its clock and 32 KB of battery RAM, as Pokemon Gold has, or 4 MB with an MBC5. At power up it counts the times it
started in its RAM ($A000, in RAM bank 0; a mark in each of the four banks), reads every ROM bank through the bank
controller (each bank holds its number at its start, in its middle and at its end), then shows it all:

- the background green when every bank read back right and the RAM held, red when one didn't;
- the top row, a black square for each time it started (so the battery save came back);
- the next, the clock's seconds (MBC3), a square a second, so it runs;
- from the third, a black square for each bank that read back right, a yellow one for each that didn't.

usage: bigrom.py OUT [--mb 2|4]   (2: MBC3 + timer + RAM + battery; 4: MBC5 + RAM + battery)"""
import argparse

GREEN, RED, YELLOW, WHITE, BLACK = 0x03E0, 0x001F, 0x03FF, 0x7FFF, 0x0000


class Asm:
    """just the SM83 instructions this needs, with labels for the jumps"""

    def __init__(self, at):
        self.at, self.code, self.labels, self.fix = at, bytearray(), {}, []

    def b(self, *xs):
        self.code += bytes(xs)

    def label(self, n):
        self.labels[n] = self.at + len(self.code)

    def jr(self, op, n):  # (op: 0x18 jr, 0x20 nz, 0x28 z, 0x30 nc, 0x38 c)
        self.b(op, 0)
        self.fix.append(("r", len(self.code) - 1, n))

    def jp(self, op, n):  # (op: 0xC3 jp, 0xCD call)
        self.b(op, 0, 0)
        self.fix.append(("a", len(self.code) - 2, n))

    def done(self):
        for kind, i, n in self.fix:
            t = self.labels[n]
            if kind == "r":
                d = t - (self.at + i + 1)
                assert -128 <= d < 128, n
                self.code[i] = d & 0xFF
            else:
                self.code[i], self.code[i + 1] = t & 0xFF, t >> 8
        return bytes(self.code)


def ld_a_mem(a, addr): a.b(0xFA, addr & 0xFF, addr >> 8)
def ld_mem_a(a, addr): a.b(0xEA, addr & 0xFF, addr >> 8)


def program(banks, mbc3):
    a = Asm(0x150)
    a.b(0xF3, 0x31, 0xFE, 0xFF)                  # di; ld sp,$FFFE
    a.label("vbl")
    a.b(0xF0, 0x44, 0xFE, 0x90)                  # ldh a,(LY); cp 144
    a.jr(0x38, "vbl")                            # jr c (wait for the vertical blank)
    a.b(0xAF, 0xE0, 0x40)                        # xor a; ldh (LCDC),a: the screen off
    # ---- the battery RAM: the count of starts at $A000 bank 0, "NPS" after it, and bank k's mark at $BFFF
    a.b(0x3E, 0x0A); ld_mem_a(a, 0x0000)         # RAM on
    a.b(0x0E, 0x00)                              # ld c,0: what went wrong
    a.b(0xAF); ld_mem_a(a, 0x4000)               # RAM bank 0
    ld_a_mem(a, 0xA001); a.b(0xFE, ord("N")); a.jr(0x20, "fresh")
    ld_a_mem(a, 0xA002); a.b(0xFE, ord("P")); a.jr(0x20, "fresh")
    ld_a_mem(a, 0xA003); a.b(0xFE, ord("S")); a.jr(0x28, "have")
    a.label("fresh")
    a.b(0xAF); ld_mem_a(a, 0xA000)
    for i, ch in enumerate(b"NPS"):
        a.b(0x3E, ch); ld_mem_a(a, 0xA001 + i)
    a.label("have")
    # (each RAM bank's mark is its number plus the count: what the last start left, then this one's)
    ld_a_mem(a, 0xA000); a.b(0x57)               # ld d,a: the count before this start
    a.b(0x06, 0x00)                              # ld b,0
    a.label("ramchk")
    a.b(0x78); ld_mem_a(a, 0x4000)               # ld a,b: RAM bank b
    a.b(0x7A); a.b(0xB7); a.jr(0x28, "ramset")   # a new cartridge: nothing to find yet
    ld_a_mem(a, 0xBFFF); a.b(0x90, 0xBA)         # sub b; cp d
    a.jr(0x28, "ramset")
    a.b(0x0C)                                    # inc c
    a.label("ramset")
    a.b(0x7A, 0x3C, 0x80); ld_mem_a(a, 0xBFFF)   # ld a,d; inc a; add a,b: the next count's mark
    a.b(0x04, 0x78, 0xFE, 0x04); a.jr(0x20, "ramchk")  # inc b; ld a,b; cp 4
    a.b(0xAF); ld_mem_a(a, 0x4000)
    a.b(0x7A, 0x3C); ld_mem_a(a, 0xA000)         # the count, one more
    a.b(0xE0, 0x80)                              # ldh ($80),a
    a.b(0x79, 0xE0, 0x82)                        # ld a,c; ldh ($82),a: the RAM banks wrong
    # ---- the tiles: 0 blank (colour 0), 1 black (3), 2 yellow (1); the map blank
    a.b(0x21, 0x00, 0x80, 0x06, 0x10, 0xAF)      # ld hl,$8000; ld b,16; xor a
    a.label("t0"); a.b(0x22, 0x05); a.jr(0x20, "t0")
    a.b(0x06, 0x10, 0x3E, 0xFF)                  # ld b,16; ld a,$FF
    a.label("t1"); a.b(0x22, 0x05); a.jr(0x20, "t1")
    a.b(0x06, 0x08)                              # ld b,8
    a.label("t2"); a.b(0x3E, 0xFF, 0x22, 0xAF, 0x22, 0x05); a.jr(0x20, "t2")
    a.b(0x21, 0x00, 0x98, 0x01, 0x00, 0x04)      # ld hl,$9800; ld bc,$0400
    a.label("map"); a.b(0xAF, 0x22, 0x0B, 0x78, 0xB1); a.jr(0x20, "map")  # xor a; ld (hl+),a; dec bc; ld a,b; or c
    # ---- row 1: a square for each start (20 at most)
    a.b(0xF0, 0x80, 0xFE, 0x14); a.jr(0x38, "few"); a.b(0x3E, 0x14)
    a.label("few")
    a.b(0x47, 0x21, 0x00, 0x98)                  # ld b,a; ld hl,$9800
    a.label("boots"); a.b(0x36, 0x01, 0x23, 0x05); a.jr(0x20, "boots")
    # ---- every bank, read through the bank controller: its number at $4000 (low, high), its low byte
    # complemented at $4002, and again at $6000 and $7FFF
    a.b(0x01, 0x01, 0x00)                        # ld bc,1: bank (b high, c low); e: banks wrong
    a.b(0x1E, 0x00)
    a.label("bank")
    a.b(0x79); ld_mem_a(a, 0x2000)               # ld a,c: its low byte
    if not mbc3:
        a.b(0x78); ld_mem_a(a, 0x3000)           # ld a,b: its ninth bit (MBC5)
    a.b(0x16, 0x01)                              # ld d,1: the tile for a bank right
    ld_a_mem(a, 0x4000); a.b(0xB9); a.jr(0x20, "wrong")
    ld_a_mem(a, 0x4001); a.b(0xB8); a.jr(0x20, "wrong")
    ld_a_mem(a, 0x4002); a.b(0x2F, 0xB9); a.jr(0x20, "wrong")
    ld_a_mem(a, 0x6000); a.b(0xB9); a.jr(0x20, "wrong")
    ld_a_mem(a, 0x7FFF); a.b(0xB9); a.jr(0x28, "right")
    a.label("wrong")
    a.b(0x16, 0x02, 0x1C)                        # ld d,2; inc e
    a.label("right")
    # its square: $9842 + 32 * (bank / 16) + bank % 16, banks 1 to 255 on rows 2 to 17
    a.b(0x79, 0xE6, 0xF0, 0x6F, 0x26, 0x00, 0x29)  # ld a,c; and $F0; ld l,a; ld h,0; add hl,hl
    a.b(0x79, 0xE6, 0x0F, 0x85, 0x6F)            # ld a,c; and $0F; add a,l; ld l,a
    a.b(0xD5, 0x11, 0x42, 0x98, 0x19, 0xD1)      # push de; ld de,$9882; add hl,de; pop de
    a.b(0x72)                                    # ld (hl),d
    a.b(0x0C)                                    # inc c
    a.jr(0x20, "nowrap"); a.b(0x04)              # (past 255: b, the ninth bit)
    a.label("nowrap")
    a.b(0x79, 0xFE, banks & 0xFF); a.jr(0x20, "bank")
    a.b(0x78, 0xFE, banks >> 8); a.jr(0x20, "bank")
    a.b(0xF0, 0x82, 0xB3)                        # ldh a,($82); or e: wrong RAM or a wrong bank
    a.b(0xE0, 0x81)                              # ldh ($81),a: 0 when all is right
    # ---- the colours: the background green or red, then yellow, white, black
    a.b(0x3E, 0x80, 0xE0, 0x68)                  # ld a,$80; ldh (BCPS),a
    a.b(0xF0, 0x81, 0xB7)                        # ldh a,($81); or a
    a.jr(0x20, "bad")
    a.b(0x3E, GREEN & 0xFF, 0xE0, 0x69, 0x3E, GREEN >> 8, 0xE0, 0x69)
    a.jr(0x18, "rest")
    a.label("bad")
    a.b(0x3E, RED & 0xFF, 0xE0, 0x69, 0x3E, RED >> 8, 0xE0, 0x69)
    a.label("rest")
    for c in (YELLOW, WHITE, BLACK):
        a.b(0x3E, c & 0xFF, 0xE0, 0x69, 0x3E, c >> 8, 0xE0, 0x69)
    a.b(0x3E, 0xE4, 0xE0, 0x47)                  # (BGP, on a Game Boy)
    a.b(0x3E, 0x91, 0xE0, 0x40)                  # the screen on: background, tiles at $8000, map at $9800
    # ---- then the clock (MBC3): at each vertical blank, row 2 shows its seconds, a square for each up to 19
    a.label("frame")
    a.b(0xF0, 0x44, 0xFE, 0x90); a.jr(0x20, "frame")  # wait for LY = 144
    if mbc3:
        a.b(0xAF); ld_mem_a(a, 0x6000); a.b(0x3E, 0x01); ld_mem_a(a, 0x6000)  # latch the clock
        a.b(0x3E, 0x08); ld_mem_a(a, 0x4000)     # its seconds at $A000
        ld_a_mem(a, 0xA000)
        a.label("mod"); a.b(0xFE, 0x14); a.jr(0x38, "modok"); a.b(0xD6, 0x14); a.jr(0x18, "mod")
        a.label("modok")
        a.b(0x47, 0x21, 0x20, 0x98, 0x0E, 0x14)  # ld b,a; ld hl,$9820; ld c,20
        a.label("sec")
        a.b(0x78, 0xB7, 0x3E, 0x00); a.jr(0x28, "secput"); a.b(0x3E, 0x01, 0x05)  # a = b ? 1 : 0, b--
        a.label("secput")
        a.b(0x22, 0x0D); a.jr(0x20, "sec")       # ld (hl+),a; dec c
        a.b(0xAF); ld_mem_a(a, 0x4000)
    a.label("line")
    a.b(0xF0, 0x44, 0xFE, 0x90); a.jr(0x28, "line")  # (out of this blank)
    a.jr(0x18, "frame")
    return a.done()


def rom(mb=2):
    mbc3 = mb <= 2
    banks = mb * 64
    out = bytearray(mb << 20)
    for n in range(1, banks):
        o = n * 0x4000
        # (the rest of each bank: a pattern of its own, nothing to pack away)
        out[o:o + 0x4000] = bytes((n * 37 + i * 11) & 0xFF for i in range(256)) * 64
        out[o], out[o + 1], out[o + 2] = n & 0xFF, n >> 8, ~n & 0xFF
        out[o + 0x2000] = out[o + 0x3FFF] = n & 0xFF
    out[0x100:0x104] = bytes([0x00, 0xC3, 0x50, 0x01])  # nop; jp $0150
    out[0x134:0x143] = b"NPBIGROM".ljust(15, b"\0")
    out[0x143] = 0x80                                      # (Game Boy Color, and Game Boy)
    out[0x147] = 0x10 if mbc3 else 0x1B                    # MBC3 + timer + RAM + battery; MBC5 + RAM + battery
    out[0x148] = {2: 0x06, 4: 0x07}[mb]
    out[0x149] = 0x03                                      # 32 KB of RAM
    code = program(banks, mbc3)
    out[0x150:0x150 + len(code)] = code
    out[0x14D] = (-sum(out[0x134:0x14D]) - 25) & 0xFF      # the header's checksum
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--mb", type=int, default=2, choices=(2, 4))
    a = ap.parse_args()
    open(a.out, "wb").write(rom(a.mb))


if __name__ == "__main__":
    main()
