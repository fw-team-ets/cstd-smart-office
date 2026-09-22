/*
 * board_config.h — tunables that are policy, not hardware.
 *
 * Values mirror config/config.py of the MicroPython v1.1.9 firmware unless a
 * comment says otherwise, so that a behaviour difference between the two
 * builds is always traceable to a line here.
 */
#pragma once

#define FW_VERSION              "2.0.0"
#define FW_COMPANY              "ETeams"
#define FW_HARDWARE             "ESP32-S3"
#define FW_BIN_NAME             "ste_fw_c-2.0.0.bin"

/* ── Logging ─────────────────────────────────────────────────────────────── */
#define CFG_LOG_RING_SIZE       140   /* main ring   */
#define CFG_RESET_RING_SIZE     10    /* boot-cause ring */
#define CFG_HEALTH_RING_SIZE    12    /* health-sample ring: ~6 min at 30 s */
#define CFG_LOG_TZ_OFFSET_S     (7 * 3600)   /* +07:00, as in the Python build */

/* ── Network ─────────────────────────────────────────────────────────────── */
#define CFG_NET_USE_DHCP_DEFAULT      1
#define CFG_NET_STATIC_IP_DEFAULT     "192.168.1.100"
#define CFG_NET_STATIC_NETMASK_DEF    "255.255.255.0"
#define CFG_NET_STATIC_GW_DEFAULT     "192.168.1.1"
#define CFG_NET_STATIC_DNS_DEFAULT    "8.8.8.8"
#define CFG_NET_MDNS_PREFIX           "ste-"   /* ste-<last 6 MAC hex>.local */

/* ── HTTPS server ────────────────────────────────────────────────────────── */
#define CFG_SERVER_PORT         8080
/*
 * Every TLS handshake costs roughly 40 KB of heap and this part has no PSRAM.
 * Three sockets is the ceiling the spec sets; going over it does not fail
 * gracefully, it fails during a handshake with the listener already committed.
 */
#define CFG_SERVER_MAX_SOCKETS  3

/* ── Pairing ─────────────────────────────────────────────────────────────── */
#define CFG_PAIR_OTP_TTL_S      120   /* spec v0.1 section 4.4 (was 300 s in the
                                       * RSA-based Python flow) */
#define CFG_PAIR_OTP_MAX_TRIES  5     /* wrong OTPs before the session is burnt */
#define CFG_PAIR_TOKEN_BYTES    32    /* 256-bit bearer token */

/* ── Watchdog / supervisor ───────────────────────────────────────────────── */
#define CFG_WDT_TIMEOUT_MS      30000 /* hardware/task WDT window */
#define CFG_WDT_FEED_PERIOD_MS  5000  /* supervisor tick */

/* ── Link watchdog ───────────────────────────────────────────────────────── */
#define CFG_LINK_POLL_MS            1000
#define CFG_LINK_DOWN_RESET_S       10
/*
 * Bounded, unlike the Python build. A flapping link there rebooted the device
 * every ~10 s forever, and every reboot drops the relay — the door physically
 * changed state on each loop. After this many link-down reboots per power-on
 * the device stays up and only logs; a power cycle grants a fresh budget.
 */
#define CFG_LINK_DOWN_MAX_RESETS    3

/*
 * Boot-time "still no IP" reboot. The Python main.py looped here forever before
 * the watchdog was even started; here the server and every task come up anyway
 * and this is only a last resort, so it is both bounded and generous enough to
 * cover the 5-10 minute switch bring-up after a building power cut.
 */
#define CFG_BOOT_NO_IP_REBOOT_S     600
#define CFG_BOOT_NO_IP_MAX_RESETS   3

/* ── iPad reachability watchdog ──────────────────────────────────────────── */
#define CFG_PEER_ENABLED        1
#define CFG_PEER_INTERVAL_S     30
#define CFG_PEER_RETRY_S        2     /* config said 2, the Python fallback said
                                       * 5; 2 is the documented value and wins */
#define CFG_PEER_TIMEOUT_MS     1000
#define CFG_PEER_FAIL_THRESHOLD 3
#define CFG_PEER_MAX_RESETS     2     /* per power-on */
#define CFG_PEER_IDLE_GRACE_S   30    /* a recent authenticated request proves
                                       * the iPad is alive: skip the ping */

/* ── Maglock policy ──────────────────────────────────────────────────────── */
/*
 * Assumes COM + NO wiring: value 1 energises the relay coil and OPENS the door,
 * de-energised (including a board power cut) LOCKS it. This has NOT been
 * measured on the real cabinet — open item 1 in the port specification.
 * Everything below is expressed in terms of these two constants so that
 * flipping the polarity is a two-line change, not an audit of the whole tree.
 */
#define MAGLOCK_OPEN            1
#define MAGLOCK_LOCKED          0

/*
 * Boot policy, matching MicroPython main.py: the door is opened as early as
 * possible and only locked once the device is known to be paired. An unpaired
 * device is a device nobody can command, so leaving it locked would trap
 * whoever is installing it.
 */
#define CFG_MAGLOCK_BOOT_STATE  MAGLOCK_OPEN

/*
 * Fail-safe: open the door while the Ethernet link is down. Default off,
 * exactly as in the Python build, because with COM+NC wiring this would LOCK
 * the door at the moment the controller becomes unreachable — the one
 * direction that traps people. Turn on only after the polarity is measured.
 */
#define CFG_LINK_DOWN_OPEN_MAGLOCK  0

/*
 * No manual-open button constants: there is no button. See board_pins.h for
 * why (GPIO44 is UART0 on PCBA Rev 1.1) and Checklist.txt for the egress
 * consequence.
 */

/* ── PD controller ───────────────────────────────────────────────────────── */
#define CFG_PD_APP_MODE_MAX_RESETS 3
#define CFG_PD_CLEAR_DEAD_BATTERY  1
#define CFG_PD_PERIODIC_S          5

/* ── Health sampling ─────────────────────────────────────────────────────── */
#define CFG_HEALTH_PERIOD_S     30
#define CFG_HEAP_WARN_BYTES     60000
#define CFG_HEAP_CLEAR_BYTES    75000   /* hysteresis: one warning per excursion */
