; Sound cartridge test ROM: programs every sound pak at once, then loops.
	org	$8000
start	orcc	#$50
	lds	#$3f00
	lda	#$8c
	sta	$ff90		; CoCo-compatible, 16K internal / 16K cartridge ROM map
	clr	$ff7f		; Multi-Pak: CTS and SCS both slot 1
; --- Stereo Composer PIA at $FF70: both ports all outputs
	clr	$ff71
	clr	$ff73
	lda	#$ff
	sta	$ff70		; DDRA
	sta	$ff72		; DDRB
	lda	#$04
	sta	$ff71
	sta	$ff73
; --- Symphony 12 PIA at $FF60: A = data, B = chip control
	clr	$ff61
	clr	$ff63
	lda	#$ff
	sta	$ff60
	sta	$ff62
	lda	#$04
	sta	$ff61
	sta	$ff63
	clr	$ff62		; bus idle
	ldx	#ayregs
s12	lda	,x+
	cmpa	#$ff
	beq	s12done
	sta	$ff60		; register number on the data bus
	ldb	#$03
	stb	$ff62		; chip 1: latch address
	clr	$ff62
	lda	,x+
	sta	$ff60		; value
	ldb	#$02
	stb	$ff62		; chip 1: write data
	clr	$ff62
	bra	s12
s12done
; --- CoCo PSG: same tone on the YM2149
	ldx	#ayregs
psg	lda	,x+
	cmpa	#$ff
	beq	psgdone
	sta	$ff5e
	lda	,x+
	sta	$ff5f
	bra	psg
psgdone
; --- CoCo PSG SRAM: enable writes, map SRAM bank 0 at $C000, write, read back
	lda	#$08
	sta	$ff5d
	lda	#$80
	sta	$ff5a
	lda	#$5a
	sta	$c000
	lda	$c000
	sta	$0500		; harness checks this
; --- main loop: Stereo Composer and Orchestra-90 square waves
loop	lda	#$ff
	sta	$ff70		; Stereo Composer left high
	clr	$ff72		; right low
	sta	$ff7a		; Orchestra-90 left (MAME mapping)
	ldx	#200
d1	leax	-1,x
	bne	d1
	clr	$ff70
	lda	#$ff
	sta	$ff72
	clr	$ff7a
	ldx	#200
d2	leax	-1,x
	bne	d2
	bra	loop

ayregs	fcb	0,$40,1,0,7,$3e,8,$0f,$ff

	fill	$ff,$fffe-*
	fdb	start
