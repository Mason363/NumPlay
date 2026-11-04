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

/* The appvars through TI-OS's own routines (every OS since 5.0 has them): var is the 9 bytes for OP1.
 * uint8_t *ce_var_find(const char *var): its size bytes (then its content), in RAM or the archive, or NULL */
	.section	.text.boot.ce_var_find, "ax", @progbits
	.global	_ce_var_find
	.type	_ce_var_find, @function
	.equ	EnoughMem, 0x02051C
	.equ	CreateVar, 0x021338
	.equ	DelVarArc, 0x021434
_ce_var_find:
	pop	de
	pop	hl
	push	hl
	push	de
	push	ix
	push	iy
	ld	iy, flags
	call	Mov9ToOP1
	call	ChkFindSym
	jr	c, .L.none
	call	ChkInRam
	ex	de, hl
	jr	z, .L.found
	ld	de, 9			; archived: past its header and name
	add	hl, de
	ld	e, (hl)
	add	hl, de
	inc	hl
.L.found:
	pop	iy
	pop	ix
	ret
.L.none:
	or	a, a
	sbc	hl, hl
	jr	.L.found

/* uint8_t *ce_var_create(const char *var, unsigned size): a new appvar in RAM, its size bytes, or NULL (no room) */
	.section	.text.boot.ce_var_create, "ax", @progbits
	.global	_ce_var_create
	.type	_ce_var_create, @function
_ce_var_create:
	ld	hl, 3
	add	hl, sp
	ld	de, (hl)		; var
	inc	hl
	inc	hl
	inc	hl
	ld	hl, (hl)		; size
	push	ix
	push	iy
	ld	iy, flags
	push	hl
	ex	de, hl
	call	Mov9ToOP1
	pop	hl
	push	hl
	ld	de, 32
	add	hl, de			; (and its symbol table entry)
	call	EnoughMem
	pop	hl
	jr	c, .L.full
	ld	a, 0x15
	call	CreateVar
	ex	de, hl
	jr	.L.made
.L.full:
	or	a, a
	sbc	hl, hl
.L.made:
	pop	iy
	pop	ix
	ret

/* void ce_var_delete(const char *var): gone, from RAM or the archive */
	.section	.text.boot.ce_var_delete, "ax", @progbits
	.global	_ce_var_delete
	.type	_ce_var_delete, @function
_ce_var_delete:
	pop	de
	pop	hl
	push	hl
	push	de
	push	ix
	push	iy
	ld	iy, flags
	call	Mov9ToOP1
	call	ChkFindSym
	call	nc, DelVarArc
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
