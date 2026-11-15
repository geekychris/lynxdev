/*
 * Rock Blaster (Lynx) - game logic. Port of amiga_mcp/examples/rock_blaster/
 * game.c, following the Atari 8-bit port's 16-bit sums.
 *
 * The rules, waves, scoring, speeds, timers and the order of the update are
 * the original's. Changes are marked "port:".
 */
#include <stdint.h>
#include "lynxrnd.h"
#include "game.h"

#pragma static-locals (on)

Fixed ship_x, ship_y, ship_dx, ship_dy;
uint8_t ship_angle, ship_alive, ship_thrust_on, ship_invuln;
int16_t ship_px, ship_py;

uint16_t bl_x[MAX_BULLETS], bl_y[MAX_BULLETS];
int16_t bl_dx[MAX_BULLETS], bl_dy[MAX_BULLETS];
uint8_t bl_active[MAX_BULLETS], bl_life[MAX_BULLETS];

uint16_t rk_x[MAX_ROCKS], rk_y[MAX_ROCKS];
int16_t rk_dx[MAX_ROCKS], rk_dy[MAX_ROCKS];
int16_t rk_px[MAX_ROCKS], rk_py[MAX_ROCKS];
uint8_t rk_sx[MAX_ROCKS], rk_sy[MAX_ROCKS];
uint8_t rk_active[MAX_ROCKS], rk_size[MAX_ROCKS], rk_shape[MAX_ROCKS];

uint8_t pt_sx[MAX_PARTICLES], pt_sy[MAX_PARTICLES], pt_k[MAX_PARTICLES], pt_age[MAX_PARTICLES];
int8_t pt_offx[64][PT_AGES], pt_offy[64][PT_AGES];
uint8_t pt_active[MAX_PARTICLES], pt_life[MAX_PARTICLES], pt_color[MAX_PARTICLES];
uint8_t particles_live;

uint32_t gs_score;
char gs_score_str[8];
int8_t gs_lives;
uint8_t gs_level, gs_state;
int16_t gs_state_timer;
int8_t gs_rock_count;
uint16_t gs_frame;
uint8_t gs_hits;

/* sin(i * 2 pi / 256) * 256, rounded (the original: its 16.16 table >> 8) */
const int16_t sin88[256] = {
       0,    6,   13,   19,   25,   31,   38,   44,   50,   56,   62,   68,   74,   80,   86,   92,
      98,  104,  109,  115,  121,  126,  132,  137,  142,  147,  152,  157,  162,  167,  172,  177,
     181,  185,  190,  194,  198,  202,  206,  209,  213,  216,  220,  223,  226,  229,  231,  234,
     237,  239,  241,  243,  245,  247,  248,  250,  251,  252,  253,  254,  255,  255,  256,  256,
     256,  256,  256,  255,  255,  254,  253,  252,  251,  250,  248,  247,  245,  243,  241,  239,
     237,  234,  231,  229,  226,  223,  220,  216,  213,  209,  206,  202,  198,  194,  190,  185,
     181,  177,  172,  167,  162,  157,  152,  147,  142,  137,  132,  126,  121,  115,  109,  104,
      98,   92,   86,   80,   74,   68,   62,   56,   50,   44,   38,   31,   25,   19,   13,    6,
       0,   -6,  -13,  -19,  -25,  -31,  -38,  -44,  -50,  -56,  -62,  -68,  -74,  -80,  -86,  -92,
     -98, -104, -109, -115, -121, -126, -132, -137, -142, -147, -152, -157, -162, -167, -172, -177,
    -181, -185, -190, -194, -198, -202, -206, -209, -213, -216, -220, -223, -226, -229, -231, -234,
    -237, -239, -241, -243, -245, -247, -248, -250, -251, -252, -253, -254, -255, -255, -256, -256,
    -256, -256, -256, -255, -255, -254, -253, -252, -251, -250, -248, -247, -245, -243, -241, -239,
    -237, -234, -231, -229, -226, -223, -220, -216, -213, -209, -206, -202, -198, -194, -190, -185,
    -181, -177, -172, -167, -162, -157, -152, -147, -142, -137, -132, -126, -121, -115, -109, -104,
     -98,  -92,  -86,  -80,  -74,  -68,  -62,  -56,  -50,  -44,  -38,  -31,  -25,  -19,  -13,   -6,
};


/* port: the SDK's xorshift instead of the 32-bit LCG (as the 8-bit ports) */
static uint16_t rng(void)
{
    return rnd16() & 0x7FFF;
}

