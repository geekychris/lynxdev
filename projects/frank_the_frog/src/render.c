/*
 * Frank the Frog - the Lynx display. The original redraws its 320x256
 * playfield with RectFill each frame; here every frame is one chain of
 * Suzy sprites drawn into the back buffer:
 *
 *   HUD strip, 13 rows (background-type sprites: the far bank with its
 *   ponds in 4 bpp, river / grass / road rows in 1 bpp with two pens each,
 *   so nothing needs clearing), the timer strip,
 *   30 lane objects (cars recoloured per lane through their pen palette,
 *   flipped for the lanes that go left; buses, logs, lily-pad rafts),
 *   the frogs in the ponds, Frank (or his splat, scaled), the HUD glyphs
 *   (a 3x5 font), lives and the timer bar (one pixel scaled to its length).
 *
 * Objects hanging off the screen edges are clipped by Suzy.
 */
#include <string.h>
#include "lynxgame.h"
#include "game.h"
#include "lanes.h"
#include "frank.h"
#include "score.h"
#include "render.h"
#include "sound.h"
#include "spr_bus.h"
#include "spr_car.h"
#include "spr_font.h"
#include "spr_face.h"
#include "spr_frog.h"
#include "spr_frogside.h"
#include "spr_grass.h"
#include "spr_home.h"
#include "spr_life.h"
#include "spr_log24.h"
#include "spr_log32.h"
#include "spr_pads.h"
#include "spr_pixel.h"
#include "spr_road.h"
#include "spr_splat.h"
#include "spr_water.h"

/* the original's 16 colours, 0xRGB (game.h C_*) */
static const uint16_t palette[16] = {
    0x000, 0x1A1, 0x555, 0x23C, 0x4E3, 0x060, 0x952, 0xE22,
    0xFD1, 0x37F, 0xB4D, 0x0A4, 0xAF6, 0xFFF, 0x8DF, 0x000 };

uint16_t time_left;

/* ------------------------------------------------------------- the SCBs */

#define G_SCORE 5                /* glyph indexes: SCORE + 6 digits ... */
#define G_HI    13               /* HI + 6 digits */
#define G_LV    21               /* LV + 2 digits */
#define NUM_GLYPHS 23
#define MAX_LIVES 5

static SCB_REHV_PAL hud_bg, rows[GRID_ROWS], timer_bg;
static SCB_REHV_PAL objs[NUM_OBJS];
static SCB_REHV_PAL homes[NUM_HOMES], frog, splat;
static SCB_REHV_PAL glyphs[NUM_GLYPHS], lives[MAX_LIVES], timer_bar;
static SCB_REHV_PAL big_frog;    /* the title */
#define MAX_TXT 72
static SCB_REHV_PAL box_in, box_t, box_b, box_l, box_r, txt[MAX_TXT];
static uint8_t txt_n;
static void txt_link(void);

SCB_REHV_PAL *obj_scb[NUM_OBJS];  /* (read by lanes_asm.s) */
uint8_t obj_off[NUM_OBJS];        /* flipped: the picture's width - 1 */
void render_objs(void);           /* lanes_asm.s */

/* font frames: 0-9, then letters */
enum { F_S = 10, F_C, F_O, F_R, F_E, F_H, F_I, F_L, F_V, F_T };

static void bg_sprite(SCB_REHV_PAL *s, const uint8_t *data, uint8_t bpp, uint8_t y)
{
    spr_init(s, data, bpp);
    s->sprctl0 = (s->sprctl0 & 0xF8) | TYPE_BACKNONCOLL;   /* pen 0 drawn too */
    spr_pos(s, 0, y);
}

static void two_pens(SCB_REHV_PAL *s, uint8_t c0, uint8_t c1)
{
    s->penpal[0] = (uint8_t)((c0 << 4) | c1);
}

/* a filled rectangle: one pixel scaled (a normal sprite: the rest of the
   pixel's data byte is pen 0, transparent) */
static void bar(SCB_REHV_PAL *s, uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t c)
{
    spr_init(s, pixel_data, PIXEL_BPP);
    spr_pos(s, x, y);
    spr_scale(s, (uint16_t)w << 8, (uint16_t)h << 8);
    two_pens(s, 0, c);
}

