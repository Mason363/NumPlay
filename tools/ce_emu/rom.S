; The flash image ce_emu runs instead of TI's: a boot that sets the hardware up as TI-OS leaves it for a
; program, then the boot code's and the OS's jump tables. A slot is a jump to code here (the C library and
; runtime of the CE toolchain, linked in) or a ret that the host traps (os.c): every slot of the tables that
; holds C9 is the host's, and those it doesn't know stop the run with the routine's name.
;
; Built with the CE toolchain's binutils (Makefile), linked at 0 with its libcrt.a and libc.a (LGPL-3.0).

	.assume	adl=1

	.equ	OP1, 0xD005F8
	.equ	OP2, 0xD00603
	.equ	FPS, 0xD0258D
	.equ	flags, 0xD00080
	.equ	stackTop, 0xD1A87E
	.equ	userMem, 0xD1A881
	.equ	HLE, 0x023000		; the host's own entry points (os.c: HLE_*)

;-------------------------------------------------------------------------------
; boot code: vectors and its jump table
;-------------------------------------------------------------------------------
	.section	.boot, "ax", @progbits

	.byte	0x5B, 0xC3		; jp.lil boot (the eZ80 starts in Z80 mode)
	.d24	boot

	.org	0x38, 0xC9
	jp	int_handler
	.org	0x66, 0xC9		; NMI: the host's (a protection fault)
	.org	0x80, 0xC9

	.macro	slot addr, target
	.org	\addr, 0xC9
	jp	\target
	.endm

	slot	0x098, my_longjmp
	slot	0x09C, _memchr
	slot	0x0A0, _memcmp
	slot	0x0A4, _memcpy
	slot	0x0A8, _memmove
	slot	0x0AC, _memset
	slot	0x0B0, _bzero		; memclear
	slot	0x0B8, my_setjmp
	slot	0x0C0, _strcat
	slot	0x0C4, _strchr
	slot	0x0C8, _strcmp
	slot	0x0CC, _strcpy
	slot	0x0D0, _strcspn
	slot	0x0D4, _strlen
	slot	0x0D8, _strncat
	slot	0x0DC, _strncmp
	slot	0x0E0, _strncpy
	slot	0x0E4, _strpbrk
	slot	0x0E8, _strrchr
	slot	0x0EC, _strspn
	slot	0x0F0, _strstr
	slot	0x0F4, _strtok
	slot	0x100, __bshl
	slot	0x104, __bshru
	slot	0x12C, __frameset
	slot	0x130, __frameset0
	slot	0x134, __iand
	slot	0x138, __icmpzero
	slot	0x13C, __idivs
	slot	0x140, __idivu
	slot	0x144, __idvrmu
	slot	0x150, __imul_b
	slot	0x154, __imulu
	slot	0x158, __imuls
	slot	0x15C, my_indcall
	slot	0x160, __ineg
	slot	0x164, __inot
	slot	0x168, __ior
	slot	0x16C, __irems
	slot	0x170, __iremu
	slot	0x174, __ishl
	slot	0x178, my_ishl_b
	slot	0x17C, __ishrs
	slot	0x180, my_ishrs_b
	slot	0x184, __ishru
	slot	0x188, my_ishru_b
	slot	0x194, my_itol
	slot	0x198, __ixor
	slot	0x19C, __ladd
	slot	0x1A0, __ladd_b
	slot	0x1A4, __land
	slot	0x1A8, __lcmps
	slot	0x1AC, __lcmpu
	slot	0x1B0, __lcmpzero
	slot	0x1B4, __ldivs
	slot	0x1B8, __ldivu
	slot	0x1BC, __ldvrmu
	slot	0x1C8, __lmuls
	slot	0x1CC, __lmulu
	slot	0x1D0, __lneg
	slot	0x1D4, __lnot
	slot	0x1D8, __lor
	slot	0x1DC, __lrems
	slot	0x1E0, __lremu
	slot	0x1E4, __lshl
	slot	0x1E8, __lshrs
	slot	0x1EC, __lshru
	slot	0x1F8, __lsub
	slot	0x1FC, __lxor
	slot	0x200, __sand
	slot	0x204, __scmpzero
	slot	0x208, __sdivs
	slot	0x20C, __sdivu
	slot	0x218, __setflag
	slot	0x224, __smuls
	slot	0x228, __smulu
	slot	0x22C, __sneg
	slot	0x230, __snot
	slot	0x234, __sor
	slot	0x238, __srems
	slot	0x23C, __sremu
	slot	0x240, __sshl
	slot	0x244, my_sshl_b
	slot	0x248, __sshrs
	slot	0x24C, my_sshrs_b
	slot	0x250, __sshru
	slot	0x254, my_sshru_b
	slot	0x260, my_stoi
	slot	0x264, my_stoiu
	slot	0x268, __sxor
	slot	0x28C, __fneg
	slot	0x39C, set6mhz
	slot	0x3A0, set48mhz
	slot	0x3A4, set6mhz
	slot	0x3A8, set48mhz
	slot	0x4F0, noop		; usb_ResetTimer
	slot	0x4F4, noop		; usb_DisableTimer
	slot	0x4F8, noop		; usb_EnableTimer
	.org	0x800, 0xC9

