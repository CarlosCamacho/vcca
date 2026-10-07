* GIME VBORD test. Each VBORD IRQ changes the border colour. VBORD fires as
* the beam leaves the active area, so in a captured frame the border colour
* changes exactly where the bottom border begins. test/vbord.mjs checks it.
        ORG     $3000
start   ORCC    #$50            mask IRQ and FIRQ
        LDA     #$7E            JMP irqh at BASIC's IRQ jump, $010C
        STA     $010C
        LDX     #irqh
        STX     $010D
        LDA     $FF01           BASIC's PIA interrupts off: HSYNC (CA1)
        ANDA    #$FE
        STA     $FF01
        LDA     $FF03           and field sync (CB1)
        ANDA    #$FE
        STA     $FF03
        LDA     $FF00           clear their flags
        LDA     $FF02
        LDA     #$6C            INIT0: native, MMU, GIME IRQ, MC3, MC2
        STA     $FF90
        CLR     $FF91
        LDA     #$08            VBORD on IRQ
        STA     $FF92
        CLR     $FF93           no GIME FIRQs
        LDA     #$80            graphics
        STA     $FF98
        LDA     #$20            200 lines (LPF=01), narrowest width: wide side borders
        STA     $FF99
        ANDCC   #$EF            unmask IRQ
loop    BRA     loop
irqh    LDA     $FF92           acknowledge
        INC     color
        LDA     color
        ANDA    #$3F
        STA     $FF9A           border colour
        RTI
color   FCB     1