static void glyph(uint8_t i, uint8_t x, uint8_t f, uint8_t c)
{
    spr_init(&glyphs[i], font_frames[f], FONT_BPP);
    spr_color(&glyphs[i], c);
    spr_pos(&glyphs[i], x, 1);
}

static void link_all(void)
{
    /* the chain, in drawing order */
    SCB_REHV_PAL *prev = &hud_bg;
    uint8_t i;
#define LINK(s) (spr_link(prev, (s)), prev = (s))
    for (i = 0; i < GRID_ROWS; ++i) LINK(&rows[i]);
    LINK(&timer_bg);
    for (i = 0; i < NUM_OBJS; ++i) LINK(&objs[i]);
    for (i = 0; i < NUM_HOMES; ++i) LINK(&homes[i]);
    LINK(&frog);
    LINK(&splat);
    for (i = 0; i < NUM_GLYPHS; ++i) LINK(&glyphs[i]);
    for (i = 0; i < MAX_LIVES; ++i) LINK(&lives[i]);
    LINK(&timer_bar);
    LINK(&big_frog);
#undef LINK
    txt_link();
}

void render_init(void)
{
    uint8_t i, r;

    gfx_palette(palette);

    bar(&hud_bg, 0, 0, 160, HUD_H, C_BLACK);
    bar(&timer_bg, 0, TIMER_Y - 1, 160, LYNX_SCREEN_H - TIMER_Y + 1, C_BLACK);
    bg_sprite(&rows[ROW_HOME], home_data, HOME_BPP, ROW_Y(ROW_HOME));
    for (r = ROW_RIVER_5; r <= ROW_RIVER_1; ++r) {
        bg_sprite(&rows[r], water_data, WATER_BPP, ROW_Y(r));
        two_pens(&rows[r], C_WATER, C_SKY);
    }
    for (r = ROW_ROAD_5; r <= ROW_ROAD_1; ++r) {
        bg_sprite(&rows[r], road_data, ROAD_BPP, ROW_Y(r));
        /* dashed lines between the road lanes, none below the last */
        two_pens(&rows[r], C_ROAD, r == ROW_ROAD_1 ? C_ROAD : C_YELLOW);
    }
    bg_sprite(&rows[ROW_MEDIAN], grass_data, GRASS_BPP, ROW_Y(ROW_MEDIAN));
    two_pens(&rows[ROW_MEDIAN], C_GRASS, C_DARK);
    bg_sprite(&rows[ROW_START], grass_data, GRASS_BPP, ROW_Y(ROW_START));
    two_pens(&rows[ROW_START], C_GRASS, C_DARK);

    for (i = 0; i < NUM_OBJS; ++i) spr_init(&objs[i], car_data, CAR_BPP);
    for (i = 0; i < NUM_HOMES; ++i) {
        spr_init(&homes[i], frog_data, FROG_BPP);
        spr_pos(&homes[i], home_cx[i] / 2 - 4, ROW_Y(ROW_HOME));
    }
    spr_init(&frog, frog_data, FROG_BPP);
    spr_init(&splat, splat_data, SPLAT_BPP);
    spr_init(&big_frog, face_data, FACE_BPP);
    spr_scale(&big_frog, SPR_SCALE_2, SPR_SCALE_2);
    spr_pos(&big_frog, 64, 10);

    glyph(0, 1, F_S, C_YELLOW);
    glyph(1, 5, F_C, C_YELLOW);
    glyph(2, 9, F_O, C_YELLOW);
    glyph(3, 13, F_R, C_YELLOW);
    glyph(4, 17, F_E, C_YELLOW);
    for (i = 0; i < 6; ++i) glyph(G_SCORE + i, 23 + 4 * i, 0, C_WHITE);
    glyph(11, 52, F_H, C_YELLOW);
    glyph(12, 56, F_I, C_YELLOW);
    for (i = 0; i < 6; ++i) glyph(G_HI + i, 62 + 4 * i, 0, C_WHITE);
    glyph(19, 91, F_L, C_YELLOW);
    glyph(20, 95, F_V, C_YELLOW);
    glyph(G_LV, 101, 0, C_WHITE);
    glyph(G_LV + 1, 105, 0, C_WHITE);
    for (i = 0; i < MAX_LIVES; ++i) {
        spr_init(&lives[i], life_data, LIFE_BPP);
        spr_pos(&lives[i], 116 + 7 * i, 1);
    }
    bar(&timer_bar, 0, TIMER_Y, 160, 3, C_FROG);
    bar(&box_in, 4, 0, 1, 1, C_BLACK);
    bar(&box_t, 4, 0, 152, 1, C_WHITE);
    bar(&box_b, 4, 0, 152, 1, C_WHITE);
    bar(&box_l, 4, 0, 1, 1, C_WHITE);
    bar(&box_r, 4, 0, 1, 1, C_WHITE);
    link_all();
    txt_clear();
}

