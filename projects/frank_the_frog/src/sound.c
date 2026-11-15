/*
 * Frank the Frog - sound. The original builds sampled effects for Paula
 * (hop blip, splat crunch, splash, home jingle, level-up fanfare, game-over
 * fall) and plays a MOD. Here: Mikey's four channels.
 *   0  hop / time-out sweeps   1  noise (splat, splash)
 *   2  jingles (home, level, game over: note lists)
 *   3  the riff (the original tune's E-F chromatic bass, as in the a8 port)
 *
 * port (speed): its own small driver instead of lynxsnd.h's snd_tone, which
 * works the timer period out with a 32-bit division (about 12,000 cycles,
 * and again every frame while a tone sweeps): the periods here are
 * constants the compiler works out (PER(hz)), and a sweep steps the timer's
 * reload value.
 */
#include "lynxsnd.h"
#include "sound.h"

uint8_t music_on = 1;

/* a Mikey channel timer for a square wave of hz: clock 1 MHz >> sel, reload
   bkup, two underflows a cycle (sdk/src/sound.c set_period, at compile time) */
#define HALF(hz)  (500000UL / (hz))
#define SEL(hz)   (HALF(hz) <= 256 ? 0 : HALF(hz) <= 512 ? 1 : HALF(hz) <= 1024 ? 2 : \
                   HALF(hz) <= 2048 ? 3 : HALF(hz) <= 4096 ? 4 : HALF(hz) <= 8192 ? 5 : 6)
#define PER(hz)   ((uint16_t)((SEL(hz) << 8) | ((HALF(hz) >> SEL(hz)) - 1)))

#pragma warn (const-comparison, off)   /* SEL()'s constant comparisons */

/* a note list: period, frames; 0 = rest; {0, 0} ends */
typedef struct { uint16_t per; uint8_t len; } note;

static const note jingle_home[] = { {PER(523), 4}, {PER(659), 4}, {PER(784), 6}, {0, 0} };
static const note jingle_level[] = {
    {PER(523), 6}, {PER(659), 6}, {PER(784), 6}, {PER(1047), 10}, {0, 4}, {PER(784), 5},
    {PER(1047), 16}, {0, 0} };
static const note jingle_over[] = {
    {PER(392), 12}, {PER(370), 12}, {PER(349), 12}, {PER(330), 28}, {0, 0} };

/* E3 E3 F3 E3 G3 E3 E3 F3 E3 A3 E3 E3 F3 G3 F3 (the a8 port's riff), an
   octave up for the Lynx speaker */
#define E PER(330)
#define F PER(349)
#define G PER(392)
#define A PER(440)
static const note riff[] = {
    {E, 6}, {E, 3}, {F, 3}, {E, 6}, {G, 6}, {E, 6}, {E, 3}, {F, 3},
    {E, 6}, {A, 6}, {E, 6}, {E, 3}, {F, 3}, {G, 6}, {F, 6}, {0, 0} };

static const note *jin, *mus;
static uint8_t jin_left, mus_left;
static uint8_t vol[4], decay[4];
static int8_t sweep[4];            /* added to the reload value each frame */

static void off(uint8_t ch)
{
    LYNX_AUD(ch, LYNX_AUD_VOL) = 0;
    LYNX_AUD(ch, LYNX_AUD_CTLA) = 0;
    LYNX_AUD(ch, LYNX_AUD_OUT) = 0;
    vol[ch] = 0;
}

static void tone(uint8_t ch, uint16_t per, uint8_t v, uint8_t d, int8_t sw)
{
    LYNX_AUD(ch, LYNX_AUD_CTLA) = 0;
    LYNX_AUD(ch, LYNX_AUD_FEED) = 0x01;        /* tap 0 only: square wave */
    LYNX_AUD(ch, LYNX_AUD_SHIFT) = 0;
    LYNX_AUD(ch, LYNX_AUD_CTLB) = 0;
    LYNX_AUD(ch, LYNX_AUD_VOL) = v;
    LYNX_AUD(ch, LYNX_AUD_BKUP) = (uint8_t)per;
    LYNX_AUD(ch, LYNX_AUD_CTLA) = (uint8_t)(0x18 | (per >> 8));
    vol[ch] = v;
    decay[ch] = d;
    sweep[ch] = sw;
}

static void noise(uint8_t ch, uint8_t pitch, uint8_t v, uint8_t d)
{
    LYNX_AUD(ch, LYNX_AUD_CTLA) = 0;
    LYNX_AUD(ch, LYNX_AUD_FEED) = 0x33;        /* taps 0,1,4,5: long sequence */
    LYNX_AUD(ch, LYNX_AUD_SHIFT) = 0x5A;
    LYNX_AUD(ch, LYNX_AUD_CTLB) = 0x10;
    LYNX_AUD(ch, LYNX_AUD_VOL) = v;
    LYNX_AUD(ch, LYNX_AUD_BKUP) = (uint8_t)(4 + (pitch << 3));
    LYNX_AUD(ch, LYNX_AUD_CTLA) = (uint8_t)(0x18 | 1);
    vol[ch] = v;
    decay[ch] = d;
    sweep[ch] = 0;
}

void sound_init(void)
{
    snd_init();                 /* both speakers, all channels off */
    jin = 0;
    mus = 0;
}

static void jingle(const note *n)
{
    jin = n;
    jin_left = 0;
}

void sound(uint8_t s)
{
    switch (s) {
    case S_HOP:    tone(0, PER(600), 50, 10, -6); break;     /* blip, rising */
    case S_SPLAT:  noise(1, 5, 110, 5); break;
    case S_SPLASH: noise(1, 1, 90, 3); break;
    case S_HOME:   jingle(jingle_home); break;
    case S_LEVEL:  jingle(jingle_level); break;
    case S_OVER:   jingle(jingle_over); break;
    case S_TIME:   tone(0, PER(300), 90, 3, 2); break;       /* falling */
    }
}

void music_play(void)
{
    mus = music_on ? riff : 0;
    mus_left = 0;
}

void music_stop(void)
{
    mus = 0;
    off(3);
}

/* step a note list: returns the list's new position (0 at the end) */
static const note *step(const note *n, uint8_t *left, uint8_t ch, uint8_t v, uint8_t d)
{
    if (*left) {
        --*left;
        return n;
    }
    if (!n->len) {
        off(ch);
        return 0;
    }
    if (n->per) tone(ch, n->per, v, d, 0);
    else off(ch);
    *left = n->len - 1;
    return n + 1;
}

void sound_tick(void)
{
    uint8_t ch, b;
    for (ch = 0; ch < 4; ++ch) {
        if (!vol[ch] || !decay[ch]) continue;
        if (vol[ch] <= decay[ch]) {
            off(ch);
            continue;
        }
        vol[ch] -= decay[ch];
        LYNX_AUD(ch, LYNX_AUD_VOL) = vol[ch];
        if (sweep[ch]) {
            b = (uint8_t)(LYNX_AUD(ch, LYNX_AUD_BKUP) + sweep[ch]);
            LYNX_AUD(ch, LYNX_AUD_BKUP) = b;
        }
    }
    if (jin) jin = step(jin, &jin_left, 2, 70, 2);
    if (mus) {
        mus = step(mus, &mus_left, 3, 28, 3);
        if (!mus) {
            mus = riff;          /* loop */
            mus_left = 0;
        }
    }
}
