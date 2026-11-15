/*
 * Frank the Frog (Atari Lynx) - game constants. Port of
 * amiga_mcp/examples/frank_the_frog/game.h (by way of the a8dev port).
 * The rules run in the original's 320-pixel coordinates; the Lynx shows
 * them at half width (160 pixels, 20 columns of 8) and rows of 7 lines
 * (the original's 16). Port changes are marked "port:".
 */
#ifndef GAME_H
#define GAME_H
#include <stdint.h>

#define SCREEN_W 320            /* the original's coordinates */
#define TILE_W 16
#define GRID_COLS (SCREEN_W / TILE_W)   /* 20 columns */
#define GRID_ROWS 13

#define ROW_HOME     0
#define ROW_RIVER_5  1
#define ROW_RIVER_4  2
#define ROW_RIVER_3  3
#define ROW_RIVER_2  4
#define ROW_RIVER_1  5
#define ROW_MEDIAN   6
#define ROW_ROAD_5   7
#define ROW_ROAD_4   8
#define ROW_ROAD_3   9
#define ROW_ROAD_2   10
#define ROW_ROAD_1   11
#define ROW_START    12

/* port: the Lynx screen (160x102): HUD line, 13 rows of 7, timer bar */
#define HUD_H        7
#define ROW_H        7
#define FIELD_Y      HUD_H                       /* 7 */
#define ROW_Y(r)     (FIELD_Y + (r) * ROW_H)
#define TIMER_Y      99                          /* 3 lines, 98 is a gap */

#define STATE_TITLE    0
#define STATE_PLAYING  1
#define STATE_DYING    2
#define STATE_LEVEL    3
#define STATE_GAMEOVER 4

#define MAX_LANE_OBJECTS 3
#define NUM_HOMES 4
#define DIR_LEFT  (-1)
#define DIR_RIGHT  1

/* port (added): a time limit per frog, the original had none. 50 Hz. */
#define FRAME_HZ    50
#define TIME_LIMIT  (60 * FRAME_HZ)

/* the palette (src/render.c) */
#define C_GRASS   1
#define C_ROAD    2
#define C_WATER   3
#define C_FROG    4
#define C_DARK    5
#define C_LOG     6
#define C_RED     7
#define C_YELLOW  8
#define C_BLUE    9
#define C_PURPLE  10
#define C_PAD     11
#define C_LILY    12
#define C_WHITE   13
#define C_SKY     14
#define C_BLACK   15

#endif
