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

; void ce_remap(uint8_t *p, int n, const uint8_t *lut): each of n bytes through the table (256-aligned)
	.section	.text._ce_remap
	.global	_ce_remap
	.type	_ce_remap, @function
_ce_remap:
	ld	iy, 0
	add	iy, sp
	ld	hl, (iy + 3)
	ld	bc, (iy + 6)
	ld	de, (iy + 9)
	ld	a, b
	or	a, c
	ret	z
1:
	ld	e, (hl)
	ld	a, (de)
	ld	(hl), a
	inc	hl
	dec	bc
	ld	a, b
	or	a, c
	jr	nz, 1b
	ret

; void ce_alpha(const uint8_t *rows, int stride, int h, uint8_t *dst, const uint8_t *ink): h rows of 2-bit alpha
; (stride bytes each, 4 pixels a byte, the first in the top bits) onto the screen (320 a row) from dst, alpha 1 to 3
; as ink[1] to ink[3], 0 left as it is
	.section	.text._ce_alpha
	.global	_ce_alpha
	.type	_ce_alpha, @function
_ce_alpha:
	push	ix
	ld	iy, 0
	add	iy, sp
	ld	a, (iy + 9)
	or	a, a
	jr	z, al_done
	ld	(al_stride), a
	ld	a, (iy + 12)
	or	a, a
	jr	z, al_done
	ld	(al_h), a
	ld	ix, (iy + 18)
	ld	a, (ix + 1)
	ld	(al_ink1), a
	ld	a, (ix + 2)
	ld	(al_ink2), a
	ld	a, (ix + 3)
	ld	(al_ink3), a
	ld	hl, (iy + 6)
	ld	de, (iy + 15)
al_row:
	push	de
	ld	a, (al_stride)
	ld	b, a
al_byte:
	ld	a, (hl)
	inc	hl
	or	a, a
	jr	z, al_skip4
	ld	c, a
	call	al_px
	call	al_px
	call	al_px
	call	al_px
	djnz	al_byte
	jr	al_next
al_skip4:
	inc	de
	inc	de
	inc	de
	inc	de
	djnz	al_byte
al_next:
	pop	de
	push	hl
	ld	hl, 320
	add	hl, de
	ex	de, hl
	pop	hl
	ld	a, (al_h)
	dec	a
	ld	(al_h), a
	jr	nz, al_row
al_done:
	pop	ix
	ret
; one pixel: C's top two bits (C shifted on), at DE (DE on)
al_px:
	sla	c
	jr	c, 2f
	sla	c
	jr	nc, 1f
	ld	a, (al_ink1)
	ld	(de), a
1:	inc	de
	ret
2:	sla	c
	ld	a, (al_ink3)
	jr	c, 3f
	ld	a, (al_ink2)
3:	ld	(de), a
	inc	de
	ret

	.section	.bss._ce_alpha
	.local	al_stride, al_h, al_ink1, al_ink2, al_ink3
al_stride:	.ds	1
al_h:		.ds	1
al_ink1:	.ds	1
al_ink2:	.ds	1
al_ink3:	.ds	1
