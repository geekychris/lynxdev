; Rock Blaster (Lynx) - the per-frame loops over the objects, in 6502.
;
; game.c keeps the original's logic in C; moving 24 rocks, 8 bullets and 64
; particles a frame and testing bullets against rocks were most of the
; frame's time in cc65's C, so they are here (as in the Atari 8-bit port's
; objs.s). Each move loop also writes the object's Suzy sprite (SCB in
; main.c): position, picture, colour, and SKIP when the object is gone.
;
; Units (game.h): positions are 1/128 world pixel, so the high byte is the
; Lynx screen pixel (the world is shown at half size); rk_px/rk_py are whole
; world pixels for the collision tests.

        .setcpu "65SC02"
        .importzp ptr1, ptr2, ptr3, tmp1, tmp2, tmp3, tmp4
        .import _rk_x, _rk_y, _rk_dx, _rk_dy, _rk_px, _rk_py
        .import _rk_active, _rk_size, _rk_shape, _rk_sx, _rk_sy
        .import _bl_x, _bl_y, _bl_dx, _bl_dy, _bl_active, _bl_life
        .import _pt_sx, _pt_sy, _pt_k, _pt_age, _pt_active, _pt_life, _pt_color
        .import _pt_offx, _pt_offy
        .import _particles_live
        .import _scbs, _scb_off, _rock_frames
        .export _rocks_move, _bullets_move, _particles_move, _hit_scan
        .export _objs_still, _erase_begin, _erase_end
        .export _hit_x, _hit_y, _hit_sx, _hit_sy, _bullet_scan, _hit_rock

MAX_ROCKS     = 24
MAX_BULLETS   = 8
MAX_PARTICLES = 64
W7_HI         = >(320 * 128)        ; $A0: the world's width, high byte
H7_HI         = >(204 * 128)        ; $66
SCB_SIZE      = 23                  ; SCB_REHV_PAL
SCB_CTL1      = 1
SCB_DATA      = 5
SCB_HPOS      = 7
SCB_VPOS      = 9
SCB_PENPAL    = 15
SKIP          = $04
S_BUL         = MAX_ROCKS           ; main.c: the SCBs of a set
S_PART        = S_BUL + MAX_BULLETS
N_SCB         = 113

        .bss
_objs_still: .res 1
pen_save: .res N_SCB
idx:    .res 1
hr:     .res 1
sqs:    .res 2
_hit_x: .res 2
_hit_y: .res 2
_hit_rock: .res 1
_hit_sx: .res 1
_hit_sy: .res 1
lim:    .res 3
bi:     .res 1

        .rodata
rock_scbs:
        .repeat MAX_ROCKS, I
        .word _scbs + I * SCB_SIZE
        .endrep
bullet_scbs:
        .repeat MAX_BULLETS, I
        .word _scbs + (S_BUL + I) * SCB_SIZE
        .endrep
part_scbs:
        .repeat MAX_PARTICLES, I
        .word _scbs + (S_PART + I) * SCB_SIZE
        .endrep
rock_rad:       .byte 20, 12, 6     ; the original's radii
rock_centre:    .byte 10, 6, 3      ; centre pixel of the art (mkart.py)
rock_colour:    .byte $08, $07, $06 ; light / medium / dim grey (pen 1 = low nibble)
PT_AGES = 32
rowx_lo:
        .repeat 64, I
        .byte <(_pt_offx + I * PT_AGES)
        .endrep
rowx_hi:
        .repeat 64, I
        .byte >(_pt_offx + I * PT_AGES)
        .endrep
rowy_lo:
        .repeat 64, I
        .byte <(_pt_offy + I * PT_AGES)
        .endrep
rowy_hi:
        .repeat 64, I
        .byte >(_pt_offy + I * PT_AGES)
        .endrep
sq_lo:
        .repeat 32, I
        .byte <(I * I)
        .endrep
sq_hi:
        .repeat 32, I
        .byte >(I * I)
        .endrep

        .code

; pos += vel (16-bit, X = index * 2), wrapped into 0 .. lim * 256: the world
; is far wider than a frame's move, so "went below 0" shows as >= lim.
; Nothing when objs_still is set.
.macro MOVEWRAP pos, vel, lim
        lda     _objs_still
        bne     :+++
        clc
        lda     pos,x
        adc     vel,x
        sta     pos,x
        lda     pos+1,x
        adc     vel+1,x
        cmp     #lim
        bcc     :++
        bit     vel+1,x
        bmi     :+
        sbc     #lim                ; carry is set
        bra     :++
:       clc
        adc     #lim
:       sta     pos+1,x
:
.endmacro

