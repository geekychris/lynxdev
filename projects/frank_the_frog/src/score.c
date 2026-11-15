/* Frank the Frog - score, lives, level (from score.c; the HUD is in render.c) */
#include <string.h>
#include "score.h"

uint32_t score_current, score_hi;
uint8_t score_lives = 3, score_level = 1;
char score_dig[6] = "000000", hi_dig[6] = "000000";

void score_init(void)
{
    score_current = 0;
    memset(score_dig, '0', 6);
    score_lives = 3;
    score_level = 1;
}

/* the digits: add n (0..9) at position pos, carrying left (stops at 999999) */
static void dig_add(uint8_t pos, uint8_t n)
{
    char c;
    for (;;) {
        c = (char)(score_dig[pos] + n);
        if (c <= '9') {
            score_dig[pos] = c;
            return;
        }
        score_dig[pos] = (char)(c - 10);
        if (pos == 0) {
            memset(score_dig, '9', 6);
            return;
        }
        --pos;
        n = 1;
    }
}

void score_add(uint16_t points)
{
    static const uint16_t pow10[5] = { 10000, 1000, 100, 10, 1 };
    uint8_t i, n;
    score_current += points;
    for (i = 0; i < 5; ++i) {
        n = 0;
        while (points >= pow10[i]) { points -= pow10[i]; ++n; }
        if (n) dig_add(i + 1, n);
    }
    if (score_current > score_hi) {
        score_hi = score_current;
        memcpy(hi_dig, score_dig, 6);
    }
}

void score_lose_life(void)
{
    if (score_lives > 0) score_lives--;
}

void score_next_level(void)
{
    score_level++;
    score_add(1000);
}
