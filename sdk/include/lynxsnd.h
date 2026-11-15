/*
 * lynxsnd.h - simple Mikey sound effects: square-wave tones, noise, sweeps,
 * each with a per-frame volume decay. Four channels (0-3).
 *
 *   snd_init();
 *   snd_tone(0, 440, 60, 4);       // channel 0: 440 Hz, volume 60, -4 per frame
 *   snd_noise(3, 2, 80, 6);        // channel 3: noise, rough pitch 2, decaying
 *   ...every frame:
 *   snd_tick();
 *
 * A channel stops when its volume reaches 0 (or snd_off). Volumes 0-127.
 * For music use cc65's lynx_snd_* (ABC tunes) or your own player on top of
 * snd_tone.
 */
#ifndef LYNXSND_H
#define LYNXSND_H

#include "lynxhw.h"

/* A tone period known at compile time: SND_PERIOD(440) is a constant
   (clock select << 8 | timer reload) for snd_tone_p / snd_freq_p, which cost
   nothing to compute at run time. snd_tone(hz) itself divides with Suzy's
   hardware divider (a few hundred cycles), so both are cheap. */
#define SND_SEL_(hz)    ((hz) >= 1954 ? 0 : (hz) >= 977 ? 1 : (hz) >= 489 ? 2 : (hz) >= 245 ? 3 : \
                         (hz) >= 123 ? 4 : (hz) >= 62 ? 5 : 6)
#define SND_Q_(hz)      ((500000UL >> SND_SEL_(hz)) / (unsigned long)((hz) < 16 ? 16 : (hz)))
#define SND_BKUP_(hz)   (SND_Q_(hz) > 255UL ? 0xFF : (uint8_t)(SND_Q_(hz) - 1))
#define SND_PERIOD(hz)  ((uint16_t)((SND_SEL_(hz) << 8) | SND_BKUP_(hz)))

void snd_init(void);
/* A square wave of `hz` (about 16 Hz .. 20 kHz) at volume vol, losing
   `decay` volume per snd_tick (0 = until snd_off). */
void snd_tone(uint8_t ch, uint16_t hz, uint8_t vol, uint8_t decay);
/* Noise: pitch 0 (high) .. 7 (low) */
void snd_noise(uint8_t ch, uint8_t pitch, uint8_t vol, uint8_t decay);
/* The same with a period from SND_PERIOD(hz) (or snd_period(hz)). */
void snd_tone_p(uint8_t ch, uint16_t period, uint8_t vol, uint8_t decay);
/* hz -> period at run time (Suzy divide). */
uint16_t __fastcall__ snd_period(uint16_t hz);
/* Change a playing tone's frequency (no restart). */
void snd_freq(uint8_t ch, uint16_t hz);
void snd_freq_p(uint8_t ch, uint16_t period);
/* Per frame: frequency sweep (Hz per tick, signed; 0 = none) for a tone
   started with snd_tone / snd_freq (in Hz; not for snd_tone_p). */
void snd_sweep(uint8_t ch, int16_t hz_per_tick);
void snd_off(uint8_t ch);
void snd_all_off(void);
/* Call once per frame: applies decays and sweeps. */
void snd_tick(void);
/* Bit n set while channel n plays. */
uint8_t snd_active(void);

#endif
