/*
 * lynxgfx.h - screen, palette, drawing and text on top of cc65's TGI driver.
 *
 * The display is 160x102, 4 bits per pixel (16 colours out of 4096). The
 * TGI driver double-buffers at $C038 and $E018 and draws everything with
 * Suzy (bars, lines and text are sprites), so it is fast. Draw into the back
 * buffer, then gfx_present() shows it at the next vertical blank.
 *
 *   gfx_init();                      // 60 Hz, default palette
 *   for (;;) {
 *       gfx_clear(COLOR_BLACK);
 *       gfx_text(8, 8, COLOR_WHITE, "HELLO");
 *       gfx_present();               // waits for the vertical blank
 *   }
 *
 * Colours 0-15; 0 is transparent for sprites and text backgrounds.
 */
#ifndef LYNXGFX_H
#define LYNXGFX_H

#include <tgi.h>
#include "lynxhw.h"

/* Frames since frame_init/gfx_init (vertical blank interrupts). */
extern volatile uint16_t frame_count;

/* Install the TGI driver, 60 Hz, the default palette, clear both buffers. */
void gfx_init(void);
/* Refresh rate 50, 60 (default) or 75 Hz. Returns 0 on success. */
uint8_t __fastcall__ gfx_set_rate(uint8_t hz);
/* Fill the back buffer with a colour. */
void __fastcall__ gfx_clear(uint8_t color);
/* Show the back buffer at the next vertical blank (waits for it); the other
   buffer becomes the back buffer. Call once per frame. */
void gfx_present(void);
/* Set palette entry idx (0-15) to r, g, b (0-15 each). */
void gfx_color(uint8_t idx, uint8_t r, uint8_t g, uint8_t b);
/* Load 16 colours as 12-bit 0xRGB values (0x0F00 = red). */
void __fastcall__ gfx_palette(const uint16_t *rgb16);
/* Shapes in the back buffer. */
void gfx_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t color);
void gfx_frame(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t color);
void gfx_line(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t color);
void gfx_pixel(uint8_t x, uint8_t y, uint8_t color);
/* 8x8 text (20 columns x 12 rows). Background transparent. */
void gfx_text(uint8_t x, uint8_t y, uint8_t color, const char *s);
void gfx_text_u(uint8_t x, uint8_t y, uint8_t color, uint16_t v);
/* Centred horizontally on the screen. */
void gfx_text_centre(uint8_t y, uint8_t color, const char *s);

/* Wait for the next vertical blank (without swapping buffers). */
void frame_wait(void);
/* Enable the vertical-blank interrupt and the frame counter (gfx_init does). */
void frame_init(void);

#endif
