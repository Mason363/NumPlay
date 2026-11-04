/* src/sf.c's float add, multiply and comparisons in eZ80 assembly (the C does them with a call for every 32-bit
 * operation: 11000 and 19000 cycles each). The same results bit for bit: IEEE single, round to nearest even, no
 * denormals (taken as 0, results too small give 0), no NaN. A float argument takes two stack slots: its low 24 bits,
 * then its top byte; a float comes back in E:HL. A significand is worked on a byte at a time, 32 bits: its 24 bits
 * and 8 below them (the last, bit 0, ORed with all that was shifted out). */
	.assume	adl=1

	.section	.bss
sf_exp:	.skip	3			/* the result's exponent + 128 (24 bits) */
sf_sign:
	.skip	1
sf_prod:
	.skip	8			/* the product's 6 bytes (and 2 zero ones read past them) */

	.section	.text

/* the end of both: H L D E the significand (bit 31 set), sf_exp, sf_sign; rounds, packs, returns E:HL */
sf_round:
	ld	a, e
	cp	a, $80
	jr	c, .Lr_pack		; below half: down
	jr	nz, .Lr_up		; above: up
	bit	0, d			; half: to even
	jr	z, .Lr_pack
.Lr_up:
	inc	d
	jr	nz, .Lr_pack
	inc	l
	jr	nz, .Lr_pack
	inc	h
	jr	nz, .Lr_pack
	ld	h, $80			; (it carried out: 1.0, a power up)
	push	hl
	ld	hl, (sf_exp)
	inc	hl
	ld	(sf_exp), hl
	pop	hl
.Lr_pack:
	push	hl
	ld	hl, (sf_exp)
	ld	bc, 129
	or	a, a
	sbc	hl, bc
	jr	c, .Lr_zero		; exponent <= 0
	ld	hl, (sf_exp)
	ld	bc, 383
	or	a, a
	sbc	hl, bc
	jr	nc, .Lr_inf		; exponent >= 255
	pop	hl
	ld	a, (sf_exp)
	add	a, $80			; the exponent's 8 bits
	ld	c, a
	ld	a, h
	add	a, a			; (the hidden bit out)
	srl	c			; exponent >> 1, its bit 0 in carry
	rra				; exponent's bit 0, then the significand's top 7
	ld	(sf_prod), a		; (bits 16-23)
	ld	a, (sf_sign)
	or	a, c
	ld	e, a			; sign, exponent's top 7
	ld	a, (sf_prod)
	ld	(sf_prod + 2), a
	ld	a, l
	ld	(sf_prod + 1), a
	ld	a, d
	ld	(sf_prod), a
	ld	hl, (sf_prod)
	ret
.Lr_zero:
	pop	hl
	ld	a, (sf_sign)
	ld	e, a
	ld	hl, 0
	ret
.Lr_inf:
	pop	hl
	ld	a, (sf_sign)
	or	a, $7F
	ld	e, a
	ld	hl, $800000
	ret

/* f32 f_add(f32 a, f32 b) */
	.global	_f_add
	.type	_f_add, @function
_f_add:
	ld	iy, 0
	add	iy, sp			; a: (iy + 3) low, (iy + 6) top; b: (iy + 9), (iy + 12)
	ld	de, 0
	ld	hl, (iy + 3)
	or	a, a
	sbc	hl, de
	jr	nz, .La_nz
	ld	a, (iy + 6)
	and	a, $7F
	jr	nz, .La_nz
	ld	hl, (iy + 9)		; a is 0: b, unless it is 0 too (then a & b: -0 only for -0 + -0)
	or	a, a
	sbc	hl, de
	jr	nz, .Lret_b
	ld	a, (iy + 12)
	and	a, $7F
	jr	nz, .Lret_b
	ld	a, (iy + 6)
	and	a, (iy + 12)
	ld	e, a
	ld	hl, 0
	ret
.Lret_b:
	ld	hl, (iy + 9)
	ld	e, (iy + 12)
	ret
