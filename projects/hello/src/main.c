/*
 * hello - the smallest lynxdev program that shows the whole loop: TGI
 * screen, palette, text, a moving box, joypad input and harness log lines.
 *
 * Log lines (tests and `./lynx log hello` read them):
 *   hello: ready harness=1        after the first frame is shown
 *   hello: frame n=60             once a second
 *   hello: move x=.. y=..         joypad
 *   hello: button a / b           A / B pressed
 *   LYNX:DONE                     after 120 frames (no test.py needed)
 */
#include "lynxgame.h"

int main(void)
{
    uint8_t x = 76, y = 60, done = 0;
    uint16_t last = 0;

    gfx_init();
    gfx_color(COLOR_DARKGREY, 1, 2, 6);          /* a dark blue background */
    gfx_clear(COLOR_DARKGREY);
    gfx_text_centre(20, COLOR_WHITE, "HELLO LYNX");
    gfx_text_centre(36, COLOR_YELLOW, "lynxdev");
    gfx_present();
    lynx_log("hello: ready harness=%u\n", lynx_in_harness());

    for (;;) {
        input_update();
        if (input_held & IN_DIRS) {
            if ((input_held & IN_LEFT) && x > 0) --x;
            if ((input_held & IN_RIGHT) && x < LYNX_SCREEN_W - 8) ++x;
            if ((input_held & IN_UP) && y > 48) --y;
            if ((input_held & IN_DOWN) && y < LYNX_SCREEN_H - 8) ++y;
            lynx_log("hello: move x=%u y=%u\n", x, y);
        }
        if (input_new & IN_A) lynx_log("hello: button a\n");
        if (input_new & IN_B) lynx_log("hello: button b\n");

        gfx_clear(COLOR_DARKGREY);
        gfx_text_centre(20, COLOR_WHITE, "HELLO LYNX");
        gfx_text_centre(36, COLOR_YELLOW, "lynxdev");
        gfx_rect(x, y, 8, 8, (input_held & IN_A) ? COLOR_RED : COLOR_GREEN);
        gfx_frame(0, 0, LYNX_SCREEN_W, LYNX_SCREEN_H, COLOR_LIGHTBLUE);
        gfx_present();

        if (frame_count - last >= 60) {
            last = frame_count;
            lynx_log("hello: frame n=%u\n", frame_count);
        }
        if (!done && frame_count >= 120) {
            done = 1;
            lynx_done();
        }
    }
}
