/*
 * lynxmath.h - Suzy's hardware maths unit: 16x16 -> 32-bit multiply and
 * 32/16 divide, far faster than cc65's software routines. Unsigned.
 * Also small helpers games use every frame.
 */
#ifndef LYNXMATH_H
#define LYNXMATH_H

#include "lynxhw.h"

/* a * b (unsigned 16 x 16 -> 32). */
uint32_t __fastcall__ math_mul16(uint16_t a, uint16_t b);
/* num / den (unsigned 32 / 16 -> 32); the remainder goes to math_rem.
   den = 0 gives 0xFFFFFFFF-ish garbage, as on the hardware: check first. */
uint32_t __fastcall__ math_div32(uint32_t num, uint16_t den);
extern uint16_t math_rem;

/* Axis-aligned boxes share at least one pixel (x, y, w, h each, 8-bit
   screen coordinates, w and h >= 1, aw + bw and ah + bh <= 255). */
#define box_hit(ax, ay, aw, ah, bx, by, bw, bh) \
    ((uint8_t)((ax) - (bx) + (bw) - 1) < (uint8_t)((aw) + (bw) - 1) && \
     (uint8_t)((ay) - (by) + (bh) - 1) < (uint8_t)((ah) + (bh) - 1))

#endif
