#include "netmon.h"
#include "board_config.h"
#include "eth.h"
#include "relay.h"
#include "display.h"
#include "pairing.h"
#include "supervisor.h"
#include "storage.h"
#include "logx.h"

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "ping/ping_sock.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"

static const char *TAG = "netmon";

static char    s_ping_status[8] = "n/a";
static bool    s_peer_online;
static int64_t s_peer_seen_us;     /* 0 = never */

void netmon_note_peer_seen(void)
{
    s_peer_seen_us = esp_timer_get_time();
}

const char *netmon_ping_status(void) { return s_ping_status; }
bool netmon_peer_online(void)        { return s_peer_online; }

/* ── Link monitor ────────────────────────────────────────────────────────── */

static void link_task(void *arg)
{
    (void)arg;

    /* -1 until the first successful sample, so that sample counts as a real
     * transition and asserts the correct door state instead of assuming one. */
    int  last_up    = -1;
    int  down_ticks = 0;
    int  link_fails = 0;   /* consecutive down samples since the last PHY reset */

    LOGI(TAG, "link monitor started (reset after %ds of link loss, max %d reboots)",
         CFG_LINK_DOWN_RESET_S, CFG_LINK_DOWN_MAX_RESETS);

    if (supervisor_is_power_on_boot()) {
        supervisor_clear_reboot_quota(NVS_NS_WDT, "linkfail");
    }

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(CFG_LINK_POLL_MS));
        supervisor_beat("link", 10000);

        bool up = eth_refresh();

        if (up) {
            down_ticks = 0;
            link_fails = 0;
        } else {
            down_ticks++;
            link_fails++;

            /*
             * Softer recovery first. A PHY that is merely wedged comes back
             * from a reset; only a link that survives that is worth a reboot,
             * which drops the relay and changes the door's state.
             */
            if (link_fails == CFG_LINK_DOWN_RESET_S / 2) {
                eth_reset_phy();
            }

            if (CFG_LINK_DOWN_RESET_S &&
                down_ticks * (CFG_LINK_POLL_MS / 1000) >= CFG_LINK_DOWN_RESET_S) {
                char detail[64];
                snprintf(detail, sizeof(detail), "ETH link down %ds",
                         down_ticks * (CFG_LINK_POLL_MS / 1000));
                if (supervisor_take_reboot_quota(NVS_NS_WDT, "linkfail",
                                                 CFG_LINK_DOWN_MAX_RESETS)) {
                    supervisor_request_reboot(REBOOT_REASON_LINK_DOWN, detail);
                } else if (down_ticks % 60 == 0) {
                    /* Budget spent: stay up and keep saying so, once a minute,
                     * instead of rebooting into the same dead network. */
                    LOGE(TAG, "%s — reboot budget spent, staying up", detail);
                }
            }
        }

        if (up == (last_up == 1)) {
            continue;   /* no transition */
        }
        bool first = (last_up < 0);
        last_up = up ? 1 : 0;

        if (up) {
            LOGI(TAG, "link %s ip=%s mac=%s",
                 first ? "UP" : "RESTORED", eth_get_ip(), eth_get_mac());
            display_idle_network(eth_get_ip(), eth_get_mac());
            relay_set_failsafe(false);
            supervisor_clear_reboot_quota(NVS_NS_WDT, "linkfail");
            supervisor_clear_reboot_quota(NVS_NS_WDT, "bootfail");
            if (pairing_is_paired()) {
                relay_set_maglock(MAGLOCK_LOCKED);
            }
        } else {
            display_no_network();
#if CFG_LINK_DOWN_OPEN_MAGLOCK
            LOGE(TAG, "link DOWN — opening maglock (fail-safe)");
            relay_set_failsafe(true);
            relay_set_maglock(MAGLOCK_OPEN);
#else
            /* Default. Enabling the fail-safe assumes COM+NO wiring that has
             * not been measured; with COM+NC it would LOCK the door at the
             * exact moment the controller stops being reachable. */
            LOGE(TAG, "link DOWN (maglock fail-safe disabled — polarity unconfirmed)");
#endif
        }
    }
}

/* ── Boot-time "no IP yet" watchdog ──────────────────────────────────────── */

/*
 * Separate from the link monitor because the failure is different: the cable
 * may be fine and the DHCP server simply not up yet. After a building power
 * cut the switches take five to ten minutes, so this window is generous and,
 * unlike the Python build's 90 s unbounded loop, it does not block anything
 * while it waits — the server and every task are already running.
 */
