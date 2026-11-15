/*
 * lynxlog.h - debug log for programs running in the lynxdev harness.
 *
 * Every byte stored to LYNX_LOG_ADDR ($FCF0) is one character of the log
 * ('\n' ends a line); a byte stored to LYNX_SIGNAL_ADDR ($FCF1) is a status
 * event: 1 = DONE, 2 = FAIL, anything else a user signal. The patched Handy
 * captures both: ./lynx log, MCP emu_log / emu_run_until, Emulator.run_until()
 * in tests. $FCF0/$FCF1 are unused Suzy addresses: a real Lynx ignores the
 * stores (cc65's crt0 leaves Suzy mapped at $FC00, MAPCTL = $0C).
 *
 * Release builds emit nothing: with LYNX_RELEASE, NDEBUG or LYNX_NO_LOG
 * defined (project.mk: RELEASE=1 or LYNX_NO_LOG=1) every call compiles away,
 * so a shipped game never touches these addresses.
 *
 * Convention: one line per event, "<program>: <what> key=value...\n".
 * A program without a test.py passes `./lynx test` by logging "LYNX:DONE"
 * (lynx_done(), signal 1) or fails with "LYNX:FAIL <why>" (lynx_fail(),
 * signal 2).
 *
 * This header is the only place the addresses are defined.
 */
#ifndef LYNXLOG_H
#define LYNXLOG_H

#define LYNX_LOG_ADDR     0xFCF0
#define LYNX_SIGNAL_ADDR  0xFCF1
#define LYNX_LOG_PORT     (*(volatile unsigned char *)LYNX_LOG_ADDR)
#define LYNX_SIGNAL_PORT  (*(volatile unsigned char *)LYNX_SIGNAL_ADDR)

#define LYNX_SIGNAL_DONE  1
#define LYNX_SIGNAL_FAIL  2

#if defined(LYNX_RELEASE) || defined(NDEBUG)
#ifndef LYNX_NO_LOG
#define LYNX_NO_LOG
#endif
#endif

#ifdef LYNX_NO_LOG
#define lynx_log(...)     ((void)0)
#define lynx_log_s(s)     ((void)0)
#define lynx_log_c(c)     ((void)0)
#define lynx_log_u(v)     ((void)0)
#define lynx_log_x(v)     ((void)0)
#define lynx_signal(v)    ((void)0)
#define lynx_done()       ((void)0)
#define lynx_fail(why)    ((void)0)
#define lynx_in_harness() 0
#else
/* printf-style (cc65: %d/%u/%x are 16-bit int, %ld/%lu for long). Output is
   cut at 127 characters. Pulls in vsnprintf (~2 KB). */
extern void __cdecl__ lynx_log(const char *fmt, ...);
/* A plain string: tiny and fast (no formatting code linked). */
extern void __fastcall__ lynx_log_s(const char *s);
/* One character. */
#define lynx_log_c(c)  (LYNX_LOG_PORT = (unsigned char)(c))
/* An unsigned decimal / 4-digit hex number (no newline), without vsnprintf. */
extern void __fastcall__ lynx_log_u(unsigned int v);
extern void __fastcall__ lynx_log_x(unsigned int v);
/* A status event (the harness can stop on it). 1 and 2 are DONE and FAIL. */
#define lynx_signal(v) (LYNX_SIGNAL_PORT = (unsigned char)(v))
/* Test markers: "LYNX:DONE\n" + signal 1, "LYNX:FAIL <why>\n" + signal 2. */
extern void lynx_done(void);
extern void __fastcall__ lynx_fail(const char *why);
/* 1 when running in the lynxdev harness: there $FCF0 reads $4C ('L') and
   $FCF1 reads $59 ('Y'). Use it for test conveniences only, never for game
   logic. */
extern unsigned char lynx_in_harness(void);
#endif

#endif
