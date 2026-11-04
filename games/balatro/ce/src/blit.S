/* The renderer's busiest loops in eZ80 code (src/gfx_ce.c). The C library's calling convention: arguments
 * on the stack (3 bytes each), ix kept. */
	.assume	adl = 1

/* void ce_shade(uint8_t *d, const uint8_t *tab, unsigned n): d[i] = tab[d[i]], n > 0, tab 256-byte aligned */
	.section	.text.ce_shade, "ax", @progbits
	.global	_ce_shade
	.type	_ce_shade, @function
_ce_shade:
	ld	hl, 9
	add	hl, sp
	ld	bc, (hl)		; n
	dec	hl
	dec	hl
	dec	hl
	ld	de, (hl)		; tab
	dec	hl
	dec	hl
	dec	hl
	ld	hl, (hl)		; d
	ex	de, hl			; de = d, hl = tab
	ld	a, c			; (b the count's low byte, c its 256s: the first round b's)
	ld	c, b
	ld	b, a
	or	a, a
	jr	z, .L.shade
	inc	c
.L.shade:
	ld	a, (de)
	ld	l, a
	ld	a, (hl)
	ld	(de), a
	inc	de
	djnz	.L.shade
	dec	c
	jr	nz, .L.shade
	ret

/* unsigned ce_hash(unsigned h, const void *p, unsigned n): h times 33, then xor each byte; 0 < n < 256 */
	.section	.text.ce_hash, "ax", @progbits
	.global	_ce_hash
	.type	_ce_hash, @function
_ce_hash:
	ld	iy, 0
	add	iy, sp
	ld	hl, (iy + 3)		; h
	ld	bc, (iy + 6)		; p
.L.hash:
	push	hl
	pop	de
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, de
	ld	a, (bc)
	xor	a, l
	ld	l, a
	inc	bc
	dec	(iy + 9)
	jr	nz, .L.hash
	ret

/* unsigned ce_hash_words(unsigned h, const void *p, unsigned n): h times 33, plus each 3 bytes; 0 < n < 256 */
	.section	.text.ce_hash_words, "ax", @progbits
	.global	_ce_hash_words
	.type	_ce_hash_words, @function
_ce_hash_words:
	ld	iy, 0
	add	iy, sp
	ld	hl, (iy + 3)		; h
	ld	b, (iy + 9)		; n
	ld	iy, (iy + 6)		; p
.L.words:
	push	hl
	pop	de
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, de
	ld	de, (iy)
	add	hl, de
	lea	iy, iy + 3
	djnz	.L.words
	ret

/* The frame's strips' hashes (src/gfx_ce.c): void ce_hash_items(unsigned *hs, const item_t *it, unsigned n,
   const unsigned *clip_hash). An item's hash: its clip's, times 33 plus each 3 bytes of its first 18 (where, what,
   its animation's frame, its first strip), times 33 xor each byte of its text; each strip it covers then gets
   times 33 plus it (the order counts). An item: +9 its clip, +17 and +18 its strips, +19 its text, 22 bytes. */
	.section	.text.ce_hash_items, "ax", @progbits
	.global	_ce_hash_items
	.type	_ce_hash_items, @function
_ce_hash_items:
	push	ix
	ld	ix, 0
	add	ix, sp
	dec	sp			; (ix - 1) strips left
	ld	iy, (ix + 9)
