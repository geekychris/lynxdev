/*
 * Frank the Frog (Atari Lynx). Port of amiga_mcp/examples/frank_the_frog.
 *
 * game_tick() is the original main loop's state machine (main.c), with the
 * original's rules in lanes.c, frank.c and score.c (shared with the a8dev
 * port). This file adds the Lynx front end: the title over the moving
 * lanes, the timer, pause, Mikey sound and the log lines the test reads.
 * Port changes are marked "port:".
 */
#include <string.h>
#include "lynxgame.h"
#include "game.h"
#include "lanes.h"
#include "frank.h"
#include "score.h"
#include "render.h"
#include "sound.h"

uint8_t game_state = STATE_TITLE;
uint8_t start_level = 1;
static uint8_t transition_frames, paused;

/* the frame-rate probe: game loops vs vertical blanks (the test reads them) */
uint16_t loop_count, late_frames;

/* ------------------------------------------------------------ log */

#ifndef LYNX_NO_LOG
/* port (speed): a small number without lynx_log_u's divisions (a log
   character costs the harness build ~300 cycles: the lines are short) */
static void log_u8(uint8_t v)
{
    uint8_t t = 0;
    while (v >= 10) { v -= 10; ++t; }
    if (t) lynx_log_c('0' + t);
    lynx_log_c('0' + v);
}
static void logu(const char *k, uint8_t v) { lynx_log_s(k); log_u8(v); }
static void logscore(void)
{
    uint8_t i = 0;
    while (i < 5 && score_dig[i] == '0') ++i;
    for (; i < 6; ++i) lynx_log_c(score_dig[i]);
}
#define LOGNL() lynx_log_c('\n')
#else
#define logu(k, v) ((void)0)
#define logscore() ((void)0)
#define LOGNL() ((void)0)
#endif

static const char *const state_names[] = { "title", "playing", "dying", "level", "gameover" };

static void die(const char *why, uint8_t snd)
{
    lynx_log_s("frank: death ");
    lynx_log_s(why);
    logu(" row=", frog_row);
    logu(" col=", frank_col());
    LOGNL();
    frank_start_death();
    game_state = STATE_DYING;
    sound(snd);
}

/* port: the timer restarts with every frog */
static void new_frog(void)
{
    frank_init();
    time_left = TIME_LIMIT;
}

static void new_game(void)
{
    score_init();
    score_level = start_level;
    new_frog();
    lanes_init(start_level);
    lanes_reset_home();
    render_lanes_setup();
}

/* ------------------------------------------------------------ the game */

static void game_tick(int8_t joy_dx, int8_t joy_dy, uint8_t fire_pressed)
{
    int8_t ride_dx, slot;
    uint8_t death_done;

    switch (game_state) {
    case STATE_TITLE:
        lanes_tick();            /* port: the lanes move behind the title */
        if (fire_pressed) {
            game_state = STATE_PLAYING;
            new_game();
        }
        break;

    case STATE_PLAYING:
        if (joy_dx || joy_dy) {
            uint8_t old_highest = frank_highest_row;
            frank_move(joy_dx, joy_dy);
            sound(S_HOP);
            if (frank_highest_row < old_highest) score_add(10);
        }

        lanes_tick();

        /* River riding */
        if (frog_row >= ROW_RIVER_5 && frog_row <= ROW_RIVER_1) {
            if (!lanes_check_river(frog_px, (uint8_t)frog_row, &ride_dx)) {
                die("splash", S_SPLASH);
            } else if (ride_dx != 0) {
                int16_t new_x = frog_px + ride_dx;
                if (new_x < -4 || new_x + TILE_W > SCREEN_W + 4) die("carried", S_SPLASH);
                else frank_set_x(new_x);
            }
        }

        /* Car collision */
        if (game_state == STATE_PLAYING && frog_row >= ROW_ROAD_5 && frog_row <= ROW_ROAD_1) {
            if (lanes_check_car(frog_px, (uint8_t)frog_row)) die("splat", S_SPLAT);
        }

        /* Home base check */
        if (game_state == STATE_PLAYING && frog_row == ROW_HOME) {
            slot = lanes_check_home(frog_px);
            if (slot >= 0) {
                lanes_fill_home(slot);
                score_add(50);
                sound(S_HOME);
                logu("frank: home slot=", slot);
                LOGNL();
                if (lanes_all_home()) {
                    game_state = STATE_LEVEL;
                    transition_frames = 60;
                    sound(S_LEVEL);
                    logu("frank: level ", score_level);
                    lynx_log_s(" complete\n");
                } else {
                    new_frog();
                }
            } else {
                die("bank", S_SPLASH);
            }
        }

        /* port (added): out of time */
        if (game_state == STATE_PLAYING && --time_left == 0) die("time", S_TIME);
        break;

    case STATE_DYING:
        death_done = !frank_die_tick();
        lanes_tick();
        if (death_done) {
            score_lose_life();
            if (score_lives == 0) {
                game_state = STATE_GAMEOVER;
                transition_frames = 30;
                sound(S_OVER);
                lynx_log_s("frank: gameover score=");
                logscore();
                LOGNL();
            } else {
                new_frog();
                game_state = STATE_PLAYING;
            }
        }
        break;

    case STATE_LEVEL:
        if (--transition_frames == 0) {
            score_next_level();
            lanes_reset_home();
            lanes_init(score_level);
            render_lanes_setup();
            new_frog();
            game_state = STATE_PLAYING;
        }
        break;

    case STATE_GAMEOVER:
        lanes_tick();
        if (transition_frames > 0) {
            transition_frames--;
        } else if (fire_pressed) {
            game_state = STATE_TITLE;
        }
        break;
    }
}

