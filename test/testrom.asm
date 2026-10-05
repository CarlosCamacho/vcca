; Test ROM for the VCC Android port's harness. Not a CoCo ROM replacement:
; it exercises CPU, SAM/VDG text display, PIA keyboard scan, the DAC and
; an FD-502 sector read, and leaves results on the text screen at $0400.
	org	$8000
start	orcc	#$50
	lds	#$3f00
	lda	#$8c		; GIME: CoCo-compatible video, 16K/16K ROM map
	sta	$ff90
	lda	#$e0		; video base: physical $70000 (logical $0000)
	sta	$ff9d
	clr	$ff9e
	sta	$ffc9		; SAM F1 set: display offset $0400
	; palette: the real ROM loads this; without it everything is black
	ldx	#$ffb0
	leay	pal,pcr
pl	lda	,y+
	sta	,x+
	cmpx	#$ffc0
	bne	pl
	; clear screen with spaces ($60 = normal space in VDG codes)
	ldx	#$0400
	lda	#$60
cls	sta	,x+
	cmpx	#$0600
	bne	cls
	; title on line 0
	ldx	#$0400
	leay	title,pcr
	lbsr	puts
	; PIA setup: $FF00 inputs, $FF02 outputs (keyboard columns)
	clr	$ff01
	clr	$ff00
	clr	$ff03
	lda	#$ff
	sta	$ff02
	lda	#$04
	sta	$ff01
	sta	$ff03
	; DAC: $FF20 bits 2-7 output, sound enable via $FF23 CB2
	clr	$ff21
	lda	#$fe
	sta	$ff20
	lda	#$34
	sta	$ff21
	clr	$ff23
	lda	#$f8		; PB3-7 outputs: VDG mode lines
	sta	$ff22
	lda	#$3c		; data register, CB2 high = sound enable
	sta	$ff23
	clr	$ff22		; alphanumeric mode
	; FDC: motor on, drive 0, restore, then read T0 S1 into $0500
	lda	#$09
	sta	$ff40
	lda	#$00		; restore
	sta	$ff48
	ldx	#0
w1	leax	-1,x
	bne	w1
	lda	#1
	sta	$ff4a		; sector register
	lda	#$80		; read sector
	sta	$ff48
	ldx	#$0420		; line 1 of the screen
	ldy	#2000
rd	lda	$ff48
	bita	#$02		; DRQ?
	beq	nodrq
	lda	$ff4b
	sta	,x+
	cmpx	#$0440
	beq	rddone
	ldy	#2000
	bra	rd
nodrq	leay	-1,y
	bne	rd
rddone	lda	#$00
	sta	$ff40		; motor off
	; main loop: show keyboard rows for each column, toggle DAC
loop	ldx	#$0460		; line 3
	ldb	#$fe
col	stb	$ff02
	lda	$ff00
	lbsr	hex
	rolb
	bcs	col
	; square wave on the DAC
	lda	$ff20
	eora	#$fc
	sta	$ff20
	sta	$ff7a		; Orchestra-90 DACs follow the same wave
	sta	$ff7b
	bra	loop

; print A as two hex digits at X (VDG codes), X advances by 2
hex	pshs	a
	lsra
	lsra
	lsra
	lsra
	bsr	nyb
	puls	a
	anda	#$0f
nyb	cmpa	#10
	blo	dig
	adda	#$41-10	; 'A'..'F' -> VDG $41.. (normal letters are $40-$5F)
	bra	put
dig	adda	#$70	; '0'..'9' -> VDG $70..$79
put	sta	,x+
	rts

puts	lda	,y+
	beq	pdone
	sta	,x+
	bra	puts
pdone	rts

pal	fcb	$12,$36,$09,$24,$3f,$1b,$2d,$26,$00,$12,$00,$3f,$00,$12,$00,$26
title	fcc	"VCC ANDROID PORT TEST ROM"
	fcb	0


	fill	$ff,$fffe-*
	fdb	start
