; The 3D view's inner loops (rs.h), position independent: run from the program (rs.s includes this) or from the
; archive (flash/rsf.s: NBCETEX carries a copy), where code costs 2 or 3 cycles a byte to read, not 4. Relative jumps
; only; the routines call each other through their trampolines (t_*, in RAM: rs.s); data at its RAM addresses.

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
b__rs_setup:
	ld	iy, 0
	add	iy, sp
	ld	hl, (iy+3)
	ld	(r_tex), hl
	ld	hl, (iy+6)
	ld	(r_cm), hl
	; the loop (the copy run: its trampoline's)
	ld	hl, (t_span_plain + 1)
	ld	a, (iy+9)
	or	a, a
	jr	z, .Lsetup_set
	ld	hl, (t_span_alpha + 1)
.Lsetup_set:
	ld	(r_loop), hl
	ret

; ------------------------------------------------------------------------------------------------- the span
; de dst, a n (1..160); r_u, r_v, r_du, r_dv (u 8.8, v 4.12)
; main set: de dst, hl the texture (h its page), bc the shade row (b its page, c the texel)
; other set: hl u, de du, bc dv; ix v; iy points at the end's low byte
b_span:
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

b_span_plain:
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
	jr	nz, b_span_plain
	pop	ix
	ret

b_span_alpha:
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
	jr	nz, b_span_alpha
	pop	ix
	ret

; ------------------------------------------------------------------------------------------------- arithmetic
; hl = hl x de >> 24: hl signed, de 0..2^24 (the low partial products left out)
b_mulhi:
	push	bc
	ld	(m_r), de
	xor	a, a
	ld	(m_neg), a
	ld	(m_a), hl
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
	ld	a, (m_a+2)
	ld	de, (m_r)
.Lmh_pos:
	; |a| in m_a: l a0, h a1, a a2; r: e r0, d r1, r2 at m_r+2. t = a2 r0 + a1 r1 + a0 r2
	ld	c, a			; a2
	ld	a, (m_r+2)
	ld	b, a			; r2
	push	de
	ld	a, l			; a0
	ld	l, d
	mlt	hl			; a1 r1
	ld	d, c
	mlt	de			; a2 r0
	add	hl, de
	ld	d, a
	ld	e, b
	mlt	de			; a0 r2
	add	hl, de
	ld	(m_t), hl
	ld	hl, (m_t+1)		; t >> 8 (m_t+3 is 0)
	pop	de
	; + a2 r1 + a1 r2
	ld	e, c
	mlt	de			; a2 r1
	add	hl, de
	ld	a, (m_a+1)
	ld	d, a
	ld	e, b
	mlt	de			; a1 r2
	add	hl, de
	; + a2 r2 << 8
	ld	d, c
	ld	e, b
	mlt	de
	ld	(m_t+1), de		; (de's top byte is 0 after mlt: m_t+3 stays 0)
	xor	a, a
	ld	(m_t), a
	ld	de, (m_t)
	add	hl, de
	pop	bc
	jr	b_neg_if

; hl = hl x a >> 8: hl signed, a 0..255 (b kept)
b_mul8:
	ld	c, a			; f
	ld	(m_a), hl
	ld	a, (m_a+2)
	ld	(m_f), a		; (its top bit: negative)
	bit	7, a
	jr	z, .Lm8_pos
	ex	de, hl
	or	a, a
	sbc	hl, hl
	sbc	hl, de
	ld	(m_a), hl
	ld	a, (m_a+2)
.Lm8_pos:
	; (s2 f << 8) + (s0 f >> 8) + s1 f
	ld	d, a
	ld	e, c
	mlt	de			; s2 f
	ld	(m_t+1), de		; (m_t+3 stays 0)
	ld	e, h			; s1
	ld	h, c
	mlt	hl			; s0 f
	ld	a, h
	ld	(m_t), a
	ld	d, c
	mlt	de			; s1 f
	ld	hl, (m_t)
	add	hl, de
	ld	a, (m_f)
	bit	7, a
	ret	z
	ex	de, hl
	or	a, a
	sbc	hl, hl
	sbc	hl, de
	ret

b_neg_if:
	ld	a, (m_neg)
	or	a, a
	ret	z
	ex	de, hl
	or	a, a
	sbc	hl, hl
	sbc	hl, de
	ret

; hl = p + a * 12
b_vptr:
	ld	l, a
	ld	h, 12
	mlt	hl
	ld	de, (p_base)
	add	hl, de
	ret

; ------------------------------------------------------------------------------------------------- edges
; the walker ix's next edge that crosses a row's middle; nz: one, z: none left
b_edge:
	ld	a, (p_n)
	ld	b, a
b_Led_try:
.Led_try:
	push	bc
	ld	a, (ix+W_IDX)
	call	t_vptr
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
	call	t_vptr			; hl: the vertex b
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
	jp	z, t_Led_next
	jp	c, t_Led_next
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
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	(e_dx), hl
	ld	hl, (e_b)
	ld	de, 6
	add	hl, de
	ld	de, (hl)		; ub
	ex	de, hl
	ld	de, (iy+6)
	or	a, a
	sbc	hl, de
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	(e_du), hl
	ld	hl, (e_b)
	ld	de, 9
	add	hl, de
	ld	de, (hl)		; vb
	ex	de, hl
	ld	de, (iy+9)
	or	a, a
	sbc	hl, de
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
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
	call	t_mulhi
	ld	(ix+W_SX), hl
	ld	de, (e_r)
	ld	hl, (e_du)
	call	t_mulhi
	ld	(ix+W_SU), hl
	ld	de, (e_r)
	ld	hl, (e_dv)
	call	t_mulhi
	ld	(ix+W_SV), hl
	; the first row: the vertex + step x f / 16
	ld	hl, (ix+W_SX)
	ld	a, (e_f)
	call	t_mul8
	ld	(e_dx), hl
	ld	hl, (ix+W_SU)
	ld	a, (e_f)
	call	t_mul8
	ld	(e_du), hl
	ld	hl, (ix+W_SV)
	ld	a, (e_f)
	call	t_mul8
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
	call	t_mul8
	ld	(e_dx), hl
	ld	a, (e_q)
	ld	hl, (e_du)
	call	t_mul8
	ld	(e_du), hl
	ld	a, (e_q)
	ld	hl, (e_dv)
	call	t_mul8
	ld	(e_dv), hl
.Led_start:
	; x = xa * 16 + e_dx, ...
	ld	hl, (iy+0)
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	de, (e_dx)
	add	hl, de
	ld	(ix+W_X), hl
	ld	hl, (iy+6)
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	de, (e_du)
	add	hl, de
	ld	(ix+W_U), hl
	ld	hl, (iy+9)
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	de, (e_dv)
	add	hl, de
	ld	(ix+W_V), hl
	pop	bc
	or	a, 1			; nz: found
	ret
b_Led_next:
.Led_next:
	pop	bc
	dec	b
	jp	nz, t_Led_try
	xor	a, a			; z: none
	ret


; de: a table of 16 steps of hl (texel x 65536 a pixel): hl x (k x 16 + 8) / 256, k 0..15
b_ktable:
	push	de
	push	hl
	ld	a, 8
	call	t_mul8
	ld	(kt_base), hl
	pop	hl
	ld	a, 16
	call	t_mul8
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
b__rs_poly:
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
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	a, h
	ld	(r_y), a
	ld	hl, (e_ymax)
	add	hl, de
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	a, h
	ld	(r_yend), a
	ld	hl, r_y
	sub	a, (hl)
	jp	z, t_Lpo_done
	jp	c, t_Lpo_done
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
	call	t_edge
	jp	z, t_Lpo_done
	ld	ix, wb
	call	t_edge
	jp	z, t_Lpo_done
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
	add	hl, hl		; (the sign to the carry)
	jr	c, .Lpo_row		; (x small: no overflow)
	jr	.Lpo_swap
.Lpo_tie:
	ld	hl, (wa+W_SX)
	ld	de, (wb+W_SX)
	or	a, a
	sbc	hl, de
	add	hl, hl		; (the sign to the carry)
	jr	c, .Lpo_row
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
b_Lpo_loop:
.Lpo_loop:
	; px1 = (xr + 127) >> 8, at most 160; px0 = (xl + 127) >> 8 (x >= 0 on the screen: the high byte)
	ld	de, 127
	ld	ix, (w_r)
	ld	hl, (ix+W_X)
	add	hl, de
	ld	a, h
	cp	a, RW + 1
	jr	c, .Lpo_x1ok
	ld	a, RW
.Lpo_x1ok:
	ld	c, a
	ld	ix, (w_l)
	ld	hl, (ix+W_X)
	add	hl, de
	ld	a, c
	sub	a, h
	jp	z, t_Lpo_step
	jp	c, t_Lpo_step
	ld	(r_n), a
	ld	a, h
	ld	(r_px0), a
	ld	a, (r_lock)
	or	a, a
	jp	nz, t_Lpo_locked
	ld	ix, (w_r)
	; w = (xr - xl) >> 4: du, dv across it (0 under a pixel)
	ld	hl, (ix+W_X)
	ld	ix, (w_l)
	ld	de, (ix+W_X)
	or	a, a
	sbc	hl, de
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
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
	jp	t_Lpo_grad
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
	call	t_mulhi
	ld	(r_du), hl
	ld	ix, (w_r)
	ld	hl, (ix+W_V)
	ld	ix, (w_l)
	ld	de, (ix+W_V)
	or	a, a
	sbc	hl, de
	ld	de, (e_r)
	call	t_mulhi
	ld	(r_dv), hl
	ld	a, (r_lock)
	or	a, a
	jr	z, .Lpo_grad
	; locked: k/16 + 1/32 of a pixel's step, k 0..15, to step from the edge to a pixel's middle
	ld	hl, (r_du)
	ld	de, r_ku
	call	t_ktable
	ld	hl, (r_dv)
	ld	de, r_kv
	call	t_ktable
	; the steps in the span's units: du >> 8, dv >> 4 (kept for the rows below)
	ld	hl, (r_du)
	ld	(m_t), hl
	ld	hl, (m_t+1)
	ld	(r_du), hl
	ld	hl, (r_dv)
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	(m_t), hl
	ld	hl, (m_t+1)
	ld	(r_dv), hl
	jr	.Lpo_locked
b_Lpo_grad:
.Lpo_grad:
	; frac = 128 - xl (low byte): from the edge to the first pixel's middle
	ld	ix, (w_l)
	ld	a, 128
	sub	a, (ix+W_X)
	ld	(e_q), a
	ld	hl, (r_du)
	call	t_mul8
	ld	de, (ix+W_U)
	add	hl, de
	call	t_clamp20
	ld	(m_t), hl
	ld	hl, (m_t+1)
	ld	(r_u), hl		; u >> 8: 8.8
	ld	a, (e_q)
	ld	hl, (r_dv)
	call	t_mul8
	ld	de, (ix+W_V)
	add	hl, de
	call	t_clamp20
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	(m_t), hl
	ld	hl, (m_t+1)
	ld	(r_v), hl		; v >> 4: 4.12
	ld	hl, (r_du)
	ld	(m_t), hl
	ld	hl, (m_t+1)
	ld	(r_du), hl		; du >> 8
	ld	hl, (r_dv)
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	(m_t), hl
	ld	hl, (m_t+1)
	ld	(r_dv), hl		; dv >> 4
	jr	.Lpo_span
b_Lpo_locked:
.Lpo_locked:
	; (ix: the left walker) from the edge to the first pixel's middle: the table's step for (128 - xl) >> 4;
	; u >> 8 (8.8) and v >> 4 (4.12) for the span, kept to the texture
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
	ld	(m_t), hl
	ld	a, (m_t+2)
	cp	a, 16
	jr	nc, .Lpo_uc
	ld	hl, (m_t+1)
.Lpo_us:
	ld	(r_u), hl
	pop	hl
	ld	de, r_kv
	add	hl, de
	ld	de, (hl)
	ld	hl, (ix+W_V)
	add	hl, de
	ld	(m_t), hl
	ld	a, (m_t+2)
	cp	a, 16
	jr	nc, .Lpo_vc
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	(m_t), hl
	ld	hl, (m_t+1)
.Lpo_vs:
	ld	(r_v), hl
	jr	.Lpo_span
.Lpo_uc:
	rla				; (past the texture: below its start, or past its end)
	ld	hl, 0x0FFF
	jr	nc, .Lpo_us
	ld	hl, 0
	jr	.Lpo_us
.Lpo_vc:
	rla
	ld	hl, 0xFFFF
	jr	nc, .Lpo_vs
	ld	hl, 0
	jr	.Lpo_vs
.Lpo_span:
	ld	a, (r_px0)
	ld	de, 0
	ld	e, a
	ld	hl, (r_row)
	add	hl, de
	ex	de, hl
	ld	a, (r_n)
	call	t_span
b_Lpo_step:
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
	call	t_wstep
	ld	ix, (w_r)
	ld	a, (r_lock)
	or	a, a
	jr	z, .Lpo_rboth
	call	t_wstep_x			; (locked: the right edge's u and v no longer needed)
	jp	t_Lpo_loop
.Lpo_rboth:
	call	t_wstep
	jp	t_Lpo_loop
b_Lpo_done:
.Lpo_done:
	pop	ix
	ret

; the walker ix a row on; its next edge when this one is done; nz: going on
b_wstep:
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
	call	t_edge
	ret	nz
	pop	hl			; (none left: the polygon is done)
	jr	.Lpo_done

; the same, x alone
b_wstep_x:
	ld	hl, (ix+W_X)
	ld	de, (ix+W_SX)
	add	hl, de
	ld	(ix+W_X), hl
	dec	(ix+W_ROWS)
	ret	nz
	call	t_edge
	ret	nz
	pop	hl
	jr	.Lpo_done

; hl into 0..0xFFFFF
b_clamp20:
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
b__rs_proj:
	ld	iy, 0
	add	iy, sp
	ld	hl, (iy+12)
	ld	(pj_out), hl
; (iy + 3: x, y, z; pj_out: where to)
b_proj_iy:
	ld	hl, (iy+9)		; z
	call	t_pr_z
	ld	hl, (iy+3)
	call	t_pr_mul
	ld	de, 1280
	add	hl, de
	push	hl
	ld	hl, (iy+6)
	call	t_pr_mul
	ex	de, hl
	ld	hl, 960
	or	a, a
	sbc	hl, de
	ld	iy, (pj_out)
	ld	(iy+3), hl
	pop	hl
	ld	(iy+0), hl
	ret

; m_r = t, m_f = the shift, for z (hl)
b_pr_z:
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
b_pr_mul:
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
	jp	t_neg_if

; ------------------------------------------------------------------------------------------------- rs_hidden
; uint8_t rs_hidden(int x, int y, int z, int r)
; 1 when the pixels around (x, y, z)'s projection, as far as r's projection and a pixel more each way, are all
; drawn already (or off the screen)
b__rs_hidden:
	ld	iy, 0
	add	iy, sp
	ld	hl, (iy+9)
	call	t_pr_z
	ld	hl, (iy+12)
	call	t_pr_mul
	ld	de, 16
	add	hl, de
	ld	(h_r), hl
	ld	hl, (iy+6)
	call	t_pr_mul
	ex	de, hl
	ld	hl, 960
	or	a, a
	sbc	hl, de
	ld	c, 119
	call	t_h_range
	jr	c, .Lh_yes
	ld	a, b
	ld	(h_y0), a
	ld	a, c
	ld	(h_rows), a
	ld	hl, (iy+3)
	call	t_pr_mul
	ld	de, 1280
	add	hl, de
	ld	c, RW - 1
	call	t_h_range
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
b_h_range:
	ld	(h_c), hl
	ld	de, (h_r)
	or	a, a
	sbc	hl, de
	call	t_h_px
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
	call	t_h_px
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
b_h_px:
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
	.equ	HUD_X, 67
	.equ	HUD_W, 187
b__rs_expand:
	push	ix
	ld	iy, 0
	add	iy, sp
	ld	hl, (iy+15)
	ld	(r_hud), hl
	ld	hl, _rs_hruns
	ld	(r_hrun), hl
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
	ld	b, RW / 2
	call	t_r_double
	push	hl
	ld	hl, -320
	add	hl, de
	ld	bc, 320
	ldir
	pop	hl
b_Lex_next:
.Lex_next:
	inc	ix
	ld	a, (r_rows)
	dec	a
	ld	(r_rows), a
	jr	nz, .Lex_row
	pop	ix
	ret

.Lex_hudrow:
	; (each screen row its own HUD row: from the runs, each pixel written once)
	ld	a, (_rs_hruns_ok)
	or	a, a
	jr	z, .Lex_hudold
	ld	(r_srow), hl
	ld	a, (ix+0)
	ld	(r_sky), a
	call	t_r_hudrow
	ld	hl, (r_srow)
	call	t_r_hudrow
	ld	hl, (r_srow)
	ld	bc, RW
	add	hl, bc
	ld	iy, r_hudlim
	jr	.Lex_next
.Lex_hudold:
	push	de
	ld	de, r_line
	ld	c, (ix+0)
	ld	b, RW / 2
	call	t_r_double
	push	hl
	ld	hl, r_line
	ld	de, r_line2
	ld	bc, 320
	ldir
	ld	de, r_line + HUD_X
	ld	hl, (r_hud)
	ld	b, HUD_W
	call	t_r_overlay
	ld	de, r_line2 + HUD_X
	ld	b, HUD_W
	call	t_r_overlay
	ld	(r_hud), hl
	pop	hl
	pop	de
	push	hl
	ld	hl, r_line
	ld	bc, 640
	ldir
	pop	hl
	jp	t_Lex_next

; b pairs of pixels from hl doubled to de (0: c, the row's sky); hl and de after them (flash/rsf.s has it too)
b_r_double:
	ld	a, (hl)
	inc	hl
	or	a, a
	jr	nz, .Lrd_set
	ld	a, c
.Lrd_set:
	ld	(de), a
	inc	de
	ld	(de), a
	inc	de
	ld	a, (hl)
	inc	hl
	or	a, a
	jr	nz, .Lrd_set2
	ld	a, c
.Lrd_set2:
	ld	(de), a
	inc	de
	ld	(de), a
	inc	de
	djnz	b_r_double
	ret

; b pixels of the layer (hl) over de, 0 see-through; hl and de after them
b_r_overlay:
	ld	a, (hl)
	inc	hl
	or	a, a
	jr	z, .Lov_skip
	ld	(de), a
.Lov_skip:
	inc	de
	djnz	b_r_overlay
	ret


; ------------------------------------------------------------------------------------------------- rs_cells
; uint8_t rs_cells(const uint8_t *mask, uint8_t slot, uint8_t up, uint8_t down, int8_t *out)
; a column's cells with a face (mask: 32 bits, bit y & 31): from slot up (0, 1, .. up - 1 levels above), then
; down (-1, -2, .. -down): the offsets of those set, in that order; how many
b__rs_cells:
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
	call	t_cl_at			; hl: the byte, e: the bit
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
	call	t_cl_at
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
b_cl_at:
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
b__rs_column:
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
	call	t__rs_cells
	pop	hl
	pop	hl
	pop	hl
	pop	hl
	pop	hl
	ld	(c_left), a
	ld	hl, c_list
	ld	(c_ptr), hl
b_Lco_next:
.Lco_next:
	ld	a, (c_left)
	or	a, a
	jp	z, t_Lco_done
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
	add	hl, hl		; (the sign to the carry)
	jp	c, t_Lco_next
	ld	hl, (c_cz)
	ld	de, (c_xa)
	or	a, a
	sbc	hl, de
	add	hl, hl		; (the sign to the carry)
	jp	c, t_Lco_next
	ld	hl, (c_cz)
	ld	de, (c_xb)
	or	a, a
	adc	hl, de
	add	hl, hl		; (the sign to the carry)
	jp	c, t_Lco_next
	ld	hl, (c_vz)
	ld	de, (c_cy)
	or	a, a
	sbc	hl, de
	add	hl, hl		; (the sign to the carry)
	jp	c, t_Lco_next
	ld	hl, (c_vz)
	or	a, a
	adc	hl, de
	add	hl, hl		; (the sign to the carry)
	jp	c, t_Lco_next
	; the fog
	ld	hl, (c_d2)
	ld	de, _rs_fog
	add	hl, de
	ld	a, (hl)
	cp	a, 9
	jp	nc, t_Lco_next
	ld	(c_fog), a
	; hidden? (far enough to tell: cz > 2R)
	ld	hl, (_rs_R)
	add	hl, hl
	ld	de, (c_cz)
	or	a, a
	sbc	hl, de
	add	hl, hl		; (the sign to the carry)
	jr	nc, .Lco_draw
	ld	hl, (_rs_R)
	push	hl
	push	de
	ld	hl, (c_cy)
	push	hl
	ld	hl, (c_cx)
	push	hl
	call	t__rs_hidden
	pop	hl
	pop	hl
	pop	hl
	pop	hl
	or	a, a
	jp	nz, t_Lco_next
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
	jp	t_Lco_next
b_Lco_done:
.Lco_done:
	pop	ix
	ret

; ------------------------------------------------------------------------------------------------- rs_quad
; void rs_quad(void)
; a quadrant's columns (rs_q*), rows out from the eye along x, columns out along z, near to far: those in the
; window, in the view's distance and sides and with a face, to rs_column (rs_x, rs_z, rs_ci, rs_X0, rs_xs, rs_zs:
; the column)
b__rs_quad:
	push	ix
	xor	a, a
	ld	(q_a), a
	ld	a, (_rs_qxs0)
	ld	(q_xs), a
	ld	hl, (_rs_qx0)
	ld	(q_x), hl
b_Lq_row:
.Lq_row:
	; x in the window?
	ld	hl, (q_x)
	ld	de, (_rs_wx0)
	or	a, a
	sbc	hl, de
	add	hl, hl		; (the sign to the carry)
	jp	c, t_Lq_done
	ld	hl, (q_x)
	ld	de, (_rs_wx1)
	or	a, a
	sbc	hl, de
	add	hl, hl		; (the sign to the carry)
	jp	nc, t_Lq_done
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
b_Lq_col:
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
	jp	c, t_Lq_rowend
	; z in the window?
	ld	hl, (q_z)
	ld	de, (_rs_wz0)
	or	a, a
	sbc	hl, de
	add	hl, hl		; (the sign to the carry)
	jp	c, t_Lq_rowend
	ld	hl, (q_z)
	ld	de, (_rs_wz1)
	or	a, a
	sbc	hl, de
	add	hl, hl		; (the sign to the carry)
	jp	nc, t_Lq_rowend
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
	add	hl, hl		; (the sign to the carry)
	pop	hl
	jr	c, .Lq_next
	or	a, a
	adc	hl, de
	add	hl, hl		; (the sign to the carry)
	jr	c, .Lq_next
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
	jr	z, .Lq_next
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
	call	t__rs_column
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
	jr	nc, .Lq_rowend
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
	jp	t_Lq_col
b_Lq_rowend:
.Lq_rowend:
	call	_plat_poll		; (a key tapped while the picture is made)
	; the next row out along x
	ld	a, (q_a)
	inc	a
	ld	(q_a), a
	ld	hl, _rs_vd
	cp	a, (hl)
	jr	z, .Lq_aok
	jr	nc, .Lq_done
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
	jp	t_Lq_row
b_Lq_done:
.Lq_done:
	pop	ix
	ret

; ------------------------------------------------------------------------------------------------- rs_code
; int rs_code(int x, int y, int z)
; bits 0-4: the planes a polygon through (x, y, z) is cut by (z < NEAR, x > 4z, -x > 4z, y > 4z, -y > 4z); bits
; 8-12: the view's sides it is past, a little wider than the screen's (z < NEAR, 16x > 15z, -16x > 15z, 4y > 3z,
; -4y > 3z)
	.equ	NEAR, 819
b__rs_code:
	ld	iy, 0
	add	iy, sp
; (iy + 3: x, y, z)
b_code_iy:
	ld	bc, 0
	ld	hl, (iy+9)
	ld	de, NEAR
	or	a, a
	sbc	hl, de
	add	hl, hl		; (the sign to the carry)
	jr	nc, .Lrc_front
	ld	bc, 0x0101
.Lrc_front:
	ld	hl, (iy+9)
	add	hl, hl
	add	hl, hl
	ld	(rc_t), hl		; 4z
	ld	de, (iy+3)
	or	a, a
	sbc	hl, de
	add	hl, hl		; (the sign to the carry)
	jr	nc, .Lrc1
	set	1, c
.Lrc1:
	ld	hl, (rc_t)
	or	a, a
	adc	hl, de
	add	hl, hl		; (the sign to the carry)
	jr	nc, .Lrc2
	set	2, c
.Lrc2:
	ld	hl, (rc_t)
	ld	de, (iy+6)
	or	a, a
	sbc	hl, de
	add	hl, hl		; (the sign to the carry)
	jr	nc, .Lrc3
	set	3, c
.Lrc3:
	ld	hl, (rc_t)
	or	a, a
	adc	hl, de
	add	hl, hl		; (the sign to the carry)
	jr	nc, .Lrc4
	set	4, c
.Lrc4:
	ld	hl, (iy+9)
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	de, (iy+9)
	or	a, a
	sbc	hl, de
	ld	(rc_t), hl		; 15z
	ld	hl, (iy+3)
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ex	de, hl			; 16x
	ld	hl, (rc_t)
	or	a, a
	sbc	hl, de
	add	hl, hl		; (the sign to the carry)
	jr	nc, .Lrc5
	set	1, b
.Lrc5:
	ld	hl, (rc_t)
	or	a, a
	adc	hl, de
	add	hl, hl		; (the sign to the carry)
	jr	nc, .Lrc6
	set	2, b
.Lrc6:
	ld	hl, (iy+9)
	push	hl
	pop	de
	add	hl, hl
	add	hl, de
	ld	(rc_t), hl		; 3z
	ld	hl, (iy+6)
	add	hl, hl
	add	hl, hl
	ex	de, hl			; 4y
	ld	hl, (rc_t)
	or	a, a
	sbc	hl, de
	add	hl, hl		; (the sign to the carry)
	jr	nc, .Lrc7
	set	3, b
.Lrc7:
	ld	hl, (rc_t)
	or	a, a
	adc	hl, de
	add	hl, hl		; (the sign to the carry)
	jr	nc, .Lrc8
	set	4, b
.Lrc8:
	or	a, a
	sbc	hl, hl
	ld	h, b
	ld	l, c
	ret

; ------------------------------------------------------------------------------------------------- rs_compose
; void rs_compose(uint8_t *dst, const uint8_t *a, const uint8_t *f): dst[i] = f[a[i]], all three 256-byte aligned
b__rs_compose:
	ld	iy, 0
	add	iy, sp
	ld	de, (iy+3)
	ld	hl, (iy+6)
	ld	bc, (iy+9)
.Lcm_loop:
	ld	c, (hl)
	ld	a, (bc)
	ld	(de), a
	inc	l
	inc	e
	ld	c, (hl)
	ld	a, (bc)
	ld	(de), a
	inc	l
	inc	e
	jr	nz, .Lcm_loop
	ret

; ------------------------------------------------------------------------------------------------- rs_lerp
; int rs_lerp(int a, int b, int f): a + floor((b - a) f / 256), a and b 0..255, f 0..256
b__rs_lerp:
	ld	iy, 0
	add	iy, sp
	ld	a, (iy+10)
	or	a, a
	jr	nz, .Llp_b
	ld	a, (iy+6)
	sub	a, (iy+3)
	jr	c, .Llp_neg
	ld	h, a
	ld	l, (iy+9)
	mlt	hl
	ld	a, h
	add	a, (iy+3)
	sbc	hl, hl
	ld	l, a
	ret
.Llp_neg:
	neg
	ld	h, a
	ld	l, (iy+9)
	mlt	hl
	ld	de, 255
	add	hl, de
	ld	a, (iy+3)
	sub	a, h
	sbc	hl, hl
	ld	l, a
	ret
.Llp_b:
	ld	hl, (iy+6)
	ret

; ------------------------------------------------------------------------------------------------- w_neighbours
; void w_neighbours(uint16_t i): the window cell i's neighbours inside it (-Y +Y -Z +Z -X +X) into w_nbr, how many
; into w_nbr_n (world.c; i = y << 10 | z << 5 | x, each 0..31; w_esx.. the window's first and last slots)
b__w_neighbours:
	ld	iy, 0
	add	iy, sp
	ld	l, (iy+3)
	ld	h, (iy+4)
	ld	iy, _w_nbr
	ld	c, 0
	ld	a, h
	rrca
	rrca
	and	a, 0x1F
	ld	b, a			; y
	ld	a, (_w_esy)
	cp	a, b
	jr	z, .Lnb_y1
	ld	a, h
	sub	a, 4
	and	a, 0x7C
	ld	d, a
	ld	a, h
	and	a, 3
	or	a, d
	ld	(iy+1), a
	ld	(iy+0), l
	lea	iy, iy+2
	inc	c
.Lnb_y1:
	ld	a, (_w_eey)
	cp	a, b
	jr	z, .Lnb_z0
	ld	a, h
	add	a, 4
	and	a, 0x7C
	ld	d, a
	ld	a, h
	and	a, 3
	or	a, d
	ld	(iy+1), a
	ld	(iy+0), l
	lea	iy, iy+2
	inc	c
.Lnb_z0:
	ld	a, h
	and	a, 3
	rlca
	rlca
	rlca
	ld	b, a
	ld	a, l
	rlca
	rlca
	rlca
	and	a, 7
	or	a, b
	ld	b, a			; z
	ld	a, (_w_esz)
	cp	a, b
	jr	z, .Lnb_z1
	ld	a, b
	dec	a
	and	a, 31
	call	t_nb_z
	inc	c
.Lnb_z1:
	ld	a, (_w_eez)
	cp	a, b
	jr	z, .Lnb_x0
	ld	a, b
	inc	a
	and	a, 31
	call	t_nb_z
	inc	c
.Lnb_x0:
	ld	a, l
	and	a, 31
	ld	b, a			; x
	ld	a, (_w_esx)
	cp	a, b
	jr	z, .Lnb_x1
	ld	a, b
	dec	a
	and	a, 31
	ld	d, a
	ld	a, l
	and	a, 0xE0
	or	a, d
	ld	(iy+0), a
	ld	(iy+1), h
	lea	iy, iy+2
	inc	c
.Lnb_x1:
	ld	a, (_w_eex)
	cp	a, b
	jr	z, .Lnb_done
	ld	a, b
	inc	a
	and	a, 31
	ld	d, a
	ld	a, l
	and	a, 0xE0
	or	a, d
	ld	(iy+0), a
	ld	(iy+1), h
	inc	c
.Lnb_done:
	ld	a, c
	ld	(_w_nbr_n), a
	ret

; z' in a: lo = (z' & 7) << 5 | x, hi = y << 2 | z' >> 3, to (iy), iy on (b, c, h, l kept)
b_nb_z:
	ld	e, a
	and	a, 7
	rrca
	rrca
	rrca
	ld	d, a
	ld	a, l
	and	a, 31
	or	a, d
	ld	(iy+0), a
	ld	a, e
	rrca
	rrca
	rrca
	and	a, 3
	ld	d, a
	ld	a, h
	and	a, 0x7C
	or	a, d
	ld	(iy+1), a
	lea	iy, iy+2
	ret

; ------------------------------------------------------------------------------------------------- w_expose
; void w_expose(void): a window column's cells with a face to draw, levels xp_k0 to xp_k1 - 1 up from the window's
; bottom (from the cell xp_p, slot xp_ys), into xp_out (4 bytes, bit y & 31, the other levels' kept); a face shows unless what is next to it is opaque, the same block or
; both are liquids; plants and torches always; past the window: below it as if solid, above air; the columns
; around it at xp_ox0.. (bits of xp_sides: 1 -x, 2 +x, 4 -z, 8 +z inside the window) (world.c)
	.equ	M_CROSS, 2
	.equ	M_TORCH, 6
	.equ	BF_OPAQUE, 1
	.equ	BF_LIQUID, 8
b__w_expose:
	push	ix
	ld	ix, (_xp_p)
	ld	a, (_xp_k0)
	ld	(xp_k), a
	ld	hl, (_xp_out)		; (the column's other levels kept)
	ld	de, xp_m
	ld	bc, 4
	ldir
b_Lxp_cell:
.Lxp_cell:
	; (this level's bit cleared first)
	call	t_xp_bit
	cpl
	and	a, (hl)
	ld	(hl), a
	ld	a, (_xp_ys)
	cp	a, 31
	ld	de, 1024
	jr	nz, .Lxp_up
	ld	de, -31 * 1024
.Lxp_up:
	lea	hl, ix+0
	add	hl, de
	ld	(xp_up), hl
	ld	a, (ix+0)
	or	a, a
	jp	z, t_Lxp_next
	ld	(xp_b), a
	ld	de, 0
	ld	e, a
	ld	hl, _blk_model
	add	hl, de
	ld	a, (hl)
	cp	a, M_CROSS
	jp	z, t_Lxp_show
	cp	a, M_TORCH
	jp	z, t_Lxp_show
	ld	a, (xp_k)
	cp	a, 31
	jp	z, t_Lxp_show
	ld	hl, _blk_flags
	add	hl, de
	ld	a, (hl)
	and	a, BF_LIQUID
	ld	(xp_liq), a
	ld	hl, (xp_up)
	call	t_xp_shows
	jr	nz, .Lxp_show
	ld	a, (xp_k)
	or	a, a
	jr	z, .Lxp_sides
	ld	a, (_xp_ys)
	or	a, a
	ld	de, -1024
	jr	nz, .Lxp_dn
	ld	de, 31 * 1024
.Lxp_dn:
	lea	hl, ix+0
	add	hl, de
	call	t_xp_shows
	jr	nz, .Lxp_show
.Lxp_sides:
	ld	a, (_xp_sides)
	bit	2, a
	jr	z, .Lxp_s1
	ld	de, (_xp_oz0)
	lea	hl, ix+0
	add	hl, de
	call	t_xp_shows
	jr	nz, .Lxp_show
.Lxp_s1:
	ld	a, (_xp_sides)
	bit	3, a
	jr	z, .Lxp_s2
	ld	de, (_xp_oz1)
	lea	hl, ix+0
	add	hl, de
	call	t_xp_shows
	jr	nz, .Lxp_show
.Lxp_s2:
	ld	a, (_xp_sides)
	bit	0, a
	jr	z, .Lxp_s3
	ld	de, (_xp_ox0)
	lea	hl, ix+0
	add	hl, de
	call	t_xp_shows
	jr	nz, .Lxp_show
.Lxp_s3:
	ld	a, (_xp_sides)
	bit	1, a
	jr	z, .Lxp_next
	ld	de, (_xp_ox1)
	lea	hl, ix+0
	add	hl, de
	call	t_xp_shows
	jr	z, .Lxp_next
b_Lxp_show:
.Lxp_show:
	call	t_xp_bit
	or	a, (hl)
	ld	(hl), a
b_Lxp_next:
.Lxp_next:
	ld	ix, (xp_up)
	ld	a, (_xp_ys)
	inc	a
	and	a, 31
	ld	(_xp_ys), a
	ld	a, (xp_k)
	inc	a
	ld	(xp_k), a
	ld	hl, _xp_k1
	cp	a, (hl)
	jp	nz, t_Lxp_cell
	ld	hl, xp_m
	ld	de, (_xp_out)
	ld	bc, 4
	ldir
	pop	ix
	ret

; hl: xp_m's byte of slot xp_ys, a: its bit
b_xp_bit:
	ld	a, (_xp_ys)
	ld	c, a
	rrca
	rrca
	rrca
	and	a, 3
	ld	hl, xp_m
	ld	de, 0
	ld	e, a
	add	hl, de
	ld	a, c
	and	a, 7
	ld	b, a
	ld	a, 1
	ret	z
.Lxb_sh:
	add	a, a
	djnz	.Lxb_sh
	ret

; nz when the cell at hl shows xp_b's face next to it
b_xp_shows:
	ld	a, (hl)
	ld	hl, xp_b
	cp	a, (hl)
	ret	z
	ld	de, 0
	ld	e, a
	ld	hl, _blk_flags
	add	hl, de
	ld	a, (hl)
	ld	c, a
	and	a, BF_OPAQUE
	jr	nz, .Lxs_hid
	ld	a, (xp_liq)
	and	a, c
	jr	nz, .Lxs_hid
	or	a, 1
	ret
.Lxs_hid:
	xor	a, a
	ret

; ------------------------------------------------------------------------------------------------- rs_shade
; void rs_shade(uint8_t *p, const uint8_t *row, uint16_t n): p[i] = row[p[i]] for n bytes (1..65535), row
; 256-byte aligned
b__rs_shade:
	ld	iy, 0
	add	iy, sp
	ld	hl, (iy+3)		; p
	ld	de, (iy+6)		; row (256-aligned: e the byte)
	ld	a, (iy+9)		; n: djnz's count the low byte, c the high (one more when the low is not 0)
	ld	c, (iy+10)
	ld	b, a
	or	a, a
	jr	z, .Lsh_loop
	inc	c
.Lsh_loop:
	ld	e, (hl)
	ld	a, (de)
	ld	(hl), a
	inc	hl
	djnz	.Lsh_loop
	dec	c
	jr	nz, .Lsh_loop
	ret

; ------------------------------------------------------------------------------------------------- data

; ------------------------------------------------------------------------------------------------- rs_clip2
; uint8_t rs_clip2(const SV *in, uint8_t n, SV *out, uint8_t k)
; the polygon cut to a side of the screen (k 0: x >= 0, 1: x <= 2560, 2: y >= 0, 3: y <= 1920): its corners into
; out, how many. Where an edge crosses: t = its share before the side, in 4096ths (bit by bit), the other values
; a + (b - a) t (mulhi), the side's own exactly
	.equ	SV_X, 0
	.equ	SV_Y, 3
	.equ	SV_U, 6
	.equ	SV_V, 9
	.equ	SV_SIZE, 12
b__rs_clip2:
	push	ix
	ld	ix, 0
	add	ix, sp
	ld	a, (ix+15)
	ld	(c2_k), a
	ld	hl, (ix+12)
	ld	(c2_out), hl
	ld	hl, (ix+6)
	ld	(c2_in), hl
	ld	(c2_a), hl
	ld	a, (ix+9)
	ld	(c2_left), a
	xor	a, a
	ld	(c2_m), a
	ld	iy, (c2_a)
	call	t_c2_d
	ld	(c2_da), hl
b_Lc2_edge:
.Lc2_edge:
	; b: the next corner (the first after the last)
	ld	hl, (c2_a)
	ld	de, SV_SIZE
	add	hl, de
	ld	a, (c2_left)
	dec	a
	ld	(c2_left), a
	jr	nz, .Lc2_b
	ld	hl, (c2_in)
.Lc2_b:
	ld	(c2_b), hl
	push	hl
	pop	iy
	call	t_c2_d
	ld	(c2_db), hl
	; a inside: kept
	ld	a, (c2_da+2)
	bit	7, a
	jr	nz, .Lc2_aout
	ld	hl, (c2_a)
	call	t_c2_put
.Lc2_aout:
	; a and b on either side: where the edge crosses
	ld	a, (c2_da+2)
	ld	c, a
	ld	a, (c2_db+2)
	xor	a, c
	bit	7, a
	jp	z, t_Lc2_next
	; k = |da - db|, r = |da|: t = r 4096 / k, 12 bits
	ld	hl, (c2_da)
	ld	de, (c2_db)
	or	a, a
	sbc	hl, de
	call	t_c2_abs
	ex	de, hl
	ld	hl, (c2_da)
	call	t_c2_abs
	ld	bc, 0
	ld	a, 12
.Lc2_div:
	add	hl, hl
	sla	c
	rl	b
	or	a, a
	sbc	hl, de
	jr	c, .Lc2_back
	inc	c
	jr	.Lc2_divn
.Lc2_back:
	add	hl, de
.Lc2_divn:
	dec	a
	jr	nz, .Lc2_div
	; t << 12 for mulhi
	push	bc
	pop	hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	(c2_tt), hl
	; each value: a + (b - a) t
	ld	iy, (c2_a)
	ld	ix, (c2_b)
	ld	hl, (ix+SV_X)
	ld	de, (iy+SV_X)
	call	t_c2_lerp
	ld	(c2_o+SV_X), hl
	ld	hl, (ix+SV_Y)
	ld	de, (iy+SV_Y)
	call	t_c2_lerp
	ld	(c2_o+SV_Y), hl
	ld	hl, (ix+SV_U)
	ld	de, (iy+SV_U)
	call	t_c2_lerp
	ld	(c2_o+SV_U), hl
	ld	hl, (ix+SV_V)
	ld	de, (iy+SV_V)
	call	t_c2_lerp
	ld	(c2_o+SV_V), hl
	; the side's own value
	ld	a, (c2_k)
	or	a, a
	sbc	hl, hl
	cp	a, 1
	jr	c, .Lc2_x			; (k 0: x = 0)
	jr	nz, .Lc2_y01
	ld	hl, RW * 16
.Lc2_x:
	ld	(c2_o+SV_X), hl
	jr	.Lc2_put
.Lc2_y01:
	cp	a, 2
	jr	z, .Lc2_y
	ld	hl, 120 * 16
.Lc2_y:
	ld	(c2_o+SV_Y), hl
.Lc2_put:
	ld	hl, c2_o
	call	t_c2_put
b_Lc2_next:
.Lc2_next:
	ld	hl, (c2_b)
	ld	(c2_a), hl
	ld	hl, (c2_db)
	ld	(c2_da), hl
	ld	a, (c2_left)
	or	a, a
	jp	nz, t_Lc2_edge
	ld	a, (c2_m)
	pop	ix
	ret

; hl: how far the corner at iy is inside side c2_k (>= 0 inside)
b_c2_d:
	ld	a, (c2_k)
	cp	a, 2
	jr	nc, .Lc2d_y
	ld	hl, (iy+SV_X)
	or	a, a
	ret	z
	ex	de, hl
	ld	hl, RW * 16
	or	a, a
	sbc	hl, de
	ret
.Lc2d_y:
	ld	hl, (iy+SV_Y)
	ret	z
	ex	de, hl
	ld	hl, 120 * 16
	or	a, a
	sbc	hl, de
	ret

; the corner at hl to c2_out, on
b_c2_put:
	ld	de, (c2_out)
	ld	bc, SV_SIZE
	ldir
	ld	(c2_out), de
	ld	hl, c2_m
	inc	(hl)
	ret

; hl = |hl| (de kept)
b_c2_abs:
	ld	(m_t), hl
	ld	a, (m_t+2)
	bit	7, a
	ret	z
	push	de
	ex	de, hl
	or	a, a
	sbc	hl, hl
	sbc	hl, de
	pop	de
	ret

; hl = de + (hl - de) t (c2_tt: t << 12), as mulhi rounds
b_c2_lerp:
	push	de
	or	a, a
	sbc	hl, de
	ld	de, (c2_tt)
	call	t_mulhi
	pop	de
	add	hl, de
	ret

; ------------------------------------------------------------------------------------------------- rs_face
; uint8_t rs_face(uint8_t f)
; face f of the cell whose corners are in rs_ck (corner k's x, y, z at k x 9 bytes; f 0..5 -Y +Y -Z +Z -X +X, 6 and 7
; a plant's two quads), with the texture rs_setup was given: passed by when all its corners are past one side of the
; view; 1 when it is to be cut by a plane (rs_fclip the planes); else projected into rs_fs: drawn (0) when all on the
; screen, else 2 (to be cut to it)
b__rs_face:
	ld	iy, 0
	add	iy, sp
	ld	l, (iy+3)
	ld	h, 4
	mlt	hl
	ld	de, rs_facec
	add	hl, de
	ld	(fc_p), hl		; its 4 corners' offsets
	xor	a, a
	ld	(fc_clip), a
	dec	a
	ld	(fc_side), a
	ld	b, 4
.Lfc_code:
	push	bc
	call	t_fc_corner		; iy: the corner's x - 3
	call	t_code_iy
	ld	a, (fc_clip)
	or	a, l
	ld	(fc_clip), a
	ld	a, (fc_side)
	and	a, h
	ld	(fc_side), a
	pop	bc
	djnz	.Lfc_code
	ld	a, (fc_side)
	or	a, a
	jr	nz, .Lfc_none
	ld	a, (fc_clip)
	ld	(_rs_fclip), a
	or	a, a
	jr	z, .Lfc_proj
	ld	a, 1
	ret
.Lfc_none:
	xor	a, a
	ret
.Lfc_proj:
	; each corner projected into rs_fs
	ld	hl, (fc_p)
	ld	de, -4
	add	hl, de
	ld	(fc_p), hl
	ld	hl, _rs_fs
	ld	(pj_out), hl
	ld	b, 4
.Lfc_p:
	push	bc
	call	t_fc_corner
	call	t_proj_iy
	ld	hl, (pj_out)
	ld	de, 12
	add	hl, de
	ld	(pj_out), hl
	pop	bc
	djnz	.Lfc_p
	; all on the screen: 0 <= x <= 2560, 0 <= y <= 1920
	ld	iy, _rs_fs
	ld	b, 4
.Lfc_on:
	ld	hl, (iy+0)
	ld	de, RW * 16 + 1
	or	a, a
	sbc	hl, de
	jr	nc, .Lfc_off		; (x < 0 too: as unsigned, past)
	ld	hl, (iy+3)
	ld	de, 120 * 16 + 1
	or	a, a
	sbc	hl, de
	jr	nc, .Lfc_off
	lea	iy, iy+12
	djnz	.Lfc_on
	ld	a, 4
	ld	hl, 0
	ld	l, a
	push	hl
	ld	hl, _rs_fs
	push	hl
	call	t__rs_poly
	pop	hl
	pop	hl
	xor	a, a
	ret
.Lfc_off:
	ld	a, 2
	ret

; iy: the next corner of the face (fc_p) as x - 3, for code_iy and proj_iy
b_fc_corner:
	ld	hl, (fc_p)
	ld	a, (hl)
	inc	hl
	ld	(fc_p), hl
	ld	hl, _rs_ck - 3
	ld	de, 0
	ld	e, a
	add	hl, de
	push	hl
	pop	iy
	ret

; ------------------------------------------------------------------------------------------------- the window's columns
; (world.c's fill: a column's levels a slot at a time, 1024 apart round the 32 slots; the light's queue (w_lq, w_lq_h,
; w_lq_t: LQ_N entries) as world.c keeps it)
	.equ	LQ_N, 5000

; hl += a x 1024 (a 0..31)
b_w_slot:
	add	a, a
	add	a, a
	ld	de, 0
	ld	d, a
	add	hl, de
	ret

; hl (a pointer at slot c) to the next slot's, c on
b_w_next:
	ld	a, c
	inc	a
	and	a, 31
	ld	c, a
	ld	de, 1024
	jr	nz, .Lwn_add
	ld	de, -31 * 1024
.Lwn_add:
	add	hl, de
	ret

; the cell index de (0..32767) onto the light's queue (as lq_push)
b_w_push:
	push	hl
	push	bc
	ld	hl, (_w_lq_t)
	inc	hl
	ld	bc, LQ_N
	or	a, a
	sbc	hl, bc
	jr	z, .Lwp_wrap
	add	hl, bc
	jr	.Lwp_t
.Lwp_wrap:
	or	a, a
	sbc	hl, hl
.Lwp_t:
	ld	bc, (_w_lq_h)
	or	a, a
	sbc	hl, bc
	jr	z, .Lwp_full
	add	hl, bc
	push	hl			; the new tail
	ld	hl, (_w_lq_t)
	add	hl, hl
	ld	bc, (_w_lq)
	add	hl, bc
	ld	(hl), e
	inc	hl
	ld	(hl), d
	pop	hl
	ld	(_w_lq_t), hl
.Lwp_full:
	pop	bc
	pop	hl
	ret

; void w_col_copy(uint8_t *p, uint8_t ys, const uint8_t *cells, uint8_t n): n cells from slot ys of the column
; at p (wblk + its index) on
b__w_col_copy:
	push	ix
	ld	ix, 0
	add	ix, sp
	ld	hl, (ix+6)
	ld	a, (ix+9)
	ld	c, a
	call	t_w_slot
	ld	iy, (ix+12)
	ld	b, (ix+15)
.Lwcc:
	ld	a, (iy+0)
	ld	(hl), a
	inc	iy
	call	t_w_next
	djnz	.Lwcc
	pop	ix
	ret

; void w_col_light(uint16_t c, uint8_t ys, uint8_t y, uint8_t y1, uint8_t top): levels y..y1 - 1 of column c from
; slot ys: the sky's light 15 from top up, the block light of what shines (queued)
b__w_col_light:
	push	ix
	ld	ix, 0
	add	ix, sp
	ld	hl, (ix+6)
	ex	de, hl
	ld	hl, 0
	ld	l, e
	ld	h, d			; c (16 bits)
	ld	a, (ix+9)
	ld	c, a
	call	t_w_slot
	ld	(wc_i), hl		; the cell index
	ld	a, (ix+12)
	ld	(wc_y), a
.Lwcl:
	ld	a, (wc_y)
	cp	a, (ix+15)
	jr	nc, .Lwcl_done
	ld	hl, (wc_i)
	ld	de, (_wblk)
	add	hl, de
	ld	a, (hl)
	ld	hl, _blk_light
	ld	de, 0
	ld	e, a
	add	hl, de
	ld	b, (hl)			; the block light
	ld	a, (wc_y)
	cp	a, (ix+18)
	ld	a, 0
	jr	c, .Lwcl_dark
	ld	a, 15
.Lwcl_dark:
	inc	b
	dec	b
	jr	z, .Lwcl_set
	ld	e, a
	ld	a, b
	add	a, a
	add	a, a
	add	a, a
	add	a, a
	or	a, e
	ld	de, (wc_i)
	call	t_w_push
.Lwcl_set:
	ld	hl, (wc_i)
	ld	de, (_wlight)
	add	hl, de
	ld	(hl), a
	ld	hl, (wc_i)
	call	t_w_next
	ld	(wc_i), hl
	ld	hl, wc_y
	inc	(hl)
	jr	.Lwcl
.Lwcl_done:
	pop	ix
	ret

; void w_col_seed_border(uint16_t c, uint16_t cn, uint8_t ys, uint8_t y, uint8_t y1): column c next to the region's
; column cn: its cells brighter than cn's next to them by more than a step, queued
b__w_col_seed_border:
	push	ix
	ld	ix, 0
	add	ix, sp
	ld	hl, 0
	ld	l, (ix+6)
	ld	h, (ix+7)
	ld	a, (ix+12)
	ld	c, a
	call	t_w_slot
	ld	(wc_i), hl
	ld	hl, 0
	ld	l, (ix+9)
	ld	h, (ix+10)
	or	a, a
	ld	de, 0
	ld	e, (ix+6)
	ld	d, (ix+7)
	sbc	hl, de
	ld	(wc_d), hl		; cn - c
	ld	a, (ix+15)
	ld	(wc_y), a
b_Lwsb:
.Lwsb:
	ld	a, (wc_y)
	cp	a, (ix+18)
	jr	nc, .Lwsb_done
	ld	hl, (wc_i)
	ld	de, (_wlight)
	add	hl, de
	ld	a, (hl)
	or	a, a
	jr	z, .Lwsb_next
	ld	b, a			; l
	ld	de, (wc_d)
	add	hl, de
	ld	c, (hl)			; nl
	; sky: l > nl + 1
	ld	a, c
	and	a, 15
	inc	a
	ld	e, a
	ld	a, b
	and	a, 15
	cp	a, e
	jr	z, .Lwsb_blk
	jr	nc, .Lwsb_lit
.Lwsb_blk:
	; block: nl's < 0xE0 and l's > nl's + 0x10
	ld	a, c
	and	a, 0xF0
	cp	a, 0xE0
	jr	nc, .Lwsb_next
	add	a, 0x10
	ld	e, a
	ld	a, b
	and	a, 0xF0
	cp	a, e
	jr	z, .Lwsb_next
	jr	c, .Lwsb_next
.Lwsb_lit:
	; (and light can go into it)
	ld	hl, (wc_i)
	ld	de, (wc_d)
	add	hl, de
	ld	de, (_wblk)
	add	hl, de
	ld	a, (hl)
	ld	hl, _blk_opacity
	ld	de, 0
	ld	e, a
	add	hl, de
	ld	a, (hl)
	cp	a, 15
	jr	nc, .Lwsb_next
	ld	de, (wc_i)
	call	t_w_push
.Lwsb_next:
	ld	a, (wc_y)
	inc	a
	ld	(wc_y), a
	ld	hl, (wc_i)
	ld	c, (ix+12)
	call	t_w_next
	ld	(ix+12), c
	ld	(wc_i), hl
	jp	t_Lwsb
.Lwsb_done:
	pop	ix
	ret

; void w_col_seed(uint16_t c, uint8_t ys, uint8_t y, uint8_t y1, uint8_t top, uint8_t nt, uint8_t inside): the
; lit cells to spread from (a sky-lit one of the region only next to a column whose sky stops higher, or above
; what lets light through), queued
b__w_col_seed:
	push	ix
	ld	ix, 0
	add	ix, sp
	ld	hl, 0
	ld	l, (ix+6)
	ld	h, (ix+7)
	ld	a, (ix+9)
	ld	c, a
	call	t_w_slot
	ld	(wc_i), hl
	ld	a, (ix+12)
	ld	(wc_y), a
b_Lws:
.Lws:
	ld	a, (wc_y)
	cp	a, (ix+15)
	jp	nc, t_Lws_done
	ld	hl, (wc_i)
	ld	de, (_wlight)
	add	hl, de
	ld	a, (hl)
	or	a, a
	jr	z, .Lws_next
	cp	a, 15
	jr	nz, .Lws_push
	ld	a, (ix+24)		; inside
	or	a, a
	jr	z, .Lws_push
	ld	a, (ix+21)		; nt <= y: none around higher
	ld	b, a
	ld	a, (wc_y)
	cp	a, b
	jr	c, .Lws_push		; (y < nt)
	; y == top and y > the window's bottom and what is under it lets light through: an edge
	cp	a, (ix+18)
	jr	nz, .Lws_next
	ld	hl, _win_y
	cp	a, (hl)
	jr	z, .Lws_next
	jr	c, .Lws_next
	ld	a, (ix+9)		; the slot below
	dec	a
	and	a, 31
	ld	hl, 0
	ld	l, (ix+6)
	ld	h, (ix+7)
	call	t_w_slot
	ld	de, (_wblk)
	add	hl, de
	ld	a, (hl)
	ld	hl, _blk_opacity
	ld	de, 0
	ld	e, a
	add	hl, de
	ld	a, (hl)
	cp	a, 15
	jr	nc, .Lws_next
.Lws_push:
	ld	de, (wc_i)
	call	t_w_push
.Lws_next:
	ld	a, (wc_y)
	inc	a
	ld	(wc_y), a
	ld	hl, (wc_i)
	ld	c, (ix+9)
	call	t_w_next
	ld	(ix+9), c
	ld	(wc_i), hl
	jp	t_Lws
b_Lws_done:
.Lws_done:
	pop	ix
	ret

; void w_light_spread(void): both lights spread from the queued cells (w_edges() first: w_neighbours' edges)
b__w_light_spread:
	push	ix
.Lls:
	ld	hl, (_w_lq_h)
	ld	de, (_w_lq_t)
	or	a, a
	sbc	hl, de
	jp	z, t_Lls_done
	add	hl, de
	push	hl
	add	hl, hl
	ld	de, (_w_lq)
	add	hl, de
	ld	de, 0
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	ld	(ls_i), de
	pop	hl
	inc	hl
	ld	de, LQ_N
	or	a, a
	sbc	hl, de
	jr	z, .Lls_h
	add	hl, de
.Lls_h:
	ld	(_w_lq_h), hl
	; l: s its sky, b its block light (high nibble)
	ld	hl, (ls_i)
	ld	de, (_wlight)
	add	hl, de
	ld	a, (hl)
	ld	b, a
	and	a, 15
	ld	(ls_s), a
	ld	a, b
	and	a, 0xF0
	ld	(ls_b), a
	cp	a, 0x11
	jr	nc, .Lls_go
	ld	a, (ls_s)
	cp	a, 2
	jr	c, .Lls
.Lls_go:
	ld	hl, (ls_i)
	push	hl
	call	t__w_neighbours
	pop	hl
	ld	ix, _w_nbr
	ld	a, (_w_nbr_n)
	ld	(ls_k), a
b_Lls_n:
.Lls_n:
	ld	a, (ls_k)
	or	a, a
	jr	z, .Lls
	dec	a
	ld	(ls_k), a
	ld	de, 0
	ld	e, (ix+0)
	ld	d, (ix+1)
	lea	ix, ix+2
	ld	(ls_n), de
	; op: what it takes, 1 at least; 15: none in
	ld	hl, (_wblk)
	add	hl, de
	ld	a, (hl)
	ld	hl, _blk_opacity
	ld	de, 0
	ld	e, a
	add	hl, de
	ld	a, (hl)
	cp	a, 15
	jr	nc, .Lls_n
	or	a, a
	jr	nz, .Lls_op
	inc	a
.Lls_op:
	ld	c, a			; op
	ld	hl, (ls_n)
	ld	de, (_wlight)
	add	hl, de
	ld	a, (hl)
	ld	b, a			; nl
	ld	(ls_ch), hl		; (where it is)
	xor	a, a
	ld	(ls_d), a		; changed
	; sky: s - op > ns
	ld	a, (ls_s)
	sub	a, c
	jr	c, .Lls_sky
	jr	z, .Lls_sky
	ld	e, a
	ld	a, b
	and	a, 15
	cp	a, e
	jr	nc, .Lls_sky
	ld	a, b
	and	a, 0xF0
	or	a, e
	ld	b, a
	ld	a, 1
	ld	(ls_d), a
.Lls_sky:
	; block: b - op x 16 > nb
	ld	a, c
	add	a, a
	add	a, a
	add	a, a
	add	a, a
	ld	e, a
	ld	a, (ls_b)
	sub	a, e
	jr	c, .Lls_blk
	jr	z, .Lls_blk
	ld	e, a
	ld	a, b
	and	a, 0xF0
	cp	a, e
	jr	nc, .Lls_blk
	ld	a, b
	and	a, 15
	or	a, e
	ld	b, a
	ld	a, 1
	ld	(ls_d), a
.Lls_blk:
	ld	a, (ls_d)
	or	a, a
	jp	z, t_Lls_n
	ld	hl, (ls_ch)
	ld	(hl), b
	ld	de, (ls_n)
	call	t_w_push
	jp	t_Lls_n
b_Lls_done:
.Lls_done:
	pop	ix
	ret

; ------------------------------------------------------------------------------------------------- rs_rle
; void rs_rle(uint8_t *q, const uint8_t *p, int sw): a picture's row of runs (tools/pack.py rle: n < 128, n + 1
; pixels follow; else n - 126 of the next) to q, sw pixels; 0 see-through
b__rs_rle:
	ld	iy, 0
	add	iy, sp
	ld	hl, (iy+9)
	exx
	ld	de, (iy+3)
	ld	hl, (iy+6)
.Lrl_run:
	ld	a, (hl)
	inc	hl
	cp	a, 128
	jr	nc, .Lrl_fill
	inc	a
	ld	b, a
	exx
	ld	de, 0
	ld	e, a
	or	a, a
	sbc	hl, de
	exx
.Lrl_lit:
	ld	a, (hl)
	inc	hl
	or	a, a
	jr	z, .Lrl_skip
	ld	(de), a
.Lrl_skip:
	inc	de
	djnz	.Lrl_lit
	jr	.Lrl_more
.Lrl_fill:
	sub	a, 126
	ld	b, a
	exx
	ld	de, 0
	ld	e, a
	or	a, a
	sbc	hl, de
	exx
	ld	a, (hl)
	inc	hl
	or	a, a
	jr	z, .Lrl_none
.Lrl_f:
	ld	(de), a
	inc	de
	djnz	.Lrl_f
	jr	.Lrl_more
.Lrl_none:
	push	hl
	ex	de, hl
	ld	de, 0
	ld	e, b
	add	hl, de
	ex	de, hl
	pop	hl
.Lrl_more:
	exx
	ld	a, h
	or	a, l
	jr	z, .Lrl_done
	bit	7, h
	jr	nz, .Lrl_done
	exx
	jr	.Lrl_run
.Lrl_done:
	ret

; ------------------------------------------------------------------------------------------------- the HUD's rows
; (rs_expand: a screen row under the HUD written once, from the HUD layer's runs (rs_hruns: pairs see-through,
; drawn, each row's adding up to HUD_W) and the world's row doubled; r_odd: the column at the second half of hl's
; pixel; r_sky the row's sky)

; a screen row of the HUD's: the world's row (hl, in rbuf) doubled where the HUD (r_hud, its runs at r_hrun) lets
; it through, the HUD's pixels elsewhere; de the screen row, after it at the end; r_hud and r_hrun on to the next
b_r_hudrow:
	xor	a, a
	ld	(r_odd), a
	ld	a, HUD_X
	call	t_r_cols
	ld	iy, (r_hrun)
	ld	a, HUD_W
	ld	(r_left), a
.Lhr_run:
	ld	a, (iy+0)
	call	t_r_cols		; the see-through: the world
	push	hl
	ld	hl, (r_hud)
	ld	bc, 0
	ld	c, (iy+0)
	add	hl, bc
	ld	c, (iy+1)		; the HUD's own
	inc	c
	dec	c
	jr	z, .Lhr_none
	ldir
.Lhr_none:
	ld	(r_hud), hl
	pop	hl
	ld	a, (iy+1)
	call	t_r_skip		; (the world under them passed by)
	ld	a, (r_left)
	sub	a, (iy+0)
	sub	a, (iy+1)
	ld	(r_left), a
	lea	iy, iy+2
	jr	nz, .Lhr_run
	ld	(r_hrun), iy
	ld	a, 320 - HUD_X - HUD_W
	jp	t_r_cols

; a columns of the world (0..255) to de, doubled from hl (0: the sky); hl and de after them
b_r_cols:
	or	a, a
	ret	z
	ld	b, a
	ld	a, (r_sky)
	ld	c, a
	ld	a, (r_odd)
	or	a, a
	jr	z, .Lrc_even
	xor	a, a
	ld	(r_odd), a
	ld	a, (hl)			; the second half of hl's pixel
	inc	hl
	or	a, a
	jr	nz, .Lrc_a
	ld	a, c
.Lrc_a:
	ld	(de), a
	inc	de
	dec	b
	ret	z
.Lrc_even:
	srl	b			; whole pixels; carry: the first half of one after them
	jr	nc, .Lrc_whole
	ld	a, 1
	ld	(r_odd), a
	inc	b
	dec	b
	call	nz, t_r_px
	ld	a, (hl)			; (hl stays on that pixel)
	or	a, a
	jr	nz, .Lrc_b
	ld	a, c
.Lrc_b:
	ld	(de), a
	inc	de
	ret
.Lrc_whole:
	inc	b
	dec	b
	ret	z
	jp	t_r_px

; b pixels from hl doubled to de (0: c, the sky)
b_r_px:
	ld	a, (hl)
	inc	hl
	or	a, a
	jr	nz, .Lrp_a
	ld	a, c
.Lrp_a:
	ld	(de), a
	inc	de
	ld	(de), a
	inc	de
	djnz	b_r_px
	ret

; a columns of the world passed by (hl on, r_odd), none written
b_r_skip:
	ld	b, a
	ld	a, (r_odd)
	add	a, b			; columns from the start of hl's pixel
	srl	a			; whole pixels; carry: half into the next
	ld	b, a
	ld	a, 0
	rla
	ld	(r_odd), a
	push	de
	ld	de, 0
	ld	e, b
	add	hl, de
	pop	de
	ret

; uint8_t *rs_runs_of(const uint8_t *h, uint8_t *o, uint8_t rows): the HUD layer's rows (h, HUD_W each) as runs
; into o: pairs (see-through, drawn), each row's adding up to HUD_W; after them (render.c makes sure they fit)
b__rs_runs_of:
	ld	iy, 0
	add	iy, sp
	ld	hl, (iy+3)
	ld	de, (iy+6)
	ld	a, (iy+9)
	ld	(r_left), a		; rows left
.Lru_row:
	ld	b, HUD_W		; pixels left in the row
.Lru_pair:
	ld	c, 0
.Lru_t:
	ld	a, (hl)
	or	a, a
	jr	nz, .Lru_tend
	inc	hl
	inc	c
	djnz	.Lru_t
	; (the row ends see-through)
	ld	a, c
	ld	(de), a
	inc	de
	xor	a, a
	ld	(de), a
	inc	de
	jr	.Lru_next
.Lru_tend:
	ld	a, c
	ld	(de), a
	inc	de
	ld	c, 0
.Lru_n:
	ld	a, (hl)
	or	a, a
	jr	z, .Lru_nend
	inc	hl
	inc	c
	djnz	.Lru_n
	ld	a, c
	ld	(de), a
	inc	de
	jr	.Lru_next
.Lru_nend:
	ld	a, c
	ld	(de), a
	inc	de
	jr	.Lru_pair
.Lru_next:
	ld	a, (r_left)
	dec	a
	ld	(r_left), a
	jr	nz, .Lru_row
	ex	de, hl
	ret
