/* lynxsnd.h: Mikey audio channels as tone / noise generators.
 *
 * Each channel is a timer that clocks a 12-bit LFSR; the output bit selects
 * +volume or -volume. With the feedback tap on bit 0 only, the register
 * alternates 0,1,0,1 (Mikey's feedback is inverted): a square wave of
 * clock / (2 * (backup + 1)). More taps give noise.
 */
#include "lynxsnd.h"

static uint8_t vol[4], decay[4];
static int16_t sweep[4];
static uint16_t hz_now[4];

#define R(a) LYNX_REG8(a)

/* snd_period is in snd_period.s (Suzy divide, ~150 cycles). */

#pragma static-locals (on)

/* the channel's 8 registers: one pointer, then (ptr),y stores (cheap in cc65) */
#define CH(ch) ((volatile uint8_t *)(0xFD20 + ((ch) << 3)))

static void set_period(volatile uint8_t *a, uint16_t period)
{
    a[LYNX_AUD_BKUP] = (uint8_t)period;
    a[LYNX_AUD_CTLA] = (uint8_t)(0x18 | (uint8_t)(period >> 8));   /* reload + count, clock */
}

void snd_init(void)
{
    uint8_t ch;
    LYNX_MSTEREO = 0;                   /* every channel to both sides */
    LYNX_REG8(0xFD44) = 0;              /* MPAN: no attenuation (Lynx II) */
    for (ch = 0; ch < 4; ++ch) snd_off(ch);
}

void snd_off(uint8_t ch)
{
    volatile uint8_t *a;
    ch &= 3;
    a = CH(ch);
    a[LYNX_AUD_VOL] = 0;
    a[LYNX_AUD_CTLA] = 0;
    a[LYNX_AUD_OUT] = 0;
    vol[ch] = 0;
    sweep[ch] = 0;
}

void snd_all_off(void)
{
    uint8_t ch;
    for (ch = 0; ch < 4; ++ch) snd_off(ch);
}

void snd_tone(uint8_t ch, uint16_t hz, uint8_t v, uint8_t d)
{
    snd_tone_p(ch, snd_period(hz), v, d);
    hz_now[ch & 3] = hz;
}

void snd_tone_p(uint8_t ch, uint16_t period, uint8_t v, uint8_t d)
{
    volatile uint8_t *a;
    ch &= 3;
    a = CH(ch);
    v &= 0x7F;
    a[LYNX_AUD_CTLA] = 0;
    a[LYNX_AUD_FEED] = 0x01;            /* tap 0 only: square wave */
    a[LYNX_AUD_SHIFT] = 0;
    a[LYNX_AUD_CTLB] = 0;
    a[LYNX_AUD_VOL] = v;
    vol[ch] = v;
    decay[ch] = d;
    sweep[ch] = 0;
    hz_now[ch] = 0;
    set_period(a, period);
}

void snd_noise(uint8_t ch, uint8_t pitch, uint8_t v, uint8_t d)
{
    volatile uint8_t *a;
    ch &= 3;
    a = CH(ch);
    v &= 0x7F;
    a[LYNX_AUD_CTLA] = 0;
    a[LYNX_AUD_FEED] = 0x33;            /* taps 0,1,4,5: long sequence */
    a[LYNX_AUD_SHIFT] = 0x5A;
    a[LYNX_AUD_CTLB] = 0x10;
    a[LYNX_AUD_VOL] = v;
    a[LYNX_AUD_BKUP] = (uint8_t)(4 + (pitch << 3));
    a[LYNX_AUD_CTLA] = (uint8_t)(0x18 | 1);
    vol[ch] = v;
    decay[ch] = d;
    sweep[ch] = 0;
}

void snd_freq(uint8_t ch, uint16_t hz)
{
    ch &= 3;
    set_period(CH(ch), snd_period(hz));
    hz_now[ch] = hz;
}

void snd_freq_p(uint8_t ch, uint16_t period)
{
    ch &= 3;
    set_period(CH(ch), period);
    hz_now[ch] = 0;
}

void snd_sweep(uint8_t ch, int16_t hz_per_tick)
{
    sweep[ch & 3] = hz_per_tick;
}

void snd_tick(void)
{
    uint8_t ch, v, d;
    int16_t h;
    for (ch = 0; ch < 4; ++ch) {
        v = vol[ch];
        if (!v) continue;
        d = decay[ch];
        if (d) {
            if (v <= d) { snd_off(ch); continue; }
            v -= d;
            vol[ch] = v;
            CH(ch)[LYNX_AUD_VOL] = v;
        }
        if (sweep[ch] && hz_now[ch]) {
            h = (int16_t)hz_now[ch] + sweep[ch];
            if (h < 16 || h > 20000) { snd_off(ch); continue; }
            set_period(CH(ch), snd_period((uint16_t)h));
            hz_now[ch] = (uint16_t)h;
        }
    }
}

uint8_t snd_active(void)
{
    uint8_t m = 0;
    if (vol[0]) m |= 1;
    if (vol[1]) m |= 2;
    if (vol[2]) m |= 4;
    if (vol[3]) m |= 8;
    return m;
}
