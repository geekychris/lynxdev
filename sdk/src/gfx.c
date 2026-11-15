/* lynxgfx.h: TGI-based screen, palette, shapes and text. */
#include <string.h>
#include "lynxgfx.h"

void gfx_init(void)
{
    tgi_install(tgi_static_stddrv);
    tgi_init();
    frame_init();
    tgi_setframerate(60);
    tgi_setbgcolor(COLOR_TRANSPARENT);
    gfx_clear(COLOR_BLACK);
    gfx_present();
    gfx_clear(COLOR_BLACK);
}

uint8_t __fastcall__ gfx_set_rate(uint8_t hz)
{
    tgi_setframerate(hz);
    return tgi_geterror() == TGI_ERR_OK ? 0 : 1;
}

void __fastcall__ gfx_clear(uint8_t color)
{
    tgi_setcolor(color);
    tgi_bar(0, 0, LYNX_SCREEN_W - 1, LYNX_SCREEN_H - 1);
}

void gfx_present(void)
{
    while (tgi_busy()) ;
    tgi_updatedisplay();
    while (tgi_busy()) ;
}

void frame_wait(void)
{
    uint8_t f = (uint8_t)frame_count;
    while ((uint8_t)frame_count == f) ;
}

void gfx_color(uint8_t idx, uint8_t r, uint8_t g, uint8_t b)
{
    LYNX_GREEN(idx) = g & 15;
    LYNX_BLUERED(idx) = (uint8_t)((b << 4) | (r & 15));
}

void __fastcall__ gfx_palette(const uint16_t *rgb16)
{
    uint8_t i;
    uint16_t c;
    for (i = 0; i < 16; ++i) {
        c = rgb16[i];
        LYNX_GREEN(i) = (uint8_t)(c >> 4) & 15;
        LYNX_BLUERED(i) = (uint8_t)(((c & 15) << 4) | ((c >> 8) & 15));
    }
}

void __fastcall__ gfx_palette_gbr(const uint16_t *gbr16)
{
    uint8_t i;
    uint16_t c;
    for (i = 0; i < 16; ++i) {
        c = gbr16[i];
        LYNX_GREEN(i) = (uint8_t)(c >> 8) & 15;
        LYNX_BLUERED(i) = (uint8_t)c;
    }
}

void gfx_palette_default(void)
{
    tgi_setpalette(tgi_getdefpalette());
}

void gfx_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t color)
{
    if (!w || !h) return;
    tgi_setcolor(color);
    tgi_bar(x, y, x + w - 1, y + h - 1);
}

void gfx_frame(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t color)
{
    gfx_rect(x, y, w, 1, color);
    gfx_rect(x, y + h - 1, w, 1, color);
    gfx_rect(x, y, 1, h, color);
    gfx_rect(x + w - 1, y, 1, h, color);
}

void gfx_line(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t color)
{
    tgi_setcolor(color);
    tgi_line(x1, y1, x2, y2);
}

void gfx_pixel(uint8_t x, uint8_t y, uint8_t color)
{
    tgi_setcolor(color);
    tgi_setpixel(x, y);
}

void gfx_text(uint8_t x, uint8_t y, uint8_t color, const char *s)
{
    tgi_setcolor(color);
    tgi_outtextxy(x, y, s);
}

void gfx_text_u(uint8_t x, uint8_t y, uint8_t color, uint16_t v)
{
    char d[6];
    uint8_t n = 5;
    d[5] = 0;
    do { d[--n] = (char)('0' + v % 10); v /= 10; } while (v);
    gfx_text(x, y, color, d + n);
}

void gfx_text_centre(uint8_t y, uint8_t color, const char *s)
{
    gfx_text((uint8_t)((LYNX_SCREEN_W - 8 * strlen(s)) / 2), y, color, s);
}
