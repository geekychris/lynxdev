/* lynxtext.h: one Suzy chain per call; the first SCB loads size and pens,
   the rest reuse them (SCB_RENONE + REUSEPAL). */
#include <string.h>
#include <tgi.h>
#include "lynxtext.h"

extern const unsigned char font3x5_data[64][16];

static SCB_REHV_PAL head;
static SCB_RENONE rest[TEXT_SMALL_MAX - 1];

uint8_t text_small(int16_t x, int16_t y, uint8_t c, const char *s)
{
    uint8_t n = 0, ch, i;
    void *prev = 0;
    for (; *s && n < TEXT_SMALL_MAX; ++s, ++n, x += TEXT_SMALL_W) {
        ch = (uint8_t)*s;
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        if (ch < 32 || ch > 95) ch = '?';
        if (n == 0) {
            head.sprctl0 = BPP_1 | TYPE_NORMAL;
            head.sprctl1 = LITERAL | REHV;
            head.sprcoll = NO_COLLIDE;
            head.next = 0;
            head.data = (unsigned char *)font3x5_data[ch - 32];
            head.hpos = x;
            head.vpos = y;
            head.hsize = 0x100;
            head.vsize = 0x100;
            head.penpal[0] = (uint8_t)(c & 15);
            for (i = 1; i < 8; ++i) head.penpal[i] = 0;
            prev = &head;
        } else {
            SCB_RENONE *r = &rest[n - 1];
            r->sprctl0 = BPP_1 | TYPE_NORMAL;
            r->sprctl1 = LITERAL | RENONE | REUSEPAL;
            r->sprcoll = NO_COLLIDE;
            r->next = 0;
            r->data = (unsigned char *)font3x5_data[ch - 32];
            r->hpos = x;
            r->vpos = y;
            ((SCB_RENONE *)prev)->next = (char *)r;
            prev = r;
        }
    }
    if (n) tgi_sprite(&head);
    return (uint8_t)(n * TEXT_SMALL_W);
}

uint8_t text_small_u(int16_t x, int16_t y, uint8_t c, uint16_t v, uint8_t digits)
{
    char d[6];
    uint8_t k = 5;
    d[5] = 0;
    do { d[--k] = (char)('0' + v % 10); v /= 10; } while (v && k);
    while (digits > 5 - k && k) d[--k] = '0';
    return text_small(x, y, c, d + k);
}

void text_small_centre(int16_t y, uint8_t c, const char *s)
{
    text_small((int16_t)((LYNX_SCREEN_W - TEXT_SMALL_W * (int16_t)strlen(s)) / 2), y, c, s);
}