.La_nz:
	ld	hl, (iy + 9)
	or	a, a
	sbc	hl, de
	jr	nz, .Lb_nz
	ld	a, (iy + 12)
	and	a, $7F
	jr	nz, .Lb_nz
.Lret_a:
	ld	hl, (iy + 3)
	ld	e, (iy + 6)
	ret
.Lb_nz:
	ld	a, (iy + 5)
	rla
	ld	a, (iy + 6)
	rla
	ld	c, a			; a's exponent
	ld	a, (iy + 11)
	rla
	ld	a, (iy + 12)
	rla
	ld	b, a			; b's
	ld	a, c
	or	a, a
	jr	z, .Lret_b		; (a denormal: as 0)
	ld	a, b
	or	a, a
	jr	z, .Lret_a
	/* the bigger one first */
	ld	a, (iy + 6)
	and	a, $7F
	ld	d, a
	ld	a, (iy + 12)
	and	a, $7F
	cp	a, d
	jr	c, .Lordered		; b's top smaller
	jr	nz, .Lswap
	ld	hl, (iy + 3)
	ld	de, (iy + 9)
	or	a, a
	sbc	hl, de
	jr	nc, .Lordered		; a's low as big or bigger
.Lswap:
	ld	hl, (iy + 3)
	ld	de, (iy + 9)
	ld	(iy + 3), de
	ld	(iy + 9), hl
	ld	a, (iy + 6)
	ld	d, (iy + 12)
	ld	(iy + 12), a
	ld	(iy + 6), d
	ld	a, c
	ld	c, b
	ld	b, a
.Lordered:
	ld	a, (iy + 6)
	and	a, $80
	ld	(sf_sign), a
	ld	hl, 0
	ld	l, c
	ld	de, 128
	add	hl, de
	ld	(sf_exp), hl		; (the exponent + 128)
	ld	a, c
	sub	a, b			; d: how far apart
	ld	b, 0			; (sticky)
	ld	h, (iy + 11)		; the small one's significand: h l d e
	set	7, h
	ld	l, (iy + 10)
	ld	d, (iy + 9)
	ld	e, 0
	cp	a, 32
	jr	c, .Lalign
	ld	hl, 0
	ld	de, 1			; (only its sticky bit)
	jr	.Laligned
.Lalign:
	cp	a, 8
	jr	c, .Lbits
	ld	c, a
	ld	a, e
	or	a, b
	ld	b, a
	ld	e, d
	ld	d, l
	ld	l, h
	ld	h, 0
	ld	a, c
	sub	a, 8
	jr	.Lalign
.Lbits:
	or	a, a
	jr	z, .Lsticky
.Lbit:
	srl	h
	rr	l
	rr	d
	rr	e
	jr	nc, .Lbit0
	set	0, b
.Lbit0:
	dec	a
	jr	nz, .Lbit
.Lsticky:
	ld	a, b
	or	a, a
	jr	z, .Laligned
	set	0, e
.Laligned:
	ld	a, (iy + 5)
	or	a, $80
	ld	b, a			; the big one's top byte
	ld	a, (iy + 6)
	xor	a, (iy + 12)
	jp	m, .Lsub
	/* the same sign: add (0, then its three bytes) */
	ld	a, d
	add	a, (iy + 3)
	ld	d, a
	ld	a, l
	adc	a, (iy + 4)
	ld	l, a
	ld	a, h
	adc	a, b
	ld	h, a
	jp	nc, sf_round
	rr	h			; (a bit over: back one, the one out kept as sticky)
	rr	l
	rr	d
	rr	e
	jr	nc, .Lcarry0
	set	0, e
.Lcarry0:
	push	hl
	ld	hl, (sf_exp)
	inc	hl
	ld	(sf_exp), hl
	pop	hl
	jp	sf_round
