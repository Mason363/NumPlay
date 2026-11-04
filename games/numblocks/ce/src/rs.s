; The 3D view's inner loops on the CE (rs.h; rs.c does the same in C). eZ80, ADL mode.
; (Cycles: a byte read from RAM 4, written 2; code is read too, 4 a byte.)

	.assume	adl=1

; walkers: an edge each side of a polygon, stepped a row at a time
	.equ	W_X, 0			; x in 1/256 pixels
	.equ	W_U, 3			; u, v: the texel x 65536
	.equ	W_V, 6
	.equ	W_SX, 9			; their steps a row
	.equ	W_SU, 12
	.equ	W_SV, 15
	.equ	W_ROWS, 18		; rows left on this edge
	.equ	W_IDX, 19		; the vertex it goes to
	.equ	W_DIR, 20		; 1 or n - 1
	.equ	W_SIZE, 21
	.equ	RW, 160

; ------------------------------------------------------------------------------------------------- rs_setup
; void rs_setup(const uint8_t *tex, const uint8_t *cm, uint8_t alpha)
	.section	.text._rs_setup
	.global	_rs_setup
	.type	_rs_setup, @function
_rs_setup:
	ld	iy, 0
	add	iy, sp
	ld	hl, (iy+3)
	ld	(r_tex), hl
	ld	hl, (iy+6)
	ld	(r_cm), hl
	ld	hl, span_plain
	ld	a, (iy+9)
	or	a, a
	jr	z, .Lsetup_plain
	ld	hl, span_alpha
.Lsetup_plain:
	ld	(r_loop), hl
	ret

	.section	.text._rs_span
; ------------------------------------------------------------------------------------------------- the span
; de dst, a n (1..160); r_u, r_v, r_du, r_dv (u 8.8, v 4.12)
; main set: de dst, hl the texture (h its page), bc the shade row (b its page, c the texel)
; other set: hl u, de du, bc dv; ix v; iy points at the end's low byte
span:
	push	ix
	add	a, e
	ld	(r_end), a		; the end's low byte (n <= 160)
	ld	hl, (r_u)
	push	de
	ld	de, (r_du)
	ld	bc, (r_dv)
	exx
	pop	de
	ld	ix, (r_v)
	ld	hl, (r_tex)
	ld	bc, (r_cm)
	ld	iy, r_end
	push	hl
	ld	hl, (r_loop)
	ex	(sp), hl
	ret				; to the loop

span_plain:
	ld	a, (de)
	or	a, a
	jr	nz, .Lp_skip
	exx
	ld	a, h			; u's texel column
	exx
	ld	l, a
	ld	a, ixh			; v's row, x 16
	and	a, 0xF0
	or	a, l
	ld	l, a
	ld	c, (hl)			; the texel
	ld	a, (bc)			; shaded
	ld	(de), a
.Lp_skip:
	inc	de
	exx
	add	hl, de
	add	ix, bc
	exx
	ld	a, e
	cp	a, (iy)
	jr	nz, span_plain
	pop	ix
	ret

span_alpha:
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
	or	a, a			; see-through
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
	jr	nz, span_alpha
	pop	ix
	ret

	.section	.text._rs_arith
; ------------------------------------------------------------------------------------------------- arithmetic
; hl = hl x de >> 24: hl signed, de 0..2^24 (the low partial products left out)
mulhi:
	ld	(m_a), hl
	ld	(m_r), de
	xor	a, a
	ld	(m_neg), a
	ld	a, (m_a+2)
	bit	7, a
	jr	z, .Lmh_pos
	ex	de, hl
	or	a, a
	sbc	hl, hl
	sbc	hl, de
	ld	(m_a), hl
	ld	a, 1
	ld	(m_neg), a
