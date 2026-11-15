/* Frank the Frog - the Lynx display: one chain of Suzy sprites a frame */
#ifndef RENDER_H
#define RENDER_H
#include <stdint.h>

void render_init(void);          /* palette, every SCB, the chain */
void render_lanes_setup(void);   /* the lane objects' pictures (new level) */
void render_frame(uint8_t state);/* positions -> draw the chain (field, objects, HUD) */
void render_hud(void);           /* the HUD digits from the score (when it changes) */
/* overlays: glyphs drawn at the end of the chain (built on a change) */
void txt_clear(void);
void render_title(void);         /* big Frank, the name, PRESS A, options */
void render_gameover(uint8_t prompt);
void render_level_done(void);
void render_paused(void);
void render_blink(uint8_t off);  /* the PRESS A line on/off */
extern uint16_t time_left;       /* frames left for this frog */
extern uint8_t start_level;      /* main.c: the title's starting level */
#endif
