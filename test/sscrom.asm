; S/SC test: reset the pak, then speak "HELLO" through its text-to-speech.
	org	$8000
start	orcc	#$50
	lds	#$3f00
	lda	#$8c
	sta	$ff90
	clr	$ff7f		; Multi-Pak slot 1
	lda	#1
	sta	$ff7d		; hold the S/SC in reset
	ldx	#0
w0	leax	-1,x
	bne	w0
	clr	$ff7d		; release: the TMS7040 starts
	ldx	#0
w1	leax	-1,x
	bne	w1
	ldy	#text
next	lda	,y+
	beq	done
wait	ldb	$ff7e
	bitb	#$80		; ready for the next byte?
	beq	wait
	sta	$ff7e
	bra	next
done	bra	done
text	fcc	"HELLO"
	fcb	13,0
	fill	$ff,$fffe-*
	fdb	start
