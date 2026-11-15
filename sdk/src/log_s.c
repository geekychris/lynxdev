/* lynxlog.h: lynx_log_x, lynx_done, lynx_fail,
   lynx_in_harness (no printf; lynx_log itself is log.c). */
#include "lynxlog.h"

#ifndef LYNX_NO_LOG
/* lynx_log_s and lynx_log_u are in log_fast.s (asm). */

void __fastcall__ lynx_log_x(unsigned int v)
{
    static const char hex[] = "0123456789ABCDEF";
    LYNX_LOG_PORT = hex[(v >> 12) & 15];
    LYNX_LOG_PORT = hex[(v >> 8) & 15];
    LYNX_LOG_PORT = hex[(v >> 4) & 15];
    LYNX_LOG_PORT = hex[v & 15];
}

unsigned char lynx_in_harness(void)
{
    unsigned char i;
    for (i = 0; i < 2; ++i)
        if (LYNX_LOG_PORT != 0x4C || LYNX_SIGNAL_PORT != 0x59) return 0;
    return 1;
}

void lynx_done(void)
{
    lynx_log_s("LYNX:DONE\n");
    lynx_signal(LYNX_SIGNAL_DONE);
}

void __fastcall__ lynx_fail(const char *why)
{
    lynx_log_s("LYNX:FAIL ");
    lynx_log_s(why);
    LYNX_LOG_PORT = '\n';
    lynx_signal(LYNX_SIGNAL_FAIL);
}
#endif
