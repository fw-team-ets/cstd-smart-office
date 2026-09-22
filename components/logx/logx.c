#include "logx.h"
#include "board_config.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/*
 * All three rings are statically allocated. Deliberate: the device has no
 * PSRAM and roughly 200 KB of heap to run TLS in, so the log buffers must
 * never compete with a handshake for it. Total cost is fixed at
 * (140 + 10 + 12) x 160 bytes ~= 26 KB of .bss.
 */
static char s_main[CFG_LOG_RING_SIZE][LOGX_LINE_MAX];
static size_t s_main_len, s_main_pos;

static char s_reset[CFG_RESET_RING_SIZE][LOGX_LINE_MAX];
static size_t s_reset_len, s_reset_pos;

static char s_health[CFG_HEALTH_RING_SIZE][LOGX_LINE_MAX];
static size_t s_health_len, s_health_pos;

static logx_level_t s_level = LOGX_INFO;
static SemaphoreHandle_t s_lock;
static StaticSemaphore_t s_lock_buf;

static const char *level_label(logx_level_t l)
{
    switch (l) {
    case LOGX_DEBUG: return "DEBUG";
    case LOGX_INFO:  return "INFO ";   /* trailing space is part of the format */
    case LOGX_WARN:  return "WARN ";
    case LOGX_ERROR: return "ERROR";
    default:         return "?    ";
    }
}

void logx_timestamp(char *buf, size_t len)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    /*
     * Fixed +07:00, the same as the Python build. There is no RTC and SNTP is
     * an open question, so before the clock is ever set this renders as
     * 1970-01-01T07:00:00+07:00 — which is at least honestly wrong rather
     * than silently plausible.
     */
    time_t local = (time_t)tv.tv_sec + CFG_LOG_TZ_OFFSET_S;
    struct tm tm;
    gmtime_r(&local, &tm);

    snprintf(buf, len, "%04d-%02d-%02dT%02d:%02d:%02d+07:00",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
}

static void ring_push(char ring[][LOGX_LINE_MAX], size_t cap,
                      size_t *len, size_t *pos, const char *line)
{
    if (*len < cap) {
        strlcpy(ring[*len], line, LOGX_LINE_MAX);
        (*len)++;
    } else {
        strlcpy(ring[*pos], line, LOGX_LINE_MAX);
        *pos = (*pos + 1) % cap;
    }
}

static size_t ring_copy(char ring[][LOGX_LINE_MAX], size_t cap,
                        size_t len, size_t pos,
                        char out[][LOGX_LINE_MAX], size_t limit)
{
    (void)cap;
    if (len == 0) {
        return 0;
    }
    if (limit == 0 || limit > len) {
        limit = len;
    }
    /*
     * `pos` is the next slot to write, which is also the oldest entry once the
     * ring has wrapped; before it wraps `pos` is still 0 and entries sit in
     * order, so (pos + i) % len addresses the i-th oldest in both cases.
     * `first` skips forward to the most recent `limit` of them.
     */
    size_t first = len - limit;
    for (size_t i = 0; i < limit; i++) {
        strlcpy(out[i], ring[(pos + first + i) % len], LOGX_LINE_MAX);
    }
    return limit;
}

void logx_init(logx_level_t level)
{
    s_lock  = xSemaphoreCreateMutexStatic(&s_lock_buf);
    s_level = level;
}

void logx_set_level(logx_level_t level) { s_level = level; }

bool logx_enabled(logx_level_t level) { return level >= s_level; }

static void emit(char ring[][LOGX_LINE_MAX], size_t cap,
                 size_t *len, size_t *pos, const char *line)
{
    /* Printed outside the lock would interleave; printed inside it costs a
     * UART write while holding a mutex that only logging contends for. */
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    puts(line);
    ring_push(ring, cap, len, pos, line);
    if (s_lock) xSemaphoreGive(s_lock);
}

void logx_log(logx_level_t level, const char *tag, const char *fmt, ...)
{
    if (level < s_level) return;

    char ts[40];
    logx_timestamp(ts, sizeof(ts));

    char msg[LOGX_LINE_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    char line[LOGX_LINE_MAX];
    snprintf(line, sizeof(line), "[%s][%s] %s: %s",
             level_label(level), ts, tag, msg);

    emit(s_main, CFG_LOG_RING_SIZE, &s_main_len, &s_main_pos, line);
}

void logx_record_reset(const char *fmt, ...)
{
    char ts[40];
    logx_timestamp(ts, sizeof(ts));

    char msg[LOGX_LINE_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    char line[LOGX_LINE_MAX];
    snprintf(line, sizeof(line), "[%s][%s] boot: %s",
             level_label(LOGX_INFO), ts, msg);

    emit(s_reset, CFG_RESET_RING_SIZE, &s_reset_len, &s_reset_pos, line);
}

void logx_record_health(const char *fmt, ...)
{
    char ts[40];
    logx_timestamp(ts, sizeof(ts));

    char msg[LOGX_LINE_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    char line[LOGX_LINE_MAX];
    snprintf(line, sizeof(line), "[%s][%s] health: %s",
             level_label(LOGX_INFO), ts, msg);

    emit(s_health, CFG_HEALTH_RING_SIZE, &s_health_len, &s_health_pos, line);
}

size_t logx_get_recent(char out[][LOGX_LINE_MAX], size_t limit)
{
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t n = ring_copy(s_main, CFG_LOG_RING_SIZE, s_main_len, s_main_pos, out, limit);
    if (s_lock) xSemaphoreGive(s_lock);
    return n;
}

size_t logx_get_resets(char out[][LOGX_LINE_MAX], size_t limit)
{
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t n = ring_copy(s_reset, CFG_RESET_RING_SIZE, s_reset_len, s_reset_pos, out, limit);
    if (s_lock) xSemaphoreGive(s_lock);
    return n;
}

size_t logx_get_health(char out[][LOGX_LINE_MAX], size_t limit)
{
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t n = ring_copy(s_health, CFG_HEALTH_RING_SIZE, s_health_len, s_health_pos, out, limit);
    if (s_lock) xSemaphoreGive(s_lock);
    return n;
}