.L.hitem:
	ld	hl, (ix + 12)		; items left
	ld	de, 0
	or	a, a
	sbc	hl, de
	jp	z, .L.hdone
	dec	hl
	ld	(ix + 12), hl
	ld	e, (iy + 9)		; the clip's hash
	ld	hl, (ix + 15)
	add	hl, de
	add	hl, de
	add	hl, de
	ld	hl, (hl)
	.irp	k, 0, 3, 6, 9, 12, 15
	push	hl
	pop	de
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, de
	ld	de, (iy + \k)
	add	hl, de
	.endr
	ld	a, (iy + 21)		; the text (none: its pointer's top byte 0)
	or	a, a
	jr	z, .L.hstrips
	ld	de, (iy + 19)
.L.hstr:
	ld	a, (de)
	or	a, a
	jr	z, .L.hstrips
	push	hl
	pop	bc
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, bc
	xor	a, l
	ld	l, a
	inc	de
	jr	.L.hstr
.L.hstrips:
	ex	de, hl			; de = the item's hash
	ld	a, (iy + 18)
	sub	a, (iy + 17)
	inc	a
	ld	(ix - 1), a
	ld	bc, 0
	ld	c, (iy + 17)
	ld	hl, (ix + 6)
	add	hl, bc
	add	hl, bc
	add	hl, bc			; its first strip's
.L.hstrip:
	push	hl
	ld	hl, (hl)
	push	hl
	pop	bc
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, bc
	add	hl, de
	ex	de, hl
	ex	(sp), hl
	ld	(hl), de
	inc	hl
	inc	hl
	inc	hl
	pop	de
	dec	(ix - 1)
	jr	nz, .L.hstrip
	lea	iy, iy + 22
	jp	.L.hitem
.L.hdone:
	ld	sp, ix
	pop	ix
	ret

/* unsigned ce_hash_str(unsigned h, const char *s): the same as ce_hash over a string's bytes */
	.section	.text.ce_hash_str, "ax", @progbits
	.global	_ce_hash_str
	.type	_ce_hash_str, @function
_ce_hash_str:
	ld	iy, 0
	add	iy, sp
	ld	hl, (iy + 3)		; h
	ld	bc, (iy + 6)		; s
.L.str:
	ld	a, (bc)
	or	a, a
	ret	z
	push	hl
	pop	de
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, de
	xor	a, l
	ld	l, a
	inc	bc
	jr	.L.str

/* void ce_glyph(uint8_t *d, const uint8_t *rows, unsigned n, unsigned col): n rows (1..255) of a glyph, a byte a row
   (the left column its top bit), in colour col; d its left on the first row, in the strip (rows of 320) */
	.section	.text.ce_glyph, "ax", @progbits
	.global	_ce_glyph
	.type	_ce_glyph, @function
_ce_glyph:
	ld	iy, 0
	add	iy, sp
	ld	hl, (iy + 3)		; d
	ld	de, (iy + 6)		; rows
	ld	b, (iy + 9)		; n
	ld	c, (iy + 12)		; col
.L.grow:
	ld	a, (de)
	inc	de
	push	hl
	or	a, a
	jr	z, .L.gend
.L.gbit:
	add	a, a
	jr	nc, .L.g0
	ld	(hl), c
.L.g0:
	inc	hl
	jr	nz, .L.gbit		; (bits left: add's zero flag)
.L.gend:
	pop	hl
	push	de
	ld	de, 320
	add	hl, de
	pop	de
	djnz	.L.grow
	ret

/* void ce_glyph_shade(uint8_t *d, const uint8_t *rows, unsigned n, const uint8_t *tab): the same, the pixels under it
   darkened through tab (256-byte aligned) */
	.section	.text.ce_glyph_shade, "ax", @progbits
	.global	_ce_glyph_shade
	.type	_ce_glyph_shade, @function
_ce_glyph_shade:
	ld	iy, 0
	add	iy, sp
	ld	hl, (iy + 3)		; d
	ld	c, (iy + 9)		; n
	ld	de, (iy + 12)		; tab
	ld	iy, (iy + 6)		; rows
.L.srow:
	ld	b, (iy)
	inc	iy
	push	hl
.L.sbit:
	sla	b
	jr	nc, .L.s0
	ld	e, (hl)
	ld	a, (de)
	ld	(hl), a
.L.s0:
	inc	hl
	jr	nz, .L.sbit		; (bits left: sla's zero flag)
	pop	hl
	push	bc
	ld	bc, 320
	add	hl, bc
	pop	bc
	dec	c
	jr	nz, .L.srow
	ret

/* A sprite's rows of runs (tools/mkart.py) into the strip, the sprite wholly between the clip's columns:
   void ce_rle(ce_rle_t *b), b as src/ce.h has it */
	.section	.text.ce_rle, "ax", @progbits
	.global	_ce_rle
	.type	_ce_rle, @function
_ce_rle:
	pop	de
	pop	iy
	push	iy
	push	de
	push	ix
.L.rrow:
	ld	hl, (iy + 3)		; the row's offset in the table
	ld	bc, 0
	ld	c, (hl)
	inc	hl
	ld	b, (hl)
	inc	hl
	ld	(iy + 3), hl
	ld	hl, (iy + 6)
	add	hl, bc			; its runs
	ld	bc, 0
	ld	de, (iy + 0)		; where they go
	ld	a, (iy + 10)
	ld	ixl, a			; the width left
	ld	a, (iy + 13)		; (the table's top byte: none, a copy)
	or	a, a
	jr	nz, .L.shrun
.L.run:					; (bc's top bytes are 0)
	ld	c, (hl)			; the skip
	inc	hl
	ex	de, hl
	add	hl, bc
	ex	de, hl
	ld	a, ixl
	sub	a, c
	ld	c, (hl)			; the length
	inc	hl
	sub	a, c
	ld	ixl, a
	inc	c
	dec	c
	jr	z, .L.rnext
	ldir
.L.rnext:
	ld	a, ixl
	or	a, a
	jr	nz, .L.run
.L.rdone:
	ld	hl, (iy + 0)
	ld	bc, 320
	add	hl, bc
	ld	(iy + 0), hl
	dec	(iy + 9)
	jr	nz, .L.rrow
	pop	ix
	ret
.L.shrun:				; a shadow: the pixels skipped, what is under them darkened
	ld	c, (hl)
	inc	hl
	ex	de, hl
	add	hl, bc
	ex	de, hl
	ld	a, ixl
	sub	a, c
	ld	c, (hl)
	inc	hl
	sub	a, c
	ld	ixl, a
	inc	c
	dec	c
	jr	z, .L.shnext
	add	hl, bc			; (past the pixels)
	push	hl
	ld	hl, (iy + 11)		; the table
	ld	b, c
.L.shpix:
	ld	a, (de)
	ld	l, a
	ld	a, (hl)
	ld	(de), a
	inc	de
	djnz	.L.shpix
	pop	hl
.L.shnext:
	ld	a, ixl
	or	a, a
	jr	nz, .L.shrun
	jr	.L.rdone

/* The background's 64 colours (src/bg_ce.c): void ce_bg_pal(uint8_t *out, const uint8_t *lut, unsigned phase,
   const uint8_t *keep). Level l (0..15) shows the paint value p = (l * 16 + 8 + phase) mod 512, folded at 256; its
   colour is lut's (33 RGB colours, every 8 values) between the two around it, then for each of the 4 banks times
   keep[b] / 256 (0: as it is); out gets the 1555 colours bank after bank, the low byte first. */
	.section	.text.ce_bg_pal, "ax", @progbits
	.global	_ce_bg_pal
	.type	_ce_bg_pal, @function
_ce_bg_pal:
	push	ix
	ld	ix, 0
	add	ix, sp
	ld	hl, -5			; (ix - 3..1) the level's colour, (ix - 4) the level, (ix - 5) a green
	add	hl, sp
	ld	sp, hl
	ld	(ix - 4), 0
.L.lev:
	ld	a, (ix - 4)
	add	a, a
	add	a, a
	add	a, a
	add	a, a
	add	a, 8
	ld	hl, (ix + 12)		; phase
	ld	de, 0
	ld	e, a
	add	hl, de
	ld	a, l
	bit	0, h
	jr	z, .L.fold
	cpl				; (511 - q)
.L.fold:
	ld	b, a			; p
	rrca
	rrca
	rrca
	and	a, 31
	ld	e, a
	add	a, a
	add	a, e
	ld	e, a			; (d is 0)
	ld	iy, (ix + 9)
	add	iy, de			; the lut's colour at or below p
	ld	a, b
	and	a, 7
	jr	nz, .L.mix
	ld	a, (iy + 0)
	ld	(ix - 3), a
	ld	a, (iy + 1)
	ld	(ix - 2), a
	ld	a, (iy + 2)
	ld	(ix - 1), a
	jr	.L.banks
.L.mix:					; the two colours' mean, weighed (8 - f) / 8 and f / 8, in 1/256
	rrca
	rrca
	rrca
	ld	b, a			; f * 32
	neg
	ld	c, a			; 256 - f * 32
	ld	d, (iy + 0)
	ld	e, c
	mlt	de
	ld	h, (iy + 3)
	ld	l, b
	mlt	hl
	add	hl, de
	ld	(ix - 3), h
	ld	d, (iy + 1)
	ld	e, c
	mlt	de
	ld	h, (iy + 4)
	ld	l, b
	mlt	hl
	add	hl, de
	ld	(ix - 2), h
	ld	d, (iy + 2)
	ld	e, c
	mlt	de
	ld	h, (iy + 5)
	ld	l, b
	mlt	hl
	add	hl, de
	ld	(ix - 1), h
.L.banks:
	ld	hl, (ix + 6)
	ld	de, 0
	ld	e, (ix - 4)
	add	hl, de
	add	hl, de			; out + 2 l
	ld	iy, (ix + 15)		; keep
	ld	b, 4
.L.bank:
	push	bc
	ld	c, (iy)
	inc	iy
	ld	a, (ix - 2)
	call	.L.scale
	ld	(ix - 5), a		; green
	ld	a, (ix - 3)
	call	.L.scale		; red
	rrca
	and	a, 0x7C
	ld	b, a
	ld	a, (ix - 5)
	rlca
	rlca
	and	a, 3
	or	a, b
	inc	hl
	ld	(hl), a			; 0rrrrrgg
	dec	hl
	ld	a, (ix - 1)
	call	.L.scale		; blue
	rrca
	rrca
	rrca
	and	a, 31
	ld	b, a
	ld	a, (ix - 5)
	add	a, a
	add	a, a
	and	a, 0xE0
	or	a, b
	ld	(hl), a			; gggbbbbb
	ld	de, 32
	add	hl, de
	pop	bc
	djnz	.L.bank
	inc	(ix - 4)
	ld	a, (ix - 4)
	cp	a, 16
	jp	nz, .L.lev
	ld	sp, ix
	pop	ix
	ret
.L.scale:				; a times c / 256 (c 0: as it is)
	inc	c
	dec	c
	ret	z
	ld	d, a
	ld	e, c
	mlt	de
	ld	a, d
	ret

/* void ce_scale_row(uint8_t *d, const uint8_t *row, unsigned n, unsigned x8, unsigned step, const uint8_t *tab):
   n pixels (1..255) of a sprite's row drawn at another width, pixel i from row[(x8 + i * step) / 256] (row 256-byte
   aligned, 255 where nothing is): copied (tab NULL), or what is under it darkened through tab (256-byte aligned) */
	.section	.text.ce_scale_row, "ax", @progbits
	.global	_ce_scale_row
	.type	_ce_scale_row, @function
_ce_scale_row:
	push	ix
	ld	ix, 0
	add	ix, sp
	ld	de, (ix + 6)		; d
	ld	hl, (ix + 9)		; row
	ld	b, (ix + 12)		; n
	ld	c, (ix + 15)		; x8's 256ths
	ld	a, (ix + 16)
	ld	l, a			; its whole pixels
	ld	a, (ix + 23)		; (the table's top byte: 0, a copy)
	ld	iy, (ix + 21)
	ld	ix, (ix + 18)		; the step: ixh whole pixels, ixl 256ths
	or	a, a
	jr	nz, .L.scsh
.L.sccp:
	ld	a, (hl)
	inc	a
	jr	z, .L.sc0
	dec	a
	ld	(de), a
.L.sc0:
	inc	de
	ld	a, c
	add	a, ixl
	ld	c, a
	ld	a, l
	adc	a, ixh
	ld	l, a
	djnz	.L.sccp
	pop	ix
	ret
.L.scsh:
	ld	a, (hl)
	inc	a
	jr	z, .L.ss0
	ld	a, (de)
	ld	iyl, a
	ld	a, (iy)
	ld	(de), a
.L.ss0:
	inc	de
	ld	a, c
	add	a, ixl
	ld	c, a
	ld	a, l
	adc	a, ixh
	ld	l, a
	djnz	.L.scsh
	pop	ix
	ret
