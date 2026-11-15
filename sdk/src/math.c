/* lynxmath.h: Suzy maths. Operands are written low byte first (writing the
   low byte of a pair clears its high byte); writing MATHA starts a multiply,
   MATHE a divide; SPRSYS bit 7 reads 1 while it works. */
#include "lynxmath.h"

#define R(a) LYNX_REG8(a)
#define MATHD R(0xFC52)
#define MATHC R(0xFC53)
#define MATHB R(0xFC54)
#define MATHA R(0xFC55)
#define MATHP R(0xFC56)
#define MATHN R(0xFC57)
#define MATHH R(0xFC60)
#define MATHG R(0xFC61)
#define MATHF R(0xFC62)
#define MATHE R(0xFC63)
#define MATHM R(0xFC6C)
#define MATHL R(0xFC6D)
#define SPRSYS_R R(0xFC92)

uint16_t math_rem;
static union { uint32_t l; uint8_t b[4]; } res;   /* byte access: no 32-bit shifts */

uint32_t __fastcall__ math_mul16(uint16_t a, uint16_t b)
{
    MATHD = (uint8_t)b;
    MATHC = (uint8_t)(b >> 8);
    MATHB = (uint8_t)a;
    MATHA = (uint8_t)(a >> 8);              /* starts */
    /* (the result is read as bytes below: cc65's 32-bit shifts cost ~1k cycles) */
    while (SPRSYS_R & 0x80) ;
    res.b[0] = MATHH;
    res.b[1] = MATHG;
    res.b[2] = MATHF;
    res.b[3] = MATHE;
    return res.l;
}

uint32_t __fastcall__ math_div32(uint32_t num, uint16_t den)
{
    res.l = num;
    MATHP = (uint8_t)den;
    MATHN = (uint8_t)(den >> 8);
    MATHH = res.b[0];
    MATHG = res.b[1];
    MATHF = res.b[2];
    MATHE = res.b[3];                       /* starts */
    while (SPRSYS_R & 0x80) ;
    math_rem = (uint16_t)MATHM | ((uint16_t)MATHL << 8);
    res.b[0] = MATHD;
    res.b[1] = MATHC;
    res.b[2] = MATHB;
    res.b[3] = MATHA;
    return res.l;
}
