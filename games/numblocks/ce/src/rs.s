; The 3D view's inner loops on the CE (rs.h; rs.c does the same in C). eZ80, ADL mode.
; (Cycles: a byte read from RAM 4, written 2; code is read too, 4 a byte.)

	.assume	adl=1


; ------------------------------------------------------------------------------------------------- rs_setup
; void rs_setup(const uint8_t *tex, const uint8_t *cm, uint8_t alpha)
; ------------------------------------------------------------------------------------------------- the routines
	.section	.text._rs_body
	.include	"src/rsb.inc"

; ------------------------------------------------------------------------------------------------- trampolines
; each routine's: jp to the program's copy, or the archive's when it is there and of this build (rs_flash_init)
	.section	.data._rs_tramps
	.global	_rs_tramps
	.global	_rs_tramps_ram
	.macro	tramp n
	.global	t_\n
t_\n:	jp	b_\n
	.endm
	.macro	ramaddr n
	.d24	b_\n
	.endm
	.include	"src/rsb_list.inc"
_rs_tramps:
	rsb_each tramp
_rs_tramps_end:
	.global	_rs_ntramps
_rs_ntramps:	.db	(_rs_tramps_end - _rs_tramps) / 4
_rs_tramps_ram:
	rsb_each ramaddr
	.global	_rs_build
_rs_build:	.d24	RSF_BUILD
	.global	_rs_setup
	.set	_rs_setup, t__rs_setup
	.global	_rs_poly
	.set	_rs_poly, t__rs_poly
	.global	_rs_proj
	.set	_rs_proj, t__rs_proj
	.global	_rs_hidden
	.set	_rs_hidden, t__rs_hidden
	.global	_rs_expand
	.set	_rs_expand, t__rs_expand
	.global	_rs_cells
	.set	_rs_cells, t__rs_cells
	.global	_rs_column
	.set	_rs_column, t__rs_column
	.global	_rs_quad
	.set	_rs_quad, t__rs_quad
	.global	_rs_code
	.set	_rs_code, t__rs_code
	.global	_rs_compose
	.set	_rs_compose, t__rs_compose
	.global	_rs_lerp
	.set	_rs_lerp, t__rs_lerp
	.global	_w_neighbours
	.set	_w_neighbours, t__w_neighbours
	.global	_w_expose
	.set	_w_expose, t__w_expose
	.global	_rs_shade
	.set	_rs_shade, t__rs_shade
	.global	_rs_rle
	.set	_rs_rle, t__rs_rle
	.global	_rs_runs_of
	.set	_rs_runs_of, t__rs_runs_of
	.global	_rs_clip2
	.set	_rs_clip2, t__rs_clip2
	.global	_rs_face
	.set	_rs_face, t__rs_face
	.global	_w_col_copy
	.set	_w_col_copy, t__w_col_copy
	.global	_w_col_light
	.set	_w_col_light, t__w_col_light
	.global	_w_col_seed_border
	.set	_w_col_seed_border, t__w_col_seed_border
	.global	_w_col_seed
	.set	_w_col_seed, t__w_col_seed
	.global	_w_light_spread
	.set	_w_light_spread, t__w_light_spread

	.section	.data._rs_vars
	.global	r_tex
r_tex:	.d24	0
	.global	r_cm
r_cm:	.d24	0
	.global	r_loop
r_loop:	.d24	0
	.global	r_u
r_u:	.d24	0
	.global	r_v
r_v:	.d24	0
	.global	r_du
r_du:	.d24	0
	.global	r_dv
r_dv:	.d24	0
	.global	r_end
r_end:	.db	0
	.global	r_row
r_row:	.d24	0
	.global	r_y
r_y:	.db	0
	.global	r_yend
r_yend:	.db	0
	.global	r_px0
r_px0:	.db	0
	.global	r_n
r_n:	.db	0
	.global	m_a
m_a:	.d24	0
	.global	m_r
