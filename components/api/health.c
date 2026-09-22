/*
 * health.c — the 30 s health sample.
 *
 * One line per tick, in its own ring buffer, in exactly the shape field
 * engineers already read:
 *
 *   health: link=UP ip=192.168.2.117 mac=3c:0f:02:d7:ea:c3 ping=0 chg=OFF \
 *           pd=APP heap=150704/44368 min=150704 largest=98304
 *
 * `largest` is new. Free heap alone says nothing about whether a 40 KB TLS
 * handshake can be allocated — a fragmented 150 KB with no contiguous 40 KB
 * block fails exactly the same way as having no memory at all, and that is the
 * failure mode this device has (no PSRAM, tight budget). The largest
 * contiguous block is the number that actually predicts it.
 */
#include "api.h"
#include "board_config.h"
#include "logx.h"
#include "eth.h"
#include "netmon.h"
#include "tps25751.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "supervisor.h"

static const char *TAG = "api";

static void health_task(void *arg)
{
    (void)arg;

    size_t heap_min = 0;
    bool   warned   = false;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(CFG_HEALTH_PERIOD_S * 1000));
        supervisor_beat("health", CFG_HEALTH_PERIOD_S * 3 * 1000);

        size_t freeb   = esp_get_free_heap_size();
        size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
        size_t total   = heap_caps_get_total_size(MALLOC_CAP_8BIT);
        size_t alloc   = (total > freeb) ? (total - freeb) : 0;

        /* Low-water mark since boot: a single free-heap figure says nothing
         * about a slow leak or creeping fragmentation. */
        if (heap_min == 0 || freeb < heap_min) {
            heap_min = freeb;
        }

        logx_record_health(
            "link=%s ip=%s mac=%s ping=%s chg=%s pd=%s heap=%u/%u min=%u largest=%u",
            eth_link_up() ? "UP" : "DOWN",
            eth_get_ip(), eth_get_mac(),
            netmon_ping_status(),
            tps_charge_state(),
            tps_mode_str(),
            (unsigned)freeb, (unsigned)alloc, (unsigned)heap_min,
            (unsigned)largest);

        /* Only an excursion reaches the main ring, and only once per
         * excursion — two thresholds so a heap hovering on the line cannot
         * ping-pong a warning into the ring on every tick. */
        if (freeb < CFG_HEAP_WARN_BYTES && !warned) {
            LOGW(TAG, "heap low: %u free / %u alloc (min %u, largest %u)",
                 (unsigned)freeb, (unsigned)alloc, (unsigned)heap_min,
                 (unsigned)largest);
            warned = true;
        } else if (warned && freeb > CFG_HEAP_CLEAR_BYTES) {
            LOGI(TAG, "heap recovered: %u free", (unsigned)freeb);
            warned = false;
        }
    }
}

void api_start_health_task(void)
{
    xTaskCreate(health_task, "health", 4096, NULL, 4, NULL);
}
