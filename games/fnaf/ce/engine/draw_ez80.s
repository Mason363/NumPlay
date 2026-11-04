; Drawing's inner loops on the calculator (gfx.c has them in C for a computer).

	.assume	adl=1

; void ce_double(const uint8_t *src, uint8_t *dst, int n): each of n bytes twice (n a multiple of 4)
	.section	.text._ce_double
	.global	_ce_double
	.type	_ce_double, @function
_ce_double:
	ld	iy, 0
	add	iy, sp
	ld	hl, (iy + 3)
	ld	de, (iy + 6)
	ld	a, (iy + 9)
	rrca
	rrca
	and	a, 63
	ret	z
	ld	b, a
1:
	ld	a, (hl)
	inc	hl
	ld	(de), a
	inc	de
	ld	(de), a
	inc	de
	ld	a, (hl)
	inc	hl
	ld	(de), a
	inc	de
	ld	(de), a
	inc	de
	ld	a, (hl)
	inc	hl
	ld	(de), a
	inc	de
	ld	(de), a
	inc	de
	ld	a, (hl)
	inc	hl
	ld	(de), a
	inc	de
	ld	(de), a
	inc	de
	djnz	1b
	ret

; void ce_remap(uint8_t *p, int n, const uint8_t *lut): each of n bytes (under 1024) through the table
; (256-aligned), 4 at a time
	.section	.text._ce_remap
	.global	_ce_remap
	.type	_ce_remap, @function
_ce_remap:
	ld	iy, 0
	add	iy, sp
	ld	hl, (iy + 3)
	ld	bc, (iy + 6)
	ld	de, (iy + 9)
	ld	a, c
	and	a, 3
	jr	z, 2f
	push	bc
	ld	b, a
1:
	ld	e, (hl)
	ld	a, (de)
	ld	(hl), a
	inc	hl
	djnz	1b
	pop	bc
2:
	srl	b
	rr	c
	srl	b
	rr	c
	ld	a, c
	or	a, a
	ret	z
	ld	b, a
3:
	ld	e, (hl)
	ld	a, (de)
	ld	(hl), a
	inc	hl
	ld	e, (hl)
	ld	a, (de)
	ld	(hl), a
	inc	hl
	ld	e, (hl)
	ld	a, (de)
	ld	(hl), a
	inc	hl
	ld	e, (hl)
	ld	a, (de)
	ld	(hl), a
	inc	hl
	djnz	3b
	ret

; void ce_alpha(const uint8_t *rows, int stride, int h, uint8_t *dst, const uint8_t *ink): h rows of 2-bit alpha
; (stride bytes each, 4 pixels a byte, the first in the top bits) onto the screen (320 a row) from dst, alpha 1 to 3
; as ink[1] to ink[3], 0 left as it is. IX the alpha, DE the screen, HL the inks (L the alpha), B the bytes left
; in the row, C the byte's pixels to come
	.section	.text._ce_alpha
	.global	_ce_alpha
	.type	_ce_alpha, @function
_ce_alpha:
	push	ix
	ld	iy, 0
	add	iy, sp
	ld	a, (iy + 9)
	or	a, a
	jp	z, al_done
	ld	(al_stride), a
	ld	a, (iy + 12)
	or	a, a
	jp	z, al_done
	ld	(al_h), a
	ld	ix, (iy + 18)
	ld	hl, al_ink
	ld	a, (ix + 1)
	ld	(al_ink + 1), a
	ld	a, (ix + 2)
	ld	(al_ink + 2), a
	ld	a, (ix + 3)
	ld	(al_ink + 3), a
	ld	ix, (iy + 6)
	ld	de, (iy + 15)
al_row:
	push	de
	ld	a, (al_stride)
	ld	b, a
al_byte:
	ld	a, (ix + 0)
	inc	ix
	or	a, a
	jr	z, al_skip4
	rlca
	rlca
	ld	c, a
	and	a, 3
	jr	z, 1f
	ld	l, a
	ld	a, (hl)
	ld	(de), a
1:	inc	de
	ld	a, c
	rlca
	rlca
	ld	c, a
	and	a, 3
	jr	z, 2f
	ld	l, a
	ld	a, (hl)
	ld	(de), a
2:	inc	de
	ld	a, c
	rlca
	rlca
	ld	c, a
	and	a, 3
	jr	z, 3f
	ld	l, a
	ld	a, (hl)
	ld	(de), a
3:	inc	de
	ld	a, c
	rlca
	rlca
	and	a, 3
	jr	z, 4f
	ld	l, a
	ld	a, (hl)
	ld	(de), a
4:	inc	de
	dec	b
	jp	nz, al_byte
	jr	al_next
al_skip4:
	inc	de
	inc	de
	inc	de
	inc	de
	djnz	al_byte
al_next:
	pop	de
	ex	de, hl
	ld	bc, 320
	add	hl, bc
	ex	de, hl
	ld	a, (al_h)
	dec	a
	ld	(al_h), a
	jr	nz, al_row
al_done:
	pop	ix
	ret

	.section	.bss._ce_alpha
	.local	al_stride, al_h
al_stride:	.ds	1
al_h:		.ds	1
	.section	.bss._ce_alpha_ink
	.balign	256
	.local	al_ink
al_ink:		.ds	4

; void ce_runs(uint8_t *at, const uint8_t *runs, int n, uint8_t c): n runs of c (y, x, length: bytes; y never
; going back, lengths 1 or more) from at, 320 a row
	.section	.text._ce_runs
	.global	_ce_runs
	.type	_ce_runs, @function
_ce_runs:
	push	ix
	ld	iy, 0
	add	iy, sp
	ld	de, (iy + 6)
	ld	ix, (iy + 9)
	ld	hl, (iy + 12)
	ld	(ru_n), hl
	ld	a, (iy + 15)
	ld	(ru_c), a
	xor	a, a
	ld	(ru_y), a
ru_next:
	ld	hl, (ru_n)
	ld	bc, 0
	or	a, a
	sbc	hl, bc
	jr	z, ru_done
	dec	hl
	ld	(ru_n), hl
	ld	a, (ix + 0)
	ld	hl, ru_y
ru_row:
	cp	a, (hl)
	jr	z, ru_at
	inc	(hl)
	ex	de, hl
	ld	bc, 320
	add	hl, bc
	ex	de, hl
	jr	ru_row
ru_at:
	ld	hl, 0
	ld	l, (ix + 1)
	add	hl, de
	ld	a, (ru_c)
	ld	(hl), a
	ld	bc, 0
	ld	c, (ix + 2)
	dec	c
	jr	z, ru_one
	push	de
	push	hl
	pop	de
	inc	de
	ldir
	pop	de
ru_one:
	lea	ix, ix + 3
	jr	ru_next
ru_done:
	pop	ix
	ret

	.section	.bss._ce_runs
	.local	ru_n, ru_c, ru_y
ru_n:	.ds	3
ru_c:	.ds	1
ru_y:	.ds	1
