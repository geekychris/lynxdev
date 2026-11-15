/* lynxspr.h */
#include <string.h>
#include <tgi.h>
#include "lynxspr.h"

const uint8_t spr_pens_identity[8] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF };

/* packed: one line of 3 bytes (offset), a literal packet of 1 pixel with
   pen 1 (1 0000 1), the end-of-line packet (0 0000), then end of sprite. */
const uint8_t spr_pixel_data[4] = { 3, 0x84, 0x00, 0x00 };

static uint8_t bpp_bits(uint8_t bpp)
{
    return (uint8_t)(((bpp - 1) & 3) << 6);
}

void spr_init(SCB_REHV_PAL *s, const uint8_t *data, uint8_t bpp)
{
    s->sprctl0 = bpp_bits(bpp) | TYPE_NORMAL;
    s->sprctl1 = LITERAL | REHV;
    s->sprcoll = NO_COLLIDE;
    s->next = 0;
    s->data = (unsigned char *)data;
    s->hpos = 0;
    s->vpos = 0;
    s->hsize = SPR_SCALE_1;
    s->vsize = SPR_SCALE_1;
    memcpy(s->penpal, spr_pens_identity, 8);
}

void spr_init_nopal(SCB_REHV *s, const uint8_t *data, uint8_t bpp)
{
    s->sprctl0 = bpp_bits(bpp) | TYPE_NORMAL;
    s->sprctl1 = LITERAL | REHV | REUSEPAL;
    s->sprcoll = NO_COLLIDE;
    s->next = 0;
    s->data = (unsigned char *)data;
    s->hpos = 0;
    s->vpos = 0;
    s->hsize = SPR_SCALE_1;
    s->vsize = SPR_SCALE_1;
}

void spr_box(SCB_REHV_PAL *s, int16_t x, int16_t y, uint8_t w, uint8_t h, uint8_t c)
{
    uint8_t i;
    s->sprctl0 = BPP_1 | TYPE_NONCOLL;
    s->sprctl1 = PACKED | REHV;
    s->sprcoll = NO_COLLIDE;
    s->next = 0;
    s->data = (unsigned char *)spr_pixel_data;
    s->hpos = x;
    s->vpos = y;
    s->hsize = (uint16_t)w << 8;
    s->vsize = (uint16_t)h << 8;
    s->penpal[0] = (uint8_t)(c & 15);       /* pen 0 -> 0, pen 1 -> c */
    for (i = 1; i < 8; ++i) s->penpal[i] = 0;
}

void spr_color(SCB_REHV_PAL *s, uint8_t c)
{
    uint8_t i, b = (uint8_t)((c << 4) | c);
    s->penpal[0] = (uint8_t)(c & 15);         /* pen 0 stays 0: transparent */
    for (i = 1; i < 8; ++i) s->penpal[i] = b;
}

void spr_pen(SCB_REHV_PAL *s, uint8_t p, uint8_t c)
{
    uint8_t *q = &s->penpal[p >> 1];
    if (p & 1) *q = (uint8_t)((*q & 0xF0) | (c & 15));
    else *q = (uint8_t)((*q & 0x0F) | (c << 4));
}

void __fastcall__ spr_draw(void *scb)
{
    tgi_sprite(scb);
}
