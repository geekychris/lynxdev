; Frank the Frog - the per-frame lane work in 6502 assembly (the C it
; implements is beside it in lanes.c and render.c, under #if 0).

        .export     _lanes_tick, _render_objs
        .import     _obj_x, _lane_first, _lane_num, _lane_period, _lane_speed
        .import     _lane_dir, _lane_low, _obj_scb, _obj_off
        .importzp   ptr1, tmp1, tmp2, tmp3, tmp4, ptr2, ptr3

NUM_LANES = 10
NUM_OBJS  = 30
RIGHT_END = 336            ; SCREEN_W + 16
SCB_HPOS  = 7              ; SCB_REHV_PAL: sprctl0 sprctl1 sprcoll next data hpos

        .bss
lane:   .res 1

        .code

; void lanes_tick(void): every object moves by its lane's speed; one that
; leaves the track comes back a period later
.proc _lanes_tick
        ldx     #0
nextlane:
        stx     lane
        lda     _lane_num,x
        bne     :+
        jmp     done
:
        sta     tmp1                    ; objects left
        lda     _lane_speed,x
        sta     tmp2
        txa
        asl     a
        tay
        lda     _lane_period,y
        sta     ptr2
        lda     _lane_period+1,y
        sta     ptr2+1
        lda     _lane_low,y
        sta     ptr3
        lda     _lane_low+1,y
        sta     ptr3+1
        lda     _lane_first,x
        asl     a
        tay
        lda     _lane_dir,x
        bmi     left

right:  clc                             ; x += speed
        lda     _obj_x,y
        adc     tmp2
        sta     tmp3
        lda     _obj_x+1,y
        adc     #0
        sta     tmp4
        lda     tmp3                    ; x >= 336 ?
        cmp     #<RIGHT_END
        lda     tmp4
        sbc     #>RIGHT_END
        bvc     :+
        eor     #$80
:       bmi     rstore
        sec                             ; x -= period
        lda     tmp3
        sbc     ptr2
        sta     tmp3
        lda     tmp4
        sbc     ptr2+1
        sta     tmp4
rstore: lda     tmp3
        sta     _obj_x,y
        lda     tmp4
        sta     _obj_x+1,y
        iny
        iny
        dec     tmp1
        beq     done
        jmp     right

left:   sec                             ; x -= speed
        lda     _obj_x,y
        sbc     tmp2
        sta     tmp3
        lda     _obj_x+1,y
        sbc     #0
        sta     tmp4
        lda     tmp3                    ; x < low ?
        cmp     ptr3
        lda     tmp4
        sbc     ptr3+1
        bvc     :+
        eor     #$80
:       bpl     lstore
        clc                             ; x += period
        lda     tmp3
        adc     ptr2
        sta     tmp3
        lda     tmp4
        adc     ptr2+1
        sta     tmp4
lstore: lda     tmp3
        sta     _obj_x,y
        lda     tmp4
        sta     _obj_x+1,y
        iny
        iny
        dec     tmp1
        beq     done
        jmp     left

done:   ldx     lane
        inx
        cpx     #NUM_LANES
        beq     :+
        jmp     nextlane
:       rts
.endproc

; void render_objs(void): obj_scb[i]->hpos = obj_x[i] / 2 (signed) + obj_off[i]
.proc _render_objs
        ldx     #0
loop:   txa
        asl     a
        tay
        lda     _obj_scb,y
        sta     ptr1
        lda     _obj_scb+1,y
        sta     ptr1+1
        lda     _obj_x+1,y
        cmp     #$80                    ; carry = sign: arithmetic shift
        ror     a
        sta     tmp1
        lda     _obj_x,y
        ror     a
        clc
        adc     _obj_off,x
        ldy     #SCB_HPOS
        sta     (ptr1),y
        lda     tmp1
        adc     #0
        iny
        sta     (ptr1),y
        inx
        cpx     #NUM_OBJS
        bne     loop
        rts
.endproc
