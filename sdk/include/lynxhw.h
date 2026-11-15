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
#define LYNX_SDK_VERSION 10200

/* cc65 2.18's <lynx.h> (LYNX_NATIVE=1 with an older cc65) lacks some of the
   default palette's names: the same values as newer cc65. */
#ifndef COLOR_TRANSPARENT
#define COLOR_TRANSPARENT 0x00
#endif
#ifndef COLOR_BLACK
#define COLOR_BLACK 0x01
#endif
#ifndef COLOR_RED
#define COLOR_RED 0x02
#endif
#ifndef COLOR_PINK
#define COLOR_PINK 0x03
#endif
#ifndef COLOR_LIGHTGREY
#define COLOR_LIGHTGREY 0x04
#endif
#ifndef COLOR_GREY
#define COLOR_GREY 0x05
#endif
#ifndef COLOR_DARKGREY
#define COLOR_DARKGREY 0x06
#endif
#ifndef COLOR_BROWN
#define COLOR_BROWN 0x07
#endif
#ifndef COLOR_PEACH
#define COLOR_PEACH 0x08
#endif
#ifndef COLOR_YELLOW
#define COLOR_YELLOW 0x09
#endif
#ifndef COLOR_LIGHTGREEN
#define COLOR_LIGHTGREEN 0x0A
#endif
#ifndef COLOR_GREEN
#define COLOR_GREEN 0x0B
#endif
#ifndef COLOR_PURPLE
#define COLOR_PURPLE 0x0C
#endif
#ifndef COLOR_BLUE
#define COLOR_BLUE 0x0D
#endif
#ifndef COLOR_LIGHTBLUE
#define COLOR_LIGHTBLUE 0x0E
#endif
#ifndef COLOR_WHITE
#define COLOR_WHITE 0x0F
#endif

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
