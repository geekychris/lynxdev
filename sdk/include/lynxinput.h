/*
 * lynxinput.h - the joypad, buttons and PAUSE.
 *
 * Call input_update() once per frame; then input_held has what is down and
 * input_new what went down since the previous update. Bits are the
 * JOYSTICK register's ($FCB0) plus PAUSE as bit 8: the same bits the
 * harness uses (`emu_press("A")`, `input buttons=`).
 */
#ifndef LYNXINPUT_H
#define LYNXINPUT_H

#include "lynxhw.h"

#define IN_UP     0x80
#define IN_DOWN   0x40
#define IN_LEFT   0x20
#define IN_RIGHT  0x10
#define IN_OPT1   0x08
#define IN_OPT2   0x04
#define IN_B      0x02   /* inner button */
#define IN_A      0x01   /* outer button */
#define IN_PAUSE  0x100
#define IN_DIRS   (IN_UP | IN_DOWN | IN_LEFT | IN_RIGHT)

extern uint16_t input_held;
extern uint16_t input_new;

/* Read the pad; returns input_held. */
uint16_t input_update(void);
/* The pad right now (no edge tracking). */
uint16_t input_read(void);

#endif
