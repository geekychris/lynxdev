/*
 * sdktest - exercises the SDK so selftest catches regressions: Suzy sprites
 * (1:1, scaled, flipped, recoloured, a linked chain), palette changes, text,
 * Mikey sound (tone with decay), random numbers and input. test.py checks
 * pixels in the screenshot and the log lines.
 */
#include "lynxgame.h"
#include "ship.h"
#include "block.h"

static SCB_REHV_PAL ship, ship2, ship3, big, c1, c2, c3, bar;

static void draw(void)
{
    gfx_clear(COLOR_BLACK);
    spr_draw(&ship);
    spr_draw(&ship2);
    spr_draw(&ship3);
    spr_draw(&big);
    spr_draw(&c1);                       /* draws c1 -> c2 -> c3 */
    gfx_rect(130, 60, 10, 10, 14);
    gfx_text(8, 88, COLOR_WHITE, "SDK OK");
    text_small(80, 90, COLOR_YELLOW, "Score");
    text_small_u(104, 90, COLOR_WHITE, 42, 5);
    gfx_present();
}

int main(void)
{
    uint8_t playing = 1;
    uint16_t a, b, c;

    gfx_init();
    snd_init();

    spr_init(&ship, ship_data, SHIP_BPP);
    spr_pos(&ship, 10, 10);
    spr_init(&ship2, ship_data, SHIP_BPP);
    spr_pos(&ship2, 30, 10);
    spr_scale(&ship2, SPR_SCALE_2, SPR_SCALE_2);
    spr_init(&ship3, ship_data, SHIP_BPP);
    spr_pos(&ship3, 60, 40);
    spr_flip(&ship3, 0, 1);              /* upside down: drawn upwards from y=40 */
    spr_init(&big, block_data, BLOCK_BPP);
    spr_color(&big, COLOR_RED);
    spr_pos(&big, 100, 10);
    spr_scale(&big, 0x0400, 0x0400);     /* 4x4 pixels -> 16x16 */
    spr_init(&c1, block_data, BLOCK_BPP);
    spr_init(&c2, block_data, BLOCK_BPP);
    spr_init(&c3, block_data, BLOCK_BPP);
    spr_color(&c1, COLOR_GREEN);
    spr_color(&c2, COLOR_BLUE);
    spr_color(&c3, COLOR_WHITE);
    spr_pos(&c1, 10, 60);
    spr_pos(&c2, 20, 60);
    spr_pos(&c3, 30, 60);
    spr_link(&c1, &c2);
    spr_link(&c2, &c3);
    spr_box(&bar, 60, 70, 37, 3, COLOR_PEACH);   /* 37x3 exactly */
    spr_link(&c3, &bar);
    gfx_color(14, 15, 0, 15);            /* pen 14 -> magenta */

    rnd_seed(1);
    a = rnd16(); b = rnd16(); c = rnd_range(10);
    lynx_log("sdktest: rnd %u %u %u\n", a, b, c);

    {
        uint32_t m = math_mul16(300, 400), q = math_div32(100000UL, 7);
        lynx_log("sdktest: math mul=%lu div=%lu rem=%u\n", m, q, math_rem);
        m = math_mul16(0xFFFF, 0xFFFF);
        lynx_log("sdktest: math big=%lu\n", m);
        lynx_log("sdktest: box %u %u\n", (unsigned)box_hit(10, 10, 8, 8, 15, 15, 8, 8),
                 (unsigned)box_hit(10, 10, 8, 8, 18, 10, 8, 8));
    }
    lynx_log("sdktest: period 440=%x 16=%x 20000=%x\n", snd_period(440), snd_period(16), snd_period(20000));
    lynx_log("sdktest: SND_PERIOD 440=%x 16=%x 20000=%x\n", SND_PERIOD(440), SND_PERIOD(16), SND_PERIOD(20000));
    lynx_log_s("sdktest: log_u ");
    lynx_log_u(0); lynx_log_c(' '); lynx_log_u(7); lynx_log_c(' '); lynx_log_u(10); lynx_log_c(' ');
    lynx_log_u(65535); lynx_log_c(' '); lynx_log_u(1009); lynx_log_c('\n');
    snd_tone(0, 440, 100, 2);
    lynx_log("sdktest: sound on vol=%u\n", (unsigned)LYNX_AUD(0, LYNX_AUD_VOL));

    draw();
    draw();                              /* both buffers hold the picture */
    lynx_log("sdktest: drawn\n");

    for (;;) {
        input_update();
        snd_tick();
        if (playing && !(snd_active() & 1)) {
            playing = 0;
            lynx_log("sdktest: sound off frame=%u\n", frame_count);
        }
        if (input_new & IN_A) {
            snd_noise(3, 2, 80, 8);
            lynx_log("sdktest: noise\n");
        }
        if (input_new & IN_OPT1) lynx_log("sdktest: option1\n");
        if (input_new & IN_OPT2) {
            snd_tone(1, 2000, 60, 0);
            snd_sweep(1, -100);
            lynx_log("sdktest: sweep\n");
        }
        if (input_new & IN_PAUSE) lynx_log("sdktest: pause\n");
        frame_wait();
    }
}
