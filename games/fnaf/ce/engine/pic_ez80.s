; The grid's blocks into pixels on the calculator: pic.c's pic_expand in eZ80 assembly (pic.h).
;
; void pic_expand(const uint8_t *grid, uint8_t *dst, int stride, int bw, int bh, const uint8_t *map)
;
; Each byte of the grid a block of 2 x 2: its codeword's four bytes from the map's four pages (DE: D the page, E the
; codeword), into the block's two rows (IY, IX); 255 leaves the block as it is.

	.assume	adl=1

	.section	.bss._pic_ez80
	.local	ex_stride2, ex_row, ex_bw, ex_bh
ex_stride2:	.ds	3
ex_row:		.ds	3
ex_bw:		.ds	3
ex_bh:		.ds	1

	.section	.text._pic_expand
	.global	_pic_expand
	.type	_pic_expand, @function
_pic_expand:
	push	ix
	ld	iy, 0
	add	iy, sp
	ld	hl, (iy + 15)		; bw
	ld	(ex_bw), hl
	ld	a, (iy + 18)		; bh
	or	a, a
	jr	z, ex_done
	ld	(ex_bh), a
	ld	hl, (iy + 9)		; dst: the first row of blocks
	ld	(ex_row), hl
	ld	bc, (iy + 12)		; stride
	ld	de, (iy + 21)		; the map: its first page
	ld	hl, (iy + 6)		; the grid
	push	hl
	ld	hl, (iy + 12)
	add	hl, hl
	ld	(ex_stride2), hl
	pop	hl
ex_rows:
	ld	iy, (ex_row)
	lea	ix, iy + 0
	add	ix, bc			; (the second row: a stride further)
	push	bc
	ld	bc, (ex_bw)
ex_block:
	ld	a, (hl)
	inc	hl
	cp	a, 255
	jr	z, ex_keep
	ld	e, a
	ld	a, (de)
	ld	(iy + 0), a
	inc	d
	ld	a, (de)
	ld	(iy + 1), a
	inc	d
	ld	a, (de)
	ld	(ix + 0), a
	inc	d
	ld	a, (de)
	ld	(ix + 1), a
	dec	d
	dec	d
	dec	d
ex_keep:
	lea	iy, iy + 2
	lea	ix, ix + 2
	dec	bc
	ld	a, b
	or	a, c
	jr	nz, ex_block
	pop	bc
	push	hl
	ld	hl, (ex_row)
	push	de
	ld	de, (ex_stride2)
	add	hl, de
	pop	de
	ld	(ex_row), hl
	pop	hl
	ld	a, (ex_bh)
	dec	a
	ld	(ex_bh), a
	jr	nz, ex_rows
ex_done:
	pop	ix
	ret
