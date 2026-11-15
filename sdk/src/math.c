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

uint32_t __fastcall__ math_mul16(uint16_t a, uint16_t b)
{
    MATHD = (uint8_t)b;
    MATHC = (uint8_t)(b >> 8);
    MATHB = (uint8_t)a;
    MATHA = (uint8_t)(a >> 8);              /* starts */
    while (SPRSYS_R & 0x80) ;
    return (uint32_t)MATHH | ((uint32_t)MATHG << 8) | ((uint32_t)MATHF << 16) | ((uint32_t)MATHE << 24);
}

uint32_t __fastcall__ math_div32(uint32_t num, uint16_t den)
{
    MATHP = (uint8_t)den;
    MATHN = (uint8_t)(den >> 8);
    MATHH = (uint8_t)num;
    MATHG = (uint8_t)(num >> 8);
    MATHF = (uint8_t)(num >> 16);
    MATHE = (uint8_t)(num >> 24);           /* starts */
    while (SPRSYS_R & 0x80) ;
    math_rem = (uint16_t)MATHM | ((uint16_t)MATHL << 8);
    return (uint32_t)MATHD | ((uint32_t)MATHC << 8) | ((uint32_t)MATHB << 16) | ((uint32_t)MATHA << 24);
}