.Lsub:
	xor	a, a
	sub	a, e
	ld	e, a
	ld	a, (iy + 3)
	sbc	a, d
	ld	d, a
	ld	a, (iy + 4)
	sbc	a, l
	ld	l, a
	ld	a, b
	sbc	a, h
	ld	h, a
	or	a, l
	or	a, d
	or	a, e
	jr	nz, .Lnorm
	ld	e, 0			; equal: +0
	ld	hl, 0
	ret
.Lnorm:
	ld	bc, 0			; the shifts left
.Lnorm_byte:
	ld	a, h
	or	a, a
	jr	nz, .Lnorm_bit
	ld	h, l
	ld	l, d
	ld	d, e
	ld	e, 0
	ld	a, c
	add	a, 8
	ld	c, a
	jr	.Lnorm_byte
.Lnorm_bit:
	bit	7, h
	jr	nz, .Lnormed
	sla	e
	rl	d
	rl	l
	rl	h
	inc	c
	jr	.Lnorm_bit
.Lnormed:
	push	hl
	ld	hl, (sf_exp)
	or	a, a
	sbc	hl, bc
	ld	(sf_exp), hl
	pop	hl
	jp	sf_round

/* f32 f_mul(f32 a, f32 b): the 48-bit product of the significands a byte at a time (mlt), its columns added up */
	.global	_f_mul
	.type	_f_mul, @function
_f_mul:
	ld	iy, 0
	add	iy, sp
	ld	a, (iy + 6)
	xor	a, (iy + 12)
	and	a, $80
	ld	(sf_sign), a
	ld	a, (iy + 5)
	rla
	ld	a, (iy + 6)
	rla
	or	a, a
	jp	z, .Lm_zero		; (0, or a denormal)
	ld	c, a
	ld	a, (iy + 11)
	rla
	ld	a, (iy + 12)
	rla
	or	a, a
	jp	z, .Lm_zero
	ld	hl, 0			; the exponent: ea + eb - 127 (+ 128)
	ld	l, a
	ld	de, 0
	ld	e, c
	add	hl, de
	inc	hl
	ld	(sf_exp), hl
	set	7, (iy + 5)		; (the hidden bits)
	set	7, (iy + 11)
	/* columns: k = i + j of a[i] * b[j], each added to what carried from the one before */
	ld	de, 0
	ld	bc, 0
	ld	b, (iy + 3)
	ld	c, (iy + 9)
	mlt	bc
	ld	hl, 0
	add	hl, bc			; column 0
	ld	a, l
	ld	(sf_prod), a
	ld	(sf_prod + 3), hl
	ld	hl, (sf_prod + 4)	; (>> 8: sf_prod + 6 is 0)
	ld	bc, 0
	ld	b, (iy + 3)
	ld	c, (iy + 10)
	mlt	bc
	add	hl, bc
	ld	bc, 0
	ld	b, (iy + 4)
	ld	c, (iy + 9)
	mlt	bc
	add	hl, bc			; column 1
	ld	a, l
	ld	(sf_prod + 1), a
	ld	(sf_prod + 3), hl
	ld	hl, (sf_prod + 4)
	ld	bc, 0
	ld	b, (iy + 3)
	ld	c, (iy + 11)
	mlt	bc
	add	hl, bc
	ld	bc, 0
	ld	b, (iy + 4)
	ld	c, (iy + 10)
	mlt	bc
	add	hl, bc
	ld	bc, 0
	ld	b, (iy + 5)
	ld	c, (iy + 9)
	mlt	bc
	add	hl, bc			; column 2
	ld	a, l
	ld	(sf_prod + 2), a
	ld	(sf_prod + 3), hl
	ld	hl, (sf_prod + 4)
	ld	bc, 0
	ld	b, (iy + 4)
	ld	c, (iy + 11)
	mlt	bc
	add	hl, bc
	ld	bc, 0
	ld	b, (iy + 5)
	ld	c, (iy + 10)
	mlt	bc
	add	hl, bc			; column 3
	ld	d, l			; (byte 3)
	ld	(sf_prod + 3), hl
	ld	hl, (sf_prod + 4)
	ld	bc, 0
	ld	b, (iy + 5)
	ld	c, (iy + 11)
	mlt	bc
	add	hl, bc			; column 4, and what is over it: bytes 4 and 5
	ld	a, (sf_prod + 1)
	ld	c, a
	ld	a, (sf_prod)
	or	a, c
	ld	c, a			; (bytes 0 and 1 for the sticky)
	ld	a, (sf_prod + 2)
	ld	e, a			; h l d e: bytes 5 4 3 2
	bit	7, h
	jr	nz, .Lm_top
	ld	a, (sf_prod + 1)	; 47 bits: up one, byte 1's top bit in
	rla
	rl	e
	rl	d
	rl	l
	rl	h
	ld	a, (sf_prod + 1)
	and	a, $7F
	ld	b, a
	ld	a, (sf_prod)
	or	a, b
	jr	.Lm_sticky
