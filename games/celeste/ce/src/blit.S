/* The CE's drawing loops, in eZ80 assembly (src/draw.c calls them; host/blit.c is the same in C for the computer).
 * Arguments are on the stack (3 bytes each, after the return address); ix is kept, the rest may change. The
 * alternate registers are used (no interrupts run while the game does). */
	.assume	adl=1
	.section	.text

/* void blit_row(const Cell *cells, unsigned n, uint8_t *dst, unsigned rows): n tiles side by side from dst (8 pixels
 * apart, 320 a row). A cell is { u8 opaque rows, u8 drawn rows, u24 tile }: a row (bit 0 the top one) also in `rows`
 * is copied whole if opaque, else pixel by pixel (0 is see-through) if drawn, else left. 0 < n */
	.global	_blit_row
	.type	_blit_row, @function
_blit_row:
	push	ix
	ld	iy, 0
	add	iy, sp
	ld	ix, (iy + 6)		; the cells
	ld	de, (iy + 12)		; dst
	exx
	ld	a, (iy + 15)
	ld	d, a			; rows (D')
	ld	a, (iy + 9)
	ld	c, a			; cells left (C')
	exx
.Lw_cell:
	exx
	ld	a, (ix + 1)
	and	a, d
	jr	z, .Lw_empty
	ld	b, a			; drawn rows (B')
	ld	a, (ix + 0)
	and	a, d
	ld	e, a			; opaque rows (E')
	ld	h, 8			; rows left (H')
	inc	a			; (0: all 8 rows shown, all opaque)
	exx
	push	de			; (the cell's dst)
	ld	hl, (ix + 2)
	jp	z, .Lw_full
.Lw_row:
	exx
	srl	e
	jr	nc, .Lw_nf
	srl	b
	exx
	ld	bc, 8
	ldir
	ld	bc, 312
.Lw_adv:
	ex	de, hl
	add	hl, bc
	ex	de, hl
	exx
	dec	h
	exx
	jr	nz, .Lw_row
	pop	de
.Lw_next:
	ld	hl, 8
	add	hl, de
	ex	de, hl
	lea	ix, ix + 5
	exx
	dec	c
	exx
	jr	nz, .Lw_cell
	pop	ix
	ret
.Lw_empty:
	exx
	jr	.Lw_next
.Lw_nf:
	srl	b
	exx
	jr	nc, .Lw_skip
	ld	b, 8
.Lw_px:
	ld	a, (hl)
	inc	hl
	or	a, a
	jr	z, .Lw_clear
	ld	(de), a
.Lw_clear:
	inc	de
	djnz	.Lw_px
	ld	bc, 312
	jr	.Lw_adv
.Lw_skip:
	ld	bc, 8
	add	hl, bc
	ld	bc, 320
	jr	.Lw_adv
.Lw_full:				; a whole opaque tile: its rows copied one after another
	ld	bc, 8
	ldir
	ld	bc, 312
	ex	de, hl
	add	hl, bc
	ex	de, hl
	ld	bc, 8
	ldir
	ld	bc, 312
	ex	de, hl
	add	hl, bc
	ex	de, hl
	ld	bc, 8
	ldir
	ld	bc, 312
	ex	de, hl
	add	hl, bc
	ex	de, hl
	ld	bc, 8
	ldir
	ld	bc, 312
	ex	de, hl
	add	hl, bc
	ex	de, hl
	ld	bc, 8
	ldir
	ld	bc, 312
	ex	de, hl
	add	hl, bc
	ex	de, hl
	ld	bc, 8
	ldir
	ld	bc, 312
	ex	de, hl
	add	hl, bc
	ex	de, hl
	ld	bc, 8
	ldir
	ld	bc, 312
	ex	de, hl
	add	hl, bc
	ex	de, hl
	ld	bc, 8
	ldir
	pop	de
	jp	.Lw_next

/* void blit_clip(const Cell *cell, uint8_t *dst, unsigned a, unsigned b, unsigned rows): a tile's columns [a, b) at
 * dst (its column 0's place), its rows as in blit_row; 0 <= a < b <= 8 */
	.global	_blit_clip
	.type	_blit_clip, @function
_blit_clip:
	push	ix
	ld	ix, 0
	add	ix, sp
	ld	iy, (ix + 6)		; the cell
	ld	a, (iy + 1)
	and	a, (ix + 18)
	jr	z, .Lc_done
	exx
	ld	b, a			; drawn rows
	ld	a, (iy + 0)
	and	a, (ix + 18)
	ld	e, a			; opaque rows
	ld	h, 8
	exx
	ld	hl, (iy + 2)
	ld	de, (ix + 12)		; a
	add	hl, de
	ld	iy, (ix + 9)		; dst
	add	iy, de
	ld	a, (ix + 15)
	sub	a, e			; b - a
	ld	(ix + 15), a		; (the count)
.Lc_row:
	lea	de, iy + 0
	push	hl
	exx
	srl	e
	jr	nc, .Lc_nf
	srl	b
	exx
	ld	bc, 0
	ld	c, (ix + 15)
	ldir
	jr	.Lc_adv
.Lc_nf:
	srl	b
	exx
	jr	nc, .Lc_adv
	ld	b, (ix + 15)
.Lc_px:
	ld	a, (hl)
	inc	hl
	or	a, a
	jr	z, .Lc_clear
	ld	(de), a
.Lc_clear:
	inc	de
	djnz	.Lc_px
.Lc_adv:
	pop	hl
	ld	bc, 8
	add	hl, bc
	ld	bc, 320
	add	iy, bc
	exx
	dec	h
	exx
	jr	nz, .Lc_row
.Lc_done:
	pop	ix
	ret

/* void blit_copy(uint8_t *dst, const uint8_t *src, unsigned n): n > 0 */
	.global	_blit_copy
	.type	_blit_copy, @function
_blit_copy:
	pop	iy			; the return address
	pop	de
	pop	hl
	pop	bc
	push	bc
	push	hl
	push	de
	ldir
	jp	(iy)

/* void blit_rle(uint8_t *dst, const uint8_t *rows, unsigned nrows): a sprite's stored rows (src/draw.c) at dst, 320
 * a row, neither clipped nor flipped: each row u8 runs, then (u8 skip, u8 n, n pixels) each; 0 < nrows < 256 */
	.global	_blit_rle
	.type	_blit_rle, @function
_blit_rle:
	push	ix
	ld	iy, 0
	add	iy, sp
	ld	ix, (iy + 6)		; the row's start
	ld	hl, (iy + 9)		; the rows
	ld	a, (iy + 12)		; rows left
.Lr_row:
	exx
	ld	e, a
	exx
	lea	de, ix + 0
	ld	a, (hl)			; runs
	inc	hl
	or	a, a
	jr	z, .Lr_next
	ld	bc, 0
.Lr_run:
	ld	c, (hl)			; skip
	inc	hl
	ex	de, hl
	add	hl, bc
	ex	de, hl
	ld	c, (hl)			; n
	inc	hl
	ldir
	dec	a
	jr	nz, .Lr_run
.Lr_next:
	ld	bc, 320
	add	ix, bc
	exx
	ld	a, e
	exx
	dec	a
	jr	nz, .Lr_row
	pop	ix
	ret

/* void build_cells(const uint8_t *fg, const uint8_t *bg, const uint8_t *table, Cell *ofg, Cell *obg, uint8_t *vis,
 * unsigned n): n cells of a row of the grids (bytes: the room's own tile numbers) as Cells, from the room's table of
 * them (8 bytes each: the Cell, then 3 unused); vis[i]: 1 where neither tile is opaque (the background shows) */
	.global	_build_cells
	.type	_build_cells, @function
_build_cells:
	push	ix
	ld	ix, 0
	add	ix, sp
	ld	iy, (ix + 21)		; vis
.Lb_cell:
	ld	hl, (ix + 6)
	ld	a, (hl)
	inc	hl
	ld	(ix + 6), hl
	or	a, a
	sbc	hl, hl
	ld	l, a
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	de, (ix + 12)
	add	hl, de
	ld	a, (hl)
	ld	(iy + 0), a		; (the fg's opaque rows, for now)
	ld	de, (ix + 15)
	ld	bc, 5
	ldir
	ld	(ix + 15), de
	ld	hl, (ix + 9)
	ld	a, (hl)
	inc	hl
	ld	(ix + 9), hl
	or	a, a
	sbc	hl, hl
	ld	l, a
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	de, (ix + 12)
	add	hl, de
	ld	a, (hl)			; the bg's opaque rows
	ld	de, (ix + 18)
	push	de
	ld	bc, 5
	ldir
	ld	(ix + 18), de
	pop	de			; (its cell)
	ld	c, a
	ld	a, (iy + 0)
	inc	a			; 0 if the fg's opaque: the bg's hidden, not drawn
	jr	nz, .Lb_fgnot
	ld	(de), a
	inc	de
	ld	(de), a
	jr	.Lb_hidden
.Lb_fgnot:
	ld	a, c
	inc	a			; 0 if the bg's opaque
	jr	z, .Lb_hidden
	ld	a, 1
.Lb_hidden:
	ld	(iy + 0), a
	inc	iy
	ld	hl, (ix + 24)
	dec	hl
	ld	(ix + 24), hl
	add	hl, de
	or	a, a
	sbc	hl, de
	jr	nz, .Lb_cell
	pop	ix
	ret

/* void blit_rect(uint8_t *dst, const uint8_t *src, unsigned w, unsigned h, unsigned src_step): h rows of w bytes,
 * dst 320 a row, src src_step a row; 0 < w, 0 < h < 256 */
	.global	_blit_rect
	.type	_blit_rect, @function
_blit_rect:
	push	ix
	ld	ix, 0
	add	ix, sp
	ld	de, (ix + 6)
	ld	hl, (ix + 9)
	ld	a, (ix + 15)
.Lq_row:
	push	de
	push	hl
	ld	bc, (ix + 12)
	ldir
	pop	hl
	ld	bc, (ix + 18)
	add	hl, bc
	pop	de
	ex	de, hl
	ld	bc, 320
	add	hl, bc
	ex	de, hl
	dec	a
	jr	nz, .Lq_row
	pop	ix
	ret

/* void blit_rle_flip(uint8_t *dst, const uint8_t *rows, unsigned nrows): as blit_rle, mirrored: dst is where the top
 * row's first stored pixel goes (its rightmost), each run going leftwards */
	.global	_blit_rle_flip
	.type	_blit_rle_flip, @function
_blit_rle_flip:
	push	ix
	ld	iy, 0
	add	iy, sp
	ld	ix, (iy + 6)		; the row's first pixel's place
	ld	hl, (iy + 9)		; the rows
	ld	a, (iy + 12)		; rows left
.Lf_row:
	exx
	ld	e, a
	exx
	lea	de, ix + 0
	ld	a, (hl)			; runs
	inc	hl
	or	a, a
	jr	z, .Lf_next
.Lf_run:
	ex	af, af'
	ld	bc, 0
	ld	c, (hl)			; skip
	inc	hl
	ex	de, hl
	or	a, a
	sbc	hl, bc
	ex	de, hl
	ld	b, (hl)			; n
	inc	hl
.Lf_px:
	ld	a, (hl)
	inc	hl
	ld	(de), a
	dec	de
	djnz	.Lf_px
	ex	af, af'
	dec	a
	jr	nz, .Lf_run
.Lf_next:
	ld	bc, 320
	add	ix, bc
	exx
	ld	a, e
	exx
	dec	a
	jr	nz, .Lf_row
	pop	ix
	ret

/* int isqrt24(int v): the integer square root of 0 <= v < 2^23, two bits at a time from the top (shifts left only) */
	.global	_isqrt24
	.type	_isqrt24, @function
_isqrt24:
	ld	iy, 0
	add	iy, sp
	ld	iy, (iy + 3)		; v
	ld	hl, 0			; what is left
	ld	de, 0			; the root so far
	ld	b, 12
.Ls_loop:
	add	iy, iy
	adc	hl, hl
	add	iy, iy
	adc	hl, hl			; left = left * 4 + v's next two bits
	push	hl
	ex	de, hl
	add	hl, hl			; root * 2
	push	hl
	add	hl, hl
	inc	hl			; root * 4 + 1
	ex	de, hl
	or	a, a
	sbc	hl, de
	pop	de			; root * 2
	jr	c, .Ls_less
	inc	de			; root * 2 + 1, left - (root * 4 + 1)
	pop	af
	djnz	.Ls_loop
	ex	de, hl
	ret
.Ls_less:
	pop	hl
	djnz	.Ls_loop
	ex	de, hl
	ret

/* void blit_rle_clip(uint8_t *dst, const uint8_t *rows, unsigned skip, unsigned nrows, unsigned xa, unsigned xb): as
 * blit_rle, the first `skip` stored rows left out, then nrows of them drawn, each only in its columns [xa, xb) (from
 * the stored part's left; dst is where that left is on the first row drawn); 0 <= xa < xb <= 255 */
	.global	_blit_rle_clip
	.type	_blit_rle_clip, @function
_blit_rle_clip:
	push	ix
	ld	ix, 0
	add	ix, sp
	ld	hl, (ix + 9)		; the rows
	ld	a, (ix + 12)		; rows to skip
	or	a, a
	jr	z, .Lk_draw
.Lk_skiprow:
	ld	b, (hl)			; runs
	inc	hl
	inc	b
	jr	.Lk_skipnext
.Lk_skiprun:
	inc	hl
	ld	de, 0
	ld	e, (hl)
	inc	hl
	add	hl, de
.Lk_skipnext:
	djnz	.Lk_skiprun
	dec	a
	jr	nz, .Lk_skiprow
.Lk_draw:
	ld	iy, (ix + 6)		; the row's left
	ld	a, (ix + 15)		; rows to draw
	or	a, a
	jr	z, .Lk_done
.Lk_row:
	ex	af, af'			; (rows left)
	ld	c, (hl)			; runs
	inc	hl
	ld	b, 0			; at: the run's first column
	inc	c
	jr	.Lk_next
.Lk_run:
	ld	a, b
	add	a, (hl)			; + skip
	inc	hl
	ld	b, a			; at
	ld	e, (hl)			; n
	inc	hl			; (hl: its pixels)
	add	a, e
	ld	d, a			; a1 = at + n ...
	ld	a, (ix + 21)
	cp	a, d
	jr	nc, .Lk_hi
	ld	d, a			; ... or xb if less
.Lk_hi:
	ld	a, (ix + 18)		; a0 = xa ...
	cp	a, b
	jr	nc, .Lk_lo
	ld	a, b			; ... or at if more
.Lk_lo:
	cp	a, d
	jr	nc, .Lk_past		; nothing in [xa, xb)
	push	hl
	push	bc
	push	de
	ld	c, a			; a0
	ld	a, d
	sub	a, c
	push	af			; a1 - a0 pixels
	ld	a, c
	sub	a, b
	ld	de, 0
	ld	e, a
	add	hl, de			; from pixel a0
	ld	e, c
	push	hl
	lea	hl, iy + 0
	add	hl, de
	ex	de, hl			; to the row's left + a0
	pop	hl
	pop	af
	ld	bc, 0
	ld	c, a
	ldir
	pop	de
	pop	bc
	pop	hl
.Lk_past:
	ld	a, b			; past the run
	add	a, e
	ld	b, a
	push	bc
	ld	bc, 0
	ld	c, e
	add	hl, bc
	pop	bc
.Lk_next:
	dec	c
	jr	nz, .Lk_run
	ld	de, 320
	add	iy, de
	ex	af, af'
	dec	a
	jr	nz, .Lk_row
.Lk_done:
	pop	ix
	ret

/* void blit_remap(uint8_t *d, const uint8_t *tab, unsigned n): d[i] = tab[d[i]], tab 256-aligned, 0 < n < 65536 */
	.global	_blit_remap
	.type	_blit_remap, @function
_blit_remap:
	ld	iy, 0
	add	iy, sp
	ld	hl, (iy + 3)
	ld	de, (iy + 6)
	ld	bc, (iy + 9)
.Lm_px:
	ld	e, (hl)
	ld	a, (de)
	ld	(hl), a
	inc	hl
	dec	bc
	ld	a, b
	or	a, c
	jr	nz, .Lm_px
	ret
