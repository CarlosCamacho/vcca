* CoCoSDC test, run as the machine's ROM (no Tandy ROM needed): exercises the
* card through its registers and leaves the results in RAM at $1000-$15FF for
* test/sdc.mjs to check. The SD card it expects is built by sdc.mjs:
*   STARTUP.CFG   "0=TEST.DSK"
*   TEST.DSK      630 sectors, byte i of LSN n = (n*7+i) & $FF
*   GAMES/TWO.DSK 630 sectors, byte i of LSN n = (n*3+i+$40) & $FF
*   GAMES/TWO.BAK a decoy that sorts first: M:GAMES/TWO must find TWO.DSK
*   RO.DSK        read-only
* and flash banks 0 and 1 starting "SDC0" and "SDC1".
*
* Results:
*   $1000 detect: 0 = the [DA] $FF42/$FF43 probe found the card
*   $1001 4 bytes of $C000 (bank 0)     $1005 4 bytes of $C000 after $FF43=1
*   $1010 'Q' size, 3 bytes              $1013 status read at once after a command
*   $1020 status: read past the end      $1021 status: write to RO.DSK
*   $1022 status: M:GAMES/TWO            $1023 status: N:NEW.DSK
*   $1024 status: X:NOSUCH.DSK           $1025 status: read LSN 5 (0 = ok)
*   $1026 floppy: sectors read OK (1)    $1027 floppy: final WD status
*   $1028 status after a write sent without waiting for READY
*   $1100 LSN 5, word mode   $1200 LSN 6, byte mode   $1300 LSN 7 after a write
*   $1400 drive 1 LSN 0 (TWO.DSK)        $1500 L:*.* then '>' (16 records)
*   $1600 floppy read, track 0 sector 2 (LSN 1)
*   $10FF $A5 when the program reached the end
	org	$8000
start	orcc	#$50
	lds	#$3f00
	lda	#$8c		; GIME: CoCo mode, MC3, MC2 (cartridge I/O), 16K/16K ROM
	sta	$ff90
	ldx	#$1000		; clear the results
clr1	clr	,x+
	cmpx	#$1800
	blo	clr1

* --- detect, the designer's probe -------------------------------------------
	lda	#$64
	sta	$ff42
	lda	$ff43
	clr	$ff42
	eora	$ff43
	suba	#$60
	sta	$1000

* --- flash banks -------------------------------------------------------------
	ldd	$c000
	std	$1001
	ldd	$c002
	std	$1003
	lda	#1
	sta	$ff43
	ldd	$c000
	std	$1005
	ldd	$c002
	std	$1007
	clr	$ff43

* --- command mode --------------------------------------------------------------
	lda	#$43
	sta	$ff40
	lda	#'Q'		; image size, drive 0
	sta	$ff49
	lda	#$c0
	sta	$ff48
	lda	$ff48		; read at once: the card is still busy
	sta	$1013
	lbsr	wait
	lda	$ff49
	sta	$1010
	ldd	$ff4a
	std	$1011

	ldx	#$1100		; LSN 5, word mode
	ldb	#5
	lda	#$80
	lbsr	readw
	stb	$1025

	clr	$ff49		; LSN 6, byte mode ($84): all 256 on $FF4B
	ldd	#6
	std	$ff4a
	lda	#$84
	sta	$ff48
	lbsr	wait
	ldx	#$1200
	ldb	#0
rdb	lda	$ff4b
	sta	,x+
	decb
	bne	rdb
	lbsr	wait

	clr	$ff49		; write LSN 7 with 255-i, then read it back
	ldd	#7
	std	$ff4a
	lda	#$a0
	sta	$ff48
	lbsr	wait
	ldb	#0
	clra
wr1	pshs	a
	coma
	sta	$ff4a		; the card takes the bytes in order, either register
	puls	a
	inca
	decb
	bne	wr1
	lbsr	wait
	ldx	#$1300
	ldb	#7
	lda	#$80
	lbsr	readw

	clr	$ff49		; a write that does not wait for READY: LSN 8
	ldd	#8
	std	$ff4a
	lda	#$a0
	sta	$ff48
	ldb	#0		; 256 bytes at once