/* (a * b) >> 8 with a 32-bit product (start-up only) */
static int16_t mulq8(int16_t a, int16_t b)
{
    return (int16_t)(((int32_t)a * b) >> 8);
}

static int16_t rng_range(int16_t lo, int16_t hi)
{
    return lo + (int16_t)(((int32_t)rng() * (hi - lo)) >> 15);
}

/* port: (v * 15) / 16 with C's truncation, without a 32-bit divide */
static Fixed slow15(Fixed v)
{
    if (v >= 0) return v - ((v + 15) >> 4);
    v = -v;
    return -(v - ((v + 15) >> 4));
}

/* the HUD's decimal digits, added in place (port, speed: no 32-bit
   divides a frame). Every score is a multiple of 10: add the tens. */
static void add_score(uint8_t points)
{
    uint8_t i = 5, d, c = points / 10;
    gs_score += points;
    for (;;) {
        d = (uint8_t)(gs_score_str[i] - '0') + c;
        c = 0;
        while (d >= 10) { d -= 10; ++c; }
        gs_score_str[i] = (char)('0' + d);
        if (!c || !i) break;
        --i;
    }
}

static void ship_reset(void)
{
    ship_x = FX16(WORLD_W / 2);
    ship_y = FX16(WORLD_H / 2);
    ship_dx = 0;
    ship_dy = 0;
    ship_alive = 1;
    ship_invuln = SHIP_INVULN_TIME;
    ship_px = WORLD_W / 2;
    ship_py = WORLD_H / 2;
}

void game_init(void)
{
    uint8_t i;
    gs_score = 0;
    for (i = 0; i < 7; ++i) gs_score_str[i] = '0';
    gs_score_str[7] = 0;
    gs_lives = TUNE_START_LIVES;
    gs_level = 1;
    gs_state = STATE_PLAYING;
    gs_state_timer = 0;
    gs_rock_count = 0;
    gs_frame = 0;

    ship_reset();
    ship_angle = 192;                /* pointing up */
    ship_thrust_on = 0;

    for (i = 0; i < MAX_BULLETS; i++) bl_active[i] = 0;
    for (i = 0; i < MAX_ROCKS; i++) rk_active[i] = 0;
    for (i = 0; i < MAX_PARTICLES; i++) pt_active[i] = 0;
    particles_live = 0;

    game_spawn_rocks(TUNE_START_ROCKS);
}

/* rock speeds in 1/128 px a frame: the tunable 0.80 px, x1.5 medium, x2 small */
#define ROCK_BASE ((uint8_t)(TUNE_ROCK_SPEED * 128L / 100))

/* a random direction and a speed of base/2 .. base (1/128 px a frame);
   base is at most 204 (small rocks) */
static void rock_launch(uint8_t i, uint8_t base)
{
    uint8_t a = rnd8(), spd;
    mul_a = rnd8();
    spd = (base >> 1) + (uint8_t)mul_q8(base - (base >> 1));
    mul_a = cos88(a);
    rk_dx[i] = mul_q8(spd);
    mul_a = sin88[a];
    rk_dy[i] = mul_q8(spd);
    rk_shape[i] = rnd8() & 3;
    /* port: the original also drew a visual angle and spin here, which its
       drawing never used */
    rk_active[i] = 1;
}

/* 0 .. 2 * half, in steps of 2 * half / 256 (port: 8 random bits, the
   original's 15: a step is under 1.3 world pixels) */
static uint16_t rnd_pos(uint16_t half)
{
    mul_a = (int16_t)half;
    return (uint16_t)mul_q8(rnd8()) << 1;
}

void game_spawn_rocks(uint8_t count)
{
    uint8_t i, j;
    for (j = 0; j < count && gs_rock_count < MAX_ROCKS; j++) {
        for (i = 0; i < MAX_ROCKS; i++) {
            if (!rk_active[i]) {
                switch ((uint8_t)(rng() & 3)) {          /* spawn at an edge */
                case 0: rk_x[i] = 0;  rk_y[i] = rnd_pos(H7 / 2); break;
                case 1: rk_x[i] = W7 - 1; rk_y[i] = rnd_pos(H7 / 2); break;
                case 2: rk_x[i] = rnd_pos(W7 / 2); rk_y[i] = 0; break;
                default: rk_x[i] = rnd_pos(W7 / 2); rk_y[i] = H7 - 1; break;
                }
                /* port: W7 - 1 / H7 - 1 for the original's W / H, which its
                   wrap() moved to 0 on the next frame */
                rk_size[i] = ROCK_LARGE;
                rk_px[i] = rk_x[i] >> 7;
                rk_py[i] = rk_y[i] >> 7;
                rk_sx[i] = (uint8_t)(rk_x[i] >> 8);
                rk_sy[i] = (uint8_t)(rk_y[i] >> 8);
                rock_launch(i, ROCK_BASE);
                gs_rock_count++;
                break;
            }
        }
    }
}

