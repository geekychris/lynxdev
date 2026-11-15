/* lynxh.h - lynxdev harness hooks for Handy (see emulator/README.md).
 *
 * The patches (emulator/patches) add calls to these functions in Handy's
 * core; everything else lives in lynxh.cpp, which also owns the main loop
 * (Handy is driven by our own frontends, not by libretro).
 */
#ifndef LYNXH_H_
#define LYNXH_H_

/* Guest log channel (the contract with sdk/include/lynxlog.h): one character
   per store to $FCF0, a signal (1 DONE, 2 FAIL) per store to $FCF1. */
#define LYNXH_LOG_ADDR_C    0xFCF0
#define LYNXH_SIGNAL_ADDR_C 0xFCF1

/* Non-zero while the CPU hook must run before every instruction. */
extern int gLynxhCpuCheck;

/* Called by C65C02::Update() before each opcode fetch while gLynxhCpuCheck
   (after a pending IRQ was taken). Returns non-zero to stop before the
   instruction at pc: Update() returns without executing it. */
int LYNXH_CpuHook(int pc);

/* Stores to unassigned Suzy addresses ($FCF0 = guest log character,
   $FCF1 = signal). */
void LYNXH_SuzyPoke(unsigned long addr, unsigned char data);
/* Reads of unassigned Suzy addresses: $FCF0 -> 'L', $FCF1 -> 'Y' in the
   harness; -1 = not ours (Handy returns $FF). */
int LYNXH_SuzyPeek(unsigned long addr);

/* Suzy starts painting a sprite chain (SPRGO) at scb. */
void LYNXH_SpriteChain(unsigned scb);

/* Frontends. */
struct lynxh_frontend {
	int window;                         /* 1: SDL window build */
	/* called while the emulator waits for commands (paused or stopped),
	   about every 15 ms: keep the window alive. redraw: first call of a wait */
	void (*idle)(int redraw);
	/* called after each completed frame (free-running window: present + pace) */
	void (*frame)(void);
	/* user input from the window (buttons, same bits as `input buttons=`) */
	unsigned (*buttons)(void);
	/* audio samples (stereo int16) produced by the last frame */
	void (*audio)(const short *samples, int frames);
};

/* Parse options, open the socket, run forever. */
int LYNXH_Main(int argc, char *argv[], const struct lynxh_frontend *fe);

/* For frontends: the last completed frame, 160x102 0x00RRGGBB, unrotated,
   and the rotation chosen (0 none, 1 left, 2 right). */
const unsigned int *LYNXH_Frame(void);
int LYNXH_Rotate(void);
int LYNXH_Zoom(void);
int LYNXH_Sound(void);
/* Ask the main loop to quit (window closed). */
void LYNXH_RequestQuit(void);

#endif /* LYNXH_H_ */
