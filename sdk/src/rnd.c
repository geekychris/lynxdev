/* lynxrnd.h: xorshift16 (7, 9, 8). */
#include "lynxrnd.h"

uint16_t rnd_state = 0xACE1;

void __fastcall__ rnd_seed(uint16_t s) { rnd_state = s ? s : 0xACE1; }

uint16_t rnd16(void)
{
    uint16_t x = rnd_state;
    x ^= x << 7;
    x ^= x >> 9;
    x ^= x << 8;
    rnd_state = x;
    return x;
}

uint8_t rnd8(void) { return (uint8_t)rnd16(); }

uint8_t __fastcall__ rnd_range(uint8_t n)
{
    return (uint8_t)(((uint16_t)rnd8() * n) >> 8);
}
