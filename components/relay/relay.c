#include "relay.h"
#include "board_pins.h"
#include "board_config.h"
#include "logx.h"

#include "driver/gpio.h"

static const char *TAG = "relay";

static bool s_ready;
static bool s_failsafe;
/* The GPIO LEVEL last written, not the logical door state. It starts at 0
 * because that is what gpio_config() leaves the pin at, and it is deliberately
 * not initialised to MAGLOCK_LOCKED: if the cabinet turns out to be COM+NC and
 * the polarity constants are flipped, MAGLOCK_LOCKED becomes 1 while the pin is
 * still physically 0, and the de-duplication below would swallow the first
 * write. */
static int  s_maglock = 0;

void relay_init(void)
{
    gpio_config_t out = {
        .pin_bit_mask = 1ULL << BOARD_RELAY_MAGLOCK_GPIO,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&out) != ESP_OK) {
        LOGE(TAG, "init failed: maglock GPIO%d", BOARD_RELAY_MAGLOCK_GPIO);
        return;
    }

    gpio_set_level(BOARD_RELAY_MAGLOCK_GPIO, 0);
    s_maglock = 0;

    s_ready = true;
    LOGI(TAG, "init: maglock=GPIO%d (no manual-open button on this board)",
         BOARD_RELAY_MAGLOCK_GPIO);
}

bool relay_is_ready(void) { return s_ready; }

void relay_set_maglock(int value)
{
    if (!s_ready) {
        LOGE(TAG, "maglock set %d ignored — driver not initialised", value);
        return;
    }
    int v = value ? 1 : 0;

    /*
     * Repeats are dropped here rather than at the GPIO. Writing the same level
     * does not move the relay, but it does produce a log line, and the Python
     * build's /relays/maglock happily logged one per request. What this does
     * NOT do is rate-limit an ALTERNATING sequence (1,0,1,0) — each of those is
     * a real mechanical operation and STE test FW_FIRM_TC_049 covers it. Adding
     * coalescing there is open item 7 in the port specification; until it is
     * decided, the behaviour matches the original.
     */
    if (v == s_maglock) {
        return;
    }
    s_maglock = v;
    gpio_set_level(BOARD_RELAY_MAGLOCK_GPIO, v);
    LOGI(TAG, "maglock -> %d (%s)", v, v == MAGLOCK_OPEN ? "open" : "locked");
}

int relay_get_maglock(void) { return s_maglock; }

void relay_set_failsafe(bool active)
{
    if (s_failsafe == active) {
        return;
    }
    s_failsafe = active;
    LOGI(TAG, "failsafe %s", active ? "ENGAGED" : "released");
}

bool relay_failsafe_active(void) { return s_failsafe; }
