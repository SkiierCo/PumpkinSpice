#include "sdlog.h"

#ifdef PS_SDLOG
#include <coreinit/time.h>
#include <stdarg.h>
#include <stdio.h>

#define SDLOG_PATH          "fs:/vol/external01/wiiu/pumpkinspice/log.txt"
#define SDLOG_PREVIOUS_PATH "fs:/vol/external01/wiiu/pumpkinspice/log-previous.txt"

void SdLogNewBoot(void) {
    // log.txt becomes log-previous.txt (the boot before, e.g. the one that crashed); older logs are deleted
    remove(SDLOG_PREVIOUS_PATH);
    rename(SDLOG_PATH, SDLOG_PREVIOUS_PATH);
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
