* GIME timer test (LOADM, then EXEC): 320x200x16, every pixel color 0. VBORD
* loads the timer with 40 and sets color 0 black; each timer FIRQ toggles
* color 0 red/green and reloads the timer with 10. The stripes are 12 lines
* tall on a 1986 GIME (n+2), 11 on a 1987 GIME (n+1), and must not drift.
        pragma  6309
        org     $3000
start   orcc    #$50
        ldmd    #$01
        sta     $FFD9
        lda     $FF01
        anda    #$FE
        sta     $FF01
        lda     $FF03
        anda    #$FE
        sta     $FF03
        lda     $FF21
        anda    #$FE
        sta     $FF21
        lda     $FF23
        anda    #$FE
        sta     $FF23
        lda     $FF00
        lda     $FF02
        lda     $FF20
        lda     $FF22
        lda     #$7C
        sta     $FF90
        clr     $FF91           timer: scanlines
        ldd     #$0000
        std     $FF94
* screen at block $30 ($60000), cleared
        lda     #$30
        sta     $FFA2
        ldx     #$4000
        ldw     #$2000
        clr     ,-s
        tfm     s,x+
        leas    1,s
        ldb     #3
cl1     inc     $FFA2
        ldx     #$4000
        ldw     #$2000
        clr     ,-s
        tfm     s,x+
        leas    1,s
        decb
        bne     cl1
        lda     #$3A
        sta     $FFA2
        lda     #$80
        sta     $FF98
        lda     #$3E            200 lines, 160 bytes, 16 colors
        sta     $FF99
        clr     $FF9A
        clr     $FF9C
        ldd     #$C000          $60000/8
        std     $FF9D
        clr     $FF9F
        clr     $FFB0
        lda     #$7E
        sta     $010C
        sta     $010F
        ldx     #irq
        stx     $010D
        ldx     #firq
        stx     $0110
        lda     $FF92
        lda     $FF93
        lda     #$08
        sta     $FF92
        lda     #$20
        sta     $FF93
        andcc   #$AF
loop    bra     loop
irq     lda     $FF92
        clr     $FFB0
        clr     tog
        ldd     #40
        stb     $FF95
        sta     $FF94
        rti
firq    pshs    d
        lda     $FF93
        ldd     #10
        stb     $FF95
        sta     $FF94
        lda     tog
        eora    #1
        sta     tog
        beq     f1
        lda     #$24            red
        bra     f2
f1      lda     #$12            green
f2      sta     $FFB0
        puls    d
        rti
tog     fcb     0
        end     start
