/* lynxrnd.h - a small fast random number generator (16-bit xorshift).
   Deterministic: seed it from frame_count at a key press for variety. */
#ifndef LYNXRND_H
#define LYNXRND_H
#include <stdint.h>
extern uint16_t rnd_state;
void __fastcall__ rnd_seed(uint16_t s);
uint8_t rnd8(void);
uint16_t rnd16(void);
/* 0..n-1 */
uint8_t __fastcall__ rnd_range(uint8_t n);
#endif
