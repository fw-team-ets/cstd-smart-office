/*
 * app_main.c — boot sequence and nothing else.
 *
 * The order matters, and it is not the order the MicroPython build used.
 * Three changes, each closing a specific field failure:
 *
 *  1. The supervisor and its watchdog start FIRST, before anything that can
 *     hang. The Python build started the watchdog only after config load,
 *     relay init, display init, PD init and an unbounded network wait — so a
 *     hang in any of those had nothing to rescue it.
 *
 *  2. Nothing blocks waiting for the network. The building loses power, the
 *     switches take five to ten minutes to come back, the board boots in
 *     seconds: the Python build sat in a retry loop and rebooted, repeatedly,
 *     without ever starting its server. Here the interface is started
 *     asynchronously and every task comes up regardless; netmon owns recovery
 *     and the (bounded) last-resort reboot.
 *
 *  3. PD init runs before the network but is hard-capped at a few seconds, so
 *     an unconfigured TPS25751 delays nothing else.
 *
 * The relay is initialised as early as it can be, because the door's state
 * during boot is a physical fact somebody may be standing in front of.
 */
#include "board_config.h"
#include "board_pins.h"
#include "logx.h"
#include "storage.h"
#include "supervisor.h"
#include "i2cbus.h"
#include "relay.h"
#include "display.h"
#include "tps25751.h"
#include "eth.h"
#include "netmon.h"
#include "pairing.h"
#include "api.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"

static const char *TAG = "main";

/*
 * Pins this firmware deliberately never configures, so that their reset state
 * (input, no pull) is what the hardware sees:
 *
 *   GPIO 5, 6, 16, 17, 18   RS485 — no driver, none planned. Configuring them
 *                           would only create a way to key a transceiver by
 *                           accident.
 *   GPIO 15                 PD_INT — the TPS25751 is polled, not interrupt
 *                           driven.
 *   GPIO 38                 TEST_BTN, GPIO 41/42 DIN_01/DIN_02, GPIO 33
 *                           EN_PWR_PP5V, GPIO 39 RELAY_02 — all out of scope.
 *   GPIO 43, 44             UART0, left to the console.
 *   GPIO 36                 must never be touched at all — see board_pins.h.
 */

static void pairing_tick_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        pairing_tick();   /* expires an unfinished pairing session */
    }
}

void app_main(void)
{
    logx_init(LOGX_INFO);

    /* Before anything else touches RTC memory: this reads why the previous
     * run ended, which is the only evidence that survives a field reboot. */
    supervisor_boot_log();
    LOGI(TAG, "ETeams STE firmware %s starting", FW_VERSION);

    /* Watchdog up first. Everything below this line is now covered. */
    supervisor_start();

    storage_init();
    storage_migrate_from_python();
    pairing_load();

    /* The door, as early as possible. Boot policy is a named constant in
     * board_config.h, not a literal here, because the relay polarity is still
     * unconfirmed on the real cabinet. */
    relay_init();
    relay_set_maglock(CFG_MAGLOCK_BOOT_STATE);
    LOGI(TAG, "maglock: %s at boot",
         CFG_MAGLOCK_BOOT_STATE == MAGLOCK_OPEN ? "open" : "locked");

    if (i2cbus_init() == ESP_OK) {
        display_init();
        display_connecting();
        /* Bounded internally: never more than about 4 s, so the network is
         * not waiting on the PD controller. */
        tps_init();
    } else {
        LOGE(TAG, "I2C bus unavailable — display and PD disabled");
    }

    /* Non-blocking. Link and address arrive later through the event loop. */
    if (eth_init() != ESP_OK) {
        LOGE(TAG, "ethernet init failed — netmon will keep retrying");
    }

    /*
     * The server starts now, with no address in hand. It binds INADDR_ANY, so
     * it is already listening when DHCP finally answers — and it keeps
     * listening across a lease change, which the Python build did not.
     */
    if (api_start() != ESP_OK) {
        /* Without the API the device cannot be commanded at all, and a reboot
         * is more useful than sitting there unreachable. */
        supervisor_request_reboot(REBOOT_REASON_API, "https server failed to start");
    }

    display_start_task();
    tps_start_periodic_task();
    netmon_start();
    api_start_health_task();
    xTaskCreate(pairing_tick_task, "pair_tick", 2560, NULL, 3, NULL);

    /* Lock the door once we know the device is under someone's control. An
     * unpaired device is left open: nobody can command it, so locking would
     * only trap whoever is installing it. */
    if (pairing_is_paired()) {
        relay_set_maglock(MAGLOCK_LOCKED);
        LOGI(TAG, "maglock: locked (device is paired)");
    } else {
        LOGI(TAG, "maglock: open — awaiting pairing");
    }

    LOGI(TAG, "boot complete: %s", supervisor_boot_cause());
    /* app_main returns; every subsystem now lives in its own task. */
}
