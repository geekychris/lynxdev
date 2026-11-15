/*
 * lynxtext.h - a small 3x5 font (4x6 cells: 40 columns x 17 rows) drawn
 * with Suzy, for scores and HUDs where TGI's 8x8 font (gfx_text, 20
 * columns) is too big. ASCII 32-95; lower case prints as upper case.
 *
 *   text_small(2, 2, COLOR_WHITE, "SCORE");
 *   text_small_u(26, 2, COLOR_YELLOW, score, 5);   // zero-padded to 5 digits
 *
 * Each call draws one sprite chain (at most TEXT_SMALL_MAX characters).
 */
#ifndef LYNXTEXT_H
#define LYNXTEXT_H

#include "lynxhw.h"

#define TEXT_SMALL_W   4      /* cell width in pixels */
#define TEXT_SMALL_H   6
#define TEXT_SMALL_MAX 40

/* Draw s at (x, y) in colour c. Returns the width drawn in pixels. */
uint8_t text_small(int16_t x, int16_t y, uint8_t c, const char *s);
/* An unsigned number; digits > 0 pads with zeros to that many digits. */
uint8_t text_small_u(int16_t x, int16_t y, uint8_t c, uint16_t v, uint8_t digits);
/* Centred on the screen. */
void text_small_centre(int16_t y, uint8_t c, const char *s);

#endif