; ptr1 = the SCB in the current set, from a table of set 0's (X = index * 2)
.macro SCB_PTR table
        clc
        lda     table,x
        adc     _scb_off
        sta     ptr1
        lda     table+1,x
        adc     _scb_off+1
        sta     ptr1+1
.endmacro

.macro SCB_HIDE
        ldy     #SCB_CTL1
        lda     (ptr1),y
        ora     #SKIP
        sta     (ptr1),y
.endmacro

.macro SCB_SHOW
        ldy     #SCB_CTL1
        lda     (ptr1),y
        and     #<~SKIP
        sta     (ptr1),y
.endmacro

; ---------------------------------------------------------------- rocks
; void rocks_move(void): every live rock moves and wraps; rk_px/py; its SCB
.proc _rocks_move
        ldy     #MAX_ROCKS - 1
loop:   sty     idx
        tya
        asl
        tax
        SCB_PTR rock_scbs
        lda     _rk_active,y
        bne     live
        SCB_HIDE
        jmp     next
live:   MOVEWRAP _rk_x, _rk_dx, W7_HI
        MOVEWRAP _rk_y, _rk_dy, H7_HI
        lda     _rk_x,x             ; px = x >> 7
        asl
        lda     _rk_x+1,x
        rol
        sta     _rk_px,x
        lda     #0
        rol
        sta     _rk_px+1,x
        lda     _rk_y,x
        asl
        lda     _rk_y+1,x
        rol
        sta     _rk_py,x
        lda     #0
        rol
        sta     _rk_py+1,x
        ldy     idx                 ; screen pixels for the hit tests' first look
        lda     _rk_x+1,x
        sta     _rk_sx,y
        lda     _rk_y+1,x
        sta     _rk_sy,y
        ; the sprite: picture (size * 4 + shape), colour, centre
        ldy     idx
        lda     _rk_size,y
        tay
        lda     rock_centre,y
        sta     tmp1
        lda     rock_colour,y
        sta     tmp2
        tya
        asl
        asl
        ldy     idx
        ora     _rk_shape,y
        asl
        tay
        lda     _rock_frames,y
        sta     tmp3
        lda     _rock_frames+1,y
        ldy     #SCB_DATA+1
        sta     (ptr1),y
        dey
        lda     tmp3
        sta     (ptr1),y
        ldy     #SCB_HPOS           ; hpos = screen x - centre (signed)
        lda     _rk_x+1,x
        sec
        sbc     tmp1
        sta     (ptr1),y
        iny
        lda     #0
        sbc     #0
        sta     (ptr1),y
        iny                         ; vpos
        lda     _rk_y+1,x
        sec
        sbc     tmp1
        sta     (ptr1),y
        iny
        lda     #0
        sbc     #0
        sta     (ptr1),y
        ldy     #SCB_PENPAL
        lda     tmp2
        sta     (ptr1),y
        SCB_SHOW
next:   ldy     idx
        dey
        bmi     done
        jmp     loop
done:   rts
.endproc

; ---------------------------------------------------------------- bullets
; void bullets_move(void): move, wrap, age (gone after BULLET_LIFE); SCBs
.proc _bullets_move
        ldy     #MAX_BULLETS - 1
loop:   sty     idx
        tya
        asl
        tax
        SCB_PTR bullet_scbs
        lda     _bl_active,y
        bne     live
        jmp     hide
live:
        MOVEWRAP _bl_x, _bl_dx, W7_HI
        MOVEWRAP _bl_y, _bl_dy, H7_HI
        ldy     idx
        lda     _objs_still
        bne     show
        lda     _bl_life,y
        dec     a
        sta     _bl_life,y
        bne     show
        lda     #0                  ; gone (not drawn, as the original)
        sta     _bl_active,y
        bra     hide
show:   ldy     #SCB_HPOS
        lda     _bl_x+1,x
        sta     (ptr1),y
        ldy     #SCB_VPOS
        lda     _bl_y+1,x
        sta     (ptr1),y
        SCB_SHOW
        bra     next
hide:   SCB_HIDE
next:   ldy     idx
        dey
        bmi     done
        jmp     loop
done:   rts
.endproc