.Lmh_pos:
	; t = a2 r0 + a1 r1 + a0 r2
	ld	a, (m_a+2)
	ld	h, a
	ld	a, (m_r)
	ld	l, a
	mlt	hl
	ld	a, (m_a+1)
	ld	d, a
	ld	a, (m_r+1)
	ld	e, a
	mlt	de
	add	hl, de
	ld	a, (m_a)
	ld	d, a
	ld	a, (m_r+2)
	ld	e, a
	mlt	de
	add	hl, de
	ld	(m_t), hl
	ld	hl, (m_t+1)		; t >> 8 (m_t+3 is 0)
	; + a2 r1 + a1 r2
	ld	a, (m_a+2)
	ld	d, a
	ld	a, (m_r+1)
	ld	e, a
	mlt	de
	add	hl, de
	ld	a, (m_a+1)
	ld	d, a
	ld	a, (m_r+2)
	ld	e, a
	mlt	de
	add	hl, de
	; + a2 r2 << 8
	ld	a, (m_a+2)
	ld	d, a
	ld	a, (m_r+2)
	ld	e, a
	mlt	de
	xor	a, a
	ld	(m_t), a
	ld	(m_t+1), de		; (de's top byte is 0 after mlt: m_t+3 stays 0)
	ld	de, (m_t)
	add	hl, de
	jr	neg_if

; hl = hl x a >> 8: hl signed, a 0..255
mul8:
	ld	(m_f), a
	ld	(m_a), hl
	xor	a, a
	ld	(m_neg), a
	ld	a, (m_a+2)
	bit	7, a
	jr	z, .Lm8_pos
	ex	de, hl
	or	a, a
	sbc	hl, hl
	sbc	hl, de
	ld	(m_a), hl
	ld	a, 1
	ld	(m_neg), a
.Lm8_pos:
	; (s0 f >> 8) + s1 f + (s2 f << 8)
	ld	a, (m_f)
	ld	e, a
	ld	a, (m_a)
	ld	d, a
	mlt	de			; s0 f
	ld	c, d			; s0 f >> 8
	ld	a, (m_f)
	ld	e, a
	ld	a, (m_a+2)
	ld	d, a
	mlt	de			; s2 f
	xor	a, a
	ld	(m_t), a
	ld	(m_t+1), de
	ld	hl, (m_t)		; s2 f << 8
	ld	a, (m_f)
	ld	e, a
	ld	a, (m_a+1)
	ld	d, a
	mlt	de			; s1 f
	add	hl, de
	ld	de, 0
	ld	e, c
	add	hl, de
	; (fall through)
neg_if:
	ld	a, (m_neg)
	or	a, a
	ret	z
	ex	de, hl
	or	a, a
	sbc	hl, hl
	sbc	hl, de
	ret

; hl = p + a * 12
vptr:
	ld	l, a
	ld	h, 12
	mlt	hl
	ld	de, (p_base)
	add	hl, de
	ret

	.section	.text._rs_edge
; ------------------------------------------------------------------------------------------------- edges
; the walker ix's next edge that crosses a row's middle; nz: one, z: none left
edge:
	ld	a, (p_n)
	ld	b, a
.Led_try:
	push	bc
	ld	a, (ix+W_IDX)
	call	vptr
	push	hl
	pop	iy			; iy: the vertex a
	ld	a, (ix+W_IDX)
	add	a, (ix+W_DIR)
	ld	hl, p_n
	cp	a, (hl)
	jr	c, .Led_in
	sub	a, (hl)
.Led_in:
	ld	(ix+W_IDX), a
	call	vptr			; hl: the vertex b
	ld	(e_b), hl
	; r0 = (ya + 7) >> 4, r1 = (yb + 7) >> 4
	push	hl
	pop	de
	inc	de
	inc	de
	inc	de
	ex	de, hl
	ld	hl, (hl)		; yb
	ld	(e_yb), hl
	ld	de, 7
	add	hl, de
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	a, h
	ld	(e_r1), a
	ld	hl, (iy+3)		; ya
	add	hl, de
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	c, h			; r0
	ld	a, (e_r1)
	sub	a, c
	jp	z, .Led_next
	jp	c, .Led_next
	ld	(ix+W_ROWS), a
	; f = r0 * 16 + 8 - ya (0..15), x 16
	ld	a, c
	add	a, a
	add	a, a
	add	a, a
	add	a, a
	add	a, 8
	sub	a, (iy+3)
	add	a, a
	add	a, a
	add	a, a
	add	a, a
	ld	(e_f), a
	; d = yb - ya
	ld	hl, (e_yb)
	ld	de, (iy+3)
	or	a, a
	sbc	hl, de
	ld	(e_d), hl
	; the deltas, x 16
	ld	hl, (e_b)
	ld	de, (hl)		; xb
	ex	de, hl
	ld	de, (iy+0)
	or	a, a
	sbc	hl, de
	call	x16
	ld	(e_dx), hl
	ld	hl, (e_b)
	ld	de, 6
	add	hl, de
	ld	de, (hl)		; ub
	ex	de, hl
	ld	de, (iy+6)
	or	a, a
	sbc	hl, de
	call	x16
	ld	(e_du), hl
	ld	hl, (e_b)
	ld	de, 9
	add	hl, de
	ld	de, (hl)		; vb
	ex	de, hl
	ld	de, (iy+9)
	or	a, a
	sbc	hl, de
	call	x16
	ld	(e_dv), hl
	ld	hl, (e_d)
	ld	de, 16
	or	a, a
	sbc	hl, de
	jr	c, .Led_short
	; d >= 16: the steps, delta x 2^28 / d >> 24
	add	hl, hl			; (d - 16) * 3
	ld	de, (e_d)
	add	hl, de
	ld	de, -16
	add	hl, de
	ld	de, (_rs_recip)
	add	hl, de
	ld	de, (hl)
	ld	(e_r), de
	ld	hl, (e_dx)
	call	mulhi
	ld	(ix+W_SX), hl
	ld	de, (e_r)
	ld	hl, (e_du)
	call	mulhi
	ld	(ix+W_SU), hl
	ld	de, (e_r)
	ld	hl, (e_dv)
	call	mulhi
	ld	(ix+W_SV), hl
	; the first row: the vertex + step x f / 16
	ld	hl, (ix+W_SX)
	ld	a, (e_f)
	call	mul8
	ld	(e_dx), hl
	ld	hl, (ix+W_SU)
	ld	a, (e_f)
	call	mul8
	ld	(e_du), hl
	ld	hl, (ix+W_SV)
	ld	a, (e_f)
	call	mul8
	ld	(e_dv), hl
	jr	.Led_start
.Led_short:
	; d < 16 (one row): delta x f / d
	xor	a, a
	sbc	hl, hl
	ld	(ix+W_SX), hl
	ld	(ix+W_SU), hl
	ld	(ix+W_SV), hl
	ld	a, (e_d)
	add	a, a
	add	a, a
	add	a, a
	add	a, a
	ld	c, a
	ld	a, (e_f)
	rrca
	rrca
	rrca
	rrca
	add	a, c			; d * 16 + f
	ld	de, 0
	ld	e, a
	ld	hl, (_rs_ratio)
	add	hl, de
	ld	a, (hl)
	ld	(e_q), a
	ld	hl, (e_dx)
	call	mul8
	ld	(e_dx), hl
	ld	a, (e_q)
	ld	hl, (e_du)
	call	mul8
	ld	(e_du), hl
	ld	a, (e_q)
	ld	hl, (e_dv)
	call	mul8
	ld	(e_dv), hl
.Led_start:
	; x = xa * 16 + e_dx, ...
	ld	hl, (iy+0)
	call	x16
	ld	de, (e_dx)
	add	hl, de
	ld	(ix+W_X), hl
	ld	hl, (iy+6)
	call	x16
	ld	de, (e_du)
	add	hl, de
	ld	(ix+W_U), hl
	ld	hl, (iy+9)
	call	x16
	ld	de, (e_dv)
	add	hl, de
	ld	(ix+W_V), hl
	pop	bc
	or	a, 1			; nz: found
	ret
.Led_next:
	pop	bc
	dec	b
	jp	nz, .Led_try
	xor	a, a			; z: none
	ret

x16:
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ret

; de: a table of 16 steps of hl (texel x 65536 a pixel): hl x (k x 16 + 8) / 256, k 0..15
ktable:
	push	de
	push	hl
	ld	a, 8
	call	mul8
	ld	(kt_base), hl
	pop	hl
	ld	a, 16
	call	mul8
	ex	de, hl			; the step
	pop	iy
	ld	hl, (kt_base)
	ld	b, 16
.Lkt:
	ld	(iy+0), hl
	lea	iy, iy+3
	add	hl, de
	djnz	.Lkt
	ret

; ------------------------------------------------------------------------------------------------- rs_poly
; void rs_poly(const SV *p, uint8_t n)
	.section	.text._rs_poly
	.global	_rs_poly
	.type	_rs_poly, @function
_rs_poly:
	push	ix
	ld	iy, 0
	add	iy, sp
	ld	hl, (iy+6)
	ld	(p_base), hl
	xor	a, a
	ld	(r_lock), a
	ld	a, (iy+9)
	ld	(p_n), a
	; the top vertex, the lowest and highest y
	ld	b, a
	ld	c, 0			; index
	ld	ix, (p_base)
	ld	hl, (ix+3)
	ld	(e_ymin), hl
	ld	(e_ymax), hl
	xor	a, a
	ld	(e_top), a
.Lpo_scan:
	ld	hl, (ix+3)
	ld	de, (e_ymin)
	or	a, a
	sbc	hl, de
	jr	nc, .Lpo_notmin
	add	hl, de
	ld	(e_ymin), hl
	ld	a, c
	ld	(e_top), a
	jr	.Lpo_nextv
.Lpo_notmin:
	add	hl, de
	ld	de, (e_ymax)
	or	a, a
	sbc	hl, de
	jr	c, .Lpo_nextv
	add	hl, de
	ld	(e_ymax), hl
.Lpo_nextv:
	lea	ix, ix+12
	inc	c
	djnz	.Lpo_scan
	; rows y .. yend
	ld	hl, (e_ymin)
	ld	de, 7
	add	hl, de
	call	x16
	ld	a, h
	ld	(r_y), a
	ld	hl, (e_ymax)
	add	hl, de
	call	x16
	ld	a, h
	ld	(r_yend), a
	ld	hl, r_y
	sub	a, (hl)
	jp	z, .Lpo_done
	jp	c, .Lpo_done
	; the walkers: a forwards, b backwards
	ld	a, (e_top)
	ld	(wa+W_IDX), a
	ld	(wb+W_IDX), a
	ld	a, 1
	ld	(wa+W_DIR), a
	ld	a, (p_n)
	dec	a
	ld	(wb+W_DIR), a
	ld	ix, wa
	call	edge
	jp	z, .Lpo_done
	ld	ix, wb
	call	edge
	jp	z, .Lpo_done
	; which is left: the smaller x (or step)
	ld	hl, wa
	ld	(w_l), hl
	ld	hl, wb
	ld	(w_r), hl
	ld	hl, (wa+W_X)
	ld	de, (wb+W_X)
	or	a, a
	sbc	hl, de
	jr	z, .Lpo_tie
	jp	m, .Lpo_row		; (x small: no overflow)
	jr	.Lpo_swap
.Lpo_tie:
	ld	hl, (wa+W_SX)
	ld	de, (wb+W_SX)
	or	a, a
	sbc	hl, de
	jp	m, .Lpo_row
	jr	z, .Lpo_row
.Lpo_swap:
	ld	hl, wb
	ld	(w_l), hl
	ld	hl, wa
	ld	(w_r), hl
	; the row's start in rbuf
.Lpo_row:
	ld	a, (r_y)
	ld	l, a
	ld	h, RW
	mlt	hl
	ld	de, (_rbuf)
	add	hl, de
	ld	(r_row), hl
.Lpo_loop:
	; px0 = (xl + 127) >> 8 (0 if xl < 0), px1 = (xr + 127) >> 8, at most 160
	ld	ix, (w_l)
	ld	hl, (ix+W_X)
	ld	de, 127
	add	hl, de
	ld	(m_t), hl
	ld	a, (m_t+2)
	bit	7, a
	ld	a, (m_t+1)
	jr	z, .Lpo_x0
	xor	a, a
.Lpo_x0:
	ld	(r_px0), a
	ld	ix, (w_r)
	ld	hl, (ix+W_X)
	add	hl, de
	ld	(m_t), hl
	ld	a, (m_t+2)
	or	a, a
	ld	a, (m_t+1)
	jr	z, .Lpo_x1
	ld	a, RW			; (past the right: never, on screen)
.Lpo_x1:
	cp	a, RW + 1
	jr	c, .Lpo_x1ok
	ld	a, RW
.Lpo_x1ok:
	ld	hl, r_px0
	sub	a, (hl)
	jp	z, .Lpo_step
	jp	c, .Lpo_step
	ld	(r_n), a
	ld	a, (r_lock)
	or	a, a
	jp	nz, .Lpo_locked
	; w = (xr - xl) >> 4: du, dv across it (0 under a pixel)
	ld	hl, (ix+W_X)
	ld	ix, (w_l)
	ld	de, (ix+W_X)
	or	a, a
	sbc	hl, de
	call	x16
	ld	(m_t), hl
	ld	hl, (m_t+1)		; (xr - xl) >> 4 (m_t+3 is 0)
	ld	de, 16
	or	a, a
	sbc	hl, de
	jr	nc, .Lpo_wide
	or	a, a
	sbc	hl, hl
	ld	(r_du), hl
	ld	(r_dv), hl
	jp	.Lpo_grad
.Lpo_wide:
	push	hl
	ld	de, 48
	or	a, a
	sbc	hl, de			; (w >= 64: four pixels or more, the gradient kept for the rows below)
	pop	hl
	jr	c, .Lpo_nolock
	ld	a, 1
	ld	(r_lock), a
.Lpo_nolock:
	push	hl			; (w - 16) * 3
	pop	de
	add	hl, hl
	add	hl, de
	ld	de, (_rs_recip)
	add	hl, de
	ld	de, (hl)
	ld	(e_r), de
	ld	ix, (w_r)
	ld	hl, (ix+W_U)
	ld	ix, (w_l)
	ld	de, (ix+W_U)
	or	a, a
	sbc	hl, de
	ld	de, (e_r)
	call	mulhi
	ld	(r_du), hl
	ld	ix, (w_r)
	ld	hl, (ix+W_V)
	ld	ix, (w_l)
	ld	de, (ix+W_V)
	or	a, a
	sbc	hl, de
	ld	de, (e_r)
	call	mulhi
	ld	(r_dv), hl
	ld	a, (r_lock)
	or	a, a
	jr	z, .Lpo_grad
	; locked: k/16 + 1/32 of a pixel's step, k 0..15, to step from the edge to a pixel's middle
	ld	hl, (r_du)
	ld	de, r_ku
	call	ktable
	ld	hl, (r_dv)
	ld	de, r_kv
	call	ktable
	; the steps in the span's units: du >> 8, dv >> 4 (kept for the rows below)
	ld	hl, (r_du)
	ld	(m_t), hl
	ld	hl, (m_t+1)
	ld	(r_du), hl
	ld	hl, (r_dv)
	call	x16
	ld	(m_t), hl
	ld	hl, (m_t+1)
	ld	(r_dv), hl
	jr	.Lpo_locked
.Lpo_grad:
	; frac = 128 - xl (low byte): from the edge to the first pixel's middle
	ld	ix, (w_l)
	ld	a, 128
	sub	a, (ix+W_X)
	ld	(e_q), a
	ld	hl, (r_du)
	call	mul8
	ld	de, (ix+W_U)
	add	hl, de
	call	clamp20
	ld	(m_t), hl
	ld	hl, (m_t+1)
	ld	(r_u), hl		; u >> 8: 8.8
	ld	a, (e_q)
	ld	hl, (r_dv)
	call	mul8
	ld	de, (ix+W_V)
	add	hl, de
	call	clamp20
	call	x16
	ld	(m_t), hl
	ld	hl, (m_t+1)
	ld	(r_v), hl		; v >> 4: 4.12
	ld	hl, (r_du)
	ld	(m_t), hl
	ld	hl, (m_t+1)
	ld	(r_du), hl		; du >> 8
	ld	hl, (r_dv)
	call	x16
	ld	(m_t), hl
	ld	hl, (m_t+1)
	ld	(r_dv), hl		; dv >> 4
	jr	.Lpo_span
.Lpo_locked:
	; from the edge to the first pixel's middle: the table's step for (128 - xl) >> 4
	ld	ix, (w_l)
	ld	a, 128
	sub	a, (ix+W_X)
	rrca
	rrca
	rrca
	rrca
	and	a, 15
	ld	l, a
	ld	h, 3
	mlt	hl
	push	hl
	ld	de, r_ku
	add	hl, de
	ld	de, (hl)
	ld	hl, (ix+W_U)
	add	hl, de
	call	clamp20
	ld	(m_t), hl
	ld	hl, (m_t+1)
	ld	(r_u), hl
	pop	hl
	ld	de, r_kv
	add	hl, de
	ld	de, (hl)
	ld	hl, (ix+W_V)
	add	hl, de
	call	clamp20
	call	x16
	ld	(m_t), hl
	ld	hl, (m_t+1)
	ld	(r_v), hl
.Lpo_span:
	ld	a, (r_px0)
	ld	de, 0
	ld	e, a
	ld	hl, (r_row)
	add	hl, de
	ex	de, hl
	ld	a, (r_n)
	call	span
.Lpo_step:
	ld	hl, r_y
	inc	(hl)
	ld	a, (r_yend)
	cp	a, (hl)
	jr	z, .Lpo_done
	ld	hl, (r_row)
	ld	de, RW
	add	hl, de
	ld	(r_row), hl
	ld	ix, (w_l)
	call	wstep
	ld	ix, (w_r)
	call	wstep
	jp	nz, .Lpo_loop
.Lpo_done:
	pop	ix
	ret

	.section	.text._rs_wstep
; the walker ix a row on; its next edge when this one is done; nz: going on
wstep:
	ld	hl, (ix+W_X)
	ld	de, (ix+W_SX)
	add	hl, de
	ld	(ix+W_X), hl
	ld	hl, (ix+W_U)
	ld	de, (ix+W_SU)
	add	hl, de
	ld	(ix+W_U), hl
	ld	hl, (ix+W_V)
	ld	de, (ix+W_SV)
	add	hl, de
	ld	(ix+W_V), hl
	dec	(ix+W_ROWS)
	ret	nz
	call	edge
	ret	nz
	pop	hl			; (none left: the polygon is done)
	jp	.Lpo_done

; hl into 0..0xFFFFF
clamp20:
	ld	(m_t), hl
	ld	a, (m_t+2)
	bit	7, a
	jr	nz, .Lcl_zero
	cp	a, 0x10
	ret	c
	ld	hl, 0xFFFFF
	ret
.Lcl_zero:
	or	a, a
	sbc	hl, hl
	ret

; ------------------------------------------------------------------------------------------------- rs_proj
; void rs_proj(int x, int y, int z, int *out)
; z shifted up until its top bit falls out (c shifts): the next 8 bits pick t = 1371 << 12 / (256 + i);
; x and y shifted up c - 3 (the top 16 bits kept), times t, >> 16: the offset from the middle in 1/16 pixels
	.section	.text._rs_proj
	.global	_rs_proj
	.type	_rs_proj, @function
_rs_proj:
	ld	iy, 0
	add	iy, sp
	ld	hl, (iy+9)		; z
	call	pr_z
	ld	hl, (iy+3)
	call	pr_mul
	ld	de, 1280
	add	hl, de
	push	hl
	ld	hl, (iy+6)
	call	pr_mul
	ex	de, hl
	ld	hl, 960
	or	a, a
	sbc	hl, de
	ld	iy, (iy+12)
	ld	(iy+3), hl
	pop	hl
	ld	(iy+0), hl
	ret

	.section	.text._rs_prz
; m_r = t, m_f = the shift, for z (hl)
pr_z:
	ld	b, 0
.Lpr_norm:
	add	hl, hl
	jr	c, .Lpr_found
	inc	b
	jr	.Lpr_norm
.Lpr_found:
	ld	(m_t), hl
	ld	a, (m_t+2)		; the index: the 8 bits after the top one
	or	a, a
	sbc	hl, hl
	ld	l, a
	add	hl, hl
	ld	de, r_rtab
	add	hl, de
	ld	de, (hl)
	ld	(m_r), de		; t
	ld	a, b
	sub	a, 3
	ld	(m_f), a		; the shift
	ret

; hl = ((hl << m_f), its top 16 bits) x t >> 16, the sign apart
pr_mul:
	ld	a, (m_f)
	or	a, a
	jr	z, .Lpm_shifted
	ld	b, a
.Lpm_shift:
	add	hl, hl
	djnz	.Lpm_shift
.Lpm_shifted:
	ld	(m_a), hl
	xor	a, a
	ld	(m_neg), a
	ld	a, (m_a+2)
	bit	7, a
	jr	z, .Lpm_pos
	ex	de, hl
	or	a, a
	sbc	hl, hl
	sbc	hl, de
	ld	(m_a), hl
	ld	a, 1
	ld	(m_neg), a
.Lpm_pos:
	; (ah al) x (th tl) >> 16 = ah th + (ah tl + al th + (al tl >> 8)) >> 8, a = m_a+1, m_a+2
	ld	a, (m_a+1)
	ld	h, a
	ld	a, (m_r)
	ld	l, a
	mlt	hl			; al tl
	ld	a, h
	ld	de, 0
	ld	e, a
	push	de
	ld	a, (m_a+2)
	ld	h, a
	ld	a, (m_r)
	ld	l, a
	mlt	hl			; ah tl
	ld	a, (m_a+1)
	ld	d, a
	ld	a, (m_r+1)
	ld	e, a
	mlt	de			; al th
	add	hl, de
	pop	de
	add	hl, de
	ld	(m_t), hl
	ld	hl, (m_t+1)		; >> 8
	ld	a, (m_a+2)
	ld	d, a
	ld	a, (m_r+1)
	ld	e, a
	mlt	de			; ah th
	add	hl, de
	jp	neg_if

; ------------------------------------------------------------------------------------------------- rs_hidden
; uint8_t rs_hidden(int x, int y, int z, int r)
; 1 when the pixels around (x, y, z)'s projection, as far as r's projection and a pixel more each way, are all
; drawn already (or off the screen)
	.section	.text._rs_hidden
	.global	_rs_hidden
	.type	_rs_hidden, @function
_rs_hidden:
	ld	iy, 0
	add	iy, sp
	ld	hl, (iy+9)
	call	pr_z
	ld	hl, (iy+12)
	call	pr_mul
	ld	de, 16
	add	hl, de
	ld	(h_r), hl
	ld	hl, (iy+6)
	call	pr_mul
	ex	de, hl
	ld	hl, 960
	or	a, a
	sbc	hl, de
	ld	c, 119
	call	h_range
	jr	c, .Lh_yes
	ld	a, b
	ld	(h_y0), a
	ld	a, c
	ld	(h_rows), a
	ld	hl, (iy+3)
	call	pr_mul
	ld	de, 1280
	add	hl, de
	ld	c, RW - 1
	call	h_range
	jr	c, .Lh_yes
	ld	a, c
	ld	(h_w), a
	ld	a, (h_y0)
	ld	h, a
	ld	l, RW
	mlt	hl
	ld	de, (_rbuf)
	add	hl, de
	ld	de, 0
	ld	e, b
	add	hl, de
.Lh_row:
	push	hl
	ld	bc, 0
	ld	a, (h_w)
	ld	c, a
	xor	a, a
	cpir
	pop	hl
	jr	z, .Lh_no
	ld	de, RW
	add	hl, de
	ld	a, (h_rows)
	dec	a
	ld	(h_rows), a
	jr	nz, .Lh_row
.Lh_yes:
	ld	a, 1
	ret
.Lh_no:
	xor	a, a
	ret

; hl the middle (1/16 pixels), c the last pixel: b the first pixel of (hl - h_r, hl + h_r), c how many; carry: none
h_range:
	ld	(h_c), hl
	ld	de, (h_r)
	or	a, a
	sbc	hl, de
	call	h_px
	jr	nc, .Lhr_lo
	xor	a, a			; (from 0)
.Lhr_lo:
	cp	a, c
	jr	z, .Lhr_lok
	jr	nc, .Lhr_off		; (past the last)
.Lhr_lok:
	ld	b, a
	ld	hl, (h_c)
	ld	de, (h_r)
	add	hl, de
	call	h_px
	ret	c			; (before the first)
	cp	a, c
	jr	c, .Lhr_hok
	ld	a, c
.Lhr_hok:
	sub	a, b
	inc	a
	ld	c, a
	or	a, a
	ret
.Lhr_off:
	scf
	ret

; a = hl >> 4 (hl in 1/16 pixels, |hl| < 2^19), 255 past 255; carry: hl < 0
h_px:
	add	hl, hl
	ret	c
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	(m_t), hl
	ld	a, (m_t+2)
	or	a, a
	ld	a, (m_t+1)
	ret	z
	ld	a, 255
	or	a, a
	ret

; ------------------------------------------------------------------------------------------------- rs_expand
; void rs_expand(const uint8_t *src, uint8_t *dst, const uint8_t *sky, const uint8_t *hud)
; 120 rows of 160 to 240 rows of 320; 0 is the row's sky colour; from screen row 180 on, the HUD's layer
; (187 wide at x 67, 0 see-through) over both rows, the rows made in a buffer first (nothing flickers)
	.section	.text._rs_expand
	.global	_rs_expand
	.type	_rs_expand, @function
	.equ	HUD_X, 67
	.equ	HUD_W, 187
_rs_expand:
	push	ix
	ld	iy, 0
	add	iy, sp
	ld	hl, (iy+15)
	ld	(r_hud), hl
	ld	a, 31			; (rows left under 31: under the HUD; none without one)
	add	hl, de
	or	a, a
	sbc	hl, de
	jr	nz, .Lex_withhud
	xor	a, a
.Lex_withhud:
	ld	(r_hudlim), a
	ld	hl, (iy+6)		; src
	ld	de, (iy+9)		; dst
	ld	ix, (iy+12)		; sky
	ld	a, 120
	ld	(r_rows), a
	ld	iy, r_hudlim
.Lex_row:
	ld	a, (r_rows)
	cp	a, (iy)
	jr	c, .Lex_hudrow
	ld	c, (ix+0)
	ld	b, RW
.Lex_px:
	ld	a, (hl)
	inc	hl
	or	a, a
	jr	nz, .Lex_set
	ld	a, c
.Lex_set:
	ld	(de), a
	inc	de
	ld	(de), a
	inc	de
	djnz	.Lex_px
	push	hl
	ld	hl, -320
	add	hl, de
	ld	bc, 320
	ldir
	pop	hl
.Lex_next:
	inc	ix
	ld	a, (r_rows)
	dec	a
	ld	(r_rows), a
	jr	nz, .Lex_row
	pop	ix
	ret

.Lex_hudrow:
	push	de
	ld	de, r_line
	ld	c, (ix+0)
	ld	b, RW
.Lex_hpx:
	ld	a, (hl)
	inc	hl
	or	a, a
	jr	nz, .Lex_hset
	ld	a, c
.Lex_hset:
	ld	(de), a
	inc	de
	ld	(de), a
	inc	de
	djnz	.Lex_hpx
	push	hl
	ld	hl, r_line
	ld	de, r_line2
	ld	bc, 320
	ldir
	ld	de, r_line + HUD_X
	ld	hl, (r_hud)
	call	r_overlay
	ld	de, r_line2 + HUD_X
	call	r_overlay
	ld	(r_hud), hl
	pop	hl
	pop	de
	push	hl
	ld	hl, r_line
	ld	bc, 640
	ldir
	pop	hl
	jr	.Lex_next

; HUD_W pixels of the layer (hl) over de, 0 see-through; hl after them
r_overlay:
	ld	b, HUD_W
.Lov_px:
	ld	a, (hl)
	inc	hl
	or	a, a
	jr	z, .Lov_skip
	ld	(de), a
.Lov_skip:
	inc	de
	djnz	.Lov_px
	ret


; ------------------------------------------------------------------------------------------------- rs_cells
; uint8_t rs_cells(const uint8_t *mask, uint8_t slot, uint8_t up, uint8_t down, int8_t *out)
; a column's cells with a face (mask: 32 bits, bit y & 31): from slot up (0, 1, .. up - 1 levels above), then
; down (-1, -2, .. -down): the offsets of those set, in that order; how many
	.section	.text._rs_cells
	.global	_rs_cells
	.type	_rs_cells, @function
_rs_cells:
	push	ix
	ld	ix, 0
	add	ix, sp
	ld	iy, (ix+18)		; out
	ld	hl, (ix+6)
	ld	de, cl_mask
	ld	bc, 4
	ldir				; (the mask, twice over: no wrapping to mind)
	ld	hl, (ix+6)
	ld	bc, 4
	ldir
	ld	c, 0			; how many
	; up: bit slot onwards
	ld	a, (ix+12)
	or	a, a
	jr	z, .Lcl_down
	ld	b, a			; levels
	ld	a, (ix+9)
	call	cl_at			; hl: the byte, e: the bit
	ld	d, 0			; offset
.Lcl_up:
	ld	a, (hl)
	and	a, e
	jr	z, .Lcl_up_no
	ld	(iy), d
	inc	iy
	inc	c
.Lcl_up_no:
	inc	d
	rlc	e
	jr	nc, .Lcl_up_same
	inc	hl
.Lcl_up_same:
	djnz	.Lcl_up
.Lcl_down:
	ld	a, (ix+15)
	or	a, a
	jr	z, .Lcl_done
	ld	b, a
	ld	a, (ix+9)
	add	a, 31			; slot - 1 (+ 32)
	call	cl_at
	ld	d, -1
.Lcl_dn:
	ld	a, (hl)
	and	a, e
	jr	z, .Lcl_dn_no
	ld	(iy), d
	inc	iy
	inc	c
.Lcl_dn_no:
	dec	d
	rrc	e
	jr	nc, .Lcl_dn_same
	dec	hl
.Lcl_dn_same:
	djnz	.Lcl_dn
.Lcl_done:
	ld	a, c
	pop	ix
	ret

; a: a bit 0..63 of cl_mask: hl its byte, e its bit (bc kept)
cl_at:
	push	af
	rrca
	rrca
	rrca
	and	a, 7
	ld	hl, cl_mask
	ld	de, 0
	ld	e, a
	add	hl, de
	pop	af
	and	a, 7
	ld	e, 1
	ret	z
.Lcla_shift:
	rlc	e
	dec	a
	jr	nz, .Lcla_shift
	ret

; ------------------------------------------------------------------------------------------------- rs_column
; void rs_column(const uint8_t *mask, int h2, int Y0, int Z0, int V0, int cx)
; a column's cells with a face (rs_cells' order), each in the view (distance, sides, top and bottom) and not
; hidden by what is drawn already: rs_cell(Y, Z, fog, j + 32, its y slot)
	.section	.text._rs_column
	.global	_rs_column
	.type	_rs_column, @function