/* each lane object's picture, pens and flip (lanes_init sets the lanes) */
void render_lanes_setup(void)
{
    uint8_t l, j, i, kind, w, j2;
    const uint8_t *d;
    for (l = 0; l < NUM_LANES; ++l) {
        kind = lane_kind[l];
        switch (kind) {
        case KIND_BUS:  d = bus_data; w = BUS_W; break;
        case KIND_LOG:  if (lane_width[l] > 48) { d = log32_data; w = LOG32_W; }
                        else { d = log24_data; w = LOG24_W; }
                        break;
        case KIND_PADS: d = pads_data; w = PADS_W; break;
        default:        d = car_data; w = CAR_W; break;
        }
        for (j = 0; j < MAX_LANE_OBJECTS; ++j) {
            i = l * MAX_LANE_OBJECTS + j;
            spr_init(&objs[i], d, 4);
            obj_scb[i] = &objs[i];
            /* cars and buses face the way they go */
            j2 = (kind <= KIND_BUS && lane_dir[l] == DIR_LEFT);
            obj_off[i] = j2 ? w - 1 : 0;
            spr_flip(&objs[i], j2, 0);
            if (kind == KIND_CAR_YELLOW) spr_pen(&objs[i], C_RED, C_YELLOW);
            else if (kind == KIND_CAR_BLUE) spr_pen(&objs[i], C_RED, C_BLUE);
            spr_pos(&objs[i], 0, ROW_Y(lane_row[l]));
            spr_hide(&objs[i], j >= lane_num[l]);
        }
    }
    link_all();                  /* spr_init cleared the links */
}

/* the original's x (signed) -> screen x */
#define HALF(x) ((int16_t)((uint16_t)((x) + 1024) >> 1) - 512)

static uint8_t last_lives = 0xFF;

#pragma static-locals (on)

void render_frame(uint8_t state)
{
    uint8_t l, i, n;
    int16_t x;
    SCB_REHV_PAL *s;

    /* lane objects (the unused ones are hidden); in asm, the C is: */
#if 0
    for (i = 0; i < NUM_OBJS; ++i) {
        s = obj_scb[i];
        s->hpos = HALF(obj_x[i]) + obj_off[i];
    }
#endif
    render_objs();

    for (i = 0; i < NUM_HOMES; ++i) spr_hide(&homes[i], !home_filled[i]);

    /* Frank, or his splat (shrinking onto where he was, as the original's
       red X closes in) */
    x = HALF(frog_px);
    if (state == STATE_PLAYING || (state == STATE_DYING && !frank_dying)) {
        spr_hide(&frog, 0);
        spr_hide(&splat, 1);
        if (frank_facing >= 2) {
            spr_data(&frog, frogside_data);
            spr_flip(&frog, frank_facing == 2, 0);
            spr_pos(&frog, frank_facing == 2 ? x + 7 : x, ROW_Y(frog_row));
        } else {
            spr_data(&frog, frog_data);
            spr_flip(&frog, 0, frank_facing == 1);
            spr_pos(&frog, x, ROW_Y(frog_row) + (frank_facing == 1 ? 6 : 0));
        }
    } else if (state == STATE_DYING) {
        uint8_t k = frank_death_timer >> 2;          /* 7 .. 0 */
        spr_hide(&frog, 1);
        spr_hide(&splat, 0);
        spr_scale(&splat, 0x100 + (k << 5), 0x100 + (k << 5));
        spr_pos(&splat, x - (k >> 1), ROW_Y(frog_row) - (k >> 1));
    } else {
        spr_hide(&frog, 1);
        spr_hide(&splat, 1);
    }

    /* lives (the HUD digits change in render_hud) */
    if (score_lives != last_lives) {
        last_lives = score_lives;
        for (i = 0; i < MAX_LIVES; ++i) spr_hide(&lives[i], i >= score_lives);
    }

    /* the timer bar: 160 pixels for TIME_LIMIT frames */
    s = &timer_bar;
    n = (uint8_t)(time_left * 16u / (TIME_LIMIT / 10));
    s->hsize = (uint16_t)n << 8;
    spr_hide(s, n == 0 || state == STATE_TITLE || state == STATE_GAMEOVER);
    l = n > 53 ? C_FROG : n > 26 ? C_YELLOW : C_RED;
    two_pens(s, 0, l);

    spr_draw(&hud_bg);
}


