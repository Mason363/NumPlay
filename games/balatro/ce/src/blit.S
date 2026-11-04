/* The renderer's busiest loops in eZ80 code (src/gfx_ce.c, bg_ce.c). The C library's calling convention: arguments
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
.L.shade:
	ld	a, (de)
	ld	l, a
	ld	a, (hl)
	ld	(de), a
	inc	de
	dec	bc
	ld	a, c
	or	a, b
	jr	nz, .L.shade
	ret

/* void ce_unpack(uint8_t *d, const uint8_t *s, unsigned n): n bytes of two 4-bit levels (the low first) into 2n
   palette indices from 192, n > 0 */
	.section	.text.ce_unpack, "ax", @progbits
	.global	_ce_unpack
	.type	_ce_unpack, @function
_ce_unpack:
	ld	hl, 9
	add	hl, sp
	ld	bc, (hl)		; n
	dec	hl
	dec	hl
	dec	hl
	ld	de, (hl)		; s
	dec	hl
	dec	hl
	dec	hl
	ld	hl, (hl)		; d
	ex	de, hl			; de = d, hl = s
.L.unpack:
	ld	a, (hl)
	and	a, 15
	add	a, 192
	ld	(de), a
	inc	de
	ld	a, (hl)
	rrca
	rrca
	rrca
	rrca
	and	a, 15
	add	a, 192
	ld	(de), a
	inc	de
	inc	hl
	dec	bc
	ld	a, c
	or	a, b
	jr	nz, .L.unpack
	ret
