/*
 * supervisor.h — the single owner of the watchdog and of every reboot.
 *
 * The Python build let unrelated tasks (the LCD monitor, the PD periodic dump)
 * feed the hardware watchdog on their own timers with no reference to whether
 * the HTTP server was still serving. A listener that died silently therefore
 * never caused a reset: the device stayed powered, pingable and useless. That
 * is the failure this component exists to prevent.
 *
 * The rule: exactly one task feeds the watchdog. Everything else either
 *   - reports a heartbeat, supervisor_beat(), or
 *   - asks for a reboot, supervisor_request_reboot().
 *
 * The supervisor withholds the feed when any registered heartbeat is overdue,
 * which lets the hardware watchdog do the reset — the most reliable path,
 * because it needs no further firmware to run correctly.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Slots are fixed so a heartbeat never allocates. */
#define SUPERVISOR_MAX_BEATS 8

typedef enum {
    REBOOT_REASON_NONE = 0,
    REBOOT_REASON_LINK_DOWN,
    REBOOT_REASON_PEER_UNREACHABLE,
    REBOOT_REASON_PD_NOT_APP,
    REBOOT_REASON_NO_IP,
    REBOOT_REASON_TASK_HUNG,
    REBOOT_REASON_API,
} reboot_reason_t;

/*
 * Reads esp_reset_reason() plus the reason the PREVIOUS run stored in
 * RTC_NOINIT memory, and writes one "boot cause: ..." line to the reset ring.
 * Must run before anything else touches RTC memory.
 */
void supervisor_boot_log(void);

/* True when this boot was a power-on. The per-power-on reboot budgets
 * (pd/appfail, wdt/peerfail, wdt/linkfail, wdt/bootfail) reset on it: cutting
 * the power is the operator saying "start counting again". */
bool supervisor_is_power_on_boot(void);

/* Human-readable cause of THIS boot, for /api/status. */
const char *supervisor_boot_cause(void);

/* Start the watchdog and the supervisor task. Called first in app_main(),
 * before anything that can hang (PD init, network bring-up). */
void supervisor_start(void);

/*
 * Register/refresh a liveness heartbeat. `name` must be a string literal (it
 * is stored by pointer). `max_age_ms` is that task's own idea of how long it
 * may legitimately go quiet — its poll interval plus margin.
 */
void supervisor_beat(const char *name, uint32_t max_age_ms);

/*
 * Ask for a reboot on the supervisor's next tick. The first request wins:
 * a task whose condition stays true calls this every iteration, and
 * overwriting the reason each time would only be noise.
 */
void supervisor_request_reboot(reboot_reason_t reason, const char *detail);

/*
 * Reboot now, recording `reason` in RTC_NOINIT so the next boot can log why.
 * Every reboot in this firmware goes through here — there are no bare
 * esp_restart() calls elsewhere.
 */
void supervisor_reboot_now(reboot_reason_t reason, const char *detail);

/*
 * Per-power-on reboot budget shared by every bounded reboot path.
 * Returns true if a reboot is still allowed and consumes one slot.
 * `nvs_ns`/`nvs_key` name the counter; `limit` is the cap.
 */
bool supervisor_take_reboot_quota(const char *nvs_ns, const char *nvs_key,
                                  int32_t limit);
void supervisor_clear_reboot_quota(const char *nvs_ns, const char *nvs_key);
