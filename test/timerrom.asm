* GIME timer test, scanline mode. A timer FIRQ every N lines; the handler
* works ~40 cycles (as a real one would), reloads the timer and changes the
* border colour. On hardware the timer counts HSYNC pulses, so every band of
* border colour is the same height. test/timer.mjs measures the bands.
N       EQU     20
        ORG     $3000
start   ORCC    #$50            mask IRQ and FIRQ
        LDA     #$7E            JMP handler at BASIC's FIRQ jump, $010F
        STA     $010F
        LDX     #handler
        STX     $0110
        LDA     #$5C            INIT0: native, MMU, GIME FIRQ, MC3, MC2
        STA     $FF90
        CLR     $FF91           INIT1: TINS=0, the timer counts scanlines
        CLR     $FF92           no GIME IRQs
        LDA     #$20            timer interrupt on FIRQ
        STA     $FF93
        CLR     color
        LDD     #N
        STB     $FF95
        STA     $FF94           writing the MSB starts the timer
        ANDCC   #$BF            unmask FIRQ
loop    BRA     loop
handler PSHS    A,B
        LDA     $FF93           acknowledge
        LDB     #6              some work before the reload
dly     DECB
        BNE     dly
        LDD     #N
        STB     $FF95
        STA     $FF94           reload: restarts the timer
        INC     color
        LDA     color
        ANDA    #$3F
        STA     $FF9A           border colour
        PULS    A,B
        RTI
color   FCB     0
