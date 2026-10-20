/* printf-style formatting and a log line for the game's own translation units
 * (src/, and pc/src files compiled like it: pc/src/ui/classic_options.c),
 * whose headers have no host stdio (host_format.h). */
#include "host_format.h"
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

int pc_format(char* out, int size, const char* format, ...)
{
    va_list args;
    int n;
    if (!out || size <= 0)
        return 0;
    va_start(args, format);
    n = vsnprintf(out, (size_t)size, format, args);
    va_end(args);
    return n < 0 ? 0 : n;
}

void pc_log_line(const char* text)
{
    fprintf(stderr, "%s\n", text ? text : "");
}

double pc_host_seconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}