/* ------------------------------------------------------------ overlays */

enum { OV_NONE, OV_TITLE, OV_LEVEL, OV_OVER, OV_OVER_PROMPT, OV_PAUSED };
static uint8_t overlay = 0xFF;

/* rebuild the overlay's glyphs when it changes (force: the title's options) */
static void update_overlay(uint8_t force)
{
    uint8_t want = paused ? OV_PAUSED
        : game_state == STATE_TITLE ? OV_TITLE
        : game_state == STATE_LEVEL ? OV_LEVEL
        : game_state == STATE_GAMEOVER ? (transition_frames ? OV_OVER : OV_OVER_PROMPT)
        : OV_NONE;
    if (want == overlay && !force) return;
    overlay = want;
    switch (want) {
    case OV_TITLE:       render_title(); break;
    case OV_LEVEL:       render_level_done(); break;
    case OV_OVER:        render_gameover(0); break;
    case OV_OVER_PROMPT: render_gameover(1); break;
    case OV_PAUSED:      render_paused(); break;
    default:             txt_clear(); break;
    }
}

int main(void)
{
    uint8_t p, prev_state, last_lives = 0xFF, last_level = 0, blink_t = 0, opts, hud_dirty = 0;
    int8_t dx, dy;
    int16_t last_row = -1;
    uint16_t vbl, last_vbl;
    uint32_t last_score = 0xFFFFFFFFUL, last_hi = 0;

    gfx_init();
    gfx_set_rate(FRAME_HZ);      /* port: 50 Hz, the original's PAL speeds */
    sound_init();
    render_init();
    lanes_init(1);
    render_lanes_setup();
    render_hud();
    lynx_log("frank: ready hz=%u\n", FRAME_HZ);
    lynx_log_s("frank: title\n");
    last_vbl = frame_count;

    for (;;) {
        input_update();
        p = (uint8_t)input_new;
        dx = dy = 0;
        if (p & IN_UP) dy = -1;
        else if (p & IN_DOWN) dy = 1;
        else if (p & IN_LEFT) dx = -1;
        else if (p & IN_RIGHT) dx = 1;

        prev_state = game_state;
        opts = 0;
        if (game_state == STATE_TITLE) {
            opts = p & (IN_OPT1 | IN_OPT2);
            if (p & IN_OPT1) {
                music_on ^= 1;
                logu("frank: music=", music_on);
                LOGNL();
            }
            if (p & IN_OPT2) {
                start_level = start_level == 4 ? 1 : start_level + 1;
                logu("frank: start level=", start_level);
                LOGNL();
            }
        }
        if ((input_new & IN_PAUSE) && (game_state == STATE_PLAYING || game_state == STATE_DYING)) {
            paused ^= 1;
            lynx_log_s(paused ? "frank: paused\n" : "frank: resumed\n");
        }
        if (!paused) game_tick(dx, dy, (p & (IN_A | IN_B)) != 0);
        frank_probe[2] = game_state;

        if (game_state != prev_state) {
            lynx_log_s("frank: state=");
            lynx_log_s(state_names[game_state]);
            lynx_log_s(" score=");
            logscore();
            logu(" lives=", score_lives);
            logu(" level=", score_level);
            LOGNL();
            if (prev_state == STATE_TITLE) music_play();
            else if (game_state == STATE_TITLE) {
                music_stop();
                lanes_init(start_level);
                render_lanes_setup();
                lynx_log_s("frank: title\n");
            } else if (game_state == STATE_GAMEOVER) music_stop();
        }

        /* one line a hop: row, column, score */
        if (frog_row != last_row && game_state == STATE_PLAYING) {
            logu("frank: row=", frog_row);
            logu(" col=", frank_col());
            lynx_log_s(" score=");
            logscore();
            LOGNL();
            last_row = frog_row;
            if (score_current != last_score) hud_dirty = 1;   /* the HUD, no score line */
            last_score = score_current;
        }
        if (hud_dirty || score_current != last_score || score_hi != last_hi || score_level != last_level) {
            hud_dirty = 0;
            if (last_score != 0xFFFFFFFFUL && score_current != last_score) {
                lynx_log_s("frank: score=");
                logscore();
                LOGNL();
            }
            if (last_level && score_level != last_level) { logu("frank: level=", score_level); LOGNL(); }
            last_score = score_current;
            last_hi = score_hi;
            last_level = score_level;
            render_hud();
        }
        if (score_lives != last_lives) {
            if (last_lives != 0xFF) { logu("frank: lives=", score_lives); LOGNL(); }
            last_lives = score_lives;
        }

        if (++blink_t == 50) blink_t = 0;
        update_overlay(opts != 0);
        render_blink(blink_t >= 35);
        render_frame(game_state);
        if (!paused) sound_tick();
        gfx_present();

        /* port: frame-rate probe. A loop that missed a vertical blank shows
           up as more than one blank since the last one. */
        vbl = frame_count;
        ++loop_count;
        if ((uint16_t)(vbl - last_vbl) > 1) late_frames += (uint16_t)(vbl - last_vbl) - 1;
        last_vbl = vbl;
    }
}
