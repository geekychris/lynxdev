/*
 * Rock Blaster (Atari Lynx). Port of amiga_mcp/examples/rock_blaster.
 *
 * game.c is the original's logic; this file is the Lynx front end: Suzy
 * sprites for everything that moves (one chain of 104 SCBs a frame), the
 * title screen with the logo zoomed in by Suzy's scaling, the HUD, pause,
 * Mikey sound effects and the original's tune, the high score, and the log
 * lines the test reads. The game runs at the original's 50 Hz, one update
 * a frame.
 */
#include <string.h>
#include "lynxgame.h"
#include "game.h"
#include "art.h"
#include "music.h"

#pragma static-locals (on)

/* ------------------------------------------------------------ palette */

/* The original's colours (0xRGB). port: the Lynx's pen 0 is transparent to
   sprites and TGI, so the background is pen 1 and the original's purple
   (unused) is gone. */
static const uint16_t palette[16] = {
    0x001, 0x001, 0xFFF, 0x08F, 0x0F8, 0x0F0, 0x888, 0xAAA,
    0xCCC, 0xF00, 0xF80, 0xFF0, 0xF0F, 0x444, 0xFA0, 0xFE0,
};

/* ------------------------------------------------------------ sprites */

/* Two sets of SCBs, one per screen buffer. port (speed): the screen isn't
   cleared every frame (a full-screen Suzy fill is a third of a frame);
   instead the set drawn into this buffer two frames ago is drawn again in
   the background colour (erase_begin/erase_end), then updated and drawn.
   objs.s writes the rocks', bullets' and particles' SCBs of the current set
   as it moves them. Frames with text over the playfield clear in full. */
#define N_LIVES_ICONS 5
#define N_HUD 10                          /* 7 score digits, L, 2 level digits */
#define S_ROCK  0
#define S_BUL   (S_ROCK + MAX_ROCKS)
#define S_PART  (S_BUL + MAX_BULLETS)
#define S_FLAME (S_PART + MAX_PARTICLES)
#define S_SHIP  (S_FLAME + 1)
#define S_LIFE  (S_SHIP + 1)
#define S_HUD   (S_LIFE + N_LIVES_ICONS)
#define N_SCB   (S_HUD + N_HUD)           /* 113 */
SCB_REHV_PAL scbs[2][N_SCB];
uint16_t scb_off;                         /* objs.s: the current set's offset */
static SCB_REHV_PAL *cs;                  /* the current set */
static uint8_t cur, full_clear;
static SCB_REHV_PAL logo1, logo2;

static void sprites_init(void)
{
    uint8_t i, set;
    SCB_REHV_PAL *s;
    for (set = 0; set < 2; ++set) {
        for (i = 0; i < N_SCB; ++i) {
            s = &scbs[set][i];
            if (i < S_BUL) spr_init(s, rock_frames[0], 1);
            else if (i < S_FLAME) { spr_init(s, dot, 1); spr_color(s, C_WHITE); }
            else if (i == S_FLAME) { spr_init(s, flame_frames[0], 1); spr_color(s, C_ORANGE); }
            else if (i == S_SHIP) { spr_init(s, ship_frames[0], 1); spr_color(s, C_WHITE); }
            else if (i < S_HUD) {
                spr_init(s, ship_frames[16], 1);          /* pointing down, flipped: up */
                spr_flip(s, 0, 1);
                spr_color(s, C_WHITE);
                s->hpos = 4 + 7 * (i - S_LIFE) - 6;      /* centre (4 + 7i, 14) */
                s->vpos = 14 + 6;
            } else {
                spr_init(s, digit_frames[0], 1);
                spr_color(s, C_WHITE);
                s->vpos = 2;
                s->hpos = i - S_HUD < 7 ? 2 + 6 * (i - S_HUD) : 160 - 2 - 6 * (N_SCB - i);
            }
            spr_hide(s, 1);
            if (i) spr_link(s - 1, s);
        }
    }
    cs = scbs[0];
    spr_init(&logo1, title_rock, 1);
    spr_color(&logo1, C_YELLOW);
    spr_init(&logo2, title_blaster, 1);
    spr_color(&logo2, C_WHITE);
    spr_link(&logo1, &logo2);
}

/* hide a range of SCBs in both sets */
static void hide_both(uint8_t from, uint8_t n)
{
    SCB_REHV_PAL *a = &scbs[0][from], *b = &scbs[1][from];
    for (; n; --n, ++a, ++b) {
        a->sprctl1 |= SKIP;
        b->sprctl1 |= SKIP;
    }
}