static void boot_ip_task(void *arg)
{
    (void)arg;

    if (supervisor_is_power_on_boot()) {
        supervisor_clear_reboot_quota(NVS_NS_WDT, "bootfail");
    }

    int waited = 0;
    while (waited < CFG_BOOT_NO_IP_REBOOT_S) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        waited += 5;
        if (eth_link_up()) {
            LOGI(TAG, "network ready: ip=%s mac=%s", eth_get_ip(), eth_get_mac());
            vTaskDelete(NULL);
        }
        if (waited % 60 == 0) {
            LOGW(TAG, "still no IP after %ds — server is up and waiting", waited);
        }
    }

    char detail[64];
    snprintf(detail, sizeof(detail), "no IP after %ds", CFG_BOOT_NO_IP_REBOOT_S);
    if (supervisor_take_reboot_quota(NVS_NS_WDT, "bootfail",
                                     CFG_BOOT_NO_IP_MAX_RESETS)) {
        supervisor_request_reboot(REBOOT_REASON_NO_IP, detail);
    } else {
        LOGE(TAG, "%s — reboot budget spent, staying up", detail);
    }
    vTaskDelete(NULL);
}

/* ── ICMP ping ───────────────────────────────────────────────────────────── */

/*
 * The paired iPad is a client: it listens on nothing, so an echo request is
 * the only way to ask "are you still on the network".
 */
typedef struct {
    SemaphoreHandle_t done;
    bool              replied;
} ping_ctx_t;

static void ping_success_cb(esp_ping_handle_t hdl, void *args)
{
    (void)hdl;
    ping_ctx_t *ctx = (ping_ctx_t *)args;
    ctx->replied = true;
}

static void ping_end_cb(esp_ping_handle_t hdl, void *args)
{
    (void)hdl;
    ping_ctx_t *ctx = (ping_ctx_t *)args;
    xSemaphoreGive(ctx->done);
}

static bool ping_once(const char *host, int timeout_ms)
{
    ip_addr_t target;
    memset(&target, 0, sizeof(target));
    ip4_addr_t v4;
    if (!ip4addr_aton(host, &v4)) {
        return false;
    }
    ip_addr_set_ip4_u32(&target, v4.addr);

    ping_ctx_t ctx = { .done = xSemaphoreCreateBinary(), .replied = false };
    if (!ctx.done) {
        return false;
    }

    esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
    cfg.target_addr = target;
    cfg.count       = 1;
    cfg.timeout_ms  = timeout_ms;
    cfg.data_size   = 8;

    esp_ping_callbacks_t cbs = {
        .on_ping_success = ping_success_cb,
        .on_ping_timeout = NULL,
        .on_ping_end     = ping_end_cb,
        .cb_args         = &ctx,
    };

    esp_ping_handle_t hdl;
    if (esp_ping_new_session(&cfg, &cbs, &hdl) != ESP_OK) {
        vSemaphoreDelete(ctx.done);
        /* A watchdog that reboots a door controller must never fire because a
         * socket could not be created. Treated as "cannot judge" by the
         * caller, not as "the peer is gone". */
        LOGW(TAG, "ping session create failed — treating peer as unjudgeable");
        return false;
    }

    esp_ping_start(hdl);
    xSemaphoreTake(ctx.done, pdMS_TO_TICKS(timeout_ms + 1000));
    esp_ping_stop(hdl);
    esp_ping_delete_session(hdl);
    vSemaphoreDelete(ctx.done);

    return ctx.replied;
}

/* ── Peer monitor ────────────────────────────────────────────────────────── */

