; lynxsnd.h: snd_period(hz) -> (clock select << 8) | timer reload.
; Clock = 1 MHz >> select; a square wave of clock / (2 * (reload + 1)).
; The select comes from hz thresholds, then reload = (500000 >> select) / hz - 1
; on Suzy's hardware divider. SND_PERIOD (lynxsnd.h) is the same at compile time.

        .export         _snd_period
        .importzp       tmp1, tmp2

MATHD   = $FC52
MATHC   = $FC53
MATHP   = $FC56
MATHN   = $FC57
MATHH   = $FC60
MATHG   = $FC61
MATHF   = $FC62
MATHE   = $FC63
SPRSYS  = $FC92

        .code
_snd_period:
        cpx     #0
        bne     @ok
        cmp     #16
        bcs     @ok
        lda     #16             ; below 16 Hz: 16 Hz
@ok:    sta     tmp1
        stx     tmp2
        sta     MATHP           ; divisor (low byte first: it clears the high one)
        stx     MATHN
        ldy     #0
@sel:   cpy     #6
        bcs     @go
        lda     tmp1            ; hz >= threshold[y]?
        cmp     thr_lo,y
        lda     tmp2
        sbc     thr_hi,y
        bcs     @go
        iny
        bra     @sel
@go:    lda     num_lo,y        ; 500000 >> select
        sta     MATHH
        lda     num_mid,y
        sta     MATHG
        lda     num_hi,y
        sta     MATHF
        stz     MATHE           ; starts the divide
@wait:  bit     SPRSYS
        bmi     @wait
        lda     MATHC           ; quotient > 255 (below ~31 Hz): cap at reload 255
        bne     @cap
        lda     MATHD
        dec     a
        bra     @ret
@cap:   lda     #$FF
@ret:   pha
        tya
        tax
        pla
        rts

        .rodata
thr_lo:  .byte  <1954, <977, <489, <245, <123, <62
thr_hi:  .byte  >1954, >977, >489, >245, >123, >62
num_lo:  .byte  $20, $90, $48, $24, $12, $09, $84
num_mid: .byte  $A1, $D0, $E8, $F4, $7A, $3D, $1E
num_hi:  .byte  $07, $03, $01, $00, $00, $00, $00