_rs_column:
	push	ix
	ld	ix, 0
	add	ix, sp
	ld	hl, (ix+9)
	ld	(c_h2), hl
	ld	hl, (ix+12)
	ld	(c_Y0), hl
	ld	hl, (ix+15)
	ld	(c_Z0), hl
	ld	hl, (ix+18)
	ld	de, (_rs_vzk)
	add	hl, de
	ld	(c_V0), hl		; (V0 + vz0 + KY)
	ld	hl, (ix+21)
	ld	(c_cx), hl
	ld	de, (_rs_KX)
	or	a, a
	sbc	hl, de
	ld	(c_xa), hl		; cx - KX
	add	hl, de
	add	hl, de
	ld	(c_xb), hl		; cx + KX
	ld	hl, c_list
	push	hl
	ld	a, (_rs_down)
	ld	l, a
	push	hl
	ld	a, (_rs_up)
	ld	l, a
	push	hl
	ld	a, (_rs_eys)
	ld	l, a
	push	hl
	ld	hl, (ix+6)
	push	hl
	call	_rs_cells
	pop	hl
	pop	hl
	pop	hl
	pop	hl
	pop	hl
	ld	(c_left), a
	ld	hl, c_list
	ld	(c_ptr), hl
.Lco_next:
	ld	a, (c_left)
	or	a, a
	jp	z, .Lco_done
	dec	a
	ld	(c_left), a
	ld	hl, (c_ptr)
	ld	a, (hl)
	inc	hl
	ld	(c_ptr), hl
	ld	(c_lj), a
	ld	hl, _rs_jbase
	add	a, (hl)
	add	a, 32
	ld	(c_o), a
	; d2 = h2 + j^2, past the view: passed by
	or	a, a
	sbc	hl, hl
	ld	l, a
	add	hl, hl
	ld	de, _rs_jS
	add	hl, de
	ld	de, 0
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	ld	hl, (c_h2)
	add	hl, de
	ld	(c_d2), hl
	ex	de, hl
	ld	hl, (_rs_vlim)
	or	a, a
	sbc	hl, de
	jr	c, .Lco_next
	; Y, Z, V from the tables (3 bytes each)
	ld	a, (c_o)
	ld	l, a
	ld	h, 3
	mlt	hl
	push	hl
	ld	de, _rs_jY
	add	hl, de
	ld	de, (hl)
	ld	hl, (c_Y0)
	add	hl, de
	ld	(c_Y), hl
	ld	de, (_rs_cy0)
	add	hl, de
	ld	(c_cy), hl
	pop	hl
	push	hl
	ld	de, _rs_jZ
	add	hl, de
	ld	de, (hl)
	ld	hl, (c_Z0)
	add	hl, de
	ld	(c_Z), hl
	ld	de, (_rs_cz0)
	add	hl, de
	ld	(c_cz), hl
	pop	hl
	ld	de, _rs_jV
	add	hl, de
	ld	de, (hl)
	ld	hl, (c_V0)
	add	hl, de
	ld	(c_vz), hl
	; the view: cz >= -R, |cx| <= cz + KX, |cy| <= vz
	ld	hl, (c_cz)
	ld	de, (_rs_R)
	or	a, a
	adc	hl, de
	jp	m, .Lco_next
	ld	hl, (c_cz)
	ld	de, (c_xa)
	or	a, a
	sbc	hl, de
	jp	m, .Lco_next
	ld	hl, (c_cz)
	ld	de, (c_xb)
	or	a, a
	adc	hl, de
	jp	m, .Lco_next
	ld	hl, (c_vz)
	ld	de, (c_cy)
	or	a, a
	sbc	hl, de
	jp	m, .Lco_next
	ld	hl, (c_vz)
	or	a, a
	adc	hl, de
	jp	m, .Lco_next
	; the fog
	ld	hl, (c_d2)
	ld	de, _rs_fog
	add	hl, de
	ld	a, (hl)
	cp	a, 9
	jp	nc, .Lco_next
	ld	(c_fog), a
	; hidden? (far enough to tell: cz > 2R)
	ld	hl, (_rs_R)
	add	hl, hl
	ld	de, (c_cz)
	or	a, a
	sbc	hl, de
	jp	p, .Lco_draw
	ld	hl, (_rs_R)
	push	hl
	push	de
	ld	hl, (c_cy)
	push	hl
	ld	hl, (c_cx)
	push	hl
	call	_rs_hidden
	pop	hl
	pop	hl
	pop	hl
	pop	hl
	or	a, a
	jp	nz, .Lco_next
