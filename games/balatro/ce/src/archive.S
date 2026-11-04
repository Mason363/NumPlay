/* void ce_archive(const char *var): puts a variable in the archive if it is in RAM. var: its 9 bytes for OP1
 * (type, then the name). It stays in the program (see ce.ld): the loader calls it first. */
	.assume	adl = 1
	.section	.text.boot.ce_archive, "ax", @progbits
	.global	_ce_archive
	.type	_ce_archive, @function

	.equ	flags, 0xD00080
	.equ	Mov9ToOP1, 0x020320
	.equ	ChkFindSym, 0x02050C
	.equ	ChkInRam, 0x021F98
	.equ	Arc_Unarc, 0x021448

_ce_archive:
	pop	de
	pop	hl
	push	hl
	push	de
	push	ix
	push	iy
	ld	iy, flags
	call	Mov9ToOP1
	call	ChkFindSym
	jr	c, .L.done
	call	ChkInRam
	jr	nz, .L.done
	call	Arc_Unarc
.L.done:
	pop	iy
	pop	ix
	ret

/* void ce_on_stack(void (*f)(void), void *top): f called with the stack from top down (the game needs more than the
 * OS's 4 KB: a Blueprint copying a Blueprint... goes deep), the OS's stack back after. */
	.section	.text.boot.ce_on_stack, "ax", @progbits
	.global	_ce_on_stack
	.type	_ce_on_stack, @function
_ce_on_stack:
	pop	de
	pop	hl
	pop	bc
	push	bc
	push	hl
	push	de
	push	ix
	ld	(.L.sp), sp
	ld	(.L.fn), hl
	push	bc
	pop	hl
	ld	sp, hl
	ld	hl, (.L.fn)
	call	.L.call
	ld	sp, (.L.sp)
	pop	ix
	ret
.L.call:
	jp	(hl)

	.section	.bss.ce_on_stack, "aw", @nobits
.L.sp:
	.skip	3
.L.fn:
	.skip	3