.Lm_top:
	push	hl			; 48 bits: a power up
	ld	hl, (sf_exp)
	inc	hl
	ld	(sf_exp), hl
	pop	hl
	ld	a, c
.Lm_sticky:
	or	a, a
	jp	z, sf_round
	set	0, e
	jp	sf_round
.Lm_zero:
	ld	a, (sf_sign)
	ld	e, a
	ld	hl, 0
	ret

/* bool f_lt(f32 a, f32 b), f_le: in order, -0 the same as 0 */
	.global	_f_lt
	.type	_f_lt, @function
_f_lt:
	call	sf_cmp			; a: -1, 0, 1 as a < b, a = b, a > b
	rla				; (carry: negative)
	ld	a, 0
	rla
	ret
	.global	_f_le
	.type	_f_le, @function
_f_le:
	call	sf_cmp
	dec	a			; (-2 or -1 when a <= b)
	rla
	ld	a, 0
	rla
	ret
/* a against b (the caller's arguments, past its return address): a = -1, 0 or 1 */
sf_cmp:
	ld	iy, 0
	add	iy, sp			; a: (iy + 6) low, (iy + 9) top; b: (iy + 12), (iy + 15)
	ld	a, (iy + 9)
	and	a, $7F
	ld	d, a
	ld	a, (iy + 15)
	and	a, $7F
	ld	e, a
	/* |a| against |b|: c = -1, 0, 1 */
	ld	c, 0
	ld	a, e
	cp	a, d
	jr	c, .Lc_agt		; |b| top < |a| top
	jr	nz, .Lc_alt
	ld	hl, (iy + 6)
	ld	bc, (iy + 12)
	or	a, a
	sbc	hl, bc
	ld	c, 0
	jr	z, .Lc_mag
	jr	nc, .Lc_agt
.Lc_alt:
	ld	c, 255
	jr	.Lc_mag
.Lc_agt:
	ld	c, 1
.Lc_mag:
	/* both zero: equal */
	ld	hl, (iy + 6)
	ld	a, d
	ld	b, 0
	or	a, a
	jr	nz, .Lc_nz
	push	bc
	ld	bc, 0
	or	a, a
	sbc	hl, bc
	pop	bc
	jr	nz, .Lc_nz
	ld	hl, (iy + 12)
	ld	a, e
	or	a, a
	jr	nz, .Lc_nz
	push	bc
	ld	bc, 0
	or	a, a
	sbc	hl, bc
	pop	bc
	jr	nz, .Lc_nz
	xor	a, a
	ret
.Lc_nz:
	ld	a, (iy + 9)
	xor	a, (iy + 15)
	jp	p, .Lc_same
	ld	a, (iy + 9)		; signs differ: the negative one is less
	or	a, a
	ld	a, 255
	ret	m
	ld	a, 1
	ret
.Lc_same:
	ld	a, (iy + 9)
	or	a, a
	ld	a, c
	ret	p			; both positive: as their sizes
	neg				; both negative: the other way
	ret