.Lco_draw:
	ld	a, (c_lj)
	ld	hl, _rs_eys
	add	a, (hl)
	and	a, 31
	ld	l, a
	push	hl
	ld	a, (c_o)
	ld	l, a
	push	hl
	ld	a, (c_fog)
	ld	l, a
	push	hl
	ld	hl, (c_Z)
	push	hl
	ld	hl, (c_Y)
	push	hl
	call	_rs_cell
	pop	hl
	pop	hl
	pop	hl
	pop	hl
	pop	hl
	jp	.Lco_next
.Lco_done:
	pop	ix
	ret

; ------------------------------------------------------------------------------------------------- rs_quad
; void rs_quad(void)
; a quadrant's columns (rs_q*), rows out from the eye along x, columns out along z, near to far: those in the
; window, in the view's distance and sides and with a face, to rs_column (rs_x, rs_z, rs_ci, rs_X0, rs_xs, rs_zs:
; the column)
	.section	.text._rs_quad
	.global	_rs_quad
	.type	_rs_quad, @function
_rs_quad:
	push	ix
	xor	a, a
	ld	(q_a), a
	ld	a, (_rs_qxs0)
	ld	(q_xs), a
	ld	hl, (_rs_qx0)
	ld	(q_x), hl
.Lq_row:
	; x in the window?
	ld	hl, (q_x)
	ld	de, (_rs_wx0)
	or	a, a
	sbc	hl, de
	jp	m, .Lq_done
	ld	hl, (q_x)
	ld	de, (_rs_wx1)
	or	a, a
	sbc	hl, de
	jp	p, .Lq_done
	; the row's first column
	ld	hl, (_rs_qXi)
	ld	(q_X), hl
	ld	hl, (_rs_qYi)
	ld	(q_Y), hl
	ld	hl, (_rs_qZi)
	ld	(q_Z), hl
	ld	hl, (_rs_qVi)
	ld	(q_V), hl
	ld	a, (_rs_qzs0)
	ld	(q_zs), a
	ld	hl, (_rs_qz0)
	ld	(q_z), hl
	ld	a, (q_a)
	or	a, a
	sbc	hl, hl
	ld	l, a
	add	hl, hl
	ld	de, (_rs_qsqi)
	add	hl, de
	ld	de, 0
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	ld	(q_hi), de		; |i| squared
	xor	a, a
	ld	(q_c), a
