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

void snd_init(void);
/* A square wave of `hz` (about 16 Hz .. 20 kHz) at volume vol, losing
   `decay` volume per snd_tick (0 = until snd_off). */
void snd_tone(uint8_t ch, uint16_t hz, uint8_t vol, uint8_t decay);
/* Noise: pitch 0 (high) .. 7 (low) */
void snd_noise(uint8_t ch, uint8_t pitch, uint8_t vol, uint8_t decay);
/* Change a playing tone's frequency (no restart). */
void snd_freq(uint8_t ch, uint16_t hz);
/* Per frame: frequency sweep (Hz per tick, signed; 0 = none) for a tone. */
void snd_sweep(uint8_t ch, int16_t hz_per_tick);
void snd_off(uint8_t ch);
void snd_all_off(void);
/* Call once per frame: applies decays and sweeps. */
void snd_tick(void);
/* Bit n set while channel n plays. */
uint8_t snd_active(void);

#endif