void render_hud(void)
{
    uint8_t i;
    for (i = 0; i < 6; ++i) {
        spr_data(&glyphs[G_SCORE + i], font_frames[score_dig[i] - '0']);
        spr_data(&glyphs[G_HI + i], font_frames[hi_dig[i] - '0']);
    }
    spr_data(&glyphs[G_LV], font_frames[score_level / 10]);
    spr_data(&glyphs[G_LV + 1], font_frames[score_level % 10]);
    spr_hide(&glyphs[G_LV], score_level < 10);
}

/* ------------------------------------------------------------- text

   Overlays (title, level complete, game over, paused) are glyphs of the
   HUD font in a pool of SCBs at the end of the chain, scaled by Suzy for the
   big lines. Built when the overlay changes; drawn with everything else. */

static const char font_map[] = "0123456789SCOREHILVTABDFGKMNPU+!";

static void txt_link(void)
{
    uint8_t i;
    spr_link(&big_frog, &box_in);
    spr_link(&box_in, &box_t);
    spr_link(&box_t, &box_b);
    spr_link(&box_b, &box_l);
    spr_link(&box_l, &box_r);
    spr_link(&box_r, txt_n ? &txt[0] : 0);
    for (i = 1; i < txt_n; ++i) spr_link(&txt[i - 1], &txt[i]);
    if (txt_n) spr_link(&txt[txt_n - 1], 0);
}

void txt_clear(void)
{
    uint8_t r;
    txt_n = 0;
    for (r = 0; r < GRID_ROWS; ++r) spr_hide(&rows[r], 0);
    spr_hide(&box_in, 1);
    spr_hide(&box_t, 1);
    spr_hide(&box_b, 1);
    spr_hide(&box_l, 1);
    spr_hide(&box_r, 1);
    spr_hide(&big_frog, 1);
    txt_link();
}

uint8_t txt_add(uint8_t x, uint8_t y, uint8_t sx, uint8_t sy, uint8_t c, const char *s)
{
    uint8_t first = txt_n, f;
    const char *p;
    SCB_REHV_PAL *t;
    for (; *s; ++s, x += sx << 2) {
        p = strchr(font_map, *s);
        if (*s == ' ' || !p || txt_n == MAX_TXT) continue;
        f = (uint8_t)(p - font_map);
        t = &txt[txt_n++];
        spr_init(t, font_frames[f], FONT_BPP);
        spr_color(t, c);
        spr_scale(t, (uint16_t)sx << 8, (uint16_t)sy << 8);
        spr_pos(t, x, y);
    }
    txt_link();
    return first;
}

uint8_t txt_centre(uint8_t y, uint8_t sx, uint8_t sy, uint8_t c, const char *s)
{
    uint8_t w = (uint8_t)((strlen(s) * 4 - 1) * sx);
    return txt_add((uint8_t)((LYNX_SCREEN_W - w) / 2), y, sx, sy, c, s);
}

