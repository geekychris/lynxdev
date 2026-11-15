/*
 * __NAME__ - made from sdk/template (./lynx new).
 *
 * Main-loop skeleton: a 60 Hz loop (gfx_present waits for the vertical
 * blank), a box moved with the joypad, A changes its colour, and lynx_log
 * lines the harness and test.py wait for.
 */
#include "lynxgame.h"

int main(void)
{
    uint8_t x = 76, y = 47, c = COLOR_YELLOW;

    gfx_init();
    lynx_log("__NAME__: ready\n");

    for (;;) {
        input_update();
        if (input_held & IN_DIRS) {
            if ((input_held & IN_LEFT) && x > 0) --x;
            if ((input_held & IN_RIGHT) && x < LYNX_SCREEN_W - 8) ++x;
            if ((input_held & IN_UP) && y > 12) --y;
            if ((input_held & IN_DOWN) && y < LYNX_SCREEN_H - 8) ++y;
            lynx_log("__NAME__: move x=%u y=%u\n", x, y);
        }
        if (input_new & IN_A) {
            c = (uint8_t)(c == COLOR_WHITE ? COLOR_RED : c + 1);
            lynx_log("__NAME__: colour c=%u frame=%u\n", c, frame_count);
        }
        gfx_clear(COLOR_DARKGREY);
        gfx_text(4, 2, COLOR_WHITE, "__NAME__");
        gfx_rect(x, y, 8, 8, c);
        gfx_present();
    }
}
