#ifndef SCORE_H
#define SCORE_H
#include <stdint.h>
void score_init(void);
void score_add(uint16_t points);
extern uint32_t score_current, score_hi;
extern uint8_t score_lives, score_level;
/* port (speed): the score and the high score as 6 decimal digits ('0'..'9'),
   kept beside the numbers so the HUD needs no 32-bit division */
extern char score_dig[6], hi_dig[6];
void score_lose_life(void);
void score_next_level(void);
#endif
