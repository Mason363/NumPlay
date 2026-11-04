; The 3D view's busiest loops again, to run from the archive (appended to NBCETEX): on the CE, code read from the
; flash's cache costs 2 or 3 cycles a byte, from RAM 4. Each at a fixed offset (rs.h: RSF_*), without an absolute
; address of its own (relative jumps only, what it needs handed in registers): they run wherever the appvar is.
	.assume	adl=1
	.section	.text

; ------------------------------------------------------------------------------------------------- 0: span_plain
; (rs.s's span sets up: de dst, hl the texture page, bc the shade row; other set: hl u, de du, bc dv; ix v;
; iy the end's low byte; ix pushed)
f_span_plain:
	ld	a, (de)
	or	a, a
	jr	nz, .Lp_skip
	exx
	ld	a, h
	exx
	ld	l, a
	ld	a, ixh
	and	a, 0xF0
	or	a, l
	ld	l, a
	ld	c, (hl)
	ld	a, (bc)
	ld	(de), a
.Lp_skip:
	inc	de
	exx
	add	hl, de
	add	ix, bc
	exx
	ld	a, e
	cp	a, (iy)
	jr	nz, f_span_plain
	pop	ix
	ret

; ------------------------------------------------------------------------------------------------- 64: span_alpha
	.org	64
f_span_alpha:
	ld	a, (de)
	or	a, a
	jr	nz, .La_skip
	exx
	ld	a, h
	exx
	ld	l, a
	ld	a, ixh
	and	a, 0xF0
	or	a, l
	ld	l, a
	ld	a, (hl)
	or	a, a
	jr	z, .La_skip
	ld	c, a
	ld	a, (bc)
	ld	(de), a
.La_skip:
	inc	de
	exx
	add	hl, de
	add	ix, bc
	exx
	ld	a, e
	cp	a, (iy)
	jr	nz, f_span_alpha
	pop	ix
	ret

; ------------------------------------------------------------------------------------------------- 128: double
; b pixels from hl doubled to de (0: c, the row's sky), hl and de after them
	.org	128
f_double:
	ld	a, (hl)
	inc	hl
	or	a, a
	jr	nz, .Ld_set
	ld	a, c
.Ld_set:
	ld	(de), a
	inc	de
	ld	(de), a
	inc	de
	ld	a, (hl)
	inc	hl
	or	a, a
	jr	nz, .Ld_set2
	ld	a, c
.Ld_set2:
	ld	(de), a
	inc	de
	ld	(de), a
	inc	de
	djnz	f_double
	ret

; ------------------------------------------------------------------------------------------------- 192: overlay
; b pixels from hl over de where not 0 (the HUD's layer), hl and de after them
	.org	192
f_overlay:
	ld	a, (hl)
	inc	hl
	or	a, a
	jr	z, .Lo_skip
	ld	(de), a
.Lo_skip:
	inc	de
	djnz	f_overlay
	ret

	.org	224
