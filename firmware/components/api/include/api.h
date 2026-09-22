/*
 * api.h — the HTTPS API the webapp talks to.
 *
 * Contract (spec v0.1 section 5). Uniform envelope:
 *     success  {"ok": true,  "data": { ... }}
 *     failure  {"ok": false, "error": "<machine_readable>"}
 * with 400 bad request, 401 missing/invalid token, 409 state does not allow
 * it, 503 PD controller not ready.
 *
 *   GET  /api/info           public   {version, mac, ip, hostname, paired}
 *   POST /api/pair/start     unpaired {} -> {expires_in}   409 when paired
 *   POST /api/pair/confirm   unpaired {otp} -> {token}     409 when paired
 *   GET  /api/status         bearer   {link, ip, mac, charging, vbus, pd_ready,
 *                                      ipad_online, uptime_s, reset_reason, ...}
 *   POST /api/charge         bearer   {on: bool} -> {charging}
 *   POST /api/relay/maglock  bearer   {value: 0|1} -> {value}
 *   GET  /api/logs           bearer   {logs[], resets[], health[]}
 *   POST /api/unpair         bearer   {} -> {}
 *
 * Two things that are not negotiable and cost nothing to get right:
 *   - the listener binds INADDR_ANY, never the address captured at boot. The
 *     Python build bound the boot-time IP (regression 1cc5bef), so a DHCP
 *     renewal that moved the address left a listener nobody could reach —
 *     pingable, not connectable, which is exactly the field symptom that
 *     started this rewrite;
 *   - CORS preflight is answered, because the webapp runs from a different
 *     origin and Safari will not even attempt the real request otherwise.
 */
#pragma once

#include "esp_err.h"

esp_err_t api_start(void);

/* Starts the 30 s health sampler (the `health:` log line). */
void api_start_health_task(void);
