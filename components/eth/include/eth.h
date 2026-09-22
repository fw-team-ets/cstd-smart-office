/*
 * eth.h — W5500 Ethernet, the only network interface in this firmware.
 *
 * Wi-Fi is gone entirely (spec v0.1, requirement 2). Nothing here ever blocks
 * the caller waiting for a link: the Python build's main.py sat in a retry
 * loop before the watchdog was even started, so a device that came up while
 * the building switches were still booting — five to ten minutes after a power
 * cut — never reached the point of starting its server. Here the interface is
 * brought up asynchronously and the rest of the firmware starts regardless;
 * netmon owns recovery.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "esp_eth.h"

esp_err_t eth_init(void);

/*
 * Live link state, read from the driver and the netif rather than a cached
 * address. True only when the PHY reports a link AND a non-zero IP is held —
 * an interface we cannot route through is down, whatever the PHY says. Any
 * failure to query counts as down.
 */
bool eth_link_up(void);

/* Re-read the interface and refresh the cached address; logs on change.
 * Returns the same answer as eth_link_up(). Called on netmon's 1 s tick. */
bool eth_refresh(void);

const char *eth_get_ip(void);    /* "0.0.0.0" when there is none */
const char *eth_get_mac(void);   /* lowercase aa:bb:cc:dd:ee:ff, never fails */
const char *eth_get_hostname(void);

/* Hard-reset the W5500 and re-run bring-up. netmon calls this after repeated
 * link failures, before escalating to a reboot. */
void eth_reset_phy(void);
