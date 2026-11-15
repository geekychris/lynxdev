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

/* clock select 0..6 = 1 MHz / 2^n */
static void set_period(uint8_t ch, uint16_t hz)
{
    uint32_t half;
    uint8_t sel = 0;
    if (hz < 16) hz = 16;
    half = 500000UL / hz;               /* timer underflows per second / 1 MHz */
    while (half > 256 && sel < 6) { half >>= 1; ++sel; }
    if (half > 256) half = 256;
    if (half < 1) half = 1;
    LYNX_AUD(ch, LYNX_AUD_BKUP) = (uint8_t)(half - 1);
    LYNX_AUD(ch, LYNX_AUD_CTLA) = (uint8_t)(0x18 | sel);   /* reload + count, clock */
    hz_now[ch] = hz;
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
    LYNX_AUD(ch, LYNX_AUD_VOL) = 0;
    LYNX_AUD(ch, LYNX_AUD_CTLA) = 0;
    LYNX_AUD(ch, LYNX_AUD_OUT) = 0;
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
    ch &= 3;
    LYNX_AUD(ch, LYNX_AUD_CTLA) = 0;
    LYNX_AUD(ch, LYNX_AUD_FEED) = 0x01;        /* tap 0 only: square wave */
    LYNX_AUD(ch, LYNX_AUD_SHIFT) = 0;
    LYNX_AUD(ch, LYNX_AUD_CTLB) = 0;
    LYNX_AUD(ch, LYNX_AUD_VOL) = v & 0x7F;
    vol[ch] = v & 0x7F;
    decay[ch] = d;
    sweep[ch] = 0;
    set_period(ch, hz);
}

void snd_noise(uint8_t ch, uint8_t pitch, uint8_t v, uint8_t d)
{
    ch &= 3;
    LYNX_AUD(ch, LYNX_AUD_CTLA) = 0;
    LYNX_AUD(ch, LYNX_AUD_FEED) = 0x33;        /* taps 0,1,4,5: long sequence */
    LYNX_AUD(ch, LYNX_AUD_SHIFT) = 0x5A;
    LYNX_AUD(ch, LYNX_AUD_CTLB) = 0x10;
    LYNX_AUD(ch, LYNX_AUD_VOL) = v & 0x7F;
    LYNX_AUD(ch, LYNX_AUD_BKUP) = (uint8_t)(4 + (pitch << 3));
    LYNX_AUD(ch, LYNX_AUD_CTLA) = (uint8_t)(0x18 | 1);
    vol[ch] = v & 0x7F;
    decay[ch] = d;
    sweep[ch] = 0;
}

void snd_freq(uint8_t ch, uint16_t hz)
{
    set_period(ch & 3, hz);
}

void snd_sweep(uint8_t ch, int16_t hz_per_tick)
{
    sweep[ch & 3] = hz_per_tick;
}

void snd_tick(void)
{
    uint8_t ch;
    for (ch = 0; ch < 4; ++ch) {
        if (!vol[ch]) continue;
        if (decay[ch]) {
            if (vol[ch] <= decay[ch]) { snd_off(ch); continue; }
            vol[ch] -= decay[ch];
            LYNX_AUD(ch, LYNX_AUD_VOL) = vol[ch];
        }
        if (sweep[ch]) {
            int16_t h = (int16_t)hz_now[ch] + sweep[ch];
            if (h < 16 || h > 20000) { snd_off(ch); continue; }
            set_period(ch, (uint16_t)h);
        }
    }
}

uint8_t snd_active(void)
{
    uint8_t ch, m = 0;
    for (ch = 0; ch < 4; ++ch) if (vol[ch]) m |= (uint8_t)(1 << ch);
    return m;
}