/* The ship's picture: 17 frames for 0..90 degrees, Suzy's flips for the
   rest. A flipped sprite mirrors around its position, so the position is
   the centre pixel's mirror (centre +6 instead of -6). */
static void ship_sprite(SCB_REHV_PAL *s, const unsigned char * const *frames, uint8_t angle,
                        int16_t cx, int16_t cy)
{
    uint8_t q = (uint8_t)((angle + 2) >> 2) & 63, r = q & 15, f, h = 0, v = 0;
    switch (q >> 4) {
    case 0: f = r; break;
    case 1: f = 16 - r; h = 1; break;
    case 2: f = r; h = 1; v = 1; break;
    default: f = 16 - r; v = 1; break;
    }
    spr_data(s, frames[f]);
    spr_flip(s, h, v);
    s->hpos = h ? cx + 6 : cx - 6;
    s->vpos = v ? cy + 6 : cy - 6;
}

/* the ship and its flame (objs.s does the rocks, bullets, particles) */
static void sprites_update(void)
{
    int16_t cx, cy;
    SCB_REHV_PAL *ship = &cs[S_SHIP], *flame = &cs[S_FLAME];

    /* the ship blinks while it can't be hit (as the original) */
    if (!ship_alive || (ship_invuln > 0 && (gs_frame & 4))) {
        spr_hide(ship, 1);
        spr_hide(flame, 1);
    } else {
        cx = (int16_t)(ship_x >> 17);
        cy = (int16_t)(ship_y >> 17);
        spr_hide(ship, 0);
        ship_sprite(ship, ship_frames, ship_angle, cx, cy);
        if (ship_thrust_on && (gs_frame & 2)) {
            spr_hide(flame, 0);
            ship_sprite(flame, flame_frames, ship_angle, cx, cy);
        } else {
            spr_hide(flame, 1);
        }
    }
}

/* the HUD's sprites (both sets): score digits, level, lives */
static void hud_set(SCB_REHV_PAL *s)
{
    uint8_t i;
    for (i = 0; i < N_LIVES_ICONS; ++i, ++s)
        spr_hide(s, i >= (uint8_t)gs_lives);
    hud_digits(s);                        /* objs.s */
    s += 7;
    s[0].data = (unsigned char *)digit_frames[10];
    s[0].hpos = gs_level < 10 ? 160 - 2 - 12 : 160 - 2 - 18;    /* "L5", "L12" */
    s[1].data = (unsigned char *)digit_frames[gs_level / 10 % 10];
    s[2].data = (unsigned char *)digit_frames[gs_level % 10];
    spr_hide(&s[1], gs_level < 10);
}

/* The HUD changed: each set takes it when it is the current one (after its
   erase, which must draw what that buffer shows: the old digits). */
static uint8_t hud_dirty;
static void hud_update(void)
{
    hud_dirty = 2;
}

/* on (game) or off (title) */
static void hud_show(uint8_t on)
{
    uint8_t i, set;
    for (set = 0; set < 2; ++set)
        for (i = S_HUD; i < N_SCB; ++i) spr_hide(&scbs[set][i], !on);
    if (on) {
        hud_set(&scbs[0][S_LIFE]);
        hud_set(&scbs[1][S_LIFE]);
    } else {
        hide_both(S_LIFE, N_LIVES_ICONS);
    }
}

/* ------------------------------------------------------------ sound */

/* Mikey channels 0 and 1: effects; 2 and 3: the tune. port: the original's
   sampled effects as Mikey tones and noise with the same shape (a falling
   blip, noise bursts long and short, a falling tone with noise).
   Channel 0's tones are set up here from timer values (the SDK's snd_tone
   takes Hz: a 32-bit divide, too slow for every shot); they fall in pitch
   by lengthening the timer's period each frame. Channel 1: snd_noise. */
static uint8_t t0_bkup, t0_vol, t0_decay, t0_step;

/* a square wave of 1 MHz / 2^sel / (2 * (bkup + 1)) Hz */
static void tone0(uint8_t bkup, uint8_t sel, uint8_t vol, uint8_t decay, uint8_t step)
{
    LYNX_AUD(0, LYNX_AUD_CTLA) = 0;
    LYNX_AUD(0, LYNX_AUD_FEED) = 0x01;
    LYNX_AUD(0, LYNX_AUD_SHIFT) = 0;
    LYNX_AUD(0, LYNX_AUD_CTLB) = 0;
    LYNX_AUD(0, LYNX_AUD_VOL) = t0_vol = vol;
    LYNX_AUD(0, LYNX_AUD_BKUP) = t0_bkup = bkup;
    LYNX_AUD(0, LYNX_AUD_CTLA) = (uint8_t)(0x18 | sel);
    t0_decay = decay;
    t0_step = step;
}