m_r:	.d24	0
	.global	m_f
m_f:	.d24	0
	.global	m_neg
m_neg:	.db	0
	.global	m_t
m_t:	.d24	0
	.db	0
	.global	p_base
p_base:	.d24	0
	.global	p_n
p_n:	.db	0
	.global	e_b
e_b:	.d24	0
	.global	e_yb
e_yb:	.d24	0
	.global	e_r1
e_r1:	.db	0
	.global	e_f
e_f:	.db	0
	.global	e_q
e_q:	.db	0
	.global	e_d
e_d:	.d24	0
	.global	e_dx
e_dx:	.d24	0
	.global	e_du
e_du:	.d24	0
	.global	e_dv
e_dv:	.d24	0
	.global	e_r
e_r:	.d24	0
	.global	e_ymin
e_ymin:	.d24	0
	.global	e_ymax
e_ymax:	.d24	0
	.global	e_top
e_top:	.db	0
	.global	w_l
w_l:	.d24	0
	.global	w_r
w_r:	.d24	0
	.global	wa
wa:	.ds	W_SIZE
	.global	wb
wb:	.ds	W_SIZE
	.global	r_hud
r_hud:	.d24	0
	.global	r_rows
r_rows:	.db	0
	.global	r_hudlim
r_hudlim:	.db	0
	.global	r_srow
r_srow:	.d24	0
	.global	r_hrun
r_hrun:	.d24	0
	.global	r_odd
r_odd:	.db	0
	.global	r_sky
r_sky:	.db	0
	.global	r_left
r_left:	.db	0
	.global	r_line
r_line:	.ds	320
	.global	r_line2
r_line2:	.ds	320
	.global	cl_mask
cl_mask:	.ds	8
	.global	r_lock
r_lock:	.db	0
	.global	r_ku
r_ku:	.ds	48
	.global	r_kv
r_kv:	.ds	48
	.global	kt_base
kt_base:	.d24	0
	.global	_rs_ck
_rs_ck:	.ds	72
	.global	wc_i
wc_i:	.d24	0
	.global	wc_d
wc_d:	.d24	0
	.global	wc_y
wc_y:	.db	0
	.global	ls_i
ls_i:	.d24	0
	.global	ls_n
ls_n:	.d24	0
	.global	ls_ch
ls_ch:	.d24	0
	.global	ls_s
ls_s:	.db	0
	.global	ls_b
ls_b:	.db	0
	.global	ls_k
ls_k:	.db	0
	.global	ls_d
ls_d:	.db	0
	.global	_rs_fs
_rs_fs:	.d24	0, 0, 0, 0, 0, 0, 65535, 0, 0, 0, 65535, 65535, 0, 0, 0, 65535
	.global	_rs_fclip
_rs_fclip:	.db	0
	.global	fc_p
fc_p:	.d24	0
	.global	fc_clip
fc_clip:	.db	0
	.global	fc_side
fc_side:	.db	0
	.global	pj_out
pj_out:	.d24	0
	.global	rs_facec
rs_facec:	.db	36, 45, 9, 0, 18, 27, 63, 54, 27, 18, 0, 9, 54, 63, 45, 36, 18, 54, 36, 0, 63, 27, 9, 45, 18, 63, 45, 0, 54, 27, 9, 36
	.global	c2_k
c2_k:	.db	0
	.global	c2_left
c2_left:	.db	0
	.global	c2_m
c2_m:	.db	0
	.global	c2_in
c2_in:	.d24	0
	.global	c2_out
c2_out:	.d24	0
	.global	c2_a
c2_a:	.d24	0
	.global	c2_b
c2_b:	.d24	0
	.global	c2_da
c2_da:	.d24	0
	.global	c2_db
c2_db:	.d24	0
	.global	c2_tt
c2_tt:	.d24	0
	.global	c2_o
c2_o:	.ds	12
	.global	h_r