; ---------------------------------------------------------------- particles
; void particles_move(void): age; gone when old or out of the screen (port)
; and hidden then (start_game hides the title's stars);
; placed at start + the precomputed offset for its velocity and age (game.c
; particles_table: the original's move and slow-down). SCBs.
.proc _particles_move
        ldy     #MAX_PARTICLES - 1
loop:   lda     _pt_active,y        ; gone ones were hidden when they went
        bne     live0
        dey
        bpl     loop
        rts
live0:  sty     idx
        tya
        asl
        tax
        SCB_PTR part_scbs
        bra     live
kill:   ldy     idx
        lda     #0
        sta     _pt_active,y
        dec     _particles_live
        SCB_HIDE
        jmp     next
live:   lda     _pt_age,y
        inc     a
        sta     _pt_age,y
        cmp     _pt_life,y
        beq     kill
        sta     tmp1                ; age
        ldx     _pt_k,y
        lda     rowx_lo,x
        sta     ptr2
        lda     rowx_hi,x
        sta     ptr2+1
        lda     rowy_lo,x
        sta     ptr3
        lda     rowy_hi,x
        sta     ptr3+1
        lda     _pt_sx,y
        ldy     tmp1
        clc
        adc     (ptr2),y            ; x: 0..159, else gone (left wraps above 159)
        cmp     #160
        bcs     kill
        sta     tmp2
        ldy     idx
        lda     _pt_sy,y
        ldy     tmp1
        clc
        adc     (ptr3),y
        cmp     #102
        bcs     kill
        ldy     #SCB_VPOS
        sta     (ptr1),y
        ldy     #SCB_HPOS
        lda     tmp2
        sta     (ptr1),y
        ldy     idx
        lda     _pt_color,y
        ldy     #SCB_PENPAL
        sta     (ptr1),y
        SCB_SHOW
next:   ldy     idx
        dey
        bmi     done
        jmp     loop
done:   rts
.endproc

; ---------------------------------------------------------------- hits
; uint8_t __fastcall__ hit_scan(uint8_t r): the first live rock (lowest
; index, as the original's loop) whose circle comes within r of
; (hit_x, hit_y), whole world pixels: dx^2 + dy^2 < (r + radius)^2.
; 0xFF if none.
.proc _hit_scan
        sta     hr
        ldx     #2                  ; lim = (r + radius) / 2 + 2: screen pixels
lims:   lda     rock_rad,x
        clc
        adc     hr
        lsr     a
        adc     #2
        sta     lim,x
        dex
        bpl     lims
        ldy     #0
loop:   lda     _rk_active,y
        bne     live
        jmp     next
live:   ldx     _rk_size,y          ; first look, screen pixels: |dx|, |dy| < lim
        lda     _hit_sx
        sec
        sbc     _rk_sx,y
        bcs     :+
        eor     #$FF
        inc     a
:       cmp     lim,x
        bcs     far
        lda     _hit_sy
        sec
        sbc     _rk_sy,y
        bcs     :+
        eor     #$FF
        inc     a
:       cmp     lim,x
        bcs     far
        bra     close
far:    jmp     next
close:  tya
        asl
        tax
        sec                         ; tmp1 = |hit_x - px| (if < 256)
        lda     _hit_x
        sbc     _rk_px,x
        sta     tmp1
        lda     _hit_x+1
        sbc     _rk_px+1,x
        beq     dx_ok
        cmp     #$FF
        bne     next
        lda     tmp1
        beq     next                ; -256
        eor     #$FF
        inc     a
        sta     tmp1
dx_ok:  sec                         ; tmp2 = |hit_y - py|
        lda     _hit_y
        sbc     _rk_py,x
        sta     tmp2
        lda     _hit_y+1
        sbc     _rk_py+1,x
        beq     dy_ok
        cmp     #$FF
        bne     next
        lda     tmp2
        beq     next
        eor     #$FF
        inc     a
        sta     tmp2
dy_ok:  ldx     _rk_size,y          ; tmp3 = r + the rock's radius
        lda     rock_rad,x
        clc
        adc     hr
        sta     tmp3
        lda     tmp1
        cmp     tmp3
        bcs     next
        lda     tmp2
        cmp     tmp3
        bcs     next
        ldx     tmp1                ; dx^2 + dy^2
        lda     sq_lo,x
        sta     sqs
        lda     sq_hi,x
        sta     sqs+1
        ldx     tmp2
        clc
        lda     sqs
        adc     sq_lo,x
        sta     sqs
        lda     sqs+1
        adc     sq_hi,x
        sta     sqs+1
        ldx     tmp3                ; < (r + radius)^2 ?
        lda     sqs
        cmp     sq_lo,x
        lda     sqs+1
        sbc     sq_hi,x
        bcc     hit
next:   iny
        cpy     #MAX_ROCKS
        beq     none
        jmp     loop
none:   lda     #$FF
        ldx     #0
        rts
hit:    tya
        ldx     #0
        rts
.endproc

; uint8_t __fastcall__ bullet_scan(uint8_t i): the first live bullet from i
; on that hits a rock (hit_scan with the original's bullet radius 2); the
; rock in hit_rock. 0xFF when none is left.
.proc _bullet_scan
        sta     bi
loop:   ldy     bi
        cpy     #MAX_BULLETS
        bcs     none
        lda     _bl_active,y
        beq     next
        tya
        asl
        tax
        lda     _bl_x,x             ; hit_x = x >> 7
        asl
        lda     _bl_x+1,x
        rol
        sta     _hit_x
        lda     #0
        rol
        sta     _hit_x+1
        lda     _bl_y,x
        asl
        lda     _bl_y+1,x
        rol
        sta     _hit_y
        lda     #0
        rol
        sta     _hit_y+1
        lda     _bl_x+1,x
        sta     _hit_sx
        lda     _bl_y+1,x
        sta     _hit_sy
        lda     #2
        jsr     _hit_scan
        cmp     #$FF
        bne     hit
next:   inc     bi
        bra     loop
hit:    sta     _hit_rock
        lda     bi
        ldx     #0
        rts
none:   lda     #$FF
        ldx     #0
        rts
.endproc

; ---------------------------------------------------------------- multiply
; int16_t __fastcall__ mul_q8(uint8_t b): (mul_a * b) >> 8, mul_a signed
; 16-bit (game.c: velocities from the sine table and a speed, random ranges;
; cc65's 32-bit multiply took ~1000 cycles). Rounds toward zero.
        .export _mul_q8, _mul_a
        .bss
_mul_a: .res 2
mb:     .res 1
neg:    .res 1
pr:     .res 3
        .code
.proc _mul_q8
        sta     mb
        stz     neg
        lda     _mul_a+1
        bpl     pos
        inc     neg                 ; |a|
        lda     _mul_a
        eor     #$FF
        clc
        adc     #1
        sta     _mul_a
        lda     _mul_a+1
        eor     #$FF
        adc     #0
        sta     _mul_a+1
pos:    stz     pr
        stz     pr+1
        stz     pr+2
        ldx     #8                  ; pr += a << n for every set bit n of b
nbit:   lsr     mb
        bcc     skip
        clc
        lda     pr+1
        adc     _mul_a
        sta     pr+1
        lda     pr+2
        adc     _mul_a+1
        sta     pr+2
skip:   ror     pr+2                ; shift the 24-bit sum right: the next bit
        ror     pr+1                ; of b weighs the same as this one did
        ror     pr
        dex
        bne     nbit
        lda     pr+1                ; (a * b) >> 8
        ldx     pr+2
        ldy     neg
        beq     done
        eor     #$FF                ; negate
        clc
        adc     #1
        pha
        txa
        eor     #$FF
        adc     #0
        tax
        pla
done:   rts
.endproc

; ---------------------------------------------------------------- erase
; erase_begin: pen 1 of every SCB in the current set -> colour 1 (the
; background; all the game's sprites are 1 bit per pixel), the old pens
; saved; erase_end puts them back.
.macro SET_PTR
        clc
        lda     #<(_scbs + SCB_PENPAL)
        adc     _scb_off
        sta     ptr1
        lda     #>(_scbs + SCB_PENPAL)
        adc     _scb_off+1
        sta     ptr1+1
.endmacro

.macro NEXT_SCB
        clc
        lda     ptr1
        adc     #SCB_SIZE
        sta     ptr1
        bcc     :+
        inc     ptr1+1
:
.endmacro

.proc _erase_begin
        SET_PTR
        ldx     #0
loop:   lda     (ptr1)
        sta     pen_save,x
        lda     #$01
        sta     (ptr1)
        NEXT_SCB
        inx
        cpx     #N_SCB
        bne     loop
        rts
.endproc

.proc _erase_end
        SET_PTR
        ldx     #0
loop:   lda     pen_save,x
        sta     (ptr1)
        NEXT_SCB
        inx
        cpx     #N_SCB
        bne     loop
        rts
.endproc

; ---------------------------------------------------------------- HUD
; void __fastcall__ hud_digits(SCB_REHV_PAL *first): the 7 score digit
; sprites' pictures from gs_score_str
        .import _gs_score_str, _digit_frames
        .export _hud_digits
.proc _hud_digits
        sta     ptr1
        stx     ptr1+1
        ldx     #0
loop:   lda     _gs_score_str,x
        sec
        sbc     #'0'
        asl     a
        tay
        lda     _digit_frames,y
        sta     tmp1
        lda     _digit_frames+1,y
        ldy     #SCB_DATA+1
        sta     (ptr1),y
        dey
        lda     tmp1
        sta     (ptr1),y
        clc
        lda     ptr1
        adc     #SCB_SIZE
        sta     ptr1
        bcc     :+
        inc     ptr1+1
:       inx
        cpx     #7
        bne     loop
        rts
.endproc
