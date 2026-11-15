/*
 * lynxspr.h - Suzy hardware sprites.
 *
 * A sprite is a Suzy sprite control block (SCB) pointing at sprite data.
 * Suzy draws a whole chain of SCBs (linked by `next`) into the back buffer
 * in one go, scaling each one in hardware (hsize/vsize, 0x100 = 1.0) and
 * mapping its pens through an 8-byte pen palette.
 *
 *   #include "ship.h"                     // made by ./lynx sprite ship.txt
 *   static SCB_REHV_PAL ship;
 *   spr_init(&ship, ship_data, 4);        // 4 bpp, normal, 1:1, identity pens
 *   ...each frame:
 *   spr_pos(&ship, x, y);
 *   spr_draw(&ship);                      // or link several and draw the head
 *
 * Sprite data comes from the converter (`./lynx sprite in.png|in.txt`,
 * tools/lynxdev/src/lynxdev/sprite.py): literal lines, each starting with
 * its length byte, ending with a 0 byte. Pen 0 is transparent.
 *
 * Coordinates are signed 16-bit (sprites may hang off any edge). The
 * position is the sprite's top-left pixel unless the data was made with an
 * action point (--anchor).
 */
#ifndef LYNXSPR_H
#define LYNXSPR_H

#include "lynxhw.h"

#define SPR_SCALE_1   0x0100u
#define SPR_SCALE_2   0x0200u
#define SPR_SCALE_HALF 0x0080u

/* The identity pen palette (pen n -> colour n). */
extern const uint8_t spr_pens_identity[8];

/* Set up a REHV_PAL sprite: bpp 1, 2, 3 or 4, TYPE_NORMAL (pen 0 transparent),
   literal data, scale 1:1, identity pens, not linked, at (0, 0). */
void spr_init(SCB_REHV_PAL *s, const uint8_t *data, uint8_t bpp);
/* The same for a plain SCB_REHV (no pen palette: Suzy keeps the previous one). */
void spr_init_nopal(SCB_REHV *s, const uint8_t *data, uint8_t bpp);

#define spr_pos(s, x, y)     ((s)->hpos = (x), (s)->vpos = (y))
#define spr_scale(s, sx, sy) ((s)->hsize = (sx), (s)->vsize = (sy))
#define spr_link(s, n)       ((s)->next = (char *)(n))
#define spr_data(s, d)       ((s)->data = (unsigned char *)(d))
/* Flip: h and/or v (mirrors around the sprite's position). */
#define spr_flip(s, h, v)    ((s)->sprctl0 = ((s)->sprctl0 & ~(HFLIP | VFLIP)) | ((h) ? HFLIP : 0) | ((v) ? VFLIP : 0))
/* Hide a sprite in a chain without unlinking it. */
#define spr_hide(s, on)      ((s)->sprctl1 = ((s)->sprctl1 & ~SKIP) | ((on) ? SKIP : 0))
/* One colour for the whole sprite: every non-zero pen -> colour c (1 bpp art). */
void spr_color(SCB_REHV_PAL *s, uint8_t c);
/* Map pen p to colour c. */
void spr_pen(SCB_REHV_PAL *s, uint8_t p, uint8_t c);

/* A solid w x h box of colour c at (x, y) as a sprite (a 1-pixel sprite
   scaled by Suzy), to put in a chain: bars, backgrounds, HUD panels.
   Exactly w x h whatever its type: don't build bars from 1-bpp *literal*
   art instead (each data byte is 8 pixels, the 7 extra are pen 0: invisible
   in TYPE_NORMAL, drawn in TYPE_BACKGROUND, making the bar ~8x too wide). */
void spr_box(SCB_REHV_PAL *s, int16_t x, int16_t y, uint8_t w, uint8_t h, uint8_t c);
#define spr_box_size(s, w, h) spr_scale(s, (uint16_t)(w) << 8, (uint16_t)(h) << 8)
/* The packed 1-pixel (pen 1) sprite data spr_box uses. */
extern const uint8_t spr_pixel_data[4];

/* Draw a sprite (and the chain it heads) into the back buffer; waits for Suzy. */
void __fastcall__ spr_draw(void *scb);

#endif