.Lq_col:
	; h2 = |i|^2 + |k|^2, past the view: the row's end
	ld	a, (q_c)
	or	a, a
	sbc	hl, hl
	ld	l, a
	add	hl, hl
	ld	de, (_rs_qsqk)
	add	hl, de
	ld	de, 0
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	ld	hl, (q_hi)
	add	hl, de
	ld	(q_h2), hl
	ex	de, hl
	ld	hl, (_rs_vlim)
	or	a, a
	sbc	hl, de
	jp	c, .Lq_rowend
	; z in the window?
	ld	hl, (q_z)
	ld	de, (_rs_wz0)
	or	a, a
	sbc	hl, de
	jp	m, .Lq_rowend
	ld	hl, (q_z)
	ld	de, (_rs_wz1)
	or	a, a
	sbc	hl, de
	jp	p, .Lq_rowend
	; the view's sides: |cx| <= Z + czm
	ld	hl, (q_X)
	ld	de, (_rs_cx0)
	add	hl, de
	ld	(q_cx), hl
	ex	de, hl
	ld	hl, (q_Z)
	ld	bc, (_rs_czm)
	add	hl, bc
	push	hl
	or	a, a
	sbc	hl, de
	pop	hl
	jp	m, .Lq_next
	or	a, a
	adc	hl, de
	jp	m, .Lq_next
	; the column: ci = zs * 32 + xs, its mask
	ld	a, (q_zs)
	ld	l, a
	ld	h, 32
	mlt	hl
	ld	a, (q_xs)
	ld	de, 0
	ld	e, a
	add	hl, de
	ld	(_rs_ci), hl
	add	hl, hl
	add	hl, hl
	ld	de, _wexp
	add	hl, de
	push	hl
	ld	a, (hl)
	inc	hl
	or	a, (hl)
	inc	hl
	or	a, (hl)
	inc	hl
	or	a, (hl)
	pop	de
	jp	z, .Lq_next
	ld	hl, (q_x)
	ld	(_rs_x), hl
	ld	hl, (q_z)
	ld	(_rs_z), hl
	ld	hl, (q_X)
	ld	(_rs_X0), hl
	ld	a, (q_xs)
	ld	(_rs_xs), a
	ld	a, (q_zs)
	ld	(_rs_zs), a
	ld	hl, (q_cx)
	push	hl
	ld	hl, (q_V)
	push	hl
	ld	hl, (q_Z)
	push	hl
	ld	hl, (q_Y)
	push	hl
	ld	hl, (q_h2)
	push	hl
	push	de
	call	_rs_column
	ld	hl, 18
	add	hl, sp
	ld	sp, hl
