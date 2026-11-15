/* Frank the Frog - the player (port of frank.h; drawing in render.c) */
#ifndef FRANK_H
#define FRANK_H
#include <stdint.h>
/* frank_probe: x, row, state (the test's bot reads it) */
extern int16_t frank_probe[3];
#define frog_px   (frank_probe[0])
#define frog_row  (frank_probe[1])
void frank_init(void);
void frank_move(int8_t dx, int8_t dy);
uint8_t frank_col(void);
void frank_set_x(int16_t x);
void frank_start_death(void);
extern uint8_t frank_facing;   /* port: 0 up, 1 down, 2 left, 3 right (drawing) */
uint8_t frank_die_tick(void);
extern uint8_t frank_dying, frank_death_timer, frank_highest_row;
#endif
