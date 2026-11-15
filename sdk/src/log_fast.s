; lynxlog.h: lynx_log_s in asm, ~16 cycles a character (the C loop cost
; ~150). The address is LYNX_LOG_ADDR in lynxlog.h ($FCF0).

        .export         _lynx_log_s
        .importzp       ptr1

LYNX_LOG_ADDR = $FCF0

        .code
_lynx_log_s:
        sta     ptr1
        stx     ptr1+1
        ldy     #0
@loop:  lda     (ptr1),y
        beq     @done
        sta     LYNX_LOG_ADDR
        iny
        bne     @loop
        inc     ptr1+1
        bra     @loop
@done:  rts

; lynx_log_u: unsigned decimal, no division (subtract powers of ten).
        .export         _lynx_log_u
        .importzp       tmp1, tmp2, tmp3, tmp4

_lynx_log_u:
        sta     tmp1            ; value, low
        stx     tmp2            ; value, high
        stz     tmp3            ; a digit was written
        ldy     #0
@digit: ldx     #'0'
@sub:   lda     tmp1
        sec
        sbc     pow_lo,y
        sta     tmp4
        lda     tmp2
        sbc     pow_hi,y
        bcc     @got
        sta     tmp2
        lda     tmp4
        sta     tmp1
        inx
        bra     @sub
@got:   cpx     #'0'
        bne     @emit
        lda     tmp3
        beq     @next
@emit:  stx     LYNX_LOG_ADDR
        stx     tmp3
@next:  iny
        cpy     #4
        bne     @digit
        lda     tmp1
        clc
        adc     #'0'
        sta     LYNX_LOG_ADDR
        rts

        .rodata
pow_lo: .byte   <10000, <1000, <100, <10
pow_hi: .byte   >10000, >1000, >100, >10