.Lq_next:
	; the next column out along z
	ld	a, (q_c)
	inc	a
	ld	(q_c), a
	ld	hl, _rs_vd
	cp	a, (hl)
	jr	z, .Lq_cok
	jp	nc, .Lq_rowend
.Lq_cok:
	ld	a, (_rs_qsz)
	ld	hl, q_zs
	add	a, (hl)
	and	a, 31
	ld	(hl), a
	ld	hl, (q_z)
	ld	de, (_rs_qszi)
	add	hl, de
	ld	(q_z), hl
	ld	hl, (q_X)
	ld	de, (_rs_qsXz)
	add	hl, de
	ld	(q_X), hl
	ld	hl, (q_Y)
	ld	de, (_rs_qsYz)
	add	hl, de
	ld	(q_Y), hl
	ld	hl, (q_Z)
	ld	de, (_rs_qsZz)
	add	hl, de
	ld	(q_Z), hl
	ld	hl, (q_V)
	ld	de, (_rs_qsVz)
	add	hl, de
	ld	(q_V), hl
	jp	.Lq_col
.Lq_rowend:
	; the next row out along x
	ld	a, (q_a)
	inc	a
	ld	(q_a), a
	ld	hl, _rs_vd
	cp	a, (hl)
	jr	z, .Lq_aok
	jp	nc, .Lq_done
