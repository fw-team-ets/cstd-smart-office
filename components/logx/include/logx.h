/*
 * logx.h — the firmware's log ring buffers.
 *
 * The line format is a contract, not a style choice: field engineers and the
 * log-fetching scripts both parse
 *
 *     [INFO ][2026-09-09T07:01:20+07:00] api: listening on https://0.0.0.0:8080
 *
 * Note the trailing space in "INFO " and "WARN " — the label column is five
 * characters wide so the timestamps line up.
 *
 * Three independent rings, exactly as in the Python build:
 *   main   (140 lines) every message at or above the configured level
 *   reset  (10 lines)  only "boot cause: ..." lines
 *   health (12 lines)  only the 30 s "health: ..." samples
 *
 * They are separate because health sampling runs forever on a fixed timer:
 * sharing one ring would evict every event line within hours, which is the
 * flood that made /logs useless on the Python build.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    LOGX_DEBUG = 10,
    LOGX_INFO  = 20,
    LOGX_WARN  = 30,
    LOGX_ERROR = 40,
} logx_level_t;

#define LOGX_LINE_MAX 160

void logx_init(logx_level_t level);
void logx_set_level(logx_level_t level);

/* True when a message at this level would actually be emitted. Call it before
 * building an expensive message (the TPS periodic dump does five I2C reads
 * purely to format debug text). */
bool logx_enabled(logx_level_t level);

void logx_log(logx_level_t level, const char *tag, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/* Boot-cause lines. Kept out of the main ring so routine logging cannot evict
 * them and so they do not consume main-ring budget. */
void logx_record_reset(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* One 30 s health sample. Own ring, fixed cost. */
void logx_record_health(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Copy up to `limit` most recent lines, oldest first, into `out`.
 * Returns how many were written. Pass limit = 0 for the whole ring. */
size_t logx_get_recent(char out[][LOGX_LINE_MAX], size_t limit);
size_t logx_get_resets(char out[][LOGX_LINE_MAX], size_t limit);
size_t logx_get_health(char out[][LOGX_LINE_MAX], size_t limit);

/* Wall-clock timestamp in the exact format used inside log lines. Exposed so
 * the API can stamp responses identically. */
void logx_timestamp(char *buf, size_t len);

#define LOGD(tag, ...) logx_log(LOGX_DEBUG, tag, __VA_ARGS__)
#define LOGI(tag, ...) logx_log(LOGX_INFO,  tag, __VA_ARGS__)
#define LOGW(tag, ...) logx_log(LOGX_WARN,  tag, __VA_ARGS__)
#define LOGE(tag, ...) logx_log(LOGX_ERROR, tag, __VA_ARGS__)