static void tone0_off(void)
{
    LYNX_AUD(0, LYNX_AUD_VOL) = 0;
    LYNX_AUD(0, LYNX_AUD_CTLA) = 0;
    t0_vol = 0;
}

void sfx_shoot(void)         { tone0(155, 1, 60, 7, 12); }   /* 1600 Hz falling to ~1000 */
void sfx_explode_large(void) { snd_noise(1, 6, 110, 3); }
void sfx_explode_small(void) { snd_noise(1, 3, 80, 5); }
void sfx_die(void)
{
    snd_noise(1, 7, 127, 2);
    tone0(138, 2, 90, 2, 3);                                 /* 900 Hz falling to ~490 */
}

static void sfx_tick(void)
{
    if (!t0_vol) return;
    if (t0_vol <= t0_decay || t0_bkup > 255 - t0_step) { tone0_off(); return; }
    LYNX_AUD(0, LYNX_AUD_VOL) = t0_vol -= t0_decay;
    LYNX_AUD(0, LYNX_AUD_BKUP) = t0_bkup += t0_step;
}

static uint8_t music_on = 1;
static const uint8_t *mu_ptr[2];
static uint8_t mu_left[2], mu_vol[2];
static const uint8_t mu_vol0[2] = { 26, 30 };       /* lead, bass */

static void music_reset(void)
{
    mu_ptr[0] = music_lead;
    mu_ptr[1] = music_bass;
    mu_left[0] = mu_left[1] = 1;
}

static void music_silence(void)
{
    LYNX_AUD(2, LYNX_AUD_VOL) = 0;
    LYNX_AUD(3, LYNX_AUD_VOL) = 0;
    mu_vol[0] = mu_vol[1] = 0;
}

/* once a frame (50 Hz = the MOD's tick): next note when one ends, and a
   short decay to a sustain level so the notes are plucked, not organ-like */
static void music_tick(void)
{
    uint8_t c, n, ch;
    for (c = 0; c < 2; ++c) {
        ch = c + 2;
        if (--mu_left[c] == 0) {
            if (mu_ptr[c][1] == 0) { music_reset(); return; }   /* the end: again */
            n = mu_ptr[c][0];
            mu_left[c] = mu_ptr[c][1];
            mu_ptr[c] += 2;
            if (!n) { LYNX_AUD(ch, LYNX_AUD_VOL) = mu_vol[c] = 0; continue; }
            --n;
            LYNX_AUD(ch, LYNX_AUD_CTLA) = 0;
            LYNX_AUD(ch, LYNX_AUD_FEED) = 0x01;    /* square wave (lynxsnd.h) */
            LYNX_AUD(ch, LYNX_AUD_SHIFT) = 0;
            LYNX_AUD(ch, LYNX_AUD_CTLB) = 0;
            LYNX_AUD(ch, LYNX_AUD_BKUP) = note_bkup[n];
            LYNX_AUD(ch, LYNX_AUD_CTLA) = (uint8_t)(0x18 | note_sel[n]);
            LYNX_AUD(ch, LYNX_AUD_VOL) = mu_vol[c] = mu_vol0[c];
        } else if (mu_vol[c] > mu_vol0[c] / 2) {
            LYNX_AUD(ch, LYNX_AUD_VOL) = --mu_vol[c];
        }
    }
}

/* ------------------------------------------------------------ text */

static uint32_t hiscore = 1000;
static char hi_str[8] = "0001000";

/* ------------------------------------------------------------ log */

/* printf-style lines for rare events; lynx_log_s/u (no printf) in play */
#ifndef LYNX_NO_LOG
#define LOG(...) lynx_log(__VA_ARGS__)
static void logu(const char *k, uint16_t v) { lynx_log_s(k); lynx_log_u(v); }
/* key and a byte 0..255, without lynx_log_u's divides (it runs in play) */
static void logb(const char *k, uint8_t v)
{
    uint8_t h = 0, t = 0;
    lynx_log_s(k);
    while (v >= 100) { v -= 100; ++h; }
    while (v >= 10) { v -= 10; ++t; }
    if (h) lynx_log_c('0' + h);
    if (h || t) lynx_log_c('0' + t);
    lynx_log_c('0' + v);
}
#else
#define LOG(...) ((void)0)
#define logu(k, v) ((void)0)
#define logb(k, v) ((void)0)
#endif

/* ------------------------------------------------------------ title */

static uint8_t title_t;

