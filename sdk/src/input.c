/* lynxinput.h */
#include "lynxinput.h"

uint16_t input_held;
uint16_t input_new;

uint16_t input_read(void)
{
    uint16_t v = LYNX_JOYSTICK;
    if (LYNX_SWITCHES & 0x01) v |= IN_PAUSE;
    return v;
}

uint16_t input_update(void)
{
    uint16_t v = input_read();
    input_new = v & ~input_held;
    input_held = v;
    return v;
}
