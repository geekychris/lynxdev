/*
 * lynxhw.h - Lynx hardware basics shared by every SDK header.
 *
 * cc65's <lynx.h> (included here) already has MIKEY / SUZY register structs,
 * the SCB_* sprite control block types and the JOYPAD_* / BUTTON_* bits.
 * This adds the SDK version, plain register addresses and colour helpers.
 */
#ifndef LYNXHW_H
#define LYNXHW_H

#include <stdint.h>
#include <lynx.h>

/* major * 10000 + minor * 100 + patch (sdk/README.md: versioning rules) */
#define LYNX_SDK_VERSION 10100

#define LYNX_SCREEN_W   160
#define LYNX_SCREEN_H   102

#define LYNX_REG8(a)    (*(volatile uint8_t *)(a))
#define LYNX_JOYSTICK   LYNX_REG8(0xFCB0)
#define LYNX_SWITCHES   LYNX_REG8(0xFCB1)
#define LYNX_MAPCTL     LYNX_REG8(0xFFF9)
#define LYNX_INTSET     LYNX_REG8(0xFD81)
#define LYNX_INTRST     LYNX_REG8(0xFD80)
/* palette: 16 GREEN registers (low nibble) then 16 BLUERED (blue high, red low) */
#define LYNX_GREEN(i)   LYNX_REG8(0xFDA0 + (i))
#define LYNX_BLUERED(i) LYNX_REG8(0xFDB0 + (i))
/* Mikey audio channel n (0..3): 8 registers each from $FD20 */
#define LYNX_AUD(n, r)  LYNX_REG8(0xFD20 + ((n) << 3) + (r))
#define LYNX_AUD_VOL    0
#define LYNX_AUD_FEED   1
#define LYNX_AUD_OUT    2
#define LYNX_AUD_SHIFT  3
#define LYNX_AUD_BKUP   4
#define LYNX_AUD_CTLA   5
#define LYNX_AUD_CNT    6
#define LYNX_AUD_CTLB   7
#define LYNX_MSTEREO    LYNX_REG8(0xFD50)

/* Interrupt bits (INTSET/INTRST): timer n = bit n; timer 2 is the vertical blank. */
#define LYNX_INT_VBL    0x04
#define LYNX_INT_HBL    0x01

#endif
