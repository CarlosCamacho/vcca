* emesDOS09 controller through the cartridge bus. Talks to the mailbox at
* $FF40-$FF4F the way kernel/controller.asm in the emesDOS09 ROM does:
* IDENTIFY, then VOLUME_MOUNT, then DIR_OPEN and DIR_READ on the root of the
* SD image. test/emes.mjs reads the results from RAM.
*   $3100  IDENTIFY reply (16 bytes)      $3120  VOLUME_MOUNT reply (32 bytes)
*   $3140  directory entries read         $3141  error from DIR_READ that ended the loop
*   $3142  DIR_OPEN error                 $3143  $A5 when the program has finished
*   $3200  directory entries, 32 bytes each
CTRL    EQU     $FF40
STATUS  EQU     $FF41
DATA    EQU     $FF42
ERROR   EQU     $FF43
ARG0    EQU     $FF44
ACK     EQU     $FF4F
        ORG     $3000
start   ORCC    #$50
        CLR     $3140
        CLR     $3143
        LDA     #1              IDENTIFY
        JSR     exec
        LDX     #$3100
        LDB     #16
        JSR     fetch
        CLR     ARG0            VOLUME_MOUNT, drive A
        LDA     #$50
        JSR     exec
        LDX     #$3120
        LDB     #32
        JSR     fetch
        CLR     ARG0            DIR_OPEN "\"
        LDA     #'\'
        STA     DATA
        CLR     DATA
        LDA     #$52
        JSR     exec
        STA     $3142
        BNE     done
        LDA     DATA
        STA     $3150           directory handle
        LDA     #$FF
        STA     ACK
        LDU     #$3200
next    LDA     $3150
        STA     ARG0
        LDA     #$53            DIR_READ
        JSR     exec
        STA     $3141
        BNE     done
        TFR     U,X
        LDB     #32
        JSR     fetch
        TFR     X,U
        INC     $3140
        BRA     next
done    LDA     #$A5
        STA     $3143
hang    BRA     hang
* Run command A; returns the controller error in A (0 = none). Gives up after
* 65536 status polls, as the ROM's bounded wait does.
exec    STA     CTRL
        LDY     #0
poll    LEAY    -1,Y
        BEQ     failed
        LDA     STATUS
        ANDA    #$0C            DONE or ERROR
        BEQ     poll
        LDA     STATUS
        ANDA    #$08
        BEQ     ok
        LDA     ERROR
        RTS
ok      CLRA
        RTS
failed  LDA     #3              TIMEOUT
        RTS
* Read B bytes of reply from DATA into ,X, then acknowledge.
fetch   LDA     DATA
        STA     ,X+
        DECB
        BNE     fetch
        LDA     #$FF
        STA     ACK
        RTS