.Lq_aok:
	ld	a, (_rs_qsx)
	ld	hl, q_xs
	add	a, (hl)
	and	a, 31
	ld	(hl), a
	ld	hl, (q_x)
	ld	de, (_rs_qsxi)
	add	hl, de
	ld	(q_x), hl
	ld	hl, (_rs_qXi)
	ld	de, (_rs_qsXx)
	add	hl, de
	ld	(_rs_qXi), hl
	ld	hl, (_rs_qYi)
	ld	de, (_rs_qsYx)
	add	hl, de
	ld	(_rs_qYi), hl
	ld	hl, (_rs_qZi)
	ld	de, (_rs_qsZx)
	add	hl, de
	ld	(_rs_qZi), hl
	ld	hl, (_rs_qVi)
	ld	de, (_rs_qsVx)
	add	hl, de
	ld	(_rs_qVi), hl
	jp	.Lq_row
.Lq_done:
	pop	ix
	ret

; ------------------------------------------------------------------------------------------------- data
	.section	.data._rs_vars
r_tex:	.d24	0
r_cm:	.d24	0
r_loop:	.d24	span_plain
r_u:	.d24	0
r_v:	.d24	0
r_du:	.d24	0
r_dv:	.d24	0
r_end:	.db	0
r_row:	.d24	0
r_y:	.db	0
r_yend:	.db	0
r_px0:	.db	0
r_n:	.db	0
m_a:	.d24	0
m_r:	.d24	0
m_f:	.d24	0
m_neg:	.db	0
m_t:	.d24	0
	.db	0