/* glyphs from..to-1 hidden or shown (blinking) */
void txt_hide(uint8_t from, uint8_t to, uint8_t on)
{
    for (; from < to; ++from) spr_hide(&txt[from], on);
}

uint8_t txt_count(void) { return txt_n; }

/* a white-framed black box */
void txt_box(uint8_t x, uint8_t y, uint8_t w, uint8_t h)
{
    spr_pos(&box_in, x + 1, y + 1);
    spr_scale(&box_in, (uint16_t)(w - 2) << 8, (uint16_t)(h - 2) << 8);
    spr_pos(&box_t, x, y);
    spr_scale(&box_t, (uint16_t)w << 8, 1u << 8);
    spr_pos(&box_b, x, y + h - 1);
    spr_scale(&box_b, (uint16_t)w << 8, 1u << 8);
    spr_pos(&box_l, x, y);
    spr_scale(&box_l, 1u << 8, (uint16_t)h << 8);
    spr_pos(&box_r, x + w - 1, y);
    spr_scale(&box_r, 1u << 8, (uint16_t)h << 8);
    spr_hide(&box_in, 0);
    spr_hide(&box_t, 0);
    spr_hide(&box_b, 0);
    spr_hide(&box_l, 0);
    spr_hide(&box_r, 0);
}

static uint8_t press_from, press_to;

/* the title: big Frank over the moving lanes, the name, the options */
void render_title(void)
{
    static const char *const music_txt[2] = { "MUSIC OFF", "MUSIC ON" };
    char buf[12];

    uint8_t i;

    txt_clear();
    spr_hide(&big_frog, 0);
    txt_box(0, 37, 160, 61);
    /* port (speed): what the box covers isn't drawn (Suzy's time) */
    for (i = ROW_RIVER_1; i < GRID_ROWS; ++i) spr_hide(&rows[i], 1);
    for (i = 0; i < NUM_OBJS; ++i)
        if (objs[i].vpos >= ROW_Y(ROW_RIVER_1)) spr_hide(&objs[i], 1);
    txt_centre(41, 2, 3, C_DARK, "FRANK THE FROG");
    txt_centre(40, 2, 3, C_FROG, "FRANK THE FROG");
    press_from = txt_centre(59, 2, 2, C_WHITE, "PRESS A");
    press_to = txt_count();
    memcpy(buf, "HI ", 3);
    memcpy(buf + 3, hi_dig, 6);
    buf[9] = 0;
    txt_centre(72, 1, 1, C_YELLOW, buf);
    memcpy(buf, "LEVEL 1", 8);
    buf[6] = (char)('0' + start_level);
    txt_add(28, 81, 1, 1, C_SKY, buf);
    txt_add(72 + 12, 81, 1, 1, C_SKY, music_txt[music_on]);
    txt_centre(90, 1, 1, C_PAD, "OPT2 LEVEL  OPT1 MUSIC");
}

/* blinking "PRESS A" (title and game over) */
void render_blink(uint8_t off)
{
    static uint8_t was;
    if (off != was) {
        was = off;
        txt_hide(press_from, press_to, off);
    }
}

void render_gameover(uint8_t prompt)
{
    char buf[13];
    txt_clear();
    txt_box(4, 28, 152, 40);
    txt_centre(32, 2, 2, C_RED, "GAME OVER");
    memcpy(buf, "SCORE ", 6);
    memcpy(buf + 6, score_dig, 6);
    buf[12] = 0;
    txt_centre(46, 1, 1, C_WHITE, buf);
    press_from = press_to = txt_count();
    if (prompt) {
        press_from = txt_centre(56, 1, 1, C_YELLOW, "PRESS A");
        press_to = txt_count();
    }
}

void render_level_done(void)
{
    txt_clear();
    txt_box(4, 34, 152, 30);
    txt_centre(38, 2, 2, C_FROG, "LEVEL COMPLETE");
    txt_centre(53, 1, 1, C_YELLOW, "+1000 BONUS!");
}

void render_paused(void)
{
    txt_clear();
    txt_box(36, 42, 88, 16);
    txt_centre(45, 2, 2, C_WHITE, "PAUSED");
}
