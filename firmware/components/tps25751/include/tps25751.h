/*
 * tps25751.h — TI TPS25751 USB Type-C / PD 3.1 controller at I2C 0x20.
 *
 * The TPS25751 is a standalone controller: it runs its own Type-C state
 * machine and PD policy engine and loads its policy configuration from an
 * external TPS26750 EEPROM over its own I2C port at power-up. The host never
 * drives PD itself; it reads status registers and asks for high-level actions
 * with 4CC tasks. Reference: TRM SLVUCR8A (doc/tps_application_note.pdf).
 *
 * Every number in the .c file is from that TRM. Five things the MicroPython
 * driver got wrong and this one must not repeat:
 *
 *  1. BOOT_FLAGS (0x2D) is NOT a version register. Byte 0 holds boot error
 *     flags. Parsing it as "major.minor" printed the dead-battery flag — the
 *     one bit that blocks all charging — as "fw 1.12" in every log line.
 *  2. Charging was switched with a 'Gaid' warm reset plus a PR_Swap. Gaid
 *     returns every host register to its default (TRM 4.1.1), wiping the very
 *     configuration just written, and the controller rejects every PR_Swap to
 *     Source while the dead-battery flag is set (TRM 4.4.1). Charging is
 *     switched through PORT_CONFIG instead.
 *  3. PORT_CONFIG bits[1:0] encode 0h Sink-only, 1h Source-only, 2h DRP,
 *     3h Disabled. Sink was coded as 2, which is DRP — and also the register's
 *     own reset default.
 *  4. A PORT_CONFIG that reads back all zeros means the IC never loaded its
 *     configuration. Writing those zeros back replaces the whole Application
 *     Customization block (OVP limits, VBUS thresholds, Type-C options). Any
 *     write to an all-zero register is refused here.
 *  5. A 4CC task that completes is not a task that succeeded. CMD_1 is cleared
 *     on completion including rejection; the outcome is the Standard Task
 *     Return Code in DATA_1 byte 0, which nothing used to read.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    char     mode[8];          /* "APP", "PTCH", ... (stripped) */
    bool     connected;        /* TYPE_C_STATUS byte0 bit0 */
    bool     plug_present;     /* STATUS bit0 */
    bool     contract;         /* ACTIVE_PDO[0..3] != 0 */
    int      power_profile_w;
    int      voltage_mv;
    int      current_ma;
    char     port_role[8];     /* STATUS bit5  -> "source"/"sink" */
    char     power_role[8];    /* PD_STATUS bit6 -> "source"/"sink"/"unknown" */
    char     data_role[4];     /* STATUS bit6  -> "UFP"/"DFP" */
    int      vbus;             /* STATUS bits[21:20] */
    bool     sourcing;         /* port_role == source AND vbus in {1,2} */
    bool     dead_battery;     /* BOOT_FLAGS byte0 bit2 */
    char     cfg_src[8];       /* "none"/"eeprom"/"i2c"/<n> */
    bool     fault;            /* a register read failed */
    int      revision;         /* BOOT_FLAGS byte4, silicon revision */
} tps_status_t;

/*
 * Three gates in order: reach APP mode, clear the dead-battery flag, then
 * touch the port configuration. Failing the first gate reboots the ESP32 a
 * bounded number of times per power-on (pd/appfail) — no host command can fix
 * a controller that never loaded its configuration, and the only lever left is
 * to let the load run again.
 *
 * Never blocks for more than about 4 s: network bring-up must not wait on the
 * PD controller.
 */
void tps_init(void);

bool tps_is_ready(void);

/* True while a port-role change is in flight (~1.5 s). Status callers should
 * skip register reads in that window rather than report the transient as a
 * fault; the API answers 409 on a second command. */
bool tps_is_busy(void);

esp_err_t tps_read_status(tps_status_t *out);

/* Source-only / Sink-only via PORT_CONFIG. The requested state is written to
 * NVS pd/charging BEFORE the register write: what has to survive a power cut
 * is what the operator asked for, not whether the IC accepted it. */
esp_err_t tps_enable_charging(void);
esp_err_t tps_disable_charging(void);

/* "ON" / "OFF" / "?" for the health line. One I2C read. */
const char *tps_charge_state(void);

/* "APP" / "PTCH" / "?" for the health line. */
const char *tps_mode_str(void);

/* Periodic register dump; only does I2C work when debug logging is on. */
void tps_start_periodic_task(void);
