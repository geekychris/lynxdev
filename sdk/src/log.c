/* lynxlog.h: lynx_log (printf-style; pulls in vsnprintf). The plain
   functions are in log_s.c, so a program that only uses those doesn't link
   printf. */
#include <stdarg.h>
#include <stdio.h>
#include "lynxlog.h"

#ifndef LYNX_NO_LOG
void __cdecl__ lynx_log(const char *fmt, ...)
{
    static char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    lynx_log_s(buf);
}
#endif