p_base:	.d24	0
p_n:	.db	0
e_b:	.d24	0
e_yb:	.d24	0
e_r1:	.db	0
e_f:	.db	0
e_q:	.db	0
e_d:	.d24	0
e_dx:	.d24	0
e_du:	.d24	0
e_dv:	.d24	0
e_r:	.d24	0
e_ymin:	.d24	0
e_ymax:	.d24	0
e_top:	.db	0
w_l:	.d24	0
w_r:	.d24	0
wa:	.ds	W_SIZE
wb:	.ds	W_SIZE
r_hud:	.d24	0
r_rows:	.db	0
r_hudlim:	.db	0
r_line:	.ds	320
r_line2:	.ds	320
cl_mask:	.ds	8
r_lock:	.db	0
r_ku:	.ds	48
r_kv:	.ds	48
kt_base:	.d24	0
h_r:	.d24	0
h_c:	.d24	0
h_y0:	.db	0
h_rows:	.db	0
h_w:	.db	0
c_h2:	.d24	0
c_Y0:	.d24	0
c_Z0:	.d24	0
c_V0:	.d24	0
c_cx:	.d24	0
c_xa:	.d24	0
c_xb:	.d24	0
c_ptr:	.d24	0
c_d2:	.d24	0
c_Y:	.d24	0
c_cy:	.d24	0
c_Z:	.d24	0
c_cz:	.d24	0
c_vz:	.d24	0
c_left:	.db	0
c_lj:	.db	0
c_o:	.db	0
c_fog:	.db	0
c_list:	.ds	64
	.global	_rs_jY
	.global	_rs_jZ
	.global	_rs_jV
	.global	_rs_jS
	.global	_rs_fog
	.global	_rs_vlim
	.global	_rs_cy0
	.global	_rs_cz0
	.global	_rs_vzk
	.global	_rs_R
	.global	_rs_KX
	.global	_rs_eys
	.global	_rs_up
	.global	_rs_down
	.global	_rs_jbase
_rs_jY:	.ds	65 * 3
_rs_jZ:	.ds	65 * 3
_rs_jV:	.ds	65 * 3
_rs_jS:	.ds	128 * 2
_rs_fog:	.ds	257
_rs_vlim:	.d24	0
_rs_cy0:	.d24	0
_rs_cz0:	.d24	0
_rs_vzk:	.d24	0
_rs_R:	.d24	0
_rs_KX:	.d24	0
_rs_eys:	.db	0
_rs_up:	.db	0
_rs_down:	.db	0
_rs_jbase:	.db	0
	.global	_rs_qXi
	.global	_rs_qYi
	.global	_rs_qZi
	.global	_rs_qVi
	.global	_rs_qsXx
	.global	_rs_qsYx
	.global	_rs_qsZx
	.global	_rs_qsVx
	.global	_rs_qsXz
	.global	_rs_qsYz
	.global	_rs_qsZz
	.global	_rs_qsVz
	.global	_rs_qx0
	.global	_rs_qz0
	.global	_rs_qsxi
	.global	_rs_qszi
	.global	_rs_qsqi
	.global	_rs_qsqk
	.global	_rs_czm
	.global	_rs_cx0
	.global	_rs_wx0
	.global	_rs_wx1
	.global	_rs_wz0
	.global	_rs_wz1
	.global	_rs_x
	.global	_rs_z
	.global	_rs_ci
	.global	_rs_X0
	.global	_rs_qsx
	.global	_rs_qsz
	.global	_rs_qxs0
	.global	_rs_qzs0
	.global	_rs_vd
	.global	_rs_xs
	.global	_rs_zs
_rs_qXi:	.d24	0
_rs_qYi:	.d24	0
_rs_qZi:	.d24	0
_rs_qVi:	.d24	0
_rs_qsXx:	.d24	0
_rs_qsYx:	.d24	0
_rs_qsZx:	.d24	0
_rs_qsVx:	.d24	0
_rs_qsXz:	.d24	0
_rs_qsYz:	.d24	0
_rs_qsZz:	.d24	0
_rs_qsVz:	.d24	0
_rs_qx0:	.d24	0
_rs_qz0:	.d24	0
_rs_qsxi:	.d24	0
_rs_qszi:	.d24	0
_rs_qsqi:	.d24	0
_rs_qsqk:	.d24	0
_rs_czm:	.d24	0
_rs_cx0:	.d24	0
_rs_wx0:	.d24	0
_rs_wx1:	.d24	0
_rs_wz0:	.d24	0
_rs_wz1:	.d24	0
_rs_x:	.d24	0
_rs_z:	.d24	0
_rs_ci:	.d24	0
_rs_X0:	.d24	0
_rs_qsx:	.db	0
_rs_qsz:	.db	0
_rs_qxs0:	.db	0
_rs_qzs0:	.db	0
_rs_vd:	.db	0
_rs_xs:	.db	0
_rs_zs:	.db	0
q_X:	.d24	0
q_Y:	.d24	0
q_Z:	.d24	0
q_V:	.d24	0
q_x:	.d24	0
q_z:	.d24	0
q_hi:	.d24	0
q_h2:	.d24	0
q_cx:	.d24	0
q_a:	.db	0
q_c:	.db	0
q_xs:	.db	0
q_zs:	.db	0

	.section	.rodata._rs_rtab
; 1371 << 12 / (256 + i)
r_rtab:
	.irp	i, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127,128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255
	.dw	(1371 * 4096) / (256 + \i)
	.endr

	.extern	_rbuf
	.extern	_rs_cell
	.extern	_wexp
	.extern	_rs_recip
	.extern	_rs_ratio
