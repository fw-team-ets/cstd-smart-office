/*
 * netmon.h — link watchdog and iPad reachability watchdog.
 *
 * Two independent loops, both feeding the supervisor rather than resetting
 * anything themselves:
 *
 *   link monitor (1 s)   re-reads the interface, drives the LCD and the
 *                        maglock fail-safe, and asks for a reboot after
 *                        CFG_LINK_DOWN_RESET_S of continuous link loss —
 *                        bounded per power-on, unlike the Python build, where
 *                        a flapping link rebooted the device forever and the
 *                        relay dropped on every loop.
 *
 *   peer monitor (30 s)  pings the paired iPad. Four gates stand between a
 *                        missed reply and a reboot, because this reboots a
 *                        controller that also drives a door:
 *                          1. a request authenticated within idle_grace_s
 *                             skips the ping entirely;
 *                          2. our own link must be up, otherwise the fault is
 *                             on this side and the link monitor owns it;
 *                          3. fail_threshold consecutive misses are required;
 *                          4. reboots are capped per power-on — an iPad that
 *                             is switched off cannot cause a reboot loop.
 *                        Anything it cannot judge (not paired, no stored
 *                        address) means no ping and no reboot.
 */
#pragma once

#include <stdbool.h>

void netmon_start(void);

/*
 * Called by the API on every successfully authenticated request: an iPad that
 * is talking to us is alive by definition, which is the normal case, so the
 * network stays quiet.
 */
void netmon_note_peer_seen(void);

/*
 * What the health line reports as ping=:
 *   off   disabled at build time
 *   n/a   cannot judge: unpaired, no stored address, or our own link is down
 *   idle  the iPad called the API recently, so no ping was sent
 *   0     a ping went out and was answered
 *   1..N  consecutive missed replies; the reboot happens at fail_threshold
 */
const char *netmon_ping_status(void);

bool netmon_peer_online(void);