/* two smaller rocks where a rock was destroyed */
static void split_rock(uint8_t parent)
{
    uint8_t new_size, j, i;
    uint8_t base_speed = ROCK_BASE;

    if (rk_size[parent] == ROCK_SMALL) return;
    new_size = rk_size[parent] + 1;
    if (new_size == ROCK_MEDIUM) base_speed = (base_speed * 3) / 2;
    if (new_size == ROCK_SMALL) base_speed = base_speed * 2;
    i = 0;

    for (j = 0; j < 2 && gs_rock_count < MAX_ROCKS; j++) {
        for (; i < MAX_ROCKS; i++) {
            if (!rk_active[i]) {
                rk_x[i] = rk_x[parent];
                rk_y[i] = rk_y[parent];
                rk_px[i] = rk_px[parent];
                rk_py[i] = rk_py[parent];
                rk_sx[i] = rk_sx[parent];
                rk_sy[i] = rk_sy[parent];
                rk_size[i] = new_size;
                rock_launch(i, base_speed);
                gs_rock_count++;
                break;
            }
        }
    }
}

/* port (speed): a particle's velocity is one of 64 made at start-up the
   original's way (a random direction, a random speed 0..3 px), picked at
   random (as the 8-bit port). Its path is the same for every particle with
   that velocity: the original's move-then-slow-down (v * 240 / 256 a frame,
   here v - v / 16) is run once here for every age, in 1/128 world pixels,
   and kept as screen pixel offsets. */
static void particle_path(int8_t *out, int16_t v)
{
    int16_t x = 0;
    uint8_t t;
    out[0] = 0;
    for (t = 1; t < PT_AGES; ++t) {
        x += v;
        v -= v >> 4;
        out[t] = (int8_t)((x + 128) >> 8);
    }
}

void particles_table(void)
{
    uint8_t k, a;
    int16_t spd;
    for (k = 0; k < 64; ++k) {
        a = (uint8_t)rng();
        spd = rng_range(0, 3 * 128);
        particle_path(pt_offx[k], mulq8(cos88(a), spd));
        particle_path(pt_offy[k], mulq8(sin88[a], spd));
    }
}

/* 10 + rng() % 20, from 5 random bits (port: 0-19 nearly evenly) */
static const uint8_t pt_lives[32] = {
    10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
    26, 27, 28, 29, 11, 13, 15, 17, 19, 20, 22, 24, 26, 28, 14, 25 };

static void spawn_particles(uint16_t x, uint16_t y, uint8_t count, uint8_t color)
{
    uint8_t i = 0, r, sx = (uint8_t)(x >> 8), sy = (uint8_t)(y >> 8);
    for (; count; --count) {
        while (i < MAX_PARTICLES && pt_active[i]) ++i;
        if (i == MAX_PARTICLES) return;
        r = rnd8();
        pt_k[i] = r & 63;
        pt_sx[i] = sx;
        pt_sy[i] = sy;
        pt_age[i] = 0;
        pt_life[i] = pt_lives[(r >> 3) & 31];
        pt_color[i] = color;
        pt_active[i] = 1;
        particles_live++;
    }
}

#define BULLET_SPD ((int16_t)(TUNE_BULLET_SPEED * 128L / 100))
#define THRUST_8   ((int16_t)((TUNE_SHIP_THRUST * 65536L / 100) >> 8))

static void fire_bullet(void)
{
    uint8_t i;
    for (i = 0; i < MAX_BULLETS; i++) {
        if (!bl_active[i]) {
            bl_x[i] = (uint16_t)(ship_x >> 9);
            bl_y[i] = (uint16_t)(ship_y >> 9);
            bl_dx[i] = cos88(ship_angle) * (BULLET_SPD / 256);
            bl_dy[i] = sin88[ship_angle] * (BULLET_SPD / 256);
            bl_life[i] = BULLET_LIFE;
            bl_active[i] = 1;
            break;
        }
    }
}

/* wrap a 16.16 coordinate into 0..size */
static Fixed wrapf(Fixed v, Fixed size)
{
    if (v < 0) v += size;
    if (v >= size) v -= size;
    return v;
}

