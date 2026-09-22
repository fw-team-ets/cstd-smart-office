#include "supervisor.h"
#include "board_config.h"
#include "logx.h"
#include "storage.h"

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "esp_attr.h"

static const char *TAG = "wdt";

/*
 * Survives every reset except a power cut, which is exactly what is needed:
 * the Python build could not tell a panic from a planned reset (MicroPython
 * folds ESP_RST_PANIC and ESP_RST_SW into one cause) and kept no record of
 * WHY it rebooted, so after a field reboot there was no evidence left at all.
 */
#define RTC_MAGIC 0x53544501u   /* 'STE' + format version 1 */

RTC_NOINIT_ATTR static uint32_t s_rtc_magic;
RTC_NOINIT_ATTR static uint32_t s_rtc_reason;
RTC_NOINIT_ATTR static char     s_rtc_detail[64];

static char s_boot_cause[128] = "unknown";
static bool s_power_on;

typedef struct {
    const char *name;
    int64_t     last_us;
    uint32_t    max_age_ms;
} beat_t;

static beat_t   s_beats[SUPERVISOR_MAX_BEATS];
static size_t   s_beat_count;
static portMUX_TYPE s_beat_mux = portMUX_INITIALIZER_UNLOCKED;

static reboot_reason_t s_pending_reason = REBOOT_REASON_NONE;
static char            s_pending_detail[64];

static const char *reason_name(reboot_reason_t r)
{
    switch (r) {
    case REBOOT_REASON_LINK_DOWN:        return "link-down";
    case REBOOT_REASON_PEER_UNREACHABLE: return "peer-unreachable";
    case REBOOT_REASON_PD_NOT_APP:       return "pd-not-app";
    case REBOOT_REASON_NO_IP:            return "no-ip";
    case REBOOT_REASON_TASK_HUNG:        return "task-hung";
    case REBOOT_REASON_API:              return "api-request";
    default:                             return "none";
    }
}

void supervisor_boot_log(void)
{
    esp_reset_reason_t cause = esp_reset_reason();
    s_power_on = (cause == ESP_RST_POWERON);

    bool have_marker = (s_rtc_magic == RTC_MAGIC) &&
                       (s_rtc_reason != REBOOT_REASON_NONE);

    switch (cause) {
    case ESP_RST_POWERON:
        snprintf(s_boot_cause, sizeof(s_boot_cause), "power-on");
        break;
    case ESP_RST_SW:
        if (have_marker) {
            snprintf(s_boot_cause, sizeof(s_boot_cause),
                     "planned reset (%s: %s)",
                     reason_name((reboot_reason_t)s_rtc_reason), s_rtc_detail);
        } else {
            snprintf(s_boot_cause, sizeof(s_boot_cause), "planned reset");
        }
        break;
    case ESP_RST_PANIC:
        snprintf(s_boot_cause, sizeof(s_boot_cause),
                 "unexpected reset (crash/panic)");
        break;
    case ESP_RST_TASK_WDT:
    case ESP_RST_INT_WDT:
    case ESP_RST_WDT:
        /* The supervisor stopped feeding. If it recorded why before it stopped,
         * say so — that is the difference between "the device hung" and "a
         * named background task went quiet". */
        if (have_marker) {
            snprintf(s_boot_cause, sizeof(s_boot_cause),
                     "watchdog timeout (device hung: %s)", s_rtc_detail);
        } else {
            snprintf(s_boot_cause, sizeof(s_boot_cause),
                     "watchdog timeout (device hung)");
        }
        break;
    case ESP_RST_EXT:
        snprintf(s_boot_cause, sizeof(s_boot_cause), "external reset");
        break;
    case ESP_RST_BROWNOUT:
        snprintf(s_boot_cause, sizeof(s_boot_cause), "brownout");
        break;
    case ESP_RST_DEEPSLEEP:
        snprintf(s_boot_cause, sizeof(s_boot_cause), "deepsleep wake");
        break;
    default:
        snprintf(s_boot_cause, sizeof(s_boot_cause), "unknown (%d)", (int)cause);
        break;
    }

    logx_record_reset("boot cause: %s", s_boot_cause);

    /* Consume the marker so the next boot cannot inherit a stale reason. */
    s_rtc_magic  = RTC_MAGIC;
    s_rtc_reason = REBOOT_REASON_NONE;
    s_rtc_detail[0] = '\0';
}

bool supervisor_is_power_on_boot(void) { return s_power_on; }

const char *supervisor_boot_cause(void) { return s_boot_cause; }