;-------------------------------------------------------------------------------
; the boot: hardware as TI-OS sets it, then the program
;-------------------------------------------------------------------------------
boot:
	di
	ld	sp, stackTop
	im	1
	ld	a, 3			; 48 MHz
	out0	(0x01), a
	; LCD controller: TI-OS's timings, 16 bpp 5:6:5 BGR, the screen at 0xD40000
	ld	hl, lcd_regs
	ld	de, 0xE30000
	ld	bc, 28
	ldir
	call	panel_init
	ld	a, 0x7F			; the backlight
	ld	(0xF60024), a
	; keypad: kb_Reset's timings, scanning all the time as under TI-OS
	ld	hl, 0x000F03
	ld	(0xF50000), hl
	ld	hl, 0x08080F
	ld	(0xF50003), hl
	; interrupts: the ON key (latched) and the OS timer
	ld	hl, 0x000011
	ld	(0xF00004), hl
	ld	(0xF0000C), hl
	ld	(0xF00008), hl
	ld	a, 1
	ld	(0xF0002C), a		; ON latch (kb_EnableOnLatch's)
	; memory protection as on the calculator: code past the OS is unprivileged, a stack overflow faults
	ld	a, 0x0B
	out0	(0x1F), a
	ld	a, 0xFF
	out0	(0x1E), a
	out0	(0x1D), a		; privileged up to 0x0BFFFF
	ld	a, 0x7D
	out0	(0x3A), a
	ld	a, 0x98
	out0	(0x3B), a
	ld	a, 0xD1
	out0	(0x3C), a		; stack limit 0xD1987D, just under stackBot
	ld	iy, flags
	call	HLE + 0 * 4		; the host: the variables, the program copied to userMem, OP1 its name
	ld	iy, flags
	ei
	call	userMem
	di
	call	HLE + 1 * 4		; the host: the program returned (HL its code)
.L.boot.done:
	halt
	jr	.L.boot.done

lcd_regs:				; timing 0-3, upbase, lpbase, control
	.d24	0x0A0338
	.byte	0x1F
	.d24	0x02013F
	.byte	0x04
	.d24	0xEF7802
	.byte	0x00
	.d24	0x000000
	.byte	0x00
	.d24	0xD40000
	.byte	0x00
	.d24	0xD40000
	.byte	0x00
	.d24	0x00092D
	.byte	0x00

; the panel over SPI with TI-OS's settings (lcddrvce.h names them): sleep out, BGR, 18 bpp, the RGB interface
panel_init:
	ld	a, 0x0B			; master, clock phase and polarity
	ld	(0xF80000), a
	ld	hl, 0x080001		; 9-bit frames
	ld	(0xF80004), hl
	ld	hl, 0x00010D		; TX on, FIFOs cleared, chip on
	ld	(0xF80008), hl
	ld	hl, panel_cmds
.L.panel_init.next:
	ld	a, (hl)
	inc	hl
	or	a, a
	ret	z
	ld	b, a			; number of parameters + 1
	ld	c, 0			; command
.L.panel_init.frame:
	push	hl
	ld	hl, 0xF8000D
.L.panel_init.wait:
	ld	a, 0xEF
	cp	a, (hl)
	jr	c, .L.panel_init.wait
	pop	hl
	ld	a, c
	ld	(0xF80019), a		; 0: command, 1: parameter
	ld	a, (hl)
	inc	hl
	ld	(0xF80018), a
	ld	c, 1
	djnz	.L.panel_init.frame
	jr	.L.panel_init.next

panel_cmds:				; count (1 + parameters), command, parameters
	.byte	1, 0x11			; SLPOUT
	.byte	2, 0x36, 0x08		; MADCTL: BGR
	.byte	2, 0x3A, 0x66		; COLMOD: 18 bpp
	.byte	3, 0xB0, 0x11, 0xF0	; RAMCTRL: RGB interface
	.byte	4, 0xB1, 0x01, 5, 20	; RGBCTRL
	.byte	2, 0xC0, 0x22		; LCMCTRL: XMV, XBGR
	.byte	5, 0x2A, 0, 0, 0x01, 0x3F	; CASET 0-319 (rows and columns exchanged)
	.byte	5, 0x2B, 0, 0, 0, 0xEF	; RASET 0-239
	.byte	1, 0x29			; DISPON
	.byte	0

int_handler:				; acknowledge; what can't be (not latched) is masked off
	push	af
	push	bc
	push	de
	push	hl
	ld	de, 0xF00014
	ld	hl, 0xF00008
	ld	b, 3
.L.int_handler.ack:
	ld	a, (de)
	ld	(hl), a
	inc	de
	inc	hl
	djnz	.L.int_handler.ack
	ld	de, 0xF00014
	ld	hl, 0xF00004
	ld	b, 3
.L.int_handler.mask:
	ld	a, (de)
	cpl
	and	a, (hl)
	ld	(hl), a
	inc	de
	inc	hl
	djnz	.L.int_handler.mask
	pop	hl
	pop	de
	pop	bc
	pop	af
	ei
	reti

noop:
	ret

set6mhz:
	push	af
	xor	a, a
	out0	(0x01), a
	pop	af
	ret

set48mhz:
	push	af
	ld	a, 3
	out0	(0x01), a
	pop	af
	ret

;-------------------------------------------------------------------------------
; the runtime routines TI's boot code has and the toolchain's libcrt.a doesn't
;-------------------------------------------------------------------------------
my_indcall:
	jp	(iy)

my_ishl_b:				; uhl <<= a
	push	af
	and	a, 31
	jr	z, .L.my_ishl_b.end
.L.my_ishl_b.loop:
	add	hl, hl
	dec	a
	jr	nz, .L.my_ishl_b.loop
.L.my_ishl_b.end:
	pop	af
	ret

my_ishru_b:				; uhl >>= a (unsigned)
	push	bc
	ld	c, a
	call	__ishru
	pop	bc
	ret

my_ishrs_b:				; uhl >>= a (signed)
	push	bc
	ld	c, a
	call	__ishrs
	pop	bc
	ret

my_sshl_b:				; hl <<= a (16 bits)
	push	bc
	ld	c, a
	call	__sshl
	pop	bc
	ret

my_sshru_b:
	push	bc
	ld	c, a
	call	__sshru
	pop	bc
	ret

my_sshrs_b:
	push	bc
	ld	c, a
	call	__sshrs
	pop	bc
	ret

my_itol:				; e:uhl = (long)uhl
	push	af
	push	hl
	add	hl, hl			; carry = sign
	sbc	a, a
	ld	e, a
	pop	hl
	pop	af
	ret

my_stoi:				; uhl = (int)(short)hl
	push	de
	push	af
	ex	de, hl
	ld	a, d
	rla
	sbc	hl, hl			; 0 or -1: the upper byte
	ld	h, d
	ld	l, e
	pop	af
	pop	de
	ret

my_stoiu:				; uhl = (unsigned)(unsigned short)hl
	push	de
	ex	de, hl
	ld	hl, 0
	ld	h, d
	ld	l, e
	pop	de
	ret

; jmp_buf: sp after the return, ix, the return address, iy
my_setjmp:
	pop	bc			; return address
	pop	hl			; env
	push	hl
	push	bc
	ex	de, hl
	ld	hl, 3
	add	hl, sp
	ex	de, hl
	ld	(hl), de
	inc	hl
	inc	hl
	inc	hl
	ld	(hl), ix
	inc	hl
	inc	hl
	inc	hl
	ld	(hl), bc
	inc	hl
	inc	hl
	inc	hl
	ld	(hl), iy
	or	a, a
	sbc	hl, hl
	ret

my_longjmp:
	pop	bc
	pop	ix			; env
	pop	hl			; val (0 returns 1)
	ld	de, 0
	or	a, a
	sbc	hl, de
	jr	nz, .L.my_longjmp.nz
	inc	hl
.L.my_longjmp.nz:
	ex	de, hl
	ld	hl, (ix + 0)
	ld	iy, (ix + 9)
	ld	bc, (ix + 6)
	ld	ix, (ix + 3)
	ld	sp, hl
	push	bc
	ex	de, hl
	ret

;-------------------------------------------------------------------------------
; the OS jump table
;-------------------------------------------------------------------------------
	.section	.os, "ax", @progbits
	.org	0x100, 0xC9

	slot	0x020274 - 0x20000, HLTimes9
	slot	0x0202FC - 0x20000, Mov9b
	slot	0x020304 - 0x20000, Mov8b
	slot	0x020320 - 0x20000, Mov9ToOP1
	slot	0x020324 - 0x20000, Mov9OP1OP2
	slot	0x02032C - 0x20000, MovFrOP1
	slot	0x0205C4 - 0x20000, PopOP1
	slot	0x0205DC - 0x20000, PopRealO1
	slot	0x020614 - 0x20000, PushRealO1
	slot	0x020628 - 0x20000, PushOP1
	slot	0x020D8C - 0x20000, GetKey
	slot	0x0210DC - 0x20000, MemClear
	slot	0x021D38 - 0x20000, os_GetKey
	slot	0x021D3C - 0x20000, 0x02014C	; os_GetCSC
	slot	0x021D48 - 0x20000, ChkBCIs0
	slot	0x021D68 - 0x20000, SetDEUToA
	slot	0x021D9C - 0x20000, LoadDEInd_s
	.org	0x3000, 0xC9		; HLE: the host's own entry points (64 slots)
	.org	0x3100, 0xC9

HLTimes9:				; hl *= 9
	push	bc
	push	hl
	pop	bc
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, bc
	pop	bc
	ret

Mov9ToOP1:
	ld	de, OP1
Mov9b:
	ld	bc, 9
	ldir
	ret

Mov8b:
	ld	bc, 8
	ldir
	ret

Mov9OP1OP2:
	ld	hl, OP1
	ld	de, OP2
	jr	Mov9b

MovFrOP1:
	ld	hl, OP1
	jr	Mov9b

PushRealO1:				; 9 bytes onto the FPS
	ld	bc, 9
	jr	push_op1
PushOP1:				; 11
	ld	bc, 11
push_op1:
	push	af
	push	de
	push	hl
	ld	de, (FPS)
	ld	hl, OP1
	ldir
	ld	(FPS), de
	pop	hl
	pop	de
	pop	af
	ret

PopRealO1:
	ld	bc, 9
	jr	pop_op1
PopOP1:
	ld	bc, 11
pop_op1:
	push	af
	push	de
	push	hl
	ld	hl, (FPS)
	or	a, a
	sbc	hl, bc
	ld	(FPS), hl
	ld	de, OP1
	ldir
	pop	hl
	pop	de
	pop	af
	ret

MemClear:				; bc bytes at hl
	push	de
	push	hl
	or	a, a
	sbc	hl, hl
	adc	hl, bc
	pop	hl
	jr	z, .L.MemClear.end
	ld	(hl), 0
	dec	bc
	push	hl
	or	a, a
	sbc	hl, hl
	adc	hl, bc
	pop	hl
	jr	z, .L.MemClear.end
	push	hl
	pop	de
	inc	de
	ldir
.L.MemClear.end:
	pop	de
	ret

ChkBCIs0:				; z if bc is 0
	push	hl
	or	a, a
	sbc	hl, hl
	adc	hl, bc
	pop	hl
	ret

SetDEUToA:				; the upper byte of de = a
	push	hl
	push	de
	ld	hl, 2
	add	hl, sp
	ld	(hl), a
	pop	de
	pop	hl
	ret

LoadDEInd_s:				; de = (hl) on 16 bits, hl += 2
	ld	de, 0
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	inc	hl
	ret

GetKey:					; waits for a key, as TI's (its code from GetCSC's, by the host)
	call	0x02014C
	or	a, a
	jr	z, GetKey
	jp	HLE + 2 * 4

os_GetKey:				; the C library's: the key in hl
	call	GetKey
	ld	hl, 0
	ld	l, a
	ret