static void ship_controls(uint8_t joy_left, uint8_t joy_right, uint8_t joy_up, uint8_t joy_fire)
{
    int16_t vx, vy;
    if (!ship_alive) return;
    if (joy_left) ship_angle -= SHIP_TURN_SPEED;
    if (joy_right) ship_angle += SHIP_TURN_SPEED;

    ship_thrust_on = joy_up;
    if (joy_up) {
        /* FIX_MUL(cos, thrust) = (cos >> 8) * (thrust >> 8) */
        ship_dx += (int16_t)(cos88(ship_angle) * THRUST_8);
        ship_dy += (int16_t)(sin88[ship_angle] * THRUST_8);
        vx = (int16_t)(ship_dx >> 8);
        vy = (int16_t)(ship_dy >> 8);
        if ((int32_t)vx * vx + (int32_t)vy * vy > (int32_t)(SHIP_MAX_SPEED >> 8) * (SHIP_MAX_SPEED >> 8)) {
            ship_dx = slow15(ship_dx);
            ship_dy = slow15(ship_dy);
        }
    }

    /* drag: (dx * 253) / 256 = dx - 3 * dx / 256 */
    ship_dx -= (int16_t)(ship_dx >> 8) * 3;
    ship_dy -= (int16_t)(ship_dy >> 8) * 3;
    ship_x = wrapf(ship_x + ship_dx, FX16(WORLD_W));
    ship_y = wrapf(ship_y + ship_dy, FX16(WORLD_H));
    ship_px = (int16_t)(ship_x >> 16);
    ship_py = (int16_t)(ship_y >> 16);

    if (ship_invuln > 0) ship_invuln--;

    if (joy_fire && (gs_frame & 7) == 0) {     /* one shot every 8 frames */
        fire_bullet();
        sfx_shoot();
    }
}

void game_update(uint8_t joy_left, uint8_t joy_right, uint8_t joy_up, uint8_t joy_fire)
{
    uint8_t i, j, sz;

    gs_frame++;
    gs_hits = 0;

    if (gs_state == STATE_GAMEOVER) {
        /* port: the original started a new game here; this one goes back
           to the title (main.c) */
        if (--gs_state_timer <= 0) gs_state = STATE_TITLE;
        objs_still = 1;                 /* frozen, but in this buffer's SCBs */
        rocks_move();
        bullets_move();
        objs_still = 0;
        goto update_particles;
    }

    if (gs_state == STATE_DEAD) {
        if (--gs_state_timer <= 0) {
            ship_reset();
            gs_state = STATE_PLAYING;
        }
        objs_still = 1;                 /* the bullets wait (as the original) */
        bullets_move();
        objs_still = 0;
        goto update_rocks_and_particles;
    }

    ship_controls(joy_left, joy_right, joy_up, joy_fire);

    bullets_move();

    /* -- bullet vs rock -- (the original's circle test, radius 2 + rock) */
    for (i = 0; (i = bullet_scan(i)) != 0xFF; ++i) {
        {
            j = hit_rock;
            bl_active[i] = 0;
            sz = rk_size[j];
            add_score(sz == ROCK_LARGE ? SCORE_LARGE : (sz == ROCK_MEDIUM ? SCORE_MEDIUM : SCORE_SMALL));
            spawn_particles(rk_x[j], rk_y[j], sz == ROCK_LARGE ? 12 : (sz == ROCK_MEDIUM ? 8 : 5),
                            sz == ROCK_SMALL ? C_GREY_LT : C_BRIGHT_YELLOW);
            split_rock(j);
            rk_active[j] = 0;
            gs_rock_count--;
            gs_hits++;
            if (sz == ROCK_LARGE) sfx_explode_large();
            else sfx_explode_small();
        }
    }

    /* -- ship vs rock -- */
    if (ship_alive && ship_invuln == 0) {
        hit_x = ship_px;
        hit_y = ship_py;
        hit_sx = (uint8_t)(ship_px >> 1);
        hit_sy = (uint8_t)(ship_py >> 1);
        if (hit_scan(SHIP_RADIUS) != 0xFF) {
            ship_alive = 0;
            spawn_particles((uint16_t)(ship_x >> 9), (uint16_t)(ship_y >> 9), 20, C_ORANGE);
            sfx_die();
            if (--gs_lives <= 0) {
                gs_state = STATE_GAMEOVER;
                gs_state_timer = 150;
            } else {
                gs_state = STATE_DEAD;
                gs_state_timer = 100;
            }
        }
    }

update_rocks_and_particles:
    rocks_move();
    if (gs_rock_count <= 0 && gs_state == STATE_PLAYING) {
        gs_level++;
        game_spawn_rocks(TUNE_START_ROCKS + (gs_level - 1) * TUNE_ROCKS_PER_LEVEL);
    }

update_particles:
    if (particles_live) particles_move();
}
