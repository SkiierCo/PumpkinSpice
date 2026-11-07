#include "sdlog.h"

#ifdef PS_SDLOG
#include <coreinit/time.h>
#include <stdarg.h>
#include <stdio.h>

#define SDLOG_PATH "fs:/vol/external01/wiiu/pumpkinspice/log.txt"

void SdLogClear(void) {
    FILE *f = fopen(SDLOG_PATH, "w");
    if (f) {
        fclose(f);
    }
}

void SdLog(const char *fmt, ...) {
    FILE *f = fopen(SDLOG_PATH, "a");
    if (!f) {
        return;
    }
    fprintf(f, "[%8llu ms] ", (unsigned long long) OSTicksToMilliseconds(OSGetSystemTime()));
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fputc('\n', f);
    fclose(f);
}
#endif