h_r:	.d24	0
	.global	h_c
h_c:	.d24	0
	.global	h_y0
h_y0:	.db	0
	.global	h_rows
h_rows:	.db	0
	.global	h_w
h_w:	.db	0
	.global	rc_t
rc_t:	.d24	0
	.global	c_h2
c_h2:	.d24	0
	.global	c_Y0
c_Y0:	.d24	0
	.global	c_Z0
c_Z0:	.d24	0
	.global	c_V0
c_V0:	.d24	0
	.global	c_cx
c_cx:	.d24	0
	.global	c_xa
c_xa:	.d24	0
	.global	c_xb
c_xb:	.d24	0
	.global	c_ptr
c_ptr:	.d24	0
	.global	c_d2
c_d2:	.d24	0
	.global	c_Y
c_Y:	.d24	0
	.global	c_cy
c_cy:	.d24	0
	.global	c_Z
c_Z:	.d24	0
	.global	c_cz
c_cz:	.d24	0
	.global	c_vz
c_vz:	.d24	0
	.global	c_left
c_left:	.db	0
	.global	c_lj
c_lj:	.db	0
	.global	c_o
c_o:	.db	0
	.global	c_fog
c_fog:	.db	0
	.global	c_list
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
	.global	_rs_flash
_rs_flash:	.d24	0
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
	.global	q_X
q_X:	.d24	0
	.global	q_Y
q_Y:	.d24	0
	.global	q_Z
q_Z:	.d24	0
	.global	q_V
q_V:	.d24	0
	.global	q_x
q_x:	.d24	0
	.global	q_z
q_z:	.d24	0
	.global	q_hi
q_hi:	.d24	0
	.global	q_h2
q_h2:	.d24	0
	.global	q_cx
q_cx:	.d24	0
	.global	q_a
q_a:	.db	0
	.global	q_c
q_c:	.db	0
	.global	q_xs
q_xs:	.db	0
	.global	q_zs
q_zs:	.db	0
	.global	_xp_p
	.global	_xp_out
	.global	_xp_ox0
	.global	_xp_ox1
	.global	_xp_oz0
	.global	_xp_oz1
	.global	_xp_ys
	.global	_xp_sides
_xp_p:	.d24	0
_xp_out:	.d24	0
_xp_ox0:	.d24	0
_xp_ox1:	.d24	0
_xp_oz0:	.d24	0
_xp_oz1:	.d24	0
_xp_ys:	.db	0
_xp_sides:	.db	0
	.global	_xp_k0
	.global	_xp_k1
_xp_k0:	.db	0
_xp_k1:	.db	32
	.global	xp_up
xp_up:	.d24	0
	.global	xp_k
xp_k:	.db	0
	.global	xp_b
xp_b:	.db	0
	.global	xp_liq
xp_liq:	.db	0
	.global	xp_m
xp_m:	.ds	4

	.section	.rodata._rs_rtab
; 1371 << 12 / (256 + i)
	.global	r_rtab
r_rtab:
	.irp	i, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127,128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255
	.dw	(1371 * 4096) / (256 + \i)
	.endr

	.extern	_rbuf
	.extern	_rs_cell
	.extern	_plat_poll
	.extern	_wexp
	.extern	_rs_hruns
	.extern	_rs_hruns_ok
	.extern	_wblk
	.extern	_wlight
	.extern	_blk_light
	.extern	_blk_opacity
	.extern	_win_y
	.extern	_w_lq
	.extern	_w_lq_h
	.extern	_w_lq_t
	.extern	_w_nbr
	.extern	_blk_model
	.extern	_blk_flags
	.extern	_w_nbr_n
	.extern	_w_esx
	.extern	_w_eex
	.extern	_w_esz
	.extern	_w_eez
	.extern	_w_esy
	.extern	_w_eey
	.extern	_rs_recip
	.extern	_rs_ratio

	.section	.note.GNU-stack,"",@progbits
