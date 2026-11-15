/* Frank the Frog - Mikey sound effects and the riff (port of sound.c) */
#ifndef SOUND_H
#define SOUND_H
#include <stdint.h>
enum { S_HOP, S_SPLAT, S_SPLASH, S_HOME, S_LEVEL, S_OVER, S_TIME };
void sound_init(void);
void sound(uint8_t s);
void sound_tick(void);          /* once a frame: decays, jingles, music */
void music_play(void);
void music_stop(void);
extern uint8_t music_on;
#endif
