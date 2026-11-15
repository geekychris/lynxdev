; lynxgfx.h: frame_count, frame_init. A cc65 interruptor counts vertical
; blanks (Mikey timer 2 interrupt). It returns with carry clear so the TGI
; driver's buffer swap runs too; irq.s acknowledges the interrupt.

        .export         _frame_count, _frame_init
        .interruptor    vbl_count

        .include        "lynx.inc"

        .bss
_frame_count:   .res    2

        .code
vbl_count:
        lda     INTSET
        and     #$04            ; timer 2 = vertical blank
        beq     @out
        inc     _frame_count
        bne     @out
        inc     _frame_count+1
@out:   clc
        rts

_frame_init:
        lda     #$80            ; timer 2 interrupt enable
        tsb     VTIMCTLA
        cli
        rts
