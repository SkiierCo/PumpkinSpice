#pragma once

// Diagnostic log on the SD card (sd:/wiiu/pumpkinspice/log.txt), built only with "make SDLOG=1".
// Every line is opened, written and closed on its own, so the file keeps everything up to a hang or crash.
// At each boot the last boot's log moves to log-previous.txt and older logs are deleted.
#ifdef PS_SDLOG
#ifdef __cplusplus
extern "C" {
#endif
void SdLogNewBoot(void);
void SdLog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#ifdef __cplusplus
}
#endif
#define SDLOG_NEW_BOOT() SdLogNewBoot()
#define SDLOG(FMT, ...)  SdLog(FMT, ##__VA_ARGS__)
#else
#define SDLOG_NEW_BOOT() \
    do {                 \
    } while (0)
#define SDLOG(FMT, ...) \
    do {                \
    } while (0)
#endif