void supervisor_beat(const char *name, uint32_t max_age_ms)
{
    int64_t now = esp_timer_get_time();

    portENTER_CRITICAL(&s_beat_mux);
    for (size_t i = 0; i < s_beat_count; i++) {
        if (s_beats[i].name == name) {
            s_beats[i].last_us    = now;
            s_beats[i].max_age_ms = max_age_ms;
            portEXIT_CRITICAL(&s_beat_mux);
            return;
        }
    }
    if (s_beat_count < SUPERVISOR_MAX_BEATS) {
        s_beats[s_beat_count].name       = name;
        s_beats[s_beat_count].last_us    = now;
        s_beats[s_beat_count].max_age_ms = max_age_ms;
        s_beat_count++;
    }
    portEXIT_CRITICAL(&s_beat_mux);
}

void supervisor_request_reboot(reboot_reason_t reason, const char *detail)
{
    /* First request wins. */
    if (s_pending_reason != REBOOT_REASON_NONE) {
        return;
    }
    s_pending_reason = reason;
    strlcpy(s_pending_detail, detail ? detail : "", sizeof(s_pending_detail));
    LOGE(TAG, "reboot requested: %s — %s", reason_name(reason), s_pending_detail);
}

void supervisor_reboot_now(reboot_reason_t reason, const char *detail)
{
    s_rtc_magic  = RTC_MAGIC;
    s_rtc_reason = (uint32_t)reason;
    strlcpy(s_rtc_detail, detail ? detail : "", sizeof(s_rtc_detail));

    LOGE(TAG, "rebooting: %s — %s", reason_name(reason), s_rtc_detail);

    /*
     * Every esp_restart() drops GPIO to high-Z, so the relay releases and the
     * door changes state while the device boots. That is a real operational
     * cost, which is why every caller of this function is behind a bounded
     * per-power-on budget.
     */
    vTaskDelay(pdMS_TO_TICKS(300));   /* let the log line reach the console */
    esp_restart();
}

bool supervisor_take_reboot_quota(const char *nvs_ns, const char *nvs_key,
                                  int32_t limit)
{
    if (limit <= 0) {
        return false;   /* 0 disables the reboot path entirely */
    }
    int32_t n = storage_get_i32(nvs_ns, nvs_key, 0) + 1;
    if (n > limit) {
        return false;
    }
    storage_set_i32(nvs_ns, nvs_key, n);
    return true;
}

void supervisor_clear_reboot_quota(const char *nvs_ns, const char *nvs_key)
{
    storage_set_i32(nvs_ns, nvs_key, 0);
}

static void supervisor_task(void *arg)
{
    (void)arg;

    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    LOGI(TAG, "supervisor started: timeout=%dms feed=%dms",
         CFG_WDT_TIMEOUT_MS, CFG_WDT_FEED_PERIOD_MS);

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(CFG_WDT_FEED_PERIOD_MS));

        /* 1. An explicit request beats everything: act immediately rather than
         *    waiting out the watchdog window on top of the caller's own
         *    bounded detection. */
        if (s_pending_reason != REBOOT_REASON_NONE) {
            supervisor_reboot_now(s_pending_reason, s_pending_detail);
            /* does not return */
        }

        /* 2. A background task that died or hung. FreeRTOS will not tell us,
         *    and neither did asyncio — a task that threw vanished silently.
         *    Withholding the feed here hands the reset to the hardware
         *    watchdog, which needs no further firmware to work. */
        int64_t now = esp_timer_get_time();
        const char *stale = NULL;
        portENTER_CRITICAL(&s_beat_mux);
        for (size_t i = 0; i < s_beat_count; i++) {
            int64_t age_ms = (now - s_beats[i].last_us) / 1000;
            if (age_ms > (int64_t)s_beats[i].max_age_ms) {
                stale = s_beats[i].name;
                break;
            }
        }
        portEXIT_CRITICAL(&s_beat_mux);

        if (stale) {
            LOGE(TAG, "task hung: %s — withholding feed, allowing WDT reset", stale);
            /* Record it now: after the watchdog fires there is no chance to. */
            s_rtc_magic  = RTC_MAGIC;
            s_rtc_reason = REBOOT_REASON_TASK_HUNG;
            strlcpy(s_rtc_detail, stale, sizeof(s_rtc_detail));
            continue;
        }

        esp_task_wdt_reset();
    }
}

void supervisor_start(void)
{
    esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms     = CFG_WDT_TIMEOUT_MS,
        .idle_core_mask = 0,      /* the idle tasks are not subscribed: only the
                                   * supervisor is, so that liveness means
                                   * "the supervisor is happy", nothing else */
        .trigger_panic  = true,
    };
    esp_err_t err = esp_task_wdt_init(&wdt_cfg);
    if (err == ESP_ERR_INVALID_STATE) {
        err = esp_task_wdt_reconfigure(&wdt_cfg);
    }
    if (err != ESP_OK) {
        LOGE(TAG, "task WDT init failed: %s", esp_err_to_name(err));
    }

    xTaskCreate(supervisor_task, "supervisor", 3072, NULL, 10, NULL);
}
