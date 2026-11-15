/*
 * Frank the Frog - the player, from amiga_mcp/examples/frank_the_frog/frank.c
 * by way of the a8dev port (drawing in render.c).
 */
#include "game.h"
#include "frank.h"

int16_t frank_probe[3];          /* x (pixels), row, game state */
static uint8_t frog_col;
uint8_t frank_dying, frank_death_timer, frank_highest_row;
uint8_t frank_facing;

void frank_init(void)
{
    frog_col = GRID_COLS / 2;
    frog_row = ROW_START;
    frog_px = frog_col * TILE_W;
    frank_death_timer = 0;
    frank_dying = 0;
    frank_highest_row = ROW_START;
    frank_facing = 0;
}

void frank_move(int8_t dx, int8_t dy)
{
    int8_t new_col = (int8_t)frog_col + dx;
    int8_t new_row = (int8_t)frog_row + dy;

    if (frank_dying) return;
    frank_facing = dy < 0 ? 0 : dy > 0 ? 1 : dx < 0 ? 2 : 3;
    if (new_col < 0 || new_col >= GRID_COLS) return;
    if (new_row < 0 || new_row >= GRID_ROWS) return;

    frog_col = (uint8_t)new_col;
    frog_row = new_row;
    frog_px = frog_col * TILE_W;
    if (frog_row < frank_highest_row) frank_highest_row = (uint8_t)frog_row;
}

uint8_t frank_col(void) { return frog_col; }

void frank_set_x(int16_t x)
{
    frog_px = x;
    frog_col = (uint8_t)(x / TILE_W);     /* (x >= -4: rounds toward 0 like the original) */
}

void frank_start_death(void)
{
    frank_dying = 1;
    frank_death_timer = 30;
}

uint8_t frank_die_tick(void)
{
    if (!frank_dying) return 0;
    if (--frank_death_timer == 0) {
        frank_dying = 0;
        return 0;
    }
    return 1;
}