/* the original's title stars (its LCG and seed), halved */
static void stars(void)
{
    uint32_t seed = 7777;
    uint8_t i;
    SCB_REHV_PAL *s;
    for (i = 0; i < 55; ++i) {
        seed = seed * 1103515245UL + 12345UL;
        s = &scbs[0][S_PART + i];
        s->hpos = (int16_t)(((uint16_t)(seed >> 16) % 320) >> 1);
        s->vpos = (int16_t)(((uint16_t)(seed >> 3) % 256) * 2 / 5);   /* 256 -> 102 */
        s->penpal[0] = i < 40 ? C_DARKGREY : C_GREY_DIM;
        s->sprctl1 &= ~SKIP;
        scbs[1][S_PART + i] = *s;
        scbs[1][S_PART + i].next = (char *)&scbs[1][S_PART + i + 1];
    }
}

static void title_enter(void)
{
    uint8_t i;
    gs_state = STATE_TITLE;
    title_t = 0;
    for (i = 0; i < MAX_ROCKS; ++i) rk_active[i] = 0;
    for (i = 0; i < MAX_BULLETS; ++i) bl_active[i] = 0;
    for (i = 0; i < MAX_PARTICLES; ++i) pt_active[i] = 0;
    particles_live = 0;
    ship_alive = 0;
    hide_both(0, S_LIFE);
    stars();
    hud_show(0);
    LOG("rb: title hi=%lu\n", hiscore);
}

static void logo_pos(SCB_REHV_PAL *s, uint8_t w, uint16_t scale, uint8_t y)
{
    s->hsize = s->vsize = scale;
    s->hpos = 80 - (int16_t)(((uint32_t)w * scale) >> 9);
    s->vpos = y + 10 - (int16_t)((7UL * scale) >> 9);
}

static void draw_title(void)
{
    /* the logo grows from nothing to 3x in Suzy's scaler */
    uint16_t sc = title_t < 32 ? (uint16_t)title_t * 24 : 0x300;
    logo_pos(&logo1, TITLE_ROCK_W, sc, 4);
    logo_pos(&logo2, TITLE_BLASTER_W, sc, 28);
    spr_draw(cs);                           /* the stars (particles' SCBs) */
    if (sc) spr_draw(&logo1);
    gfx_rect(20, 54, 120, 1, C_BLUE);
    gfx_rect(20, 56, 120, 1, C_BLUE);
    gfx_text(4, 60, C_GREY_LT, "LEFT/RIGHT  TURN");
    gfx_text(4, 69, C_GREY_LT, "UP/B THRUST A FIRE");
    gfx_text(4, 79, C_GREY_DIM, "HI");
    gfx_text(28, 79, C_YELLOW, hi_str);
    if (title_t & 32)
        gfx_text_centre(91, C_BRIGHT_YELLOW, "PRESS A TO START");
}

/* ------------------------------------------------------------ main */

static uint8_t paused;
static uint16_t fps_vbl, fps_loops, late, last_vbl;
static uint8_t busy_max;

static void start_game(void)
{
    rnd_seed(frame_count | 1);              /* different rocks each game */
    game_init();
    hide_both(S_PART, MAX_PARTICLES);       /* the stars */
    sprites_update();
    hud_show(1);
    full_clear = 2;
    fps_vbl = frame_count;
    fps_loops = late = 0;
    busy_max = 0;
    LOG("rb: state=playing level=1 lives=%u\n", (unsigned)gs_lives);
}

static void game_over(void)
{
    uint8_t new_hi = gs_score > hiscore;
    if (new_hi) {
        hiscore = gs_score;
        memcpy(hi_str, gs_score_str, 8);
    }
    LOG("rb: gameover score=%lu level=%u hi=%lu new=%u\n", gs_score, gs_level, hiscore, new_hi);
}

