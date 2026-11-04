; The archive's copy of the 3D view's inner loops (src/rsb.inc), after NBCETEX's tables: on the CE, code read through
; the flash's cache costs 2 or 3 cycles a byte, from RAM 4. Built after the program, its RAM addresses from the
; program's map (rsf_syms.inc, tools/rsfsyms.py); the program runs it when its build matches (rs_flash_init).
	.assume	adl=1
	.include	"src/rsb_list.inc"
	.include	"rsf_syms.inc"

	.section	.text
rsf_start:
	.ascii	"NBRS"
	.d24	RSF_BUILD
	.set	nroutines, 0
	.macro	rsf_count n
	.set	nroutines, nroutines + 1
	.endm
	rsb_each rsf_count
	.db	nroutines
	; each routine's offset, in the trampolines' order
	.macro	rsf_off n
	.d24	b_\n - rsf_start
	.endm
	rsb_each rsf_off

	.include	"src/rsb.inc"