static void peer_task(void *arg)
{
    (void)arg;

#if !CFG_PEER_ENABLED
    strlcpy(s_ping_status, "off", sizeof(s_ping_status));
    LOGI(TAG, "peer monitor disabled");
    vTaskDelete(NULL);
    return;
#else
    LOGI(TAG, "peer monitor started (every %ds, retry %ds, reboot after %d misses, "
              "max %d reboots)",
         CFG_PEER_INTERVAL_S, CFG_PEER_RETRY_S, CFG_PEER_FAIL_THRESHOLD,
         CFG_PEER_MAX_RESETS);

    if (supervisor_is_power_on_boot()) {
        supervisor_clear_reboot_quota(NVS_NS_WDT, "peerfail");
    }

    /* Latched once the reboot budget is spent, so the "giving up" line is
     * logged on the transition and not once per threshold cycle forever. */
    bool gave_up = false;

    const uint32_t beat_max_age_ms =
        (CFG_PEER_INTERVAL_S + CFG_PEER_FAIL_THRESHOLD * CFG_PEER_RETRY_S + 30) * 1000;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(CFG_PEER_INTERVAL_S * 1000));
        supervisor_beat("peer", beat_max_age_ms);

        char peer[46];
        if (!pairing_is_paired() || !pairing_get_peer_ip(peer, sizeof(peer))) {
            strlcpy(s_ping_status, "n/a", sizeof(s_ping_status));
            continue;
        }

        /* Gate 1: the iPad demonstrably spoke to us recently. */
        if (s_peer_seen_us != 0 &&
            (esp_timer_get_time() - s_peer_seen_us) <
                (int64_t)CFG_PEER_IDLE_GRACE_S * 1000000) {
            strlcpy(s_ping_status, "idle", sizeof(s_ping_status));
            s_peer_online = true;
            continue;
        }

        /* Gate 2: our own link. A fault on this side is the link monitor's. */
        if (!eth_link_up()) {
            strlcpy(s_ping_status, "n/a", sizeof(s_ping_status));
            continue;
        }

        if (ping_once(peer, CFG_PEER_TIMEOUT_MS)) {
            strlcpy(s_ping_status, "0", sizeof(s_ping_status));
            s_peer_online = true;
            gave_up = false;
            /* A healthy peer earns the full reboot budget back. */
            supervisor_clear_reboot_quota(NVS_NS_WDT, "peerfail");
            continue;
        }

        /* Gate 3: consecutive misses. The first miss opens a fast retry burst
         * so a peer that is genuinely gone is confirmed in seconds, while a
         * single dropped echo costs nothing. */
        int fails = 1;
        snprintf(s_ping_status, sizeof(s_ping_status), "%d", fails);
        if (!gave_up) {
            LOGW(TAG, "no reply from %s (1/%d)", peer, CFG_PEER_FAIL_THRESHOLD);
        }

        while (fails < CFG_PEER_FAIL_THRESHOLD) {
            vTaskDelay(pdMS_TO_TICKS(CFG_PEER_RETRY_S * 1000));
            supervisor_beat("peer", beat_max_age_ms);
            if (ping_once(peer, CFG_PEER_TIMEOUT_MS)) {
                break;
            }
            fails++;
            snprintf(s_ping_status, sizeof(s_ping_status), "%d", fails);
            if (!gave_up) {
                LOGW(TAG, "no reply from %s (%d/%d)",
                     peer, fails, CFG_PEER_FAIL_THRESHOLD);
            }
        }

        if (fails < CFG_PEER_FAIL_THRESHOLD) {
            LOGI(TAG, "%s answered again after %d miss(es)", peer, fails);
            strlcpy(s_ping_status, "0", sizeof(s_ping_status));
            s_peer_online = true;
            gave_up = false;
            supervisor_clear_reboot_quota(NVS_NS_WDT, "peerfail");
            continue;
        }

        s_peer_online = false;
        display_ipad_offline();

        /* Gate 4: the per-power-on reboot cap. */
        if (!supervisor_take_reboot_quota(NVS_NS_WDT, "peerfail",
                                          CFG_PEER_MAX_RESETS)) {
            if (!gave_up) {
                LOGE(TAG, "%s unreachable and reboot limit (%d) reached — "
                          "staying up, no further reboots", peer, CFG_PEER_MAX_RESETS);
                gave_up = true;
            }
            /* Keep pinging: the point is to notice when the iPad comes back. */
            continue;
        }

        char detail[64];
        snprintf(detail, sizeof(detail), "%s missed %d probes", peer, fails);
        supervisor_request_reboot(REBOOT_REASON_PEER_UNREACHABLE, detail);
    }
#endif
}

void netmon_start(void)
{
    xTaskCreate(link_task,    "netmon_link", 4096, NULL, 6, NULL);
    xTaskCreate(peer_task,    "netmon_peer", 4096, NULL, 5, NULL);
    xTaskCreate(boot_ip_task, "netmon_boot", 3072, NULL, 4, NULL);
}