int main(void)
{
    uint8_t prev_state, prev_lives, prev_level;
    uint16_t now;
    uint8_t busy, erased;

    gfx_init();
    gfx_set_rate(50);                       /* the original's PAL frame rate */
    gfx_palette(palette);
    snd_init();
    sprites_init();
    particles_table();
    music_reset();
    LOG("rb: ready hz=50\n");
    title_enter();

    for (;;) {
        input_update();

        /* --- erase what this buffer showed two frames ago (before the
           game's update writes this set) --- */
        erased = 0;
        if ((gs_state == STATE_PLAYING || gs_state == STATE_DEAD) && !paused && !full_clear) {
            erase_begin();                  /* objs.s: every pen -> background */
            spr_draw(cs);
            erase_end();
            erased = 1;
        }

        /* --- input and state changes outside the game's update --- */
        if (gs_state == STATE_TITLE) {
            if (input_new & IN_OPT1) {
                music_on ^= 1;
                if (!music_on) music_silence();
                LOG("rb: music=%u\n", (unsigned)music_on);
            }
            if (input_new & IN_A) {
                start_game();
                input_held |= IN_A;         /* that press doesn't fire */
            }
        } else if (input_new & IN_PAUSE) {
            paused ^= 1;
            if (paused) { snd_all_off(); tone0_off(); music_silence(); }
            LOG("rb: paused=%u\n", (unsigned)paused);
        }
        if (paused && (input_new & IN_OPT1)) {
            /* the Lynx way: PAUSE, then OPTION 1 = restart (here: the title) */
            paused = 0;
            title_enter();
        }

        if (gs_state != STATE_TITLE && !paused) {
            prev_state = gs_state;
            prev_lives = (uint8_t)gs_lives;
            prev_level = gs_level;
            /* port: B thrusts too (the d-pad's UP is awkward with A) */
            game_update((input_held & IN_LEFT) != 0, (input_held & IN_RIGHT) != 0,
                        (input_held & (IN_UP | IN_B)) != 0, (input_held & IN_A) != 0);
            if (gs_hits) {
                lynx_log_s("rb: score=");
                lynx_log_s(gs_score_str);
                logb(" rocks=", (uint8_t)gs_rock_count);
                lynx_log_c('\n');
            }
            if ((uint8_t)gs_lives != prev_lives) { logu("rb: lives=", (uint8_t)gs_lives); lynx_log_c('\n'); }
            if (gs_level != prev_level) {
                logu("rb: level=", gs_level);
                logu(" rocks=", (uint8_t)gs_rock_count);
                lynx_log_c('\n');
            }
            if (gs_hits || (uint8_t)gs_lives != prev_lives || gs_level != prev_level) hud_update();
            if (gs_state != prev_state) {
                if (gs_state == STATE_GAMEOVER) game_over();
                else if (gs_state == STATE_PLAYING) LOG("rb: respawn\n");
            }
            if (gs_state == STATE_GAMEOVER && gs_state_timer < 100 && (input_new & IN_A))
                gs_state = STATE_TITLE;     /* port: A leaves the game over early */
            if (gs_state == STATE_TITLE) title_enter();
        }

        /* --- draw --- */
        if (!erased) {
            gfx_clear(C_BG);
            if (full_clear) --full_clear;
        }
        if (gs_state == STATE_TITLE) {
            draw_title();
            ++title_t;
            full_clear = 2;
        } else {
            sprites_update();
            if (hud_dirty) {
                hud_set(&cs[S_LIFE]);
                --hud_dirty;
            }
            spr_draw(cs);
            if (gs_state == STATE_GAMEOVER || paused) full_clear = 2;   /* text: no erasing */
            if (gs_state == STATE_GAMEOVER) {
                gfx_text_centre(34, C_RED, "GAME OVER");
                gfx_text(16, 48, C_WHITE, "SCORE");
                gfx_text(64, 48, C_WHITE, gs_score_str);
                if (gs_score == hiscore && gs_score) gfx_text_centre(60, C_YELLOW, "NEW HIGH SCORE");
                gfx_text_centre(74, C_BRIGHT_YELLOW, "PRESS A");
            }
            if (paused) gfx_text_centre(46, C_YELLOW, "PAUSED");
        }

        /* --- the frame's cost: display lines since the vertical blank --- */
        busy = (uint8_t)(MIKEY.timer2.reload - MIKEY.timer2.count);
        if (busy > busy_max) busy_max = busy;

        gfx_present();
        cur ^= 1;                           /* the other buffer, the other set */
        cs = scbs[cur];
        scb_off = cur ? sizeof scbs[0] : 0;

        if (!paused) {
            snd_tick();
            sfx_tick();
            if (music_on) music_tick();
        }

        /* --- frame rate: loops per vertical blank, logged every 2 s --- */
        now = frame_count;
        if ((uint16_t)(now - last_vbl) > 1) late += (uint16_t)(now - last_vbl) - 1;
        last_vbl = now;
        ++fps_loops;
        if ((uint16_t)(now - fps_vbl) >= 100) {
            if (gs_state != STATE_TITLE && !paused) {
                logb("rb: fps=", (uint8_t)(fps_loops * 50u / (uint16_t)(now - fps_vbl)));
                logb(" late=", (uint8_t)late);
                logb(" busy=", busy_max);
                logb("/", MIKEY.timer2.reload + 1);
                logb(" rocks=", (uint8_t)gs_rock_count);
                logb(" parts=", particles_live);
                lynx_log_c('\n');
            }
            fps_vbl = now;
            fps_loops = late = 0;
            busy_max = 0;
        }
    }
}
