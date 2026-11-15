/*
 * Rock Blaster (Atari Lynx) - game definitions. Port of
 * amiga_mcp/examples/rock_blaster/game.h, by way of the Atari 8-bit port
 * (a8dev/projects/rock_blaster), which did the same sums in 16 bits.
 *
 * The world keeps the original's units: 320 pixels across, as on the Amiga,
 * and 204 high (port: the original's 256, cut to the Lynx's shape). The
 * Lynx shows it at half size, 160 x 102, so every speed, radius and timer is
 * the original's and a screen pixel is two world pixels.
 *
 * Positions other than the ship's are 16-bit in 1/128 world pixel: the high
 * byte is the screen pixel. The ship keeps the original's 16.16.
 * port (speed): objects are arrays, one entry each (struct-of-arrays).
 */
#ifndef GAME_H
#define GAME_H
#include <stdint.h>

#define WORLD_W 320
#define WORLD_H 204                         /* port: was 256 */

typedef int32_t Fixed;                      /* 16.16, the ship */
#define FX16(x)     ((Fixed)(x) << 16)

#define P7(px)      ((uint16_t)(px) << 7)   /* 1/128 world px */
#define W7          P7(WORLD_W)
#define H7          P7(WORLD_H)

/* Ship */
#define SHIP_MAX_SPEED  FX16(3)
#define SHIP_TURN_SPEED 3
#define SHIP_RADIUS     8
#define SHIP_INVULN_TIME 100

extern Fixed ship_x, ship_y, ship_dx, ship_dy;
extern uint8_t ship_angle;                  /* 0 = right, 64 = down */
extern uint8_t ship_alive, ship_thrust_on;
extern uint8_t ship_invuln;
extern int16_t ship_px, ship_py;            /* whole world pixels */

/* Bullets */
#define MAX_BULLETS     8
#define BULLET_LIFE     40
extern uint16_t bl_x[MAX_BULLETS], bl_y[MAX_BULLETS];
extern int16_t bl_dx[MAX_BULLETS], bl_dy[MAX_BULLETS];
extern uint8_t bl_active[MAX_BULLETS], bl_life[MAX_BULLETS];

/* Rocks */
#define ROCK_LARGE   0
#define ROCK_MEDIUM  1
#define ROCK_SMALL   2
#define MAX_ROCKS    24
extern uint16_t rk_x[MAX_ROCKS], rk_y[MAX_ROCKS];
extern int16_t rk_dx[MAX_ROCKS], rk_dy[MAX_ROCKS];
extern int16_t rk_px[MAX_ROCKS], rk_py[MAX_ROCKS];   /* whole world pixels */
extern uint8_t rk_sx[MAX_ROCKS], rk_sy[MAX_ROCKS];   /* screen pixels */
extern uint8_t rk_active[MAX_ROCKS], rk_size[MAX_ROCKS], rk_shape[MAX_ROCKS];

/* Particles */
#define MAX_PARTICLES 64
/* port (speed): a particle is its start (screen pixels), one of 64
   velocities (pt_k) and its age; where it is at each age, with the
   original's slowing down, is precomputed (pt_offx/pt_offy, screen pixels) */
#define PT_AGES 32
extern uint8_t pt_sx[MAX_PARTICLES], pt_sy[MAX_PARTICLES], pt_k[MAX_PARTICLES], pt_age[MAX_PARTICLES];
extern uint8_t pt_active[MAX_PARTICLES], pt_life[MAX_PARTICLES], pt_color[MAX_PARTICLES];
extern int8_t pt_offx[64][PT_AGES], pt_offy[64][PT_AGES];
extern uint8_t particles_live;

/* Game state */
#define STATE_TITLE    0
#define STATE_PLAYING  1
#define STATE_DEAD     2
#define STATE_GAMEOVER 3
extern uint32_t gs_score;
extern char gs_score_str[8];                /* "0000000": the HUD's digits */
extern int8_t gs_lives;
extern uint8_t gs_level, gs_state;
extern int16_t gs_state_timer;
extern int8_t gs_rock_count;
extern uint16_t gs_frame;
extern uint8_t gs_hits;                     /* rocks hit in the last update */

/* Tunables (the original's defaults, *100) */
#define TUNE_ROCK_SPEED      80
#define TUNE_SHIP_THRUST     20
#define TUNE_BULLET_SPEED    400
#define TUNE_START_LIVES     3
#define TUNE_START_ROCKS     4
#define TUNE_ROCKS_PER_LEVEL 2

void game_init(void);
void game_spawn_rocks(uint8_t count);
void game_update(uint8_t joy_left, uint8_t joy_right, uint8_t joy_up, uint8_t joy_fire);
void particles_table(void);
extern const int16_t sin88[256];            /* sin * 256 */
#define cos88(a) sin88[(uint8_t)((a) + 64)]

#define SCORE_LARGE  20
#define SCORE_MEDIUM 50
#define SCORE_SMALL  100

/* objs.s: the per-frame loops (they also write the objects' SCBs) */
void rocks_move(void);                      /* move, wrap, rk_px/py */
void bullets_move(void);                    /* move, wrap, age */
void particles_move(void);                  /* age, place from the tables */
/* first live rock within r + its radius of (hit_x, hit_y), or 0xFF */
uint8_t __fastcall__ hit_scan(uint8_t r);
extern int16_t hit_x, hit_y;                /* world pixels */
extern uint8_t hit_sx, hit_sy;              /* the same in screen pixels */
/* next live bullet from i that hits a rock (the rock in hit_rock), or 0xFF */
uint8_t __fastcall__ bullet_scan(uint8_t i);
extern uint8_t hit_rock;
/* set: the move loops only write the SCBs (frozen objects) */
extern uint8_t objs_still;
/* every pen of the current SCB set -> the background colour (then draw the
   set: it erases what it drew two frames ago), and back */
void erase_begin(void);
void erase_end(void);
/* the 7 score digit SCBs' pictures from gs_score_str (SCB_REHV_PAL *) */
void __fastcall__ hud_digits(void *first);
/* (mul_a * b) >> 8 */
int16_t __fastcall__ mul_q8(uint8_t b);
extern int16_t mul_a;

/* the colours (palette in main.c) */
#define C_BG        1
#define C_WHITE     2
#define C_BLUE      3
#define C_GREY_DIM  6
#define C_GREY_MED  7
#define C_GREY_LT   8
#define C_RED       9
#define C_ORANGE    10
#define C_YELLOW    11
#define C_DARKGREY  13
#define C_BRIGHT_YELLOW 15

/* sound effects (main.c) */
void sfx_shoot(void);
void sfx_explode_large(void);
void sfx_explode_small(void);
void sfx_die(void);
#endif