wr2	sta	$ff4b
	decb
	bne	wr2
	lda	$ff48
	sta	$1028
	clr	$ff40		; abandon it: leave command mode, and come back
	lda	#$43
	sta	$ff40

	clr	$ff49		; LSN 630: one past the end
	ldd	#630
	std	$ff4a
	lda	#$80
	sta	$ff48
	lbsr	wait
	sta	$1020

	ldx	#mtwo		; mount GAMES/TWO (no extension) in drive 1
	lda	#$e1
	lbsr	block
	sta	$1022
	ldx	#$1400
	ldb	#0
	lda	#$81
	lbsr	readw

	ldx	#mro		; RO.DSK in drive 1, then try to write it
	lda	#$e1
	lbsr	block
	clr	$ff49
	clr	$ff4a
	clr	$ff4b
	ldx	#$c000		; any 256 bytes
	lda	#$a1
	lbsr	block
	sta	$1021

	ldx	#mnew		; a new image in drive 1
	lda	#$e1
	lbsr	block
	sta	$1023

	ldx	#mkill		; delete a file that is not there
	lda	#$e0
	lbsr	block
	sta	$1024

	ldx	#mlist		; list the root, first page
	lda	#$e0
	lbsr	block
	lda	#'>'
	sta	$ff49
	lda	#$c0
	sta	$ff48
	lbsr	wait
	ldx	#$1500
	ldb	#128
rdl	ldu	$ff4a
	stu	,x++
	decb
	bne	rdl
	lbsr	wait

	clr	$ff40		; leave command mode

* --- floppy personality: WD read of track 0 sector 2, drive 0, polled ------
	lda	#$09		; drive 0, motor on, single density (no NMI)
	sta	$ff40
	clr	$ff49		; track 0
	lda	#2
	sta	$ff4a		; sector 2
	ldy	#0
wsettle	leay	-1,y		; let the motor and head settle
	bne	wsettle
	lda	#$80		; read sector
	sta	$ff48
	ldx	#$1600
	ldy	#0		; timeout
	ldb	#0		; 256 bytes, counted
fl1	lda	$ff48
	bita	#$02		; DRQ
	bne	fl2
	leay	-1,y
	bne	fl1
	bra	fldone
fl2	lda	$ff4b
	sta	,x+
	ldy	#0
	decb
	bne	fl1
	inc	$1026
fldone	ldy	#0
fl3	lda	$ff48		; wait for BUSY to clear
	bita	#$01
	beq	fl4
	leay	-1,y
	bne	fl3
fl4	sta	$1027
	clr	$ff40

	lda	#$a5
	sta	$10ff
done	bra	done

* readw: A = command ($80+drive), B = LSN (0-255), X = buffer. Reads 256
* bytes in word mode. B = final status.
readw	clr	$ff49
	clr	$ff4a
	stb	$ff4b
	sta	$ff48
	bsr	wait
	bita	#$80		; FAILED
	bne	rwfail
	bita	#$02		; no block offered
	beq	rwfail
	ldb	#128
rw1	ldu	$ff4a
	stu	,x++
	decb
	bne	rw1
	bsr	wait
rwfail	tfr	a,b
	rts

* block: A = command ($A0/$E0 + drive), X = 256 bytes to send. Waits for
* READY, sends them as 128 words, waits for the result. A = final status.
block	sta	$ff48
	bsr	ready
	ldb	#128
bk1	ldu	,x++
	stu	$ff4a
	decb
	bne	bk1
	bsr	wait
	rts

* wait: until BUSY clears with no block pending, or READY. A = status.
wait	ldy	#0
wt1	lda	$ff48
	bmi	wtdone		; FAILED
	bita	#$01
	beq	wtdone		; not busy
	bita	#$02
	bne	wtdone		; READY: a block to move
	leay	-1,y
	bne	wt1
wtdone	rts

* ready: until READY (or FAILED).
ready	ldy	#0
rd1	lda	$ff48
	bmi	rddone
	bita	#$02
	bne	rddone
	leay	-1,y
	bne	rd1
rddone	rts

nmi	rti

mtwo	fcc	"M:GAMES/TWO"
	fcb	0
	fill	0,256-12
mro	fcc	"M:RO.DSK"
	fcb	0
	fill	0,256-9
mnew	fcc	"N:NEW.DSK"
	fcb	0
	fill	0,256-10
mkill	fcc	"X:NOSUCH.DSK"
	fcb	0
	fill	0,256-13
mlist	fcc	"L:*.*"
	fcb	0
	fill	0,256-6

	fill	$ff,$fffc-*
	fdb	nmi		; NMI
	fdb	start		; RESET
