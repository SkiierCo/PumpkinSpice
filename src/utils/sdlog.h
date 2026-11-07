#pragma once

// Diagnostic log on the SD card (sd:/wiiu/pumpkinspice/log.txt), built only with "make SDLOG=1".
// Every line is opened, written and closed on its own, so the file keeps everything up to a hang or crash.
#ifdef PS_SDLOG
#ifdef __cplusplus
extern "C" {
#endif
void SdLogClear(void);
void SdLog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#ifdef __cplusplus
}
#endif
#define SDLOG_CLEAR()   SdLogClear()
#define SDLOG(FMT, ...) SdLog(FMT, ##__VA_ARGS__)
#else
#define SDLOG_CLEAR() \
    do {              \
    } while (0)
#define SDLOG(FMT, ...) \
    do {                \
    } while (0)
#endif
